#pragma once

// GPU-DISC-001I -- CUDA-only.
//
// CUDA port of cfd::pressure_velocity::assembleGeometricPressureCorrection
// (PressureCorrectionEquation.cpp:181), the body behind the incompressible
// assemblePressureCorrection.
//
// ASSEMBLY ONLY. The pressure-correction linear SOLVE is a different thing and
// was already qualified by GPU-PCORR-001; this gate qualifies the assembly.
// No velocity correction, no face-flux correction, no SIMPLE integration.
//
// See results/gpu-disc-001/pressure-correction-assembly/audit.md.

#include <string>
#include <vector>

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

struct PressureCorrectionView;

// The assembled pressure-correction system, device-resident. Candidate CSR:
// row P holds column P plus every internal-face neighbour, ascending.
//
// `faceCoefficient` and `explicitFaceFlux` mirror PressureCorrectionAssembly's
// own outputs -- the face-flux correction step reuses the exact coefficients
// the matrix was built with, so they are produced here rather than recomputed.
struct DevicePressureCorrectionSystem {
  DeviceBuffer<cfd::Index> rowOffsets;
  DeviceBuffer<cfd::Index> columnIndices;
  DeviceBuffer<cfd::Real> values;
  DeviceBuffer<cfd::Real> rhs;
  DeviceBuffer<cfd::Real> faceCoefficient;
  DeviceBuffer<cfd::Real> explicitFaceFlux;
};

struct PressureCorrectionOptionsDevice {
  bool nonOrthogonal{false};
  cfd::Index referenceCell{0};
  cfd::Real density{1.0};
};

class DevicePressureCorrectionPlan {
 public:
  // `pressureBoundaries` must be scalar-valued. The plan records, once, whether
  // any patch is FixedValue -- which decides `pinReferenceCell` -- and builds
  // the gradient plan the explicit non-orthogonal term needs (p' boundary
  // conditions: FixedValue(0) at FixedValue pressure patches, FixedGradient(0)
  // elsewhere, exactly makeGradientBoundaries).
  bool build(const cfd::mesh::Mesh& mesh,
             const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return mesh_; }
  // pinReferenceCell == !hasOpenBoundary. Exposed so a test can assert the
  // DECISION, not merely its numerical consequence.
  [[nodiscard]] bool pinReferenceCell() const noexcept { return pinReferenceCell_; }
  [[nodiscard]] bool threeDimensional() const noexcept { return threeDimensional_; }
  [[nodiscard]] cfd::Index fixedValueBoundaryFaces() const noexcept { return fixedValueFaces_; }
  [[nodiscard]] cfd::Index axisAlignedFaces() const noexcept { return axisAlignedFaces_; }
  [[nodiscard]] cfd::Index generalFaces() const noexcept { return generalFaces_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

  // GPU-PIPE-001 (GPU-resident pressure solve). The CSR structure depends on
  // which cell is pinned, so it cannot be finalised in build(): `referenceCell`
  // only arrives per assembly.
  //
  // WHY THE STRUCTURE DEPENDS ON IT AT ALL. The pinned row is exactly
  // `diagonal = 1, every off-diagonal = 0` (assembleKernel's isReference
  // branch). toHostSystem() then DROPS those explicit zeros, because
  // SparseMatrixBuilder only receives entries with a non-zero value -- so the
  // matrix the host solver has always received is *narrower* than the device
  // one, by exactly the pinned row's off-diagonal count. Measured: 2 entries at
  // 40x40, 160x160 and 320x320 alike (results/gpu-pipe-001/
  // gpu-resident-pressure-solve/audit.md section 8).
  //
  // Building the structure without those entries makes the device matrix
  // IDENTICAL to the host one, which is what lets the solve run device-resident
  // without changing the problem. It also leaves the existing host path
  // bit-for-bit unchanged: toHostSystem() simply finds nothing to drop.
  //
  // Keyed on `referenceCell`, so production -- where SIMPLE's referenceCell_ is
  // fixed for the whole solve -- rebuilds once and then never again.
  void ensureStructure(cfd::Index referenceCell) const;
  [[nodiscard]] cfd::Index entryCount() const noexcept { return entryCount_; }

 private:
  friend void fillPressureCorrectionView(const DevicePressureCorrectionPlan&,
                                         PressureCorrectionView&);
  friend void assemblePressureCorrectionDevice(
      const DevicePressureCorrectionPlan&, const DeviceBuffer<cfd::Real>&,
      const DeviceBuffer<cfd::Real>&, const DeviceBuffer<cfd::Real>&,
      const DeviceBuffer<cfd::Real>*, const DeviceBuffer<cfd::Real>*,
      const PressureCorrectionOptionsDevice&, DevicePressureCorrectionSystem&);

  DeviceMesh mesh_;
  // For the explicit term's gradient of the previous p'.
  DeviceGradientPlan previousGradient_;

  bool usable_{false};
  bool pinReferenceCell_{true};
  bool threeDimensional_{false};
  std::string unsupportedReason_;
  cfd::Index cellCount_{0};
  cfd::Index faceCount_{0};
  // Mutable: ensureStructure() is logically const (it establishes a cache), and
  // the assembly entry point holds the plan by const reference.
  mutable cfd::Index entryCount_{0};
  cfd::Index fixedValueFaces_{0};
  cfd::Index axisAlignedFaces_{0};
  cfd::Index generalFaces_{0};

  // Per face. For a boundary face d = faceCentroid - ownerCentroid; for an
  // internal face d = neighbourCentroid - ownerCentroid. The axis-aligned test
  // uses the matching d, so the flags differ from the face-flux plan's and are
  // built here rather than reused.
  DeviceBuffer<cfd::Real> dX_, dY_, dZ_, distance_;
  DeviceBuffer<cfd::Index> axisAligned_, axisIndex_, areaZIsZero_, isFixedValue_;
  // Interior interpolation distances for the response coefficients.
  DeviceBuffer<cfd::Real> faceDPf_, faceDNf_;

  DeviceBuffer<cfd::Index> cellFaceOffsets_, cellFaceSorted_;
  // The FULL pattern (every row carries its diagonal plus each distinct
  // internal neighbour), retained on the host so ensureStructure() can derive
  // the pinned variant without the Mesh, which the plan does not keep.
  std::vector<cfd::Index> fullRowOffsets_, fullColumnIndices_;
  // The structure actually uploaded, and the referenceCell it was built for.
  // kNoStructure means "not built yet" -- distinct from every valid cell index.
  static constexpr cfd::Index kNoStructure = static_cast<cfd::Index>(-1);
  mutable cfd::Index structureReferenceCell_{kNoStructure};
  mutable DeviceBuffer<cfd::Index> rowOffsets_, columnIndices_;

  mutable DeviceBuffer<cfd::Real> gradPrevX_, gradPrevY_, gradPrevZ_;
};

// Assembles the pressure-correction system.
//
// `previousPressureCorrection` is null on the first pass; when non-null AND
// options.nonOrthogonal is set, the explicit term is built from its gradient,
// exactly as the CPU does. `dW` is required on a 3D mesh.
void assemblePressureCorrectionDevice(
    const DevicePressureCorrectionPlan& plan, const DeviceBuffer<cfd::Real>& predictorMassFlux,
    const DeviceBuffer<cfd::Real>& dU, const DeviceBuffer<cfd::Real>& dV,
    const DeviceBuffer<cfd::Real>* dW, const DeviceBuffer<cfd::Real>* previousPressureCorrection,
    const PressureCorrectionOptionsDevice& options, DevicePressureCorrectionSystem& system);

}  // namespace cfd::gpu
