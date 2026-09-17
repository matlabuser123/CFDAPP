#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MultiBlockSpec.hpp"

namespace cfd::mesh {

// Geometric computations derived from mesh data, plus the structured
// mesh generators. Does not store a duplicate mesh -- every method is a
// pure function of the Mesh/Face/Vector2 arguments passed in. The
// geometric functions are dimension-independent (P12-MESH-005): they use
// all three components of the (Vector3) points and vectors, so they serve
// the 2D meshes (z = 0) and the 3D hexahedral mesh alike.
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

  // P12-GRAD-002 -- where the line through a boundary cell's centroid along a
  // given direction meets that cell's boundary face, and how far along the
  // direction that point lies. The boundary-consistent Green-Gauss face value
  // (Gradient.cpp) fits a quadratic along the owner-neighbor direction and needs
  // its third point ON the boundary, not at the boundary face centroid: the
  // centroid only lies on that line when the mesh happens to be orthogonal
  // there, and requiring it to was the defect P12-GRAD-001 failed to fix with a
  // tolerance (results/p12-grad-002/formulation.md sections 2 and 5).
  //
  //   t     = ((x_P - x_f) . n) / (direction . n)      (> 0 looking inward)
  //   point = x_P - t * direction
  //
  // `direction` points from the owner centroid INTO the mesh and must be a UNIT
  // vector, so `distance` is a length in the mesh's own units.
  //
  // `valid` is false -- never a computed NaN/Inf -- only when `direction . n` is
  // not safely bounded away from zero, i.e. the line runs parallel to the
  // boundary face, or when the resulting t is not positive: degenerate cells
  // that MeshQuality rejects before any solve. On a valid mesh |direction . n| >=
  // cos(max boundary non-orthogonality), so this is a smooth, bounded function
  // of the coordinates with no threshold selecting a formula.
  struct BoundaryLineIntersection {
    Real distance{0.0};
    Vector2 point{0.0, 0.0};
    bool valid{false};
  };
  [[nodiscard]] static BoundaryLineIntersection boundaryLineIntersection(const Mesh& mesh,
                                                                         const Face& boundaryFace,
                                                                         const Vector2& direction);

  // P12-DIFF-002 -- the inward stencil a one-sided boundary reconstruction needs: the boundary
  // face, its owner P, and the far cell F across the owner's opposite interior face, expressed in
  // the wall-NORMAL coordinate that such a reconstruction is built in.
  //
  //   h1 = (x_f - x_P) . n      normal distance from the face to the owner centroid
  //   h2 = (x_f - x_F) . n      normal distance from the face to the far centroid   (h2 > h1 > 0)
  //   deltaP = x_P - (x_f - h1 n)   purely tangential offset of P from the normal ray
  //   deltaF = x_F - (x_f - h2 n)   likewise for F
  //
  // The offsets are what makes the reconstruction valid off-orthogonal: a normal-direction stencil
  // may only use values that sit ON the ray, so a caller transfers each cell value with its own
  // gradient, phi~ = phi - grad(phi) . delta. On an exactly orthogonal face both offsets are
  // exactly {0,0} and the transfer term vanishes identically, so an orthogonal mesh is unaffected.
  //
  // `valid` is false -- never a computed NaN/Inf -- when the cell has no interior face across the
  // boundary face (a one-cell-thick or 1x1 domain: a purely TOPOLOGICAL condition that cannot flip
  // under round-off), or when h1 > 0 and h2 > h1 do not both hold (a degenerate cell whose far
  // centroid is not further from the wall than its own; MeshQuality rejects such meshes before any
  // solve). Callers fall back to their two-point treatment in that case.
  struct BoundaryInwardStencil {
    Real h1{0.0};
    Real h2{0.0};
    Vector2 deltaP{0.0, 0.0};
    Vector2 deltaF{0.0, 0.0};
    Index farCell{0};
    bool valid{false};
  };
  [[nodiscard]] static BoundaryInwardStencil boundaryInwardStencil(const Mesh& mesh,
                                                                   const Face& boundaryFace);

  // Generates a structured, orthogonal, cell-centered 2D Cartesian mesh
  // over [0, lengthX] x [0, lengthY] with nx * ny cells.
  //
  // Indexing: cell(i, j) = j * nx + i, 0 <= i < nx, 0 <= j < ny.
  // Area vectors: Sf points owner -> neighbor for internal faces, and
  // outward from the domain for boundary faces (see docs/architecture).
  // Boundary patches: "left", "right", "bottom", "top".
  // The mesh also carries its vertex grid (Mesh::structuredGrid(),
  // vertex(i, j) = (i * dx, j * dy)); cells and faces are unchanged by it.
  [[nodiscard]] static Mesh createCartesian2D(Index nx, Index ny, Real lengthX, Real lengthY);

  // P12-MESH-002: the structured, orthogonal 2D mesh over [0, x.node(nx)] x
  // [0, y.node(ny)] with column widths x.width(i) and row heights
  // y.width(j) -- the tensor-product generalization of createCartesian2D,
  // which it now implements (same topology, face order, patches and vertex
  // grid; createCartesian2D(nx, ny, Lx, Ly) == createRectilinear2D(
  // AxisSpacing::uniform(nx, Lx), AxisSpacing::uniform(ny, Ly)) bit for
  // bit). Cells are axis-aligned rectangles: centroid (x.center(i),
  // y.center(j)), volume x.width(i) * y.width(j); every face is exactly
  // orthogonal and unskewed.
  [[nodiscard]] static Mesh createRectilinear2D(const AxisSpacing& x, const AxisSpacing& y);

  // P12-MESH-002: createRectilinear2D with each axis graded by
  // AxisSpacing::graded (MeshGrading.hpp) -- a Uniform axis (or ratio
  // exactly 1) is the Cartesian spacing bit for bit. Throws
  // InvalidArgumentError for an invalid grading (see gradedNodeCoordinates).
  [[nodiscard]] static Mesh createGraded2D(Index nx, Index ny, Real lengthX, Real lengthY,
                                           const AxisGrading& xGrading,
                                           const AxisGrading& yGrading);

  // P12-MESH-001: a structured 2D quadrilateral mesh with the SAME topology
  // as createCartesian2D (cell(i, j) = j * nx + i, the same face order and
  // owner/neighbor pairs, patches "left" (i = 0), "right" (i = nx),
  // "bottom" (j = 0), "top" (j = ny)) but arbitrary vertex positions --
  // `vertices` is the row-major (nx + 1) x (ny + 1) grid of
  // StructuredGrid.hpp. Geometry is exact polygon geometry of those
  // vertices: cell centroid/volume by the shoelace formulas on the four
  // corners, face centroid = edge midpoint, face area vector = edge
  // rotated by 90 degrees, pointing owner -> neighbor (internal) or out of
  // the domain (boundary). For vertices on a uniform rectangular lattice
  // this reproduces the Cartesian geometry up to round-off.
  //
  // Validity (InvalidArgumentError naming the offending vertex / cell):
  // nx, ny > 0; exactly (nx + 1) * (ny + 1) vertices; every coordinate
  // finite; every cell a strictly convex quadrilateral with
  // counter-clockwise corners (each of its four corner turns strictly
  // positive) -- which rules out folded, self-intersecting,
  // clockwise-ordered, zero-area and degenerate-edge cells.
  [[nodiscard]] static Mesh createStructuredQuad2D(Index nx, Index ny,
                                                   const std::vector<Vector2>& vertices);

  // P12-MESH-003: a conformal multi-block structured mesh (MultiBlockSpec.hpp)
  // -- genuinely non-rectangular 2D domains (bends, steps, channels with
  // obstacles as holes between blocks). Cells: block by block, local
  // cell(i, j) = offset + j * nx + i; faces: per block, vertical then
  // horizontal (the single-block order), each interface face created once
  // (owner in the interface's `first` block, area vector toward the `second`);
  // patches: the spec's named patches (whole block sides, in the order
  // listed). Cell and face geometry are those of createStructuredQuad2D.
  //
  // Throws InvalidArgumentError (naming the block / side / cell / vertex)
  // for: no blocks; empty or duplicate block or patch names; nx or ny 0; a
  // wrong vertex count; a non-finite coordinate; a cell that is not a
  // strictly convex counter-clockwise quad (folded, crossed edges,
  // inverted/clockwise block, zero area, collapsed vertices); a block side
  // used by no interface/patch or by more than one; a reference to a
  // non-existent block; an interface whose sides have different face counts
  // or non-identical vertices (in the declared orientation); two boundary
  // faces on the same edge (an undeclared interface, a duplicated block);
  // and any cell whose centroid does not lie inside the domain exactly once
  // (winding number != 1 against the oriented boundary -- overlapping or
  // self-overlapping blocks). Disconnected regions are rejected by the
  // MeshQuality gate (connected components).
  [[nodiscard]] static Mesh createMultiBlock2D(const MultiBlockSpec& spec);

  // P12-MESH-005: a structured, orthogonal, cell-centred 3D Cartesian
  // hexahedral mesh over [x0, x0 + lengthX] x [y0, y0 + lengthY] x
  // [z0, z0 + lengthZ] (origin = (x0, y0, z0)) with nx * ny * nz uniform
  // cells of size dx = lengthX / nx, dy = lengthY / ny, dz = lengthZ / nz --
  // the same face-based Mesh as every 2D builder (no separate 3D mesh type).
  //
  // Cells: cell(i, j, k) = (k * ny + j) * nx + i; centroid = the box centre,
  // volume dx * dy * dz.
  // Faces, each created once: first the x-faces (k outer, then j, then
  // i = 0..nx), then the y-faces (k, j = 0..ny, i), then the z-faces
  // (k = 0..nz, j, i). An internal face's owner is the lower-index cell
  // (cell(i-1, j, k) for x-face i) and its area vector points owner ->
  // neighbour (+x, +y, +z); a boundary face's points out of the domain.
  // Each cell lists its six faces in the canonical order west, east,
  // south, north, bottom, top (outward normals -x, +x, -y, +y, -z, +z).
  // Boundary patches, in this order: "xmin", "xmax", "ymin", "ymax",
  // "zmin", "zmax". The mesh carries its vertex grid (StructuredGrid with
  // nz > 0; vertex(i, j, k) = origin + (i dx, j dy, k dz)) for export.
  //
  // Coordinates use the same arithmetic as createCartesian2D on each axis
  // (AxisSpacing::uniform), offset by the origin. Throws
  // InvalidArgumentError if nx, ny or nz is 0, a length is not finite and
  // positive, or an origin coordinate is not finite.
  [[nodiscard]] static Mesh createCartesian3D(Index nx, Index ny, Index nz, Real lengthX,
                                              Real lengthY, Real lengthZ,
                                              const Vector3& origin = Vector3{});

  // --- P12-MESH-007: topology-preserving geometry update --------------------
  // (results/p12-mesh-007/architecture.md section 3.3.)
  //
  // The vertex map of a mesh built from structured block grids: which
  // vertices are the corners of each cell and the ends/corners of each face.
  // Built once from the mesh's own cells, faces and grids (the only topology
  // a moving mesh needs that Mesh does not already hold); never duplicates
  // the cell/face connectivity itself.
  struct StructuredTopology {
    int dimension{2};
    // Welded global vertices: block vertices with equal coordinates are one
    // vertex (multi-block interfaces), numbered in first-appearance order
    // (block by block, each block's own vertex order). Their coordinates at
    // the time the map was built.
    std::vector<Vector3> vertices;
    // blockVertexIds[b][v]: the global vertex of block b's local vertex v.
    std::vector<std::vector<Index>> blockVertexIds;
    // The blocks' names (StructuredGrid::name; empty for single-grid meshes).
    std::vector<std::string> blockNames;
    // Cell corners, counter-clockwise in 2D ((i,j), (i+1,j), (i+1,j+1),
    // (i,j+1); corner[4..7] unused) and, in 3D, those four at k then at k+1.
    // block/i/j/k locate the cell for messages.
    struct CellCorners {
      std::array<Index, 8> corner{};
      Index block{};
      Index i{};
      Index j{};
      Index k{};
    };
    std::vector<CellCorners> cells;
    // 2D: vertex[0], vertex[1] are the (a, b) arguments of the builder's
    // edge-face formula, `vertical` selects the vertical (edge along j) or
    // horizontal (edge along i) formula and `negate` its outward flag -- the
    // same call the builder made, so unchanged vertices give the builder's
    // face geometry bit for bit. 3D: vertex[0..3] = p0..p3 of a bilinear face,
    // ordered so that 1/2 (p2 - p0) x (p3 - p1) points along the stored area
    // vector (out of the owner cell).
    struct FaceVertices {
      std::array<Index, 4> vertex{};
      bool vertical{false};
      bool negate{false};
    };
    std::vector<FaceVertices> faces;
  };

  // Builds the vertex map of `mesh` and verifies it: the geometry computed
  // from the map (computeGeometry) must reproduce every stored cell and face
  // to 1e-9 of the cell / face size. Throws InvalidArgumentError if the mesh
  // has no structured grid, or its geometry is not the geometry of its grid.
  [[nodiscard]] static StructuredTopology structuredTopology(const Mesh& mesh);

  // The full geometry (MeshGeometryState) of the vertex positions `vertices`
  // (one per welded vertex of `topology`):
  //   2D -- exactly the builders' formulas (createStructuredQuad2D /
  //         createMultiBlock2D: shoelace cell area and centroid; face
  //         centroid = edge midpoint, area vector = the edge rotated,
  //         outward);
  //   3D -- the trilinear hexahedron: volume = integral of det J and centroid
  //         = integral of x det J / volume (2 x 2 x 2 Gauss, exact: degree
  //         <= 3 per variable); face area vector 1/2 (p2 - p0) x (p3 - p1) (the
  //         exact vector area of the bilinear face); face centroid = the
  //         projected-area-weighted centroid of the four triangles about the
  //         vertex average (the exact area centroid of a planar face).
  // Throws InvalidArgumentError naming the cell -- block, (i, j[, k]) -- and
  // the defect if a cell is not valid: 2D not a strictly convex
  // counter-clockwise quadrilateral (degenerate edge, zero area, inverted,
  // not convex); 3D a corner Jacobian det <= 0 (the corner is named).
  [[nodiscard]] static MeshGeometryState computeGeometry(const StructuredTopology& topology,
                                                         const std::vector<Vector3>& vertices);

  // The signed volume each face sweeps when every vertex moves linearly in
  // time from `from` to `to` (one entry per face, positive when the face
  // moves along its area vector). Exact for that path:
  //   2D -- S_f(midpoint configuration) . (dp + dq) / 2 (the swept area is a
  //         bilinear integral, equal to its centre value);
  //   3D -- the integral over the face and the step of xdot . (x_s x x_r),
  //         2 x 2 x 2 Gauss in (s, r, t) (degree <= 2 per variable).
  // With the cell geometry of computeGeometry this gives the discrete
  // geometric conservation law V^{n+1} - V^n = sum_f s_Pf dV_f exactly up to
  // round-off (s_Pf = +1 for the owner, -1 for the neighbor).
  [[nodiscard]] static std::vector<Real> sweptVolumes(const StructuredTopology& topology,
                                                      const std::vector<Vector3>& from,
                                                      const std::vector<Vector3>& to);
};

}  // namespace cfd::mesh
