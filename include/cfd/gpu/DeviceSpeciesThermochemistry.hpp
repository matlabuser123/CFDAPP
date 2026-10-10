#pragma once

// P18.8 -- the P18 thermochemistry, written ONCE for both host and device.
//
// This header is NOT CUDA-only. Like HllcFlux.hpp and DeviceThermodynamics.hpp it uses
// CFD_HLLC_HOST_DEVICE, which expands to `__host__ __device__` under nvcc and to NOTHING under
// gcc, so the same text compiles into the CPU build and into a kernel. That is what satisfies
// G9's condition (1), "one shared source compiles for both" -- and it also lets the
// cross-comparison tests run in the CPU-ONLY configuration, so the restatements below are checked
// against production even on a machine with no GPU.
//
// ===================================================================
// WHY ANYTHING IS RESTATED AT ALL
//
// P18.3's MixtureThermodynamics holds a std::vector of SpeciesThermodynamics, each holding a
// SensibleEnthalpy holding a std::vector-backed TemperatureDependentProperty. P18.4's
// ReactionMechanism holds std::string names. None of that can cross to the device.
//
// But the ARITHMETIC is a handful of scalars per species and per reaction, and P18.8 ships those
// as flat PODs. The restatement is therefore of the FORMULAS, not of the validation -- which
// stays on the host, because a device cannot throw (audit.md section 6).
//
// ===================================================================
// EVERY EXPRESSION BELOW IS PARENTHESISED THE WAY THE CPU WRITES IT.
//
// That is not style. P17.10 learned it the hard way and DeviceThermodynamics.hpp records it: the
// CPU computes `(rho * Y_i) / M_i`, which is a multiply then a divide; writing
// `rho * (Y_i / M_i)` is the same number in exact arithmetic and a DIFFERENT one in floating
// point. The CPU source each formula mirrors is cited at the formula, so a reader can check the
// parenthesisation rather than trust it. kernel_inventory.md section 4 lists every place where the
// obvious simplification is the wrong one.
//
// ACCUMULATION ORDER IS ALSO THE CPU'S: ascending species index, and for multi-reaction sources
// ascending reaction index then ascending species index. Both are cell-local loops, so there is no
// cross-thread reduction and G9's conditions (2) and (5) are satisfied vacuously.
//
// COMPILED WITH --fmad=false, like every other qualified kernel (G9's condition (4), already the
// per-source convention in cuda/CMakeLists.txt).

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace species_detail {

// Host/device shims. Explicit rather than relying on std:: being available in device code --
// the same approach hllc_detail uses.
CFD_HLLC_HOST_DEVICE inline Real absR(Real v) {
#if defined(__CUDA_ARCH__)
  return ::fabs(v);
#else
  return std::abs(v);
#endif
}
CFD_HLLC_HOST_DEVICE inline Real expR(Real v) {
#if defined(__CUDA_ARCH__)
  return ::exp(v);
#else
  return std::exp(v);
#endif
}
CFD_HLLC_HOST_DEVICE inline Real logR(Real v) {
#if defined(__CUDA_ARCH__)
  return ::log(v);
#else
  return std::log(v);
#endif
}
CFD_HLLC_HOST_DEVICE inline Real powR(Real a, Real b) {
#if defined(__CUDA_ARCH__)
  return ::pow(a, b);
#else
  return std::pow(a, b);
#endif
}
CFD_HLLC_HOST_DEVICE inline bool finiteR(Real v) {
#if defined(__CUDA_ARCH__)
  return ::isfinite(v);
#else
  return std::isfinite(v);
#endif
}

}  // namespace species_detail

// R_u, CODATA 2018. The SAME literal P18.3 defines, restated rather than included so this header
// has no dependency on the host species headers -- and asserted equal to P18.3's value by the
// equivalence suite, so the two cannot drift.
inline constexpr Real kDeviceUniversalGasConstant = 8.31446261815324;

// The maximum species and reaction counts a device state carries. Fixed so the PODs are flat and
// need no device allocation; the equivalence suite asserts the test mechanisms fit.
inline constexpr Index kMaxDeviceSpecies = 8;
inline constexpr Index kMaxDeviceReactions = 4;

// ===================================================================
// The PODs
// ===================================================================

// One species' scalars, in place of the whole SpeciesThermodynamics + FormationEnthalpies pair.
//
// specificHeat IS A CONSTANT, not a polynomial. P18.8's device path covers the CALORICALLY PERFECT
// case only, for the same reason P17.10's DeviceThermodynamics does: a temperature-dependent cp is
// a std::vector-backed polynomial that cannot cross, and widening it is not P18.8's scope. The
// host-side guard is that the equivalence suite CONSTRUCTS its mixtures with constant cp, and the
// kernel inventory records the limitation.
struct DeviceSpecies {
  Real molecularWeight{};    // M_i   [kg/mol]
  Real gasConstant{};        // R_i = R_u / M_i, COMPUTED ON THE HOST so it cannot drift
  Real specificHeat{};       // cp_i  [J/(kg K)], constant
  Real formationEnthalpy{};  // h_f,i [J/kg]
};

// One reaction's stoichiometry, dense over the registry.
struct DeviceReaction {
  Real net[kMaxDeviceSpecies]{};       // nu_i = nu''_i - nu'_i
  Real reactant[kMaxDeviceSpecies]{};  // nu'_i, the ORDER
  Real product[kMaxDeviceSpecies]{};   // nu''_i
  bool reversible{false};
};

// One direction's Arrhenius parameters.
struct DeviceArrhenius {
  Real preExponential{};    // A
  Real exponent{};          // n
  Real activationEnergy{};  // Ea  [J/mol]
};

// The whole mechanism as a flat POD, uploaded once per solve and never mutated.
struct DeviceMechanism {
  Index speciesCount{};
  Index reactionCount{};
  DeviceSpecies species[kMaxDeviceSpecies]{};
  DeviceReaction reactions[kMaxDeviceReactions]{};
  DeviceArrhenius forward[kMaxDeviceReactions]{};
  DeviceArrhenius reverse[kMaxDeviceReactions]{};
};

// Why an operation was refused. The device cannot throw, so the reason is carried out as a value --
// and the equivalence suite compares THE REASON, not merely the fact of refusal, because two
// backends rejecting the same state for different reasons is a divergence in the admissible set.
// This mirrors P17.10's DeviceRecoveryStatus exactly.
enum class DeviceSpeciesStatus {
  Ok = 0,
  NonFiniteTemperature = 1,
  NonPositiveTemperature = 2,
  NonFiniteDensity = 3,
  NonPositiveDensity = 4,
  NonFiniteComposition = 5,
  CompositionOutOfBounds = 6,
  CompositionSumInvalid = 7,
  NonFiniteMolecularWeightSum = 8,
  NonPositiveSpecificHeatVolume = 9,
  NonFiniteRate = 10,
  NonFiniteSource = 11,
  NonFiniteEnergy = 12,
  SensibleEnergyNotPositive = 13,
  SpeciesWouldGoNegative = 14,
  TemperatureNotBracketed = 15
};

// FIRST FAILURE WINS, and that is a semantic choice the equivalence gate depends on.
//
// The CPU throws at the FIRST invalid thing it meets, so its reason is always the ROOT CAUSE. A
// device status written last-write-wins would instead name whatever failed LAST -- a downstream
// consequence. The two backends would then reject the same state for DIFFERENT REASONS, which E11
// calls a divergence in the admissible set, and G9 asks for "the same first-offending cell".
//
// This was not hypothetical: the first version of this header overwrote the status freely, and
// N11_SilentlyAcceptingAnInvalidStateIsDetected caught it -- a T < 0 state reported
// NonFiniteEnergy (a consequence) instead of NonPositiveTemperature (the cause). Preserved at
// logs/02_first_failure_PRESERVED.log.
CFD_HLLC_HOST_DEVICE inline void setStatusOnce(DeviceSpeciesStatus* status,
                                               DeviceSpeciesStatus reason) {
  if (status != nullptr && *status == DeviceSpeciesStatus::Ok) {
    *status = reason;
  }
}

// The control switches that make every negative control of the frozen gate a FLAG rather than a
// source edit -- the pattern DeviceThermodynamics.hpp established. All false is the production
// path, and the equivalence suite asserts that the all-false case is the one that matches.
struct DeviceSpeciesControls {
  bool permuteSpeciesIndex{false};         // N1
  bool oneWeightForAllSpecies{false};      // N2
  bool flipArrheniusExponentSign{false};   // N4
  bool omitMolecularWeightInOmega{false};  // N5
  bool flipHeatReleaseSign{false};         // N6
  bool omitLastReaction{false};            // N8
  bool acceptInvalidState{false};          // N11
};

// ===================================================================
// Mixture properties -- P18.3
// ===================================================================

// 1 / M_mix = sum_i Y_i / M_i, then M_mix = 1 / that.
// CPU: src/species/MixtureThermodynamics.cpp:118-126
//          reciprocal += composition[i] / species_[i].molecularWeight();
//          return 1.0 / reciprocal;
// ASCENDING SPECIES INDEX, a divide per term, one reciprocal at the end. NOT
// 1.0 / sum(Y_i * (1/M_i)), which is a different number.
CFD_HLLC_HOST_DEVICE inline Real deviceMolecularWeight(const DeviceMechanism& mechanism,
                                                       const Real* composition,
                                                       const DeviceSpeciesControls& controls,
                                                       DeviceSpeciesStatus* status) {
  Real reciprocal = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    const Index source = controls.permuteSpeciesIndex ? ((i + 1) % mechanism.speciesCount) : i;
    const Real weight = controls.oneWeightForAllSpecies ? mechanism.species[0].molecularWeight
                                                        : mechanism.species[source].molecularWeight;
    reciprocal += composition[i] / weight;
  }
  if (!species_detail::finiteR(reciprocal) || !(reciprocal > 0.0)) {
    setStatusOnce(status, DeviceSpeciesStatus::NonFiniteMolecularWeightSum);
    return 0.0;
  }
  return 1.0 / reciprocal;
}

// R_mix = sum_i Y_i R_i, with R_i = R_u / M_i computed on the HOST.
// CPU: src/species/MixtureThermodynamics.cpp:140  total += composition[i] *
// species_[i].gasConstant(); THE SUM OF Y_i R_i, NOT R_u / M_mix. P18.3 exposes both and proves
// they are not bitwise equal -- 17.2% of random doubles fail the 1/(1/x) round trip -- so using the
// other form here would be a defect, not a simplification.
CFD_HLLC_HOST_DEVICE inline Real deviceGasConstant(const DeviceMechanism& mechanism,
                                                   const Real* composition,
                                                   const DeviceSpeciesControls& controls) {
  Real total = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    const Index source = controls.permuteSpeciesIndex ? ((i + 1) % mechanism.speciesCount) : i;
    total += composition[i] * mechanism.species[source].gasConstant;
  }
  return total;
}

// cp_mix = sum_i Y_i cp_i.
// CPU: src/species/MixtureThermodynamics.cpp:160  total += composition[i] *
// species_[i].specificHeatPressure(T);
CFD_HLLC_HOST_DEVICE inline Real deviceSpecificHeatPressure(const DeviceMechanism& mechanism,
                                                            const Real* composition,
                                                            const DeviceSpeciesControls& controls) {
  Real total = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    const Index source = controls.permuteSpeciesIndex ? ((i + 1) % mechanism.speciesCount) : i;
    total += composition[i] * mechanism.species[source].specificHeat;
  }
  return total;
}

// cv_mix = cp_mix - R_mix, and gamma = cp/cv.
// CPU: the mixture builds cfd::compressible::ThermodynamicProperties(rMix, cpMix), whose cv is
// cp - R and whose gamma is cp / cv (ThermodynamicProperties' own ctor).
CFD_HLLC_HOST_DEVICE inline Real deviceSpecificHeatVolume(Real specificHeatP, Real gasConstant) {
  return specificHeatP - gasConstant;
}
CFD_HLLC_HOST_DEVICE inline Real deviceSpecificHeatRatio(Real specificHeatP, Real specificHeatV) {
  return specificHeatP / specificHeatV;
}

// rho = p / (R_mix T).
// CPU: src/compressible/IdealGasEOS.cpp:30  const Real rho = pressure / (gasConstant_ *
// temperature);
CFD_HLLC_HOST_DEVICE inline Real deviceDensityFromPressure(Real pressure, Real gasConstant,
                                                           Real temperature) {
  return pressure / (gasConstant * temperature);
}
// p = rho R_mix T.  CPU: IdealGasEOS::pressure -- (rho*R)*T, left to right.
CFD_HLLC_HOST_DEVICE inline Real devicePressure(Real density, Real gasConstant, Real temperature) {
  return density * gasConstant * temperature;
}
// T = p / (rho R_mix).
CFD_HLLC_HOST_DEVICE inline Real deviceTemperatureFromPressure(Real pressure, Real density,
                                                               Real gasConstant) {
  return pressure / (density * gasConstant);
}

// h_mix = sum_i Y_i h_i(T), with h_i the SENSIBLE enthalpy. For a CONSTANT cp the integral from
// T_ref is cp_i * (T - T_ref), which is what the device carries; the equivalence suite constructs
// its mixtures with constant cp and h_ref = 0 so this restatement is exact.
CFD_HLLC_HOST_DEVICE inline Real deviceSensibleEnthalpy(const DeviceMechanism& mechanism,
                                                        const Real* composition, Real temperature,
                                                        Real referenceTemperature) {
  Real total = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    total +=
        composition[i] * (mechanism.species[i].specificHeat * (temperature - referenceTemperature));
  }
  return total;
}

// ===================================================================
// Arrhenius -- P18.5, including amendment A1
// ===================================================================

// x = -Ea / (R_u T).
// CPU: src/species/ArrheniusKinetics.cpp:69  return -activationEnergy_ / (kUniversalGasConstant *
// temperature);
CFD_HLLC_HOST_DEVICE inline Real deviceExponentialArgument(const DeviceArrhenius& parameters,
                                                           Real temperature,
                                                           const DeviceSpeciesControls& controls) {
  const Real ea = controls.flipArrheniusExponentSign ? -parameters.activationEnergy
                                                     : parameters.activationEnergy;
  return -ea / (kDeviceUniversalGasConstant * temperature);
}

// ln k = ln A + n ln T + x, summed in the CPU's fixed order.
// CPU: ArrheniusKinetics.cpp:79-80
CFD_HLLC_HOST_DEVICE inline Real deviceLogRateCoefficient(const DeviceArrhenius& parameters,
                                                          Real temperature,
                                                          const DeviceSpeciesControls& controls) {
  return species_detail::logR(parameters.preExponential) +
         (parameters.exponent * species_detail::logR(temperature)) +
         deviceExponentialArgument(parameters, temperature, controls);
}

// k(T), AMENDMENT A1: the direct form where A T^n is finite, log space as the overflow fallback.
// CPU: ArrheniusKinetics.cpp:83-128. The A == 0 exact branch, the isfinite(A T^n) predicate and
// the fallback are all mirrored, INCLUDING the order of the two multiplies.
CFD_HLLC_HOST_DEVICE inline Real deviceRateCoefficient(const DeviceArrhenius& parameters,
                                                       Real temperature,
                                                       const DeviceSpeciesControls& controls,
                                                       DeviceSpeciesStatus* status,
                                                       int* branchTaken) {
  if (parameters.preExponential == 0.0) {
    if (branchTaken != nullptr) {
      *branchTaken = 0;  // the exact A == 0 branch
    }
    return 0.0;
  }
  const Real powerLaw =
      parameters.preExponential * species_detail::powR(temperature, parameters.exponent);
  if (species_detail::finiteR(powerLaw)) {
    const Real direct = powerLaw * species_detail::expR(deviceExponentialArgument(
                                       parameters, temperature, controls));
    if (!species_detail::finiteR(direct)) {
      setStatusOnce(status, DeviceSpeciesStatus::NonFiniteRate);
      if (branchTaken != nullptr) {
        *branchTaken = 3;  // direct, refused
      }
      return direct;
    }
    if (branchTaken != nullptr) {
      *branchTaken = 1;  // DIRECT
    }
    return direct;
  }
  const Real viaLog =
      species_detail::expR(deviceLogRateCoefficient(parameters, temperature, controls));
  if (!species_detail::finiteR(viaLog)) {
    setStatusOnce(status, DeviceSpeciesStatus::NonFiniteRate);
    if (branchTaken != nullptr) {
      *branchTaken = 4;  // fallback, refused
    }
    return viaLog;
  }
  if (branchTaken != nullptr) {
    *branchTaken = 2;  // LOG-SPACE FALLBACK
  }
  return viaLog;
}

// ===================================================================
// Progress rates and sources -- P18.5 and P18.4
// ===================================================================

// C_i = (rho Y_i) / M_i.
// CPU: ArrheniusKinetics.cpp:254  (rho * state.massFraction(i, cell)) /
// mixture.species(i).molecularWeight() A MULTIPLY THEN A DIVIDE. rho * (Y_i / M_i) is a different
// number.
CFD_HLLC_HOST_DEVICE inline Real deviceConcentration(const DeviceMechanism& mechanism,
                                                     Index species, Real density,
                                                     Real massFraction) {
  return (density * massFraction) / mechanism.species[species].molecularWeight;
}

// Pi_i C_i^(order_i) over ASCENDING SPECIES INDEX, with a zero concentration short-circuiting to
// EXACTLY zero so a zero never reaches pow().
// CPU: ArrheniusKinetics.cpp's concentrationProduct, same loop, same short circuit.
CFD_HLLC_HOST_DEVICE inline Real deviceConcentrationProduct(const DeviceMechanism& mechanism,
                                                            Index reactionIndex,
                                                            const Real* concentrations,
                                                            bool forwardSide) {
  const DeviceReaction& reaction = mechanism.reactions[reactionIndex];
  Real product = 1.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    const Real order = forwardSide ? reaction.reactant[i] : reaction.product[i];
    if (order == 0.0) {
      continue;
    }
    const Real c = concentrations[i];
    if (c == 0.0) {
      return 0.0;
    }
    product *= species_detail::powR(c, order);
  }
  return product;
}

// q = k_f Pi C^(nu') - k_r Pi C^(nu''), the second term only when reversible.
// CPU: ArrheniusKinetics.cpp:332-346, same two multiplies and the same subtraction.
CFD_HLLC_HOST_DEVICE inline Real deviceProgressRate(const DeviceMechanism& mechanism,
                                                    Index reactionIndex, const Real* concentrations,
                                                    Real temperature,
                                                    const DeviceSpeciesControls& controls,
                                                    DeviceSpeciesStatus* status) {
  int branch = -1;
  const Real kForward = deviceRateCoefficient(mechanism.forward[reactionIndex], temperature,
                                              controls, status, &branch);
  const Real qForward =
      kForward * deviceConcentrationProduct(mechanism, reactionIndex, concentrations, true);
  Real q = qForward;
  if (mechanism.reactions[reactionIndex].reversible) {
    const Real kReverse = deviceRateCoefficient(mechanism.reverse[reactionIndex], temperature,
                                                controls, status, &branch);
    const Real qReverse =
        kReverse * deviceConcentrationProduct(mechanism, reactionIndex, concentrations, false);
    q = qForward - qReverse;
  }
  if (!species_detail::finiteR(q)) {
    setStatusOnce(status, DeviceSpeciesStatus::NonFiniteRate);
  }
  return q;
}

// omega_i = sum_r (M_i nu_{i,r}) q_r, accumulated in ASCENDING REACTION INDEX then ascending
// species index -- the CPU's order.
// CPU: src/species/ReactionMechanism.cpp:329  Su[cell] += (mixture.species(i).molecularWeight() *
// nu) * rate; NOTE THE PARENTHESISATION: (M_i * nu) * q, not M_i * (nu * q).
CFD_HLLC_HOST_DEVICE inline void deviceSpeciesSources(const DeviceMechanism& mechanism,
                                                      const Real* progressRates, Real* omegaOut,
                                                      const DeviceSpeciesControls& controls) {
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    omegaOut[i] = 0.0;
  }
  const Index reactions =
      controls.omitLastReaction ? (mechanism.reactionCount - 1) : mechanism.reactionCount;
  for (Index r = 0; r < reactions; ++r) {
    const Real rate = progressRates[r];
    for (Index i = 0; i < mechanism.speciesCount; ++i) {
      const Real nu = mechanism.reactions[r].net[i];
      if (nu == 0.0) {
        continue;
      }
      const Real weight =
          controls.omitMolecularWeightInOmega ? 1.0 : mechanism.species[i].molecularWeight;
      omegaOut[i] += (weight * nu) * rate;
    }
  }
}

// ===================================================================
// Heat release -- P18.6
// ===================================================================

// Delta H_r^0 = sum_i nu_i (M_i h_f,i), ascending species index.
// CPU: src/species/HeatRelease.cpp  total += nu * formation.molar(mixture, i), where molar is
// massSpecific(i) * molecularWeight(). So the product is (h_f * M), then times nu.
CFD_HLLC_HOST_DEVICE inline Real deviceStandardReactionEnthalpy(const DeviceMechanism& mechanism,
                                                                Index reactionIndex) {
  Real total = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    const Real nu = mechanism.reactions[reactionIndex].net[i];
    if (nu == 0.0) {
      continue;
    }
    total += nu * (mechanism.species[i].formationEnthalpy * mechanism.species[i].molecularWeight);
  }
  return total;
}

// S_E = - sum_r q_r Delta H_r^0, ascending reaction index.
// CPU: HeatRelease.cpp  source[cell] -= q * deltaH;
CFD_HLLC_HOST_DEVICE inline Real deviceHeatRelease(const DeviceMechanism& mechanism,
                                                   const Real* progressRates,
                                                   const DeviceSpeciesControls& controls,
                                                   DeviceSpeciesStatus* status) {
  Real source = 0.0;
  const Index reactions =
      controls.omitLastReaction ? (mechanism.reactionCount - 1) : mechanism.reactionCount;
  for (Index r = 0; r < reactions; ++r) {
    const Real deltaH = deviceStandardReactionEnthalpy(mechanism, r);
    source -= progressRates[r] * deltaH;
  }
  if (controls.flipHeatReleaseSign) {
    source = -source;
  }
  if (!species_detail::finiteR(source)) {
    setStatusOnce(status, DeviceSpeciesStatus::NonFiniteSource);
  }
  return source;
}

// S_E = - sum_i h_f,i omega_i -- G7's own formula, the INDEPENDENT route.
// CPU: HeatRelease.cpp  source[cell] -= hf * su[cell];
CFD_HLLC_HOST_DEVICE inline Real deviceHeatReleaseFromSources(const DeviceMechanism& mechanism,
                                                              const Real* omega) {
  Real source = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    source -= mechanism.species[i].formationEnthalpy * omega[i];
  }
  return source;
}

// ===================================================================
// Energy accounting and temperature recovery -- P18.6
// ===================================================================

// The sensible pool, rho cp(T,Y) T.
// CPU: HeatRelease.cpp  density * mixture.specificHeatPressure(composition, temperature) *
// temperature LEFT TO RIGHT: (rho * cp) * T.
CFD_HLLC_HOST_DEVICE inline Real deviceSensibleEnergyDensity(
    const DeviceMechanism& mechanism, const Real* composition, Real density, Real temperature,
    const DeviceSpeciesControls& controls) {
  return density * deviceSpecificHeatPressure(mechanism, composition, controls) * temperature;
}

// The chemical pool, rho sum_i Y_i h_f,i, ascending species index.
// CPU: HeatRelease.cpp  chemical += composition[i] * formation.massSpecific(i);  then  density *
// chemical
CFD_HLLC_HOST_DEVICE inline Real deviceChemicalEnergyDensity(const DeviceMechanism& mechanism,
                                                             const Real* composition,
                                                             Real density) {
  Real chemical = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    chemical += composition[i] * mechanism.species[i].formationEnthalpy;
  }
  return density * chemical;
}

// T from the sensible energy density: solve rho cp(T,Y) T = u_sens.
//
// FOR A CONSTANT cp THE INVERSE IS CLOSED-FORM, and the CPU's bisection converges to it -- but
// "converges to" is not "is", so this is NOT a restatement of the CPU's bisection. The equivalence
// suite therefore compares the device value against the CPU's BISECTION RESULT and requires
// bitwise agreement, which holds only because the device runs the SAME bisection, below, with the
// same bracket and the same iteration rule.
// CPU: src/species/HeatRelease.cpp temperatureFromSensibleEnergy.
CFD_HLLC_HOST_DEVICE inline Real deviceTemperatureFromSensibleEnergy(
    const DeviceMechanism& mechanism, const Real* composition, Real density,
    Real sensibleEnergyDensity, Real minimumTemperature, Real maximumTemperature,
    const DeviceSpeciesControls& controls, DeviceSpeciesStatus* status) {
  if (!species_detail::finiteR(density) || !(density > 0.0)) {
    setStatusOnce(status, DeviceSpeciesStatus::NonPositiveDensity);
    return 0.0;
  }
  if (!species_detail::finiteR(sensibleEnergyDensity)) {
    setStatusOnce(status, DeviceSpeciesStatus::NonFiniteEnergy);
    return 0.0;
  }
  if (sensibleEnergyDensity <= 0.0) {
    setStatusOnce(status, DeviceSpeciesStatus::SensibleEnergyNotPositive);
    return 0.0;
  }

  Real low = minimumTemperature;
  Real high = maximumTemperature;
  if (low <= 0.0) {
    low = 1.0e-6;
  }
  auto residual = [&](Real t) {
    return (density * deviceSpecificHeatPressure(mechanism, composition, controls) * t) -
           sensibleEnergyDensity;
  };
  if (residual(low) > 0.0 || residual(high) < 0.0) {
    if (!controls.acceptInvalidState) {
      setStatusOnce(status, DeviceSpeciesStatus::TemperatureNotBracketed);
    }
    return 0.0;
  }
  // 200 halvings, exiting when the interval is one ulp wide -- the CPU's rule exactly.
  for (int iteration = 0; iteration < 200; ++iteration) {
    const Real mid = 0.5 * (low + high);
    if (mid <= low || mid >= high) {
      break;
    }
    if (residual(mid) < 0.0) {
      low = mid;
    } else {
      high = mid;
    }
  }
  return 0.5 * (low + high);
}

// ===================================================================
// The coupled one-step -- P18.6's advanceClosedReactor, cell-local
// ===================================================================

// Everything one cell of the closed adiabatic reactor needs, and everything it produces. The
// equivalence suite compares EVERY FIELD, which is what section 19's "compare every intermediate
// quantity" and G9's "compared on the STATE" require.
struct DeviceReactorStep {
  Real concentration[kMaxDeviceSpecies]{};
  Real progressRate[kMaxDeviceReactions]{};
  Real rateCoefficient[kMaxDeviceReactions]{};
  int branch[kMaxDeviceReactions]{};
  Real omega[kMaxDeviceSpecies]{};
  Real omegaSum{};
  Real heatRelease{};
  Real heatReleaseFromSources{};
  Real sensibleBefore{};
  Real sensibleAfter{};
  Real chemicalBefore{};
  Real chemicalAfter{};
  Real compositionAfter[kMaxDeviceSpecies]{};
  Real temperatureAfter{};
  Real molecularWeight{};
  Real gasConstant{};
  Real specificHeatP{};
  Real specificHeatV{};
  Real specificHeatRatio{};
  DeviceSpeciesStatus status{DeviceSpeciesStatus::Ok};
};

// ONE STEP, in P18.6's frozen order: rates -> omega -> S_E -> species -> energy -> T recovery.
// `staleTemperature` and `staleComposition` are controls N7 and N9.
CFD_HLLC_HOST_DEVICE inline void deviceAdvanceReactorCell(
    const DeviceMechanism& mechanism, const Real* composition, Real density, Real temperature,
    Real dt, Real minimumTemperature, Real maximumTemperature,
    const DeviceSpeciesControls& controls, bool staleTemperature, bool staleComposition,
    Real staleTemperatureValue, DeviceReactorStep* out) {
  out->status = DeviceSpeciesStatus::Ok;

  if (!species_detail::finiteR(temperature)) {
    setStatusOnce(&out->status, DeviceSpeciesStatus::NonFiniteTemperature);
    if (!controls.acceptInvalidState) {
      return;
    }
  } else if (!(temperature > 0.0)) {
    setStatusOnce(&out->status, DeviceSpeciesStatus::NonPositiveTemperature);
    if (!controls.acceptInvalidState) {
      return;
    }
  }
  if (!species_detail::finiteR(density) || !(density > 0.0)) {
    setStatusOnce(&out->status, DeviceSpeciesStatus::NonPositiveDensity);
    if (!controls.acceptInvalidState) {
      return;
    }
  }

  out->molecularWeight = deviceMolecularWeight(mechanism, composition, controls, &out->status);
  out->gasConstant = deviceGasConstant(mechanism, composition, controls);
  out->specificHeatP = deviceSpecificHeatPressure(mechanism, composition, controls);
  out->specificHeatV = deviceSpecificHeatVolume(out->specificHeatP, out->gasConstant);
  out->specificHeatRatio = deviceSpecificHeatRatio(out->specificHeatP, out->specificHeatV);
  if (!(out->specificHeatV > 0.0)) {
    setStatusOnce(&out->status, DeviceSpeciesStatus::NonPositiveSpecificHeatVolume);
    if (!controls.acceptInvalidState) {
      return;
    }
  }

  const Real evaluationTemperature = staleTemperature ? staleTemperatureValue : temperature;

  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    out->concentration[i] = deviceConcentration(mechanism, i, density, composition[i]);
  }
  for (Index r = 0; r < mechanism.reactionCount; ++r) {
    int branch = -1;
    out->rateCoefficient[r] = deviceRateCoefficient(mechanism.forward[r], evaluationTemperature,
                                                    controls, &out->status, &branch);
    out->branch[r] = branch;
    out->progressRate[r] = deviceProgressRate(mechanism, r, out->concentration,
                                              evaluationTemperature, controls, &out->status);
  }

  deviceSpeciesSources(mechanism, out->progressRate, out->omega, controls);
  out->omegaSum = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    out->omegaSum += out->omega[i];
  }

  out->heatRelease = deviceHeatRelease(mechanism, out->progressRate, controls, &out->status);
  out->heatReleaseFromSources = deviceHeatReleaseFromSources(mechanism, out->omega);

  out->sensibleBefore =
      deviceSensibleEnergyDensity(mechanism, composition, density, temperature, controls);
  out->chemicalBefore = deviceChemicalEnergyDensity(mechanism, composition, density);

  // the species update, ADDITIVE IN MASS-FRACTION SPACE -- P18.6's own form, for the reason its
  // header records: the (rho Y + dt omega)/rho round trip is not exact and drifted sum Y past
  // P18.1's tolerance.
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    const Real increment = (dt * out->omega[i]) / density;
    const Real partialDensity = density * (composition[i] + increment);
    if (partialDensity < 0.0) {
      setStatusOnce(&out->status, DeviceSpeciesStatus::SpeciesWouldGoNegative);
      if (!controls.acceptInvalidState) {
        return;
      }
    }
    out->compositionAfter[i] = composition[i] + increment;
  }

  out->sensibleAfter = out->sensibleBefore + (dt * out->heatRelease);
  const Real* recoveryComposition = staleComposition ? composition : out->compositionAfter;
  out->temperatureAfter = deviceTemperatureFromSensibleEnergy(
      mechanism, recoveryComposition, density, out->sensibleAfter, minimumTemperature,
      maximumTemperature, controls, &out->status);
  out->chemicalAfter = deviceChemicalEnergyDensity(mechanism, out->compositionAfter, density);
}

}  // namespace cfd::gpu
