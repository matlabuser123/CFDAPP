#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::mesh {

class Mesh;

struct MeshQualityReport {
  Real minimumVolume{};
  Real maximumVolume{};
  Real minimumFaceArea{};
  Real maximumAspectRatio{};
  bool valid{};
};

// Basic sanity/quality checks for an orthogonal Cartesian mesh: positive
// volumes/areas, valid owner/neighbor ids, geometric closure, aspect
// ratio. Deliberately simple -- no skewness/non-orthogonality metrics
// until arbitrary (non-Cartesian) meshes are introduced.
class MeshQuality {
 public:
  MeshQuality() = delete;

  [[nodiscard]] static MeshQualityReport evaluate(const Mesh& mesh);
};

}  // namespace cfd::mesh
