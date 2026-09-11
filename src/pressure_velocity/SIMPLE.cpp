#include "cfd/pressure_velocity/SIMPLE.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/GpuResidencyManager.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/turbulence/LaminarModel.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::calculateMassFlux;
using cfd::physics::evaluateContinuity;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;
using cfd::turbulence::LaminarModel;
using cfd::turbulence::TurbulenceModel;

namespace {

bool allFinite(const ScalarField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

bool allFinite(const SurfaceField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

bool allFinite(const VectorField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i].x) || !std::isfinite(field[i].y)) return false;
  }
  return true;
}

Real rms(const ScalarField& field) {
  Real sumSquares = 0.0;
  for (Index i = 0; i < field.size(); ++i) {
    sumSquares += field[i] * field[i];
  }
  return std::sqrt(sumSquares / static_cast<Real>(field.size()));
}

ScalarField toScalarField(const Vector& v) {
  ScalarField field(v.size());
  for (Index i = 0; i < v.size(); ++i) field[i] = v[i];
  return field;
}

ScalarField selectComponent(const VectorField& velocity, VelocityComponent component) {
  ScalarField field(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    field[i] = (component == VelocityComponent::U) ? velocity[i].x : velocity[i].y;
  }
  return field;
}

Vector toVector(const ScalarField& field) {
  Vector v(field.size());
  for (Index i = 0; i < field.size(); ++i) v[i] = field[i];
  return v;
}

VectorField combineComponents(const Vector& u, const Vector& v) {
  VectorField result(u.size());
  for (Index i = 0; i < u.size(); ++i) {
    result[i] = Vector2{u[i], v[i]};
  }
  return result;
}

}  // namespace

SIMPLE::SIMPLE(SIMPLESettings settings, Index referenceCell, TurbulenceModel* turbulenceModel,
               const ScalarField* temperature, const BoussinesqBuoyancy* buoyancy,
               SIMPLEProgressCallback progressCallback, SIMPLECancellationCheck cancellationCheck)
    : settings_(std::move(settings)),
      referenceCell_(referenceCell),
      turbulenceModel_(turbulenceModel),
      temperature_(temperature),
      buoyancy_(buoyancy),
      progressCallback_(std::move(progressCallback)),
      cancellationCheck_(std::move(cancellationCheck)) {}

const SIMPLESettings& SIMPLE::settings() const noexcept { return settings_; }
Index SIMPLE::referenceCell() const noexcept { return referenceCell_; }

SIMPLEResult SIMPLE::solve(const Mesh& mesh, const FluidProperties& fluid,
                           const BoundaryConditionSet& velocityBoundaries,
                           const BoundaryConditionSet& pressureBoundaries,
                           VectorField initialVelocity, ScalarField initialPressure) const {
  SIMPLEResult result;

  // Configuration problems are a legitimate solve()-time outcome a
  // caller should branch on via `status`, not an exception escaping
  // solve() -- consistent with every other failure category here. This
  // includes constructing the two linear solvers: LinearSolver's own
  // constructor validates its settings (e.g. maxIterations >= 1) and
  // throws InvalidArgumentError, which must be caught here rather than
  // left to propagate out of solve().
  //
  // P6-GPU-002 -- Performance: cfd::algebra::makeLinearSolver() (not a
  // direct BiCGSTAB construction any more) is what actually implements
  // "SIMPLE -> LinearSolver interface -> backend selection" --
  // settings_.momentumSolver/pressureSolver's own type/backend fields
  // (LinearSolverSettings, defaulting to BiCGSTAB+CPU -- unchanged
  // behavior for every pre-P6-GPU-002 caller) decide which concrete
  // solver comes back, including a deterministic, logged fallback to CPU
  // if GPU was requested but is unavailable (see that function's own
  // header comment). Held as a polymorphic cfd::algebra::LinearSolver
  // pointer rather than a concrete-type std::optional, since the
  // concrete type is now a runtime decision, not a compile-time one.
  bool configurationValid = true;
  std::unique_ptr<cfd::algebra::LinearSolver> momentumSolver;
  std::unique_ptr<cfd::algebra::LinearSolver> pressureSolver;
  try {
    validateSIMPLESettings(settings_);
    if (initialVelocity.size() != mesh.numberOfCells() ||
        initialPressure.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "SIMPLE::solve: initial field size does not match mesh cell count");
    }
    if (referenceCell_ >= mesh.numberOfCells()) {
      throw InvalidArgumentError("SIMPLE::solve: referenceCell out of range");
    }
    // P3-PHYS-001: temperature_/buoyancy_ must be both null or both
    // non-null (constructor-time invariant, re-checked here rather than
    // in the constructor -- same "solve()-time InvalidConfiguration, not
    // a throwing constructor" convention as referenceCell above), and
    // temperature_ (when present) must match this mesh's cell count.
    if ((temperature_ == nullptr) != (buoyancy_ == nullptr)) {
      throw InvalidArgumentError(
          "SIMPLE::solve: temperature and buoyancy must be both null or both non-null");
    }
    if (temperature_ != nullptr && temperature_->size() != mesh.numberOfCells()) {
      throw InvalidArgumentError("SIMPLE::solve: temperature size does not match mesh cell count");
    }
    momentumSolver = cfd::algebra::makeLinearSolver(settings_.momentumSolver);
    pressureSolver = cfd::algebra::makeLinearSolver(settings_.pressureSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = SIMPLEStatus::InvalidConfiguration;
    result.velocity = std::move(initialVelocity);
    result.pressure = std::move(initialPressure);
    result.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);
    return result;
  }

  VectorField velocity = std::move(initialVelocity);
  ScalarField pressure = std::move(initialPressure);
  SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  if (!allFinite(velocity) || !allFinite(pressure) || !allFinite(massFlux)) {
    result.status = SIMPLEStatus::NonFiniteState;
    result.velocity = std::move(velocity);
    result.pressure = std::move(pressure);
    result.massFlux = std::move(massFlux);
    return result;
  }

  // P2-TURB-003: `turbulenceModel_` is optional (defaults to null) --
  // when unset, this solve() call uses a local LaminarModel, scoped to
  // this call and this mesh, so mu_t = 0 everywhere and mu_eff reduces to
  // exactly fluid.dynamicViscosity() in every cell (see
  // LaminarModelTest.EffectiveViscosityEqualsMolecularViscosity). This
  // keeps the pre-P2-TURB-003 two-argument SIMPLE(settings, referenceCell)
  // construction path laminar, exactly as before.
  std::optional<LaminarModel> defaultTurbulenceModel;
  TurbulenceModel* activeModel = turbulenceModel_;
  if (activeModel == nullptr) {
    defaultTurbulenceModel.emplace(mesh);
    activeModel = &(*defaultTurbulenceModel);
  }

  SIMPLEStatus finalStatus = SIMPLEStatus::MaxIterations;

  // P6-GPU-001 -- Performance: scoped to exactly this solve() call --
  // "case initialization -> create persistent GPU resources -> ... ->
  // cleanup" (TODO.md's own target lifecycle) maps directly onto one
  // solve() call's outer-iteration loop, so RAII destruction at the end
  // of this function is the whole cleanup story; no separate lifecycle
  // hook is needed. Constructed unconditionally (cheap -- one
  // cudaGetDeviceCount() query when CUDA-enabled, a no-op struct
  // otherwise) but only ever *used* below when
  // settings_.enableGpuResidency is set, so a default-settings solve()
  // call issues zero CUDA calls, in either build configuration.
  cfd::gpu::GpuResidencyManager gpuResidency;

  for (Index iteration = 0; iteration < settings_.maxIterations; ++iteration) {
    // P5-B section 13: checked at the top of the outer loop only, before
    // this iteration touches velocity/pressure/massFlux at all -- so a
    // cancellation always leaves `result` (assembled below from the last
    // *completed* iteration's fields) a valid, non-corrupted state, never
    // a partially-updated one.
    if (cancellationCheck_ && cancellationCheck_()) {
      finalStatus = SIMPLEStatus::Cancelled;
      break;
    }

    const ScalarField previousU = selectComponent(velocity, VelocityComponent::U);
    const ScalarField previousV = selectComponent(velocity, VelocityComponent::V);

    std::optional<MomentumAssembly> uAssembly;
    std::optional<MomentumAssembly> vAssembly;
    std::optional<ScalarField> effectiveViscosity;
    try {
      // Correct the turbulence model against the current (previous-
      // iteration) velocity/pressure before assembling momentum with it
      // -- the same lagged-current-state convention this loop already
      // applies to boundary conditions like Outlet/Symmetry (see
      // MomentumEquation's boundaryVelocity()). For LaminarModel this is
      // a documented no-op; for a transport-equation model (e.g.
      // P2-TURB-004's KEpsilonModel) this can legitimately fail (a k/
      // epsilon linear solve that does not converge, or a non-finite
      // result) -- caught by the same try/catch as momentum assembly
      // below, not left to propagate out of solve() uncaught.
      activeModel->correct(mesh, velocity, pressure);
      effectiveViscosity = activeModel->effectiveViscosity(fluid.dynamicViscosity());
      // P3-PHYS-001: temperature_/buoyancy_ are passed through unchanged
      // every iteration (held fixed for the whole solve() call -- see
      // this class's own header comment on the deliberate one-way-
      // coupling scope); both null for every pre-existing call site,
      // which is a structurally-identical assembly (see
      // assembleRelaxedMomentumComponent's own header comment), not
      // merely a numerically-zero source.
      uAssembly = assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, massFlux, *effectiveViscosity, velocityBoundaries,
          pressureBoundaries, VelocityComponent::U, previousU, settings_.velocityRelaxation,
          temperature_, buoyancy_);
      vAssembly = assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, massFlux, *effectiveViscosity, velocityBoundaries,
          pressureBoundaries, VelocityComponent::V, previousV, settings_.velocityRelaxation,
          temperature_, buoyancy_);
    } catch (const NumericalError&) {
      // uAssembly/vAssembly left empty -- fall through to the check below.
    } catch (const InvalidArgumentError&) {
      // P2-TURB-003: a turbulence model that has produced a non-finite or
      // non-positive mu_eff (rejected by assembleDiffusionContribution's
      // own field-based overload) is a runtime-invalid state, not a
      // configuration error -- treated the same as NonFiniteState below,
      // not left to propagate out of solve() uncaught.
    }
    if (!uAssembly.has_value() || !vAssembly.has_value()) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    // P6-GPU-001: mirror this iteration's freshly-assembled momentum
    // matrices/fields into the persistent GPU residency path (structure
    // uploaded once, value-only updates on every later iteration whose
    // sparsity is unchanged -- see GpuResidencyManager's own header
    // comment). Read-only w.r.t. everything below -- never influences
    // uResult/vResult, which remain the sole CPU-computed source of this
    // iteration's velocity.
    if (settings_.enableGpuResidency) {
      gpuResidency.syncMatrix("momentum_u", uAssembly->system.matrix());
      gpuResidency.syncMatrix("momentum_v", vAssembly->system.matrix());
      gpuResidency.syncField("u", previousU);
      gpuResidency.syncField("v", previousV);
    }

    // Warm-start from the previous iterate -- this is not just an
    // efficiency choice: SolverResult::initialResidual (= ||b - A x0||)
    // then measures how far the *previous* SIMPLE iterate is from
    // satisfying the *current* (just-reassembled) momentum equation,
    // which is what genuinely shrinks as the outer SIMPLE iteration
    // approaches a steady state. The linear solve's own *final*
    // residual would instead just measure "did this one linear solve
    // converge tightly", which is near-zero by construction on every
    // successful inner solve regardless of outer-loop progress -- using
    // that as the SIMPLE convergence gate would falsely report
    // convergence after a single iteration (TODO.md section 37: "be
    // explicit... do not invent a second incompatible residual
    // definition").
    const auto uResult = momentumSolver->solve(uAssembly->system, toVector(previousU));
    if (!uResult.converged()) {
      finalStatus = SIMPLEStatus::MomentumFailure;
      break;
    }
    const auto vResult = momentumSolver->solve(vAssembly->system, toVector(previousV));
    if (!vResult.converged()) {
      finalStatus = SIMPLEStatus::MomentumFailure;
      break;
    }

    const VectorField velocityStar = combineComponents(uResult.solution, vResult.solution);
    if (!allFinite(velocityStar)) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    const SurfaceField predictorFlux =
        calculateMassFlux(mesh, velocityStar, fluid, velocityBoundaries);
    if (!allFinite(predictorFlux)) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    const ScalarField dU = computeMomentumResponseCoefficient(mesh, uAssembly->diagonal);
    const ScalarField dV = computeMomentumResponseCoefficient(mesh, vAssembly->diagonal);

    std::optional<PressureCorrectionAssembly> pAssembly;
    try {
      pAssembly = assemblePressureCorrection(mesh, predictorFlux, dU, dV, fluid.density(),
                                             referenceCell_, pressureBoundaries);
    } catch (const NumericalError&) {
      // pAssembly left empty -- fall through to the check below.
    }
    if (!pAssembly.has_value()) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    // P6-GPU-001: same read-only mirroring as the momentum matrices
    // above, for the pressure-correction system and the pre-correction
    // pressure field.
    if (settings_.enableGpuResidency) {
      gpuResidency.syncMatrix("pressure", pAssembly->system.matrix());
      gpuResidency.syncField("pressure", pressure);
    }

    const auto pResult = pressureSolver->solve(pAssembly->system);
    if (!pResult.converged()) {
      finalStatus = SIMPLEStatus::PressureCorrectionFailure;
      break;
    }
    const ScalarField pPrime = toScalarField(pResult.solution);
    if (!allFinite(pPrime)) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    ScalarField pressureNew(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      pressureNew[cell.id()] =
          pressure[cell.id()] + (settings_.pressureRelaxation * pPrime[cell.id()]);
    }

    const VectorField velocityNew =
        correctVelocity(mesh, velocityStar, dU, dV, pPrime, pressureBoundaries);
    const SurfaceField fluxNew =
        correctFaceMassFlux(mesh, predictorFlux, pAssembly->faceCoefficient, pPrime);

    if (!allFinite(velocityNew) || !allFinite(pressureNew) || !allFinite(fluxNew)) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    // pResult used the default zero initial guess (p' resets to 0 every
    // outer iteration -- TODO.md section 17), so its initialResidual is
    // ||b_p|| = the magnitude of the predictor imbalance driving this
    // correction: the same "before this iteration's linear solve"
    // principle as uResidual/vResidual above, applied to pressure
    // correction's own reset convention.
    const Real uResidual = uResult.initialResidual;
    const Real vResidual = vResult.initialResidual;
    const Real pResidual = pResult.initialResidual;
    const auto continuity = evaluateContinuity(mesh, fluxNew);
    const Real continuityResidual = rms(continuity.cellImbalance);
    const Real globalImbalance = std::abs(continuity.globalNetFlux);

    result.uResidualHistory.push_back(uResidual);
    result.vResidualHistory.push_back(vResidual);
    result.pressureResidualHistory.push_back(pResidual);
    result.continuityHistory.push_back(continuityResidual);

    velocity = velocityNew;
    pressure = pressureNew;
    massFlux = fluxNew;
    result.iterations = iteration + 1;

    result.finalUResidual = uResidual;
    result.finalVResidual = vResidual;
    result.finalPressureResidual = pResidual;
    result.finalContinuityResidual = continuityResidual;
    result.globalMassImbalance = globalImbalance;

    // P5-B section 14: fired once per completed outer iteration, with
    // exactly the residuals just pushed into result's own history above
    // -- a live view of the same canonical data the final SIMPLEResult
    // carries, not a second computation.
    if (progressCallback_) {
      progressCallback_(SIMPLEIterationProgress{result.iterations, settings_.maxIterations,
                                                uResidual, vResidual, pResidual, continuityResidual,
                                                globalImbalance});
    }

    // P2-TURB-004 section 24: gate on the active turbulence model's own
    // convergence residual too, when it reports one -- laminar/
    // LaminarModel report std::nullopt, so this is a no-op unaffecting
    // condition for every pre-P2-TURB-004 case (`turbulenceConverged`
    // reduces to `true`, exactly the prior behavior).
    const std::optional<Real> turbulenceResidual = activeModel->convergenceResidual();
    result.finalTurbulenceResidual = turbulenceResidual;
    const bool turbulenceConverged =
        !turbulenceResidual.has_value() || (*turbulenceResidual <= settings_.turbulenceTolerance);

    const bool converged =
        (uResidual <= settings_.velocityTolerance) && (vResidual <= settings_.velocityTolerance) &&
        (pResidual <= settings_.pressureTolerance) &&
        (continuityResidual <= settings_.continuityTolerance) &&
        (globalImbalance <= settings_.continuityTolerance) && turbulenceConverged;
    if (converged) {
      finalStatus = SIMPLEStatus::Converged;
      break;
    }
  }

  result.status = finalStatus;
  result.velocity = std::move(velocity);
  result.pressure = std::move(pressure);
  result.massFlux = std::move(massFlux);
  return result;
}

}  // namespace cfd::pressure_velocity
