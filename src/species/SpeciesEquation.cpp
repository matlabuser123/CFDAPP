#include "cfd/species/SpeciesEquation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::species {

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
using cfd::physics::FluidProperties;

namespace {

// Same evaluate-at-current-state pattern as
// thermal::EnergyEquation.cpp's boundaryTemperatureValue() and
// physics::MomentumEquation.cpp's boundaryVelocity().
Real boundaryConcentrationValue(const Mesh& mesh, const Face& face,
                                const ScalarField& concentration,
                                const BoundaryConditionSet& concentrationBoundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), concentrationBoundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("SpeciesEquation: boundary condition is not scalar-valued");
  }
  const Real ownerValue = concentration[face.owner()];
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  return scalarBc->boundaryValue(ownerValue, distance);
}

}  // namespace

void assembleSpeciesDiffusionContribution(const Mesh& mesh, Real diffusionCoefficient,
                                          const ScalarField& concentration,
                                          const BoundaryConditionSet& concentrationBoundaries,
                                          SparseMatrixBuilder& builder, Vector& rhs) {
  if (concentration.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleSpeciesDiffusionContribution: concentration size does not match mesh cell count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real conductance = diffusionCoefficient * face.area() / distance;

      const Real yB =
          boundaryConcentrationValue(mesh, face, concentration, concentrationBoundaries);

      builder.add(ownerId, ownerId, conductance);
      rhs[ownerId] += conductance * yB;
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real conductance = diffusionCoefficient * face.area() / dPN;

    builder.add(ownerId, ownerId, conductance);
    builder.add(ownerId, neighborId, -conductance);
    builder.add(neighborId, neighborId, conductance);
    builder.add(neighborId, ownerId, -conductance);
  }
}

void assembleSpeciesConvectionContribution(const Mesh& mesh, const SurfaceField& massFlux,
                                           const ScalarField& concentration,
                                           const BoundaryConditionSet& concentrationBoundaries,
                                           SparseMatrixBuilder& builder, Vector& rhs) {
  if (concentration.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleSpeciesConvectionContribution: concentration size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleSpeciesConvectionContribution: massFlux size does not match mesh face count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real ownerFlux = massFlux[faceId];  // owner-oriented, already rho-weighted.

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      if (ownerFlux >= 0.0) {
        // Outflow: upwind value is the (unknown) owner value itself.
        builder.add(ownerId, ownerId, ownerFlux);
      } else {
        // Inflow: upwind value is the (known) boundary value -> RHS.
        const Real yB =
            boundaryConcentrationValue(mesh, face, concentration, concentrationBoundaries);
        rhs[ownerId] -= ownerFlux * yB;
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

void assembleSpeciesSourceContribution(const Mesh& mesh, Real volumetricSource, Vector& rhs) {
  if (!std::isfinite(volumetricSource)) {
    throw InvalidArgumentError(
        "assembleSpeciesSourceContribution: volumetricSource must be finite");
  }
  for (const auto& cell : mesh.cells()) {
    rhs[cell.id()] += volumetricSource * cell.volume();
  }
}

SpeciesAssembly assembleSpeciesTransportEquation(
    const Mesh& mesh, const ScalarField& concentration, const SurfaceField& massFlux,
    const FluidProperties& fluid, const SpeciesProperties& species,
    const BoundaryConditionSet& concentrationBoundaries, Real volumetricSource) {
  if (concentration.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleSpeciesTransportEquation: concentration size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleSpeciesTransportEquation: massFlux size does not match mesh face count");
  }

  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  const Real diffusionCoefficient = fluid.density() * species.diffusivity();
  assembleSpeciesDiffusionContribution(mesh, diffusionCoefficient, concentration,
                                       concentrationBoundaries, builder, rhs);
  assembleSpeciesConvectionContribution(mesh, massFlux, concentration, concentrationBoundaries,
                                        builder, rhs);
  assembleSpeciesSourceContribution(mesh, volumetricSource, rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleSpeciesTransportEquation: assembled system contains a non-finite value");
  }

  return SpeciesAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

ConcentrationBounds concentrationBounds(const ScalarField& concentration) {
  ConcentrationBounds bounds{std::numeric_limits<Real>::infinity(),
                             -std::numeric_limits<Real>::infinity()};
  for (Index i = 0; i < concentration.size(); ++i) {
    bounds.minimum = std::min(bounds.minimum, concentration[i]);
    bounds.maximum = std::max(bounds.maximum, concentration[i]);
  }
  return bounds;
}

}  // namespace cfd::species
