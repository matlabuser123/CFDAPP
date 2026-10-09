#include "cfd/compressible/ThermodynamicProperties.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::compressible {

ThermodynamicProperties::ThermodynamicProperties(Real gasConstant, Real specificHeatPressure)
    : eos_(gasConstant), specificHeatPressure_(specificHeatPressure) {
  if (!std::isfinite(specificHeatPressure_) || !(specificHeatPressure_ > gasConstant)) {
    throw InvalidArgumentError(
        "ThermodynamicProperties: specificHeatPressure (cp) must be finite and > gasConstant (R), "
        "so that cv = cp - R > 0");
  }
  specificHeatVolume_ = specificHeatPressure_ - gasConstant;
  specificHeatRatio_ = specificHeatPressure_ / specificHeatVolume_;
}

Real ThermodynamicProperties::gasConstant() const noexcept { return eos_.gasConstant(); }
Real ThermodynamicProperties::specificHeatPressure() const noexcept {
  return specificHeatPressure_;
}
Real ThermodynamicProperties::specificHeatVolume() const noexcept { return specificHeatVolume_; }
Real ThermodynamicProperties::specificHeatRatio() const noexcept { return specificHeatRatio_; }

const EquationOfState& ThermodynamicProperties::equationOfState() const noexcept { return eos_; }

Real ThermodynamicProperties::density(Real pressure, Real temperature) const {
  return eos_.density(pressure, temperature);
}

Real ThermodynamicProperties::speedOfSound(Real temperature) const {
  if (!std::isfinite(temperature) || !(temperature > 0.0)) {
    throw InvalidArgumentError(
        "ThermodynamicProperties::speedOfSound: temperature must be finite "
        "and > 0");
  }
  return std::sqrt(specificHeatRatio_ * gasConstant() * temperature);
}

Real machNumber(Real speed, Real speedOfSound) {
  if (!std::isfinite(speedOfSound) || !(speedOfSound > 0.0)) {
    throw InvalidArgumentError("machNumber: speedOfSound must be finite and > 0");
  }
  return speed / speedOfSound;
}

cfd::fields::ScalarField evaluateDensityField(const cfd::mesh::Mesh& mesh,
                                              const cfd::fields::ScalarField& pressure,
                                              const cfd::fields::ScalarField& temperature,
                                              const ThermodynamicProperties& thermodynamics) {
  if (pressure.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "evaluateDensityField: pressure size does not match mesh cell count");
  }
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "evaluateDensityField: temperature size does not match mesh cell count");
  }
  cfd::fields::ScalarField density(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    // thermodynamics.density() (via IdealGasEOS::density) already
    // validates p, T, and the resulting rho, naming which one failed --
    // that per-cell context is lost here, so wrap it with the offending
    // cell index.
    try {
      density[i] = thermodynamics.density(pressure[i], temperature[i]);
    } catch (const InvalidArgumentError& e) {
      throw InvalidArgumentError("evaluateDensityField: cell " + std::to_string(i) + ": " +
                                 e.what());
    }
  }
  return density;
}

}  // namespace cfd::compressible
