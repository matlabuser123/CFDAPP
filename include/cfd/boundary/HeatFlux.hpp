#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Prescribed wall heat flux via Fourier's law, expressed against the
// mesh's outward face normal -- the same convention every boundary
// face's area vector already uses (Face.hpp / MeshGeometry):
//
//   q'' = -k * dT/dn      =>      dT/dn = -q'' / k
//
// Sign convention (defined once, here -- keep every caller consistent
// with it): POSITIVE heatFlux means heat LEAVING the domain through this
// boundary, i.e. flowing along the outward normal; NEGATIVE means heat
// ENTERING. This is the natural reading of Fourier's law against an
// outward normal: if the domain is losing heat outward, temperature
// decreases in the outward direction (dT/dn < 0), so q'' = -k*dT/dn > 0.
// Worked example: k=2 W/(m K), q''=+10 W/m^2 => dT/dn = -10/2 = -5 K/m
// (temperature falls by 5 K per metre moving outward, consistent with
// heat leaving).
//
// Once dT/dn is computed (at construction, from the given conductivity),
// boundaryValue() reconstructs the face value exactly like
// FixedGradient does: T_boundary = T_owner + dT/dn * normalDistance --
// this class uses the same underlying Neumann machinery, just
// parameterized by a physical heat flux and conductivity instead of a
// raw gradient.
//
// conductivity is supplied here, at construction, rather than read from
// a live thermal::ThermalProperties: the generic ScalarBoundaryCondition
// interface this class implements has no way to reach one at evaluation
// time (BoundaryConditionSet is deliberately physics-agnostic -- see
// BoundaryCondition.hpp's own header comment), so this is the only
// design that fits the existing API without changing it for every other
// boundary condition. Callers are responsible for passing the same
// conductivity used elsewhere for this simulation's ThermalProperties --
// the same class of caveat FixedGradient's raw gradient value already
// carries (nothing here re-validates consistency against a separately
// constructed ThermalProperties).
class HeatFlux final : public ScalarBoundaryCondition {
 public:
  // Throws InvalidArgumentError if heatFlux is not finite, or
  // conductivity is not finite and strictly positive.
  HeatFlux(Real heatFlux, Real conductivity);

  [[nodiscard]] Real heatFlux() const noexcept;
  [[nodiscard]] Real conductivity() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  // Throws InvalidArgumentError if normalDistance is non-finite or <= 0
  // (same FixedGradient::boundaryValue contract).
  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;

 private:
  Real heatFlux_{};
  Real conductivity_{};
};

}  // namespace cfd::boundary
