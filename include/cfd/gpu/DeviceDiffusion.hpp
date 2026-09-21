#pragma once

// GPU-DISC-001C -- CUDA-only. See DeviceBuffer.hpp's "never include from a
// CPU-only-compiled file" rule.
//
// CUDA port of the PRODUCTION implicit diffusion assembly: the shared face
// terms of cfd::discretization::NonOrthogonalDiffusion (the one implementation
// behind momentum viscous, thermal conduction, species and turbulence scalar
// diffusion) driven by the assembly of
// turbulence::assembleScalarDiffusionContribution.
//
// NOT a port of cfd::discretization::diffusion(), the explicit operator. That
// one returns div(Gamma grad phi) as a field, has no production caller, and is
// where the recorded MESH-005 two-cells-across defect lives. See
// results/gpu-disc-001/diffusion/audit.md section 1 for why the implicit path
// is the one that matters, and section 6 for how the known defect is handled.
//
// Design follows 001B exactly: everything that depends only on the mesh and the
// boundary-condition TYPES is built once on the host by calling the production
// MeshGeometry functions, and the kernels evaluate only what depends on the
// fields. The gradient the assembly needs is the already-qualified CUDA
// Green-Gauss operator, consumed device-resident -- not a second gradient.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceGradient.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// The assembled diffusion contribution, device-resident.
//
// The matrix is CSR with the CANDIDATE sparsity pattern: row P holds column P
// plus every cell sharing an internal face with P, ascending. SparseMatrixBuilder
// drops an entry whose accumulated value is exactly 0.0, so the CPU's pattern is
// value-dependent and can in principle be a subset of this one. For diffusion it
// never is -- every coefficient is Gamma*|S|/d > 0 -- but that is checked by the
// differential rather than assumed, and this type keeps the structural entry so
// the check is possible.
struct DeviceDiffusionSystem {
  DeviceBuffer<cfd::Index> rowOffsets;     // size cellCount + 1
  DeviceBuffer<cfd::Index> columnIndices;  // size rowOffsets[cellCount]
  DeviceBuffer<cfd::Real> values;          // same size as columnIndices
  DeviceBuffer<cfd::Real> rhs;             // size cellCount
};

// Immutable, mesh-and-boundary-dependent data for one diffusion operator.
class DeviceDiffusionPlan {
 public:
  // `gradientScheme` must be GreenGauss: it is the only scheme 001B ported, and
  // substituting it for a requested LeastSquares would silently change the
  // discretization. Returns false with a reason otherwise, as it does for any
  // boundary condition that cannot be reproduced bitwise.
  bool build(const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& boundaries,
             bool leastSquaresRequested = false);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceGradientPlan& gradientPlan() const noexcept { return gradient_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return gradient_.mesh(); }
  // How many boundary faces took P12-DIFF-002's three-point reconstruction, and
  // how many internal faces have a valid non-orthogonal decomposition. Reported
  // so a differential run can show these branches were actually reached.
  [[nodiscard]] cfd::Index higherOrderBoundaryFaces() const noexcept { return higherOrderFaces_; }
  [[nodiscard]] cfd::Index correctableInternalFaces() const noexcept { return correctableFaces_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  friend void assembleScalarDiffusionDevice(const DeviceDiffusionPlan&,
                                            const DeviceBuffer<cfd::Real>&,
                                            const DeviceBuffer<cfd::Real>&, bool,
                                            DeviceDiffusionSystem&);

  DeviceGradientPlan gradient_;
  bool usable_{false};
  std::string unsupportedReason_;
  cfd::Index higherOrderFaces_{0};
  cfd::Index correctableFaces_{0};

  // --- per face, indexed by face id ---------------------------------------
  // Interior: the interpolation distances (both kept, so the CPU's exact
  // expression is evaluated rather than a premultiplied weight) and dPN.
  DeviceBuffer<cfd::Real> faceDPf_, faceDNf_, faceDPN_;
  // decomposeFaceArea: |S_orth| and S_nonorth, plus whether it was valid.
  DeviceBuffer<cfd::Index> faceDecompValid_;
  DeviceBuffer<cfd::Real> faceOrthMag_, faceNonOrthX_, faceNonOrthY_, faceNonOrthZ_;

  // Boundary: owner-to-face distance, whether the condition prescribes a value,
  // and the bitwise-verified encoding of boundaryValue at that distance.
  DeviceBuffer<cfd::Real> bDistance_;
  DeviceBuffer<cfd::Index> bPrescribed_;
  DeviceBuffer<cfd::Real> bValueA_, bValueB_;
  DeviceBuffer<cfd::Index> bValueKind_;
  // boundaryInwardStencil (P12-DIFF-002): cP, cF, cB depend only on h1 and h2,
  // so they are precomputed; deltaP/deltaF are the tangential transfers.
  DeviceBuffer<cfd::Index> bStencilValid_, bFarCell_;
  DeviceBuffer<cfd::Real> bCP_, bCF_, bCB_;
  DeviceBuffer<cfd::Real> bDeltaPX_, bDeltaPY_, bDeltaPZ_;
  DeviceBuffer<cfd::Real> bDeltaFX_, bDeltaFY_, bDeltaFZ_;
  // decomposeBoundaryFaceArea, the two-point non-orthogonal fallback.
  DeviceBuffer<cfd::Index> bDecompValid_;
  DeviceBuffer<cfd::Real> bOrthMag_, bNonOrthX_, bNonOrthY_, bNonOrthZ_;

  // --- gather structure ----------------------------------------------------
  // Each cell's incident faces sorted by FACE ID -- the order the production
  // face loop visits them, which is the order SparseMatrixBuilder's stable sort
  // preserves for repeated (row, column) triplets. `cell.faceIds()` order would
  // be an unchecked guess.
  DeviceBuffer<cfd::Index> cellFaceOffsets_, cellFaceSorted_;
  // Candidate CSR pattern: column P plus each internal-face neighbour, ascending.
  DeviceBuffer<cfd::Index> rowOffsets_, columnIndices_;
  cfd::Index cellCount_{0};
  cfd::Index entryCount_{0};

  // Gradient scratch, owned here so repeated assembly on the same mesh performs
  // no allocation. Mutable for the same reason the gradient plan's scratch is:
  // an assembly does not change what the plan means.
  mutable DeviceBuffer<cfd::Real> gradX_, gradY_, gradZ_;
};

// Assembles the diffusion contribution of `phi` with per-cell `diffusivity`
// into `system`, all device-resident.
//
// `nonOrthogonalEnabled` mirrors NonOrthogonalCorrectionOptions::enabled: it
// gates the INTERNAL-face correction only. The boundary reconstruction is always
// applied, because P12-DIFF-002 A2 deliberately decoupled it from that flag --
// reproducing that is the difference between matching production and matching a
// plausible-looking variant of it.
//
// The gradient is computed internally by the qualified 001B operator and stays
// on the device. No host/device transfer occurs.
void assembleScalarDiffusionDevice(const DeviceDiffusionPlan& plan,
                                   const DeviceBuffer<cfd::Real>& phi,
                                   const DeviceBuffer<cfd::Real>& diffusivity,
                                   bool nonOrthogonalEnabled, DeviceDiffusionSystem& system);

}  // namespace cfd::gpu
