#pragma once

// Internal helper shared by VTKWriter and JSONWriter: the structured
// (nx, ny, dx, dy) description of a mesh for export. Originally inferred
// from cell centroids alone, when a Mesh stored no vertices (P1 -- Result
// Export section 23: "you may generate deterministic Cartesian VTK points
// from the structured geometry metadata, but document this as an export
// representation. Do not add VTK-only state to the numerical mesh.").
// P12-MESH-001 gave every production mesh its vertex grid
// (cfd::mesh::StructuredGrid) -- not VTK-only state but the mesh's own
// defining geometry, which a non-orthogonal quad mesh cannot be exported
// without (its vertices are not recoverable from centroids); the numerical
// code never reads it.
//
// P12-MESH-001: a mesh that carries its generating StructuredGrid
// (Mesh::structuredGrid(), set by createCartesian2D and
// createStructuredQuad2D -- every production mesh) reports that grid's
// nx/ny directly; dx/dy are then the first cell's edge lengths along i and
// j (exactly the Cartesian spacing for a Cartesian mesh). Only a mesh
// without a grid is inferred from centroids, relying on createCartesian2D's
// layout (cell id = j*nx + i, row-major; cell(i,j) centroid =
// ((i+0.5)*dx, (j+0.5)*dy), domain origin at (0,0)) -- and every cell
// centroid is checked against that layout (relative tolerance 1e-9), so a
// non-Cartesian mesh throws InvalidArgumentError instead of yielding a
// wrong nx/ny.

#include "cfd/core/Types.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::io::detail {

struct StructuredMeshInfo {
  Index nx;
  Index ny;
  Real dx;
  Real dy;
};

[[nodiscard]] StructuredMeshInfo inferStructuredMeshInfo(const cfd::mesh::Mesh& mesh);

}  // namespace cfd::io::detail
