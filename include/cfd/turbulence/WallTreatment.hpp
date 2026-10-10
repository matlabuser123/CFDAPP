#pragma once

#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

// P16 -- applying the wall functions to a mesh.
//
// WallFunctions.hpp is deliberately pure: every formula there is a function of
// its arguments alone, so each can be checked against the log law without
// constructing a solve. This file is the layer that knows about meshes -- which
// faces are walls, which cells are next to them, what the wall-normal distance
// is, and what the wall-parallel velocity is.
//
// WHAT A WALL FUNCTION HAS TO REACH
// ---------------------------------
// Three separate places, and a treatment that reaches only some of them is not
// a wall function:
//
//   1. MOMENTUM -- the wall shear. Without this the solve still feels a laminar
//      wall and the whole exercise is a diagnostic. Delivered as a per-face
//      effective viscosity (see wallViscosity below), so the momentum assembly
//      keeps its existing mu*U_P/y_P boundary term and simply receives a
//      different mu at that face.
//   2. EPSILON (or omega) -- IMPOSED in the wall-adjacent cell, not transported.
//      The epsilon equation is not valid in a cell that spans the log layer.
//   3. PRODUCTION -- replaced in the wall cell by the equilibrium expression,
//      because mu_t*S^2 from a cell-centre gradient badly under-resolves dU/dy
//      exactly where production peaks.
//
// k is left ZERO-GRADIENT at the wall. Imposing both k and epsilon
// over-constrains the wall cell: the standard Launder-Spalding arrangement
// fixes epsilon and lets k respond to the modified production.

namespace cfd::turbulence {

// One boundary face whose velocity condition is Wall or MovingWall.
struct WallFace {
  Index faceId{};
  Index ownerCell{};
  // Perpendicular distance from the owner cell centroid to the face. On a
  // Cartesian mesh this is the familiar half-cell height; the projection makes
  // it the true wall-normal distance on a mesh whose grid lines meet the wall
  // obliquely, the same correction computeWallDistance already applies.
  Real wallDistance{};
  Real area{};
  cfd::Vector2 unitNormal{};
  // Zero for a stationary wall; the wall's own velocity for a MovingWall, so
  // the velocity the wall function sees is always RELATIVE to the surface.
  cfd::Vector2 wallVelocity{};
};

// Every wall face in mesh order. Throws InvalidArgumentError if no patch's
// velocity condition is Wall/MovingWall -- a wall treatment with no wall is a
// configuration error, not an empty result.
[[nodiscard]] std::vector<WallFace> collectWallFaces(
    const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

// Everything the wall function produces for one wall face.
struct WallFaceState {
  // Wall-parallel velocity magnitude in the owner cell, relative to the wall.
  Real tangentialVelocity{};
  Real wallShearStress{};
  // sqrt(tau_w/rho) -- the REPORTED friction velocity, tied to the shear the
  // solution actually produced.
  Real frictionVelocity{};
  // C_mu^(1/4) sqrt(k_P) -- the EQUILIBRIUM estimate the wall function uses
  // internally. Kept separately because the two agree only in equilibrium and
  // their gap is a useful diagnostic.
  Real equilibriumFrictionVelocity{};
  Real yPlus{};
  WallResolution resolution{WallResolution::LogLayer};
  // The values to impose in / override for the owner cell.
  Real dissipation{};          // epsilon_P
  Real specificDissipation{};  // omega_P, the k-omega counterpart
  Real production{};           // P_k in the wall cell
  Real effectiveViscosity{};   // mu at the wall face, for momentum
  // P16b: which branch dominates at this face under the automatic
  // treatment. REPORTED, never selected -- the blend is continuous and
  // does not switch. Always LogLayer in HighReWallFunction mode.
  WallRegime regime{WallRegime::LogLayer};
  // P16b: how much the LOG branch counts here, in [0, 1]. Exactly 1 in
  // HighReWallFunction mode, which is what makes that path bitwise
  // unchanged -- a model blending on this weight gets the pure equilibrium
  // value back.
  Real logLayerWeight{1.0};
  // True when effectiveViscosity hit its mu_t >= 0 floor -- a buffer-layer
  // symptom, surfaced rather than swallowed.
  bool viscosityFloored{};
};

// Evaluates one wall face from the current velocity and k fields.
//
// Does NOT itself reject an out-of-band y+: `resolution` reports where the cell
// landed and the caller decides. Burying the decision here would make the
// formula untestable outside its validity range, which is exactly where a test
// most needs to probe it.
// How a wall face is treated. Defaults to the validated high-Re path so
// every existing caller and every existing result is unchanged.
struct WallTreatmentOptions {
  WallTreatmentMode mode{WallTreatmentMode::HighReWallFunction};
  // SST beta1, needed only by the automatic mode sublayer omega branch.
  // Carried here rather than read from a model config so this layer stays
  // free of model types.
  Real beta1{0.075};
};

[[nodiscard]] WallFaceState evaluateWallFace(const cfd::mesh::Mesh& mesh, const WallFace& wallFace,
                                             const cfd::fields::VectorField& velocity,
                                             const cfd::fields::ScalarField& turbulentEnergy,
                                             Real density, Real dynamicViscosity,
                                             const WallFunctionCoefficients& coefficients,
                                             const WallTreatmentOptions& options = {});

// The whole wall treatment for one iteration.
struct WallTreatment {
  // Per wall face, in the order collectWallFaces returned them.
  std::vector<WallFaceState> faces;

  // Per FACE (size = mesh.numberOfFaces()), zero everywhere except wall faces:
  // the effective viscosity the momentum assembly should use there. Zero means
  // "no override, use the cell value", so a mesh with no walls produces a field
  // that changes nothing.
  cfd::fields::SurfaceField wallViscosity;

  // Per CELL diagnostics, zero away from walls. y+ is a real output a user can
  // export and plot, not an internal transient -- a wall treatment whose
  // resolution cannot be inspected cannot be trusted.
  cfd::fields::ScalarField yPlus;
  cfd::fields::ScalarField wallShearStress;

  // Wall-adjacent cells and the epsilon/omega to impose there. A cell touching
  // two wall faces (a corner) appears ONCE, carrying the value from its NEAREST
  // wall face -- imposing two values in one cell is not a choice between them,
  // it is a bug.
  std::vector<Index> wallCells;
  std::vector<Real> wallCellDissipation;
  std::vector<Real> wallCellSpecificDissipation;
  std::vector<Real> wallCellProduction;
  // P16b: the per-wall-cell log-layer weight, in the same order as
  // wallCells. All 1.0 in HighReWallFunction mode.
  std::vector<Real> wallCellLogLayerWeight;

  // How many wall faces fell outside the wall function's valid band, and the
  // extremes seen. Reported rather than silently accepted: running a high-Re
  // wall function at y+ = 7 is using it where its own derivation does not hold,
  // which is what produced the 27-33% Re_tau error this phase exists to fix.
  Index facesOutsideValidBand{};
  Index facesWithFlooredViscosity{};

  // P16b: branch-population diagnostics for the automatic treatment.
  //
  // These REPLACE the high-Re mode G1 pass/fail precondition when the
  // automatic treatment is active. A face at y+ = 3 is not a violation
  // there -- it is the sublayer branch doing its job. What matters is
  // that no face is handled by a branch outside that branch intended
  // region, which the blend guarantees because it never switches.
  //
  // facesOutsideValidBand is still counted in BOTH modes so the high-Re
  // evidence stays comparable; in automatic mode it is a DIAGNOSTIC and
  // not a gate.
  Index viscousSublayerFaces{};
  Index blendedFaces{};
  Index logLayerFaces{};
  Real minimumYPlus{};
  Real maximumYPlus{};
};

[[nodiscard]] WallTreatment computeWallTreatment(const cfd::mesh::Mesh& mesh,
                                                 const std::vector<WallFace>& wallFaces,
                                                 const cfd::fields::VectorField& velocity,
                                                 const cfd::fields::ScalarField& turbulentEnergy,
                                                 Real density, Real dynamicViscosity,
                                                 const WallFunctionCoefficients& coefficients,
                                                 const WallTreatmentOptions& options = {});

// A human-readable one-line summary of the resolution a treatment achieved --
// for the structured diagnostic a caller emits when facesOutsideValidBand > 0.
[[nodiscard]] std::string describeWallResolution(const WallTreatment& treatment);

}  // namespace cfd::turbulence
