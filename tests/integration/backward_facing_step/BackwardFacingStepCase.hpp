#pragma once

// P12-NUM-007 -- the 2D laminar backward-facing step of Gartling (1990),
// "A test problem for outflow boundary conditions -- flow over a
// backward-facing step", Int. J. Numer. Methods Fluids 11, 953-967:
// expansion ratio 2, the domain starting AT the step (no upstream channel).
//
// Geometry capability (the minimal one NUM-007 needs, not a meshing
// feature): the fluid domain downstream of the step is a plain rectangle
// [0, L] x [0, H] (H = 1, step height h = H/2), so the existing
// MeshGeometry::createCartesian2D mesh is used unchanged and only its
// left boundary is re-partitioned into
//   * "step"          -- the vertical step face, y in [0, h): no-slip wall;
//   * "inlet_<face>"  -- one patch per face of y in [h, H): Inlet with the
//                        face-AVERAGE of the parabola u = 24 (y-h)(H-y)
//                        (mean 1, max 1.5), so the inflow rate is exactly
//                        U_mean (H - h) = 0.5 on every grid.
// No blocked cells, no new mesh type, no library change: the Mesh
// constructor already takes arbitrary boundary patches.
//
// Re = U_mean H / nu (Gartling's definition; ER = 2 makes it equal to the
// hydraulic-diameter Reynolds number of the inlet channel).

#include <optional>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"
#include "cfd/validation/ProductionValidation.hpp"

namespace cfd::validation::step {

inline constexpr Real kChannelHeight = 1.0;  // H
inline constexpr Real kStepHeight = 0.5;     // h
inline constexpr Real kMeanInletVelocity = 1.0;
inline constexpr Real kDensity = 1.0;

struct StepSpec {
  Real reynolds{800.0};
  Index cellsPerHeight{20};  // ny; nx = cellsPerHeight * length
  Real length{15.0};         // L / H
  discretization::ConvectionScheme scheme{discretization::ConvectionScheme::QUICK};
};

// nx x ny Cartesian mesh of [0, L] x [0, H] with the left boundary split
// as described above. ny must be even (the step edge on a face line).
[[nodiscard]] mesh::Mesh makeStepMesh(const StepSpec& spec);

// Face-average of u = 24 (y-h)(H-y) over [y0, y1] within the inlet.
[[nodiscard]] Real averagedInletVelocity(Real y0, Real y1);

struct StepBoundaries {
  boundary::BoundaryConditionSet velocity;
  boundary::BoundaryConditionSet pressure;
};
// Velocity: inlet patches Inlet(face-average, 0), "step"/"bottom"/"top"
// Wall, "right" Outlet. Pressure: zero gradient except FixedValue(0) at
// the outlet.
[[nodiscard]] StepBoundaries makeStepBoundaries(const mesh::Mesh& mesh);

// A sign change of a sampled wall-shear distribution tau(x): location by
// linear interpolation between the two straddling samples, and whether
// tau goes from negative to positive (downstream).
struct ShearZero {
  Real x{0.0};
  bool negativeToPositive{false};
};
// Every zero crossing of tau sampled at increasing x (a sample exactly 0
// counts as the crossing point). Throws std::invalid_argument on size
// mismatch or fewer than 2 samples.
[[nodiscard]] std::vector<ShearZero> shearZeroCrossings(const std::vector<Real>& x,
                                                        const std::vector<Real>& tau);

// Wall shear along the bottom wall (y = 0) and the top wall (y = H) at the
// wall-adjacent cell centres, tau = mu du/dn (inward normal) over the half
// cell: bottom tau_b = mu u_P / (dy/2), top tau_t = -mu u_P / (dy/2) --
// positive for the attached downstream flow on the bottom wall, negative
// for the attached flow on the top wall.
struct WallShear {
  std::vector<Real> x;
  std::vector<Real> bottom;
  std::vector<Real> top;
};
[[nodiscard]] WallShear wallShear(const mesh::Mesh& mesh, Index nx, Index ny,
                                  const fields::VectorField& velocity, Real viscosity);

// The separation topology read off the wall shear, algorithmically (no
// visual estimate):
//   reattachment  -- the downstream-most negative -> positive crossing of
//                    the bottom-wall shear (the primary eddy's end; the
//                    shear is positive from there to the outlet);
//   corner eddy   -- the positive-shear region at the step foot, if
//                    resolved: its end is the positive -> negative crossing
//                    immediately upstream of the reattachment (the start
//                    of the primary eddy's reversed shear);
//   upper bubble  -- the first top-wall crossing where the attached
//                    (negative) top shear turns positive (separation) and
//                    the next one back (reattachment), if present.
// All lengths in units of the step height h, measured from the step.
struct SeparationTopology {
  std::optional<Real> reattachment;
  std::optional<Real> cornerEddyEnd;
  std::optional<Real> upperSeparation;
  std::optional<Real> upperReattachment;
  std::size_t bottomCrossings{0};
  std::size_t topCrossings{0};
};
[[nodiscard]] SeparationTopology separationTopology(const WallShear& shear);

// Solves spec with SIMPLE (alpha 0.7/0.3, outer tolerances 1e-6, Jacobi-
// preconditioned BiCGSTAB pressure solve 1e-12/1e-6, momentum 1e-12/1e-8,
// P12-NUM-004 fallback enabled) from rest and records it: diagnostics
// x_r/h, corner-eddy end, upper-wall separation / reattachment / bubble
// length, inflow / outflow / section flows; checks solve_accepted,
// mass_flow (|Q - 0.5| <= 1e-6 at inlet, outlet and 4 sections),
// wall_normal_flux (<= 1e-6).
[[nodiscard]] ValidationRun runStep(const StepSpec& spec);

}  // namespace cfd::validation::step
