#pragma once

// GPU-DISC-001E -- CUDA-only. The one reusable device boundary-condition layer.
//
// 001B, 001C and 001D each needed a piece of boundary-condition support and each
// built what it needed. This consolidates them into a single container plus a
// single set of __device__ evaluators, and adds the two things none of them
// exposed:
//
//   * the boundary condition's TYPE, which the pressure-correction path
//     dispatches on (PressureCorrectionEquation.cpp:252 skips any face whose
//     type is not FixedValue);
//   * prescribesBoundaryValue as a stored per-face flag rather than a predicate
//     each operator re-derives.
//
// It reuses BoundaryEncoding.hpp and VectorBoundaryEncoding.hpp rather than
// restating them: those encodings are already bitwise-verified, and a second
// copy is exactly what would drift.
//
// See results/gpu-disc-001/boundary-conditions/audit.md.

#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceMesh.hpp"
#include "cfd/gpu/VectorBoundaryEncoding.hpp"

namespace cfd::mesh {
class Mesh;
}

namespace cfd::gpu {

// Mirrors cfd::boundary::BoundaryConditionType, same order, so a host caller can
// cast between them and a kernel can compare without one.
inline constexpr cfd::Index kBcFixedValue = 0;
inline constexpr cfd::Index kBcFixedGradient = 1;
inline constexpr cfd::Index kBcWall = 2;
inline constexpr cfd::Index kBcMovingWall = 3;
inline constexpr cfd::Index kBcInlet = 4;
inline constexpr cfd::Index kBcOutlet = 5;
inline constexpr cfd::Index kBcSymmetry = 6;
inline constexpr cfd::Index kBcFixedTemperature = 7;
inline constexpr cfd::Index kBcHeatFlux = 8;
inline constexpr cfd::Index kBcAdiabatic = 9;
inline constexpr cfd::Index kBcWallOmega = 10;

// The raw pointers a kernel needs. Every array is indexed by FACE ID and is
// meaningful only on a boundary face; interior entries hold neutral values.
struct DeviceBoundaryView {
  const cfd::Index* type;             // one of the kBc* constants
  const cfd::Index* prescribesValue;  // 1 when the condition prescribes the value
  // Scalar evaluation at the straight-line owner-to-face distance.
  const cfd::Index* scalarKind;
  const cfd::Real* scalarA;
  const cfd::Real* scalarB;
  // Scalar evaluation at a second, caller-chosen per-face distance (the
  // gradient's oblique normal distance). Equal to the primary encoding on any
  // face where the caller supplied no alternative distance.
  const cfd::Index* altKind;
  const cfd::Real* altA;
  const cfd::Real* altB;
  // Vector evaluation.
  const cfd::Index* vectorKind;
  const cfd::Real* constX;
  const cfd::Real* constY;
  const cfd::Real* constZ;
  const cfd::Real* normalX;
  const cfd::Real* normalY;
  const cfd::Real* normalZ;
  const cfd::Real* distance;  // straight-line owner-to-face distance
};

// --- device evaluators ----------------------------------------------------
// Deliberately header-inline so every operator compiles the SAME code. Each
// reproduces its CPU counterpart's arithmetic, not merely its value.

#if defined(__CUDACC__)

__device__ inline cfd::Real bcEvaluateScalar(const DeviceBoundaryView& bc, cfd::Index face,
                                             cfd::Real ownerValue) {
  const cfd::Index kind = bc.scalarKind[face];
  if (kind == kBoundaryEncodingConstant) return bc.scalarA[face];
  if (kind == kBoundaryEncodingShift) return ownerValue + bc.scalarA[face];
  return bc.scalarA[face] + (bc.scalarB[face] * ownerValue);
}

// The same, at the alternative distance (oblique-Neumann faces).
__device__ inline cfd::Real bcEvaluateScalarAlt(const DeviceBoundaryView& bc, cfd::Index face,
                                                cfd::Real ownerValue) {
  const cfd::Index kind = bc.altKind[face];
  if (kind == kBoundaryEncodingConstant) return bc.altA[face];
  if (kind == kBoundaryEncodingShift) return ownerValue + bc.altA[face];
  return bc.altA[face] + (bc.altB[face] * ownerValue);
}

__device__ inline void bcEvaluateVector(const DeviceBoundaryView& bc, cfd::Index face, cfd::Real ux,
                                        cfd::Real uy, cfd::Real uz, cfd::Real& outX,
                                        cfd::Real& outY, cfd::Real& outZ) {
  const cfd::Index kind = bc.vectorKind[face];
  if (kind == kVectorBoundaryConstant) {
    outX = bc.constX[face];
    outY = bc.constY[face];
    outZ = bc.constZ[face];
    return;
  }
  if (kind == kVectorBoundaryIdentity) {
    outX = ux;
    outY = uy;
    outZ = uz;
    return;
  }
  const cfd::Real nx = bc.normalX[face];
  const cfd::Real ny = bc.normalY[face];
  const cfd::Real nz = bc.normalZ[face];
  // cfd::dot's z guard: the z term is dropped when exactly zero.
  const cfd::Real inPlane = (ux * nx) + (uy * ny);
  const cfd::Real zTerm = uz * nz;
  const cfd::Real normalComponent = zTerm == 0.0 ? inPlane : inPlane + zTerm;
  outX = ux - (nx * normalComponent);
  outY = uy - (ny * normalComponent);
  outZ = uz - (nz * normalComponent);
}

// upwindBoundaryFaceValue (Convection.cpp:57): outflow carries the interior
// value; inflow uses the ghost mirrored through the boundary value.
__device__ inline cfd::Real bcGhostValue(const DeviceBoundaryView& bc, cfd::Index face,
                                         cfd::Real ownerValue, cfd::Real faceFlux) {
  if (faceFlux >= 0.0) return ownerValue;
  const cfd::Real phiB = bcEvaluateScalar(bc, face, ownerValue);
  return (2.0 * phiB) - ownerValue;
}

__device__ inline bool bcPrescribesValue(const DeviceBoundaryView& bc, cfd::Index face) {
  return bc.prescribesValue[face] != 0;
}

__device__ inline cfd::Index bcType(const DeviceBoundaryView& bc, cfd::Index face) {
  return bc.type[face];
}

#endif  // __CUDACC__

// Per-(mesh, boundary set) device boundary data.
//
// A set may be scalar-valued, vector-valued, or (for a mesh whose patches carry
// a mix) both; `build` records which interfaces are available and rejects any
// condition it cannot reproduce bitwise rather than approximating it.
class DeviceBoundaryConditions {
 public:
  // `alternativeDistance`, when non-empty, must have one entry per FACE: a
  // positive value selects a second scalar encoding at that distance for that
  // face (the gradient's oblique normal distance); a non-positive value means
  // "same as the primary". Pass an empty vector when no operator needs it.
  bool build(const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& conditions,
             const std::vector<cfd::Real>& alternativeDistance = {});

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] bool hasScalar() const noexcept { return hasScalar_; }
  [[nodiscard]] bool hasVector() const noexcept { return hasVector_; }
  // Boundary faces reaching each encoded form, so a differential can show the
  // branches were exercised rather than assumed.
  [[nodiscard]] cfd::Index scalarConstantFaces() const noexcept { return scalarConstant_; }
  [[nodiscard]] cfd::Index scalarShiftFaces() const noexcept { return scalarShift_; }
  [[nodiscard]] cfd::Index scalarAffineFaces() const noexcept { return scalarAffine_; }
  [[nodiscard]] cfd::Index vectorConstantFaces() const noexcept { return vectorConstant_; }
  [[nodiscard]] cfd::Index vectorIdentityFaces() const noexcept { return vectorIdentity_; }
  [[nodiscard]] cfd::Index vectorSymmetryFaces() const noexcept { return vectorSymmetry_; }
  // Host-side copies, for building other plans and for the differential.
  [[nodiscard]] const std::vector<cfd::Index>& hostType() const noexcept { return hostType_; }
  [[nodiscard]] const std::vector<cfd::Index>& hostPrescribes() const noexcept {
    return hostPrescribes_;
  }
  [[nodiscard]] DeviceBoundaryView view() const noexcept;
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  bool usable_{false};
  bool hasScalar_{false};
  bool hasVector_{false};
  std::string unsupportedReason_;
  cfd::Index scalarConstant_{0}, scalarShift_{0}, scalarAffine_{0};
  cfd::Index vectorConstant_{0}, vectorIdentity_{0}, vectorSymmetry_{0};

  std::vector<cfd::Index> hostType_, hostPrescribes_;

  DeviceBuffer<cfd::Index> type_, prescribes_;
  DeviceBuffer<cfd::Index> scalarKind_, altKind_, vectorKind_;
  DeviceBuffer<cfd::Real> scalarA_, scalarB_, altA_, altB_;
  DeviceBuffer<cfd::Real> constX_, constY_, constZ_;
  DeviceBuffer<cfd::Real> normalX_, normalY_, normalZ_;
  DeviceBuffer<cfd::Real> distance_;
};

// Everything the shared evaluators produce for one field, per face. Used by the
// isolated differential; an operator would call the evaluators directly inside
// its own kernel rather than materialising this.
struct DeviceBoundaryEvaluation {
  DeviceBuffer<cfd::Real> scalarValue;
  DeviceBuffer<cfd::Real> alternativeScalarValue;
  DeviceBuffer<cfd::Real> vectorX, vectorY, vectorZ;
  DeviceBuffer<cfd::Real> ghostValue;
  DeviceBuffer<cfd::Index> type;
  DeviceBuffer<cfd::Index> prescribesValue;
};

void evaluateBoundaryConditionsDevice(
    const DeviceBoundaryConditions& conditions, const DeviceMesh& mesh,
    const DeviceBuffer<cfd::Real>& scalarField, const DeviceBuffer<cfd::Real>& velocityX,
    const DeviceBuffer<cfd::Real>& velocityY, const DeviceBuffer<cfd::Real>& velocityZ,
    const DeviceBuffer<cfd::Real>& faceFlux, DeviceBoundaryEvaluation& out);

}  // namespace cfd::gpu
