#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::mesh {

class Mesh;

struct MeshQualityReport {
  Real minimumVolume{};
  Real maximumVolume{};
  Real minimumFaceArea{};
  Real maximumAspectRatio{};

  // P12-NUM-003: aggregated over every INTERNAL face (boundary faces
  // have no owner-neighbor line to measure against -- see
  // MeshGeometry::nonOrthogonalityAngleDegrees/skewness's own header
  // comments). Zero (both max and mean) on a mesh with no internal
  // faces at all (e.g. a 1x1 mesh) -- a vacuous "no data" zero, not a
  // claim of perfect orthogonality. See
  // MeshGeometry::decomposeFaceArea/nonOrthogonalityAngleDegrees/
  // skewness for the exact mathematical definitions -- this struct only
  // aggregates, never redefines, them.
  Real maxNonOrthogonalityDegrees{};
  Real meanNonOrthogonalityDegrees{};
  Real maxSkewness{};
  Real meanSkewness{};

  bool valid{};
};

// Basic sanity/quality checks for a 2D mesh: positive volumes/areas,
// valid owner/neighbor ids, geometric closure, aspect ratio (still a
// structured-Cartesian-only computation -- see cellExtents in the .cpp),
// plus (P12-NUM-003) genuinely mesh-topology-generic non-orthogonality/
// skewness metrics reused directly from MeshGeometry, not redefined
// here.
class MeshQuality {
 public:
  MeshQuality() = delete;

  [[nodiscard]] static MeshQualityReport evaluate(const Mesh& mesh);
};

}  // namespace cfd::mesh
