// GPU-DISC-001F -- host-side construction of the momentum assembly plan.
//
// Composes two already-qualified plans and adds only the diffusion geometry:
//
//   DeviceMomentumConvectionPlan  (001D) -- device mesh, vector BCs, velocity
//                                           gradient, CSR pattern, convection
//   DeviceGradientPlan            (001B) -- the scalar gradient for the pressure source
//
// The diffusion geometry is built by calling the SAME production MeshGeometry
// functions 001C calls (decomposeFaceArea, decomposeBoundaryFaceArea,
// boundaryInwardStencil). That is the same host code running again, not a
// second implementation -- the device-side face-term arithmetic is shared
// through DeviceDiffusionTerms.hpp.

#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

bool DeviceMomentumAssemblyPlan::build(
    const Mesh& mesh, const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  higherOrderFaces_ = correctableFaces_ = 0;

  if (!convection_.build(mesh, velocityBoundaries)) {
    unsupportedReason_ = "momentum convection plan unusable: " + convection_.unsupportedReason();
    return false;
  }
  if (!pressureGradient_.build(mesh, pressureBoundaries)) {
    unsupportedReason_ = "pressure gradient plan unusable: " + pressureGradient_.unsupportedReason();
    return false;
  }

  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  cellCount_ = convection_.cellCount();
  entryCount_ = convection_.entryCount();

  std::vector<Real> dPf(nf, 0.0), dNf(nf, 0.0), dPN(nf, 0.0);
  std::vector<Index> decompValid(nf, 0);
  std::vector<Real> orthMag(nf, 0.0), nonOrthX(nf, 0.0), nonOrthY(nf, 0.0), nonOrthZ(nf, 0.0);
  std::vector<Real> bDistance(nf, 0.0);
  std::vector<Index> bPrescribed(nf, 0), bStencilValid(nf, 0), bFarCell(nf, 0);
  std::vector<Real> bCP(nf, 0.0), bCF(nf, 0.0), bCB(nf, 0.0);
  std::vector<Real> bDeltaPX(nf, 0.0), bDeltaPY(nf, 0.0), bDeltaPZ(nf, 0.0);
  std::vector<Real> bDeltaFX(nf, 0.0), bDeltaFY(nf, 0.0), bDeltaFZ(nf, 0.0);
  std::vector<Index> bDecompValid(nf, 0);
  std::vector<Real> bOrthMag(nf, 0.0), bNonOrthX(nf, 0.0), bNonOrthY(nf, 0.0), bNonOrthZ(nf, 0.0);

  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (!face.isBoundary()) {
      dPf[f] = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
      dNf[f] = MeshGeometry::distance(mesh.cell(*face.neighbor()).centroid(), face.centroid());
      dPN[f] = MeshGeometry::ownerNeighborDistance(mesh, face);
      const auto decomposition = MeshGeometry::decomposeFaceArea(mesh, face);
      if (decomposition.valid) {
        decompValid[f] = 1;
        ++correctableFaces_;
        orthMag[f] = magnitude(decomposition.orthogonal);
        nonOrthX[f] = decomposition.nonOrthogonal.x;
        nonOrthY[f] = decomposition.nonOrthogonal.y;
        nonOrthZ[f] = decomposition.nonOrthogonal.z;
      }
      continue;
    }

    const Index ownerId = face.owner();
    bDistance[f] = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    // prescribesVelocity(): the same classifier the CPU momentum path uses.
    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), velocityBoundaries);
    bPrescribed[f] = cfd::discretization::prescribesBoundaryValue(bc.type()) ? 1 : 0;

    const auto stencil = MeshGeometry::boundaryInwardStencil(mesh, face);
    if (stencil.valid) {
      bStencilValid[f] = 1;
      ++higherOrderFaces_;
      const Real h1 = stencil.h1;
      const Real h2 = stencil.h2;
      bCP[f] = h2 / (h1 * (h2 - h1));
      bCF[f] = h1 / (h2 * (h2 - h1));
      bCB[f] = (1.0 / h1) + (1.0 / h2);
      bFarCell[f] = stencil.farCell;
      bDeltaPX[f] = stencil.deltaP.x; bDeltaPY[f] = stencil.deltaP.y; bDeltaPZ[f] = stencil.deltaP.z;
      bDeltaFX[f] = stencil.deltaF.x; bDeltaFY[f] = stencil.deltaF.y; bDeltaFZ[f] = stencil.deltaF.z;
    }
    const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
    if (decomposition.valid) {
      bDecompValid[f] = 1;
      bOrthMag[f] = magnitude(decomposition.orthogonal);
      bNonOrthX[f] = decomposition.nonOrthogonal.x;
      bNonOrthY[f] = decomposition.nonOrthogonal.y;
      bNonOrthZ[f] = decomposition.nonOrthogonal.z;
    }
  }

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(faceDPf_, dPf); put(faceDNf_, dNf); put(faceDPN_, dPN);
  put(decompValid_, decompValid); put(orthMag_, orthMag);
  put(nonOrthX_, nonOrthX); put(nonOrthY_, nonOrthY); put(nonOrthZ_, nonOrthZ);
  put(bDistance_, bDistance); put(bPrescribed_, bPrescribed);
  put(bStencilValid_, bStencilValid); put(bFarCell_, bFarCell);
  put(bCP_, bCP); put(bCF_, bCF); put(bCB_, bCB);
  put(bDeltaPX_, bDeltaPX); put(bDeltaPY_, bDeltaPY); put(bDeltaPZ_, bDeltaPZ);
  put(bDeltaFX_, bDeltaFX); put(bDeltaFY_, bDeltaFY); put(bDeltaFZ_, bDeltaFZ);
  put(bDecompValid_, bDecompValid); put(bOrthMag_, bOrthMag);
  put(bNonOrthX_, bNonOrthX); put(bNonOrthY_, bNonOrthY); put(bNonOrthZ_, bNonOrthZ);

  usable_ = true;
  return true;
}

std::size_t DeviceMomentumAssemblyPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return convection_.residentBytes() + pressureGradient_.residentBytes() + bytes(faceDPf_) +
         bytes(faceDNf_) + bytes(faceDPN_) + bytes(decompValid_) + bytes(orthMag_) +
         bytes(nonOrthX_) + bytes(nonOrthY_) + bytes(nonOrthZ_) + bytes(bDistance_) +
         bytes(bPrescribed_) + bytes(bStencilValid_) + bytes(bFarCell_) + bytes(bCP_) +
         bytes(bCF_) + bytes(bCB_) + bytes(bDeltaPX_) + bytes(bDeltaPY_) + bytes(bDeltaPZ_) +
         bytes(bDeltaFX_) + bytes(bDeltaFY_) + bytes(bDeltaFZ_) + bytes(bDecompValid_) +
         bytes(bOrthMag_) + bytes(bNonOrthX_) + bytes(bNonOrthY_) + bytes(bNonOrthZ_);
}

}  // namespace cfd::gpu
