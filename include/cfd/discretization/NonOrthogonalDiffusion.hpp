#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// P12-NUM-003 -- the ONE implementation of the non-orthogonal correction
// for every IMPLICIT (matrix-assembling) diffusion term in the code:
// momentum viscous terms (physics::assembleDiffusionContribution), thermal
// conduction (thermal::assembleThermalDiffusionContribution), species
// diffusion (species::assembleSpeciesDiffusionContribution) and the k /
// epsilon / omega diffusion of every turbulence model
// (turbulence::assembleScalarDiffusionContribution). Each of those keeps
// its own diffusivity and boundary-VALUE evaluation (they legitimately
// differ); only the face geometry below is shared, so the correction
// formula itself exists exactly once. (cfd::discretization::diffusion, an
// explicit operator returning div(Gamma grad phi) rather than assembling a
// matrix, applies the same decomposition in flux form -- Diffusion.cpp.)
//
// Formulation (over-relaxed, MeshGeometry::decomposeAreaVector):
//   flux into owner = Gamma_f [ |S_orth|/|d| (phi_N - phi_P) + S_nonorth . grad(phi)_f ]
// The first term is implicit; the second is explicit (lagged gradient). In
// a matrix whose owner row represents -(flux into owner), the implicit part
// becomes `coefficient` on the diagonal/off-diagonal and the known part
// moves to the RHS as +explicitFlux (owner row) / -explicitFlux (neighbor
// row) -- the sign derivation is in MomentumEquation.hpp's header comment.

// Settings every implicit diffusion assembler takes. Disabled (the
// default) is exactly the pre-P12-NUM-003 two-point operator.
struct NonOrthogonalCorrectionOptions {
  bool enabled{false};
  // Gradient used for the explicit S_nonorth . grad(phi) term. See
  // results/p12-num-003/summary.md: LeastSquares is exact for linear fields
  // on any mesh; GreenGauss (skew-corrected, P12-NUM-003) is close but not
  // exact on a skewed mesh.
  GradientScheme gradientScheme{GradientScheme::GreenGauss};
};

// True for boundary conditions that prescribe the boundary VALUE (FixedValue,
// FixedTemperature, WallOmega; velocity: Wall, MovingWall, Inlet): their
// boundary flux is computed from that value, so its non-orthogonal part is
// corrected like an internal face's. False for conditions that prescribe the
// normal gradient / flux (FixedGradient, HeatFlux, Adiabatic, Outlet,
// Symmetry): correcting those would corrupt a prescribed flux.
[[nodiscard]] bool prescribesBoundaryValue(cfd::boundary::BoundaryConditionType type) noexcept;

struct FaceDiffusionTerms {
  // Implicit coefficient Gamma_f * basis / distance, basis = |S_orth| when
  // corrected, |Sf| otherwise (the pre-existing two-point coefficient,
  // evaluated in the same operand order so an uncorrected call is
  // bit-identical to the pre-P12-NUM-003 expression).
  Real coefficient{};
  // Known (explicit) part of the flux INTO THE OWNER: Gamma_f * S_nonorth .
  // grad(phi)_f; exactly 0 when uncorrected.
  Real explicitFlux{};
};

// Internal face. `gradPhi` null -> uncorrected. Otherwise the face gradient
// is the distance-weighted interpolation of the owner/neighbor cell
// gradients; a degenerate decomposition falls back to uncorrected for this
// face (never NaN/Inf). On an orthogonal face the corrected result equals
// the uncorrected one bit-for-bit (decomposition exactly {Sf, 0}).
[[nodiscard]] FaceDiffusionTerms internalFaceDiffusionTerms(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face, Real gammaFace, Real distance,
    const cfd::fields::VectorField* gradPhi);

// Boundary face, split against d = x_face - x_owner, explicit part from the
// OWNER cell gradient. Corrected only when `gradPhi` is non-null AND
// `prescribedValue` (see prescribesBoundaryValue); otherwise exactly the
// uncorrected coefficient and zero explicit flux.
[[nodiscard]] FaceDiffusionTerms boundaryFaceDiffusionTerms(const cfd::mesh::Mesh& mesh,
                                                            const cfd::mesh::Face& face,
                                                            Real gammaFace, Real distance,
                                                            const cfd::fields::VectorField* gradPhi,
                                                            bool prescribedValue);

}  // namespace cfd::discretization
