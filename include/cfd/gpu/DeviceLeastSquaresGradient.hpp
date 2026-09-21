#pragma once

// GPU-DISC-001J -- CUDA-only.
//
// CUDA port of cfd::discretization::leastSquaresGradient (Gradient.cpp:361),
// the second production scalar-gradient operator. `correctVelocity` forwards
// SIMPLESettings::gradientScheme, which a case file may set to
// "least_squares" (SolverConfigParser.cpp:283), so this operator is on the
// production velocity-correction path and GPU-DISC-001B -- Green-Gauss only --
// did not cover it.
//
// See results/gpu-disc-001/velocity-correction/audit.md section 7.
//
// What is precomputed and what is not:
//
//   Every DISPLACEMENT is pure geometry -- the interior ones trivially, the
//   oblique-Neumann ones (unitNormal * normalDistance) too. So the normal
//   matrix S, its cofactors, its determinant, the conditioning verdict and the
//   per-cell 2D/3D verdict are field-independent and are built once, here, in
//   cell.faceIds() order using the very expressions Gradient.cpp uses.
//
//   The right-hand side b is NOT: it carries valueDifference, which depends on
//   the field and, at a boundary, on boundaryValue(phi_P, d). It is accumulated
//   on device, in the same order, with the same zero-distance skip.
//
//   The finiteness backstop is also field-dependent, so `wellConditioned` is
//   split: the host decides the det/scale part, the device re-checks isfinite
//   and falls back too if it fires.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/gpu/DeviceMesh.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

struct LeastSquaresGradientView;

// One entry per (cell, face) slot, in cell.faceIds() order.
inline constexpr cfd::Index kLsEntryInterior = 0;
inline constexpr cfd::Index kLsEntryBoundary = 1;
// A displacement whose branch-appropriate |d|^2 is not > 0 is skipped entirely,
// exactly as the CPU `continue`s. Marked on the host because it is geometry.
inline constexpr cfd::Index kLsEntrySkipped = 2;

class DeviceLeastSquaresGradientPlan {
 public:
  // `boundaries` must be scalar-valued on every patch. Returns false and leaves
  // the plan unusable if any condition cannot be encoded bitwise -- never
  // silently approximates.
  bool build(const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& boundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return greenGauss_.mesh(); }
  // The fallback operator, and the owner of the device mesh both paths share.
  [[nodiscard]] const DeviceGradientPlan& greenGauss() const noexcept { return greenGauss_; }

  [[nodiscard]] cfd::Index cellCount() const noexcept { return cellCount_; }
  [[nodiscard]] cfd::Index entryCount() const noexcept { return entryCount_; }
  [[nodiscard]] cfd::Index obliqueEntries() const noexcept { return obliqueEntries_; }
  [[nodiscard]] cfd::Index skippedEntries() const noexcept { return skippedEntries_; }
  // Cells whose local system the host rejected as singular/ill-conditioned:
  // they take the Green-Gauss fallback. Exposed so a test can assert the
  // fallback is actually exercised rather than assumed.
  [[nodiscard]] cfd::Index illConditionedCells() const noexcept { return illConditioned_; }
  [[nodiscard]] cfd::Index threeDimensionalCells() const noexcept { return threeDimensional_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  friend void fillLeastSquaresGradientView(const DeviceLeastSquaresGradientPlan&,
                                           LeastSquaresGradientView&);
  friend void leastSquaresGradientDevice(const DeviceLeastSquaresGradientPlan&,
                                         const DeviceBuffer<cfd::Real>&, DeviceBuffer<cfd::Real>&,
                                         DeviceBuffer<cfd::Real>&, DeviceBuffer<cfd::Real>&);

  DeviceGradientPlan greenGauss_;

  bool usable_{false};
  std::string unsupportedReason_;
  cfd::Index cellCount_{0};
  cfd::Index entryCount_{0};
  cfd::Index obliqueEntries_{0};
  cfd::Index skippedEntries_{0};
  cfd::Index illConditioned_{0};
  cfd::Index threeDimensional_{0};

  // Per entry.
  DeviceBuffer<cfd::Index> entryOffsets_;   // cellCount + 1
  DeviceBuffer<cfd::Index> entryKind_;      // kLsEntry*
  DeviceBuffer<cfd::Index> entryNeighbor_;  // interior only
  // The boundary condition at the distance THAT entry uses -- the oblique
  // normal distance where the oblique treatment applies, the straight-line
  // distance otherwise.
  DeviceBuffer<cfd::Real> entryA_, entryB_;
  DeviceBuffer<cfd::Index> entryEncoding_;
  // weight * d, already rounded. The CPU evaluates `weight * dx * dphi` as
  // ((weight * dx) * dphi), so storing the inner product and multiplying by
  // dphi on device is bitwise identical -- and the same three values build S
  // here on the host.
  DeviceBuffer<cfd::Real> entryWdX_, entryWdY_, entryWdZ_;

  // Per cell. For a 2D cell only c11/c12/c22 are meaningful (they hold the 2x2
  // solve's Syy, Sxy and Sxx); the kernel branches on `cellThreeD_`.
  DeviceBuffer<cfd::Index> cellThreeD_;
  DeviceBuffer<cfd::Index> cellConditioned_;
  DeviceBuffer<cfd::Real> c11_, c12_, c13_, c22_, c23_, c33_, det_;
};

// grad(phi) by weighted least squares, with the Green-Gauss fallback applied
// per cell exactly as the CPU does. Everything stays on the device.
//
// NOT thread-safe per plan: the embedded Green-Gauss plan owns scratch.
void leastSquaresGradientDevice(const DeviceLeastSquaresGradientPlan& plan,
                                const DeviceBuffer<cfd::Real>& phi, DeviceBuffer<cfd::Real>& gradX,
                                DeviceBuffer<cfd::Real>& gradY, DeviceBuffer<cfd::Real>& gradZ);

}  // namespace cfd::gpu
