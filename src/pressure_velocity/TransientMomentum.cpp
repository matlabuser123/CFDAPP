#include "cfd/pressure_velocity/TransientMomentum.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/TimeDerivative.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::implicitEulerTimeDerivative;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::assembleConvectionContribution;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::assemblePressureSourceContribution;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

void applyTransientTerm(SparseMatrixBuilder& builder, Vector& rhs,
                        const Vector& diagonalContribution, const Vector& sourceContribution) {
  const Index n = rhs.size();
  if (diagonalContribution.size() != n || sourceContribution.size() != n) {
    throw InvalidArgumentError(
        "applyTransientTerm: diagonalContribution/sourceContribution size does not match rhs");
  }
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, diagonalContribution[i]);
    rhs[i] += sourceContribution[i];
  }
}

MomentumAssembly assembleTransientMomentumComponent(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& pressure,
    const SurfaceField& massFlux, const FluidProperties& fluid,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& pressureBoundaries,
    VelocityComponent component, const ScalarField& previousComponentValue, Real dt) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n || pressure.size() != n) {
    throw InvalidArgumentError(
        "assembleTransientMomentumComponent: velocity/pressure size does not match mesh cell "
        "count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleTransientMomentumComponent: massFlux size does not match mesh face count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("assembleTransientMomentumComponent: dt must be finite and > 0");
  }

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleDiffusionContribution(mesh, fluid.dynamicViscosity(), velocity, velocityBoundaries,
                                component, builder, rhs);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, builder,
                                 rhs);
  assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs);

  // implicitEulerTimeDerivative already validates previousComponentValue's
  // size against mesh.numberOfCells() and dt's finiteness/positivity, so
  // there is nothing further to check here before converting its
  // ScalarField output to the algebra::Vector form applyTransientTerm
  // works in.
  const auto timeCoefficients =
      implicitEulerTimeDerivative(mesh, previousComponentValue, fluid.density(), dt);
  Vector diagonalContribution(n);
  Vector sourceContribution(n);
  for (Index i = 0; i < n; ++i) {
    diagonalContribution[i] = timeCoefficients.diagonal[i];
    sourceContribution[i] = timeCoefficients.source[i];
  }
  applyTransientTerm(builder, rhs, diagonalContribution, sourceContribution);

  SparseMatrix matrix = builder.build();
  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleTransientMomentumComponent: assembled system contains a non-finite value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::pressure_velocity
