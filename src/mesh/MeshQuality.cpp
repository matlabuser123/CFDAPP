#include "cfd/mesh/MeshQuality.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

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

  // P12-NUM-003: accumulated over internal faces only -- see this
  // struct's own header comment.
  Real sumNonOrthogonality = 0.0;
  Real sumSkewness = 0.0;
  Index internalFaceCount = 0;

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

    // Non-orthogonality/skewness are undefined for a boundary face (no
    // neighbor) -- and, independently of any OTHER cell/face's own
    // defects (which must not suppress THIS face's own otherwise-valid
    // metric), an out-of-range owner/neighbor id on THIS face would make
    // mesh.cell(...) itself throw, so skip only in that specific case.
    if (face.isBoundary()) {
      continue;
    }
    if (face.owner() >= mesh.numberOfCells() || *face.neighbor() >= mesh.numberOfCells()) {
      continue;  // already recorded as report.valid = false above.
    }
    // decomposeFaceArea's own `valid` flag is the single source of
    // truth for "is this face's geometry well-posed enough to measure"
    // -- nonOrthogonalityAngleDegrees/skewness share the exact same
    // underlying degeneracy condition (see MeshGeometry.cpp's
    // isFaceGeometryWellPosed), so checking it once here avoids ever
    // reaching either function's own throw/nullopt path for a face
    // already known to be degenerate -- detected and reported via
    // `report.valid = false`, never a silent NaN/Inf or an uncaught
    // exception out of this evaluate() call.
    const auto decomposition = MeshGeometry::decomposeFaceArea(mesh, face);
    if (!decomposition.valid) {
      report.valid = false;
      continue;
    }

    ++internalFaceCount;
    const Real angle = MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face);
    report.maxNonOrthogonalityDegrees = std::max(report.maxNonOrthogonalityDegrees, angle);
    sumNonOrthogonality += angle;

    const auto skew = MeshGeometry::skewness(mesh, face);
    if (skew.has_value()) {
      report.maxSkewness = std::max(report.maxSkewness, *skew);
      sumSkewness += *skew;
    }
  }

  if (internalFaceCount > 0) {
    report.meanNonOrthogonalityDegrees = sumNonOrthogonality / static_cast<Real>(internalFaceCount);
    report.meanSkewness = sumSkewness / static_cast<Real>(internalFaceCount);
  }

  return report;
}

}  // namespace cfd::mesh
