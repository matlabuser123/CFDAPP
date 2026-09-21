#include "cfd/pressure_velocity/SIMPLE.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSolverFallback.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Logger.hpp"
#include "cfd/discretization/Gradient.hpp"
// GPU-DISC-001Q: per-stage wall time. steady_clock reads only -- no
// synchronization is added anywhere, so production behaviour is unchanged.
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/GpuResidencyManager.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"
#include "cfd/solver/SolverRobustness.hpp"
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

// GPU-PIPE-001: has the turbulence model actually changed the effective
// viscosity since the last upload? Bitwise, because "unchanged" has to mean
// unchanged -- an approximate comparison would let a genuinely updated
// viscosity stay stale on the device.
//
// LaminarModel returns a constant field for the whole solve, so on a laminar
// case this is true from iteration 2 onward and the viscosity uploads exactly
// once. A transport model that updates it every iteration re-uploads every
// iteration, which is correct and is the cost of that model, not of residency.
bool sameField(const ScalarField& a, const ScalarField& b) {
  if (a.size() != b.size()) return false;
  for (Index i = 0; i < a.size(); ++i) {
    if (std::memcmp(&a[i], &b[i], sizeof(cfd::Real)) != 0) return false;
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
    if (!isFinite(field[i])) return false;
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
    field[i] = cfd::physics::velocityComponentValue(velocity[i], component);
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

// P12-MESH-006: the 3D velocity (u, v, w).
VectorField combineComponents(const Vector& u, const Vector& v, const Vector& w) {
  VectorField result(u.size());
  for (Index i = 0; i < u.size(); ++i) {
    result[i] = Vector3{u[i], v[i], w[i]};
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

void SIMPLE::setMomentumSource(const VectorField* source) noexcept { momentumSource_ = source; }
const VectorField* SIMPLE::momentumSource() const noexcept { return momentumSource_; }

SIMPLEResult SIMPLE::solve(const Mesh& mesh, const FluidProperties& fluid,
                           const BoundaryConditionSet& velocityBoundaries,
                           const BoundaryConditionSet& pressureBoundaries,
                           VectorField initialVelocity, ScalarField initialPressure) const {
  // GPU-DISC-001Q: the whole call, so stageSeconds.other() is a residual that
  // accounts for everything the ten named stages do not.
  cfd::Timer solveTimer;
  SIMPLEResult result;
  // P12-MESH-006: a 3D mesh adds the W momentum equation (same assembly, same
  // relaxation, same linear solver as U and V) and the W response in the
  // pressure/velocity correction; the face-flux scheme is resolved for the
  // mesh (Automatic -> Linear in 2D, exactly as before; RhieChow in 3D).
  const bool threeDimensional = mesh.dimension() == 3;
  const FaceFluxScheme faceFlux = resolveFaceFluxScheme(settings_.faceFlux, mesh.dimension());
  result.faceFlux = faceFlux;

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
    // P12-NUM-006: the optional prescribed momentum source.
    if (momentumSource_ != nullptr) {
      if (momentumSource_->size() != mesh.numberOfCells()) {
        throw InvalidArgumentError(
            "SIMPLE::solve: momentum source size does not match mesh cell count");
      }
      if (!allFinite(*momentumSource_)) {
        throw InvalidArgumentError("SIMPLE::solve: momentum source must be finite");
      }
    }
    // P12-NUM-004: with robustness.linearSolverFallback disabled (default)
    // this is exactly makeLinearSolver(...); enabled, the same primary
    // solver wrapped in the one fallback policy (LinearSolverFallback.hpp).
    momentumSolver = cfd::algebra::makeLinearSolverWithFallback(
        settings_.momentumSolver, settings_.robustness.linearSolverFallback);
    pressureSolver = cfd::algebra::makeLinearSolverWithFallback(
        settings_.pressureSolver, settings_.robustness.linearSolverFallback);
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

  // P12-NUM-004: the one outer-iteration monitor (convergence decision,
  // normalized residuals, stagnation/divergence detection, adaptive
  // relaxation -- cfd/solver/SolverRobustness.hpp). With default
  // robustness settings its convergence decision is the pre-P12-NUM-004
  // absolute gate, it never reports Stagnated/Diverging, and relaxation()
  // returns settings_.velocityRelaxation/pressureRelaxation unchanged.
  cfd::solver::OuterIterationMonitor monitor(
      settings_.robustness,
      cfd::solver::OuterConvergenceTolerances{
          settings_.velocityTolerance, settings_.pressureTolerance, settings_.continuityTolerance,
          settings_.turbulenceTolerance},
      settings_.velocityRelaxation, settings_.pressureRelaxation);
  const auto failLinearSolve = [&](std::string_view equation,
                                   const cfd::algebra::LinearSolverSettings& solverSettings,
                                   const cfd::algebra::SolverResult& failed, Index iteration) {
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), iteration, equation,
                                            failed.fallback);
    monitor.diagnostics().statusDetail =
        cfd::solver::describeLinearSolveFailure(equation, solverSettings.type, failed);
  };

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

  // GPU-DISC-001M: the device DISCRETIZATION path -- the operators themselves,
  // not just the linear solves. Prepared once per solve() because the plans are
  // per-mesh, and ALL OR NOTHING: a refusal makes the whole solve run the CPU
  // operators and records why, so a mixed chain no gate has verified is never
  // executed.
  //
  // The non-orthogonal corrector passes (N > 1) stay on the CPU: their pass
  // loops call CPU-only helpers, and running pass 1 on the device and the rest
  // on the host would be exactly such a mixed chain. Declined explicitly, with
  // a reason, rather than silently half-applied.
  cfd::gpu::GpuSimpleDiscretization gpuDiscretization;
  bool useGpuDiscretization = false;
  if (settings_.enableGpuDiscretization) {
    std::string reason;
    if (settings_.nonOrthogonalCorrections > 1) {
      reason = "nonOrthogonalCorrections > 1 keeps the corrector passes on the CPU";
    } else if (!gpuDiscretization.prepare(mesh, velocityBoundaries, pressureBoundaries, reason)) {
      // `reason` was set by prepare().
    } else {
      useGpuDiscretization = true;
    }
    result.gpuDiscretization = useGpuDiscretization;
    if (!useGpuDiscretization) {
      result.gpuDiscretizationFallbackReason = reason;
      cfd::Logger::instance().warning("SIMPLE: GPU discretization requested but unavailable (" +
                                      reason + "); running the CPU discretization path");
    }
  }

  // GPU-PIPE-001 -- the GPU-resident pressure solve. Engaged only where it is
  // EXACTLY equivalent to the host round trip it replaces; every other
  // configuration keeps the existing path untouched. Each condition is a real
  // difference, not caution:
  //
  //   GPU discretization      the device system only exists on this path.
  //   backend == GPU          a CPU pressure solver must receive a host
  //                           system; that is the `disc-only` arm the
  //                           benchmarks compare against, and it must stay
  //                           bitwise what it was.
  //   type == BiCGSTAB        only BiCGSTAB has a resident entry point. GpuCG
  //                           was deliberately not touched.
  //   no fallback             makeLinearSolverWithFallback wraps the solver and
  //                           re-solves on the CPU when the GPU solve fails.
  //                           Bypassing the wrapper would change what happens
  //                           on failure -- including the KNOWN BiCGSTAB
  //                           breakdown, which must keep reproducing exactly.
  //   no residency mirror     gpuResidency.syncMatrix() reads the host matrix,
  //                           which the resident path never builds.
  //
  // Loop-invariant, so it is decided once here rather than re-tested every
  // outer iteration.
  const bool useResidentPressureSolve =
      useGpuDiscretization && !settings_.enableGpuResidency &&
      !settings_.robustness.linearSolverFallback.enabled &&
      settings_.pressureSolver.backend == cfd::algebra::LinearSolverBackend::GPU &&
      settings_.pressureSolver.type == cfd::algebra::LinearSolverType::BiCGSTAB;
  result.residentPressureSolve = useResidentPressureSolve;

  // GPU-PIPE-001 final residency -- the GPU-resident SIMPLE outer iteration.
  // Everything the resident pressure solve requires, plus three conditions of
  // its own. Each is a real difference, not caution:
  //
  //   laminar model           activeModel->correct(mesh, velocity, pressure)
  //                           below reads the HOST velocity, which a resident
  //                           loop deliberately leaves stale. LaminarModel's
  //                           correct() is a documented no-op and its
  //                           effective viscosity is constant for the whole
  //                           solve, so a stale host velocity changes nothing
  //                           it computes. A transport model genuinely reads
  //                           it, so that configuration keeps the
  //                           per-iteration download it has today.
  //   momentum backend GPU    a CPU momentum solver must receive a host
  //                           system -- the `disc-only` benchmark arm, which
  //                           must stay bitwise what it was.
  //   momentum type BiCGSTAB  only BiCGSTAB has a resident entry point.
  //
  // The solver fallback wrapper and the residency mirror are already excluded
  // by useResidentPressureSolve, for the same reasons it excludes them.
  // Loop-invariant, so it is decided once here.
  const bool useResidentSimpleLoop =
      useResidentPressureSolve && activeModel->name() == "laminar" &&
      settings_.momentumSolver.backend == cfd::algebra::LinearSolverBackend::GPU &&
      settings_.momentumSolver.type == cfd::algebra::LinearSolverType::BiCGSTAB;
  result.residentSimpleLoop = useResidentSimpleLoop;

  // GPU-DISC-001Q: everything before this point is per-solve setup -- derived
  // fields, the device plans, the initial mass flux. `stageTimer` is reset at
  // the start of each stage below and its elapsed time accumulated into the
  // matching counter; `solveTimer` runs for the whole call.
  result.stageSeconds.setup = solveTimer.elapsedSeconds();
  cfd::Timer stageTimer;
  // GPU-PIPE-001: the viscosity last uploaded to the device, so a laminar solve
  // uploads it once instead of every iteration. Empty on the CPU path, where it
  // is never read.
  ScalarField previousViscosity;

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

    // GPU-PIPE-001 final residency: on the resident path the relaxation's
    // phiOld and the momentum warm start are both taken from the DEVICE
    // velocity inside assembleMomentumResident/solveMomentumResident, and the
    // host copy is deliberately stale -- so these are left empty rather than
    // computed from a field no longer authoritative. Every consumer of them
    // below is inside a branch the resident path does not take.
    ScalarField previousU;
    ScalarField previousV;
    std::optional<ScalarField> previousW;
    if (!useResidentSimpleLoop) {
      previousU = selectComponent(velocity, VelocityComponent::U);
      previousV = selectComponent(velocity, VelocityComponent::V);
      if (threeDimensional) previousW = selectComponent(velocity, VelocityComponent::W);
    }
    // P12-NUM-004: this iteration's relaxation factors -- fixed for the
    // whole iteration (the adaptive controller only updates them after a
    // completed iteration); equal to the settings unless it is enabled.
    const cfd::solver::RelaxationFactors relaxation = monitor.relaxation();
    const Index outerIteration = iteration + 1;

    std::optional<MomentumAssembly> uAssembly;
    std::optional<MomentumAssembly> vAssembly;
    std::optional<MomentumAssembly> wAssembly;
    // GPU-PIPE-001 final residency: on the resident path the momentum systems
    // never become host MomentumAssembly objects, so the three optionals stay
    // empty and this records that the assembly nonetheless succeeded -- the
    // same shape residentAssemblyOk already has for pressure.
    bool residentMomentumOk = false;
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
      // GPU-DISC-001Q: momentum assembly starts here. On the device path this
      // includes beginIteration's per-iteration upload, which is the cost of
      // feeding that assembly and belongs with it rather than in `other`.
      stageTimer.reset();
      // GPU-DISC-001M: this iteration's inputs, uploaded once. Every device
      // stage below then reads state that is already resident.
      //
      // GPU-PIPE-001: only the FIRST iteration uploads. After that the device
      // carries its own corrected velocity, corrected flux and updated pressure
      // forward, so the six full-field H2D copies that used to happen every
      // iteration happen once per solve instead. The viscosity is re-uploaded
      // only when the turbulence model actually changed it -- a laminar solve
      // transfers it once and never again.
      if (useGpuDiscretization) {
        if (iteration == 0) {
          gpuDiscretization.uploadInitialState(velocity, pressure, massFlux, *effectiveViscosity);
          previousViscosity = *effectiveViscosity;
        } else {
          gpuDiscretization.beginIterationResident();
          if (!sameField(previousViscosity, *effectiveViscosity)) {
            gpuDiscretization.setViscosity(*effectiveViscosity);
            previousViscosity = *effectiveViscosity;
          }
        }
      }
      // P3-PHYS-001: temperature_/buoyancy_ are passed through unchanged
      // every iteration (held fixed for the whole solve() call -- see
      // this class's own header comment on the deliberate one-way-
      // coupling scope); both null for every pre-existing call site,
      // which is a structurally-identical assembly (see
      // assembleRelaxedMomentumComponent's own header comment), not
      // merely a numerically-zero source.
      // P12-NUM-003: nonOrthogonalCorrections == 0 (default) -> false,
      // exactly today's assembly; >= 1 -> pass 1 of the correction loop
      // (explicit correction evaluated from the lagged `velocity`).
      if (useResidentSimpleLoop) {
        // GPU-PIPE-001 final residency: the SAME device assembly as the branch
        // below -- same kernels, same arguments -- but phiOld is carried from
        // the resident velocity instead of uploaded, and the CSR system stays
        // on the device instead of being downloaded and rebuilt into a host
        // SparseMatrix.
        const Index scheme = static_cast<Index>(settings_.convectionScheme);
        const bool nonOrthogonal = settings_.nonOrthogonalCorrections > 0;
        gpuDiscretization.assembleMomentumResident(0, relaxation.velocity, scheme, nonOrthogonal);
        gpuDiscretization.assembleMomentumResident(1, relaxation.velocity, scheme, nonOrthogonal);
        if (threeDimensional) {
          gpuDiscretization.assembleMomentumResident(2, relaxation.velocity, scheme, nonOrthogonal);
        }
        residentMomentumOk = true;
      } else if (useGpuDiscretization) {
        // GPU-DISC-001M: the verified device assembly (GPU-DISC-001F). The
        // system comes back on the host because LinearSolver takes a host
        // LinearSystem; the diagonal stays resident for the response
        // coefficients, so `diagonal` here is deliberately left empty and the
        // device path below never reads it.
        const Index scheme = static_cast<Index>(settings_.convectionScheme);
        const bool nonOrthogonal = settings_.nonOrthogonalCorrections > 0;
        uAssembly = MomentumAssembly{gpuDiscretization.assembleMomentum(
                                         0, previousU, relaxation.velocity, scheme, nonOrthogonal),
                                     Vector{}};
        vAssembly = MomentumAssembly{gpuDiscretization.assembleMomentum(
                                         1, previousV, relaxation.velocity, scheme, nonOrthogonal),
                                     Vector{}};
        if (threeDimensional) {
          wAssembly =
              MomentumAssembly{gpuDiscretization.assembleMomentum(
                                   2, *previousW, relaxation.velocity, scheme, nonOrthogonal),
                               Vector{}};
        }
      } else {
        uAssembly = assembleRelaxedMomentumComponent(
            mesh, velocity, pressure, massFlux, *effectiveViscosity, velocityBoundaries,
            pressureBoundaries, VelocityComponent::U, previousU, relaxation.velocity, temperature_,
            buoyancy_, settings_.convectionScheme, settings_.gradientScheme,
            settings_.nonOrthogonalCorrections > 0, /*nonOrthogonalCorrectionVelocity=*/nullptr,
            momentumSource_);
        vAssembly = assembleRelaxedMomentumComponent(
            mesh, velocity, pressure, massFlux, *effectiveViscosity, velocityBoundaries,
            pressureBoundaries, VelocityComponent::V, previousV, relaxation.velocity, temperature_,
            buoyancy_, settings_.convectionScheme, settings_.gradientScheme,
            settings_.nonOrthogonalCorrections > 0, /*nonOrthogonalCorrectionVelocity=*/nullptr,
            momentumSource_);
        if (threeDimensional) {
          wAssembly = assembleRelaxedMomentumComponent(
              mesh, velocity, pressure, massFlux, *effectiveViscosity, velocityBoundaries,
              pressureBoundaries, VelocityComponent::W, *previousW, relaxation.velocity,
              temperature_, buoyancy_, settings_.convectionScheme, settings_.gradientScheme,
              settings_.nonOrthogonalCorrections > 0, /*nonOrthogonalCorrectionVelocity=*/nullptr,
              momentumSource_);
        }
      }
      result.stageSeconds.momentumAssembly += stageTimer.elapsedSeconds();
    } catch (const NumericalError&) {
      // uAssembly/vAssembly left empty -- fall through to the check below.
    } catch (const InvalidArgumentError&) {
      // P2-TURB-003: a turbulence model that has produced a non-finite or
      // non-positive mu_eff (rejected by assembleDiffusionContribution's
      // own field-based overload) is a runtime-invalid state, not a
      // configuration error -- treated the same as NonFiniteState below,
      // not left to propagate out of solve() uncaught.
    }
    if (!residentMomentumOk && (!uAssembly.has_value() || !vAssembly.has_value() ||
                                (threeDimensional && !wAssembly.has_value()))) {
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
      if (threeDimensional) {
        gpuResidency.syncMatrix("momentum_w", wAssembly->system.matrix());
        gpuResidency.syncField("w", *previousW);
      }
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
    // GPU-PIPE-001 final residency: the resident call runs the SAME BiCGSTAB
    // implementation -- one algorithm, two entry points -- directly against
    // the device system, warm started from the same bytes `toVector(previousU)`
    // would have carried, and leaves u* on the device.
    stageTimer.reset();
    const auto uResult = useResidentSimpleLoop
                             ? gpuDiscretization.solveMomentumResident(0, settings_.momentumSolver)
                             : momentumSolver->solve(uAssembly->system, toVector(previousU));
    if (!uResult.converged()) {
      failLinearSolve("u-momentum", settings_.momentumSolver, uResult, outerIteration);
      finalStatus = SIMPLEStatus::MomentumFailure;
      break;
    }
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration, "u-momentum",
                                            uResult.fallback);
    const auto vResult = useResidentSimpleLoop
                             ? gpuDiscretization.solveMomentumResident(1, settings_.momentumSolver)
                             : momentumSolver->solve(vAssembly->system, toVector(previousV));
    if (!vResult.converged()) {
      failLinearSolve("v-momentum", settings_.momentumSolver, vResult, outerIteration);
      finalStatus = SIMPLEStatus::MomentumFailure;
      break;
    }
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration, "v-momentum",
                                            vResult.fallback);
    result.momentumLinearIterations += uResult.iterations + vResult.iterations;
    std::optional<cfd::algebra::SolverResult> wResult;
    if (threeDimensional) {
      wResult = useResidentSimpleLoop
                    ? gpuDiscretization.solveMomentumResident(2, settings_.momentumSolver)
                    : momentumSolver->solve(wAssembly->system, toVector(*previousW));
      if (!wResult->converged()) {
        failLinearSolve("w-momentum", settings_.momentumSolver, *wResult, outerIteration);
        finalStatus = SIMPLEStatus::MomentumFailure;
        break;
      }
      cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration, "w-momentum",
                                              wResult->fallback);
      result.momentumLinearIterations += wResult->iterations;
    }

    // GPU-PIPE-001 final residency: on the resident path the predictor stays
    // where the solve left it -- on the device -- so no host VectorField is
    // built and nothing is uploaded back. Both stages that read the predictor
    // (response coefficients, predicted face flux) already read it there.
    VectorField velocityStar;
    if (!useResidentSimpleLoop) {
      velocityStar = threeDimensional
                         ? combineComponents(uResult.solution, vResult.solution, wResult->solution)
                         : combineComponents(uResult.solution, vResult.solution);
      // GPU-DISC-001M: the predictor goes back to the device for every stage
      // below. This H2D is forced by the host LinearSolver interface and is
      // counted in the transfer audit rather than hidden.
      if (useGpuDiscretization) {
        gpuDiscretization.setMomentumSolution(0, uResult.solution);
        gpuDiscretization.setMomentumSolution(1, vResult.solution);
        if (threeDimensional) gpuDiscretization.setMomentumSolution(2, wResult->solution);
      }
    }
    // The same guard either way; only where it is evaluated differs. On the
    // resident path four bytes cross instead of a whole field.
    const bool predictorFinite =
        useResidentSimpleLoop
            ? gpuDiscretization.residentMomentumPredictorAllFinite(threeDimensional)
            : allFinite(velocityStar);
    if (!predictorFinite) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    // P12-NUM-003: passes 2..N of the explicit non-orthogonal correction
    // loop (N = settings_.nonOrthogonalCorrections) -- see
    // runNonOrthogonalCorrectionPasses (RelaxedMomentum.hpp), the single
    // implementation of that loop. Not even entered for the default N = 0
    // or for N = 1, so those paths are exactly the pass-1-only solve
    // above. Each pass re-solves the SAME relaxed momentum equation
    // (identical implicit matrix) with only the explicit correction re-
    // evaluated from the latest predictor velocity -- the standard
    // "non-orthogonal corrector" iteration (cf. OpenFOAM's
    // nNonOrthogonalCorrectors, whose count is offset by one from this
    // one: there 0 still means one corrected solve; here 0 means the
    // correction is off entirely, so every pre-P12-NUM-003 case is
    // unchanged). uResult/vResult above (pass 1) remain the source of this
    // iteration's residual history: their initialResidual is the
    // "previous outer iterate vs freshly-assembled equation" measure the
    // convergence gate is defined on (see the warm-start comment above).
    if (settings_.nonOrthogonalCorrections > 1) {
      NonOrthogonalPassResult passes = runNonOrthogonalCorrectionPasses(
          settings_.nonOrthogonalCorrections, velocityStar, *momentumSolver, mesh, velocity,
          pressure, massFlux, *effectiveViscosity, velocityBoundaries, pressureBoundaries,
          previousU, previousV, relaxation.velocity, temperature_, buoyancy_,
          settings_.convectionScheme, settings_.gradientScheme, momentumSource_,
          previousW.has_value() ? &*previousW : nullptr);
      for (const auto& report : passes.fallbackReports) {
        cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration,
                                                "momentum-pass", report);
      }
      if (passes.status == NonOrthogonalPassStatus::NonFiniteState) {
        finalStatus = SIMPLEStatus::NonFiniteState;
        break;
      }
      if (passes.status == NonOrthogonalPassStatus::MomentumFailure) {
        if (passes.failedSolve.has_value()) {
          monitor.diagnostics().statusDetail = cfd::solver::describeLinearSolveFailure(
              "momentum-pass", settings_.momentumSolver.type, *passes.failedSolve);
        }
        finalStatus = SIMPLEStatus::MomentumFailure;
        break;
      }
      velocityStar = std::move(passes.velocityStar);
      uAssembly = std::move(passes.u);
      vAssembly = std::move(passes.v);
      if (passes.w.has_value()) wAssembly = std::move(passes.w);
      result.momentumPredictorPasses += passes.passesExecuted;
      result.momentumLinearIterations += passes.linearIterations;
    }
    ++result.momentumPredictorPasses;
    // GPU-DISC-001Q: includes the non-orthogonal momentum corrector passes
    // (re-assembly + re-solve, a single boundary rather than two interleaved
    // ones) and, on the device path, the predictor upload the host
    // LinearSolver interface forces.
    result.stageSeconds.momentumSolve += stageTimer.elapsedSeconds();
    stageTimer.reset();

    // Response coefficients d = V/aP of this iteration's (relaxed) momentum
    // diagonals -- pure functions of them, so computing them before the
    // predictor flux (which Rhie-Chow needs) changes nothing for Linear.
    // GPU-DISC-001M: on the device path the coefficients are computed from the
    // resident diagonals (GPU-DISC-001G) and stay resident -- no host copy is
    // made, because nothing on that path reads one.
    ScalarField dU;
    ScalarField dV;
    std::optional<ScalarField> dW;
    if (useGpuDiscretization) {
      gpuDiscretization.computeResponseCoefficients(threeDimensional);
    } else {
      dU = computeMomentumResponseCoefficient(mesh, uAssembly->diagonal);
      dV = computeMomentumResponseCoefficient(mesh, vAssembly->diagonal);
      if (threeDimensional) dW = computeMomentumResponseCoefficient(mesh, wAssembly->diagonal);
    }
    const ScalarField* dWPointer = dW.has_value() ? &*dW : nullptr;
    result.stageSeconds.responseCoefficients += stageTimer.elapsedSeconds();
    stageTimer.reset();

    // P12-MESH-006: the predictor face flux -- the linear interpolation of u*
    // (Linear, exactly as before), or Rhie-Chow (RhieChow.hpp) with the same
    // cell pressure gradient the momentum equations used this iteration.
    SurfaceField predictorFlux;
    if (useGpuDiscretization) {
      // GPU-DISC-001H (Rhie-Chow) or the device linear flux, with the pressure
      // gradient (GPU-DISC-001B) also computed on the device from the
      // start-of-iteration pressure. The result stays resident: the pressure
      // assembly and the flux correction both read it there, so it is never
      // downloaded.
      gpuDiscretization.computePredictedFaceFlux(
          faceFlux == FaceFluxScheme::RhieChow, fluid.density(), relaxation.velocity,
          static_cast<Index>(settings_.gradientScheme), threeDimensional);
    } else if (faceFlux == FaceFluxScheme::RhieChow) {
      std::optional<SurfaceField> rhieChowFlux;
      try {
        const VectorField gradP = cfd::discretization::gradient(mesh, pressure, pressureBoundaries,
                                                                settings_.gradientScheme);
        rhieChowFlux = rhieChowMassFlux(mesh, velocityStar, pressure, gradP, dU, dV, dWPointer,
                                        fluid, velocityBoundaries, relaxation.velocity);
      } catch (const NumericalError&) {
        // left empty -- NonFiniteState below.
      }
      if (!rhieChowFlux.has_value()) {
        finalStatus = SIMPLEStatus::NonFiniteState;
        break;
      }
      predictorFlux = std::move(*rhieChowFlux);
    } else {
      predictorFlux = calculateMassFlux(mesh, velocityStar, fluid, velocityBoundaries);
    }
    result.stageSeconds.predictedFaceFlux += stageTimer.elapsedSeconds();
    // The device path's predictor stays resident, so there is nothing here to
    // check; its finiteness is covered by the allFinite() on the corrected flux
    // below, which is the value that is actually committed.
    if (!useGpuDiscretization && !allFinite(predictorFlux)) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }

    // P12-NUM-003: pressure-correction non-orthogonal corrector loop
    // (N = settings_.nonOrthogonalCorrections). The face coupling is always
    // the geometric one (PressureCorrectionEquation.hpp); what N controls:
    //   N = 0  two-point coupling, no explicit term (on a Cartesian mesh
    //          exactly the pre-P12-NUM-003 equation);
    //   N >= 1 over-relaxed implicit coefficient (the ORTHOGONAL part,
    //          |E|/|d|), and passes 2..N re-assemble with the EXPLICIT
    //          non-orthogonal part -rho_f T . grad(p'_{k-1}) on the RHS and
    //          re-solve for p'_k (from a zero initial guess -- see the pass
    //          loop below). Pass 1 has no explicit
    //          term: in this incremental p' formulation p' starts every
    //          outer iteration at zero. (OpenFOAM's pEqn loop carries the
    //          previous *pressure* instead, so its count is offset by one.)
    // p' is applied ONCE, after the last pass: the pressure update, the
    // velocity correction and the face-flux correction all use the final
    // p', and the flux correction adds exactly the explicit face terms the
    // final assembly's RHS assumed -- so the corrected flux satisfies the
    // discrete continuity equation that assembly encodes.
    PressureCorrectionOptions pressureOptions;
    pressureOptions.nonOrthogonal = settings_.nonOrthogonalCorrections > 0;
    pressureOptions.gradientScheme = settings_.gradientScheme;

    std::optional<PressureCorrectionAssembly> pAssembly;
    // GPU-PIPE-001: on the resident path the system never becomes a host
    // PressureCorrectionAssembly, so `pAssembly` stays empty and this records
    // that the assembly nonetheless succeeded.
    bool residentAssemblyOk = false;
    stageTimer.reset();
    try {
      if (useResidentPressureSolve) {
        // GPU-PIPE-001: identical device assembly to the branch below -- same
        // kernels, same arguments -- but the CSR system stays on the device
        // instead of being downloaded and rebuilt into a host SparseMatrix.
        gpuDiscretization.assemblePressureCorrectionResident(
            pressureOptions.nonOrthogonal, referenceCell_, fluid.density(), threeDimensional);
        residentAssemblyOk = true;
      } else if (useGpuDiscretization) {
        // GPU-DISC-001I, from the resident predictor flux and response
        // coefficients. Only the assembled system crosses to the host, for the
        // solver; faceCoefficient and explicitFaceFlux stay resident for the
        // flux correction, which is exactly why 001I emits them on device.
        pAssembly = PressureCorrectionAssembly{gpuDiscretization.assemblePressureCorrection(
                                                   pressureOptions.nonOrthogonal, referenceCell_,
                                                   fluid.density(), nullptr, threeDimensional),
                                               SurfaceField{}, SurfaceField{}};
      } else {
        pAssembly =
            assemblePressureCorrection(mesh, predictorFlux, dU, dV, fluid.density(), referenceCell_,
                                       pressureBoundaries, pressureOptions, dWPointer);
      }
    } catch (const NumericalError&) {
      // pAssembly left empty -- fall through to the check below.
    }
    result.stageSeconds.pressureAssembly += stageTimer.elapsedSeconds();
    if (!pAssembly.has_value() && !residentAssemblyOk) {
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

    stageTimer.reset();
    // GPU-PIPE-001: the resident call runs the SAME GpuBiCGSTAB algorithm --
    // one implementation, two entry points (GpuResidentSolve.hpp) -- directly
    // against the device system, and leaves p' on the device.
    const auto pResult =
        useResidentPressureSolve
            ? gpuDiscretization.solvePressureCorrectionResident(settings_.pressureSolver)
            : pressureSolver->solve(pAssembly->system);
    result.stageSeconds.pressureSolve += stageTimer.elapsedSeconds();
    if (!pResult.converged()) {
      failLinearSolve("pressure-correction", settings_.pressureSolver, pResult, outerIteration);
      finalStatus = SIMPLEStatus::PressureCorrectionFailure;
      break;
    }
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration,
                                            "pressure-correction", pResult.fallback);
    result.pressureLinearIterations += pResult.iterations;
    // GPU-PIPE-001: on the resident path p' never comes to the host at all --
    // the solve left it on the device and both corrections read it there -- so
    // this stays empty. Every later use of it is inside a branch the resident
    // path does not take (the corrector-pass loop, which the GPU path declines,
    // and the host pressure update and corrections).
    ScalarField pPrime;
    if (!useResidentPressureSolve) {
      pPrime = toScalarField(pResult.solution);
      // GPU-DISC-001M: p' goes back to the device for the two corrections. The
      // host copy is still needed for the pressure update, which is a per-cell
      // loop here rather than an operator.
      if (useGpuDiscretization) gpuDiscretization.setPressureCorrection(pResult.solution);
    }
    // The same guard either way; only where it is evaluated differs. On the
    // resident path four bytes cross instead of a whole field.
    const bool pressureCorrectionFinite =
        useResidentPressureSolve ? gpuDiscretization.residentPressureCorrectionAllFinite()
                                 : allFinite(pPrime);
    if (!pressureCorrectionFinite) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
    }
    ++result.pressureCorrectionPasses;

    bool pressurePassFailed = false;
    for (Index pass = 1; pass < settings_.nonOrthogonalCorrections; ++pass) {
      const ScalarField previousPPrime = pPrime;
      pressureOptions.previousPressureCorrection = &previousPPrime;
      std::optional<PressureCorrectionAssembly> passAssembly;
      stageTimer.reset();
      try {
        passAssembly =
            assemblePressureCorrection(mesh, predictorFlux, dU, dV, fluid.density(), referenceCell_,
                                       pressureBoundaries, pressureOptions, dWPointer);
      } catch (const NumericalError&) {
        // passAssembly left empty -- fall through to the check below.
      }
      pressureOptions.previousPressureCorrection = nullptr;
      result.stageSeconds.pressureAssembly += stageTimer.elapsedSeconds();
      if (!passAssembly.has_value()) {
        finalStatus = SIMPLEStatus::NonFiniteState;
        pressurePassFailed = true;
        break;
      }
      // Zero initial guess, like pass 1 (SIMPLE's "p' resets to 0"
      // convention): a solve warm-started from p'_{k-1} begins from a
      // residual only as large as the explicit term's change, and BiCGSTAB's
      // absolute breakdown thresholds were measured to trip at that scale
      // (Breakdown at ~2e-10 from a 5e-8 start on the distorted cavity) --
      // starting from zero keeps every pass at pass 1's well-tested scale.
      // Same exact solution either way.
      stageTimer.reset();
      const auto passResult = pressureSolver->solve(passAssembly->system);
      result.stageSeconds.pressureSolve += stageTimer.elapsedSeconds();
      if (!passResult.converged()) {
        failLinearSolve("pressure-correction-pass", settings_.pressureSolver, passResult,
                        outerIteration);
        finalStatus = SIMPLEStatus::PressureCorrectionFailure;
        pressurePassFailed = true;
        break;
      }
      cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration,
                                              "pressure-correction-pass", passResult.fallback);
      result.pressureLinearIterations += passResult.iterations;
      pPrime = toScalarField(passResult.solution);
      if (!allFinite(pPrime)) {
        finalStatus = SIMPLEStatus::NonFiniteState;
        pressurePassFailed = true;
        break;
      }
      pAssembly = std::move(passAssembly);
      ++result.pressureCorrectionPasses;
    }
    if (pressurePassFailed) {
      break;
    }

    stageTimer.reset();
    ScalarField pressureNew;
    if (useGpuDiscretization) {
      // GPU-PIPE-001: on the device, from the resident pressure and the p' just
      // uploaded. Bitwise identical to the host loop below -- same association
      // order, compiled without FMA contraction (see
      // DevicePersistentFieldsKernel.cu). `pressure` stays resident; the host
      // copy is refreshed only when something actually needs it.
      gpuDiscretization.updatePressure(relaxation.pressure);
    } else {
      pressureNew = ScalarField(mesh.numberOfCells());
      for (const auto& cell : mesh.cells()) {
        pressureNew[cell.id()] = pressure[cell.id()] + (relaxation.pressure * pPrime[cell.id()]);
      }
    }
    // The relaxed pressure update is a per-cell host loop on the CPU path -- it
    // is not an operator and GPU-DISC-001 did not port it. Counted as
    // bookkeeping so it stays visible rather than disappearing into `other`.
    result.stageSeconds.bookkeeping += stageTimer.elapsedSeconds();

    VectorField velocityNew;
    SurfaceField fluxNew;
    stageTimer.reset();
    if (useGpuDiscretization) {
      // GPU-DISC-001J and 001K, from the resident predictor velocity, predictor
      // flux and face coefficients.
      //
      // GPU-PIPE-001: the results are carried into the persistent fields on the
      // device rather than downloaded. Only the mass flux comes back, because
      // evaluateContinuity() below genuinely needs the whole face field on the
      // host to stay bitwise-equal to the CPU (audit.md S3). Velocity and
      // pressure are downloaded once, after the loop.
      gpuDiscretization.correctVelocityResident(static_cast<Index>(settings_.gradientScheme),
                                                threeDimensional);
      // The velocity comes back every iteration, and NOT because the device
      // needs it -- the device kept its own copy. The HOST needs it: the next
      // iteration takes `previousU`/`previousV` from it for the under-relaxation
      // RHS and, critically, for the momentum solver's warm-start initial guess
      // `toVector(previousU)`. Leaving it stale changed the solver's iteration
      // path and broke equivalence at outer iteration 2 -- found by
      // full_solve_equivalence, and the reason this download is here.
      //
      // What residency still buys on this field is the UPLOAD: the device never
      // re-uploads it, because it carried its own corrected velocity forward.
      //
      // GPU-PIPE-001 final residency: on the resident path this download is
      // GONE. The two host consumers it existed for -- the next iteration's
      // under-relaxation phiOld and the momentum solver's warm-start guess --
      // both read the DEVICE velocity now, so the field comes back once,
      // after the loop, like the pressure already does.
      if (!useResidentSimpleLoop) gpuDiscretization.downloadVelocity(velocityNew);
      result.stageSeconds.velocityCorrection += stageTimer.elapsedSeconds();
      stageTimer.reset();
      gpuDiscretization.correctFaceMassFluxResident(settings_.nonOrthogonalCorrections > 1);
      gpuDiscretization.downloadMassFlux(fluxNew);
    } else {
      velocityNew = correctVelocity(mesh, velocityStar, dU, dV, pPrime, pressureBoundaries,
                                    settings_.gradientScheme, dWPointer);
      result.stageSeconds.velocityCorrection += stageTimer.elapsedSeconds();
      stageTimer.reset();
      fluxNew = correctFaceMassFlux(
          mesh, predictorFlux, pAssembly->faceCoefficient, pPrime,
          (settings_.nonOrthogonalCorrections > 1) ? &pAssembly->explicitFaceFlux : nullptr);
    }
    result.stageSeconds.faceFluxCorrection += stageTimer.elapsedSeconds();
    stageTimer.reset();

    // GPU-PIPE-001: on the resident path velocity and pressure live on the
    // device, so the guard is evaluated THERE and one integer per field crosses
    // the boundary instead of five full fields. It is a predicate, so the
    // reduction order carries no bitwise consequence -- the one reduction in
    // this codebase for which that is true.
    //
    // GPU-PIPE-001 final residency: velocity joins pressure on the device
    // side of that split. The face flux stays on the host side because it is
    // already here -- evaluateContinuity below needs the whole field to stay
    // bitwise equal to the CPU (audit.md section 5).
    const bool stateFinite =
        useResidentSimpleLoop
            ? (gpuDiscretization.residentVelocityAllFinite() && allFinite(fluxNew) &&
               gpuDiscretization.residentPressureAllFinite())
        : useGpuDiscretization
            ? (allFinite(velocityNew) && allFinite(fluxNew) &&
               gpuDiscretization.residentPressureAllFinite())
            : (allFinite(velocityNew) && allFinite(pressureNew) && allFinite(fluxNew));
    if (!stateFinite) {
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
    // P12-MESH-006: the W residual (same definition as U/V), 3D only.
    const Real wResidual = threeDimensional ? wResult->initialResidual : 0.0;
    if (threeDimensional) result.wResidualHistory.push_back(wResidual);

    // GPU-PIPE-001 final residency: NEITHER `velocity` nor `pressure` is
    // carried here on the resident path. The device updated both itself and
    // the host copies stay deliberately stale until the single download after
    // the loop. `velocity` used to be carried because the next iteration read
    // previousU/previousV from it; the resident momentum assembly reads the
    // device copy instead, so that reason is gone. The authority model in
    // GpuSimpleDiscretization is what makes staleness a stated contract
    // rather than an accident. `massFlux` IS still carried, because
    // evaluateContinuity below reads it on the host (audit.md section 5).
    if (!useResidentSimpleLoop) velocity = velocityNew;
    if (!useGpuDiscretization) pressure = pressureNew;
    massFlux = fluxNew;
    result.iterations = iteration + 1;

    result.finalUResidual = uResidual;
    result.finalVResidual = vResidual;
    result.finalPressureResidual = pResidual;
    result.finalContinuityResidual = continuityResidual;
    result.globalMassImbalance = globalImbalance;
    result.finalWResidual = wResidual;

    // P5-B section 14: fired once per completed outer iteration, with
    // exactly the residuals just pushed into result's own history above
    // -- a live view of the same canonical data the final SIMPLEResult
    // carries, not a second computation.
    if (progressCallback_) {
      progressCallback_(SIMPLEIterationProgress{result.iterations, settings_.maxIterations,
                                                uResidual, vResidual, pResidual, continuityResidual,
                                                globalImbalance, wResidual, threeDimensional});
    }

    // P2-TURB-004 section 24: gate on the active turbulence model's own
    // convergence residual too, when it reports one -- laminar/
    // LaminarModel report std::nullopt, so this is a no-op unaffecting
    // condition for every pre-P2-TURB-004 case (`turbulenceConverged`
    // reduces to `true`, exactly the prior behavior).
    const std::optional<Real> turbulenceResidual = activeModel->convergenceResidual();
    result.finalTurbulenceResidual = turbulenceResidual;

    // P12-NUM-004: the convergence gate (default: exactly the absolute
    // comparisons u, v <= velocityTolerance, p <= pressureTolerance,
    // continuity and global imbalance <= continuityTolerance, turbulence
    // residual (when reported) <= turbulenceTolerance), then -- only if
    // enabled -- divergence, stagnation, and the relaxation update.
    // P12-MESH-006: a 3D solve also gates on W (never Converged while W is not).
    const cfd::solver::OuterIterationVerdict verdict =
        monitor.record(cfd::solver::OuterResidualSample{
            uResidual, vResidual, pResidual, continuityResidual, globalImbalance,
            turbulenceResidual, threeDimensional ? std::optional<Real>(wResidual) : std::nullopt});
    // Accumulated BEFORE the verdict branches, so the iteration that converges
    // or diverges still contributes its bookkeeping rather than dropping it.
    result.stageSeconds.bookkeeping += stageTimer.elapsedSeconds();
    if (verdict == cfd::solver::OuterIterationVerdict::Converged) {
      finalStatus = SIMPLEStatus::Converged;
      break;
    }
    if (verdict == cfd::solver::OuterIterationVerdict::Diverging) {
      finalStatus = SIMPLEStatus::Diverging;
      break;
    }
    if (verdict == cfd::solver::OuterIterationVerdict::Stagnated) {
      finalStatus = SIMPLEStatus::Stagnated;
      break;
    }
  }

  // GPU-PIPE-001: the one place the resident velocity and pressure come back.
  // Everything the API promises to hand a caller -- result fields, export,
  // GUI/CLI retrieval, validation hooks -- is fed from here, so nothing
  // host-visible is left stale. Skipped when the solve never ran an iteration
  // (an InvalidConfiguration or a first-iteration failure returns earlier, and
  // a zero-iteration budget leaves the host state exactly as it arrived).
  if (useGpuDiscretization && result.iterations > 0) {
    gpuDiscretization.downloadVelocity(velocity);
    gpuDiscretization.downloadPressure(pressure);
  }

  result.status = finalStatus;
  result.robustness = monitor.takeDiagnostics();
  result.velocity = std::move(velocity);
  result.pressure = std::move(pressure);
  result.massFlux = std::move(massFlux);
  result.stageSeconds.total = solveTimer.elapsedSeconds();
  return result;
}

}  // namespace cfd::pressure_velocity
