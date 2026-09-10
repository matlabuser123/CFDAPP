#include "cfd/viz/VectorSampling.hpp"

#include "cfd/core/Exception.hpp"

namespace cfd::viz {

using cfd::fields::VectorField;
using cfd::mesh::Mesh;

std::vector<VectorSample> sampleVectorField(const Mesh& mesh, const VectorField& velocity,
                                            Index stride) {
  if (stride < 1) {
    throw InvalidArgumentError("sampleVectorField: stride must be >= 1");
  }
  if (static_cast<std::size_t>(velocity.size()) != mesh.numberOfCells()) {
    throw InvalidArgumentError("sampleVectorField: velocity size does not match mesh cell count");
  }

  std::vector<VectorSample> samples;
  for (const auto& cell : mesh.cells()) {
    if (cell.id() % stride != 0) continue;
    samples.push_back(VectorSample{cell.centroid(), velocity[cell.id()]});
  }
  return samples;
}

std::vector<VectorSample> sampleVectorFieldRaw(const std::vector<Vector2>& points,
                                               const std::vector<Real>& velocityX,
                                               const std::vector<Real>& velocityY, Index stride) {
  if (stride < 1) {
    throw InvalidArgumentError("sampleVectorFieldRaw: stride must be >= 1");
  }
  if (points.size() != velocityX.size() || points.size() != velocityY.size()) {
    throw InvalidArgumentError("sampleVectorFieldRaw: points/velocityX/velocityY size mismatch");
  }

  std::vector<VectorSample> samples;
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (i % static_cast<std::size_t>(stride) != 0) continue;
    samples.push_back(VectorSample{points[i], Vector2{velocityX[i], velocityY[i]}});
  }
  return samples;
}

}  // namespace cfd::viz
