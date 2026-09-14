#include "cfd/discretization/VectorGradient.hpp"

#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

namespace {

VelocityGradientField greenGaussSum(const Mesh& mesh, const std::vector<Vector2>& faceVelocity) {
  VectorField gradU(mesh.numberOfCells(), Vector2{0.0, 0.0});
  VectorField gradV(mesh.numberOfCells(), Vector2{0.0, 0.0});

  for (const auto& cell : mesh.cells()) {
    Vector2 sumU{0.0, 0.0};
    Vector2 sumV{0.0, 0.0};
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Vector2 sfCell =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      sumU += sfCell * faceVelocity[faceId].x;
      sumV += sfCell * faceVelocity[faceId].y;
    }
    gradU[cell.id()] = sumU * (1.0 / cell.volume());
    gradV[cell.id()] = sumV * (1.0 / cell.volume());
  }

  return VelocityGradientField{std::move(gradU), std::move(gradV)};
}

// Plain Green-Gauss (the pre-P12-NUM-003 formula), plus -- P12-NUM-003 --
// the same skewness-corrected re-sweeps as Gradient.hpp's
// greenGaussGradient (same kGreenGaussSkewCorrectionSweeps, same "only
// faces with a nonzero skew vector" rule, so bit-identical on orthogonal
// meshes), each velocity component transported with its own gradient.
VelocityGradientField greenGaussVelocityGradient(const Mesh& mesh, const VectorField& velocity,
                                                 const BoundaryConditionSet& velocityBoundaries) {
  std::vector<Vector2> faceVelocity(mesh.numberOfFaces());
  std::vector<Index> skewedFaces;
  for (const auto& face : mesh.faces()) {
    faceVelocity[face.id()] = interpolateFace(mesh, face, velocity, velocityBoundaries);
    if (face.isBoundary()) {
      continue;
    }
    const auto crossing = cfd::mesh::MeshGeometry::ownerNeighborCrossing(mesh, face);
    if (crossing.has_value() && (crossing->skewVector.x != 0.0 || crossing->skewVector.y != 0.0)) {
      skewedFaces.push_back(face.id());
    }
  }
  VelocityGradientField result = greenGaussSum(mesh, faceVelocity);
  for (Index sweep = 0; sweep < kGreenGaussSkewCorrectionSweeps && !skewedFaces.empty(); ++sweep) {
    for (const Index faceId : skewedFaces) {
      faceVelocity[faceId] = interpolateInternalFaceSkewCorrected(mesh, mesh.face(faceId), velocity,
                                                                  result.gradU, result.gradV);
    }
    result = greenGaussSum(mesh, faceVelocity);
  }
  return result;
}

// P12-NUM-003: the vector-BC counterpart of Gradient.cpp's
// leastSquaresGradient -- IDENTICAL stencil (one (displacement, value
// difference) pair per face: the neighbor centroid for an internal face,
// the boundary face's own centroid and its boundary-condition value for a
// boundary face -- here the full VectorBoundaryCondition value, via the
// same interpolateFace() the GreenGauss path uses), the SAME
// solveLeastSquaresGradient() primitive (no second least-squares solver),
// and the SAME fallback policy (a cell whose local system is not well-
// conditioned takes its GreenGauss value). The u and v solves share one
// displacement set, so they are well-/ill-conditioned together.
VelocityGradientField leastSquaresVelocityGradient(const Mesh& mesh, const VectorField& velocity,
                                                   const BoundaryConditionSet& velocityBoundaries) {
  const VelocityGradientField greenGaussFallback =
      greenGaussVelocityGradient(mesh, velocity, velocityBoundaries);

  VectorField gradU(mesh.numberOfCells(), Vector2{0.0, 0.0});
  VectorField gradV(mesh.numberOfCells(), Vector2{0.0, 0.0});
  for (const auto& cell : mesh.cells()) {
    std::vector<Vector2> displacements;
    std::vector<Real> uDifferences;
    std::vector<Real> vDifferences;
    displacements.reserve(cell.faceIds().size());
    uDifferences.reserve(cell.faceIds().size());
    vDifferences.reserve(cell.faceIds().size());

    const Vector2& uP = velocity[cell.id()];
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      Vector2 position{0.0, 0.0};
      Vector2 value{0.0, 0.0};
      if (!face.isBoundary()) {
        const Index neighborId = (face.owner() == cell.id()) ? *face.neighbor() : face.owner();
        position = mesh.cell(neighborId).centroid();
        value = velocity[neighborId];
      } else {
        position = face.centroid();
        value = interpolateFace(mesh, face, velocity, velocityBoundaries);
      }
      displacements.push_back(position - cell.centroid());
      uDifferences.push_back(value.x - uP.x);
      vDifferences.push_back(value.y - uP.y);
    }

    const LeastSquaresGradientResult uResult =
        solveLeastSquaresGradient(displacements, uDifferences);
    const LeastSquaresGradientResult vResult =
        solveLeastSquaresGradient(displacements, vDifferences);
    gradU[cell.id()] =
        uResult.wellConditioned ? uResult.gradient : greenGaussFallback.gradU[cell.id()];
    gradV[cell.id()] =
        vResult.wellConditioned ? vResult.gradient : greenGaussFallback.gradV[cell.id()];
  }
  return VelocityGradientField{std::move(gradU), std::move(gradV)};
}

}  // namespace

VelocityGradientField computeVelocityGradient(const Mesh& mesh, const VectorField& velocity,
                                              const BoundaryConditionSet& velocityBoundaries,
                                              GradientScheme scheme) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "computeVelocityGradient: velocity size does not match mesh cell "
        "count");
  }
  if (scheme == GradientScheme::LeastSquares) {
    return leastSquaresVelocityGradient(mesh, velocity, velocityBoundaries);
  }
  // GreenGauss -- the pre-P12-NUM-003 formula plus skewness-corrected face
  // values (bit-identical on orthogonal meshes).
  return greenGaussVelocityGradient(mesh, velocity, velocityBoundaries);
}

}  // namespace cfd::discretization
