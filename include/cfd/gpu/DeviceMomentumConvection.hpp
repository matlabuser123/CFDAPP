#pragma once

// GPU-DISC-001D closure -- CUDA-only.
//
// CUDA port of physics::assembleConvectionContribution: the production
// convection contribution of one velocity component, for all four schemes, with
// vector boundary conditions.
//
// This is a CONVECTION-ONLY closure. It assembles the convection term and
// nothing else -- no diffusion, no transient term, no momentum sources, no
// relaxation, no pressure gradient. It does not constitute momentum assembly,
// and the Momentum Path TODO items stay unchecked.
//
// It carries one dependency the scalar path did not: LinearUpwind needs
// discretization::computeVelocityGradient, which is a DIFFERENT operator from
// the scalar greenGaussGradient qualified in 001B -- it has no P12-GRAD-002
// boundary fit and resolves vector boundary conditions. That operator is
// therefore ported here too (greenGaussVelocityGradient, with P12-NUM-003's
// skewness sweeps), and qualified by the same differential.

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/gpu/DeviceConvectionTerms.hpp"
#include "cfd/gpu/DeviceMesh.hpp"
#include "cfd/gpu/VectorBoundaryEncoding.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// kVelocityU/V/W live in DeviceConvection.hpp (included above) so the shared
// face-term headers can use them without a circular include.

// A device-resident velocity field, structure-of-arrays.
struct DeviceVelocity {
  DeviceBuffer<cfd::Real> x, y, z;
};

// grad(U) per component, each a 3-vector per cell. gradW is only filled on a 3D
// mesh, matching VelocityGradientField's own contract.
struct DeviceVelocityGradient {
  DeviceBuffer<cfd::Real> gradUx, gradUy, gradUz;
  DeviceBuffer<cfd::Real> gradVx, gradVy, gradVz;
  DeviceBuffer<cfd::Real> gradWx, gradWy, gradWz;
};

struct ConvectionAssemblyView;

class DeviceMomentumConvectionPlan {
 public:
  bool build(const cfd::mesh::Mesh& mesh,
             const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

  [[nodiscard]] bool usable() const noexcept { return usable_; }
  [[nodiscard]] const std::string& unsupportedReason() const noexcept { return unsupportedReason_; }
  [[nodiscard]] const DeviceMesh& mesh() const noexcept { return mesh_; }
  [[nodiscard]] bool threeDimensional() const noexcept { return threeDimensional_; }
  [[nodiscard]] cfd::Index skewedFaceCount() const noexcept { return skewedFaceIds_.size(); }
  [[nodiscard]] cfd::Index farUpstreamOwnerUpwind() const noexcept { return farOwnerUpwind_; }
  [[nodiscard]] cfd::Index farUpstreamNeighborUpwind() const noexcept { return farNeighborUpwind_; }
  // Counts per encoded vector-boundary form, so a differential run can show
  // which conditions were actually exercised.
  [[nodiscard]] cfd::Index constantBoundaryFaces() const noexcept { return constantFaces_; }
  [[nodiscard]] cfd::Index identityBoundaryFaces() const noexcept { return identityFaces_; }
  [[nodiscard]] cfd::Index symmetryBoundaryFaces() const noexcept { return symmetryFaces_; }
  [[nodiscard]] std::size_t residentBytes() const noexcept;

  // GPU-DISC-001F: the momentum assembly reuses this plan's device mesh, vector
  // boundary encoding and CSR pattern rather than rebuilding them. Exposed as
  // read-only accessors so that reuse does not require friendship.
  [[nodiscard]] cfd::Index cellCount() const noexcept { return cellCount_; }
  [[nodiscard]] cfd::Index entryCount() const noexcept { return entryCount_; }
  [[nodiscard]] const cfd::Index* rowOffsets() const noexcept { return rowOffsets_.data(); }
  [[nodiscard]] const cfd::Index* columnIndices() const noexcept { return columnIndices_.data(); }
  [[nodiscard]] const cfd::Index* sortedFaceOffsets() const noexcept {
    return cellFaceOffsets_.data();
  }
  [[nodiscard]] const cfd::Index* sortedFaces() const noexcept { return cellFaceSorted_.data(); }
  [[nodiscard]] const cfd::Index* boundaryKind() const noexcept { return bKind_.data(); }
  [[nodiscard]] const cfd::Real* boundaryConstX() const noexcept { return bConstX_.data(); }
  [[nodiscard]] const cfd::Real* boundaryConstY() const noexcept { return bConstY_.data(); }
  [[nodiscard]] const cfd::Real* boundaryConstZ() const noexcept { return bConstZ_.data(); }
  [[nodiscard]] const cfd::Real* boundaryNormalX() const noexcept { return bNormalX_.data(); }
  [[nodiscard]] const cfd::Real* boundaryNormalY() const noexcept { return bNormalY_.data(); }
  [[nodiscard]] const cfd::Real* boundaryNormalZ() const noexcept { return bNormalZ_.data(); }
  // The per-face convection geometry, for a caller evaluating the shared face
  // terms itself (the momentum assembly has to, so its convection contributions
  // interleave with diffusion's in the CPU's accumulation order).
  [[nodiscard]] DeviceConvectionGeometry geometry() const noexcept {
    DeviceConvectionGeometry g;
    g.dPf = faceDPf_.data();
    g.dNf = faceDNf_.data();
    g.offOX = offsetOwnerX_.data();
    g.offOY = offsetOwnerY_.data();
    g.offOZ = offsetOwnerZ_.data();
    g.offNX = offsetNeighborX_.data();
    g.offNY = offsetNeighborY_.data();
    g.offNZ = offsetNeighborZ_.data();
    g.farOValid = farOwnerValid_.data();
    g.farOCell = farOwnerCell_.data();
    g.farOHCU = farOwnerHCU_.data();
    g.farNValid = farNeighborValid_.data();
    g.farNCell = farNeighborCell_.data();
    g.farNHCU = farNeighborHCU_.data();
    return g;
  }
  [[nodiscard]] DeviceVectorBoundaryView boundaryView() const noexcept {
    DeviceVectorBoundaryView bc;
    bc.kind = bKind_.data();
    bc.constX = bConstX_.data();
    bc.constY = bConstY_.data();
    bc.constZ = bConstZ_.data();
    bc.normalX = bNormalX_.data();
    bc.normalY = bNormalY_.data();
    bc.normalZ = bNormalZ_.data();
    return bc;
  }

 private:
  friend void fillConvectionAssemblyView(const DeviceMomentumConvectionPlan&,
                                         ConvectionAssemblyView&);
  friend void computeVelocityGradientDevice(const DeviceMomentumConvectionPlan&,
                                            const DeviceVelocity&, DeviceVelocityGradient&);
  friend void assembleMomentumConvectionDevice(const DeviceMomentumConvectionPlan&,
                                               const DeviceVelocity&,
                                               const DeviceBuffer<cfd::Real>&, cfd::Index,
                                               cfd::Index, DeviceConvectionSystem&);

  DeviceMesh mesh_;
  bool usable_{false};
  bool threeDimensional_{false};
  std::string unsupportedReason_;
  cfd::Index cellCount_{0};
  cfd::Index entryCount_{0};
  cfd::Index farOwnerUpwind_{0};
  cfd::Index farNeighborUpwind_{0};
  cfd::Index constantFaces_{0};
  cfd::Index identityFaces_{0};
  cfd::Index symmetryFaces_{0};

  DeviceBuffer<cfd::Real> faceDPf_, faceDNf_;
  DeviceBuffer<cfd::Real> offsetOwnerX_, offsetOwnerY_, offsetOwnerZ_;
  DeviceBuffer<cfd::Real> offsetNeighborX_, offsetNeighborY_, offsetNeighborZ_;
  DeviceBuffer<cfd::Index> farOwnerValid_, farOwnerCell_;
  DeviceBuffer<cfd::Real> farOwnerHCU_;
  DeviceBuffer<cfd::Index> farNeighborValid_, farNeighborCell_;
  DeviceBuffer<cfd::Real> farNeighborHCU_;

  // Vector boundary conditions, encoded and bitwise-verified.
  DeviceBuffer<cfd::Index> bKind_;
  DeviceBuffer<cfd::Real> bConstX_, bConstY_, bConstZ_;
  DeviceBuffer<cfd::Real> bNormalX_, bNormalY_, bNormalZ_;

  // P12-NUM-003 skewness sweeps of the velocity gradient.
  DeviceBuffer<cfd::Index> skewedFaceIds_;
  DeviceBuffer<cfd::Real> skewT_, skewVecX_, skewVecY_, skewVecZ_;

  DeviceBuffer<cfd::Index> cellFaceOffsets_, cellFaceSorted_;
  DeviceBuffer<cfd::Index> rowOffsets_, columnIndices_;

  mutable DeviceBuffer<cfd::Real> faceVelX_, faceVelY_, faceVelZ_;
  mutable DeviceVelocityGradient gradient_;
};

// CUDA port of discretization::computeVelocityGradient (GreenGauss branch).
// Exposed so the differential can compare it directly, not only through
// LinearUpwind.
void computeVelocityGradientDevice(const DeviceMomentumConvectionPlan& plan,
                                   const DeviceVelocity& velocity,
                                   DeviceVelocityGradient& gradient);

// CUDA port of physics::assembleConvectionContribution for one component.
// `component` is one of kVelocityU/V/W, `scheme` one of the kConvection*
// constants. Assembles into `system`, all device-resident.
void assembleMomentumConvectionDevice(const DeviceMomentumConvectionPlan& plan,
                                      const DeviceVelocity& velocity,
                                      const DeviceBuffer<cfd::Real>& massFlux, cfd::Index component,
                                      cfd::Index scheme, DeviceConvectionSystem& system);

}  // namespace cfd::gpu
