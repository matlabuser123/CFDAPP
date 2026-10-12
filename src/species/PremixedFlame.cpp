// G8 closure, step 4 -- the steady 1D premixed flame. See PremixedFlame.hpp for the formulation,
// the eigenvalue argument, and the limitations.
#include "cfd/species/PremixedFlame.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/SpeciesTransport.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

namespace cfd::species {
namespace {

using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

std::string exact(Real value) {
  std::ostringstream out;
  out.precision(17);
  out << value;
  return out.str();
}

Vector toVector(const ScalarField& field) {
  Vector v(field.size());
  for (Index i = 0; i < field.size(); ++i) {
    v[i] = field[i];
  }
  return v;
}

void requirePositiveFinite(Real value, const char* name) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("solvePremixedFlame: ") + name + " is " + exact(value) +
                               "; must be finite and > 0");
  }
}

// The one-step restriction, in one place. The stoichiometric helpers below (burnt composition,
// adiabatic temperature) are written for a single global reaction, which is what G8 freezes; a
// multi-step mechanism needs an equilibrium or element-balance route to both, and inventing one
// here on speculation is exactly the scope creep CLAUDE.md section 2 forbids.
const Reaction& requireSingleReaction(const ReactionMechanism& mechanism, const char* where) {
  if (mechanism.reactionCount() != 1) {
    throw InvalidArgumentError(
        std::string(where) + ": the mechanism holds " + std::to_string(mechanism.reactionCount()) +
        " reactions. The burnt composition and the adiabatic temperature are derived from a "
        "SINGLE global reaction's stoichiometry here; a multi-step mechanism needs an equilibrium "
        "or element-balance route to both, which this phase does not build.");
  }
  return mechanism.reaction(0);
}

void validateChemistry(const PremixedFlameChemistry& chemistry, const char* where) {
  if (chemistry.mechanism == nullptr || chemistry.mixture == nullptr ||
      chemistry.formation == nullptr || chemistry.kinetics == nullptr) {
    throw InvalidArgumentError(std::string(where) +
                               ": the chemistry carries a null pointer. All four of mechanism, "
                               "mixture, formation and kinetics are required.");
  }
}

void validateConditions(const PremixedFlameChemistry& chemistry,
                        const PremixedFlameConditions& conditions, const char* where) {
  validateChemistry(chemistry, where);
  const Index ns = chemistry.mechanism->speciesCount();

  if (conditions.unburntComposition.size() != ns) {
    throw InvalidArgumentError(std::string(where) + ": the unburnt composition holds " +
                               std::to_string(conditions.unburntComposition.size()) +
                               " entries; the mechanism holds " + std::to_string(ns) + " species");
  }
  if (conditions.conductivity == nullptr) {
    throw InvalidArgumentError(
        std::string(where) +
        ": conditions.conductivity is null. lambda(T) has NO DEFAULT on purpose -- S_L ~ "
        "sqrt(lambda), so a defaulted constant would be an uncited number standing in the most "
        "sensitive position of the whole measurement. Supply a cited model, a ConstantProperty "
        "included.");
  }
  requirePositiveFinite(conditions.pressure, "pressure");
  requirePositiveFinite(conditions.unburntTemperature, "unburntTemperature");
  requirePositiveFinite(conditions.specificHeat, "specificHeat");
  requirePositiveFinite(conditions.lewisNumber, "lewisNumber");

  if (conditions.fuelIndex >= ns) {
    throw InvalidArgumentError(std::string(where) + ": fuelIndex is " +
                               std::to_string(conditions.fuelIndex) + "; the mechanism holds " +
                               std::to_string(ns) + " species");
  }
  const Reaction& reaction = requireSingleReaction(*chemistry.mechanism, where);
  if (!(reaction.reactantCoefficient(conditions.fuelIndex) > 0.0)) {
    throw InvalidArgumentError(
        std::string(where) + ": species \"" +
        chemistry.mechanism->registry().name(conditions.fuelIndex) +
        "\" was named as the fuel but is not a REACTANT of \"" + reaction.name() +
        "\". The consumption integral and the burnt composition are both taken on the fuel, so "
        "naming a product or a diluent would silently measure the wrong quantity.");
  }
  for (Index i = 0; i < ns; ++i) {
    if (!std::isfinite(conditions.unburntComposition[i])) {
      throw InvalidArgumentError(std::string(where) + ": the unburnt mass fraction of species \"" +
                                 chemistry.mechanism->registry().name(i) + "\" is " +
                                 exact(conditions.unburntComposition[i]));
    }
  }
}

void validateSettings(const PremixedFlameSettings& settings) {
  if (settings.cells < 8) {
    throw InvalidArgumentError("solvePremixedFlame: cells is " + std::to_string(settings.cells) +
                               "; a flame front cannot be represented on fewer than 8 cells");
  }
  requirePositiveFinite(settings.domainLength, "domainLength");
  requirePositiveFinite(settings.tolerance, "tolerance");
  requirePositiveFinite(settings.cellCourant, "cellCourant");
  requirePositiveFinite(settings.maximumCellDiffusionNumber, "maximumCellDiffusionNumber");
  requirePositiveFinite(settings.driftSettleTolerance, "driftSettleTolerance");
  requirePositiveFinite(settings.relaxationTimesPerTrial, "relaxationTimesPerTrial");
  requirePositiveFinite(settings.finalMarchRelaxationTimes, "finalMarchRelaxationTimes");
  requirePositiveFinite(settings.seedWidthFactor, "seedWidthFactor");
  requirePositiveFinite(settings.startupTimeStepDivisor, "startupTimeStepDivisor");
  if (!(settings.recentreDeadBandCells >= 1.0)) {
    throw InvalidArgumentError(
        "solvePremixedFlame: recentreDeadBandCells is " + exact(settings.recentreDeadBandCells) +
        "; must be >= 1. Below one cell the front is translated on essentially every step, and the "
        "translation is not conservative -- it leaks fresh fuel off the upstream end.");
  }
  if (!(settings.timeStepGrowth > 1.0)) {
    throw InvalidArgumentError(
        "solvePremixedFlame: timeStepGrowth is " + exact(settings.timeStepGrowth) +
        "; must be > 1, or dt could never reach its target and no drift window would ever open");
  }
  if (settings.driftWindow == 0 || settings.maximumStepsPerTrial == 0 ||
      settings.settledWindowsRequired == 0) {
    throw InvalidArgumentError(
        "solvePremixedFlame: driftWindow, settledWindowsRequired and maximumStepsPerTrial must all "
        "be >= 1; a trial that cannot measure a window cannot decide a side");
  }
  if (settings.maxOuterIterations == 0) {
    throw InvalidArgumentError("solvePremixedFlame: maxOuterIterations is 0");
  }
  if (settings.maximumBracketSteps == 0) {
    throw InvalidArgumentError(
        "solvePremixedFlame: maximumBracketSteps is 0, so the eigenvalue could never be "
        "bracketed and the seed would be returned unchanged -- which is exactly the vacuous "
        "result this solver is built to avoid");
  }
  const auto inOpenUnitInterval = [](Real v) { return std::isfinite(v) && v > 0.0 && v < 1.0; };
  if (!inOpenUnitInterval(settings.anchorFraction)) {
    throw InvalidArgumentError("solvePremixedFlame: anchorFraction is " +
                               exact(settings.anchorFraction) + "; must be within (0, 1)");
  }
  if (!inOpenUnitInterval(settings.anchorTemperatureFraction)) {
    throw InvalidArgumentError("solvePremixedFlame: anchorTemperatureFraction is " +
                               exact(settings.anchorTemperatureFraction) +
                               "; must be within (0, 1)");
  }
  if (!inOpenUnitInterval(settings.temperatureRelaxation) &&
      settings.temperatureRelaxation != 1.0) {
    throw InvalidArgumentError("solvePremixedFlame: temperatureRelaxation is " +
                               exact(settings.temperatureRelaxation) + "; must be within (0, 1]");
  }
  if (!inOpenUnitInterval(settings.compositionRelaxation) &&
      settings.compositionRelaxation != 1.0) {
    throw InvalidArgumentError("solvePremixedFlame: compositionRelaxation is " +
                               exact(settings.compositionRelaxation) + "; must be within (0, 1]");
  }
}

// =============================================================================================
// THE CHEMISTRY SUB-STEP OF THE OPERATOR SPLITTING -- at CONSTANT PRESSURE.
// =============================================================================================
//
// WHY THIS EXISTS AND THE EXPLICIT SOURCE DOES NOT. The first version added the heat release
// straight to the energy equation's right-hand side, which is an EXPLICIT treatment of a stiff
// exothermic source. One step then injects
//
//     Delta T = S_E dt / (rho cp)
//
// and with S_E = 5.5e10 W/m^3 and dt = 9.4e-4 s that is 2.2e5 K in a single step. Measured: peak
// temperatures of 17646 K, 38511 K and 144482 K against an adiabatic 1800 K -- 10x to 80x a bound
// an adiabatic flame cannot exceed at all. The step was ~50 chemical times, and no explicit
// treatment of an Arrhenius source survives that. Linearising it implicitly does not help either:
// dS_E/dT > 0, so the Jacobian term SUBTRACTS from the diagonal and destabilises the matrix. The
// instability is the physical one of ignition, not an artefact of the discretisation.
//
// So transport and chemistry are SPLIT: transport is solved implicitly with no reaction at all,
// then the chemistry is advanced over the same dt with its own adaptive sub-stepping. dt is then
// free of the chemical time, which is the entire point -- bounding dt by tau_c instead would have
// needed of order 38000 steps per trial.
//
// WHY NOT ChemistryIntegrator, which already does adaptive sub-stepping. Because it is a
// CONSTANT-DENSITY reactor: it is built on P18.6's advanceClosedReactor, which conserves sensible
// ENERGY at fixed rho and recovers T through cv = cp - R. A flame at constant pressure must
// conserve ENTHALPY, giving Delta T = q/(rho cp) rather than q/(rho cv), and cp/cv = gamma ~ 1.3
// here -- so using it unmodified would inflate every temperature rise by about 30% and move the
// adiabatic flame temperature that the frozen cp exists to pin. Extending ChemistryIntegrator with
// a constant-pressure closure is the better long-term shape and is recorded as such; it is not
// done here because advanceClosedReactor is already-qualified P18.6 code.
//
// THE RATES COME FROM THE PRODUCTION KINETICS PATH, cell by cell, so this shares its rate law with
// the gated mechanism instead of re-deriving it. Only the sub-step control and the
// constant-pressure temperature update are local.
struct ChemistrySplitOutcome {
  std::size_t subSteps{};
  Real smallestSubStep{};
  bool failed{false};
  std::string message;
};

ChemistrySplitOutcome advanceChemistryAtConstantPressure(
    const ReactionMechanism& mechanism, const MixtureThermodynamics& mixture,
    const FormationEnthalpies& formation, const ReactionKinetics& kinetics, Real pressure,
    Real specificHeat, Real dt, Real maximumFractionalChange, std::size_t maximumSubSteps,
    std::vector<cfd::fields::ScalarField>& composition, cfd::fields::ScalarField& temperature,
    cfd::fields::ScalarField& density) {
  ChemistrySplitOutcome outcome;
  outcome.smallestSubStep = std::numeric_limits<Real>::infinity();

  const Index ns = mechanism.speciesCount();
  const Index cells = temperature.size();

  // One-cell working fields, allocated ONCE and reused for every cell and sub-step.
  std::vector<cfd::fields::ScalarField> cellComposition(ns, cfd::fields::ScalarField(1));
  cfd::fields::ScalarField cellTemperature(1);
  cfd::fields::ScalarField cellDensity(1);
  std::vector<Real> bulk(ns);

  for (Index c = 0; c < cells; ++c) {
    for (Index i = 0; i < ns; ++i) {
      cellComposition[i][0] = composition[i][c];
    }
    cellTemperature[0] = temperature[c];
    cellDensity[0] = density[c];

    Real remaining = dt;
    while (remaining > 0.0) {
      if (outcome.subSteps >= maximumSubSteps) {
        outcome.failed = true;
        outcome.message = "the chemistry sub-step budget of " + std::to_string(maximumSubSteps) +
                          " was exhausted at cell " + std::to_string(c) + " with " +
                          exact(remaining) + " s of " + exact(dt) + " left";
        return outcome;
      }

      const auto rates = arrheniusProgressRatesFromFields(
          mechanism, kinetics, mixture, cellComposition, cellDensity, cellTemperature,
          NegativeConcentrationPolicy::ZeroRateWhereNegative);
      const SpeciesSources sources = reactionSpeciesSources(mechanism, mixture, rates, 1);
      const cfd::fields::ScalarField heat =
          reactionHeatRelease(mechanism, mixture, formation, rates, 1);

      // tau = rho Y_i / |omega_i| over the CONSUMED species only. A species being produced places
      // no bound on the step -- it cannot run out.
      Real tau = std::numeric_limits<Real>::infinity();
      bool reacting = false;
      for (Index i = 0; i < ns; ++i) {
        const Real omega = sources.perSpecies[i].Su[0];
        if (!(omega < 0.0)) {
          continue;
        }
        reacting = true;
        const Real mass = cellDensity[0] * cellComposition[i][0];
        if (mass <= 0.0) {
          continue;
        }
        tau = std::min(tau, mass / std::abs(omega));
      }

      Real step = remaining;
      if (reacting && std::isfinite(tau)) {
        step = std::min(step, maximumFractionalChange * tau);
      }
      if (!(step > 0.0)) {
        // Nothing can advance: leave the rest of this cell's dt unintegrated rather than loop.
        outcome.failed = true;
        outcome.message = "the chemistry sub-step collapsed to " + exact(step) + " at cell " +
                          std::to_string(c) + " (tau = " + exact(tau) +
                          ", T = " + exact(cellTemperature[0]) + ")";
        return outcome;
      }

      for (Index i = 0; i < ns; ++i) {
        cellComposition[i][0] += step * sources.perSpecies[i].Su[0] / cellDensity[0];
      }
      // CONSTANT PRESSURE: the enthalpy balance, with the same constant cp and the same formation
      // enthalpies the energy equation uses, so the two agree on T_ad by construction.
      cellTemperature[0] += step * heat[0] / (cellDensity[0] * specificHeat);
      if (!std::isfinite(cellTemperature[0]) || !(cellTemperature[0] > 0.0)) {
        outcome.failed = true;
        outcome.message = "the chemistry sub-step drove the temperature at cell " +
                          std::to_string(c) + " to " + exact(cellTemperature[0]);
        return outcome;
      }

      // rho follows from the ideal gas law at the FIXED pressure -- the gas expands as it burns,
      // which is what makes this a constant-pressure step rather than a constant-volume one.
      for (Index i = 0; i < ns; ++i) {
        bulk[i] = cellComposition[i][0];
      }
      cellDensity[0] = mixture.density(bulk, pressure, cellTemperature[0]);
      if (!std::isfinite(cellDensity[0]) || !(cellDensity[0] > 0.0)) {
        outcome.failed = true;
        outcome.message = "the chemistry sub-step drove the density at cell " + std::to_string(c) +
                          " to " + exact(cellDensity[0]);
        return outcome;
      }

      outcome.smallestSubStep = std::min(outcome.smallestSubStep, step);
      ++outcome.subSteps;
      remaining -= step;
    }

    for (Index i = 0; i < ns; ++i) {
      composition[i][c] = cellComposition[i][0];
    }
    temperature[c] = cellTemperature[0];
    density[c] = cellDensity[0];
  }

  if (!std::isfinite(outcome.smallestSubStep)) {
    outcome.smallestSubStep = 0.0;
  }
  return outcome;
}

}  // namespace

// ===================================================================================
// The stoichiometric helpers
// ===================================================================================

std::vector<Real> flameBurntComposition(const PremixedFlameChemistry& chemistry,
                                        const PremixedFlameConditions& conditions) {
  validateConditions(chemistry, conditions, "flameBurntComposition");

  const ReactionMechanism& mechanism = *chemistry.mechanism;
  const MixtureThermodynamics& mixture = *chemistry.mixture;
  const Reaction& reaction = mechanism.reaction(0);

  const Real nuFuel = reaction.reactantCoefficient(conditions.fuelIndex);
  const Real mFuel = mixture.species(conditions.fuelIndex).molecularWeight();

  // xi = moles of reaction per kg of mixture needed to consume ALL the fuel. The fuel's own term
  // then gives Y_F,b = Y_F,u - (Y_F,u / (nu_F M_F)) nu_F M_F = 0 exactly, which is the check that
  // the expression is the right way up.
  const Real xi = conditions.unburntComposition[conditions.fuelIndex] / (nuFuel * mFuel);

  std::vector<Real> burnt = conditions.unburntComposition;
  for (Index i = 0; i < mechanism.speciesCount(); ++i) {
    burnt[i] += xi * reaction.netStoichiometry(i) * mixture.species(i).molecularWeight();
  }
  return burnt;
}

Real flameAdiabaticTemperature(const PremixedFlameChemistry& chemistry,
                               const PremixedFlameConditions& conditions) {
  validateConditions(chemistry, conditions, "flameAdiabaticTemperature");

  const ReactionMechanism& mechanism = *chemistry.mechanism;
  const MixtureThermodynamics& mixture = *chemistry.mixture;
  const Reaction& reaction = mechanism.reaction(0);

  // Delta H_r^0 [J/mol], negative for an exothermic reaction.
  const Real deltaH = standardReactionEnthalpy(reaction, mixture, *chemistry.formation);
  const Real nuFuel = reaction.reactantCoefficient(conditions.fuelIndex);
  const Real mFuel = mixture.species(conditions.fuelIndex).molecularWeight();
  const Real yFuel = conditions.unburntComposition[conditions.fuelIndex];

  // This is the SAME balance the solved energy equation satisfies, which is the point: integrating
  // mdot cp dT/dx = S_E across the whole front gives cp (T_b - T_u) = -Delta H_r Y_F,u/(nu_F M_F),
  // so the solved peak temperature must approach this value and any disagreement is a defect in
  // one of the two.
  return conditions.unburntTemperature -
         deltaH * yFuel / (nuFuel * mFuel * conditions.specificHeat);
}

// ===================================================================================
// The solver
// ===================================================================================

PremixedFlameResult solvePremixedFlame(const PremixedFlameChemistry& chemistry,
                                       const PremixedFlameConditions& conditions,
                                       const PremixedFlameSettings& settings) {
  validateConditions(chemistry, conditions, "solvePremixedFlame");
  validateSettings(settings);

  const ReactionMechanism& mechanism = *chemistry.mechanism;
  const MixtureThermodynamics& mixture = *chemistry.mixture;
  const SpeciesRegistry& registry = mechanism.registry();
  const Index ns = mechanism.speciesCount();
  const Index nc = settings.cells;
  const Real domain = settings.domainLength;

  // A single-cell-thick strip: a 1D problem on the 2D machinery, with the lateral boundaries
  // zero-gradient so nothing varies across it.
  const Mesh mesh = MeshGeometry::createCartesian2D(nc, 1, domain, domain / static_cast<Real>(nc));

  const Real tUnburnt = conditions.unburntTemperature;
  const Real cp = conditions.specificHeat;
  const Real rhoUnburnt =
      mixture.density(conditions.unburntComposition, conditions.pressure, tUnburnt);
  const Real tAdiabatic = flameAdiabaticTemperature(chemistry, conditions);
  const std::vector<Real> burnt = flameBurntComposition(chemistry, conditions);

  if (!(tAdiabatic > tUnburnt)) {
    throw InvalidArgumentError(
        "solvePremixedFlame: the adiabatic flame temperature is " + exact(tAdiabatic) +
        " K against an unburnt " + exact(tUnburnt) +
        " K. This mixture and mechanism release no heat, so there is no flame to find and the "
        "anchor temperature would be meaningless.");
  }

  const Real tAnchor = tUnburnt + settings.anchorTemperatureFraction * (tAdiabatic - tUnburnt);
  const Real xAnchor = settings.anchorFraction * domain;

  // --- boundaries: Dirichlet at the cold inlet, zero gradient everywhere else.
  BoundaryConditionSet temperatureBoundaries;
  temperatureBoundaries.set(mesh, "left", std::make_unique<FixedValue>(tUnburnt));
  temperatureBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  temperatureBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  temperatureBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));

  std::vector<BoundaryConditionSet> compositionBoundaries(ns);
  std::vector<const BoundaryConditionSet*> compositionBoundaryPointers(ns);
  for (Index i = 0; i < ns; ++i) {
    compositionBoundaries[i].set(mesh, "left",
                                 std::make_unique<FixedValue>(conditions.unburntComposition[i]));
    compositionBoundaries[i].set(mesh, "right", std::make_unique<FixedGradient>(0.0));
    compositionBoundaries[i].set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
    compositionBoundaries[i].set(mesh, "top", std::make_unique<FixedGradient>(0.0));
    compositionBoundaryPointers[i] = &compositionBoundaries[i];
  }

  // --- geometry the reductions need. The cross-section is taken from the mesh rather than from
  // the caller's numbers: V_total = A L exactly on this grid, so A = V_total / L.
  std::vector<Real> xCentre(nc);
  Real totalVolume = 0.0;
  for (Index c = 0; c < nc; ++c) {
    xCentre[c] = mesh.cell(c).centroid().x;
    totalVolume += mesh.cell(c).volume();
  }
  const Real crossSection = totalVolume / domain;

  // --- initial profile: a smooth front at the anchor. A step would hand the first Arrhenius
  // evaluation a discontinuity; the ramp is at least two cells wide so it is resolved on any mesh.
  ScalarField temperature(nc);
  std::vector<ScalarField> composition(ns, ScalarField(nc));
  // alpha_max is taken at the BURNT side, where the density is lowest and the diffusivity largest.
  // Using the unburnt value would understate it by the expansion ratio and let dt be too large
  // exactly where the structure is thinnest in time.
  const Real rhoBurnt = mixture.density(burnt, conditions.pressure, tAdiabatic);
  const Real alphaMax = conditions.conductivity->value(tAdiabatic) / (rhoBurnt * cp);

  // alpha_unburnt is a DIFFERENT quantity and the two are not interchangeable. The time step is
  // bounded by the largest diffusivity in the field, which is the burnt-side one above. But the
  // FLAME'S OWN SCALES follow from the convection-diffusion balance that defines the structure,
  //
  //     mdot cp dT/dx = d/dx(lambda dT/dx)   =>   delta_preheat = lambda/(rho_u cp S) = alpha_u/S
  //
  // where the MASS FLUX sets the scale, so the unburnt density enters. Using the burnt-side value
  // here overstates the preheat thickness by the expansion ratio -- measured consequence: a seed
  // 3.6x too wide, which drove the front into a one-cell jump and took the fuel balance from 0.42
  // to 0.85.
  const Real alphaUnburnt = conditions.conductivity->value(tUnburnt) / (rhoUnburnt * cp);
  if (!std::isfinite(alphaMax) || !(alphaMax > 0.0)) {
    // THROWN, not reported through `status`: this is a fault in the supplied lambda(T), cp or
    // pressure, which is what the documented contract throws for. It is also evaluated before the
    // result object exists, because both the seeded preheat width and dt are derived from it.
    throw InvalidArgumentError(
        "solvePremixedFlame: the burnt-side thermal diffusivity is " + exact(alphaMax) +
        "; lambda(T_ad) = " + exact(conditions.conductivity->value(tAdiabatic)) +
        ", burnt density = " + exact(rhoBurnt) + ", cp = " + exact(cp) +
        " must together give a finite positive value, since the seeded flame width and the time "
        "step are both scaled by it");
  }

  // THE SEEDED PREHEAT WIDTH, set per trial from alpha_burnt / S.
  //
  // The flame has TWO thickness scales and this seed was built on the wrong one. The inner layer is
  // (T_ad - T_u)/max|dT/dx| ~ 0.67 mm, but the PREHEAT extent is alpha_burnt/S_L ~ 4.7 mm -- larger
  // by the expansion ratio, since alpha = lambda/(rho cp) and rho falls sixfold across the front.
  // The measured profile shows the preheat spanning 10 to 15 mm, matching the burnt-side scale and
  // not the inner one, so a 0.8 mm seed left a sixfold relaxation to perform on every trial.
  //
  // Bounded below by two cells so the ramp is always resolved, and above by an eighth of the domain
  // so it cannot fill the strip.
  Real ramp = std::max(domain / 50.0, 2.0 * domain / static_cast<Real>(nc));
  const auto seedRampFor = [&](Real trialMassFlux) {
    const Real speed = trialMassFlux / rhoUnburnt;
    const Real preheat = (speed > 0.0) ? settings.seedWidthFactor * alphaUnburnt / speed : ramp;
    return std::min(std::max(preheat, 2.0 * domain / static_cast<Real>(nc)), domain / 8.0);
  };
  const auto seedProfile = [&]() {
    for (Index c = 0; c < nc; ++c) {
      const Real blend = 0.5 * (1.0 + std::tanh((xCentre[c] - xAnchor) / ramp));
      temperature[c] = tUnburnt + (tAdiabatic - tUnburnt) * blend;
      for (Index i = 0; i < ns; ++i) {
        // A convex blend of two compositions that each sum to 1 also sums to 1, so the seeded
        // state satisfies the mixture constraint exactly rather than approximately.
        composition[i][c] = conditions.unburntComposition[i] +
                            (burnt[i] - conditions.unburntComposition[i]) * blend;
      }
    }
  };

  // A PROVISIONAL SEED, at the mesh-derived ramp, so that no field is ever read zero-initialised.
  // `densityFromState` below evaluates the mixture cell by cell, and on a default-constructed
  // ScalarField that means T = 0 with an all-zero composition -- which MixtureThermodynamics
  // correctly refuses ("cv_mix = cp_mix - R_mix = 0 at T = 0"). The per-trial seeds use the proper
  // preheat width instead; this one exists only so the setup path is never looking at zeros.
  //
  // It cannot use seedRampFor() because the mass flux that width is derived from has not been
  // estimated yet, a few lines below.
  seedProfile();

  // --- THE INITIAL MASS FLUX, and why it has to be a real estimate rather than any positive
  //     number.
  //
  // THE ITERATION HAS A FINITE BASIN OF ATTRACTION, and extinction is an ABSORBING state. If mdot
  // starts well below the eigenvalue the front is driven into the cold Dirichlet inlet, which
  // quenches it; once the gas is cold the Arrhenius rate is negligible and nothing can reignite
  // it, so the march cannot recover no matter how the eigenvalue controller responds. Measured on
  // the synthetic flame: a first attempt seeded from a generic diffusion velocity started 10x too
  // slow, and the peak temperature fell below the anchor in FOUR outer iterations -- faster than
  // any stable gain can make up a factor of ten. This is a property of the method and is recorded
  // as one.
  //
  // So the seed is the standard Zel'dovich/Mallard-Le Chatelier scaling, evaluated from the
  // mechanism and the frozen properties alone:
  //
  //     tau_c  = rho_u Y_F,u / |omega_F(T_ad, rho_u, Y_u)|      the chemical time
  //     alpha  = lambda(T_ad) / (rho_u cp)                      the thermal diffusivity
  //     S_est  = sqrt(alpha / tau_c)
  //
  // The rate is deliberately evaluated at the FLAME temperature but the UNBURNT composition and
  // density -- the reactants have not yet been consumed where the reaction runs fastest -- which
  // for a first-order reaction reduces to the exact identity tau_c = 1/k(T_ad).
  //
  // NOTHING HERE IS FITTED TO A KNOWN FLAME SPEED. It reads only the mechanism, cp and lambda(T),
  // and the converged answer must be shown INDEPENDENT of it (test 2), which is what keeps a
  // starting estimate from becoming a hidden input to the result.
  PremixedFlameResult result;
  result.unburntDensity = rhoUnburnt;
  result.adiabaticTemperature = tAdiabatic;

  Real massFluxEstimate = 0.0;
  {
    const ScalarField adiabaticField(1, tAdiabatic);
    const ScalarField unburntDensityField(1, rhoUnburnt);
    std::vector<ScalarField> unburntFields(ns, ScalarField(1));
    for (Index i = 0; i < ns; ++i) {
      unburntFields[i][0] = conditions.unburntComposition[i];
    }
    const auto seedRates = arrheniusProgressRatesFromFields(
        mechanism, *chemistry.kinetics, mixture, unburntFields, unburntDensityField, adiabaticField,
        NegativeConcentrationPolicy::ZeroRateWhereNegative);
    const SpeciesSources seedSources = reactionSpeciesSources(mechanism, mixture, seedRates, 1);
    const Real omegaFuel = std::abs(seedSources.perSpecies[conditions.fuelIndex].Su[0]);
    const Real yFuel = conditions.unburntComposition[conditions.fuelIndex];

    if (omegaFuel > 0.0 && yFuel > 0.0) {
      const Real chemicalTime = (rhoUnburnt * yFuel) / omegaFuel;
      const Real alpha = conditions.conductivity->value(tAdiabatic) / (rhoUnburnt * cp);
      massFluxEstimate = rhoUnburnt * std::sqrt(alpha / chemicalTime);
    }
  }

  Real massFlux = settings.initialMassFlux;
  if (!(massFlux > 0.0)) {
    massFlux = massFluxEstimate;
  }
  if (!(massFlux > 0.0) || !std::isfinite(massFlux)) {
    // No rate at the flame temperature at all: there is nothing to seed from and nothing to find.
    result.status = PremixedFlameStatus::NoFlame;
    return result;
  }
  result.massFluxEstimate = massFluxEstimate;

  // THE INNER LINEAR SOLVER, and why its tolerances are set here rather than defaulted.
  //
  // The default absoluteTolerance of 1e-12 is UNREACHABLE for these systems: the energy equation's
  // right-hand side is O(1e4) in these units, so 1e-12 absolute asks for 16 orders of reduction,
  // and the solve was reported as a failure at a relative residual of 4e-9 after 2000 iterations.
  // That is a tolerance artefact, not a divergence. The absolute criterion is therefore pushed
  // below the representable range of any residual here and the RELATIVE criterion governs, which
  // is the scale-free statement actually wanted when the same solver handles both a temperature
  // field O(1e3) and a mass fraction O(1).
  //
  // The relative tolerance stays tight, but the SOLVE is not what limits sum_i Y_i - 1. The
  // telescoping is real and measured -- at unity Lewis number the per-species matrices are
  // bit-identical, so sum_i (A y_i) = A (sum_i y_i) holds exactly -- yet the solve contributes
  // only ~1 %: an EXACT solve still misses the bound by 230x. Tightness is kept, not justified,
  // here; the dependence is non-monotonic. Reported by maxCompositionSumDeviation. See TD-049.
  //
  // Jacobi preconditioning, because the chemistry's Patankar Sp term spans orders of magnitude
  // across the front and unpreconditioned BiCGSTAB stagnated on it.
  LinearSolverSettings linearSettings;
  linearSettings.maxIterations = 5000;
  linearSettings.relativeTolerance = 1.0e-11;

  // THE ABSOLUTE TOLERANCE IS DERIVED FROM EACH SYSTEM'S OWN RIGHT-HAND SIDE, which is why the
  // solver is built per solve rather than once.
  //
  // Both fixed choices failed, in opposite directions, and both failures were measured:
  //
  //   * the DEFAULT 1e-12 is unreachable for the energy equation, whose right-hand side is O(1e4)
  //     in these units -- it asks for 16 orders of reduction. The solve was reported failed at a
  //     RELATIVE residual of 4e-9 after 2000 iterations, which is a tolerance artefact, not a
  //     divergence.
  //   * pushing it to 1e-300 so the relative criterion would govern then meant a nearly-exact
  //     system could never stop, and BiCGSTAB iterated into cancellation BREAKDOWN -- reported on
  //     the species system at a residual of 3.9e-4 against an initial 2.0e-4, both negligible.
  //
  // No single dimensional constant can serve a temperature field of O(1e3) and a mass fraction of
  // O(1) at once, so the bound is made scale-free: eps-level relative to ||b||, which is the
  // smallest residual the arithmetic can meaningfully distinguish from zero for that system.
  // WHAT COUNTS AS A GOOD ENOUGH SOLVE IS JUDGED ON THE ANSWER, not on the solver's status.
  //
  // BiCGSTAB's status describes its own recurrence; it is not a statement about whether the
  // solution is usable. Two measured failures made that distinction unavoidable:
  //
  //   * `MaxIterations` on the energy system at a RELATIVE residual of 4e-9 -- a perfectly good
  //     solution rejected because a tolerance relative to the INITIAL residual asked for ten more
  //     orders;
  //   * `Breakdown` on the species system at a residual of 1.15e-3 against an initial 9.6e-4. Both
  //     are negligible against that system's own right-hand side: early in the startup ramp the
  //     transient term dominates so heavily that the previous iterate is already almost the exact
  //     solution, and asking for 1e-11 of an already-tiny initial residual drives the recurrence
  //     into cancellation.
  //
  // So the residual is judged against ||b||, which is scale-free and -- unlike the initial residual
  // -- does not get harder to meet as the guess gets better. This FAILS CLOSED: a genuinely
  // diverged solve has a large residual against ||b|| and is still rejected, with its numbers
  // named. A system with b == 0 has x == 0 as its exact solution, so a zero residual is required
  // there and nothing is waved through.
  const Real residualAcceptanceFraction = 1.0e-9;

  Real worstLinearResidual = 0.0;  // max over every solve of ||r||/||b||: reported, not assumed
  Real lastDensityChange = 0.0;    // max_cell |d rho|/rho over the step just taken
  Real worstDensityChange = 0.0;

  struct SolveOutcome {
    bool acceptable{false};
    cfd::algebra::Vector solution;
    Real residual{};
    Real rhsNorm{};
    cfd::algebra::SolverStatus status{};
    Index iterations{};
  };

  const auto solveSystem = [&](const LinearSystem& system, const ScalarField& guess) {
    const Vector& rhs = system.rhs();
    Real rhsNorm = 0.0;
    for (Index i = 0; i < rhs.size(); ++i) {
      rhsNorm += rhs[i] * rhs[i];
    }
    rhsNorm = std::sqrt(rhsNorm);

    LinearSolverSettings scaled = linearSettings;
    scaled.absoluteTolerance =
        std::max(std::numeric_limits<Real>::min(), residualAcceptanceFraction * rhsNorm);
    const BiCGSTAB solver(scaled, std::make_shared<cfd::algebra::JacobiPreconditioner>());
    const auto raw = solver.solve(system, toVector(guess));

    SolveOutcome outcome;
    outcome.solution = raw.solution;
    outcome.residual = raw.finalResidual;
    outcome.rhsNorm = rhsNorm;
    outcome.status = raw.status;
    outcome.iterations = raw.iterations;
    outcome.acceptable =
        raw.converged() || raw.finalResidual <= residualAcceptanceFraction * rhsNorm;
    worstLinearResidual =
        std::max(worstLinearResidual, (rhsNorm > 0.0) ? raw.finalResidual / rhsNorm : 0.0);
    return outcome;
  };

  const ScalarField specificHeatField(nc, cp);
  const Real seedSpeed = massFluxEstimate / rhoUnburnt;

  // --- THE TIME STEP. The inner problem is MARCHED IN TIME, not solved steady -- see the header
  //     for why the steady position-anchored form was abandoned. dt is scaled from the flame's own
  //     transit time t_f = alpha / S_est^2 (equivalently delta/S_est with delta = alpha/S_est), so
  //     it is derived from the problem rather than picked.
  // Set per trial by trialTimeStep() below; `advance` reads it by reference.
  Real dt = 0.0;
  const Real cellSizeForDt = domain / static_cast<Real>(nc);
  const auto trialTimeStep = [&](Real trialMassFlux) -> Real {
    const Real inflowSpeed = trialMassFlux / rhoUnburnt;
    const Real courantLimit = settings.cellCourant * cellSizeForDt / inflowSpeed;
    const Real diffusionLimit =
        settings.maximumCellDiffusionNumber * cellSizeForDt * cellSizeForDt / alphaMax;
    return (settings.timeStep > 0.0) ? settings.timeStep : std::min(courantLimit, diffusionLimit);
  };

  ScalarField density(nc);
  ScalarField densityOld(nc);
  ScalarField temperatureOld = temperature;
  std::vector<ScalarField> compositionOld = composition;

  // rho^n for the first step, from the seeded profile. The conservative transient term needs BOTH
  // density levels and P14 is emphatic about which is which, so they are built separately.
  const auto densityFromState = [&](const std::vector<ScalarField>& fields, const ScalarField& temp,
                                    ScalarField& into) {
    std::vector<Real> cell(ns);
    for (Index c = 0; c < nc; ++c) {
      for (Index i = 0; i < ns; ++i) {
        cell[i] = fields[i][c];
      }
      into[c] = mixture.density(cell, conditions.pressure, temp[c]);
    }
  };
  densityFromState(composition, temperature, densityOld);

  const Real cellSize = domain / static_cast<Real>(nc);

  // THE X-FACES IN ORDER, west to east: face j lies between cell j-1 and cell j, so a strip of nc
  // cells has nc + 1 of them. Found GEOMETRICALLY rather than assumed from the mesh's internal
  // face ordering -- the lateral faces are exactly those whose area vector has no x component.
  std::vector<Index> orderedXFaces;
  {
    std::vector<std::pair<Real, Index>> byPosition;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      if (std::abs(mesh.face(f).areaVector().x) > 0.0) {
        byPosition.emplace_back(mesh.face(f).centroid().x, f);
      }
    }
    std::sort(byPosition.begin(), byPosition.end());
    orderedXFaces.reserve(byPosition.size());
    for (const auto& entry : byPosition) {
      orderedXFaces.push_back(entry.second);
    }
  }
  if (orderedXFaces.size() != nc + 1) {
    throw InvalidArgumentError("solvePremixedFlame: the 1D strip has " +
                               std::to_string(orderedXFaces.size()) + " x-faces; a strip of " +
                               std::to_string(nc) + " cells must have exactly " +
                               std::to_string(nc + 1) +
                               ". The continuity march depends on that, so it is checked rather "
                               "than assumed.");
  }
  Real cumulativeShift = 0.0;  // the front's TRUE travel, including every recentring
  // TD-004: the final march's own elapsed time, declared here rather than inside that loop
  // because the conservation balances -- which are computed after it -- divide the ledger by it.
  Real ledgerElapsed = 0.0;
  Real previousFrontPosition = 0.0;
  bool havePreviousFront = false;
  Index totalSteps = 0;
  PremixedFlameStatus marchStatus = PremixedFlameStatus::Converged;

  // -----------------------------------------------------------------------------------------
  // The anchor-temperature crossing, in domain coordinates. The FIRST crossing is taken: the
  // upstream edge of the front, which is the one the cold boundary controls.
  // -----------------------------------------------------------------------------------------
  // HOW FAR THE FRONT CAN POSSIBLY MOVE IN ONE STEP, derived from the signal speeds rather than
  // chosen. The front is carried by the gas and propagates into it, so its speed relative to the
  // mesh is bounded by the largest convective velocity plus the laminar flame speed. The largest
  // convective velocity is in the BURNT gas, where the density is lowest:
  //
  //     u_burnt = mdot / rho_burnt,   rho_burnt = p Mbar / (R T_ad)
  //     maxJump = kJumpMargin * (u_burnt + S_L) * dt / cellSize      [cells]
  //
  // For this fixture at dt = 3.77e-05: u_burnt = 0.2918 m/s gives 0.550 cells and S_L gives 0.092,
  // so ~0.64 cells of physically possible motion per step. kJumpMargin = 8 allows for a startup
  // transient moving the front faster than the converged flame does, and is EMPIRICAL -- the
  // speeds it multiplies are not. Even at that margin the bound is ~5 cells, which rejects the
  // measured 94.66 and 327.51-cell jumps by 18x and 64x.
  constexpr Real kJumpMargin = 8.0;
  Real previousLocated = 0.0;
  bool haveLocated = false;

  // THE FRONT IS THE CROSSING NEAREST THE PREVIOUS ONE, not the first crossing in the array.
  //
  // A temperature profile can cross tAnchor more than once; the original form took the first
  // crossing scanning from c = 0 and could therefore latch onto a spurious upstream wiggle and
  // report it as the front. Its own comment explains the intent -- "the upstream edge of the
  // front, which is the one the cold boundary controls" -- and that intent is preserved on the
  // FIRST call, where there is no previous position to compare against and the first crossing is
  // still the right choice. Afterwards the front is TRACKED.
  const auto locateFront = [&](Real& position, Real admissibleJump) -> bool {
    bool found = false;
    Real best = 0.0;
    bool rejectedAny = false;
    Index crossings = 0;

    for (Index c = 0; c + 1 < nc; ++c) {
      const Real below = temperature[c] - tAnchor;
      const Real above = temperature[c + 1] - tAnchor;
      if (below * above <= 0.0 && temperature[c] != temperature[c + 1]) {
        const Real crossing = xCentre[c] + (tAnchor - temperature[c]) /
                                               (temperature[c + 1] - temperature[c]) *
                                               (xCentre[c + 1] - xCentre[c]);
        if (!haveLocated) {
          position = crossing;  // first call: the upstream edge, as before
          previousLocated = crossing;
          haveLocated = true;
          return true;
        }
        ++crossings;
        const Real distance = std::abs(crossing - previousLocated);
        if (admissibleJump > 0.0 && distance > admissibleJump) {
          rejectedAny = true;  // not reachable from where the front was -- not the front
          const Real jumpCells = distance / cellSize;
          if (jumpCells > result.worstRejectedJumpCells) {
            result.worstRejectedJumpCells = jumpCells;
            result.worstRejectedJumpPosition = crossing;  // the two move together, by construction
          }
          if (std::abs(crossing - xAnchor) < cellSize) {
            ++result.rejectedJumpsLandingAtAnchor;
          }
          if (jumpCells < 10.0) {
            ++result.rejectedJumpsUnder10Cells;
          } else if (jumpCells < 50.0) {
            ++result.rejectedJumps10To50Cells;
          } else {
            ++result.rejectedJumpsOver50Cells;
          }
          continue;
        }
        // EARLY-OUT on the first ADMISSIBLE crossing. The scan continues past INADMISSIBLE ones
        // -- it must, or a spurious crossing ahead of the real front would end the search -- but
        // once a crossing within the window is found there is nothing to gain by scanning on.
        //
        // "First" and "nearest" coincide here, for two measured reasons: the admissible window is
        // only ~5 cells wide, so any two crossings inside it are within 5 cells of each other and
        // of the front; and `maxFrontCrossings` measured 1 on this fixture, so there is never a
        // second crossing to choose between. Keeping the full scan cost ~6x in wall time (30 min
        // against the usual 5), which matters because the gate forbids any further test acquiring
        // a timeout and three already have.
        found = true;
        best = crossing;
        break;
      }
    }
    result.maxFrontCrossings = std::max(result.maxFrontCrossings, crossings);
    if (rejectedAny) {
      ++result.rejectedFrontCrossings;
    }
    if (!found) {
      return false;  // every crossing was inadmissible: report no front rather than guess
    }
    position = best;
    previousLocated = best;
    haveLocated = true;
    return true;
  };

  // -----------------------------------------------------------------------------------------
  // RECENTRING. Translating the whole profile by a WHOLE NUMBER of cells is an exact operation
  // on this uniform grid -- no interpolation, so it introduces no error of its own -- and it is
  // what lets the march run for as long as a trial needs without the front leaving the domain.
  // The upstream fill is the unburnt inlet state and the downstream fill is the outflow edge
  // value, which are the states those ends already hold. The displacement is accumulated so the
  // front's TRUE travel, and therefore the drift, is unaffected by the shift.
  // -----------------------------------------------------------------------------------------
  // TD-004: the content integrals the ledger differences. Fuel is rho*Y_F dx [kg/m^2]; energy is
  // the SENSIBLE content rho*cp*(T - T_u) dx [J/m^2], which is the quantity the energy balance
  // below is written on, so the two are directly comparable.
  //
  // Both are evaluated from the CURRENT state inside `recentre`, before and after the translation,
  // rather than reasoned about per cell. That is what makes the ledger exact by construction: it
  // measures whatever the shift actually did, including both fill branches, without a second
  // model of the operator that could disagree with it.
  const auto fuelContent = [&]() {
    Real total = 0.0;
    for (Index c = 0; c < nc; ++c) {
      total += density[c] * composition[conditions.fuelIndex][c] * mesh.cell(c).volume();
    }
    return total / crossSection;
  };
  const auto sensibleContent = [&]() {
    Real total = 0.0;
    for (Index c = 0; c < nc; ++c) {
      total += density[c] * cp * (temperature[c] - tUnburnt) * mesh.cell(c).volume();
    }
    return total / crossSection;
  };

  // Set before each recentre call from the current dt; 0 disables the check.
  Real maximumShiftCells = 0.0;

  // The admissible single-step front jump, in cells, from the CURRENT dt and mass flux. Derived
  // above; both call sites use this one function so they cannot drift apart.
  const auto admissibleJumpCells = [&](Real currentMassFlux, Real currentDt) {
    // The largest convective velocity present is mdot / min(rho), taken from the LIVE density
    // field rather than from an EOS evaluation at T_ad -- it is a tighter and more honest bound,
    // it needs no extra constants, and it adapts if the state is not yet fully burnt. The front
    // also propagates into the gas at the laminar flame speed, mdot / rho_unburnt.
    Real minDensity = density[0];
    for (Index c = 1; c < nc; ++c) {
      minDensity = std::min(minDensity, density[c]);
    }
    const Real convective = (minDensity > 0.0) ? currentMassFlux / minDensity : 0.0;
    const Real speed = convective + currentMassFlux / rhoUnburnt;
    return kJumpMargin * speed * currentDt / cellSize;
  };

  const auto recentre = [&](Real position) {
    const Real offsetCells = (position - xAnchor) / cellSize;
    // THE DEAD BAND. Leaving the front alone while it stays near the anchor is what stops the
    // translation running every step; see PremixedFlameSettings for the fuel it was leaking.
    if (std::abs(offsetCells) < settings.recentreDeadBandCells) {
      return;
    }
    const long shift = static_cast<long>(std::llround(offsetCells));
    if (shift == 0) {
      return;
    }
    // A SHIFT THE SOLVER CANNOT JUSTIFY IS REFUSED, not executed. The locator above should no
    // longer hand over an implausible position, so this is the second line of defence and it is
    // deliberately kept: the measured failure was a 164-cell shift moving 84 % of the domain's
    // fuel inventory, and an operator that will do that on request is one bad position away from
    // destroying the solution again. The bound is the whole admissible excursion from the anchor:
    // the dead band plus one step's admissible jump.
    if (maximumShiftCells > 0.0 && std::abs(offsetCells) > maximumShiftCells) {
      ++result.rejectedShifts;
      return;
    }
    result.largestShiftCells =
        std::max(result.largestShiftCells, static_cast<Index>(std::abs(shift)));
    const auto shiftField = [&](ScalarField& field, Real upstreamFill) {
      ScalarField shifted(nc);
      for (Index c = 0; c < nc; ++c) {
        const long source = static_cast<long>(c) + shift;
        if (source < 0) {
          shifted[c] = upstreamFill;
        } else if (source >= static_cast<long>(nc)) {
          shifted[c] = field[nc - 1];
        } else {
          shifted[c] = field[static_cast<Index>(source)];
        }
      }
      field = shifted;
    };
    // The ledger brackets the translation. `density` is refreshed from the shifted state first, so
    // the "after" integral uses the density that actually belongs to the shifted profile rather
    // than the pre-shift one -- otherwise the ledger would attribute a density change to the shift.
    const Real fuelBefore = fuelContent();
    const Real energyBefore = sensibleContent();

    shiftField(temperature, tUnburnt);
    shiftField(temperatureOld, tUnburnt);
    for (Index i = 0; i < ns; ++i) {
      shiftField(composition[i], conditions.unburntComposition[i]);
      shiftField(compositionOld[i], conditions.unburntComposition[i]);
    }
    densityFromState(compositionOld, temperatureOld, densityOld);
    densityFromState(composition, temperature, density);

    result.recentringFuelLedger += fuelContent() - fuelBefore;
    result.recentringEnergyLedger += sensibleContent() - energyBefore;

    // ---- TD-004: TRANSLATE THE LOCATOR'S TRACKED POSITION WITH THE FIELD.
    //
    // `locateFront` accepts a crossing only within `admissibleJump` of `previousLocated`. This
    // operator moves the whole field and used to leave that tracked position behind, so the FIRST
    // recentring of a march put the front out from under the locator and every later crossing then
    // looked like an inadmissible jump. Measured consequence: the locator refused the TRUE front
    // once per step for the remaining 16 000+ steps, `recentre` was never invoked again, and the
    // front drifted 51 cells off the anchor at relaxT 10 and 86 at relaxT 30. Evidence:
    // `results/technical_debt/TD004_premixed_flame/audit_A2.md`.
    //
    // THE SIGN IS DERIVED, not chosen. `shiftField` sets `shifted[c] = field[c + shift]`, so a
    // feature at index `i` moves to index `i - shift` and its position DECREASES by
    // `shift * cellSize`. The tracked position must move the same way, which is also why
    // `cumulativeShift` ADDS the same quantity below: that one converts a shifted position back to
    // an absolute one for the drift history, so the two have opposite signs by construction.
    //
    // WHY NOT RESET `haveLocated` INSTEAD, which is what the RE-SEED path at the final march does:
    // that path's first-call branch accepts ANY crossing, which disarms the jump guard for a step.
    // Re-seeding can afford that because it moves the front discontinuously and no prior position
    // is meaningful afterwards. A translation is different -- the displacement is known exactly --
    // so the guard stays armed and the tracked position is corrected instead.
    //
    // Checked against the measurement: previousLocated 3.559962 mm, shift 3, cellSize 2e-5 m gives
    // 3.499962 mm, and the front immediately after that shift sits at ~3.4999 mm. The distance the
    // locator then measures collapses to ~0 instead of 3.287 cells, and tracking continues.
    previousLocated -= static_cast<Real>(shift) * cellSize;
    cumulativeShift += static_cast<Real>(shift) * cellSize;
    ++result.recentrings;
  };

  // -----------------------------------------------------------------------------------------
  // ONE IMPLICIT-EULER STEP of the coupled species/energy system at the given mass flux.
  // -----------------------------------------------------------------------------------------
  const auto advance = [&](Real currentMassFlux) -> PremixedFlameStatus {
    // ---- THE OLD TIME LEVEL IS CAPTURED HERE, WHERE THE STEP BEGINS. TD-032.
    //
    // `temperatureOld` and `compositionOld` are the transient term's level-n fields: the energy
    // assembly reads the first through `ThermalTimeState::temperatureOld` and each species assembly
    // reads the second through `SpeciesTimeState::fieldsOld`. Both are consumed at the TOP of a
    // step, so both must hold the state THIS step starts from.
    //
    // They used to be assigned much further down, in the same three-line block as `densityOld`, at
    // a point where `temperature` and `composition` still held their start-of-step values -- so the
    // copy was a no-op on the first step of a march and ONE STEP STALE on every step after it. The
    // transient term then read level k-1 while the operator coefficients came from level k, which
    // connects step k-1 to step k+1: the even and odd steps become two interleaved sub-sequences,
    // coupled only through those coefficients and not through the transient term at all, and two
    // sub-sequences with no transient coupling need not share a fixed point. They did not. The
    // reported field alternated EVERY STEP between the cold preheat zone and a nearly fully burnt
    // state 1484 K away, and the reported front between the anchor and the inlet. Measured at 500
    // and 250 cells; `results/technical_debt/TD032_lagged_time_level/`.
    //
    // Because the first step of every march is seeded `old = current` (at each `runTrial` and
    // before the final march), a one-step check could never see this. Nor could the transient tests
    // in `tests/unit/thermal/` and `tests/unit/transport/`: they hand an assembly an old level and
    // check what it builds, and the assembly was always correct. The defect was entirely in which
    // field this caller handed it.
    //
    // `densityOld` is deliberately NOT moved here. It is a WITHIN-step quantity -- the `dt`
    // controller at the end of this lambda compares the post-chemistry density against it -- so
    // start-of-step is exactly what it needs and it is correct where it is. One assignment site was
    // serving two different requirements, and satisfied the one belonging to the field that never
    // crossed a step boundary.
    temperatureOld = temperature;
    compositionOld = composition;

    densityFromState(composition, temperature, density);
    for (Index c = 0; c < nc; ++c) {
      if (!std::isfinite(density[c]) || !(density[c] > 0.0)) {
        return PremixedFlameStatus::NonFiniteState;
      }
    }

    const ScalarField conductivityField =
        cfd::physics::evaluatePropertyField(mesh, temperature, *conditions.conductivity);

    SpeciesDiffusivities diffusivities;
    diffusivities.gamma.assign(ns, ScalarField(nc));
    for (Index c = 0; c < nc; ++c) {
      // Gamma is supplied DIRECTLY rather than through makeSpeciesDiffusivities, because that
      // factory takes a constant D_i per species and here D_i varies with temperature through
      // lambda(T). Gamma = lambda/(cp Le) IS rho D_i, which is what the assembler consumes.
      const Real gamma = conductivityField[c] / (cp * conditions.lewisNumber);
      for (Index i = 0; i < ns; ++i) {
        diffusivities.gamma[i][c] = gamma;
      }
    }

    // The raw-fields kinetics entry point, with the continuous-extension policy: this is a
    // working vector, not a MassFractionState, and a linear solve converged to a tolerance can
    // leave Y_i at -1e-14 where the exact discrete solution is 0. The magnitude is reported,
    // never repaired.
    // NO REACTION IN THE TRANSPORT STEP. The chemistry is advanced separately, after this, by
    // advanceChemistryAtConstantPressure -- see its header comment for why an in-equation source
    // cannot work for a stiff exothermic reaction. With no source the species operator is an
    // M-matrix from upwind convection, positive diffusion and the transient term alone, so Y_i
    // cannot go negative; the Patankar split that used to provide that is no longer needed.
    //
    // The rates ARE still evaluated, for one purpose only: the velocity divergence constraint
    // below needs the instantaneous sources. They never enter a matrix or a right-hand side.
    ReactionProgressRates rates = zeroProgressRates(mechanism.reactionCount(), nc);
    if (settings.reactionsEnabled) {
      rates = arrheniusProgressRatesFromFields(mechanism, *chemistry.kinetics, mixture, composition,
                                               density, temperature,
                                               NegativeConcentrationPolicy::ZeroRateWhereNegative);
    }
    const SpeciesSources chemicalSources = reactionSpeciesSources(mechanism, mixture, rates, nc);
    const ScalarField heatRelease =
        reactionHeatRelease(mechanism, mixture, *chemistry.formation, rates, nc);

    // A one-line summary of the state, for any failure below. An assembly that rejects a
    // coefficient cannot say what the field looked like, and that is what makes such a failure
    // expensive to diagnose.
    const auto stateSummary = [&]() {
      Real tMin = temperature[0];
      Real tMax = temperature[0];
      Real rMin = density[0];
      Real rMax = density[0];
      for (Index c = 0; c < nc; ++c) {
        tMin = std::min(tMin, temperature[c]);
        tMax = std::max(tMax, temperature[c]);
        rMin = std::min(rMin, density[c]);
        rMax = std::max(rMax, density[c]);
      }
      return " [T in " + exact(tMin) + " .. " + exact(tMax) + ", rho in " + exact(rMin) + " .. " +
             exact(rMax) + ", mdot = " + exact(currentMassFlux) + ", dt = " + exact(dt) + "]";
    };

    // ---- THE FACE MASS FLUX. Uniform, from the scalar mdot, projected on each face's own area
    //      vector so the sign follows the mesh's owner/neighbour convention and the lateral faces
    //      carry exactly zero.
    //
    // A UNIFORM rho*u IS THE STEADY CONTINUITY EQUATION, and this march is unsteady -- so during
    // the transient it is an approximation. It is the right approximation here, and the route to
    // that conclusion is worth recording because two more elaborate closures were built and both
    // failed:
    //
    //   1. d(rho u)/dx = -(rho^{n+1} - rho^n)/dt, the discretely exact closure. UNUSABLE across a
    //      split step: the chemistry moves the density by a finite jump over dt, so the inferred
    //      velocity grows as 1/dt -- and the convective temperature change it induces,
    //      (m/rho)(dT/dx)dt, has the dt cancel exactly, making the feedback SCALE-FREE. Measured:
    //      a time-step controller cut dt to 6.6e-17 and the trials still failed at the same step.
    //   2. the thermodynamic divergence constraint, div(u) from the heat-release and species
    //      sources, which IS bounded. But it satisfies the CONTINUOUS relation, not the discrete
    //      identity the conservative transient term needs, so the leftover discrete continuity
    //      residual still rescaled the temperature: peak 4755 K and |sum Y - 1| = 4.26.
    //
    // THE REAL CULPRIT WAS THE TRANSIENT TERM'S FORM, not the flux. See the time state below.
    SurfaceField faceMassFlux(mesh.numberOfFaces());
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      faceMassFlux[f] = currentMassFlux * mesh.face(f).areaVector().x;
    }

    SpeciesDiffusiveFluxes closure;
    try {
      closure = speciesDiffusiveFaceFluxes(mesh, registry, composition, diffusivities,
                                           compositionBoundaryPointers);
    } catch (const Error& error) {
      result.diagnostic =
          std::string("the diffusive closure refused the state: ") + error.what() + stateSummary();
      return PremixedFlameStatus::NonFiniteState;
    }

    // ---- THE TIME TERM, IN NON-CONSERVATIVE FORM, and this is the correction that mattered.
    //
    // P14's conservative term is
    //     a_P = rho^{n+1} V/dt        b_P = rho^n phi^n V/dt
    // which conserves rho*phi. That is the right choice for a solver whose face fluxes satisfy the
    // DISCRETE continuity equation. This one's cannot, for the reasons given at the mass flux
    // above, and the mismatch then appears as a rescaling of the solved variable by
    // rho^n/rho^{n+1} on every step -- measured as a temperature of 149 K against a 300 K inlet in
    // one direction and 4755 K in the other, and as |sum Y - 1| of 0.89 and 4.26.
    //
    // Passing the SAME density at both levels gives
    //     a_P = rho V/dt              b_P = rho phi^n V/dt
    // whose difference is rho (phi^{n+1} - phi^n) V/dt: the non-conservative form rho d(phi)/dt,
    // with no density ratio in it at all.
    //
    // THIS CANNOT CHANGE THE CONVERGED ANSWER. At steady state the time term vanishes identically,
    // whichever form it took, so the form is a property of the PATH to the eigenvalue and not of
    // the eigenvalue. What it buys is exact preservation of the mixture constraint: summing the
    // species equations now gives rho (sum_i Y^{n+1} - sum_i Y^n) V/dt plus the transport of
    // sum_i Y_i, and with sum_i Y^n = 1 and a uniform inlet the constant is reproduced exactly.
    // Reported as maxCompositionSumDeviation, so the claim is measured rather than argued.
    SpeciesTimeState speciesTime;
    speciesTime.transient = true;
    speciesTime.dt = dt;
    speciesTime.fieldsOld = &compositionOld;
    speciesTime.rhoOld = &density;
    speciesTime.rhoNew = &density;

    std::vector<ScalarField> nextComposition = composition;
    for (Index i = 0; i < ns; ++i) {
      // An EXPLICIT zero source: the reaction is not part of this equation at all. Stated as a
      // zero field rather than omitted, so what the transport step solves is unambiguous.
      cfd::transport::ScalarSourceFields source;
      source.Su = ScalarField(nc);
      source.Sp = ScalarField(nc);

      // std::optional, because ScalarTransportAssembly holds a LinearSystem and is therefore not
      // default-constructible -- there is no meaningful empty linear system.
      std::optional<cfd::transport::ScalarTransportAssembly> assembly;
      try {
        assembly =
            speciesTransportAssembly(mesh, i, registry, composition, faceMassFlux, diffusivities,
                                     compositionBoundaries[i], source, speciesTime, &closure);
      } catch (const Error& error) {
        result.diagnostic = "the species assembly for \"" + registry.name(i) +
                            "\" refused: " + error.what() + stateSummary();
        return PremixedFlameStatus::NonFiniteState;
      }
      const auto solved = solveSystem(assembly->system, composition[i]);
      if (!solved.acceptable) {
        result.diagnostic =
            "the species transport solve for \"" + registry.name(i) + "\" left a residual of " +
            exact(solved.residual) + " against ||b|| = " + exact(solved.rhsNorm) + " (status " +
            std::to_string(static_cast<int>(solved.status)) + " after " +
            std::to_string(solved.iterations) + " iterations, mdot = " + exact(currentMassFlux) +
            ", dt = " + exact(dt) + ")";
        return PremixedFlameStatus::LinearSolveFailure;
      }
      for (Index c = 0; c < nc; ++c) {
        nextComposition[i][c] = solved.solution[c];
      }
    }

    // The field-property energy assembly carries lambda(T) and cp per cell but accepts only a
    // UNIFORM source, so the per-cell heat release is added to the assembled RHS by the
    // production function that owns that term. It is RHS-only by construction
    // (rhs[cell] += source * volume), so this composition is exact.
    cfd::thermal::ThermalTimeState thermalTime;
    thermalTime.transient = true;
    thermalTime.dt = dt;
    thermalTime.temperatureOld = &temperatureOld;
    thermalTime.densityOld = &density;  // the SAME level: non-conservative, see the note above
    thermalTime.specificHeatOld = &specificHeatField;
    thermalTime.densityNew = &density;
    thermalTime.specificHeatNew = &specificHeatField;

    std::optional<cfd::thermal::EnergyAssembly> energy;
    try {
      energy = cfd::thermal::assembleEnergyEquation(
          mesh, temperature, faceMassFlux, conductivityField, specificHeatField,
          temperatureBoundaries, 0.0, cfd::discretization::NonOrthogonalCorrectionOptions{},
          thermalTime);
    } catch (const Error& error) {
      result.diagnostic =
          std::string("the energy assembly refused: ") + error.what() + stateSummary();
      return PremixedFlameStatus::NonFiniteState;
    }

    // The energy equation carries NO heat release either: it is pure convection-diffusion over
    // this step, and the release is applied by the chemistry sub-step below.
    const LinearSystem& energySystem = energy->system;

    const auto solvedEnergy = solveSystem(energySystem, temperature);
    if (!solvedEnergy.acceptable) {
      result.diagnostic = "the energy solve left a residual of " + exact(solvedEnergy.residual) +
                          " against ||b|| = " + exact(solvedEnergy.rhsNorm) + " (status " +
                          std::to_string(static_cast<int>(solvedEnergy.status)) + " after " +
                          std::to_string(solvedEnergy.iterations) +
                          " iterations, mdot = " + exact(currentMassFlux) + ", dt = " + exact(dt) +
                          ")";
      return PremixedFlameStatus::LinearSolveFailure;
    }

    // TD-032: `temperatureOld` and `compositionOld` are now captured at the TOP of this lambda --
    // they cross a step boundary and were one step stale here. `densityOld` stays: it is consumed
    // by the `dt` controller below, inside this same step, where start-of-step is what is wanted.
    densityOld = density;

    // A DIVERGENCE BRACKET, not a clamp. An adiabatic flame's temperature lies between T_u and
    // T_ad; anything far outside means the march has diverged, and the generous factors below are
    // chosen so that only a divergence trips it. Checked SEPARATELY after each half of the split,
    // because "which half produced it" is the whole question: transport alone is pure
    // convection-diffusion and obeys a maximum principle, so it CANNOT overshoot, and a report
    // naming transport would itself be evidence of a defect in the assembly rather than stiffness.
    const Real temperatureFloor = 0.5 * tUnburnt;
    const Real temperatureCeiling = tUnburnt + 3.0 * (tAdiabatic - tUnburnt);

    for (Index c = 0; c < nc; ++c) {
      const Real next = temperature[c] + settings.temperatureRelaxation *
                                             (solvedEnergy.solution[c] - temperature[c]);
      if (!std::isfinite(next)) {
        result.diagnostic = "the TRANSPORT half produced a non-finite temperature at cell " +
                            std::to_string(c) + stateSummary();
        return PremixedFlameStatus::NonFiniteState;
      }
      if (next < temperatureFloor || next > temperatureCeiling) {
        result.diagnostic =
            "the TRANSPORT half drove the temperature at cell " + std::to_string(c) + " to " +
            exact(next) + ", outside the divergence bracket [" + exact(temperatureFloor) + ", " +
            exact(temperatureCeiling) +
            "]. Transport carries no reaction source and obeys a "
            "maximum principle, so this is an assembly or boundary defect, not stiffness." +
            stateSummary();
        return PremixedFlameStatus::NonFiniteState;
      }
      temperature[c] = next;
    }
    for (Index i = 0; i < ns; ++i) {
      for (Index c = 0; c < nc; ++c) {
        const Real next = composition[i][c] + settings.compositionRelaxation *
                                                  (nextComposition[i][c] - composition[i][c]);
        if (!std::isfinite(next)) {
          return PremixedFlameStatus::NonFiniteState;
        }
        composition[i][c] = next;
      }
    }
    // ---- THE CHEMISTRY HALF OF THE SPLIT, over the same dt, with its own adaptive sub-stepping.
    if (settings.reactionsEnabled) {
      densityFromState(composition, temperature, density);
      const auto chemistryOutcome = advanceChemistryAtConstantPressure(
          mechanism, mixture, *chemistry.formation, *chemistry.kinetics, conditions.pressure, cp,
          dt, settings.chemistryFractionalChange, settings.maximumChemistrySubSteps, composition,
          temperature, density);
      result.chemistrySubSteps += chemistryOutcome.subSteps;
      if (chemistryOutcome.failed) {
        result.diagnostic =
            "the CHEMISTRY half failed: " + chemistryOutcome.message + stateSummary();
        return PremixedFlameStatus::NonFiniteState;
      }
      for (Index c = 0; c < nc; ++c) {
        if (temperature[c] < temperatureFloor || temperature[c] > temperatureCeiling) {
          result.diagnostic = "the CHEMISTRY half drove the temperature at cell " +
                              std::to_string(c) + " to " + exact(temperature[c]) +
                              ", outside the divergence bracket [" + exact(temperatureFloor) +
                              ", " + exact(temperatureCeiling) + "]" + stateSummary();
          return PremixedFlameStatus::NonFiniteState;
        }
      }

      // What the dt controller steers on. `densityOld` still holds the density at the START of this
      // step, so this is exactly the jump the continuity closure had to turn into a velocity.
      lastDensityChange = 0.0;
      for (Index c = 0; c < nc; ++c) {
        if (densityOld[c] > 0.0) {
          lastDensityChange =
              std::max(lastDensityChange, std::abs(density[c] - densityOld[c]) / densityOld[c]);
        }
      }
      worstDensityChange = std::max(worstDensityChange, lastDensityChange);
    }

    ++totalSteps;
    return PremixedFlameStatus::Converged;
  };

  // -----------------------------------------------------------------------------------------
  // ONE TRIAL: march `steps` steps at a fixed mass flux and return the front's mean drift over
  // the SECOND HALF, so the structure's response to the change in mdot is not what is measured.
  //
  // The SIGN is what the search uses, and it is the robust part: at a fixed mdot a healthy flame
  // is washed downstream when mdot exceeds the eigenvalue and flashes upstream when it falls
  // short. The MAGNITUDE is only reported.
  // -----------------------------------------------------------------------------------------
  // A trial's outcome. EXTINCTION IS AN OUTCOME WITH A SIDE, not a failure: at a mass flux well
  // above the eigenvalue the front is convected away faster than it can propagate and dies
  // downstream, and well below it the front flashes back into the cold inlet and quenches there.
  // Both extremes go out, so a search that treated extinction as "give up" would abort on its own
  // first trial whenever the seed was off -- which is exactly what happened before this was fixed.
  // The direction of the front's NET TRAVEL before it died is what names the side.
  struct Trial {
    bool valid{false};         // a drift was measured
    bool settled{false};       // the drift stopped changing, rather than the budget running out
    bool extinguished{false};  // the flame went out during the trial
    bool hasSide{false};       // a measured drift assigned this trial to a side
    bool tooLarge{false};      // that side
    Real drift{};
    Real position{};
    Real timeStep{};
    Index steps{};
  };

  // EACH TRIAL IS INDEPENDENT, re-seeded from the same profile. Warm-starting from the previous
  // trial would be cheaper, but a trial that extinguished leaves a dead field behind and every
  // later trial would then inherit it -- the search would measure the order its own trials
  // happened to run in. Re-seeding costs one structure-formation transient per trial, which is
  // why the drift is averaged over the SECOND HALF of the march rather than the whole of it.
  const auto runTrial = [&](Real trialMassFlux) -> Trial {
    Trial trial;

    ramp = seedRampFor(trialMassFlux);
    seedProfile();
    // TD-004: FORGET THE TRACKED FRONT. The locator takes the crossing nearest the previous one,
    // so that state is only meaningful for a continuous march. Re-seeding discontinuously moves
    // the front back to the anchor, and carrying the previous trial's position across that would
    // make the new front look like an inadmissible jump -- which is exactly what happened: every
    // trial reported no front, the bisection collapsed, and S_L came back 29.56 cm/s instead of
    // 4.86 with a zero-step final march. Caught by the probe's own "independent variable did not
    // move" guard, logs/11.
    haveLocated = false;
    temperatureOld = temperature;
    compositionOld = composition;
    densityFromState(composition, temperature, densityOld);
    cumulativeShift = 0.0;

    const Real targetTimeStep = trialTimeStep(trialMassFlux);
    if (!std::isfinite(targetTimeStep) || !(targetTimeStep > 0.0)) {
      marchStatus = PremixedFlameStatus::InvalidConfiguration;
      result.diagnostic = "the trial time step came out as " + exact(targetTimeStep) +
                          " for mdot = " + exact(trialMassFlux);
      return trial;
    }
    dt = targetTimeStep / settings.startupTimeStepDivisor;
    lastDensityChange = 0.0;
    trial.timeStep = targetTimeStep;
    result.timeStep = targetTimeStep;

    // Elapsed TIME, not step count: dt varies while the ramp is active, so a drift computed as
    // (distance per step)/dt would be wrong by the whole ramp factor. Positions are paired with
    // the times at which they were measured.
    Real elapsed = 0.0;
    std::vector<Real> times;
    times.reserve(static_cast<std::size_t>(settings.maximumStepsPerTrial));

    // A SLIDING-WINDOW drift, kept throughout the march rather than only at the end. If the flame
    // later dies, the last window still carries a MEASURED direction of travel, which is what
    // names the side. An earlier version inferred the side from the accumulated recentring and
    // defaulted to "downstream" when nothing had been recentred yet -- a fail-open that read every
    // ambiguous trial as "mdot too large" and halved the mass flux 13 times in a row without ever
    // bracketing anything.
    const Index window = std::max<Index>(4, settings.driftWindow);
    const Real inflowSpeed = trialMassFlux / rhoUnburnt;
    const Real settleBound = settings.driftSettleTolerance * inflowSpeed;

    // A TRIAL MAY NOT READ ITS DRIFT UNTIL THE STRUCTURE HAS RELAXED, and that time is set by
    // diffusion across the preheat zone:
    //
    //     delta_preheat = alpha_burnt / S     t_relax = delta_preheat^2/alpha_burnt =
    //     alpha_burnt/S^2
    //
    // The old fixed 128 steps was about HALF a relaxation time at the eigenvalue, and every
    // outstanding symptom traced to it: the final march at the "converged" mdot flashed the flame
    // 206 cells back to the inlet, S_L moved 4.3% with the drift window, and the fuel balance sat
    // 42% out because the measured state was a recentring limit cycle rather than a steady flame.
    // t_relax = delta_preheat^2 / alpha_u = alpha_u / S^2, with the PREHEAT scale, not the
    // burnt-side diffusivity -- see the note at alphaUnburnt.
    const Real relaxationTime = alphaUnburnt / (inflowSpeed * inflowSpeed);
    const Real relaxationSteps = settings.relaxationTimesPerTrial * relaxationTime / targetTimeStep;
    const Index requiredSteps =
        std::max(settings.minimumStepsPerTrial,
                 static_cast<Index>(
                     std::min(relaxationSteps, static_cast<Real>(settings.maximumStepsPerTrial))));

    std::vector<Real> history;
    history.reserve(static_cast<std::size_t>(settings.maximumStepsPerTrial));

    Real previousWindowDrift = 0.0;
    bool havePreviousWindow = false;
    Index settledWindows = 0;

    for (Index step = 0; step < settings.maximumStepsPerTrial; ++step) {
      marchStatus = advance(trialMassFlux);
      if (marchStatus != PremixedFlameStatus::Converged) {
        // A TRIAL THAT DIVERGED IS A TRIAL OUTCOME, NOT A FATAL ERROR -- the same correction
        // extinction already needed. A mass flux far from the eigenvalue genuinely blows the flame
        // out or drives it into the inlet, and the march can diverge while doing so; aborting the
        // whole search there means the bracketing phase can never widen past its first bad guess.
        // The side is taken from the last window that was measured, and left UNDECIDED if there
        // was none.
        trial.extinguished = true;
        trial.steps = step;
        // ---- TD-037: READ THE DRIFT OVER WHATEVER HISTORY EXISTS, not only over a full window.
        //
        // This used to require `history.size() > window`. A trial that extinguished before
        // accumulating more than `driftWindow` positions therefore produced no drift, no side, and
        // aborted the entire search -- `trialSaysTooLarge` fails closed, which is correct, but it
        // was being handed an unmeasurable trial by a perfectly legitimate setting.
        //
        // MEASURED: the trial at mdot 0.0870611 lives 40 steps, and is sided at `driftWindow` 32
        // but unsided at 64. Before TD-032's time-level fix the same trial lived 71 steps and the
        // old condition held with 7 steps of margin against a window of 64 -- so the constraint
        // was always there and was never guaranteed, merely unviolated. Evidence:
        // `results/technical_debt/TD037_drift_window_guard/`.
        //
        // THE FLOOR OF 4 IS THIS CODE'S OWN STANDARD, not a new tolerance. `window` above is
        // `std::max<Index>(4, settings.driftWindow)`, so a 4-position span is already the smallest
        // window this solver is willing to measure. A trial with less history than that is still
        // refused and still left unsided: this change makes a measurement POSSIBLE where the data
        // supports one, and never invents a side where it does not.
        //
        // THE NORMAL PATH IS DELIBERATELY UNTOUCHED. It reads at window boundaries by design --
        // `(step % window) == window - 1` -- and a trial that is still marching is not short of
        // history. Only extinction can end a trial before its first boundary.
        constexpr std::size_t minimumDriftSpan = 4;
        if (history.size() > minimumDriftSpan) {
          const std::size_t back = history.size() - 1;
          const std::size_t effectiveWindow = std::min(static_cast<std::size_t>(window), back);
          const std::size_t ago = back - effectiveWindow;
          const Real span = times[back] - times[ago];
          if (span > 0.0) {
            trial.drift = (history[back] - history[ago]) / span;
            trial.hasSide = true;
            trial.tooLarge = (trial.drift > 0.0);
          }
        }
        // The search continues; the diagnostic from the diverged step is kept for the report.
        marchStatus = PremixedFlameStatus::Converged;
        return trial;
      }
      Real position = 0.0;
      const Real jumpCells = admissibleJumpCells(trialMassFlux, dt);
      maximumShiftCells = settings.recentreDeadBandCells + jumpCells;
      if (!locateFront(position, jumpCells * cellSize)) {
        trial.extinguished = true;
        trial.steps = step;
        // The side comes from the drift over whatever history exists. With too little history to
        // measure one at all, the trial is left UNDECIDED on purpose: guessing is what broke the
        // search before.
        //
        // TD-037: this is the SECOND of two early-return sites that read the extinction drift --
        // the other is the `marchStatus != Converged` branch above, and the two blocks must stay
        // in step. They were byte-identical, and fixing only that one left THIS path, which is the
        // one a flame going out actually takes (`locateFront` stops finding a crossing), still
        // requiring a full window. The frozen gate's C1 caught it. See the fuller derivation of
        // the floor at the other site.
        constexpr std::size_t minimumDriftSpan = 4;
        if (history.size() > minimumDriftSpan) {
          const std::size_t back = history.size() - 1;
          const std::size_t effectiveWindow = std::min(static_cast<std::size_t>(window), back);
          const std::size_t ago = back - effectiveWindow;
          const Real span = times[back] - times[ago];
          if (span > 0.0) {
            trial.drift = (history[back] - history[ago]) / span;
            trial.hasSide = true;
            trial.tooLarge = (trial.drift > 0.0);
          }
        }
        return trial;
      }
      history.push_back(position + cumulativeShift);
      elapsed += dt;
      times.push_back(elapsed);
      trial.position = history.back();
      recentre(position);

      // Grow dt toward the mesh-derived target. Done AFTER the step, so the step just taken and
      // the time just accumulated used the same dt. There is no density-change cut: see the
      // header for why the controller that used to be here was removed rather than retuned.
      dt = std::min(targetTimeStep, dt * settings.timeStepGrowth);

      // The drift over the window just completed, compared with the one before it. The trial ends
      // when they agree to within driftSettleTolerance * u_in, for the required number of windows
      // in a row -- which is the statement "the propagation speed has stopped changing".
      if (history.size() > static_cast<std::size_t>(window) && (step % window) == window - 1) {
        const std::size_t back = history.size() - 1;
        const std::size_t ago = back - static_cast<std::size_t>(window);
        const Real span = times[back] - times[ago];
        if (!(span > 0.0)) {
          continue;
        }
        const Real windowDrift = (history[back] - history[ago]) / span;

        // THE RAMP MUST BE FINISHED before a window counts toward settling: a window spanning a
        // changing dt is measuring the startup, not the propagation.
        if (dt < targetTimeStep) {
          previousWindowDrift = windowDrift;
          havePreviousWindow = true;
          settledWindows = 0;
          continue;
        }

        if (havePreviousWindow && std::abs(windowDrift - previousWindowDrift) < settleBound) {
          ++settledWindows;
        } else {
          settledWindows = 0;
        }
        previousWindowDrift = windowDrift;
        havePreviousWindow = true;

        trial.drift = windowDrift;
        trial.hasSide = true;
        trial.tooLarge = (windowDrift > 0.0);
        trial.steps = step + 1;

        if (settledWindows >= settings.settledWindowsRequired && step + 1 >= requiredSteps) {
          trial.valid = true;
          trial.settled = true;
          return trial;
        }
      }
    }

    // The budget ran out before the drift settled. The last window is still reported, and the
    // trial is marked UNSETTLED so the caller can see that rather than infer convergence.
    trial.valid = trial.hasSide;
    trial.settled = false;
    return trial;
  };

  // Which side of the eigenvalue a trial puts its mass flux on. FAILS CLOSED: a trial that never
  // measured a drift returns false and the search reports it rather than inventing a side.
  const auto trialSaysTooLarge = [](const Trial& t, bool& tooLarge) -> bool {
    if (!t.hasSide) {
      return false;
    }
    tooLarge = t.tooLarge;
    return true;
  };

  // Record every trial, whatever it decided. See PremixedFlameTrial for why this is evidence.
  const auto recordTrial = [&](Real tried, const Trial& t, bool tooLarge) {
    PremixedFlameTrial entry;
    entry.massFlux = tried;
    entry.drift = t.drift;
    entry.measured = t.hasSide;
    entry.settled = t.settled;
    entry.extinguished = t.extinguished;
    entry.tooLarge = tooLarge;
    entry.timeStep = t.timeStep;
    entry.steps = t.steps;
    result.trials.push_back(entry);
  };

  // ---- FINALISING, through one path.
  //
  // Every return from here on goes through this, including the failure returns. An earlier
  // version filled the profiles only on the success path, so a caller that got NoFlame or
  // NonFiniteState received a result whose `massFractions` was an EMPTY vector -- and a
  // diagnostic that indexed it segfaulted. A half-built result is worse than no result: a
  // non-converged flame is still the evidence that has to be reported (CLAUDE.md section 1),
  // so the state is always reported, whatever the status says about it.
  const auto finalise = [&]() -> PremixedFlameResult& {
    // ---- the reported state, whatever the status: a non-converged flame is still evidence.
    result.massFlux = massFlux;
    result.flameSpeed = massFlux / rhoUnburnt;

    result.x = xCentre;
    result.temperature.assign(nc, 0.0);
    result.density.assign(nc, 0.0);
    result.velocity.assign(nc, 0.0);
    result.heatRelease.assign(nc, 0.0);
    result.massFractions.assign(ns, std::vector<Real>(nc, 0.0));

    result.minMassFraction = 0.0;
    result.maxCompositionSumDeviation = 0.0;
    result.peakTemperature = -std::numeric_limits<Real>::infinity();

    std::vector<Real> cellComposition(ns);
    for (Index c = 0; c < nc; ++c) {
      Real sum = 0.0;
      for (Index i = 0; i < ns; ++i) {
        cellComposition[i] = composition[i][c];
        result.massFractions[i][c] = composition[i][c];
        result.minMassFraction = std::min(result.minMassFraction, composition[i][c]);
        sum += composition[i][c];
      }
      result.maxCompositionSumDeviation =
          std::max(result.maxCompositionSumDeviation, std::abs(sum - 1.0));

      result.temperature[c] = temperature[c];
      result.density[c] = mixture.density(cellComposition, conditions.pressure, temperature[c]);
      result.velocity[c] = massFlux / result.density[c];
      result.peakTemperature = std::max(result.peakTemperature, temperature[c]);
    }

    // ---- THE REPORTED FRONT POSITION, MEASURED FROM THE REPORTED FIELD.
    //
    // This member was assigned `xAnchor` -- the ANCHOR, which is the INPUT
    // `anchorFraction * domainLength` -- and that was its ONLY assignment anywhere in the file.
    // The header documents it as "x where T crosses T_anchor" and it was not that: it reported
    // 3.50000 mm for a state whose real crossing sat 48.2 cells upstream, identically to six
    // figures in both a healthy and a broken solve. The test that existed to guard it compared
    // `anchorFraction * domainLength` against `fraction * domainLength` -- the same expression,
    // so an identity that could not fail for any solver behaviour at all.
    // Evidence: results/technical_debt/TD005_flame_speed_extraction/logs/02_field_dump.log.
    //
    // WHY THIS IS A SEPARATE SCAN AND NOT A CALL TO `locateFront`. That lambda is the CONTROL
    // locator: it tracks the previous position and REFUSES any crossing further away than one
    // step could carry the front. That is correct for deciding whether to translate the frame
    // (TD-004) and wrong for reporting where the front IS -- a report that declines to look is
    // exactly the fail-open the header warns about. It is also why `worstFinalOffsetCells`
    // under-reports: it is updated only on steps where the control locator accepted a crossing,
    // so the large offsets it exists to reveal are the ones it cannot see. This scan is therefore
    // UNCONDITIONAL, and it touches none of the control locator's tracking state.
    //
    // WHICH CROSSING. The FIRST, scanning from the inlet: the upstream edge of the front, which is
    // the one the cold boundary controls. That is the documented definition and it is also the
    // control locator's own first-call behaviour. A profile with more than one crossing already
    // reports `maxFrontCrossings`, so a reader can tell when this choice mattered.
    //
    // 0 means the profile never crosses tAnchor -- nothing is burning. That is more honest than
    // the previous behaviour, which reported the anchor position for a domain with no flame in it.
    //
    // This is READ-ONLY on the final state and feeds nothing back into the march, so every
    // computed answer must be bitwise unchanged by it. That is a gate criterion, not an assumption.
    result.flamePosition = 0.0;
    for (Index c = 0; c + 1 < nc; ++c) {
      const Real below = temperature[c] - tAnchor;
      const Real above = temperature[c + 1] - tAnchor;
      if (below * above <= 0.0 && temperature[c] != temperature[c + 1]) {
        result.flamePosition = xCentre[c] + (tAnchor - temperature[c]) /
                                                (temperature[c + 1] - temperature[c]) *
                                                (xCentre[c + 1] - xCentre[c]);
        break;
      }
    }

    // The heat release and the consumption integral on the FINAL state, recomputed so what is
    // reported belongs to the state that is reported and not to the last iterate before it.
    ReactionProgressRates finalRates = zeroProgressRates(mechanism.reactionCount(), nc);
    if (settings.reactionsEnabled) {
      ScalarField finalDensity(nc);
      for (Index c = 0; c < nc; ++c) {
        finalDensity[c] = result.density[c];
      }
      finalRates = arrheniusProgressRatesFromFields(
          mechanism, *chemistry.kinetics, mixture, composition, finalDensity, temperature,
          NegativeConcentrationPolicy::ZeroRateWhereNegative);
    }
    const SpeciesSources finalSources = reactionSpeciesSources(mechanism, mixture, finalRates, nc);
    const ScalarField finalHeat =
        reactionHeatRelease(mechanism, mixture, *chemistry.formation, finalRates, nc);

    // TD-004: the ledger as a RATE, so it is directly comparable with the flux balances below.
    // A content change of X kg/m^2 spread over the marched time is an apparent flux of X/t, and
    // that is the quantity to weigh against `fuelThrough`.
    if (ledgerElapsed > 0.0) {
      result.recentringFuelLedgerRate = result.recentringFuelLedger / ledgerElapsed;
      result.recentringEnergyLedgerRate = result.recentringEnergyLedger / ledgerElapsed;
    }

    Real fuelIntegral = 0.0;  // integral omega_F dx  [kg/(m^2 s)]
    for (Index c = 0; c < nc; ++c) {
      result.heatRelease[c] = finalHeat[c];
      fuelIntegral += finalSources.perSpecies[conditions.fuelIndex].Su[c] * mesh.cell(c).volume();
    }
    fuelIntegral /= crossSection;

    const Real yFuelUnburnt = conditions.unburntComposition[conditions.fuelIndex];
    result.consumptionSpeed =
        (yFuelUnburnt != 0.0) ? -fuelIntegral / (rhoUnburnt * yFuelUnburnt) : 0.0;

    // ---- THE CONSERVATION BALANCES ON THE REPORTED STATE. See PremixedFlameResult for why the
    //      obvious checks on this solver are vacuous and these are not: the seeded profile already
    //      holds Y_fuel == 0 and T == T_ad at the outlet before any chemistry has run, so
    //      "the fuel is consumed" and "the peak reaches T_ad" both pass on the initial condition.
    //      These are integral identities that hold for a converged steady flame and nothing else.
    {
      const Index last = nc - 1;
      const Real halfCell = 0.5 * (domain / static_cast<Real>(nc));
      const Real lambdaInlet = conditions.conductivity->value(temperature[0]);
      const Real gammaInlet = lambdaInlet / (cp * conditions.lewisNumber);

      // The inlet is Dirichlet half a cell upstream of the first centroid, which is the same
      // geometry the assembler applies to that boundary face.
      const Real fuelInflux =
          gammaInlet * (composition[conditions.fuelIndex][0] - yFuelUnburnt) / halfCell;
      const Real heatInflux = lambdaInlet * (temperature[0] - tUnburnt) / halfCell;

      Real heatIntegral = 0.0;
      for (Index c = 0; c < nc; ++c) {
        heatIntegral += finalHeat[c] * mesh.cell(c).volume();
      }
      heatIntegral /= crossSection;

      const Real fuelThrough = massFlux * yFuelUnburnt;
      if (fuelThrough > 0.0) {
        const Real convected = massFlux * (composition[conditions.fuelIndex][last] - yFuelUnburnt);
        result.fuelBalanceResidual = std::abs(convected - fuelIntegral - fuelInflux) / fuelThrough;
      }
      const Real heatThrough = massFlux * cp * (tAdiabatic - tUnburnt);
      if (heatThrough > 0.0) {
        const Real convectedHeat = massFlux * cp * (temperature[last] - tUnburnt);
        result.energyBalanceResidual =
            std::abs(convectedHeat - heatIntegral - heatInflux) / heatThrough;
      }
    }

    // The diffusive flame thickness, from the steepest resolved gradient. Central differences
    // inside, one-sided at the ends.
    Real steepest = 0.0;
    for (Index c = 0; c < nc; ++c) {
      const Index lo = (c == 0) ? 0 : c - 1;
      const Index hi = (c + 1 == nc) ? c : c + 1;
      if (hi == lo) {
        continue;
      }
      steepest = std::max(
          steepest, std::abs((temperature[hi] - temperature[lo]) / (xCentre[hi] - xCentre[lo])));
    }
    result.flameThickness = (steepest > 0.0) ? (tAdiabatic - tUnburnt) / steepest : 0.0;

    result.totalTimeSteps = totalSteps;
    result.worstLinearResidual = worstLinearResidual;
    result.worstDensityChange = worstDensityChange;
    return result;
  };

  ramp = seedRampFor(massFlux);
  seedProfile();
  // TD-004: FORGET THE TRACKED FRONT. The locator takes the crossing nearest the previous one,
  // so that state is only meaningful for a continuous march. Re-seeding discontinuously moves
  // the front back to the anchor, and carrying the previous trial's position across that would
  // make the new front look like an inadmissible jump -- which is exactly what happened: every
  // trial reported no front, the bisection collapsed, and S_L came back 29.56 cm/s instead of
  // 4.86 with a zero-step final march. Caught by the probe's own "independent variable did not
  // move" guard, logs/11.
  haveLocated = false;
  densityFromState(composition, temperature, densityOld);
  temperatureOld = temperature;
  compositionOld = composition;

  // =========================================================================================
  // PHASE 1 -- BRACKET the eigenvalue by the sign of the drift.
  // =========================================================================================
  Real low = 0.0;   // the front flashes upstream here: mdot too SMALL
  Real high = 0.0;  // the front is washed downstream here: mdot too LARGE
  Index brackets = 0;
  bool decided = false;

  while (brackets <= settings.maximumBracketSteps) {
    const Trial trial = runTrial(massFlux);
    if (marchStatus != PremixedFlameStatus::Converged) {
      result.status = marchStatus;
      return finalise();
    }
    bool tooLarge = false;
    const bool sided = trialSaysTooLarge(trial, tooLarge);
    recordTrial(massFlux, trial, tooLarge);
    if (!sided) {
      result.status = PremixedFlameStatus::NoFlame;
      return finalise();
    }
    if (trial.valid) {
      result.frontDrift = trial.drift;
      decided = true;
    }
    if (tooLarge) {
      high = massFlux;
    } else {
      low = massFlux;
    }
    if (low > 0.0 && high > 0.0) {
      break;  // bracketed
    }
    ++brackets;
    if (brackets > settings.maximumBracketSteps) {
      break;
    }
    massFlux = (high == 0.0) ? massFlux * 2.0 : massFlux * 0.5;
  }
  result.bracketSteps = brackets;
  static_cast<void>(decided);

  // =========================================================================================
  // PHASE 2 -- BISECT. The GEOMETRIC midpoint, because mdot is a positive scale that the
  // bracketing may have moved by factors of two, not an additive offset.
  // =========================================================================================
  if (low > 0.0 && high > 0.0) {
    Index bisections = 0;
    while (bisections < settings.maxOuterIterations && (high - low) / low > settings.tolerance) {
      ++bisections;
      massFlux = std::sqrt(low * high);
      const Trial trial = runTrial(massFlux);
      if (marchStatus != PremixedFlameStatus::Converged) {
        result.status = marchStatus;
        return finalise();
      }
      bool tooLarge = false;
      const bool sided = trialSaysTooLarge(trial, tooLarge);
      recordTrial(massFlux, trial, tooLarge);
      if (!sided) {
        result.status = PremixedFlameStatus::NoFlame;
        break;
      }
      if (tooLarge) {
        high = massFlux;
      } else {
        low = massFlux;
      }
      if (trial.valid) {
        result.frontDrift = trial.drift;
      }
    }
    result.outerIterations = bisections;
    result.massFluxResidual = (high - low) / low;
    if (result.status != PremixedFlameStatus::NoFlame) {
      // The reported eigenvalue is the bracket's geometric centre: it is bracketed, so the
      // remaining uncertainty is the reported residual and not an unknown.
      massFlux = std::sqrt(low * high);
      result.status = (result.massFluxResidual <= settings.tolerance)
                          ? PremixedFlameStatus::Converged
                          : PremixedFlameStatus::MaxIterations;

      // ---- ONE FINAL MARCH AT THE CONVERGED MASS FLUX, so the reported STATE belongs to the
      //      reported flame speed.
      //
      // Without this the profiles are whichever trial happened to run last -- at a mass flux the
      // bisection had already rejected -- while `massFlux` is the bracket centre. The symptom was
      // unmistakable once the search started converging: the consumption integral came back at
      // 2.58 cm/s against a reported S_L of 4.50 cm/s. That consistency check only means anything
      // if both numbers describe the same solution, and reporting a profile that does not
      // correspond to the quoted eigenvalue would make every profile error in the evidence a
      // measurement of the wrong state.
      // AND IT IS MARCHED TO A STEADY STATE, not merely until the drift settles. Those are
      // different conditions: the front can stop moving while the profile behind it is still
      // filling in. Measured consequence -- a state marched only until the drift settled (128
      // steps, about 3.6 flame transit times) gave a consumption integral of 2.58 cm/s against a
      // flame speed of 4.50 cm/s, a 43% violation of an identity that is exact for a steady flame.
      ramp = seedRampFor(massFlux);
      seedProfile();
      // TD-004: FORGET THE TRACKED FRONT. The locator takes the crossing nearest the previous one,
      // so that state is only meaningful for a continuous march. Re-seeding discontinuously moves
      // the front back to the anchor, and carrying the previous trial's position across that would
      // make the new front look like an inadmissible jump -- which is exactly what happened: every
      // trial reported no front, the bisection collapsed, and S_L came back 29.56 cm/s instead of
      // 4.86 with a zero-step final march. Caught by the probe's own "independent variable did not
      // move" guard, logs/11.
      haveLocated = false;
      temperatureOld = temperature;
      compositionOld = composition;
      densityFromState(composition, temperature, densityOld);
      cumulativeShift = 0.0;
      // TD-004: the ledger is RESET here, so what it reports belongs to the final march -- the
      // same state the conservation balances below are computed on. The trials' own recentring is
      // not part of that state and folding it in would make the rate incomparable with the
      // balances.
      result.recentringFuelLedger = 0.0;
      result.recentringEnergyLedger = 0.0;
      ledgerElapsed = 0.0;
      const Real finalTarget = trialTimeStep(massFlux);
      dt = finalTarget / settings.startupTimeStepDivisor;

      // Enough relaxation times for the structure to settle, capped by the safety ceiling. See
      // PremixedFlameSettings::finalMarchRelaxationTimes for why a fixed count wasted 37 minutes
      // per solve.
      const Real finalSpeed = massFlux / rhoUnburnt;
      const Real finalRelaxation = alphaUnburnt / (finalSpeed * finalSpeed);
      const Index finalBudget =
          std::min(settings.finalMarchSteps,
                   std::max<Index>(settings.minimumStepsPerTrial,
                                   static_cast<Index>(settings.finalMarchRelaxationTimes *
                                                      finalRelaxation / finalTarget)));

      for (Index step = 0; step < finalBudget; ++step) {
        const ScalarField previousTemperature = temperature;
        const std::vector<ScalarField> previousComposition = composition;

        marchStatus = advance(massFlux);
        if (marchStatus != PremixedFlameStatus::Converged) {
          result.status = marchStatus;
          break;
        }
        ++result.finalMarchSteps;
        ledgerElapsed += dt;

        Real position = 0.0;
        bool recentredThisStep = false;  // TD-031: separates a frame translation from the physics
        const Real jumpCells = admissibleJumpCells(massFlux, dt);
        maximumShiftCells = settings.recentreDeadBandCells + jumpCells;
        if (locateFront(position, jumpCells * cellSize)) {
          const Index before = result.recentrings;
          // The single-step JUMP in the located position, which distinguishes a creeping front
          // from the locator latching onto a different crossing. `previousFrontPosition` is
          // seeded on the first step so the first comparison is not against zero.
          if (havePreviousFront) {
            result.worstFrontJumpCells = std::max(
                result.worstFrontJumpCells, std::abs(position - previousFrontPosition) / cellSize);
          }
          previousFrontPosition = position;
          havePreviousFront = true;
          result.worstFinalOffsetCells =
              std::max(result.worstFinalOffsetCells, std::abs(position - xAnchor) / cellSize);
          recentre(position);
          if (result.recentrings != before) {
            ++result.finalMarchRecentrings;
            recentredThisStep = true;
          }
        }
        dt = std::min(finalTarget, dt * settings.timeStepGrowth);

        // Steadiness is only judged once the ramp has finished, for the same reason a drift window
        // does not count during the ramp: the change per step is still dominated by dt growing.
        if (dt < finalTarget) {
          continue;
        }
        Real temperatureChange = 0.0;
        Real compositionChange = 0.0;
        // TD-031: the argmax is tracked alongside the max so the change can be LOCALISED. The
        // `if (d > max)` form selects exactly what `std::max(max, d)` did -- neither updates on a
        // tie -- so `temperatureChange` is bitwise what it was before this diagnostic was added.
        Index worstCell = 0;
        for (Index c = 0; c < nc; ++c) {
          const Real cellChange = std::abs(temperature[c] - previousTemperature[c]);
          if (cellChange > temperatureChange) {
            temperatureChange = cellChange;
            worstCell = c;
          }
          for (Index i = 0; i < ns; ++i) {
            compositionChange = std::max(compositionChange,
                                         std::abs(composition[i][c] - previousComposition[i][c]));
          }
        }
        temperatureChange /= (tAdiabatic - tUnburnt);
        result.temperatureResidual = temperatureChange;
        result.compositionResidual = compositionChange;

        // TD-031 diagnostics -- read-only, and deliberately AFTER the two assignments above so
        // nothing here can affect them. The steadiness break below is untouched.
        if (temperatureChange > result.worstStateChange) {
          result.worstStateChange = temperatureChange;
          result.worstStateChangeCell = worstCell;
          result.worstStateChangeWasRecentred = recentredThisStep;
        }
        if (!recentredThisStep) {
          result.worstStateChangeNoRecentre =
              std::max(result.worstStateChangeNoRecentre, temperatureChange);
        }
        if (temperatureChange < settings.finalMarchTolerance &&
            compositionChange < settings.finalMarchTolerance) {
          result.steadyStateReached = true;
          break;
        }
      }
    }
  } else if (result.status != PremixedFlameStatus::NoFlame) {
    result.status = PremixedFlameStatus::MaxIterations;
  }

  result.totalTimeSteps = totalSteps;

  return finalise();
}

}  // namespace cfd::species
