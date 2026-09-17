#include "cfd/thermal/EnergyEquation.hpp"

#include <cmath>
#include <optional>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::thermal {

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

// Evaluates the boundary temperature for `face` (must be a boundary face)
// against the *current* owner value -- exact for FixedValue (which
// ignores ownerValue), reconstructed from it for FixedGradient. Same
// evaluate-at-current-state pattern as
// physics::MomentumEquation.cpp's boundaryVelocity() and
// discretization::interpolateFace.
Real boundaryTemperatureValue(const Mesh& mesh, const Face& face, const ScalarField& temperature,
                              const BoundaryConditionSet& temperatureBoundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), temperatureBoundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("EnergyEquation: boundary condition is not scalar-valued");
  }
  const Real ownerValue = temperature[face.owner()];
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  return scalarBc->boundaryValue(ownerValue, distance);
}

// P12-NUM-003: grad(T) for the explicit non-orthogonal term, computed once
// per assembly and only when the correction is enabled.
// P12-DIFF-002 A2: the Dirichlet wall-flux reconstruction needs a cell gradient for its tangential
// transfer term, and WHICH wall-flux scheme is used must not depend on `options.enabled` -- that
// flag controls the iterative/deferred non-orthogonal correction, a separate concept (see
// results/p12-diff-002/a2/activation_architecture.md). The gradient is therefore always computed,
// and `options.enabled` now gates only the INTERNAL-face correction. Before A2 this returned
// nullopt when disabled, which silently made the Dirichlet wall flux first order for every case
// leaving solver.json's `non_orthogonal_corrections` at its default of 0 -- including every
// orthogonal case, where P12-DIFF-001 measured that error as exactly 0.5 h.
cfd::fields::VectorField correctionGradient(
    const Mesh& mesh, const ScalarField& temperature,
    const BoundaryConditionSet& temperatureBoundaries,
    const cfd::discretization::NonOrthogonalCorrectionOptions& options) {
  return thermalBoundaryCorrectionGradient(mesh, temperature, temperatureBoundaries,
                                           options.gradientScheme);
}

bool prescribesTemperature(const Mesh& mesh, const Face& face,
                           const BoundaryConditionSet& temperatureBoundaries) {
  return cfd::discretization::prescribesBoundaryValue(
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), temperatureBoundaries).type());
}

}  // namespace

Real storedDiagonalOrZero(const SparseMatrix& matrix, Index row) {
  const Index* offsets = matrix.rowOffsetsData();
  const Index* columns = matrix.columnIndicesData();
  const Real* values = matrix.valuesData();
  for (Index k = offsets[row]; k < offsets[row + 1]; ++k) {
    if (columns[k] == row) return values[k];
  }
  return 0.0;
}

BoundaryDiffusionContribution boundaryDiffusionContribution(
    const Mesh& mesh, const Face& face, const ScalarField& temperature,
    const BoundaryConditionSet& temperatureBoundaries, Real coefficient,
    std::optional<Real> boundaryValueCoefficient) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), temperatureBoundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("EnergyEquation: boundary condition is not scalar-valued");
  }
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  if (cfd::discretization::prescribesBoundaryValue(bc.type())) {
    // Unchanged: T_b is known -- exactly the pre-existing A(P,P) += Df,
    // rhs += Df * T_b.
    // P12-DIFF-002: the prescribed value's multiplier may differ from the diagonal.
    return {coefficient, boundaryValueCoefficient.value_or(coefficient) *
                             scalarBc->boundaryValue(temperature[face.owner()], distance)};
  }
  // T_b = T_P + g d: the flow Df (T_P - T_b) = -Df g d is known exactly
  // (boundaryValue(0, d) = g d); nothing depends on T_P.
  return {0.0, coefficient * scalarBc->boundaryValue(0.0, distance)};
}

cfd::fields::VectorField thermalBoundaryCorrectionGradient(
    const Mesh& mesh, const ScalarField& temperature,
    const BoundaryConditionSet& temperatureBoundaries,
    cfd::discretization::GradientScheme gradientScheme) {
  return cfd::discretization::gradient(mesh, temperature, temperatureBoundaries, gradientScheme);
}

void assembleThermalBoundaryFaceContribution(const Mesh& mesh, const Face& face, Real conductivity,
                                             const ScalarField& temperature,
                                             const BoundaryConditionSet& temperatureBoundaries,
                                             const cfd::fields::VectorField& gradT,
                                             SparseMatrixBuilder& builder, Vector& rhs) {
  const Index ownerId = face.owner();
  const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
  const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
      mesh, face, conductivity, distance, &gradT,
      prescribesTemperature(mesh, face, temperatureBoundaries));

  // Always applied -- the transfer term is exactly 0 on an orthogonal face.
  rhs[ownerId] += terms.explicitFlux;

  // Prescribed T_b: A(P,P) += Df, the -Df*tB (known) part of the boundary flux
  // moves to the RHS as +Df*tB -- same derivation as
  // MomentumEquation::assembleDiffusionContribution's boundary branch, with k in
  // place of mu and T in place of u. Gradient-type conditions: the prescribed
  // flux, exactly (P12-MESH-003 fix; see boundaryDiffusionContribution).
  const auto boundary =
      boundaryDiffusionContribution(mesh, face, temperature, temperatureBoundaries,
                                    terms.coefficient, terms.boundaryValueCoefficient);
  builder.add(ownerId, ownerId, boundary.diagonal);
  rhs[ownerId] += boundary.source;
  // P12-DIFF-002: one implicit far-cell entry when the reconstruction applies
  // (only for value-prescribing faces, where boundary.diagonal != 0).
  if (terms.farCellCoefficient != 0.0 && boundary.diagonal != 0.0) {
    builder.add(ownerId, terms.farCell, -terms.farCellCoefficient);
  }
}

void assembleThermalDiffusionContribution(
    const Mesh& mesh, Real conductivity, const ScalarField& temperature,
    const BoundaryConditionSet& temperatureBoundaries, SparseMatrixBuilder& builder, Vector& rhs,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalDiffusionContribution: temperature size does not match mesh cell count");
  }
  const auto gradT = correctionGradient(mesh, temperature, temperatureBoundaries, nonOrthogonal);
  // A2: boundary faces always get the gradient (the wall-flux SCHEME must not depend on the
  // iterative control); internal faces keep the pre-existing gate, so `enabled` still means
  // "apply the non-orthogonal correction".
  const cfd::fields::VectorField* internalGradT = nonOrthogonal.enabled ? &gradT : nullptr;

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      // One shared implementation, also used by the region-aware conjugate assembly.
      assembleThermalBoundaryFaceContribution(mesh, face, conductivity, temperature,
                                              temperatureBoundaries, gradT, builder, rhs);
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const auto terms = cfd::discretization::internalFaceDiffusionTerms(mesh, face, conductivity,
                                                                       dPN, internalGradT);
    const Real diffusionCoefficient = terms.coefficient;
    if (internalGradT != nullptr) {
      rhs[ownerId] += terms.explicitFlux;
      rhs[neighborId] -= terms.explicitFlux;
    }

    // Face-once assembly: both rows' equal/opposite contributions are
    // added right here, from a single face visit (same convention as
    // MomentumEquation).
    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void assembleThermalDiffusionContribution(
    const Mesh& mesh, const ScalarField& conductivity, const ScalarField& temperature,
    const BoundaryConditionSet& temperatureBoundaries, SparseMatrixBuilder& builder, Vector& rhs,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalDiffusionContribution: temperature size does not match mesh cell count");
  }
  if (conductivity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalDiffusionContribution: conductivity size does not match mesh cell count");
  }
  // Reject a non-finite or non-positive k(T) at the one point it enters
  // this assembly -- same "validate at the entry point" convention
  // MomentumEquation.cpp's field-based effectiveViscosity overload already
  // applies to mu_eff.
  for (Index i = 0; i < conductivity.size(); ++i) {
    if (!std::isfinite(conductivity[i]) || !(conductivity[i] > 0.0)) {
      throw InvalidArgumentError(
          "assembleThermalDiffusionContribution: conductivity must be finite and > 0 in every "
          "cell");
    }
  }

  const auto gradT = correctionGradient(mesh, temperature, temperatureBoundaries, nonOrthogonal);
  // A2: boundary faces always get the gradient (the wall-flux SCHEME must not depend on the
  // iterative control); internal faces keep the pre-existing gate, so `enabled` still means
  // "apply the non-orthogonal correction".
  const cfd::fields::VectorField* internalGradT = nonOrthogonal.enabled ? &gradT : nullptr;

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      // No neighbor cell to interpolate against at a boundary face -- use
      // the owner cell's own conductivity (see this overload's header
      // comment). One shared implementation, also used by the region-aware
      // conjugate assembly.
      assembleThermalBoundaryFaceContribution(mesh, face, conductivity[ownerId], temperature,
                                              temperatureBoundaries, gradT, builder, rhs);
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real kFace = cfd::discretization::interpolateInternalFace(mesh, face, conductivity);
    const auto terms =
        cfd::discretization::internalFaceDiffusionTerms(mesh, face, kFace, dPN, internalGradT);
    const Real diffusionCoefficient = terms.coefficient;
    if (internalGradT != nullptr) {
      rhs[ownerId] += terms.explicitFlux;
      rhs[neighborId] -= terms.explicitFlux;
    }

    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void assembleThermalConvectionContribution(const Mesh& mesh, Real specificHeat,
                                           const SurfaceField& massFlux,
                                           const ScalarField& temperature,
                                           const BoundaryConditionSet& temperatureBoundaries,
                                           SparseMatrixBuilder& builder, Vector& rhs) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalConvectionContribution: temperature size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleThermalConvectionContribution: massFlux size does not match mesh face count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real ownerFlux = massFlux[faceId];  // owner-oriented, already rho-weighted.
    // The transported quantity is cp*T (not bare T), so the mass flux is
    // scaled by specificHeat once here and reused for every matrix/RHS
    // contribution below -- see this file's header comment on why no
    // separate density multiply is needed.
    const Real effectiveFlux = specificHeat * ownerFlux;

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      if (ownerFlux >= 0.0) {
        // Outflow: upwind value is the (unknown) owner value itself.
        builder.add(ownerId, ownerId, effectiveFlux);
      } else {
        // Inflow: upwind value is the (known) boundary value -> RHS.
        const Real tB = boundaryTemperatureValue(mesh, face, temperature, temperatureBoundaries);
        rhs[ownerId] -= effectiveFlux * tB;
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    // Single upwind decision per face, reused for both rows (matches
    // MomentumEquation's face-once evaluation).
    if (ownerFlux >= 0.0) {
      builder.add(ownerId, ownerId, effectiveFlux);
      builder.add(neighborId, ownerId, -effectiveFlux);
    } else {
      builder.add(ownerId, neighborId, effectiveFlux);
      builder.add(neighborId, neighborId, -effectiveFlux);
    }
  }
}

void assembleThermalConvectionContribution(const Mesh& mesh, const ScalarField& specificHeat,
                                           const SurfaceField& massFlux,
                                           const ScalarField& temperature,
                                           const BoundaryConditionSet& temperatureBoundaries,
                                           SparseMatrixBuilder& builder, Vector& rhs) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalConvectionContribution: temperature size does not match mesh cell count");
  }
  if (specificHeat.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalConvectionContribution: specificHeat size does not match mesh cell "
        "count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleThermalConvectionContribution: massFlux size does not match mesh face count");
  }
  for (Index i = 0; i < specificHeat.size(); ++i) {
    if (!std::isfinite(specificHeat[i]) || !(specificHeat[i] > 0.0)) {
      throw InvalidArgumentError(
          "assembleThermalConvectionContribution: specificHeat must be finite and > 0 in every "
          "cell");
    }
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real ownerFlux = massFlux[faceId];  // owner-oriented, already rho-weighted.

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      if (ownerFlux >= 0.0) {
        // Outflow: upwind cell is the owner itself -- evaluate cp there
        // (see this overload's header comment on the upwind-cell, not
        // face-interpolated, convention for specificHeat).
        const Real effectiveFlux = specificHeat[ownerId] * ownerFlux;
        builder.add(ownerId, ownerId, effectiveFlux);
      } else {
        // Inflow: upwind "cell" is the boundary itself -- no cell-centered
        // cp exists there, so (matching how the scalar overload already
        // treats a uniform cp identically on both sides of a face) the
        // owner cell's cp is used, since it is the only cp value this
        // assembly has any basis for at this face.
        const Real effectiveFlux = specificHeat[ownerId] * ownerFlux;
        const Real tB = boundaryTemperatureValue(mesh, face, temperature, temperatureBoundaries);
        rhs[ownerId] -= effectiveFlux * tB;
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    if (ownerFlux >= 0.0) {
      // Owner -> neighbor: fluid leaving the owner cell carries the
      // owner's cp.
      const Real effectiveFlux = specificHeat[ownerId] * ownerFlux;
      builder.add(ownerId, ownerId, effectiveFlux);
      builder.add(neighborId, ownerId, -effectiveFlux);
    } else {
      // Neighbor -> owner: fluid leaving the neighbor cell carries the
      // neighbor's cp.
      const Real effectiveFlux = specificHeat[neighborId] * ownerFlux;
      builder.add(ownerId, neighborId, effectiveFlux);
      builder.add(neighborId, neighborId, -effectiveFlux);
    }
  }
}

void assembleThermalSourceContribution(const Mesh& mesh, Real volumetricHeatSource, Vector& rhs) {
  if (!std::isfinite(volumetricHeatSource)) {
    throw InvalidArgumentError(
        "assembleThermalSourceContribution: volumetricHeatSource must be finite");
  }
  for (const auto& cell : mesh.cells()) {
    rhs[cell.id()] += volumetricHeatSource * cell.volume();
  }
}

void assembleThermalSourceContribution(const Mesh& mesh, const ScalarField& volumetricHeatSource,
                                       Vector& rhs) {
  if (volumetricHeatSource.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleThermalSourceContribution: volumetricHeatSource size does not match mesh cell "
        "count");
  }
  for (const auto& cell : mesh.cells()) {
    const Real source = volumetricHeatSource[cell.id()];
    if (!std::isfinite(source)) {
      throw InvalidArgumentError(
          "assembleThermalSourceContribution: volumetricHeatSource must be finite");
    }
    rhs[cell.id()] += source * cell.volume();
  }
}

namespace {

// The constant-property energy assembly shared by the uniform-source and
// the per-cell-source (P12-NUM-006) overloads: diffusion, convection, then
// `addSource` -- the same contribution order as before the field overload
// existed, so the uniform-source result is unchanged.
template <typename AddSource>
EnergyAssembly assembleConstantPropertyEnergyEquation(
    const Mesh& mesh, const ScalarField& temperature, const SurfaceField& massFlux,
    const ThermalProperties& thermal, const BoundaryConditionSet& temperatureBoundaries,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal,
    const AddSource& addSource) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleEnergyEquation: temperature size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleEnergyEquation: massFlux size does not match mesh face count");
  }

  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleThermalDiffusionContribution(mesh, thermal.conductivity(), temperature,
                                       temperatureBoundaries, builder, rhs, nonOrthogonal);
  assembleThermalConvectionContribution(mesh, thermal.specificHeat(), massFlux, temperature,
                                        temperatureBoundaries, builder, rhs);
  addSource(rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = storedDiagonalOrZero(matrix, row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("assembleEnergyEquation: assembled system contains a non-finite value");
  }

  return EnergyAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace

EnergyAssembly assembleEnergyEquation(
    const Mesh& mesh, const ScalarField& temperature, const SurfaceField& massFlux,
    const ThermalProperties& thermal, const BoundaryConditionSet& temperatureBoundaries,
    Real volumetricHeatSource,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal) {
  return assembleConstantPropertyEnergyEquation(
      mesh, temperature, massFlux, thermal, temperatureBoundaries, nonOrthogonal,
      [&](Vector& rhs) { assembleThermalSourceContribution(mesh, volumetricHeatSource, rhs); });
}

EnergyAssembly assembleEnergyEquation(
    const Mesh& mesh, const ScalarField& temperature, const SurfaceField& massFlux,
    const ThermalProperties& thermal, const BoundaryConditionSet& temperatureBoundaries,
    const ScalarField& volumetricHeatSource,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal) {
  return assembleConstantPropertyEnergyEquation(
      mesh, temperature, massFlux, thermal, temperatureBoundaries, nonOrthogonal,
      [&](Vector& rhs) { assembleThermalSourceContribution(mesh, volumetricHeatSource, rhs); });
}

EnergyAssembly assembleEnergyEquation(
    const Mesh& mesh, const ScalarField& temperature, const SurfaceField& massFlux,
    const ScalarField& conductivity, const ScalarField& specificHeat,
    const BoundaryConditionSet& temperatureBoundaries, Real volumetricHeatSource,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleEnergyEquation: temperature size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleEnergyEquation: massFlux size does not match mesh face count");
  }

  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleThermalDiffusionContribution(mesh, conductivity, temperature, temperatureBoundaries,
                                       builder, rhs, nonOrthogonal);
  assembleThermalConvectionContribution(mesh, specificHeat, massFlux, temperature,
                                        temperatureBoundaries, builder, rhs);
  assembleThermalSourceContribution(mesh, volumetricHeatSource, rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = storedDiagonalOrZero(matrix, row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("assembleEnergyEquation: assembled system contains a non-finite value");
  }

  return EnergyAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::thermal
