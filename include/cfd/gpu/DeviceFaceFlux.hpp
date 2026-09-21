#pragma once

// GPU-DISC-001H -- CUDA-only.
//
// CUDA port of the production predicted face-mass-flux path:
//
//   Linear     physics::calculateMassFlux
//   RhieChow   pressure_velocity::rhieChowMassFlux
//                = calculateMassFlux + rhieChowFaceCorrection
//
// Both are production: resolveFaceFluxScheme maps Automatic to Linear in 2D and
// RhieChow in 3D, and a case file can force either. The 2D Linear default is the
// recorded "2D Linear face-flux pressure mode" technical debt -- REPRODUCED
// here, not fixed.
//
// Pressure correction is NOT part of this phase.
//
// See results/gpu-disc-001/rhie-chow/audit.md.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

struct FaceFluxPlanView;

// Per-face immutable data the flux needs beyond what the convection plan holds.
// The two geometric predicates the CPU evaluates -- isAxisAligned(sf) and
// exactlyParallel(d, sf) -- are EXACT tests, so they are decided once here and
// stored as a flag plus an axis index; the device never branches on a float.
class DeviceFaceFluxPlan {
 public:
  bool build(const cfd::mesh::Mesh& mesh,
             const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMomentumConvectionPlan& convectionPlan() const noexcept {
    return convection_;
  }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return convection_.mesh(); }
  // Internal faces taking the axis-aligned coupling branch, and the general one.
  [[nodiscard]] cfd::Index axisAlignedFaces() const noexcept { return axisAlignedFaces_; }
  [[nodiscard]] cfd::Index generalFaces() const noexcept { return generalFaces_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  friend void fillFaceFluxPlanView(const DeviceFaceFluxPlan&, FaceFluxPlanView&);
  friend void calculateMassFluxDevice(const DeviceFaceFluxPlan&, const DeviceVelocity&, cfd::Real,
                                      DeviceBuffer<cfd::Real>&);
  friend void rhieChowFaceCorrectionDevice(
      const DeviceFaceFluxPlan&, const DeviceBuffer<cfd::Real>&, const DeviceBuffer<cfd::Real>&,
      const DeviceBuffer<cfd::Real>&, const DeviceBuffer<cfd::Real>&,
      const DeviceBuffer<cfd::Real>&, const DeviceBuffer<cfd::Real>&,
      const DeviceBuffer<cfd::Real>*, cfd::Real, cfd::Real, DeviceBuffer<cfd::Real>&);

  DeviceMomentumConvectionPlan convection_;
  bool usable_{false};
  bool threeDimensional_{false};
  std::string unsupportedReason_;
  cfd::Index faceCount_{0};
  cfd::Index axisAlignedFaces_{0};
  cfd::Index generalFaces_{0};

  // d = centroid(neighbour) - centroid(owner), and |d|.
  DeviceBuffer<cfd::Real> dX_, dY_, dZ_, distance_;
  // 1 when isAxisAligned(sf) && exactlyParallel(d, sf); the axis (0/1/2) the
  // branch selects; and whether sf.z is exactly zero (the 2D response-vector
  // form).
  DeviceBuffer<cfd::Index> axisAligned_, axisIndex_, areaZIsZero_;
};

// physics::calculateMassFlux. Owner-oriented, one canonical value per face.
void calculateMassFluxDevice(const DeviceFaceFluxPlan& plan, const DeviceVelocity& velocity,
                             cfd::Real density, DeviceBuffer<cfd::Real>& massFlux);

// pressure_velocity::rhieChowFaceCorrection. Boundary faces stay exactly 0.0.
// `pressureGradX/Y/Z` is the cell pressure gradient; `dU`, `dV`, `dW` the
// momentum response coefficients (dW null on a 2D mesh).
void rhieChowFaceCorrectionDevice(
    const DeviceFaceFluxPlan& plan, const DeviceBuffer<cfd::Real>& pressure,
    const DeviceBuffer<cfd::Real>& pressureGradX, const DeviceBuffer<cfd::Real>& pressureGradY,
    const DeviceBuffer<cfd::Real>& pressureGradZ, const DeviceBuffer<cfd::Real>& dU,
    const DeviceBuffer<cfd::Real>& dV, const DeviceBuffer<cfd::Real>* dW, cfd::Real density,
    cfd::Real alpha, DeviceBuffer<cfd::Real>& correction);

// pressure_velocity::rhieChowMassFlux = calculateMassFlux + the correction.
void rhieChowMassFluxDevice(const DeviceFaceFluxPlan& plan, const DeviceVelocity& velocity,
                            const DeviceBuffer<cfd::Real>& pressure,
                            const DeviceBuffer<cfd::Real>& pressureGradX,
                            const DeviceBuffer<cfd::Real>& pressureGradY,
                            const DeviceBuffer<cfd::Real>& pressureGradZ,
                            const DeviceBuffer<cfd::Real>& dU, const DeviceBuffer<cfd::Real>& dV,
                            const DeviceBuffer<cfd::Real>* dW, cfd::Real density, cfd::Real alpha,
                            DeviceBuffer<cfd::Real>& massFlux);

}  // namespace cfd::gpu
