// GPU-DISC-001H -- host-side construction of the face-flux plan.
//
// The only thing this adds beyond the 001D convection plan is the per-face
// geometry the Rhie-Chow coupling needs, decided with the CPU's own EXACT
// predicates so the device never branches on a float:
//
//   isAxisAligned(sf)      exactly one of sf.x, sf.y, sf.z is non-zero
//   exactlyParallel(d, sf) cross(d, sf) == Vector3{}
//
// Both are restated here because PressureCorrectionEquation.cpp keeps them in an
// anonymous namespace. They are two lines each, transcribed exactly, and the
// differential covers meshes on both sides of each test.

#include <string>
#include <vector>

#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::Vector3;
using cfd::mesh::Face;
using cfd::mesh::Mesh;

namespace {

// PressureCorrectionEquation.cpp:60
bool isAxisAligned(const Vector2& sf) noexcept {
  const int nonZero = (sf.x != 0.0 ? 1 : 0) + (sf.y != 0.0 ? 1 : 0) + (sf.z != 0.0 ? 1 : 0);
  return nonZero == 1;
}
// PressureCorrectionEquation.cpp:65
bool exactlyParallel(const Vector2& a, const Vector2& b) noexcept {
  return cross(a, b) == Vector3{};
}

}  // namespace

bool DeviceFaceFluxPlan::build(const Mesh& mesh,
                               const cfd::boundary::BoundaryConditionSet& velocityBoundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  axisAlignedFaces_ = generalFaces_ = 0;

  if (!convection_.build(mesh, velocityBoundaries)) {
    unsupportedReason_ = "convection plan unusable: " + convection_.unsupportedReason();
    return false;
  }
  threeDimensional_ = mesh.dimension() == 3;
  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  faceCount_ = nf;

  std::vector<Real> dx(nf, 0.0), dy(nf, 0.0), dz(nf, 0.0), distance(nf, 0.0);
  std::vector<Index> aligned(nf, 0), axis(nf, 0), zeroZ(nf, 0);

  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    const Vector2& sf = face.areaVector();
    zeroZ[f] = (sf.z == 0.0) ? 1 : 0;
    if (face.isBoundary()) continue;
    const Vector2 d =
        mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
    dx[f] = d.x;
    dy[f] = d.y;
    dz[f] = d.z;
    distance[f] = magnitude(d);
    if (isAxisAligned(sf) && exactlyParallel(d, sf)) {
      aligned[f] = 1;
      axis[f] = (sf.x != 0.0) ? 0 : ((sf.y != 0.0) ? 1 : 2);
      ++axisAlignedFaces_;
    } else {
      ++generalFaces_;
    }
  }

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(dX_, dx); put(dY_, dy); put(dZ_, dz); put(distance_, distance);
  put(axisAligned_, aligned); put(axisIndex_, axis); put(areaZIsZero_, zeroZ);

  usable_ = true;
  return true;
}

std::size_t DeviceFaceFluxPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return convection_.residentBytes() + bytes(dX_) + bytes(dY_) + bytes(dZ_) + bytes(distance_) +
         bytes(axisAligned_) + bytes(axisIndex_) + bytes(areaZIsZero_);
}

}  // namespace cfd::gpu
