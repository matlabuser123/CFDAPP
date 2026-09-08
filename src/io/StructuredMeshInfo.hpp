#pragma once

// Internal helper shared by VTKWriter and JSONWriter: infers the
// structured-Cartesian (nx, ny, dx, dy) description of a Mesh built by
// MeshGeometry::createCartesian2D, from its cell centroids alone. Mesh
// itself stores no explicit vertices (P1 -- Result Export section 23
// explicitly anticipates and sanctions this: "you may generate
// deterministic Cartesian VTK points from the structured geometry
// metadata, but document this as an export representation. Do not add
// VTK-only state to the numerical mesh.") -- this file is that
// documented export-only representation, not a numerical-core concept.
//
// Relies on createCartesian2D's own layout (cell id = j*nx + i,
// row-major; cell(i,j) centroid = ((i+0.5)*dx, (j+0.5)*dy), domain
// origin at (0,0)) -- the same convention
// tests/integration/cavity/CavityValidationUtils.cpp and
// tests/integration/poiseuille/PoiseuilleValidationUtils.cpp already
// rely on for profile extraction. Throws InvalidArgumentError if mesh
// does not fit that layout (e.g. cell count not divisible by the
// inferred row length).

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
