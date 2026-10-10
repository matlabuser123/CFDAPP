#include "cfd/compressible/HllcFlux.hpp"

#include <string>

#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Exception.hpp"

// P17.7 -- the HOST-ONLY half of the HLLC layer.
//
// Everything numerical lives in the header, compiled by both gcc and nvcc from one source. This
// file holds only what cannot exist in device code:
//   * makePrimitiveState(), which builds a face state through P17.2's ThermodynamicProperties and
//     P17.5's TotalEnergyState. This is the ONLY place a PrimitiveState is given its p, T, a and
//     rhoE, so there is exactly one authoritative thermodynamic path and none inside the flux.
//   * hllcFluxChecked(), which turns the header's StateValidity into the project's
//     InvalidArgumentError with a message that names the offending quantity.
//
// Nothing here re-derives a formula that the header already contains.

namespace cfd::compressible {

namespace {

constexpr const char* kFactory = "makePrimitiveState";

const char* describe(StateValidity v) {
  switch (v) {
    case StateValidity::Valid:
      return "valid";
    case StateValidity::NonFiniteDensity:
      return "density must be finite";
    case StateValidity::NonPositiveDensity:
      return "density must be > 0";
    case StateValidity::NonFinitePressure:
      return "pressure must be finite";
    case StateValidity::NonPositivePressure:
      return "pressure must be > 0";
    case StateValidity::NonFiniteVelocity:
      return "every velocity component must be finite";
    case StateValidity::NonFiniteTotalEnergy:
      return "the total-energy density rhoE must be finite";
    case StateValidity::NonFiniteSoundSpeed:
      return "the speed of sound (or gamma) must be finite and admissible";
    case StateValidity::NonPositiveSoundSpeed:
      return "the speed of sound must be > 0";
    case StateValidity::NonFiniteNormal:
      return "every face-normal component must be finite";
    case StateValidity::NonUnitNormal:
      return "the face normal must be a UNIT normal -- pass the area separately";
  }
  return "unknown validity code";
}

}  // namespace

PrimitiveState makePrimitiveState(const TotalEnergyState& thermodynamics, Real density,
                                  Vector3 velocity, Real temperature) {
  // Delegate the value checks to the authoritative models rather than repeating them: P17.2's
  // speedOfSound validates the temperature and P17.5's conservedTotalEnergy validates density,
  // temperature and velocity. Duplicating those checks here would create a second place where the
  // admissible set is defined, which is the kind of divergence P17.5's dual-cp hazard was about.
  if (!std::isfinite(density) || !(density > 0.0)) {
    throw InvalidArgumentError(std::string(kFactory) + ": density must be finite and > 0");
  }

  PrimitiveState s{};
  s.density = density;
  s.velocity = velocity;
  s.temperature = temperature;

  const ThermodynamicProperties& thermo = thermodynamics.thermodynamics();
  s.speedOfSound = thermo.speedOfSound(temperature);  // P17.2 -- the ONLY sound speed
  s.pressure = thermo.equationOfState().pressure(density, temperature);  // P17.2 -- the ONLY EOS
  s.totalEnergyDensity = thermodynamics.conservedTotalEnergy(density, temperature,
                                                             velocity);  // P17.5 -- the ONLY rhoE

  const StateValidity v = validateState(s);
  if (v != StateValidity::Valid) {
    throw InvalidArgumentError(std::string(kFactory) + ": " + describe(v));
  }
  return s;
}

PrimitiveState makePrimitiveStateFromConserved(const TotalEnergyState& thermodynamics,
                                               const ConservedState& q) {
  // conserved -> primitive goes through P17.5's recover(), which is the only inverse in the
  // project. It throws on a non-finite or non-positive density, a non-finite rhoE or momentum,
  // kinetic energy exceeding the available total energy, or a temperature outside the property
  // model's range -- and it clamps NOTHING. That policy is inherited here deliberately: an energy
  // floor would let a diverging solve look converged.
  const auto recovered = thermodynamics.recover(q.density, q.momentumDensity, q.totalEnergyDensity);

  PrimitiveState s{};
  s.density = q.density;
  s.velocity = recovered.velocity;
  s.temperature = recovered.temperature;
  s.pressure = recovered.pressure;  // ABSOLUTE, from P17.2's EOS -- never the gamma form
  s.speedOfSound = thermodynamics.thermodynamics().speedOfSound(recovered.temperature);
  s.totalEnergyDensity = q.totalEnergyDensity;

  const StateValidity v = validateState(s);
  if (v != StateValidity::Valid) {
    throw InvalidArgumentError(std::string("makePrimitiveStateFromConserved: ") + describe(v));
  }
  return s;
}

HllcResult hllcFluxChecked(const PrimitiveState& left, const PrimitiveState& right,
                           const Vector3& unitNormal, Real gamma, WaveSpeedEstimator estimator) {
  const HllcResult out = hllcFlux(left, right, unitNormal, gamma, estimator);
  if (!out.valid) {
    throw InvalidArgumentError(std::string("hllcFluxChecked: ") + describe(out.validity));
  }
  // The star states are PROVEN admissible for admissible inputs (the denominator bound in
  // HllcFlux.hpp), so reaching either of these is a defect in this layer, not bad input. They are
  // checked anyway -- a proof that is never tested is an assumption.
  if (!(out.starDensityLeft > 0.0) || !(out.starDensityRight > 0.0)) {
    throw NumericalError(
        "hllcFluxChecked: a star density is not positive, which is impossible for admissible "
        "inputs under the proven denominator bound -- this indicates a defect in the flux layer");
  }
  if (!std::isfinite(out.flux.mass) || !std::isfinite(out.flux.energy) ||
      !isFinite(out.flux.momentum)) {
    throw NumericalError("hllcFluxChecked: the flux is not finite for admissible inputs");
  }
  return out;
}

const char* describeValidity(StateValidity v) { return describe(v); }

}  // namespace cfd::compressible
