#pragma once

// GPU-DISC-001B -- CUDA-only. See DeviceBuffer.hpp's "never include from a
// CPU-only-compiled file" rule.
//
// CUDA port of cfd::discretization::greenGaussGradient. The CPU implementation
// is the numerical reference; this reproduces it, including P12-NUM-003's
// skewness sweeps, P12-MESH-001's oblique-Neumann boundary faces and
// P12-GRAD-002's boundary-consistent face value.
//
// The design rests on one observation from the audit
// (results/gpu-disc-001/gradients/audit.md): almost every hard quantity in that
// algorithm is a function of the MESH, not of the field -- opposite-face
// topology, crossing weights, boundary-line intersections, and therefore also
// the winner of the boundary-consistent claim conflict. Those are built ONCE on
// the host, using the very same MeshGeometry functions the CPU path calls, and
// uploaded. The kernels evaluate only what actually depends on the field.
//
// That is precomputation, not reformulation: every arithmetic expression is
// preserved verbatim, in the same order, so the target is BITWISE equality with
// the CPU gradient rather than agreement to a tolerance.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceMesh.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// Immutable, mesh-and-boundary-dependent data for one gradient operator.
//
// Built once per (mesh, boundary set). Holds no field values, so it is valid for
// every gradient evaluation on that mesh.
class DeviceGradientPlan {
 public:
  // Returns false and leaves the plan unusable if any boundary face carries a
  // condition this path cannot reproduce bitwise (see `unsupportedReason`).
  // Never silently approximates: an unsupported condition disqualifies the mesh
  // for the device gradient rather than being replaced by something close.
  bool build(const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& boundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return mesh_; }
  [[nodiscard]] cfd::Index claimCount() const noexcept { return claimCount_; }
  [[nodiscard]] cfd::Index skewedFaceCount() const noexcept { return skewedFaceIds_.size(); }
  [[nodiscard]] cfd::Index obliqueFaceCount() const noexcept { return obliqueFaceIds_.size(); }
  // Whether greenGaussGradient's sweep loop runs at all. Mirrors the CPU's
  // `!skewedFaces.empty() || !obliqueFaces.empty() || boundaryTransfer` exactly
  // -- the sweep count is part of the discretization, so getting this wrong
  // would silently change the answer on an orthogonal mesh.
  [[nodiscard]] bool sweepsNeeded() const noexcept { return sweepsNeeded_; }
  [[nodiscard]] bool boundaryTransferNeeded() const noexcept { return boundaryTransfer_; }
  // Device memory attributable to this plan, INCLUDING the scratch below. That
  // means the number grows after the first gradient evaluation allocates the
  // scratch -- a plan reported before any evaluation is smaller than the same
  // plan reported after one. Stated because two evidence logs print this at
  // different points and would otherwise look inconsistent.
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  friend void greenGaussGradientDevice(const DeviceGradientPlan&, const DeviceBuffer<cfd::Real>&,
                                       cfd::Index, DeviceBuffer<cfd::Real>&,
                                       DeviceBuffer<cfd::Real>&, DeviceBuffer<cfd::Real>&);

  DeviceMesh mesh_;
  bool usable_{false};
  bool sweepsNeeded_{false};
  bool boundaryTransfer_{false};
  std::string unsupportedReason_;
  cfd::Index claimCount_{0};

  // Interior interpolation: the CPU computes
  //   (dNf * phiP + dPf * phiN) / (dPf + dNf)
  // so both distances are stored and the same expression is evaluated, rather
  // than a single premultiplied weight (which would not be bitwise equal).
  DeviceBuffer<cfd::Real> faceDPf_, faceDNf_;

  // Boundary faces, indexed by face id; the distance is interpolate()'s
  // straight-line |x_f - x_P|. `kind` selects which EXPRESSION the device
  // evaluates, because reproducing boundaryValue bitwise means reproducing its
  // arithmetic, not just its value in exact arithmetic:
  //
  //   kBoundaryConstant  value = a            FixedValue, FixedTemperature, WallOmega
  //   kBoundaryShift     value = phiP + a     FixedGradient, Adiabatic, HeatFlux
  //   kBoundaryAffine    value = a + b*phiP   anything else that is still affine
  //
  // The shift form exists because recovering b as boundaryValue(1,d) -
  // boundaryValue(0,d) does NOT give exactly 1.0 for a Neumann condition: with
  // a = g*d, fl(1 + a) - a differs from 1 whenever a has bits below 1 ULP of
  // (1 + a). Every form is verified bitwise against the condition itself.
  DeviceBuffer<cfd::Real> boundaryA_, boundaryB_;
  DeviceBuffer<cfd::Index> boundaryKind_;

  // P12-NUM-003 skew correction. Compact arrays over the skewed faces only,
  // matching the CPU's `skewedFaces` list.
  DeviceBuffer<cfd::Index> skewedFaceIds_;
  DeviceBuffer<cfd::Real> skewVecX_, skewVecY_, skewVecZ_;  // crossing skew vector
  DeviceBuffer<cfd::Real> skewTOfFace_;                     // crossing weight t

  // P12-MESH-001 oblique-Neumann boundary faces, re-evaluated inside every
  // sweep as boundaryValue(phiP, d_n) + grad(phi)_P . d_t. A SECOND affine
  // encoding is needed because the distance here is d.n, not the straight-line
  // distance interpolate() used for the initial value.
  DeviceBuffer<cfd::Index> obliqueFaceIds_;
  DeviceBuffer<cfd::Real> obliqueA_, obliqueB_;
  DeviceBuffer<cfd::Index> obliqueKind_;
  DeviceBuffer<cfd::Real> obliqueTangentX_, obliqueTangentY_, obliqueTangentZ_;

  // P12-GRAD-002 boundary-consistent claims, conflict already resolved on the
  // host so at most one claim exists per (cell, target face).
  DeviceBuffer<cfd::Index> claimCell_, claimTargetFace_, claimBoundaryFace_, claimFarCell_;
  DeviceBuffer<cfd::Real> claimFarDistance_, claimBackDistance_, claimW_;
  DeviceBuffer<cfd::Real> claimOffsetX_, claimOffsetY_, claimOffsetZ_;
  // Per (cell,face) slot in the CSR connectivity: index into the claim arrays,
  // or kNoClaim. Lets the summation kernel substitute a claimed value without
  // searching, preserving the CPU's "claimed value if claimed, else faceValue".
  DeviceBuffer<cfd::Index> slotClaim_;

  // Scratch, owned by the plan so repeated evaluation on the same mesh performs
  // no allocation (DeviceBuffer never shrinks). Mutable because a gradient
  // evaluation does not change the plan's meaning -- the plan stays immutable
  // mesh data; these are its workspace.
  mutable DeviceBuffer<cfd::Real> faceValues_;
  mutable DeviceBuffer<cfd::Real> claimValues_;

 public:
  static constexpr cfd::Index kNoClaim = static_cast<cfd::Index>(-1);
  // Aliases of the shared encoding constants (BoundaryEncoding.hpp), so the
  // plan builder and the kernel cannot disagree about what a `kind` means.
  static constexpr cfd::Index kBoundaryConstant = kBoundaryEncodingConstant;
  static constexpr cfd::Index kBoundaryShift = kBoundaryEncodingShift;
  static constexpr cfd::Index kBoundaryAffine = kBoundaryEncodingAffine;
};

// grad(phi) for a device-resident scalar field, written into gradX/gradY/gradZ
// (resized as needed). The result stays on the device: no D2H occurs here, so a
// later GPU-DISC operator can consume it directly.
//
// `sweeps` mirrors kGreenGaussSkewCorrectionSweeps; pass that constant for
// production behaviour.
//
// NOT thread-safe with respect to a single plan: the plan owns the face-value
// and claim-value scratch, so two concurrent evaluations sharing one plan would
// race on it. One plan per thread, or one evaluation at a time -- which is what
// the solver does, and what keeps repeated evaluation allocation-free.
void greenGaussGradientDevice(const DeviceGradientPlan& plan, const DeviceBuffer<cfd::Real>& phi,
                              cfd::Index sweeps, DeviceBuffer<cfd::Real>& gradX,
                              DeviceBuffer<cfd::Real>& gradY, DeviceBuffer<cfd::Real>& gradZ);

}  // namespace cfd::gpu
