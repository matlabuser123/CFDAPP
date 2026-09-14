#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::LinearSystem;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::boundaryConditionForFace;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::evaluateContinuity;

namespace {

// p' boundary condition for the gradient used by correctVelocity, per
// patch: FixedValue(0.0) where the actual pressure patch is FixedValue
// (Dirichlet, p'=0 there -- matching assemblePressureCorrection's
// treatment), FixedGradient(0.0) everywhere else.
BoundaryConditionSet makeGradientBoundaries(const Mesh& mesh,
                                            const BoundaryConditionSet& pressureBoundaries) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() == BoundaryConditionType::FixedValue) {
      boundaries.set(mesh, patch.name(), std::make_unique<FixedValue>(0.0));
    } else {
      boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
    }
  }
  return boundaries;
}

// Exactly axis-aligned area vector (exactly one component zero): the case
// in which S_D = D_f Sf is exactly d_comp * Sf, see
// pressureCorrectionFaceCoupling's header comment.
bool isAxisAligned(const Vector2& sf) noexcept {
  return ((sf.y == 0.0) && (sf.x != 0.0)) || ((sf.x == 0.0) && (sf.y != 0.0));
}

bool exactlyParallel(const Vector2& a, const Vector2& b) noexcept {
  return (a.x * b.y) - (a.y * b.x) == 0.0;
}

// The shared coupling formula for one face, given the face-level response
// coefficients (distance-weighted for an internal face, owner values for a
// boundary face) and the face's owner-to-{neighbor, face} vector d.
PressureFaceCoupling coupling(const Face& face, const Vector2& d, Real faceDensity, Real du,
                              Real dv, bool nonOrthogonal) {
  const Vector2& sf = face.areaVector();
  const Real distance = magnitude(d);
  if (isAxisAligned(sf) && exactlyParallel(d, sf)) {
    // S_D = d_comp * Sf exactly and E = S_D, T = 0 -- evaluated in the
    // pre-P12-NUM-003 operand order (bit-identical on Cartesian meshes).
    const Real dComponent = (sf.y == 0.0) ? du : dv;
    return PressureFaceCoupling{faceDensity * face.area() * dComponent / distance,
                                Vector2{0.0, 0.0}};
  }
  const Vector2 responseVector{du * sf.x, dv * sf.y};
  if (nonOrthogonal) {
    const auto decomposition = MeshGeometry::decomposeAreaVector(d, responseVector);
    if (decomposition.valid) {
      return PressureFaceCoupling{faceDensity * magnitude(decomposition.orthogonal) / distance,
                                  decomposition.nonOrthogonal * faceDensity};
    }
  }
  return PressureFaceCoupling{faceDensity * magnitude(responseVector) / distance,
                              Vector2{0.0, 0.0}};
}

}  // namespace

ScalarField computeMomentumResponseCoefficient(const Mesh& mesh, const Vector& momentumDiagonal) {
  if (momentumDiagonal.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "computeMomentumResponseCoefficient: momentumDiagonal size does not match mesh cell "
        "count");
  }
  ScalarField d(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    d[cell.id()] = cell.volume() / momentumDiagonal[cell.id()];
  }
  return d;
}

namespace {

// Degenerate geometry (e.g. coincident owner/neighbor centroids, |d| = 0)
// makes the coupling non-finite: detected here and reported as a
// NumericalError -- the failure category SIMPLE/CompressibleSIMPLE turn
// into NonFiniteState -- instead of surfacing later as a generic
// SparseMatrix validation error.
PressureFaceCoupling checked(PressureFaceCoupling coupling) {
  if (!std::isfinite(coupling.coefficient) || !std::isfinite(coupling.nonOrthogonal.x) ||
      !std::isfinite(coupling.nonOrthogonal.y)) {
    throw NumericalError(
        "pressureCorrectionFaceCoupling: non-finite face coupling (degenerate face geometry)");
  }
  return coupling;
}

}  // namespace

PressureFaceCoupling pressureCorrectionFaceCoupling(const Mesh& mesh, const Face& face,
                                                    Real faceDensity,
                                                    const ScalarField& uResponseCoefficient,
                                                    const ScalarField& vResponseCoefficient,
                                                    bool nonOrthogonal) {
  const Index ownerId = face.owner();
  if (face.isBoundary()) {
    const Vector2 d = face.centroid() - mesh.cell(ownerId).centroid();
    return checked(coupling(face, d, faceDensity, uResponseCoefficient[ownerId],
                            vResponseCoefficient[ownerId], nonOrthogonal));
  }
  const Vector2 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(ownerId).centroid();
  const Vector2& sf = face.areaVector();
  if (isAxisAligned(sf) && exactlyParallel(d, sf)) {
    // Interpolate only the component the legacy formula used (the other is
    // multiplied by an exact zero anyway) -- same arithmetic as before.
    const ScalarField& response = (sf.y == 0.0) ? uResponseCoefficient : vResponseCoefficient;
    const Real dFace = cfd::discretization::interpolateInternalFace(mesh, face, response);
    return checked(coupling(face, d, faceDensity, dFace, dFace, nonOrthogonal));
  }
  return checked(
      coupling(face, d, faceDensity,
               cfd::discretization::interpolateInternalFace(mesh, face, uResponseCoefficient),
               cfd::discretization::interpolateInternalFace(mesh, face, vResponseCoefficient),
               nonOrthogonal));
}

PressureCorrectionAssembly assembleGeometricPressureCorrection(
    const Mesh& mesh, const SurfaceField& predictorMassFlux, const SurfaceField& faceDensity,
    const ScalarField& uResponseCoefficient, const ScalarField& vResponseCoefficient,
    Index referenceCell, const BoundaryConditionSet& pressureBoundaries,
    const PressureCorrectionOptions& options, const ScalarField* additionalDiagonal) {
  const Index n = mesh.numberOfCells();
  if (predictorMassFlux.size() != mesh.numberOfFaces() ||
      faceDensity.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleGeometricPressureCorrection: predictorMassFlux/faceDensity size does not match "
        "mesh face count");
  }
  if (uResponseCoefficient.size() != n || vResponseCoefficient.size() != n) {
    throw InvalidArgumentError(
        "assembleGeometricPressureCorrection: response coefficient size does not match mesh cell "
        "count");
  }
  if (additionalDiagonal != nullptr && additionalDiagonal->size() != n) {
    throw InvalidArgumentError(
        "assembleGeometricPressureCorrection: additionalDiagonal size does not match mesh cell "
        "count");
  }
  if (options.previousPressureCorrection != nullptr &&
      options.previousPressureCorrection->size() != n) {
    throw InvalidArgumentError(
        "assembleGeometricPressureCorrection: previousPressureCorrection size does not match mesh "
        "cell count");
  }
  if (referenceCell >= n) {
    throw InvalidArgumentError("assembleGeometricPressureCorrection: referenceCell out of range");
  }

  // A FixedValue (Dirichlet) pressure patch anywhere already removes the
  // pressure-correction null space physically -- forcing `referenceCell`
  // to zero as well would over-constrain an already well-posed system
  // (see header comment). Determined up front so the per-face loop below
  // can decide, once, whether referenceCell's own row will be forced
  // afterward.
  bool hasOpenBoundary = false;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() == BoundaryConditionType::FixedValue) {
      hasOpenBoundary = true;
      break;
    }
  }
  const bool pinReferenceCell = !hasOpenBoundary;

  // P12-NUM-003: grad(p'_prev) for the explicit non-orthogonal term, only
  // on a correction pass after the first.
  std::optional<VectorField> gradPrevious;
  if (options.nonOrthogonal && options.previousPressureCorrection != nullptr) {
    gradPrevious = cfd::discretization::gradient(mesh, *options.previousPressureCorrection,
                                                 makeGradientBoundaries(mesh, pressureBoundaries),
                                                 options.gradientScheme);
  }

  SparseMatrixBuilder builder(n, n);
  // P4 -- Performance: same reservation rationale as
  // RelaxedMomentum.cpp's own -- this is SIMPLE's other per-outer-
  // iteration assembly call.
  builder.reserve(5 * n);
  SurfaceField faceCoefficient(mesh.numberOfFaces(), 0.0);
  SurfaceField explicitFaceFlux(mesh.numberOfFaces(), 0.0);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Index ownerId = face.owner();
    if (face.isBoundary()) {
      const auto& bc = boundaryConditionForFace(mesh, faceId, pressureBoundaries);
      if (bc.type() != BoundaryConditionType::FixedValue) {
        continue;  // Neumann-like: zero coupling -- see the header comment.
      }
      // Dirichlet (fixed-pressure) boundary: F_f' = D_f * (p'_P - 0),
      // using the owner cell's own response coefficients (no
      // interpolation -- there is no neighbor cell) and the vector from
      // the owner centroid to the boundary face.
      const auto terms =
          pressureCorrectionFaceCoupling(mesh, face, faceDensity[faceId], uResponseCoefficient,
                                         vResponseCoefficient, options.nonOrthogonal);
      faceCoefficient[faceId] = terms.coefficient;
      if (gradPrevious.has_value()) {
        explicitFaceFlux[faceId] = -dot(terms.nonOrthogonal, (*gradPrevious)[ownerId]);
      }
      if (!pinReferenceCell || ownerId != referenceCell) {
        builder.add(ownerId, ownerId, terms.coefficient);
      }
      continue;
    }

    const auto terms =
        pressureCorrectionFaceCoupling(mesh, face, faceDensity[faceId], uResponseCoefficient,
                                       vResponseCoefficient, options.nonOrthogonal);
    const Real dCoefficient = terms.coefficient;
    faceCoefficient[faceId] = dCoefficient;
    if (gradPrevious.has_value()) {
      explicitFaceFlux[faceId] =
          -dot(terms.nonOrthogonal,
               cfd::discretization::interpolateInternalFace(mesh, face, *gradPrevious));
    }

    const Index neighborId = *face.neighbor();

    // Face-once assembly (TODO.md section 51): both rows' equal/opposite
    // contributions added from this single face visit -- except the
    // reference cell's own row, which (when pinReferenceCell) is left
    // untouched here and forced to the identity equation below
    // (SparseMatrixBuilder only ever sums entries, so its row can't be
    // "added then overwritten").
    if (!pinReferenceCell || ownerId != referenceCell) {
      builder.add(ownerId, ownerId, dCoefficient);
      builder.add(ownerId, neighborId, -dCoefficient);
    }
    if (!pinReferenceCell || neighborId != referenceCell) {
      builder.add(neighborId, neighborId, dCoefficient);
      builder.add(neighborId, ownerId, -dCoefficient);
    }
  }

  const auto continuity = evaluateContinuity(mesh, predictorMassFlux);
  Vector rhs(n);
  for (const auto& cell : mesh.cells()) {
    rhs[cell.id()] = -continuity.cellImbalance[cell.id()];
  }
  // P12-NUM-003: sum_f s_f (F*_f + D_f (p'_P - p'_N) + explicit_f) = 0, so
  // the explicit part moves to the RHS with s_f = +1 (owner) / -1 (neighbor).
  if (gradPrevious.has_value()) {
    for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
      const Face& face = mesh.face(faceId);
      rhs[face.owner()] -= explicitFaceFlux[faceId];
      if (!face.isBoundary()) {
        rhs[*face.neighbor()] += explicitFaceFlux[faceId];
      }
    }
  }

  if (additionalDiagonal != nullptr) {
    for (const auto& cell : mesh.cells()) {
      const Index id = cell.id();
      if (pinReferenceCell && id == referenceCell) continue;
      builder.add(id, id, (*additionalDiagonal)[id]);
    }
  }

  if (pinReferenceCell) {
    builder.add(referenceCell, referenceCell, 1.0);
    rhs[referenceCell] = 0.0;
  }

  SparseMatrix matrix = builder.build();
  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleGeometricPressureCorrection: assembled system contains a non-finite value");
  }

  return PressureCorrectionAssembly{LinearSystem(std::move(matrix), std::move(rhs)),
                                    std::move(faceCoefficient), std::move(explicitFaceFlux)};
}

PressureCorrectionAssembly assemblePressureCorrection(
    const Mesh& mesh, const SurfaceField& predictorMassFlux,
    const ScalarField& uResponseCoefficient, const ScalarField& vResponseCoefficient, Real density,
    Index referenceCell, const BoundaryConditionSet& pressureBoundaries,
    const PressureCorrectionOptions& options) {
  const Index n = mesh.numberOfCells();
  if (predictorMassFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assemblePressureCorrection: predictorMassFlux size does not match mesh face count");
  }
  if (uResponseCoefficient.size() != n || vResponseCoefficient.size() != n) {
    throw InvalidArgumentError(
        "assemblePressureCorrection: response coefficient size does not match mesh cell count");
  }
  if (!std::isfinite(density) || !(density > 0.0)) {
    throw InvalidArgumentError("assemblePressureCorrection: density must be finite and > 0");
  }
  if (referenceCell >= n) {
    throw InvalidArgumentError("assemblePressureCorrection: referenceCell out of range");
  }
  // Constant density: every face carries the same rho (bit-identical to the
  // pre-P12-NUM-003 single `density` multiply).
  const SurfaceField faceDensity(mesh.numberOfFaces(), density);
  return assembleGeometricPressureCorrection(mesh, predictorMassFlux, faceDensity,
                                             uResponseCoefficient, vResponseCoefficient,
                                             referenceCell, pressureBoundaries, options, nullptr);
}

SurfaceField correctFaceMassFlux(const Mesh& mesh, const SurfaceField& predictorMassFlux,
                                 const SurfaceField& faceCoefficient,
                                 const ScalarField& pressureCorrection,
                                 const SurfaceField* explicitFaceFlux) {
  if (predictorMassFlux.size() != mesh.numberOfFaces() ||
      faceCoefficient.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("correctFaceMassFlux: field size does not match mesh face count");
  }
  if (explicitFaceFlux != nullptr && explicitFaceFlux->size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "correctFaceMassFlux: explicitFaceFlux size does not match mesh face count");
  }
  if (pressureCorrection.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "correctFaceMassFlux: pressureCorrection size does not match mesh cell count");
  }

  SurfaceField corrected(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real pOwner = pressureCorrection[face.owner()];
    // Internal face: F_f' = D_f*(p'_owner - p'_neighbor). Boundary face:
    // F_f' = D_f*(p'_owner - 0), the Dirichlet-pressure-patch term from
    // assemblePressureCorrection -- faceCoefficient is exactly 0 for
    // every other boundary face, making this a no-op there.
    const Real pNeighbor = face.isBoundary() ? 0.0 : pressureCorrection[*face.neighbor()];
    const Real fluxCorrection = faceCoefficient[faceId] * (pOwner - pNeighbor);
    corrected[faceId] = predictorMassFlux[faceId] + fluxCorrection;
    if (explicitFaceFlux != nullptr) {
      corrected[faceId] += (*explicitFaceFlux)[faceId];
    }
  }
  return corrected;
}

VectorField correctVelocity(const Mesh& mesh, const VectorField& predictorVelocity,
                            const ScalarField& uResponseCoefficient,
                            const ScalarField& vResponseCoefficient,
                            const ScalarField& pressureCorrection,
                            const BoundaryConditionSet& pressureBoundaries,
                            cfd::discretization::GradientScheme scheme) {
  const Index n = mesh.numberOfCells();
  if (predictorVelocity.size() != n || uResponseCoefficient.size() != n ||
      vResponseCoefficient.size() != n || pressureCorrection.size() != n) {
    throw InvalidArgumentError("correctVelocity: field size does not match mesh cell count");
  }

  const BoundaryConditionSet boundaries = makeGradientBoundaries(mesh, pressureBoundaries);
  const VectorField gradPPrime =
      cfd::discretization::gradient(mesh, pressureCorrection, boundaries, scheme);

  VectorField corrected(n);
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    corrected[id] =
        Vector2{predictorVelocity[id].x - (uResponseCoefficient[id] * gradPPrime[id].x),
                predictorVelocity[id].y - (vResponseCoefficient[id] * gradPPrime[id].y)};
  }
  return corrected;
}

}  // namespace cfd::pressure_velocity
