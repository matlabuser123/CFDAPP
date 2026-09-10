#include "cfd/viz/DerivedFields.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::viz {

using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

FieldStatistics computeFieldStatistics(const ScalarField& field) {
  FieldStatistics stats;
  Real sum = 0.0;
  Index count = 0;
  for (Index i = 0; i < field.size(); ++i) {
    const Real value = field[i];
    if (!std::isfinite(value)) continue;
    if (!stats.hasData) {
      stats.minimum = value;
      stats.maximum = value;
      stats.hasData = true;
    } else {
      stats.minimum = std::min(stats.minimum, value);
      stats.maximum = std::max(stats.maximum, value);
    }
    sum += value;
    ++count;
  }
  if (count > 0) stats.average = sum / static_cast<Real>(count);
  return stats;
}

ScalarField velocityMagnitude(const VectorField& velocity) {
  ScalarField magnitude(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    magnitude[i] = cfd::magnitude(velocity[i]);
  }
  return magnitude;
}

ScalarField vorticity2D(const Mesh& mesh, const VectorField& velocity) {
  if (static_cast<std::size_t>(velocity.size()) != mesh.numberOfCells()) {
    throw InvalidArgumentError("vorticity2D: velocity size does not match mesh cell count");
  }

  const std::size_t n = mesh.numberOfCells();
  std::vector<Vector2> gradU(n, Vector2{});
  std::vector<Vector2> gradV(n, Vector2{});

  for (const auto& face : mesh.faces()) {
    Vector2 faceVelocity;
    if (face.isBoundary()) {
      // Zero-gradient extrapolation -- see this header's own comment on
      // why a generic diagnostic cannot know the real boundary type.
      faceVelocity = velocity[face.owner()];
    } else {
      faceVelocity = 0.5 * (velocity[face.owner()] + velocity[*face.neighbor()]);
    }

    gradU[face.owner()] = gradU[face.owner()] + (faceVelocity.x * face.areaVector());
    gradV[face.owner()] = gradV[face.owner()] + (faceVelocity.y * face.areaVector());
    if (!face.isBoundary()) {
      const Index neighborId = *face.neighbor();
      gradU[neighborId] = gradU[neighborId] - (faceVelocity.x * face.areaVector());
      gradV[neighborId] = gradV[neighborId] - (faceVelocity.y * face.areaVector());
    }
  }

  ScalarField vorticity(velocity.size());
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Vector2 du = (1.0 / cell.volume()) * gradU[id];
    const Vector2 dv = (1.0 / cell.volume()) * gradV[id];
    vorticity[id] = dv.x - du.y;  // omega_z = dv/dx - du/dy.
  }
  return vorticity;
}

}  // namespace cfd::viz
