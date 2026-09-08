#include "cfd/pressure_velocity/SIMPLE.hpp"

#include <cmath>
#include <optional>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"

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

SIMPLE::SIMPLE(SIMPLESettings settings, Index referenceCell)
    : settings_(std::move(settings)), referenceCell_(referenceCell) {}

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
  // includes constructing the two BiCGSTAB solvers: LinearSolver's own
  // constructor validates its settings (e.g. maxIterations >= 1) and
  // throws InvalidArgumentError, which must be caught here rather than
  // left to propagate out of solve().
  bool configurationValid = true;
  std::optional<BiCGSTAB> momentumSolver;
  std::optional<BiCGSTAB> pressureSolver;
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
    momentumSolver.emplace(settings_.momentumSolver);
    pressureSolver.emplace(settings_.pressureSolver);
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

  SIMPLEStatus finalStatus = SIMPLEStatus::MaxIterations;

  for (Index iteration = 0; iteration < settings_.maxIterations; ++iteration) {
    const ScalarField previousU = selectComponent(velocity, VelocityComponent::U);
    const ScalarField previousV = selectComponent(velocity, VelocityComponent::V);

    std::optional<MomentumAssembly> uAssembly;
    std::optional<MomentumAssembly> vAssembly;
    try {
      uAssembly = assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
          VelocityComponent::U, previousU, settings_.velocityRelaxation);
      vAssembly = assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
          VelocityComponent::V, previousV, settings_.velocityRelaxation);
    } catch (const NumericalError&) {
      // uAssembly/vAssembly left empty -- fall through to the check below.
    }
    if (!uAssembly.has_value() || !vAssembly.has_value()) {
      finalStatus = SIMPLEStatus::NonFiniteState;
      break;
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

    const bool converged = (uResidual <= settings_.velocityTolerance) &&
                           (vResidual <= settings_.velocityTolerance) &&
                           (pResidual <= settings_.pressureTolerance) &&
                           (continuityResidual <= settings_.continuityTolerance) &&
                           (globalImbalance <= settings_.continuityTolerance);
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
