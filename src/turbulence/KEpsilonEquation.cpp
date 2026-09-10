#include "cfd/turbulence/KEpsilonEquation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

namespace cfd::turbulence {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

bool allFinite(const ScalarField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

ScalarField toScalarField(const Vector& v) {
  ScalarField field(v.size());
  for (Index i = 0; i < v.size(); ++i) field[i] = v[i];
  return field;
}

Vector toVector(const ScalarField& field) {
  Vector v(field.size());
  for (Index i = 0; i < field.size(); ++i) v[i] = field[i];
  return v;
}

}  // namespace

ScalarField computeEffectiveDiffusivity(Real molecularViscosity,
                                        const ScalarField& turbulentViscosity, Real sigma) {
  if (!std::isfinite(molecularViscosity) || !(molecularViscosity > 0.0)) {
    throw InvalidArgumentError(
        "computeEffectiveDiffusivity: molecularViscosity must be finite and > 0");
  }
  if (!std::isfinite(sigma) || !(sigma > 0.0)) {
    throw InvalidArgumentError("computeEffectiveDiffusivity: sigma must be finite and > 0");
  }
  ScalarField gamma(turbulentViscosity.size());
  for (Index i = 0; i < turbulentViscosity.size(); ++i) {
    const Real muT = turbulentViscosity[i];
    if (!std::isfinite(muT) || muT < 0.0) {
      throw InvalidArgumentError(
          "computeEffectiveDiffusivity: turbulentViscosity must be finite and >= 0");
    }
    // Gamma = mu + mu_t/sigma -- NOT (mu+mu_t)/sigma.
    gamma[i] = molecularViscosity + (muT / sigma);
  }
  return gamma;
}

void assembleScalarDiffusionContribution(const Mesh& mesh, const ScalarField& diffusivity,
                                         const ScalarField& phi,
                                         const BoundaryConditionSet& boundaries,
                                         SparseMatrixBuilder& builder, Vector& rhs) {
  const Index n = mesh.numberOfCells();
  if (phi.size() != n) {
    throw InvalidArgumentError(
        "assembleScalarDiffusionContribution: phi size does not match mesh cell count");
  }
  if (diffusivity.size() != n) {
    throw InvalidArgumentError(
        "assembleScalarDiffusionContribution: diffusivity size does not match mesh cell count");
  }
  for (Index i = 0; i < n; ++i) {
    if (!std::isfinite(diffusivity[i]) || !(diffusivity[i] > 0.0)) {
      throw InvalidArgumentError(
          "assembleScalarDiffusionContribution: diffusivity must be finite and > 0 in every "
          "cell");
    }
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real gammaFace = diffusivity[ownerId];
      const Real diffusionCoefficient = gammaFace * face.area() / distance;

      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        throw InvalidArgumentError(
            "assembleScalarDiffusionContribution: boundary condition is not scalar-valued");
      }
      const Real phiB = scalarBc->boundaryValue(phi[ownerId], distance);

      builder.add(ownerId, ownerId, diffusionCoefficient);
      rhs[ownerId] += diffusionCoefficient * phiB;
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real gammaFace = cfd::discretization::interpolateInternalFace(mesh, face, diffusivity);
    const Real diffusionCoefficient = gammaFace * face.area() / dPN;

    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void applyImplicitScalarSource(const Mesh& mesh, SparseMatrixBuilder& builder, Vector& rhs,
                               const ScalarField& Su, const ScalarField& Sp) {
  const Index n = mesh.numberOfCells();
  if (Su.size() != n || Sp.size() != n) {
    throw InvalidArgumentError(
        "applyImplicitScalarSource: Su/Sp size does not match mesh cell count");
  }
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    if (!std::isfinite(Su[id]) || !std::isfinite(Sp[id])) {
      throw InvalidArgumentError("applyImplicitScalarSource: Su/Sp must be finite in every cell");
    }
    builder.add(id, id, -Sp[id] * cell.volume());
    rhs[id] += Su[id] * cell.volume();
  }
}

ScalarTransportAssembly assembleScalarTransportEquation(const Mesh& mesh, const ScalarField& phi,
                                                        const SurfaceField& massFlux,
                                                        const ScalarField& diffusivity,
                                                        const BoundaryConditionSet& boundaries,
                                                        const ScalarField& Su,
                                                        const ScalarField& Sp) {
  const Index n = mesh.numberOfCells();
  if (phi.size() != n) {
    throw InvalidArgumentError(
        "assembleScalarTransportEquation: phi size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleScalarTransportEquation: massFlux size does not match mesh face count");
  }

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleScalarDiffusionContribution(mesh, diffusivity, phi, boundaries, builder, rhs);
  // specificHeat = 1.0: see this file's own header comment on why this
  // reuse is exact for a bare (non-cp-weighted) transported scalar.
  cfd::thermal::assembleThermalConvectionContribution(mesh, /*specificHeat=*/1.0, massFlux, phi,
                                                      boundaries, builder, rhs);
  applyImplicitScalarSource(mesh, builder, rhs, Su, Sp);

  SparseMatrix matrix = builder.build();
  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleScalarTransportEquation: assembled system contains a non-finite value");
  }

  return ScalarTransportAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs),
                                 std::move(diagonal)};
}

ScalarField solveRelaxedScalarTransport(const Mesh& mesh, const ScalarField& phi,
                                        const SurfaceField& massFlux,
                                        const ScalarField& diffusivity,
                                        const BoundaryConditionSet& boundaries,
                                        const ScalarField& Su, const ScalarField& Sp, Real alpha,
                                        Real floorValue,
                                        const cfd::algebra::LinearSolverSettings& solverSettings) {
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleScalarDiffusionContribution(mesh, diffusivity, phi, boundaries, builder, rhs);
  // specificHeat = 1.0: see this file's own header comment on why this
  // reuse is exact for a bare (non-cp-weighted) transported scalar.
  cfd::thermal::assembleThermalConvectionContribution(mesh, /*specificHeat=*/1.0, massFlux, phi,
                                                      boundaries, builder, rhs);
  applyImplicitScalarSource(mesh, builder, rhs, Su, Sp);

  const Vector phiOld = toVector(phi);
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, phiOld, alpha);

  SparseMatrix matrix = builder.build();
  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("solveRelaxedScalarTransport: assembled system is non-finite");
  }

  const cfd::algebra::BiCGSTAB solver(solverSettings);
  const auto result = solver.solve(cfd::algebra::LinearSystem(std::move(matrix), rhs), phiOld);
  if (!result.converged()) {
    throw NumericalError("solveRelaxedScalarTransport: linear solve did not converge");
  }

  ScalarField solved = toScalarField(result.solution);
  if (!allFinite(solved)) {
    throw NumericalError("solveRelaxedScalarTransport: solution is non-finite");
  }
  for (Index i = 0; i < n; ++i) {
    solved[i] = std::max(solved[i], floorValue);
  }
  return solved;
}

}  // namespace cfd::turbulence
