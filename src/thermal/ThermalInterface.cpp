#include "cfd/thermal/ThermalInterface.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::thermal {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

void requirePositiveFinite(Real value, const char* what) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("interfaceConductance: ") + what +
                               " must be finite and > 0");
  }
}

// Same evaluate-at-current-state pattern as EnergyEquation.cpp's own
// (private, so not reusable directly) boundaryTemperatureValue -- every
// file in this codebase that needs this defines its own tiny copy rather
// than sharing one across translation units for a two-line helper (same
// convention as the SIMPLE.cpp/PISO.cpp/RestartSnapshot.cpp allFinite()
// precedent).
Real boundaryTemperatureValue(const Mesh& mesh, const Face& face, const ScalarField& temperature,
                              const BoundaryConditionSet& temperatureBoundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), temperatureBoundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("ThermalInterface: boundary condition is not scalar-valued");
  }
  const Real ownerValue = temperature[face.owner()];
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  return scalarBc->boundaryValue(ownerValue, distance);
}

}  // namespace

Real interfaceConductance(Real k1, Real d1, Real k2, Real d2, Real area) {
  requirePositiveFinite(k1, "k1");
  requirePositiveFinite(d1, "d1");
  requirePositiveFinite(k2, "k2");
  requirePositiveFinite(d2, "d2");
  requirePositiveFinite(area, "area");
  const Real resistance = (d1 / k1) + (d2 / k2);
  return area / resistance;
}

void assembleRegionAwareThermalDiffusionContribution(
    const Mesh& mesh, const ThermalRegionMap& regions, const ScalarField& temperature,
    const BoundaryConditionSet& temperatureBoundaries, SparseMatrixBuilder& builder, Vector& rhs) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleRegionAwareThermalDiffusionContribution: temperature size does not match mesh "
        "cell count");
  }
  if (regions.numberOfCells() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleRegionAwareThermalDiffusionContribution: regions size does not match mesh cell "
        "count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real conductivity = regions.regionForCell(ownerId).properties.conductivity();
      const Real diffusionCoefficient = conductivity * face.area() / distance;

      const Real tB = boundaryTemperatureValue(mesh, face, temperature, temperatureBoundaries);

      builder.add(ownerId, ownerId, diffusionCoefficient);
      rhs[ownerId] += diffusionCoefficient * tB;
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();

    Real diffusionCoefficient{};
    if (regions.sameRegion(ownerId, neighborId)) {
      // Identical to EnergyEquation's own single-material path -- see
      // this function's own header comment for why this is exactly the
      // k1==k2 limit of the interface formula below, not a separate rule.
      const Real conductivity = regions.regionForCell(ownerId).properties.conductivity();
      const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
      diffusionCoefficient = conductivity * face.area() / dPN;
    } else {
      const Real k1 = regions.regionForCell(ownerId).properties.conductivity();
      const Real k2 = regions.regionForCell(neighborId).properties.conductivity();
      const Real d1 = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real d2 = MeshGeometry::distance(face.centroid(), mesh.cell(neighborId).centroid());
      diffusionCoefficient = interfaceConductance(k1, d1, k2, d2, face.area());
    }

    // Face-once, conservative: the same coefficient is added to both
    // rows' equal/opposite entries, exactly like the single-material
    // path -- this is what makes heat-flux continuity structural (see
    // this file's own header comment) rather than an extra constraint
    // that could numerically drift from temperature continuity.
    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

EnergyAssembly assembleConjugateConductionEquation(
    const Mesh& mesh, const ScalarField& temperature, const ThermalRegionMap& regions,
    const BoundaryConditionSet& temperatureBoundaries, Real volumetricHeatSource) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleConjugateConductionEquation: temperature size does not match mesh cell count");
  }
  if (regions.numberOfCells() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleConjugateConductionEquation: regions size does not match mesh cell count");
  }

  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature, temperatureBoundaries,
                                                  builder, rhs);
  assembleThermalSourceContribution(mesh, volumetricHeatSource, rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleConjugateConductionEquation: assembled system contains a non-finite value");
  }

  return EnergyAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::thermal
