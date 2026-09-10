#pragma once

#include "cfd/compressible/EquationOfState.hpp"
#include "cfd/compressible/IdealGasEOS.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::compressible {

// P3-PHYS-006: a coherent calorically-perfect-ideal-gas thermodynamic
// state (this task's own section 4) -- owns the one EOS this task
// implements (IdealGasEOS) plus specific heat, deriving cv/gamma from
// (R, cp) so no mutually inconsistent independent values can be
// supplied (section 4's own "avoid storing mutually inconsistent
// independent values" / "if users provide R, cp then derive cv=cp-R,
// gamma=cp/cv" convention). Other, temperature-dependent properties
// (mu(T), k(T)) are deliberately NOT duplicated here -- they already
// have a canonical home in cfd::physics::TemperatureProperty
// (P3-PHYS-003, section 7's own "do not create a competing temperature-
// property system"); a caller combines a ThermodynamicProperties with
// however many TemperatureProperty instances it needs for mu/k
// separately.
class ThermodynamicProperties {
 public:
  // Throws InvalidArgumentError if gasConstant is not finite or <= 0
  // (via the internal IdealGasEOS), or if specificHeatPressure (cp) is
  // not finite or <= gasConstant (cv = cp - R would be <= 0 otherwise --
  // a physically invalid calorically-perfect gas).
  ThermodynamicProperties(Real gasConstant, Real specificHeatPressure);

  [[nodiscard]] Real gasConstant() const noexcept;         // R
  [[nodiscard]] Real specificHeatPressure() const noexcept;  // cp
  [[nodiscard]] Real specificHeatVolume() const noexcept;    // cv = cp - R
  [[nodiscard]] Real specificHeatRatio() const noexcept;     // gamma = cp/cv

  [[nodiscard]] const EquationOfState& equationOfState() const noexcept;

  // Convenience delegate to equationOfState().density(p, T).
  [[nodiscard]] Real density(Real pressure, Real temperature) const;

  // a = sqrt(gamma * R * T) (section 31/32). Throws InvalidArgumentError
  // if temperature is not finite or <= 0.
  [[nodiscard]] Real speedOfSound(Real temperature) const;

 private:
  IdealGasEOS eos_;
  Real specificHeatPressure_{};
  Real specificHeatVolume_{};
  Real specificHeatRatio_{};
};

// Ma = |velocity| / speedOfSound (section 31). Throws InvalidArgumentError
// if speedOfSound is not finite or <= 0.
[[nodiscard]] Real machNumber(Real speed, Real speedOfSound);

// P3-PHYS-006 sections 8-9: the one authoritative per-cell density
// update path -- rho_P = EOS(p_P, T_P) evaluated at every cell, mirroring
// cfd::physics::evaluatePropertyField's role for temperature-dependent
// properties (P3-PHYS-003) and cfd::multiphase::evaluateMixtureDensityField's
// role for mixture density (P3-PHYS-005): "one function every consumer
// calls" rather than "recompute rho inline wherever it's needed" (section
// 9's own explicit "do not independently recompute density inside
// continuity/momentum/energy/pressure correction/post-processing").
// `pressure` here is *absolute* thermodynamic pressure (section 23) --
// callers own the gauge-to-absolute conversion before calling this.
//
// Throws InvalidArgumentError if pressure.size() or temperature.size()
// != mesh.numberOfCells(), or if any (pressure, temperature) pair
// produces an invalid (non-finite or <= 0) density -- the same "reject
// during evaluation, name the offending cell" convention
// evaluatePropertyField already established.
[[nodiscard]] cfd::fields::ScalarField evaluateDensityField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& pressure,
    const cfd::fields::ScalarField& temperature, const ThermodynamicProperties& thermodynamics);

}  // namespace cfd::compressible
