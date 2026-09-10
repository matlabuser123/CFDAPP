#include "cfd/viz/FieldProbe.hpp"

#include <limits>

#include "cfd/core/Exception.hpp"

namespace cfd::viz {

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

std::optional<Index> nearestIndex(const std::vector<Vector2>& points, const Vector2& point) {
  if (points.empty()) return std::nullopt;

  Index best = 0;
  Real bestDistanceSquared = std::numeric_limits<Real>::infinity();
  for (std::size_t i = 0; i < points.size(); ++i) {
    const Vector2 delta = points[i] - point;
    const Real distanceSquared = dot(delta, delta);
    if (distanceSquared < bestDistanceSquared) {
      bestDistanceSquared = distanceSquared;
      best = static_cast<Index>(i);
    }
  }
  return best;
}

std::optional<Index> nearestCell(const Mesh& mesh, const Vector2& point) {
  if (mesh.numberOfCells() == 0) return std::nullopt;

  Index best = 0;
  Real bestDistanceSquared = std::numeric_limits<Real>::infinity();
  for (const auto& cell : mesh.cells()) {
    const Vector2 delta = cell.centroid() - point;
    const Real distanceSquared = dot(delta, delta);
    if (distanceSquared < bestDistanceSquared) {
      bestDistanceSquared = distanceSquared;
      best = cell.id();
    }
  }
  return best;
}

Real probeScalar(const Mesh& mesh, const ScalarField& field, const Vector2& point) {
  if (static_cast<std::size_t>(field.size()) != mesh.numberOfCells()) {
    throw InvalidArgumentError("probeScalar: field size does not match mesh cell count");
  }
  const std::optional<Index> cellId = nearestCell(mesh, point);
  if (!cellId.has_value()) {
    throw InvalidArgumentError("probeScalar: mesh has no cells");
  }
  return field[*cellId];
}

std::vector<LineSample> sampleLine(const Mesh& mesh, const ScalarField& field, const Vector2& start,
                                   const Vector2& end, Index numberOfSamples) {
  if (numberOfSamples < 2) {
    throw InvalidArgumentError("sampleLine: numberOfSamples must be >= 2");
  }
  if (static_cast<std::size_t>(field.size()) != mesh.numberOfCells()) {
    throw InvalidArgumentError("sampleLine: field size does not match mesh cell count");
  }

  std::vector<LineSample> samples;
  samples.reserve(static_cast<std::size_t>(numberOfSamples));
  for (Index k = 0; k < numberOfSamples; ++k) {
    const Real t = static_cast<Real>(k) / static_cast<Real>(numberOfSamples - 1);
    const Vector2 queryPoint = start + (t * (end - start));
    const std::optional<Index> cellId = nearestCell(mesh, queryPoint);
    if (!cellId.has_value()) {
      throw InvalidArgumentError("sampleLine: mesh has no cells");
    }
    samples.push_back(LineSample{queryPoint, mesh.cell(*cellId).centroid(), field[*cellId]});
  }
  return samples;
}

Real probeScalarRaw(const std::vector<Vector2>& points, const std::vector<Real>& values,
                    const Vector2& point) {
  if (points.size() != values.size()) {
    throw InvalidArgumentError("probeScalarRaw: points/values size mismatch");
  }
  const std::optional<Index> index = nearestIndex(points, point);
  if (!index.has_value()) {
    throw InvalidArgumentError("probeScalarRaw: no points given");
  }
  return values[static_cast<std::size_t>(*index)];
}

std::vector<LineSample> sampleLineRaw(const std::vector<Vector2>& points,
                                      const std::vector<Real>& values, const Vector2& start,
                                      const Vector2& end, Index numberOfSamples) {
  if (numberOfSamples < 2) {
    throw InvalidArgumentError("sampleLineRaw: numberOfSamples must be >= 2");
  }
  if (points.size() != values.size()) {
    throw InvalidArgumentError("sampleLineRaw: points/values size mismatch");
  }

  std::vector<LineSample> samples;
  samples.reserve(static_cast<std::size_t>(numberOfSamples));
  for (Index k = 0; k < numberOfSamples; ++k) {
    const Real t = static_cast<Real>(k) / static_cast<Real>(numberOfSamples - 1);
    const Vector2 queryPoint = start + (t * (end - start));
    const std::optional<Index> index = nearestIndex(points, queryPoint);
    if (!index.has_value()) {
      throw InvalidArgumentError("sampleLineRaw: no points given");
    }
    samples.push_back(
        LineSample{queryPoint, points[static_cast<std::size_t>(*index)], values[static_cast<std::size_t>(*index)]});
  }
  return samples;
}

}  // namespace cfd::viz
