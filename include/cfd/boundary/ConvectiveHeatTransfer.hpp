#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

// P15 -- the convective (Robin / mixed) thermal boundary condition.
//
//   -k dT/dn = h_c (T_w - T_inf)
//
// A wall losing heat to a fluid at T_inf through a film coefficient h_c. It is
// the boundary a real cooled or heated surface has, and it was the one thermal
// BC CFDApp did not express: FixedTemperature is Dirichlet, HeatFlux and
// Adiabatic are Neumann, and nothing carried the mixed form.
//
// WHY boundaryValue() THROWS, AND WHY THAT IS THE RIGHT DESIGN
// ------------------------------------------------------------
// Eliminating the unknown wall temperature between the Robin condition and the
// one-sided conduction estimate gives a face value
//
//   T_w = ( (k/d) T_P + h_c T_inf ) / ( k/d + h_c )
//
// which depends on the local CONDUCTIVITY k. `ScalarBoundaryCondition::
// boundaryValue(ownerValue, normalDistance)` is not given k, and cannot be:
// k is a material property that varies per cell and, after P15, with
// temperature. A BC that cached its own k would silently disagree with the
// material model the moment k(T) moved.
//
// So this class carries only the physical information -- h_c and T_inf -- and
// the thermal assembly computes the coefficient from the face's own k. That is
// exactly the split BoundaryCondition.hpp's own header states:
//
//   "Boundary conditions specify the mathematical boundary information;
//    discretization operators decide how that information modifies the
//    finite-volume matrix."
//
// boundaryValue() returns the zero-gradient equivalent rather than throwing.
// An earlier version threw, to stop a silently-wrong face value reaching a
// generic assembler -- but that also broke discretization::gradient(), which
// evaluates every boundary face and legitimately needs a value. The rejection
// therefore lives where it can be precise: cfd::transport's diffusion assembly
// refuses this condition by type, which is the one path that could otherwise
// treat a convective wall as adiabatic without complaint.
//
// CONSEQUENCE, STATED PLAINLY: the P14 generic scalar transport path does NOT
// support this BC -- it refuses it with a named error. Thermal assembly handles
// it. Generalising it would mean passing Gamma into the boundary query, which
// changes a base-class signature used by every scalar and vector BC; that is a
// deliberate future change, not a P15 side effect.

namespace cfd::boundary {

class ConvectiveHeatTransfer final : public ScalarBoundaryCondition {
 public:
  // Throws InvalidArgumentError if heatTransferCoefficient is not finite or
  // < 0, or ambientTemperature is not finite.
  //
  // h_c == 0 IS allowed and is meaningful: it is a perfectly insulated wall,
  // the adiabatic limit of this condition. It is not rejected, because a study
  // sweeping h_c down to zero should not hit a cliff at the physically
  // sensible end of its range.
  ConvectiveHeatTransfer(Real heatTransferCoefficient, Real ambientTemperature);

  [[nodiscard]] Real heatTransferCoefficient() const noexcept { return heatTransferCoefficient_; }
  [[nodiscard]] Real ambientTemperature() const noexcept { return ambientTemperature_; }

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  // Returns the ZERO-GRADIENT equivalent (the owner value). See the .cpp for
  // why this does not throw: the thermal assembly intercepts this condition by
  // type and never calls it, while the gradient operator legitimately needs a
  // value and uses it only for the non-orthogonal correction -- which is
  // exactly zero on an orthogonal face. The generic transport path refuses
  // this condition explicitly instead (cfd::transport).
  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;

  // The face temperature implied by the condition once the local conductance
  // IS known. Used by the thermal assembly, and separately testable against a
  // hand calculation.
  //
  //   T_w = ( (k/d) T_P + h_c T_inf ) / ( k/d + h_c )
  //
  // Throws InvalidArgumentError if conductivity or normalDistance is not
  // finite, or normalDistance <= 0.
  [[nodiscard]] Real wallTemperature(Real ownerValue, Real conductivity, Real normalDistance) const;

  // The series conductance per unit area: 1 / ( d/k + 1/h_c ), the wall
  // conduction resistance and the film resistance in series. Returns 0 when
  // h_c == 0 (the insulated limit), which is the correct answer and not a
  // division by zero.
  [[nodiscard]] Real conductancePerArea(Real conductivity, Real normalDistance) const;

 private:
  Real heatTransferCoefficient_{};
  Real ambientTemperature_{};
};

}  // namespace cfd::boundary
