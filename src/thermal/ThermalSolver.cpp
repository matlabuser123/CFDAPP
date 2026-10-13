#include "cfd/thermal/ThermalSolver.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/ThermalWallFunction.hpp"
#include "cfd/thermal/TurbulentConductivity.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalInterface.hpp"

namespace cfd::thermal {

using cfd::algebra::BiCGSTAB;
using cfd::algebra::SolverResult;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

namespace {

// Same per-file local helper convention as pressure_velocity::SIMPLE.cpp/
// PISO.cpp/solver::RestartSnapshot.cpp -- Field<T> has no generic
// allFinite() member, so every consumer defines its own tiny check.
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

Real maxAbsoluteDifference(const ScalarField& a, const ScalarField& b) {
  Real maxDiff = 0.0;
  for (Index i = 0; i < a.size(); ++i) {
    maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
  }
  return maxDiff;
}

// Shared outer Picard/fixed-point loop -- see ThermalSolverSettings's own
// header comment for why this loop exists at all. `assemble` is already
// bound to whichever equation (single-material convection-diffusion, or
// CHT region-aware conduction) the caller wants; this function only owns
// the iterate-until-self-consistent machinery, identical either way.
// Preconditions (checked by the caller before this runs): settings are
// valid, a BiCGSTAB solver was already constructed, initialTemperature is
// finite and correctly sized.
ThermalResult runPicardLoop(const ThermalSolverSettings& settings, BiCGSTAB& linearSolver,
                            const ScalarField& initialTemperature,
                            const std::function<EnergyAssembly(const ScalarField&)>& assemble) {
  ThermalResult result;
  ScalarField temperature = initialTemperature;
  ThermalStatus finalStatus = ThermalStatus::MaxIterations;
  Index outerIterationsRun = 0;
  Real referenceResidual = 0.0;  // ||b - A T|| at the start of the first linear solve

  for (Index outer = 0; outer < settings.maxIterations; ++outer) {
    // Sizes and finiteness are already checked (before the loop, and via
    // the allFinite() checks below on every previous iteration's output),
    // so assembly should never actually throw here -- this mirrors
    // SIMPLE.cpp's own defensive try/catch around its (similarly
    // pre-checked) momentum assembly step.
    std::optional<EnergyAssembly> assembly;
    try {
      assembly = assemble(temperature);
    } catch (const NumericalError&) {
      // assembly left empty -- fall through to the check below.
    }
    if (!assembly.has_value()) {
      finalStatus = ThermalStatus::NonFiniteState;
      outerIterationsRun = outer + 1;
      break;
    }

    const SolverResult linearResult = linearSolver.solve(assembly->system, toVector(temperature));
    if (outer == 0) referenceResidual = linearResult.initialResidual;
    result.linearIterations = linearResult.iterations;
    result.initialResidual = linearResult.initialResidual;
    result.finalResidual = linearResult.finalResidual;
    result.residualHistory = linearResult.residualHistory;

    // P12-MESH-003: once the boundary values no longer lag
    // (boundaryDiffusionContribution), a later outer iteration re-solves a
    // system whose solution the previous iteration already produced, and
    // the relative target (of a residual that is already tiny) can be
    // below round-off -- BiCGSTAB then reports Breakdown after having
    // reduced the residual further. Such a breakdown is an attained solve
    // when its residual meets the linear tolerance measured against the
    // ORIGINAL imbalance (the first outer iteration's initial residual);
    // its solution is kept and the outer criterion (max |dT| < tolerance)
    // decides as usual. Every other non-converged solve (and any in the
    // first outer iteration) is still a LinearSolveFailure.
    const bool attainedBreakdown =
        outer > 0 && linearResult.status == cfd::algebra::SolverStatus::Breakdown &&
        std::isfinite(linearResult.finalResidual) &&
        (linearResult.finalResidual <= settings.linearSolver.absoluteTolerance ||
         linearResult.finalResidual <= settings.linearSolver.relativeTolerance * referenceResidual);
    if (!linearResult.converged() && !attainedBreakdown) {
      finalStatus = ThermalStatus::LinearSolveFailure;
      outerIterationsRun = outer + 1;
      break;
    }

    ScalarField newTemperature = toScalarField(linearResult.solution);
    if (!allFinite(newTemperature)) {
      finalStatus = ThermalStatus::NonFiniteState;
      outerIterationsRun = outer + 1;
      break;
    }

    const Real maxChange = maxAbsoluteDifference(newTemperature, temperature);
    result.outerChangeHistory.push_back(maxChange);
    result.maxTemperatureChange = maxChange;
    temperature = std::move(newTemperature);
    outerIterationsRun = outer + 1;

    if (maxChange < settings.tolerance) {
      finalStatus = ThermalStatus::Converged;
      break;
    }
  }

  result.temperature = std::move(temperature);
  result.iterations = outerIterationsRun;
  result.status = finalStatus;
  return result;
}

}  // namespace

ThermalSolver::ThermalSolver(ThermalSolverSettings settings) : settings_(std::move(settings)) {}

const ThermalSolverSettings& ThermalSolver::settings() const noexcept { return settings_; }

ThermalResult ThermalSolver::solve(const Mesh& mesh, const ScalarField& initialTemperature,
                                   const SurfaceField& massFlux, const ThermalProperties& thermal,
                                   const BoundaryConditionSet& temperatureBoundaries,
                                   Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  // Configuration problems are a legitimate solve()-time outcome a caller
  // should branch on via `status`, not an exception escaping solve() --
  // same convention as pressure_velocity::SIMPLE::solve. This includes
  // constructing the BiCGSTAB solver: LinearSolver's own constructor
  // validates its settings (e.g. maxIterations >= 1) and throws
  // InvalidArgumentError, which must be caught here rather than left to
  // propagate out of solve().
  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("ThermalSolver::solve: maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError("ThermalSolver::solve: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: initialTemperature size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: massFlux size does not match mesh face count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature) || !allFinite(massFlux)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  const auto assemble = [&](const ScalarField& temperature) {
    return assembleEnergyEquation(mesh, temperature, massFlux, thermal, temperatureBoundaries,
                                  volumetricHeatSource, settings_.nonOrthogonal);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

ThermalResult ThermalSolver::solve(const Mesh& mesh, const ScalarField& initialTemperature,
                                   const SurfaceField& massFlux, const ThermalProperties& thermal,
                                   const BoundaryConditionSet& temperatureBoundaries,
                                   const ScalarField& volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("ThermalSolver::solve: maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError("ThermalSolver::solve: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: initialTemperature size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: massFlux size does not match mesh face count");
    }
    if (volumetricHeatSource.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: volumetricHeatSource size does not match mesh cell count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature) || !allFinite(massFlux) || !allFinite(volumetricHeatSource)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  const auto assemble = [&](const ScalarField& temperature) {
    return assembleEnergyEquation(mesh, temperature, massFlux, thermal, temperatureBoundaries,
                                  volumetricHeatSource, settings_.nonOrthogonal);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

ThermalResult ThermalSolver::solve(const Mesh& mesh, const ScalarField& initialTemperature,
                                   const SurfaceField& massFlux,
                                   const cfd::physics::TemperatureProperty& conductivityModel,
                                   const cfd::physics::TemperatureProperty& specificHeatModel,
                                   const BoundaryConditionSet& temperatureBoundaries,
                                   Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("ThermalSolver::solve: maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError("ThermalSolver::solve: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: initialTemperature size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: massFlux size does not match mesh face count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature) || !allFinite(massFlux)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  // Re-evaluated fresh every outer iteration from the *current* temperature
  // iterate -- see this overload's own header comment on why this reuses
  // runPicardLoop's existing assemble-lambda mechanism rather than adding a
  // second loop. runPicardLoop only ever catches NumericalError out of
  // `assemble` (turning it into ThermalStatus::NonFiniteState), but
  // evaluatePropertyField signals a rejected (non-finite/non-positive)
  // property value via InvalidArgumentError -- translated to NumericalError
  // right here so a mid-iteration property excursion is reported through
  // the same status channel as any other non-finite assembled state,
  // instead of escaping solve() as an uncaught exception (this overload's
  // own "never throws for a legitimate numerical-failure outcome"
  // convention, same as solve() above).
  const auto assemble = [&](const ScalarField& temperature) -> EnergyAssembly {
    ScalarField conductivity;
    ScalarField specificHeat;
    try {
      conductivity = cfd::physics::evaluatePropertyField(mesh, temperature, conductivityModel);
      specificHeat = cfd::physics::evaluatePropertyField(mesh, temperature, specificHeatModel);
    } catch (const InvalidArgumentError& e) {
      throw NumericalError(std::string("ThermalSolver::solve (variable properties): ") + e.what());
    }
    return assembleEnergyEquation(mesh, temperature, massFlux, conductivity, specificHeat,
                                  temperatureBoundaries, volumetricHeatSource,
                                  settings_.nonOrthogonal);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

ThermalResult ThermalSolver::solve(const Mesh& mesh, const ScalarField& initialTemperature,
                                   const SurfaceField& massFlux,
                                   const cfd::physics::TemperatureProperty& conductivityModel,
                                   const cfd::physics::TemperatureProperty& specificHeatModel,
                                   const BoundaryConditionSet& temperatureBoundaries,
                                   const TurbulentThermalCoupling& turbulence,
                                   Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("ThermalSolver::solve (turbulent): maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError(
          "ThermalSolver::solve (turbulent): tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve (turbulent): initialTemperature size does not match mesh cell "
          "count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve (turbulent): massFlux size does not match mesh face count");
    }
    // The coupling is validated HERE rather than per outer iteration: a null
    // or mis-sized mu_t field is a configuration error, not a numerical
    // excursion, and reporting it as InvalidConfiguration keeps it
    // distinguishable from a mid-solve property failure.
    if (turbulence.turbulentViscosity == nullptr) {
      throw InvalidArgumentError(
          "ThermalSolver::solve (turbulent): turbulentViscosity is null -- a laminar case must "
          "use the overload without a coupling rather than passing an empty one");
    }
    if (turbulence.turbulentViscosity->size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve (turbulent): turbulentViscosity size does not match mesh cell "
          "count");
    }
    if (!std::isfinite(turbulence.turbulentPrandtl) || !(turbulence.turbulentPrandtl > 0.0)) {
      throw InvalidArgumentError(
          "ThermalSolver::solve (turbulent): turbulentPrandtl must be finite and > 0");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature) || !allFinite(massFlux) ||
      !allFinite(*turbulence.turbulentViscosity)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  // The SAME assemble lambda mechanism and the SAME outer Picard loop as the
  // variable-property overload above. The only difference is one line: the
  // conductivity handed to the assembler is k_eff rather than k_mol.
  //
  // k_mol and cp are re-evaluated from the current iterate first, so the
  // turbulent part is formed against THIS iterate's cp -- k_t = mu_t cp(T)/Pr_t
  // -- rather than a reference value. That is the whole reason the coupling
  // lives inside the lambda and not outside the loop.
  // ONE definition of the wall-law evaluation, used by the assembly below AND
  // by the post-solve diagnostic. There is deliberately no second formula.
  struct WallFields {
    cfd::fields::SurfaceField conductance;
    cfd::fields::SurfaceField heatFlux;
  };
  const auto evaluateWallFaces = [&](const ScalarField& temperature,
                                     const ScalarField& specificHeat,
                                     const ScalarField& molecularConductivity) -> WallFields {
    WallFields fields{cfd::fields::SurfaceField(mesh.numberOfFaces(), 0.0),
                      cfd::fields::SurfaceField(mesh.numberOfFaces(), 0.0)};
    for (const Index faceId : *turbulence.wallFaceIds) {
      const auto& face = mesh.face(faceId);
      const Index owner = face.owner();
      const Real frictionVelocity =
          std::sqrt(std::max((*turbulence.wallShearStress)[owner], 0.0) / turbulence.density);
      // Molecular Prandtl from the CURRENT iterate's properties, not a
      // reference pair, and from k_mol rather than k_eff.
      const Real molecularPrandtl =
          turbulence.dynamicViscosity * specificHeat[owner] / molecularConductivity[owner];
      // The same route the assembler takes to the boundary value, including
      // its scalar-BC check.
      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, faceId, temperatureBoundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        throw InvalidArgumentError(
            "ThermalSolver::solve (thermal wall function): wall boundary condition is not "
            "scalar-valued");
      }
      const Real distance =
          cfd::mesh::MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
      const Real wallTemperature = scalarBc->boundaryValue(temperature[owner], distance);

      const auto wall = cfd::thermal::computeThermalWallFace(
          turbulence.density, specificHeat[owner], frictionVelocity, (*turbulence.yPlus)[owner],
          face.area(), temperature[owner], wallTemperature, molecularPrandtl,
          turbulence.turbulentPrandtl, turbulence.wallFunctionCoefficients);
      fields.conductance[faceId] = wall.conductance;
      fields.heatFlux[faceId] = wall.heatFlux;
    }
    return fields;
  };

  const auto assemble = [&](const ScalarField& temperature) -> EnergyAssembly {
    ScalarField conductivity;
    ScalarField specificHeat;
    ScalarField molecularConductivity;
    try {
      conductivity = cfd::physics::evaluatePropertyField(mesh, temperature, conductivityModel);
      specificHeat = cfd::physics::evaluatePropertyField(mesh, temperature, specificHeatModel);
      // Kept before k_eff overwrites it: the wall law needs the MOLECULAR
      // Prandtl number, which is mu cp / k_mol, not mu cp / k_eff.
      molecularConductivity = conductivity;
      conductivity = cfd::thermal::effectiveConductivityField(
          mesh, conductivity, *turbulence.turbulentViscosity, specificHeat,
          turbulence.turbulentPrandtl);
    } catch (const InvalidArgumentError& e) {
      throw NumericalError(std::string("ThermalSolver::solve (turbulent): ") + e.what());
    }

    cfd::fields::SurfaceField wallConductance;
    if (turbulence.thermalWallFunctionActive()) {
      try {
        wallConductance =
            evaluateWallFaces(temperature, specificHeat, molecularConductivity).conductance;
      } catch (const InvalidArgumentError& e) {
        throw NumericalError(std::string("ThermalSolver::solve (thermal wall function): ") +
                             e.what());
      }
    }

    return assembleEnergyEquation(
        mesh, temperature, massFlux, conductivity, specificHeat, temperatureBoundaries,
        volumetricHeatSource, settings_.nonOrthogonal,
        // nullptr when inactive: the identical pre-P16 branch, not a zero field.
        turbulence.thermalWallFunctionActive() ? &wallConductance : nullptr);
  };
  result = runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);

  // P16-TWF -- the production wall result at the CONVERGED temperature, from
  // the same lambda the assembly used. Diagnostics read this; nothing
  // downstream re-derives the wall heat flux.
  if (turbulence.thermalWallFunctionActive()) {
    try {
      const ScalarField finalConductivity =
          cfd::physics::evaluatePropertyField(mesh, result.temperature, conductivityModel);
      const ScalarField finalSpecificHeat =
          cfd::physics::evaluatePropertyField(mesh, result.temperature, specificHeatModel);
      auto fields = evaluateWallFaces(result.temperature, finalSpecificHeat, finalConductivity);
      result.wallConductance = std::move(fields.conductance);
      result.wallHeatFlux = std::move(fields.heatFlux);
    } catch (const InvalidArgumentError&) {
      // A property excursion at the converged state is already reported through
      // `status` by the loop above; leaving the diagnostic empty is preferable
      // to failing a solve that succeeded.
    }
  }
  return result;
}

ThermalResult ThermalSolver::solveConjugateConduction(
    const Mesh& mesh, const ScalarField& initialTemperature, const ThermalRegionMap& regions,
    const BoundaryConditionSet& temperatureBoundaries, Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: maxIterations must be "
          ">= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: initialTemperature size does not match mesh "
          "cell count");
    }
    if (regions.numberOfCells() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: regions size does not match mesh cell count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  const auto assemble = [&](const ScalarField& temperature) {
    return assembleConjugateConductionEquation(mesh, temperature, regions, temperatureBoundaries,
                                               volumetricHeatSource);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

}  // namespace cfd::thermal
