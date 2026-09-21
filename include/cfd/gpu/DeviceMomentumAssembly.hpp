#pragma once

// GPU-DISC-001F -- CUDA-only.
//
// CUDA port of cfd::pressure_velocity::assembleRelaxedMomentumComponent, the
// SIMPLE momentum assembly:
//
//   diffusion + convection + pressure source [+ buoyancy] [+ momentum source]
//   + implicit under-relaxation
//
// ASSEMBLY ONLY. No momentum response coefficients, no predicted face flux, no
// Rhie-Chow, no pressure-correction assembly, no velocity correction, no SIMPLE
// integration.
//
// Every heavy term is delegated to an already-qualified device operator rather
// than reimplemented:
//
//   convection          assembleMomentumConvectionDevice      (001D, 10352 bitwise cases)
//   velocity gradient   computeVelocityGradientDevice         (001D)
//   pressure gradient   greenGaussGradientDevice              (001B, 132 bitwise cases)
//   diffusion terms     DeviceDiffusionTerms.hpp              (001C's implementation, shared)
//   vector boundaries   VectorBoundaryEncoding.hpp            (001D/001E)
//
// What is new here is the diffusion glue for a velocity component, the pressure
// source, the under-relaxation, and the accumulation ordering that makes the
// assembled system comparable bit for bit.
//
// See results/gpu-disc-001/momentum-assembly/audit.md.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/gpu/DeviceDiffusionTerms.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// The assembled momentum system for one component, device-resident. Same
// candidate-CSR contract as the other assemblies: row P holds column P plus
// every internal-face neighbour, ascending.
struct DeviceMomentumSystem {
  DeviceBuffer<cfd::Index> rowOffsets;
  DeviceBuffer<cfd::Index> columnIndices;
  DeviceBuffer<cfd::Real> values;
  DeviceBuffer<cfd::Real> rhs;
  DeviceBuffer<cfd::Real> diagonal;  // matrix.diagonal(row), as MomentumAssembly reports it
};

// Which terms to assemble. Mirrors the optional arguments of
// assembleRelaxedMomentumComponent.
struct MomentumAssemblyOptions {
  cfd::Index component{kVelocityU};
  cfd::Index convectionScheme{kConvectionUpwind};
  bool applyNonOrthogonalCorrection{false};
  // alpha == 1.0 takes the CPU's exact early-return path: no relaxation triplet
  // is appended at all, rather than an extra +0.0.
  cfd::Real relaxationAlpha{1.0};
};

struct ConvectionAssemblyView;
struct MomentumAssemblyView;

class DeviceMomentumAssemblyPlan {
 public:
  // `velocityBoundaries` must be vector-valued, `pressureBoundaries` scalar-valued.
  bool build(const cfd::mesh::Mesh& mesh,
             const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
             const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMomentumConvectionPlan& convectionPlan() const noexcept {
    return convection_;
  }
  [[nodiscard]] const DeviceGradientPlan& pressureGradientPlan() const noexcept {
    return pressureGradient_;
  }
  [[nodiscard]] cfd::Index higherOrderBoundaryFaces() const noexcept { return higherOrderFaces_; }
  [[nodiscard]] cfd::Index correctableInternalFaces() const noexcept { return correctableFaces_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  friend void fillMomentumAssemblyView(const DeviceMomentumAssemblyPlan&, MomentumAssemblyView&);
  friend void assembleRelaxedMomentumDevice(const DeviceMomentumAssemblyPlan&,
                                            const DeviceVelocity&, const DeviceBuffer<cfd::Real>&,
                                            const DeviceBuffer<cfd::Real>&,
                                            const DeviceBuffer<cfd::Real>&,
                                            const DeviceBuffer<cfd::Real>&,
                                            const DeviceBuffer<cfd::Real>*,
                                            const MomentumAssemblyOptions&, DeviceMomentumSystem&);

  // The convection plan owns the device mesh, the vector boundary encoding, the
  // velocity gradient and the CSR pattern -- all of which the momentum assembly
  // needs and none of which it rebuilds.
  DeviceMomentumConvectionPlan convection_;
  // The scalar gradient plan for the pressure source.
  DeviceGradientPlan pressureGradient_;

  bool usable_{false};
  std::string unsupportedReason_;
  cfd::Index cellCount_{0};
  cfd::Index entryCount_{0};
  cfd::Index higherOrderFaces_{0};
  cfd::Index correctableFaces_{0};

  // Diffusion geometry, from the same production MeshGeometry calls 001C used.
  DeviceBuffer<cfd::Real> faceDPf_, faceDNf_, faceDPN_;
  DeviceBuffer<cfd::Index> decompValid_;
  DeviceBuffer<cfd::Real> orthMag_, nonOrthX_, nonOrthY_, nonOrthZ_;
  DeviceBuffer<cfd::Real> bDistance_;
  DeviceBuffer<cfd::Index> bPrescribed_, bStencilValid_, bFarCell_;
  DeviceBuffer<cfd::Real> bCP_, bCF_, bCB_;
  DeviceBuffer<cfd::Real> bDeltaPX_, bDeltaPY_, bDeltaPZ_;
  DeviceBuffer<cfd::Real> bDeltaFX_, bDeltaFY_, bDeltaFZ_;
  DeviceBuffer<cfd::Index> bDecompValid_;
  DeviceBuffer<cfd::Real> bOrthMag_, bNonOrthX_, bNonOrthY_, bNonOrthZ_;

  mutable DeviceVelocityGradient velocityGradient_;
  mutable DeviceBuffer<cfd::Real> pressureGradX_, pressureGradY_, pressureGradZ_;
};

// Assembles the SIMPLE momentum system for one component.
//
// `previousComponent` is the relaxation's phiOld; pass `momentumSource` (a
// per-cell body force for this component, already component-selected) or
// nullptr. Buoyancy, when used, is folded into `momentumSource` by the caller --
// both are pure RHS additions applied in the same place, so the device needs one
// path, not two.
void assembleRelaxedMomentumDevice(
    const DeviceMomentumAssemblyPlan& plan, const DeviceVelocity& velocity,
    const DeviceBuffer<cfd::Real>& pressure, const DeviceBuffer<cfd::Real>& effectiveViscosity,
    const DeviceBuffer<cfd::Real>& massFlux, const DeviceBuffer<cfd::Real>& previousComponent,
    const DeviceBuffer<cfd::Real>* momentumSource, const MomentumAssemblyOptions& options,
    DeviceMomentumSystem& system);

}  // namespace cfd::gpu
