#pragma once

// GPU-DISC-001J -- CUDA-only.
//
// CUDA port of cfd::pressure_velocity::correctVelocity
// (PressureCorrectionEquation.cpp:404): the cell-centred SIMPLE/PISO velocity
// correction.
//
//   boundaries   = makeGradientBoundaries(mesh, pressureBoundaries)
//                      FixedValue(0.0)    where the PRESSURE patch is FixedValue
//                      FixedGradient(0.0) on every other patch
//   gradPPrime   = gradient(mesh, p', boundaries, scheme)
//   u = u* - (dU * dp'/dx),  v = v* - (dV * dp'/dy),  w = w* - (dW * dp'/dz)
//
// Two contracts are easy to get wrong and are called out in the header because
// getting either wrong is invisible on most inputs:
//
// 1. THE 2D W CONTRACT. With no W response the CPU returns `Vector2{x, y}`,
//    which is `Vector3` with z value-initialised to exactly +0.0. The
//    predictor's z is DISCARDED, not preserved and not left alone. A port that
//    copies it agrees on every case whose predictor already has z == 0.
//
// 2. The branch is on the W-response POINTER, not on mesh.dimension(). A 2D
//    mesh handed a non-null W response legally takes the 3D branch --
//    requireWResponse only rejects null-on-3D and a size mismatch.
//
// NOT in the equation, verified by audit: no density, no cell volume, no
// under-relaxation, no clipping, no cross terms, and no velocity boundary
// condition re-applied afterwards by any caller.
//
// See results/gpu-disc-001/velocity-correction/audit.md.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceLeastSquaresGradient.hpp"
#include "cfd/gpu/DeviceMesh.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// Mirrors cfd::discretization::GradientScheme. Plain constants so the value can
// cross into device code without a cast, as elsewhere in this layer.
inline constexpr cfd::Index kGradientSchemeGreenGauss = 0;
inline constexpr cfd::Index kGradientSchemeLeastSquares = 1;

class DeviceVelocityCorrectionPlan {
 public:
  // `pressureBoundaries` is the PRESSURE condition set, exactly what
  // correctVelocity is handed; makeGradientBoundaries is applied here, so the
  // caller never has to build the p' set itself and cannot get it wrong.
  bool build(const cfd::mesh::Mesh& mesh,
             const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return leastSquares_.mesh(); }
  [[nodiscard]] const DeviceGradientPlan& greenGauss() const noexcept {
    return leastSquares_.greenGauss();
  }
  [[nodiscard]] const DeviceLeastSquaresGradientPlan& leastSquares() const noexcept {
    return leastSquares_;
  }
  [[nodiscard]] cfd::Index cellCount() const noexcept { return cellCount_; }
  // How many patches makeGradientBoundaries turned into FixedValue(0.0).
  // Exposed so a test can assert the p' boundary set actually differs from a
  // uniformly-Neumann one rather than assuming it.
  [[nodiscard]] cfd::Index fixedValuePressurePatches() const noexcept { return fixedValuePatches_; }
  [[nodiscard]] bool threeDimensionalMesh() const noexcept { return threeDimensional_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  DeviceLeastSquaresGradientPlan leastSquares_;  // embeds the Green-Gauss plan

  bool usable_{false};
  bool threeDimensional_{false};
  std::string unsupportedReason_;
  cfd::Index cellCount_{0};
  cfd::Index fixedValuePatches_{0};
};

// p' -> grad(p') -> corrected velocity, all device-resident.
//
// `gradX/gradY/gradZ` receive the pressure-correction gradient (resized as
// needed). They are a real output, not scratch: the face-flux correction will
// want the same values, and the differential compares them independently of the
// corrected velocity so a sign error localises to one link.
//
// `dW` null selects the 2D branch, exactly as the CPU's null wResponse does --
// including forcing corrected.z to exactly +0.0. It is the CALLER's
// responsibility to pass it on a 3D mesh, mirroring requireWResponse, which
// this function also enforces.
void correctVelocityDevice(const DeviceVelocityCorrectionPlan& plan,
                           const DeviceVelocity& predictorVelocity,
                           const DeviceBuffer<cfd::Real>& dU, const DeviceBuffer<cfd::Real>& dV,
                           const DeviceBuffer<cfd::Real>* dW,
                           const DeviceBuffer<cfd::Real>& pressureCorrection, cfd::Index scheme,
                           DeviceBuffer<cfd::Real>& gradX, DeviceBuffer<cfd::Real>& gradY,
                           DeviceBuffer<cfd::Real>& gradZ, DeviceVelocity& corrected);

}  // namespace cfd::gpu
