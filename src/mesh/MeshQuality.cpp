#include "cfd/mesh/MeshQuality.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::mesh {

namespace {

// For an axis-aligned rectangular cell, infers {dx, dy} from the
// magnitudes of its bounding faces: a face whose area vector is
// x-aligned is a "vertical" face with area == dy; one whose area vector
// is y-aligned is a "horizontal" face with area == dx (see
// MeshGeometry::createCartesian2D's convention).
std::pair<Real, Real> cellExtents(const Mesh& mesh, const Cell& cell) {
  Real dx = 0.0;
  Real dy = 0.0;
  for (const Index faceId : cell.faceIds()) {
    const Face& face = mesh.face(faceId);
    const Vector2& sf = face.areaVector();
    if (std::abs(sf.x) >= std::abs(sf.y)) {
      dy = face.area();
    } else {
      dx = face.area();
    }
  }
  return {dx, dy};
}

}  // namespace

MeshQualityReport MeshQuality::evaluate(const Mesh& mesh) {
  MeshQualityReport report;
  report.minimumVolume = std::numeric_limits<Real>::max();
  report.maximumVolume = std::numeric_limits<Real>::lowest();
  report.minimumFaceArea = std::numeric_limits<Real>::max();
  report.maximumAspectRatio = 0.0;
  report.valid = true;

  for (const Cell& cell : mesh.cells()) {
    if (!std::isfinite(cell.volume()) || !(cell.volume() > 0.0)) {
      report.valid = false;
    }
    report.minimumVolume = std::min(report.minimumVolume, cell.volume());
    report.maximumVolume = std::max(report.maximumVolume, cell.volume());

    if (cell.faceIds().empty()) {
      report.valid = false;
      continue;
    }

    const auto [dx, dy] = cellExtents(mesh, cell);
    if (dx > 0.0 && dy > 0.0) {
      const Real aspectRatio = std::max(dx, dy) / std::min(dx, dy);
      report.maximumAspectRatio = std::max(report.maximumAspectRatio, aspectRatio);
    } else {
      report.valid = false;
    }
  }

  for (const Face& face : mesh.faces()) {
    if (!std::isfinite(face.area()) || !(face.area() > 0.0)) {
      report.valid = false;
    }
    report.minimumFaceArea = std::min(report.minimumFaceArea, face.area());

    if (face.owner() >= mesh.numberOfCells()) {
      report.valid = false;
    }
    if (face.neighbor().has_value()) {
      if (*face.neighbor() >= mesh.numberOfCells()) {
        report.valid = false;
      }
      if (*face.neighbor() == face.owner()) {
        report.valid = false;
      }
    }
  }

  return report;
}

}  // namespace cfd::mesh
