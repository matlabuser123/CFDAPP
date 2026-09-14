#pragma once

#include <optional>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::mesh {

// Geometric computations derived from mesh data, plus the structured
// Cartesian mesh generator. Does not store a duplicate mesh -- every
// method is a pure function of the Mesh/Face/Vector2 arguments passed in.
class MeshGeometry {
 public:
  MeshGeometry() = delete;

  [[nodiscard]] static Real distance(const Vector2& a, const Vector2& b) noexcept;
  [[nodiscard]] static Vector2 displacement(const Vector2& from, const Vector2& to) noexcept;

  [[nodiscard]] static Vector2 unitNormal(const Face& face);
  [[nodiscard]] static Real ownerNeighborDistance(const Mesh& mesh, const Face& face);

  // Among `cell`'s other faces, finds the internal face whose outward-
  // from-`cell` unit normal is most anti-parallel to `referenceFace`'s
  // own outward-from-`cell` normal -- i.e. the face "across" the cell
  // from `referenceFace`, found by topology/geometry rather than any
  // structured-mesh indexing. Returns std::nullopt if the cell has no
  // OTHER internal face at all (e.g. every side of `cell` is a boundary,
  // as in a 1x1 mesh, or `cell` is boundary-adjacent in every direction
  // but the one `referenceFace` itself occupies).
  //
  // `referenceFace` may be a boundary face of `cell` (the original use:
  // Gradient.cpp's boundary-exact quadratic-fit reconstruction, second-
  // order one-sided derivative from three points -- boundary, owner,
  // opposite neighbor) OR an internal face with `cell` as EITHER its
  // owner or neighbor (P12-NUM-001: locating a face's second-upstream
  // cell for QUICK -- see Convection.cpp). Both cases correctly resolve
  // `referenceFace`'s own outward-from-`cell` orientation before
  // comparing (a boundary face's stored area vector already points
  // outward from its only cell, the owner, by construction -- see
  // Mesh.hpp's Sf convention -- so this generalization is a no-op for
  // every pre-existing boundary-face caller).
  [[nodiscard]] static std::optional<Index> oppositeInteriorFace(const Mesh& mesh, const Cell& cell,
                                                                 const Face& referenceFace);

  // P12-NUM-003 -- non-orthogonal decomposition of an internal face's
  // area vector Sf (over-relaxed approach, Jasak 1996 / Moukalled et al.
  // "The Finite Volume Method in CFD"): with d = neighbor.centroid -
  // owner.centroid,
  //   S_orth    = (Sf . Sf) / (d . Sf) * d
  //   S_nonorth = Sf - S_orth
  // Chosen (over the "minimum correction" or "orthogonal correction"
  // alternatives) because it keeps the IMPLICIT two-point coefficient
  // |S_orth|/|d| >= the plain orthogonal one as non-orthogonality grows
  // (never *weaker* diagonal dominance than today's uncorrected
  // formula), the standard reason production codes (e.g. OpenFOAM)
  // default to it. On a perfectly orthogonal face (Sf parallel to d),
  // S_orth == Sf exactly and S_nonorth == {0,0} exactly -- confirmed by
  // direct algebraic substitution (d.Sf = |d||Sf| when parallel, so
  // S_orth = (|Sf|^2)/(|d||Sf|) * d = (|Sf|/|d|)*d, magnitude |Sf|,
  // same direction as d hence as Sf), not merely close to zero.
  //
  // `valid` is false (both vectors left as the harmless {0,0}
  // placeholder, never NaN/Inf) whenever `d . Sf` is not safely bounded
  // away from zero relative to |d||Sf| (i.e. the angle between the face
  // and the owner-neighbor line is at or past 90 degrees) -- a
  // genuinely degenerate/pathological mesh configuration where this
  // decomposition's own denominator breaks down, never silently
  // computed into a corrupted or sign-flipped result.
  struct NonOrthogonalDecomposition {
    Vector2 orthogonal{0.0, 0.0};
    Vector2 nonOrthogonal{0.0, 0.0};
    // Defaults to false so a value-initialized/default-constructed
    // instance (e.g. decomposeFaceArea's own `return {};` on the
    // degenerate path) is never mistaken for a successfully computed
    // decomposition -- `valid` is only ever true via the explicit
    // {orthogonal, nonOrthogonal, true} construction on the success path.
    bool valid = false;
  };
  [[nodiscard]] static NonOrthogonalDecomposition decomposeFaceArea(const Mesh& mesh,
                                                                    const Face& face);

  // The pure-geometry core of decomposeFaceArea: the same over-relaxed
  // split of `sf` relative to an arbitrary direction `d` (same exact-
  // parallel short-circuit, same well-posedness guard). decomposeFaceArea
  // and decomposeBoundaryFaceArea are both thin wrappers over this -- one
  // authoritative formula.
  [[nodiscard]] static NonOrthogonalDecomposition decomposeAreaVector(const Vector2& d,
                                                                      const Vector2& sf) noexcept;

  // Boundary-face counterpart of decomposeFaceArea, with d = x_face -
  // x_owner (the owner-to-boundary-face vector the boundary flux's own
  // two-point derivative is taken along). Used ONLY for boundary faces
  // carrying a Dirichlet-type condition (see Diffusion.hpp/
  // MomentumEquation.hpp): there the boundary VALUE is prescribed and the
  // flux must be computed, so its non-orthogonal part needs the same
  // correction as an internal face; a Neumann-type face's flux is itself
  // prescribed and is never corrected. Exactly {Sf, {0,0}} on every
  // boundary face of createCartesian2D (d exactly parallel to Sf).
  // Throws InvalidArgumentError for an internal face.
  [[nodiscard]] static NonOrthogonalDecomposition decomposeBoundaryFaceArea(const Mesh& mesh,
                                                                            const Face& face);

  // Non-orthogonality angle (degrees, in [0, 180]) between the face area
  // vector Sf and d = neighbor.centroid - owner.centroid -- 0 for a
  // perfectly orthogonal face. Throws InvalidArgumentError for a
  // boundary face (no neighbor, hence no d) -- same convention as
  // ownerNeighborDistance.
  [[nodiscard]] static Real nonOrthogonalityAngleDegrees(const Mesh& mesh, const Face& face);

  // Skewness: the normalized distance between the face's own centroid
  // and the point where the line through the owner and neighbor
  // centroids crosses the face's own (line, in 2D) plane -- 0 when the
  // face centroid lies exactly on that line (true for every non-
  // degenerate Cartesian face), a DISTINCT geometric effect from
  // non-orthogonality (a face can be perfectly orthogonal yet skewed,
  // or non-orthogonal yet unskewed -- see results/p12-num-003/
  // summary.md for a worked example). Normalized by |d| (the owner-
  // neighbor distance), a defensible, documented, dimensionless choice.
  // std::nullopt (never a computed NaN/Inf) when the owner-neighbor line
  // is parallel to the face itself (the same `d . Sf` degeneracy
  // decomposeFaceArea guards against -- the crossing point is undefined
  // there). Throws InvalidArgumentError for a boundary face.
  [[nodiscard]] static std::optional<Real> skewness(const Mesh& mesh, const Face& face);

  // P12-NUM-003 -- the geometry skewness correction needs: where the
  // owner-neighbor line x(t) = x_P + t*d crosses the face's own line (the
  // point f' at which linear interpolation between x_P and x_N is exact
  // for a linear field), and the skew vector x_f - x_f' from that point
  // to the true face centroid (zero on an unskewed face). skewness()
  // above is |skewVector| / |d|. `t` is the interpolation weight of the
  // NEIGHBOR value at f' (phi_f' = phi_P + t*(phi_N - phi_P)); it equals
  // 0.5 on a uniform Cartesian mesh. std::nullopt under the same `d . Sf`
  // degeneracy as decomposeFaceArea (no well-defined crossing). Throws
  // InvalidArgumentError for a boundary face.
  struct FaceCrossing {
    Real t{};
    Vector2 crossingPoint{0.0, 0.0};
    Vector2 skewVector{0.0, 0.0};
  };
  [[nodiscard]] static std::optional<FaceCrossing> ownerNeighborCrossing(const Mesh& mesh,
                                                                         const Face& face);

  // Generates a structured, orthogonal, cell-centered 2D Cartesian mesh
  // over [0, lengthX] x [0, lengthY] with nx * ny cells.
  //
  // Indexing: cell(i, j) = j * nx + i, 0 <= i < nx, 0 <= j < ny.
  // Area vectors: Sf points owner -> neighbor for internal faces, and
  // outward from the domain for boundary faces (see docs/architecture).
  // Boundary patches: "left", "right", "bottom", "top".
  [[nodiscard]] static Mesh createCartesian2D(Index nx, Index ny, Real lengthX, Real lengthY);
};

}  // namespace cfd::mesh
