#include "cfd/physics/MomentumEquation.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::physics {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::VectorBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

Real selectComponent(const Vector2& v, VelocityComponent component) {
  return (component == VelocityComponent::U) ? v.x : v.y;
}

// Evaluates the boundary velocity for `face` (must be a boundary face)
// against the *current* owner value -- exact for Wall/MovingWall/Inlet
// (which ignore ownerValue), and a lagged/linearized approximation for
// Outlet/Symmetry (which depend on it). This is the same evaluate-at-
// current-state pattern cfd::discretization::interpolateFace already
// uses; matrix assembly cannot do better without turning Outlet/Symmetry
// into an implicit relation, which is out of scope for this phase.
Vector2 boundaryVelocity(const Mesh& mesh, const Face& face, const VectorField& velocity,
                         const BoundaryConditionSet& velocityBoundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), velocityBoundaries);
  const auto* vectorBc = dynamic_cast<const VectorBoundaryCondition*>(&bc);
  if (vectorBc == nullptr) {
    throw InvalidArgumentError("MomentumEquation: boundary condition is not vector-valued");
  }
  const Vector2& ownerValue = velocity[face.owner()];
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Vector2 unitNormal = MeshGeometry::unitNormal(face);
  return vectorBc->boundaryValue(ownerValue, distance, unitNormal);
}

}  // namespace

void assembleDiffusionContribution(const Mesh& mesh, Real dynamicViscosity,
                                   const VectorField& velocity,
                                   const BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component, SparseMatrixBuilder& builder,
                                   Vector& rhs) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: velocity size does not match mesh cell count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real diffusionCoefficient = dynamicViscosity * face.area() / distance;

      const Vector2 uB = boundaryVelocity(mesh, face, velocity, velocityBoundaries);
      const Real phiB = selectComponent(uB, component);

      // A(P,P) += Df; the -Df*phiB (known) part of the boundary flux
      // moves to the RHS as +Df*phiB (see TODO.md section 10/26 sign
      // derivation in the physics assembly comment above).
      builder.add(ownerId, ownerId, diffusionCoefficient);
      rhs[ownerId] += diffusionCoefficient * phiB;
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real diffusionCoefficient = dynamicViscosity * face.area() / dPN;

    // Face-once assembly: both rows' equal/opposite contributions are
    // added right here, from a single face visit (TODO.md section 51).
    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void assembleDiffusionContribution(const Mesh& mesh, const ScalarField& effectiveViscosity,
                                   const VectorField& velocity,
                                   const BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component, SparseMatrixBuilder& builder,
                                   Vector& rhs) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: velocity size does not match mesh cell count");
  }
  if (effectiveViscosity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: effectiveViscosity size does not match mesh cell count");
  }
  // A diffusion coefficient built from a non-finite or non-positive
  // mu_eff would either corrupt the assembled system with a NaN/Inf (only
  // sometimes caught downstream by matrix.allFinite()) or silently flip
  // the sign of a diffusion term for a negative-but-finite value (never
  // caught downstream at all) -- reject it here, at the one point mu_eff
  // enters momentum assembly, the same "validate at the entry point"
  // convention FluidProperties's own constructor already applies to
  // molecular viscosity.
  for (Index i = 0; i < effectiveViscosity.size(); ++i) {
    if (!std::isfinite(effectiveViscosity[i]) || !(effectiveViscosity[i] > 0.0)) {
      throw InvalidArgumentError(
          "assembleDiffusionContribution: effectiveViscosity must be finite and > 0 in every "
          "cell");
    }
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      // No neighbor cell to interpolate against at a boundary face -- use
      // the owner cell's own effective viscosity (see this overload's
      // header comment).
      const Real muFace = effectiveViscosity[ownerId];
      const Real diffusionCoefficient = muFace * face.area() / distance;

      const Vector2 uB = boundaryVelocity(mesh, face, velocity, velocityBoundaries);
      const Real phiB = selectComponent(uB, component);

      builder.add(ownerId, ownerId, diffusionCoefficient);
      rhs[ownerId] += diffusionCoefficient * phiB;
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real muFace =
        cfd::discretization::interpolateInternalFace(mesh, face, effectiveViscosity);
    const Real diffusionCoefficient = muFace * face.area() / dPN;

    // Face-once assembly: both rows' equal/opposite contributions are
    // added right here, from a single face visit (TODO.md section 51).
    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void assembleConvectionContribution(const Mesh& mesh, const SurfaceField& massFlux,
                                    const VectorField& velocity,
                                    const BoundaryConditionSet& velocityBoundaries,
                                    VelocityComponent component, SparseMatrixBuilder& builder,
                                    Vector& rhs) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleConvectionContribution: velocity size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleConvectionContribution: massFlux size does not match mesh face count");
  }

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real ownerFlux = massFlux[faceId];  // owner-oriented: >=0 means owner -> neighbor/out.

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      if (ownerFlux >= 0.0) {
        // Outflow: upwind value is the (unknown) owner value itself.
        builder.add(ownerId, ownerId, ownerFlux);
      } else {
        // Inflow: upwind value is the (known) boundary value -> RHS.
        const Vector2 uB = boundaryVelocity(mesh, face, velocity, velocityBoundaries);
        const Real phiB = selectComponent(uB, component);
        rhs[ownerId] -= ownerFlux * phiB;
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    // Single upwind decision per face, reused for both rows (matches
    // cfd::discretization::convection's face-once evaluation).
    if (ownerFlux >= 0.0) {
      builder.add(ownerId, ownerId, ownerFlux);
      builder.add(neighborId, ownerId, -ownerFlux);
    } else {
      builder.add(ownerId, neighborId, ownerFlux);
      builder.add(neighborId, neighborId, -ownerFlux);
    }
  }
}

void assemblePressureSourceContribution(const Mesh& mesh, const ScalarField& pressure,
                                        const BoundaryConditionSet& pressureBoundaries,
                                        VelocityComponent component, Vector& rhs) {
  if (pressure.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assemblePressureSourceContribution: pressure size does not match mesh cell count");
  }

  const VectorField gradP = cfd::discretization::gradient(mesh, pressure, pressureBoundaries);
  for (const auto& cell : mesh.cells()) {
    const Real gradComponent = selectComponent(gradP[cell.id()], component);
    rhs[cell.id()] += -cell.volume() * gradComponent;
  }
}

void assembleBuoyancySourceContribution(const Mesh& mesh, const ScalarField& temperature,
                                        const BoussinesqBuoyancy& buoyancy,
                                        VelocityComponent component, Vector& rhs) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleBuoyancySourceContribution: temperature size does not match mesh cell count");
  }
  for (const auto& cell : mesh.cells()) {
    const Vector2 sourcePerVolume = buoyancy.source(temperature[cell.id()]);
    rhs[cell.id()] += cell.volume() * selectComponent(sourcePerVolume, component);
  }
}

namespace {

MomentumAssembly assembleComponent(const Mesh& mesh, const VectorField& velocity,
                                   const ScalarField& pressure, const SurfaceField& massFlux,
                                   const FluidProperties& fluid,
                                   const BoundaryConditionSet& velocityBoundaries,
                                   const BoundaryConditionSet& pressureBoundaries,
                                   VelocityComponent component) {
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleDiffusionContribution(mesh, fluid.dynamicViscosity(), velocity, velocityBoundaries,
                                component, builder, rhs);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, builder,
                                 rhs);
  assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("assembleMomentum: assembled system contains a non-finite value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace

MomentumSystems assembleMomentum(const Mesh& mesh, const VectorField& velocity,
                                 const ScalarField& pressure, const SurfaceField& massFlux,
                                 const FluidProperties& fluid,
                                 const BoundaryConditionSet& velocityBoundaries,
                                 const BoundaryConditionSet& pressureBoundaries) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("assembleMomentum: velocity size does not match mesh cell count");
  }
  if (pressure.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("assembleMomentum: pressure size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("assembleMomentum: massFlux size does not match mesh face count");
  }

  MomentumAssembly u =
      assembleComponent(mesh, velocity, pressure, massFlux, fluid, velocityBoundaries,
                        pressureBoundaries, VelocityComponent::U);
  MomentumAssembly v =
      assembleComponent(mesh, velocity, pressure, massFlux, fluid, velocityBoundaries,
                        pressureBoundaries, VelocityComponent::V);
  return MomentumSystems{std::move(u), std::move(v)};
}

}  // namespace cfd::physics
