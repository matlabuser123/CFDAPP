#include "cfd/compressible/CompressibleSIMPLE.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSolverFallback.hpp"
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
  cfd::solver::validateSolverRobustnessSettings(settings.robustness, settings.velocityRelaxation,
                                                settings.pressureRelaxation);
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

void CompressibleSIMPLE::setMomentumSource(const VectorField* source) noexcept {
  momentumSource_ = source;
}
const VectorField* CompressibleSIMPLE::momentumSource() const noexcept { return momentumSource_; }

CompressibleSIMPLEResult CompressibleSIMPLE::solve(
    const Mesh& mesh, Real dynamicViscosity, const BoundaryConditionSet& velocityBoundaries,
    const BoundaryConditionSet& pressureBoundaries, const ScalarField& temperature,
    const BoundaryConditionSet* temperatureBoundaries, VectorField initialVelocity,
    ScalarField initialPressure, ScalarField initialDensity) const {
  // P12-MESH-006: the compressible solver is two-dimensional (u, v); refused
  // explicitly on a 3D mesh now that the shared momentum/pressure components
  // accept one.
  cfd::mesh::requireTwoDimensional(mesh, "CompressibleSIMPLE");
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
      throw InvalidArgumentError(
          "CompressibleSIMPLE::solve: referencePressure must be finite "
          "and > 0");
    }
    // P12-NUM-006: the optional prescribed momentum source.
    if (momentumSource_ != nullptr &&
        (momentumSource_->size() != mesh.numberOfCells() || !allFinite(*momentumSource_))) {
      throw InvalidArgumentError(
          "CompressibleSIMPLE::solve: momentum source must match the mesh cell count and be "
          "finite");
    }
    // P12-NUM-004: exactly makeLinearSolver(...) unless the fallback policy
    // is enabled (see SIMPLE.cpp).
    momentumSolver = cfd::algebra::makeLinearSolverWithFallback(
        settings_.momentumSolver, settings_.robustness.linearSolverFallback);
    pressureSolver = cfd::algebra::makeLinearSolverWithFallback(
        settings_.pressureSolver, settings_.robustness.linearSolverFallback);
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
  // P12-NUM-006 (found by the CompressibleSIMPLE MMS): the EOS rejects a
  // non-positive absolute pressure / temperature with InvalidArgumentError.
  // solve() throws nothing itself, so an initial state outside the EOS
  // domain is an InvalidConfiguration, reported with the EOS message.
  SurfaceField massFlux;
  try {
    massFlux = calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressure,
                                             pressureBoundaries, referencePressure_,
                                             thermodynamics_, temperature, temperatureBoundaries);
  } catch (const InvalidArgumentError& e) {
    result.status = CompressibleSIMPLEStatus::InvalidConfiguration;
    result.robustness.statusDetail = std::string("initial state rejected: ") + e.what();
    result.velocity = std::move(velocity);
    result.pressure = std::move(pressure);
    result.density = std::move(density);
    result.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);
    return result;
  }

  if (!allFinite(velocity) || !allFinite(pressure) || !allFinite(density) || !allFinite(massFlux)) {
    result.status = CompressibleSIMPLEStatus::NonFiniteState;
    result.velocity = std::move(velocity);
    result.pressure = std::move(pressure);
    result.density = std::move(density);
    result.massFlux = std::move(massFlux);
    return result;
  }

  CompressibleSIMPLEStatus finalStatus = CompressibleSIMPLEStatus::MaxIterations;

  // P12-NUM-004: the same shared outer-iteration monitor as SIMPLE (no
  // turbulence residual here; the turbulence tolerance is unused).
  cfd::solver::OuterIterationMonitor monitor(
      settings_.robustness,
      cfd::solver::OuterConvergenceTolerances{settings_.velocityTolerance,
                                              settings_.pressureTolerance,
                                              settings_.continuityTolerance, 1.0},
      settings_.velocityRelaxation, settings_.pressureRelaxation);
  const auto failLinearSolve = [&](std::string_view equation,
                                   const cfd::algebra::LinearSolverSettings& solverSettings,
                                   const cfd::algebra::SolverResult& failed, Index iteration) {
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), iteration, equation,
                                            failed.fallback);
    monitor.diagnostics().statusDetail =
        cfd::solver::describeLinearSolveFailure(equation, solverSettings.type, failed);
  };
  // P12-NUM-003: see CompressibleSIMPLESettings::nonOrthogonalCorrections.
  const cfd::discretization::NonOrthogonalCorrectionOptions nonOrthogonal{
      settings_.nonOrthogonalCorrections > 0, settings_.gradientScheme};

  for (Index iteration = 0; iteration < settings_.maxIterations; ++iteration) {
    const ScalarField previousU = selectComponent(velocity, VelocityComponent::U);
    const ScalarField previousV = selectComponent(velocity, VelocityComponent::V);
    // P12-NUM-004: fixed for this whole iteration (see SIMPLE.cpp).
    const cfd::solver::RelaxationFactors relaxation = monitor.relaxation();
    const Index outerIteration = iteration + 1;

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
          relaxation.velocity, settings_.pseudoTimeStep, nonOrthogonal, settings_.gradientScheme,
          momentumSource_);
      vAssembly = assembleRelaxedCompressibleMomentumComponent(
          mesh, velocity, pressure, massFlux, density, density, dynamicViscosity,
          velocityBoundaries, pressureBoundaries, VelocityComponent::V, previousV,
          relaxation.velocity, settings_.pseudoTimeStep, nonOrthogonal, settings_.gradientScheme,
          momentumSource_);
    } catch (const NumericalError&) {
      // uAssembly/vAssembly left empty -- fall through to the check below.
    }
    if (!uAssembly.has_value() || !vAssembly.has_value()) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    const auto uResult = momentumSolver->solve(uAssembly->system, toVector(previousU));
    if (!uResult.converged()) {
      failLinearSolve("u-momentum", settings_.momentumSolver, uResult, outerIteration);
      finalStatus = CompressibleSIMPLEStatus::MomentumFailure;
      break;
    }
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration, "u-momentum",
                                            uResult.fallback);
    const auto vResult = momentumSolver->solve(vAssembly->system, toVector(previousV));
    if (!vResult.converged()) {
      failLinearSolve("v-momentum", settings_.momentumSolver, vResult, outerIteration);
      finalStatus = CompressibleSIMPLEStatus::MomentumFailure;
      break;
    }
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration, "v-momentum",
                                            vResult.fallback);

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
    // P12-NUM-006: an EOS evaluation of an iterated state outside the EOS
    // domain (non-positive absolute pressure, e.g. a diverging pressure
    // transient) is a non-physical runtime state -- reported as
    // NonFiniteState with the EOS message (the same classification SIMPLE
    // gives a non-positive mu_eff), never thrown out of solve().
    SurfaceField predictorFlux;
    SurfaceField faceDensity;
    try {
      predictorFlux = calculateCompressibleMassFlux(
          mesh, velocityStar, density, velocityBoundaries, pressure, pressureBoundaries,
          referencePressure_, thermodynamics_, temperature, temperatureBoundaries);
      faceDensity = evaluateCompressibleFaceDensity(mesh, density, pressure, pressureBoundaries,
                                                    referencePressure_, thermodynamics_,
                                                    temperature, temperatureBoundaries);
    } catch (const InvalidArgumentError& e) {
      monitor.diagnostics().statusDetail =
          std::string("non-physical thermodynamic state: ") + e.what();
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }
    if (!allFinite(predictorFlux)) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    const ScalarField dU =
        cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, uAssembly->diagonal);
    const ScalarField dV =
        cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, vAssembly->diagonal);

    const ScalarField pressureAbsolute = absolutePressureField(pressure, referencePressure_);

    // P12-NUM-003: the same non-orthogonal pressure-correction corrector
    // loop as SIMPLE (see SIMPLE.cpp's own comment): pass 1 over-relaxed
    // (N >= 1) or two-point (N = 0) with no explicit term, passes 2..N with
    // the explicit -rho_f T . grad(p'_{k-1}) term; p' applied once, after
    // the last pass, with the flux correction using that assembly's own
    // explicit face terms.
    cfd::pressure_velocity::PressureCorrectionOptions pressureOptions;
    pressureOptions.nonOrthogonal = settings_.nonOrthogonalCorrections > 0;
    pressureOptions.gradientScheme = settings_.gradientScheme;

    std::optional<cfd::pressure_velocity::PressureCorrectionAssembly> pAssembly;
    try {
      pAssembly = assembleCompressiblePressureCorrection(
          mesh, predictorFlux, faceDensity, dU, dV, pressureAbsolute, temperature, thermodynamics_,
          settings_.pseudoTimeStep, referenceCell_, pressureBoundaries, pressureOptions);
    } catch (const NumericalError&) {
      // pAssembly left empty -- fall through to the check below.
    } catch (const InvalidArgumentError& e) {
      // P12-NUM-006: the EOS derivative of a non-physical state (see above).
      monitor.diagnostics().statusDetail =
          std::string("non-physical thermodynamic state: ") + e.what();
    }
    if (!pAssembly.has_value()) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }

    const auto pResult = pressureSolver->solve(pAssembly->system);
    if (!pResult.converged()) {
      failLinearSolve("pressure-correction", settings_.pressureSolver, pResult, outerIteration);
      finalStatus = CompressibleSIMPLEStatus::PressureCorrectionFailure;
      break;
    }
    cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration,
                                            "pressure-correction", pResult.fallback);
    ScalarField pPrime = toScalarField(pResult.solution);
    if (!allFinite(pPrime)) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
    }
    ++result.pressureCorrectionPasses;

    bool pressurePassFailed = false;
    for (Index pass = 1; pass < settings_.nonOrthogonalCorrections; ++pass) {
      const ScalarField previousPPrime = pPrime;
      pressureOptions.previousPressureCorrection = &previousPPrime;
      std::optional<cfd::pressure_velocity::PressureCorrectionAssembly> passAssembly;
      try {
        passAssembly = assembleCompressiblePressureCorrection(
            mesh, predictorFlux, faceDensity, dU, dV, pressureAbsolute, temperature,
            thermodynamics_, settings_.pseudoTimeStep, referenceCell_, pressureBoundaries,
            pressureOptions);
      } catch (const NumericalError&) {
        // passAssembly left empty -- fall through to the check below.
      }
      pressureOptions.previousPressureCorrection = nullptr;
      if (!passAssembly.has_value()) {
        finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
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
      const auto passResult = pressureSolver->solve(passAssembly->system);
      if (!passResult.converged()) {
        failLinearSolve("pressure-correction-pass", settings_.pressureSolver, passResult,
                        outerIteration);
        finalStatus = CompressibleSIMPLEStatus::PressureCorrectionFailure;
        pressurePassFailed = true;
        break;
      }
      cfd::solver::recordLinearSolverFallback(monitor.diagnostics(), outerIteration,
                                              "pressure-correction-pass", passResult.fallback);
      pPrime = toScalarField(passResult.solution);
      if (!allFinite(pPrime)) {
        finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
        pressurePassFailed = true;
        break;
      }
      pAssembly = std::move(passAssembly);
      ++result.pressureCorrectionPasses;
    }
    if (pressurePassFailed) {
      break;
    }

    ScalarField pressureNew(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      pressureNew[cell.id()] = pressure[cell.id()] + (relaxation.pressure * pPrime[cell.id()]);
    }

    const VectorField velocityNew = cfd::pressure_velocity::correctVelocity(
        mesh, velocityStar, dU, dV, pPrime, pressureBoundaries, settings_.gradientScheme);
    const SurfaceField fluxNew = cfd::pressure_velocity::correctFaceMassFlux(
        mesh, predictorFlux, pAssembly->faceCoefficient, pPrime,
        (settings_.nonOrthogonalCorrections > 1) ? &pAssembly->explicitFaceFlux : nullptr);

    // P12-COMP-002's own core requirement: density is updated via the EOS
    // from the just-corrected pressure -- genuinely iterated state, not a
    // post-hoc read after the loop.
    ScalarField densityNew(mesh.numberOfCells());
    bool thermodynamicStateValid = true;
    try {
      for (const auto& cell : mesh.cells()) {
        const Index id = cell.id();
        densityNew[id] =
            thermodynamics_.density(referencePressure_ + pressureNew[id], temperature[id]);
      }
    } catch (const InvalidArgumentError& e) {
      // P12-NUM-006: the corrected pressure left the EOS domain (see above).
      monitor.diagnostics().statusDetail =
          std::string("non-physical thermodynamic state: ") + e.what();
      thermodynamicStateValid = false;
    }
    if (!thermodynamicStateValid) {
      finalStatus = CompressibleSIMPLEStatus::NonFiniteState;
      break;
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

    // P12-NUM-004: the convergence gate (default: exactly the
    // pre-P12-NUM-004 absolute comparisons u, v <= velocityTolerance,
    // p <= pressureTolerance, continuity and global imbalance <=
    // continuityTolerance), then -- only if enabled -- divergence,
    // stagnation and the relaxation update (see SIMPLE.cpp).
    const cfd::solver::OuterIterationVerdict verdict =
        monitor.record(cfd::solver::OuterResidualSample{
            uResidual, vResidual, pResidual, continuityResidual, globalImbalance, std::nullopt});
    if (verdict == cfd::solver::OuterIterationVerdict::Converged) {
      finalStatus = CompressibleSIMPLEStatus::Converged;
      break;
    }
    if (verdict == cfd::solver::OuterIterationVerdict::Diverging) {
      finalStatus = CompressibleSIMPLEStatus::Diverging;
      break;
    }
    if (verdict == cfd::solver::OuterIterationVerdict::Stagnated) {
      finalStatus = CompressibleSIMPLEStatus::Stagnated;
      break;
    }
  }

  result.status = finalStatus;
  result.robustness = monitor.takeDiagnostics();
  result.velocity = std::move(velocity);
  result.pressure = std::move(pressure);
  result.density = std::move(density);
  result.massFlux = std::move(massFlux);
  return result;
}

}  // namespace cfd::compressible
