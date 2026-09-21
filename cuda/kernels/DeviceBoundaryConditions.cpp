// GPU-DISC-001E -- host-side construction of the shared device boundary layer.
//
// Calls the production lookup (boundaryConditionForFace), the production
// classifier (prescribesBoundaryValue) and the production geometry
// (MeshGeometry::distance / unitNormal). Nothing here re-derives any of them.

#include "cfd/gpu/DeviceBoundaryConditions.hpp"

#include <string>
#include <vector>

#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
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

bool DeviceBoundaryConditions::build(const Mesh& mesh,
                                     const cfd::boundary::BoundaryConditionSet& conditions,
                                     const std::vector<Real>& alternativeDistance) {
  usable_ = false;
  hasScalar_ = hasVector_ = false;
  unsupportedReason_.clear();
  scalarConstant_ = scalarShift_ = scalarAffine_ = 0;
  vectorConstant_ = vectorIdentity_ = vectorSymmetry_ = 0;

  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  if (!alternativeDistance.empty() && alternativeDistance.size() != nf) {
    unsupportedReason_ = "alternativeDistance must have one entry per face";
    return false;
  }

  hostType_.assign(nf, kBcFixedValue);
  hostPrescribes_.assign(nf, 0);
  std::vector<Index> scalarKind(nf, kBoundaryEncodingConstant);
  std::vector<Index> altKind(nf, kBoundaryEncodingConstant);
  std::vector<Index> vectorKind(nf, kVectorBoundaryConstant);
  std::vector<Real> scalarA(nf, 0.0), scalarB(nf, 0.0), altA(nf, 0.0), altB(nf, 0.0);
  std::vector<Real> constX(nf, 0.0), constY(nf, 0.0), constZ(nf, 0.0);
  std::vector<Real> normalX(nf, 0.0), normalY(nf, 0.0), normalZ(nf, 0.0);
  std::vector<Real> distance(nf, 0.0);

  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (!face.isBoundary()) continue;

    const Index ownerId = face.owner();
    const Real d = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    const Vector2 normal = MeshGeometry::unitNormal(face);
    distance[f] = d;
    normalX[f] = normal.x;
    normalY[f] = normal.y;
    normalZ[f] = normal.z;

    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), conditions);
    hostType_[f] = static_cast<Index>(bc.type());
    hostPrescribes_[f] = cfd::discretization::prescribesBoundaryValue(bc.type()) ? 1 : 0;

    if (const auto* scalarBc =
            dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc)) {
      hasScalar_ = true;
      if (!encodeBoundaryCondition(*scalarBc, d, scalarA[f], scalarB[f], scalarKind[f])) {
        unsupportedReason_ = "face " + std::to_string(f) + ": scalar condition '" +
                             std::string(bc.name()) +
                             "' is not bitwise-reproducible as a constant, a shift or an affine "
                             "function of phi_P";
        return false;
      }
      if (scalarKind[f] == kBoundaryEncodingConstant) ++scalarConstant_;
      else if (scalarKind[f] == kBoundaryEncodingShift) ++scalarShift_;
      else ++scalarAffine_;

      // The alternative encoding: a second distance on the same face, for the
      // gradient's oblique-Neumann re-evaluation. Defaults to the primary.
      const Real alt = alternativeDistance.empty() ? 0.0 : alternativeDistance[f];
      if (alt > 0.0) {
        if (!encodeBoundaryCondition(*scalarBc, alt, altA[f], altB[f], altKind[f])) {
          unsupportedReason_ = "face " + std::to_string(f) + ": scalar condition '" +
                               std::string(bc.name()) +
                               "' is not bitwise-reproducible at the alternative distance";
          return false;
        }
      } else {
        altKind[f] = scalarKind[f];
        altA[f] = scalarA[f];
        altB[f] = scalarB[f];
      }
    }

    if (const auto* vectorBc =
            dynamic_cast<const cfd::boundary::VectorBoundaryCondition*>(&bc)) {
      hasVector_ = true;
      Vector2 constantValue{0.0, 0.0, 0.0};
      if (!encodeVectorBoundaryCondition(*vectorBc, d, normal, constantValue, vectorKind[f])) {
        unsupportedReason_ = "face " + std::to_string(f) + ": vector condition '" +
                             std::string(bc.name()) +
                             "' is not bitwise-reproducible as a constant, the identity or the "
                             "symmetry projection";
        return false;
      }
      constX[f] = constantValue.x;
      constY[f] = constantValue.y;
      constZ[f] = constantValue.z;
      if (vectorKind[f] == kVectorBoundaryConstant) ++vectorConstant_;
      else if (vectorKind[f] == kVectorBoundaryIdentity) ++vectorIdentity_;
      else ++vectorSymmetry_;
    }

    if (!hasScalar_ && !hasVector_) {
      unsupportedReason_ = "face " + std::to_string(f) + ": condition '" +
                           std::string(bc.name()) + "' is neither scalar- nor vector-valued";
      return false;
    }
  }

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(type_, hostType_);
  put(prescribes_, hostPrescribes_);
  put(scalarKind_, scalarKind);
  put(altKind_, altKind);
  put(vectorKind_, vectorKind);
  put(scalarA_, scalarA);
  put(scalarB_, scalarB);
  put(altA_, altA);
  put(altB_, altB);
  put(constX_, constX);
  put(constY_, constY);
  put(constZ_, constZ);
  put(normalX_, normalX);
  put(normalY_, normalY);
  put(normalZ_, normalZ);
  put(distance_, distance);

  usable_ = true;
  return true;
}

DeviceBoundaryView DeviceBoundaryConditions::view() const noexcept {
  DeviceBoundaryView v{};
  v.type = type_.data();
  v.prescribesValue = prescribes_.data();
  v.scalarKind = scalarKind_.data();
  v.scalarA = scalarA_.data();
  v.scalarB = scalarB_.data();
  v.altKind = altKind_.data();
  v.altA = altA_.data();
  v.altB = altB_.data();
  v.vectorKind = vectorKind_.data();
  v.constX = constX_.data();
  v.constY = constY_.data();
  v.constZ = constZ_.data();
  v.normalX = normalX_.data();
  v.normalY = normalY_.data();
  v.normalZ = normalZ_.data();
  v.distance = distance_.data();
  return v;
}

std::size_t DeviceBoundaryConditions::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return bytes(type_) + bytes(prescribes_) + bytes(scalarKind_) + bytes(altKind_) +
         bytes(vectorKind_) + bytes(scalarA_) + bytes(scalarB_) + bytes(altA_) + bytes(altB_) +
         bytes(constX_) + bytes(constY_) + bytes(constZ_) + bytes(normalX_) + bytes(normalY_) +
         bytes(normalZ_) + bytes(distance_);
}

}  // namespace cfd::gpu
