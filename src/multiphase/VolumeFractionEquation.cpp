#include "cfd/multiphase/VolumeFractionEquation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/TimeDerivative.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::multiphase {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Same evaluate-at-current-state pattern as every other transport
// equation's own boundary helper in this codebase (e.g.
// species::SpeciesEquation.cpp's boundaryConcentrationValue).
Real boundaryAlphaValue(const Mesh& mesh, const Face& face, const ScalarField& alpha,
                        const BoundaryConditionSet& alphaBoundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), alphaBoundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("VolumeFractionEquation: boundary condition is not scalar-valued");
  }
  const Real ownerValue = alpha[face.owner()];
  const Real distance =
      cfd::mesh::MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  return scalarBc->boundaryValue(ownerValue, distance);
}

}  // namespace

void assembleVolumeFractionConvectionContribution(const Mesh& mesh, const SurfaceField& massFlux,
                                                  const ScalarField& alpha,
                                                  const BoundaryConditionSet& alphaBoundaries,
                                                  SparseMatrixBuilder& builder, Vector& rhs) {
  if (alpha.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleVolumeFractionConvectionContribution: alpha size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleVolumeFractionConvectionContribution: massFlux size does not match mesh face "
        "count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real ownerFlux = massFlux[faceId];  // owner-oriented, already rho-weighted.

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      if (ownerFlux >= 0.0) {
        builder.add(ownerId, ownerId, ownerFlux);
      } else {
        const Real alphaB = boundaryAlphaValue(mesh, face, alpha, alphaBoundaries);
        rhs[ownerId] -= ownerFlux * alphaB;
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    if (ownerFlux >= 0.0) {
      builder.add(ownerId, ownerId, ownerFlux);
      builder.add(neighborId, ownerId, -ownerFlux);
    } else {
      builder.add(ownerId, neighborId, ownerFlux);
      builder.add(neighborId, neighborId, -ownerFlux);
    }
  }
}

VolumeFractionAssembly assembleVolumeFractionTransportEquation(
    const Mesh& mesh, const ScalarField& alphaOld, const SurfaceField& massFlux,
    const BoundaryConditionSet& alphaBoundaries, Real dt) {
  if (alphaOld.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleVolumeFractionTransportEquation: alphaOld size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleVolumeFractionTransportEquation: massFlux size does not match mesh face count");
  }

  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  // Bare coefficient of 1 (not a physical density) -- see this function's
  // own header comment.
  const cfd::discretization::TimeDerivativeCoefficients timeTerm =
      cfd::discretization::implicitEulerTimeDerivative(mesh, alphaOld, /*density=*/1.0, dt);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, timeTerm.diagonal[i]);
    rhs[i] += timeTerm.source[i];
  }

  assembleVolumeFractionConvectionContribution(mesh, massFlux, alphaOld, alphaBoundaries, builder,
                                               rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleVolumeFractionTransportEquation: assembled system contains a non-finite value");
  }

  return VolumeFractionAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs),
                                std::move(diagonal)};
}

AlphaBounds volumeFractionBounds(const ScalarField& alpha) {
  AlphaBounds bounds{std::numeric_limits<Real>::infinity(), -std::numeric_limits<Real>::infinity()};
  for (Index i = 0; i < alpha.size(); ++i) {
    bounds.minimum = std::min(bounds.minimum, alpha[i]);
    bounds.maximum = std::max(bounds.maximum, alpha[i]);
  }
  return bounds;
}

Real phaseVolume(const Mesh& mesh, const ScalarField& alpha) {
  if (alpha.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("phaseVolume: alpha size does not match mesh cell count");
  }
  Real volume = 0.0;
  for (const auto& cell : mesh.cells()) {
    volume += alpha[cell.id()] * cell.volume();
  }
  return volume;
}

}  // namespace cfd::multiphase
