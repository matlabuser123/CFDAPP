#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// Finite-volume divergence of (diffusivity * grad(phi)). For constant
// diffusivity, div(Gamma*grad(phi)) = Gamma * laplacian(phi) (see
// Laplacian.hpp). Internal faces contribute
// Gamma*Af*(phiN-phiP)/dPN evaluated once, added to the owner and
// subtracted from the neighbor -- so internal contributions cancel
// exactly across the whole mesh (face-once conservation, see
// PROJECT_STRUCTURE.md).
//
// P12-NUM-003: `applyNonOrthogonalCorrection` (default false, exactly
// today's only behavior for every pre-P12-NUM-003 call site, which
// never passes it) selects the non-orthogonal-corrected flux:
//   internal face:  Gamma * [|S_orth|/dPN * (phiN-phiP) + S_nonorth . grad(phi)_f]
// (over-relaxed decomposition -- see MeshGeometry::decomposeFaceArea's own
// header comment for the exact formula and the exact-zero-on-orthogonal-
// faces guarantee), with grad(phi)_f the cell gradient reconstructed via
// `gradientScheme` (P12-NUM-002's own gradient(), reused -- never a
// second gradient implementation here) and distance-weighted to the face;
//   Dirichlet-type boundary face (FixedValue/FixedTemperature/WallOmega):
//     (|S_orth,b|/|Sf|) * [pre-existing boundary flux] + Gamma * S_nonorth,b . grad(phi)_P
// with the split taken against d = x_face - x_owner
// (MeshGeometry::decomposeBoundaryFaceArea) and the owner gradient;
//   Neumann-type boundary face (FixedGradient/HeatFlux/Adiabatic): never
// corrected -- its flux is prescribed, and the task forbids corrupting it.
// The boundary-face correction is NOT optional polish: without it a
// boundary-adjacent cell is only half-corrected, and the resulting O(1)
// imbalance does not converge (measured -- see results/p12-num-003/
// summary.md). On an orthogonal (e.g. Cartesian) mesh every decomposition
// is exactly {Sf, 0}, so the corrected operator is BIT-IDENTICAL to the
// uncorrected one. A face whose geometry is too degenerate for the
// decomposition (`valid = false`) falls back to the uncorrected formula
// for that one face -- deterministic, never NaN/Inf. Default gradient
// scheme: GreenGauss (every other operator's default; skewness-corrected
// since P12-NUM-003, so the corrected operator is linear-exact to ~1e-11 on
// the verification meshes); LeastSquares makes it exact to round-off --
// both measured in the evidence.
[[nodiscard]] cfd::fields::ScalarField diffusion(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field, Real diffusivity,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    bool applyNonOrthogonalCorrection = false,
    GradientScheme gradientScheme = GradientScheme::GreenGauss);

}  // namespace cfd::discretization
