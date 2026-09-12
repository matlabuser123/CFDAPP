#include "cfd/compressible/CompressibleSIMPLE.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/CompressiblePressureCorrection.hpp"
#include "cfd/compressible/CompressibleRelaxedMomentum.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

namespace cfd::compressible {

using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::evaluateContinuity;
using cfd::physics::VelocityComponent;

namespace {

void validateRelaxation(Real alpha, const char* name) {
  if (!std::isfinite(alpha) || !(alpha > 0.0) || alpha > 1.0) {
    throw InvalidArgumentError(std::string("validateCompressibleSIMPLESettings: ") + name +
                               " must be finite and in (0, 1]");
  }
}

void validateTolerance(Real tolerance, const char* name) {
  if (!std::isfinite(tolerance) || !(tolerance > 0.0)) {
    throw InvalidArgumentError(std::string("validateCompressibleSIMPLESettings: ") + name +
                               " must be finite and > 0");
  }
}

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
  for (Index i = 0; i < field.size(); ++i) sumSquares += field[i] * field[i];
  return std::sqrt(sumSquares / static_cast<Real>(field.size()));
}

ScalarField selectComponent(const VectorField& velocity, VelocityComponent component) {
  ScalarField field(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    field[i] = (component == VelocityComponent::U) ? velocity[i].x : velocity[i].y;
  }
  return field;
}

ScalarField toScalarField(const Vector& v) {
  ScalarField field(v.size());
  for (Index i = 0; i < v.size(); ++i) field[i] = v[i];
  return field;
}

Vector toVector(const ScalarField& field) {
  Vector v(field.size());
  for (Index i = 0; i < field.size(); ++i) v[i] = field[i];
  return v;
}

VectorField combineComponents(const Vector& u, const Vector& v) {
  VectorField result(u.size());
  for (Index i = 0; i < u.size(); ++i) result[i] = Vector2{u[i], v[i]};
  return result;
}

ScalarField absolutePressureField(const ScalarField& gaugePressure, Real referencePressure) {
  ScalarField result(gaugePressure.size());
  for (Index i = 0; i < gaugePressure.size(); ++i) result[i] = referencePressure + gaugePressure[i];
  return result;
}

}  // namespace

void validateCompressibleSIMPLESettings(const CompressibleSIMPLESettings& settings) {
  if (settings.maxIterations == 0) {
    throw InvalidArgumentError("validateCompressibleSIMPLESettings: maxIterations must be > 0");
  }
  validateRelaxation(settings.velocityRelaxation, "velocityRelaxation");
  validateRelaxation(settings.pressureRelaxation, "pressureRelaxation");
  validateTolerance(settings.pseudoTimeStep, "pseudoTimeStep");
  validateTolerance(settings.velocityTolerance, "velocityTolerance");
  validateTolerance(settings.pressureTolerance, "pressureTolerance");
  validateTolerance(settings.continuityTolerance, "continuityTolerance");
}

CompressibleSIMPLE::CompressibleSIMPLE(CompressibleSIMPLESettings settings,
                                       ThermodynamicProperties thermodynamics,
                                       Real referencePressure, Index referenceCell)
    : settings_(std::move(settings)),
      thermodynamics_(std::move(thermodynamics)),
      referencePressure_(referencePressure),
      referenceCell_(referenceCell) {}

const CompressibleSIMPLESettings& CompressibleSIMPLE::settings() const noexcept {
  return settings_;
}
Index CompressibleSIMPLE::referenceCell() const noexcept { return referenceCell_; }

CompressibleSIMPLEResult CompressibleSIMPLE::solve(
    const Mesh& mesh, Real dynamicViscosity, const BoundaryConditionSet& velocityBoundaries,
    const BoundaryConditionSet& pressureBoundaries, const ScalarField& temperature,
    const BoundaryConditionSet* temperatureBoundaries, VectorField initialVelocity,
    ScalarField initialPressure, ScalarField initialDensity) const {
  CompressibleSIMPLEResult result;

  bool configurationValid = true;
  std::unique_ptr<cfd::algebra::LinearSolver> momentumSolver;
  std::unique_ptr<cfd::algebra::LinearSolver> pressureSolver;
  try {
    validateCompressibleSIMPLESettings(settings_);
    if (initialVelocity.size() != mesh.numberOfCells() ||
        initialPressure.size() != mesh.numberOfCells() ||
        initialDensity.size() != mesh.numberOfCells() ||
        temperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "CompressibleSIMPLE::solve: initial/temperature field size does not match mesh cell "
          "count");
    }
    if (referenceCell_ >= mesh.numberOfCells()) {
      throw InvalidArgumentError("CompressibleSIMPLE::solve: referenceCell out of range");
    }
    if (!std::isfinite(referencePressure_) || !(referencePressure_ > 0.0)) {
      throw InvalidArgumentError("CompressibleSIMPLE::solve: referencePressure must be finite "
                                 "and > 0");
    }
    momentumSolver = cfd::algebra::makeLinearSolver(settings_.momentumSolver);
    pressureSolver = cfd::algebra::makeLinearSolver(settings_.pressureSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = CompressibleSIMPLEStatus::InvalidConfiguration;
    result.velocity = std::move(initialVelocity);
    result.pressure = std::move(initialPressure);
    result.density = std::move(initialDensity);
    result.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);
    return result;
  }

  VectorField velocity = std::move(initialVelocity);
  ScalarField pressure = std::move(initialPressure);
  ScalarField density = std::move(initialDensity);
  SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressure, pressureBoundaries,
      referencePressure_, thermodynamics_, temperature, temperatureBoundaries);

  if (!allFinite(velocity) || !allFinite(pressure) || !allFinite(density) ||
      !allFinite(massFlux)) {
    result.status = CompressibleSIMPLEStatus::NonFiniteState;
    result.velocity = std::move(velocity);
    result.pressure = std::move(pressure);
    result.density = std::move(density);
    result.massFlux = std::move(massFlux);
    return result;
  }

  CompressibleSIMPLEStatus finalStatus = CompressibleSIMPLEStatus::MaxIterations;

  for (Index iteration = 0; iteration < settings_.maxIterations; ++iteration) {
    const ScalarField previousU = selectComponent(velocity, VelocityComponent::U);
    const ScalarField previousV = selectComponent(velocity, VelocityComponent::V);

    // Pseudo-transient (dual-time) lagged-coefficient convention (see
    // CompressibleRelaxedMomentum.hpp's own header comment): both
    // densityOld and densityNew are the current best-known density at
    // the start of this iteration -- density becomes genuinely updated
    // (not this same lagged value) only after this iteration's pressure
    // correction, below.
    std::optional<cfd::physics::MomentumAssembly> uAssembly;
    std::optional<cfd::physics::MomentumAssembly> vAssembly;
    try {
      uAssembly = assembleRelaxedCompressibleMomentumComponent(
          mesh, velocity, pressure, massFlux, density, density, dynamicViscosity,
          velocityBoundaries, pressureBoundaries, VelocityComponent::U, previousU,
          settings_.velocityRelaxation, settings_.pseudoTimeStep);
      vAssembly = assembleRelaxedCompressibleMomentumComponent(
          mesh, velocity, pressure, massFlux, density, density, dynamicViscosity,
          velocityBoundaries, pressureBoundaries, VelocityComponent::V, previousV,
          settings_.velocityRelaxation, settings_.pseudoTimeStep);
    } catch (const NumericalError&) {
      // uAssembly/vAssembly left empty -- fall through to the check below.
    }
    if (!uAssembly.has_value() || !vAssembly.has_value()) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    const auto uResult = momentumSolver->solve(uAssembly->system, toVector(previousU));
    if (!uResult.converged()) {
      finalStatus = CompressibleSIMPLEStatus::MomentumFailure;
      break;
    }
    const auto vResult = momentumSolver->solve(vAssembly->system, toVector(previousV));
    if (!vResult.converged()) {
      finalStatus = CompressibleSIMPLEStatus::MomentumFailure;
      break;
    }

    const VectorField velocityStar = combineComponents(uResult.solution, vResult.solution);
    if (!allFinite(velocityStar)) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    // P12-COMP-002: the predictor flux and the pressure-correction D_f
    // coefficient below both derive from the SAME current (pre-
    // correction) density/pressure/temperature state -- calling
    // calculateCompressibleMassFlux here and evaluateCompressibleFaceDensity
    // again below is deterministic, bit-identical redundant computation
    // (a performance-only cost, explicitly out of scope to optimize for
    // this first coupled implementation), not a consistency risk.
    const SurfaceField predictorFlux = calculateCompressibleMassFlux(
        mesh, velocityStar, density, velocityBoundaries, pressure, pressureBoundaries,
        referencePressure_, thermodynamics_, temperature, temperatureBoundaries);
    if (!allFinite(predictorFlux)) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    const ScalarField dU =
        cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, uAssembly->diagonal);
    const ScalarField dV =
        cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, vAssembly->diagonal);

    const SurfaceField faceDensity = evaluateCompressibleFaceDensity(
        mesh, density, pressure, pressureBoundaries, referencePressure_, thermodynamics_,
        temperature, temperatureBoundaries);
    const ScalarField pressureAbsolute = absolutePressureField(pressure, referencePressure_);

    std::optional<cfd::pressure_velocity::PressureCorrectionAssembly> pAssembly;
    try {
      pAssembly = assembleCompressiblePressureCorrection(
          mesh, predictorFlux, faceDensity, dU, dV, pressureAbsolute, temperature, thermodynamics_,
          settings_.pseudoTimeStep, referenceCell_, pressureBoundaries);
    } catch (const NumericalError&) {
      // pAssembly left empty -- fall through to the check below.
    }
    if (!pAssembly.has_value()) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    const auto pResult = pressureSolver->solve(pAssembly->system);
    if (!pResult.converged()) {
      finalStatus = CompressibleSIMPLEStatus::PressureCorrectionFailure;
      break;
    }
    const ScalarField pPrime = toScalarField(pResult.solution);
    if (!allFinite(pPrime)) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    ScalarField pressureNew(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      pressureNew[cell.id()] =
          pressure[cell.id()] + (settings_.pressureRelaxation * pPrime[cell.id()]);
    }

    const VectorField velocityNew = cfd::pressure_velocity::correctVelocity(
        mesh, velocityStar, dU, dV, pPrime, pressureBoundaries);
    const SurfaceField fluxNew = cfd::pressure_velocity::correctFaceMassFlux(
        mesh, predictorFlux, pAssembly->faceCoefficient, pPrime);

    // P12-COMP-002's own core requirement: density is updated via the EOS
    // from the just-corrected pressure -- genuinely iterated state, not a
    // post-hoc read after the loop.
    ScalarField densityNew(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      const Index id = cell.id();
      densityNew[id] = thermodynamics_.density(referencePressure_ + pressureNew[id], temperature[id]);
    }

    if (!allFinite(velocityNew) || !allFinite(pressureNew) || !allFinite(densityNew) ||
        !allFinite(fluxNew)) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

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
    density = densityNew;
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
      finalStatus = CompressibleSIMPLEStatus::Converged;
      break;
    }
  }

  result.status = finalStatus;
  result.velocity = std::move(velocity);
  result.pressure = std::move(pressure);
  result.density = std::move(density);
  result.massFlux = std::move(massFlux);
  return result;
}

}  // namespace cfd::compressible
