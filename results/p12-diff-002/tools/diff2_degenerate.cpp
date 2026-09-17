// P12-DIFF-002 W3b: the degenerate meshes named in the frozen gate (2D 1x1, 8x1, 1x8; 3D 1x1x1,
// 8x8x1). For every boundary face this reports whether the production reconstruction took the
// higher-order path or the fallback, and whether the returned terms are BITWISE identical to the
// pre-DIFF-002 two-point expression.
//
// The pre-change expression is reproduced here literally -- it is still present in the production
// source as the fallback branch of boundaryFaceDiffusionTerms, so the comparison below is against
// the actual pre-change code, transcribed once:
//     decomposeBoundaryFaceArea valid : coefficient = gamma |S_orth| / d,
//                                       explicitFlux = gamma S_nonorth . grad_P
//     otherwise                       : coefficient = gamma |S| / d, explicitFlux = 0
//     boundaryValueCoefficient = coefficient, farCellCoefficient = 0
// Comparison is on the raw bit patterns, not a tolerance.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using cfd::discretization::boundaryFaceDiffusionTerms;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kGamma = 0.37;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// The pre-DIFF-002 two-point terms, transcribed from the production fallback branch.
discretization::FaceDiffusionTerms preChangeTerms(const Mesh& mesh, const Face& face, Real distance,
                                                  const VectorField& grad) {
  discretization::FaceDiffusionTerms terms;
  const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
  if (decomposition.valid) {
    terms.coefficient = kGamma * magnitude(decomposition.orthogonal) / distance;
    terms.explicitFlux = kGamma * dot(decomposition.nonOrthogonal, grad[face.owner()]);
  } else {
    terms.coefficient = kGamma * face.area() / distance;
    terms.explicitFlux = 0.0;
  }
  terms.boundaryValueCoefficient = terms.coefficient;
  terms.farCellCoefficient = 0.0;
  return terms;
}

void report(const std::string& name, const Mesh& mesh) {
  // A deliberately non-trivial transfer gradient: if any face took the higher-order path its
  // explicitFlux would differ from the pre-change value even where the geometry is orthogonal.
  VectorField grad(mesh.numberOfCells(), Vector3{});
  for (const auto& cell : mesh.cells()) {
    grad[cell.id()] = Vector3{0.8 + cell.centroid().x, -0.3 * cell.centroid().y, 0.11};
  }

  std::size_t boundaryFaces = 0;
  std::size_t higherOrder = 0;
  std::size_t fallback = 0;
  std::size_t bitwiseIdentical = 0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) continue;
    ++boundaryFaces;
    const Real distance =
        MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
    const auto terms = boundaryFaceDiffusionTerms(mesh, face, kGamma, distance, &grad, true);
    const auto before = preChangeTerms(mesh, face, distance, grad);
    if (terms.higherOrder) {
      ++higherOrder;
    } else {
      ++fallback;
    }
    if (sameBits(terms.coefficient, before.coefficient) &&
        sameBits(terms.explicitFlux, before.explicitFlux) &&
        sameBits(terms.boundaryValueCoefficient, before.boundaryValueCoefficient) &&
        sameBits(terms.farCellCoefficient, before.farCellCoefficient)) {
      ++bitwiseIdentical;
    }
  }
  std::printf(
      "W3b %-14s cells %4zu  boundary faces %4zu  higher-order %4zu  fallback %4zu  "
      "bitwise-identical %4zu/%zu  %s\n",
      name.c_str(), static_cast<std::size_t>(mesh.numberOfCells()), boundaryFaces, higherOrder,
      fallback, bitwiseIdentical, boundaryFaces,
      (fallback == boundaryFaces && bitwiseIdentical == boundaryFaces) ? "PASS" : "FAIL");

  // Which patches obtained the higher-order stencil: this separates "the criterion was mis-derived"
  // from "the code is wrong". A patch normal to a direction the mesh has >= 2 cells across has a
  // genuine opposite interior cell, so the reconstruction is available there by construction.
  for (const auto& patch : mesh.boundaryPatches()) {
    std::size_t patchHigher = 0;
    for (const Index faceId : patch.faceIds()) {
      const Face& face = mesh.face(faceId);
      const Real distance =
          MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
      if (boundaryFaceDiffusionTerms(mesh, face, kGamma, distance, &grad, true).higherOrder) {
        ++patchHigher;
      }
    }
    std::printf("       patch %-8s faces %4zu  higher-order %4zu\n", patch.name().c_str(),
                patch.faceIds().size(), patchHigher);
  }
}

}  // namespace

int main() {
  std::printf(
      "# W3b as frozen: 100%% fallback on every face AND bitwise identity to the "
      "pre-change library.\n");
  std::printf("# gamma %.6g; comparison is on raw bit patterns.\n\n", kGamma);
  report("2D 1x1", MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0));
  report("2D 8x1", MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0));
  report("2D 1x8", MeshGeometry::createCartesian2D(1, 8, 1.0, 8.0));
  report("3D 1x1x1", MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0));
  report("3D 8x8x1", MeshGeometry::createCartesian3D(8, 8, 1, 8.0, 8.0, 1.0));
  return 0;
}
