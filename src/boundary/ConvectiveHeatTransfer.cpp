#include "cfd/boundary/ConvectiveHeatTransfer.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

ConvectiveHeatTransfer::ConvectiveHeatTransfer(Real heatTransferCoefficient,
                                               Real ambientTemperature)
    : heatTransferCoefficient_(heatTransferCoefficient), ambientTemperature_(ambientTemperature) {
  if (!std::isfinite(heatTransferCoefficient_) || heatTransferCoefficient_ < 0.0) {
    throw InvalidArgumentError(
        "ConvectiveHeatTransfer: heat transfer coefficient must be finite and >= 0");
  }
  if (!std::isfinite(ambientTemperature_)) {
    throw InvalidArgumentError("ConvectiveHeatTransfer: ambient temperature must be finite");
  }
}

BoundaryConditionType ConvectiveHeatTransfer::type() const noexcept {
  return BoundaryConditionType::ConvectiveHeatTransfer;
}

std::string_view ConvectiveHeatTransfer::name() const noexcept { return "ConvectiveHeatTransfer"; }

Real ConvectiveHeatTransfer::boundaryValue(Real ownerValue, Real) const {
  // Returns the ZERO-GRADIENT equivalent. The only caller that reaches it is
  // the gradient operator.
  //
  // This threw in the first implementation, on the argument that a plausible
  // wrong value would be consumed silently by every generic assembler. That
  // argument holds for the generic ASSEMBLY path -- and its rejection now
  // lives in cfd::transport, the one place it can be reached -- but it does
  // not hold here:
  //
  //   * the thermal assembly intercepts this condition BY TYPE before ever
  //     calling boundaryValue, so the assembled coefficients never use it;
  //   * discretization::gradient() DOES call it, on every boundary face, when
  //     building the correction gradient. Throwing there killed every
  //     convective case before any physics ran -- found by the first run of
  //     the thermal validation suite;
  //   * that gradient feeds ONLY the non-orthogonal correction, which is
  //     exactly zero on an orthogonal face. On an orthogonal mesh this value
  //     has no effect at all; on a non-orthogonal one it perturbs a
  //     correction term slightly.
  //
  // The physically meaningful quantities remain conductancePerArea() and
  // wallTemperature(), which take k explicitly. Nothing computes a Robin flux
  // from this return value.
  return ownerValue;
}

Real ConvectiveHeatTransfer::conductancePerArea(Real conductivity, Real normalDistance) const {
  if (!std::isfinite(conductivity) || !(conductivity > 0.0)) {
    throw InvalidArgumentError("ConvectiveHeatTransfer: conductivity must be finite and > 0");
  }
  if (!std::isfinite(normalDistance) || !(normalDistance > 0.0)) {
    throw InvalidArgumentError("ConvectiveHeatTransfer: normal distance must be finite and > 0");
  }
  // Series resistance: wall conduction d/k, then the film 1/h_c.
  // h_c == 0 is the insulated limit -- infinite film resistance, zero
  // conductance. Handled explicitly rather than by dividing by zero.
  if (heatTransferCoefficient_ == 0.0) return 0.0;
  return 1.0 / (normalDistance / conductivity + 1.0 / heatTransferCoefficient_);
}

Real ConvectiveHeatTransfer::wallTemperature(Real ownerValue, Real conductivity,
                                             Real normalDistance) const {
  if (!std::isfinite(ownerValue)) {
    throw InvalidArgumentError("ConvectiveHeatTransfer: owner temperature must be finite");
  }
  if (!std::isfinite(conductivity) || !(conductivity > 0.0)) {
    throw InvalidArgumentError("ConvectiveHeatTransfer: conductivity must be finite and > 0");
  }
  if (!std::isfinite(normalDistance) || !(normalDistance > 0.0)) {
    throw InvalidArgumentError("ConvectiveHeatTransfer: normal distance must be finite and > 0");
  }
  // Eliminating T_w between  -k (T_w - T_P)/d = h_c (T_w - T_inf):
  //   T_w = ( (k/d) T_P + h_c T_inf ) / ( k/d + h_c )
  const Real wallConductance = conductivity / normalDistance;
  if (heatTransferCoefficient_ == 0.0) {
    // Insulated: no heat crosses, so the wall sits at the owner temperature.
    return ownerValue;
  }
  return (wallConductance * ownerValue + heatTransferCoefficient_ * ambientTemperature_) /
         (wallConductance + heatTransferCoefficient_);
}

}  // namespace cfd::boundary
