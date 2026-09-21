// GPU-DISC-001C -- host-side construction of the immutable diffusion plan.
//
// Every geometric quantity below comes from the production MeshGeometry
// functions the CPU assembler itself calls -- decomposeFaceArea,
// decomposeBoundaryFaceArea, boundaryInwardStencil, ownerNeighborDistance,
// distance. Nothing is re-derived with a local formula, so the device cannot
// disagree with the CPU about the mesh.
//
// See results/gpu-disc-001/diffusion/audit.md for the algorithm this mirrors
// and for why the implicit path is the production one.

#include <algorithm>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/gpu/DeviceDiffusion.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

bool DeviceDiffusionPlan::build(const Mesh& mesh,
                                const cfd::boundary::BoundaryConditionSet& boundaries,
                                bool leastSquaresRequested) {
  usable_ = false;
  unsupportedReason_.clear();
  higherOrderFaces_ = 0;
  correctableFaces_ = 0;

  if (leastSquaresRequested) {
    unsupportedReason_ =
        "GradientScheme::LeastSquares is not ported (GPU-DISC-001B covered GreenGauss only); "
        "substituting GreenGauss would silently change the discretization";
    return false;
  }
  // The assembly ALWAYS needs a gradient (P12-DIFF-002 A2), so the gradient plan
  // is a hard prerequisite -- and its own boundary-condition verification is
  // what rejects a mesh this path cannot reproduce bitwise.
  if (!gradient_.build(mesh, boundaries)) {
    unsupportedReason_ = "gradient plan unusable: " + gradient_.unsupportedReason();
    return false;
  }

  const Index nc = static_cast<Index>(mesh.numberOfCells());
  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  cellCount_ = nc;

  std::vector<Real> dPf(nf, 0.0), dNf(nf, 0.0), dPN(nf, 0.0);
  std::vector<Index> decompValid(nf, 0);
  std::vector<Real> orthMag(nf, 0.0), nonOrthX(nf, 0.0), nonOrthY(nf, 0.0), nonOrthZ(nf, 0.0);

  std::vector<Real> bDistance(nf, 0.0);
  std::vector<Index> bPrescribed(nf, 0);
  std::vector<Real> bA(nf, 0.0), bB(nf, 0.0);
  std::vector<Index> bKind(nf, kBoundaryEncodingConstant);
  std::vector<Index> bStencilValid(nf, 0), bFarCell(nf, 0);
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
    const Real distance =
        MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    bDistance[f] = distance;

    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
    const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
    if (scalarBc == nullptr) {
      unsupportedReason_ = "face " + std::to_string(f) + ": boundary condition is not scalar-valued";
      return false;
    }
    if (!encodeBoundaryCondition(*scalarBc, distance, bA[f], bB[f], bKind[f])) {
      unsupportedReason_ = "face " + std::to_string(f) + ": boundary condition '" +
                           std::string(bc.name()) +
                           "' is not bitwise-reproducible as a constant, a shift or an affine "
                           "function of phi_P; mesh rejected for the device diffusion path";
      return false;
    }
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
      bDeltaPX[f] = stencil.deltaP.x;
      bDeltaPY[f] = stencil.deltaP.y;
      bDeltaPZ[f] = stencil.deltaP.z;
      bDeltaFX[f] = stencil.deltaF.x;
      bDeltaFY[f] = stencil.deltaF.y;
      bDeltaFZ[f] = stencil.deltaF.z;
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

  // --- gather structure ----------------------------------------------------
  // Incident faces per cell, sorted by face id: the order the production loop
  // visits them, and therefore the order repeated (row, column) triplets keep
  // through SparseMatrixBuilder's stable sort.
  std::vector<Index> faceOffsets(nc + 1, 0);
  std::vector<Index> faceSorted;
  std::vector<Index> rowOffsets(nc + 1, 0);
  std::vector<Index> columnIndices;
  {
    std::vector<Index> faces;
    std::vector<Index> columns;
    for (Index c = 0; c < nc; ++c) {
      faceOffsets[c] = static_cast<Index>(faceSorted.size());
      rowOffsets[c] = static_cast<Index>(columnIndices.size());

      faces.assign(mesh.cell(c).faceIds().begin(), mesh.cell(c).faceIds().end());
      std::sort(faces.begin(), faces.end());
      faceSorted.insert(faceSorted.end(), faces.begin(), faces.end());

      columns.clear();
      columns.push_back(c);
      for (const Index faceId : faces) {
        const Face& face = mesh.face(faceId);
        if (face.isBoundary()) continue;
        // The far-cell entry a boundary face may add targets the cell across the
        // owner's opposite interior face, which is already an internal-face
        // neighbour -- so it introduces no new column. Asserted by the
        // differential, which compares the CPU's own pattern against this one.
        columns.push_back((face.owner() == c) ? *face.neighbor() : face.owner());
      }
      std::sort(columns.begin(), columns.end());
      columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
      columnIndices.insert(columnIndices.end(), columns.begin(), columns.end());
    }
    faceOffsets[nc] = static_cast<Index>(faceSorted.size());
    rowOffsets[nc] = static_cast<Index>(columnIndices.size());
  }
  entryCount_ = static_cast<Index>(columnIndices.size());

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(faceDPf_, dPf);
  put(faceDNf_, dNf);
  put(faceDPN_, dPN);
  put(faceDecompValid_, decompValid);
  put(faceOrthMag_, orthMag);
  put(faceNonOrthX_, nonOrthX);
  put(faceNonOrthY_, nonOrthY);
  put(faceNonOrthZ_, nonOrthZ);
  put(bDistance_, bDistance);
  put(bPrescribed_, bPrescribed);
  put(bValueA_, bA);
  put(bValueB_, bB);
  put(bValueKind_, bKind);
  put(bStencilValid_, bStencilValid);
  put(bFarCell_, bFarCell);
  put(bCP_, bCP);
  put(bCF_, bCF);
  put(bCB_, bCB);
  put(bDeltaPX_, bDeltaPX);
  put(bDeltaPY_, bDeltaPY);
  put(bDeltaPZ_, bDeltaPZ);
  put(bDeltaFX_, bDeltaFX);
  put(bDeltaFY_, bDeltaFY);
  put(bDeltaFZ_, bDeltaFZ);
  put(bDecompValid_, bDecompValid);
  put(bOrthMag_, bOrthMag);
  put(bNonOrthX_, bNonOrthX);
  put(bNonOrthY_, bNonOrthY);
  put(bNonOrthZ_, bNonOrthZ);
  put(cellFaceOffsets_, faceOffsets);
  put(cellFaceSorted_, faceSorted);
  put(rowOffsets_, rowOffsets);
  put(columnIndices_, columnIndices);

  usable_ = true;
  return true;
}

std::size_t DeviceDiffusionPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return gradient_.residentBytes() + bytes(faceDPf_) + bytes(faceDNf_) + bytes(faceDPN_) +
         bytes(faceDecompValid_) + bytes(faceOrthMag_) + bytes(faceNonOrthX_) +
         bytes(faceNonOrthY_) + bytes(faceNonOrthZ_) + bytes(bDistance_) + bytes(bPrescribed_) +
         bytes(bValueA_) + bytes(bValueB_) + bytes(bValueKind_) + bytes(bStencilValid_) +
         bytes(bFarCell_) + bytes(bCP_) + bytes(bCF_) + bytes(bCB_) + bytes(bDeltaPX_) +
         bytes(bDeltaPY_) + bytes(bDeltaPZ_) + bytes(bDeltaFX_) + bytes(bDeltaFY_) +
         bytes(bDeltaFZ_) + bytes(bDecompValid_) + bytes(bOrthMag_) + bytes(bNonOrthX_) +
         bytes(bNonOrthY_) + bytes(bNonOrthZ_) + bytes(cellFaceOffsets_) + bytes(cellFaceSorted_) +
         bytes(rowOffsets_) + bytes(columnIndices_);
}

}  // namespace cfd::gpu
