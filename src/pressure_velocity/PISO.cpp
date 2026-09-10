#include "cfd/pressure_velocity/PISO.hpp"

#include <cmath>
#include <optional>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"
#include "cfd/solver/CFL.hpp"
#include "cfd/turbulence/LaminarModel.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::BiCGSTAB;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::calculateMassFlux;
using cfd::physics::evaluateContinuity;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;
using cfd::solver::calculateCFL;
using cfd::solver::TransientState;
using cfd::solver::TransientStepResult;
using cfd::solver::TransientStepStatus;
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

TransientStepResult failureResult(TransientStepStatus status, const TransientState& previousState) {
  TransientStepResult result;
  result.status = status;
  result.state = previousState;
  return result;
}

}  // namespace

PISO::PISO(const Mesh& mesh, const FluidProperties& fluid,
           const BoundaryConditionSet& velocityBoundaries,
           const BoundaryConditionSet& pressureBoundaries, PISOSettings settings,
           Index referenceCell, TurbulenceModel* turbulenceModel)
    : mesh_(mesh),
      fluid_(fluid),
      velocityBoundaries_(velocityBoundaries),
      pressureBoundaries_(pressureBoundaries),
      settings_(settings),
      referenceCell_(referenceCell),
      turbulenceModel_(turbulenceModel) {}

const PISOSettings& PISO::settings() const noexcept { return settings_; }
Index PISO::referenceCell() const noexcept { return referenceCell_; }

TransientStepResult PISO::solveTimeStep(const TransientState& previousState, Real dt) const {
  // Configuration problems are a legitimate solveTimeStep()-time outcome
  // a caller branches on via `status`, not an exception escaping this
  // function -- mirrors SIMPLE::solve()'s own treatment of its
  // equivalent up-front checks, including catching the BiCGSTAB
  // constructors' own settings validation.
  bool configurationValid = true;
  std::optional<BiCGSTAB> momentumSolver;
  std::optional<BiCGSTAB> pressureSolver;
  try {
    if (previousState.velocity.size() != mesh_.numberOfCells() ||
        previousState.pressure.size() != mesh_.numberOfCells() ||
        previousState.massFlux.size() != mesh_.numberOfFaces()) {
      throw InvalidArgumentError("PISO::solveTimeStep: previousState size does not match mesh");
    }
    if (referenceCell_ >= mesh_.numberOfCells()) {
      throw InvalidArgumentError("PISO::solveTimeStep: referenceCell out of range");
    }
    if (!std::isfinite(dt) || !(dt > 0.0)) {
      throw InvalidArgumentError("PISO::solveTimeStep: dt must be finite and > 0");
    }
    momentumSolver.emplace(settings_.momentumSolver);
    pressureSolver.emplace(settings_.pressureSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    return failureResult(TransientStepStatus::InvalidConfiguration, previousState);
  }

  // Defense in depth (TODO.md P2 section 15/51): PISO cannot assume its
  // caller already validated previousState -- TransientSolver checks the
  // *previous* step's own output after accepting it, but this is a
  // general TransientStepSolver interface, not exclusively driven by
  // TransientSolver.
  if (!allFinite(previousState.velocity) || !allFinite(previousState.pressure) ||
      !allFinite(previousState.massFlux)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  TransientStepResult result;
  // Pre-step CFL, from the previous state's flux and this step's dt,
  // before the momentum predictor runs at all -- available regardless of
  // how the rest of this step turns out, matching TransientStepResult's
  // own documented convention.
  result.maxCFL = calculateCFL(mesh_, previousState.massFlux, fluid_.density(), dt).maxCFL;

  const ScalarField previousU = selectComponent(previousState.velocity, VelocityComponent::U);
  const ScalarField previousV = selectComponent(previousState.velocity, VelocityComponent::V);

  // P2-TURB-003: same optional-model/local-LaminarModel-fallback pattern
  // as SIMPLE::solve() -- see this class's own constructor comment. mu_t
  // is corrected against previousState's velocity/pressure (the state
  // this step's own predictor is about to be built from), then mu_eff is
  // used for the transient momentum predictor's diffusion term below.
  std::optional<LaminarModel> defaultTurbulenceModel;
  TurbulenceModel* activeModel = turbulenceModel_;
  if (activeModel == nullptr) {
    defaultTurbulenceModel.emplace(mesh_);
    activeModel = &(*defaultTurbulenceModel);
  }
  std::optional<MomentumAssembly> uAssembly;
  std::optional<MomentumAssembly> vAssembly;
  std::optional<ScalarField> effectiveViscosity;
  try {
    // See SIMPLE::solve()'s identical comment: correct() can legitimately
    // fail for a transport-equation model (e.g. P2-TURB-004's
    // KEpsilonModel), so it runs inside this same try/catch rather than
    // before it.
    activeModel->correct(mesh_, previousState.velocity, previousState.pressure);
    effectiveViscosity = activeModel->effectiveViscosity(fluid_.dynamicViscosity());
    uAssembly = assembleTransientMomentumComponent(
        mesh_, previousState.velocity, previousState.pressure, previousState.massFlux, fluid_,
        *effectiveViscosity, velocityBoundaries_, pressureBoundaries_, VelocityComponent::U,
        previousU, dt);
    vAssembly = assembleTransientMomentumComponent(
        mesh_, previousState.velocity, previousState.pressure, previousState.massFlux, fluid_,
        *effectiveViscosity, velocityBoundaries_, pressureBoundaries_, VelocityComponent::V,
        previousV, dt);
  } catch (const NumericalError&) {
    // uAssembly/vAssembly left empty -- fall through to the check below.
  } catch (const InvalidArgumentError&) {
    // P2-TURB-003: a turbulence model that has produced a non-finite or
    // non-positive mu_eff (rejected by assembleDiffusionContribution's
    // own field-based overload) is a runtime-invalid state, not a
    // configuration error -- treated the same as NonFiniteState below,
    // not left to propagate out of solveTimeStep() uncaught.
  }
  if (!uAssembly.has_value() || !vAssembly.has_value()) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  const auto uResult = momentumSolver->solve(uAssembly->system, toVector(previousU));
  if (!uResult.converged()) {
    return failureResult(TransientStepStatus::MomentumFailure, previousState);
  }
  const auto vResult = momentumSolver->solve(vAssembly->system, toVector(previousV));
  if (!vResult.converged()) {
    return failureResult(TransientStepStatus::MomentumFailure, previousState);
  }

  const VectorField predictorVelocity = combineComponents(uResult.solution, vResult.solution);
  if (!allFinite(predictorVelocity)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }
  const SurfaceField predictorFlux =
      calculateMassFlux(mesh_, predictorVelocity, fluid_, velocityBoundaries_);
  if (!allFinite(predictorFlux)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  // Same response coefficients reused for both corrections below -- no
  // momentum reassembly between them (TODO.md P2 -- PISO-F/G notes).
  const ScalarField dU = computeMomentumResponseCoefficient(mesh_, uAssembly->diagonal);
  const ScalarField dV = computeMomentumResponseCoefficient(mesh_, vAssembly->diagonal);

  // --- Pressure correction #1: assemble from F* -> solve -> apply -------
  std::optional<PressureCorrectionAssembly> pAssemblyOne;
  try {
    pAssemblyOne = assemblePressureCorrection(mesh_, predictorFlux, dU, dV, fluid_.density(),
                                              referenceCell_, pressureBoundaries_);
  } catch (const NumericalError&) {
    // pAssemblyOne left empty -- fall through to the check below.
  }
  if (!pAssemblyOne.has_value()) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }
  const auto pResultOne = pressureSolver->solve(pAssemblyOne->system);
  if (!pResultOne.converged()) {
    return failureResult(TransientStepStatus::PressureCorrectionFailure, previousState);
  }
  const ScalarField pPrimeOne = toScalarField(pResultOne.solution);
  if (!allFinite(pPrimeOne)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  ScalarField p1(mesh_.numberOfCells());
  for (const auto& cell : mesh_.cells()) {
    p1[cell.id()] = previousState.pressure[cell.id()] + pPrimeOne[cell.id()];
  }
  const VectorField u1 =
      correctVelocity(mesh_, predictorVelocity, dU, dV, pPrimeOne, pressureBoundaries_);
  const SurfaceField f1 =
      correctFaceMassFlux(mesh_, predictorFlux, pAssemblyOne->faceCoefficient, pPrimeOne);
  if (!allFinite(p1) || !allFinite(u1) || !allFinite(f1)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  // --- Pressure correction #2: assemble from F1 (never F*) -> solve -> --
  // --- apply --------------------------------------------------------------
  std::optional<PressureCorrectionAssembly> pAssemblyTwo;
  try {
    pAssemblyTwo = assemblePressureCorrection(mesh_, f1, dU, dV, fluid_.density(), referenceCell_,
                                              pressureBoundaries_);
  } catch (const NumericalError&) {
    // pAssemblyTwo left empty -- fall through to the check below.
  }
  if (!pAssemblyTwo.has_value()) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }
  const auto pResultTwo = pressureSolver->solve(pAssemblyTwo->system);
  if (!pResultTwo.converged()) {
    return failureResult(TransientStepStatus::PressureCorrectionFailure, previousState);
  }
  const ScalarField pPrimeTwo = toScalarField(pResultTwo.solution);
  if (!allFinite(pPrimeTwo)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  ScalarField p2(mesh_.numberOfCells());
  for (const auto& cell : mesh_.cells()) {
    p2[cell.id()] = p1[cell.id()] + pPrimeTwo[cell.id()];
  }
  const VectorField u2 = correctVelocity(mesh_, u1, dU, dV, pPrimeTwo, pressureBoundaries_);
  const SurfaceField f2 = correctFaceMassFlux(mesh_, f1, pAssemblyTwo->faceCoefficient, pPrimeTwo);
  if (!allFinite(p2) || !allFinite(u2) || !allFinite(f2)) {
    return failureResult(TransientStepStatus::NonFiniteState, previousState);
  }

  // Final continuity, from the authoritative F2 -- never F1 or the
  // predictor flux (TODO.md P2 -- PISO-G notes).
  const auto finalContinuity = evaluateContinuity(mesh_, f2);

  result.status = TransientStepStatus::Converged;
  result.state.velocity = u2;
  result.state.pressure = p2;
  result.state.massFlux = f2;
  result.continuityResidual = finalContinuity.maxCellImbalance;
  result.massImbalance = std::abs(finalContinuity.globalNetFlux);
  return result;
}

}  // namespace cfd::pressure_velocity
