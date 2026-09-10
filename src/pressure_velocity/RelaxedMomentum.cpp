#include "cfd/pressure_velocity/RelaxedMomentum.hpp"

#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::assembleBuoyancySourceContribution;
using cfd::physics::assembleConvectionContribution;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::assemblePressureSourceContribution;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

MomentumAssembly assembleRelaxedMomentumComponent(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& pressure,
    const SurfaceField& massFlux, const ScalarField& effectiveViscosity,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& pressureBoundaries,
    VelocityComponent component, const ScalarField& previousComponentValue, Real alpha,
    const ScalarField* temperature, const BoussinesqBuoyancy* buoyancy) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n || pressure.size() != n) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: velocity/pressure size does not match mesh cell "
        "count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: massFlux size does not match mesh face count");
  }
  if (effectiveViscosity.size() != n) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: effectiveViscosity size does not match mesh cell "
        "count");
  }
  if ((temperature == nullptr) != (buoyancy == nullptr)) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: temperature and buoyancy must be both null or both "
        "non-null");
  }

  SparseMatrixBuilder builder(n, n);
  // P4 -- Performance: reserve for a 2D 5-point-stencil upper bound (each
  // internal face contributes up to 4 triplets between diffusion+
  // convection, each boundary face up to 2 -- 5*n comfortably covers a
  // structured Cartesian mesh's actual triplet count without ever being a
  // hard limit; add() still works correctly past it, just without the
  // reservation's benefit). This is SIMPLE's own per-outer-iteration
  // momentum assembly, the measured hotspot -- see TODO.md's own P4
  // status note for the before/after benchmark evidence.
  builder.reserve(5 * n);
  Vector rhs(n, 0.0);

  assembleDiffusionContribution(mesh, effectiveViscosity, velocity, velocityBoundaries, component,
                                builder, rhs);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, builder,
                                 rhs);
  assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs);
  if (buoyancy != nullptr) {
    assembleBuoyancySourceContribution(mesh, *temperature, *buoyancy, component, rhs);
  }

  Vector previousComponentVector(n);
  for (Index i = 0; i < n; ++i) {
    previousComponentVector[i] = previousComponentValue[i];
  }
  applyImplicitUnderRelaxation(builder, rhs, previousComponentVector, alpha);

  SparseMatrix matrix = builder.build();
  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleRelaxedMomentumComponent: assembled system contains a non-finite value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::pressure_velocity
