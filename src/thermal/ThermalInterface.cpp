#include "cfd/thermal/ThermalInterface.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

namespace cfd::thermal {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
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

  // P12-DIFF-002: the cell gradient the Dirichlet wall reconstruction needs for its tangential
  // transfer term (exactly zero on an orthogonal face). Built once per assembly, and always --
  // which wall-flux scheme is used must not depend on any iterative control. The conjugate path
  // itself remains uncorrected on INTERNAL faces, unchanged.
  const cfd::fields::VectorField gradT =
      thermalBoundaryCorrectionGradient(mesh, temperature, temperatureBoundaries);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      // Exactly the single-material assembly's boundary treatment, through the ONE shared
      // implementation (EnergyEquation's assembleThermalBoundaryFaceContribution): the P12-DIFF-002
      // second-order one-sided Dirichlet reconstruction where a valid inward stencil exists, the
      // historical two-point fallback where none does, and the exact prescribed flux for
      // gradient-type conditions.
      //
      // This branch used to compute `conductivity * face.area() / distance` itself and pass only
      // that coefficient, which selected the pre-DIFF-002 two-point wall flux. When P12-DIFF-002 A2
      // made the single-material wall flux unconditionally second order, this copy silently stopped
      // matching it: a SINGLE-region conjugate solve differed from the equivalent single-material
      // solve by up to 3.02 K (results/p12-diff-002/thermal-interface-fix/acceptance_gate.md
      // section 1). Sharing the implementation is what stops that recurring -- do not reintroduce a
      // local boundary-coefficient formula here.
      //
      // The owner cell's own conductivity is the right one: a boundary face has no neighbour to
      // interpolate against, and for a face of a multi-region mesh the wall is in contact with the
      // owner's material only.
      assembleThermalBoundaryFaceContribution(
          mesh, face, regions.regionForCell(face.owner()).properties.conductivity(), temperature,
          temperatureBoundaries, gradT, builder, rhs);
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
    diagonal[row] = storedDiagonalOrZero(matrix, row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleConjugateConductionEquation: assembled system contains a non-finite value");
  }

  return EnergyAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::thermal
