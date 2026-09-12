#include "cfd/compressible/CompressibleRelaxedMomentum.hpp"

#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/compressible/CompressibleMomentum.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"

namespace cfd::compressible {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::assembleConvectionContribution;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::assemblePressureSourceContribution;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::applyImplicitUnderRelaxation;

MomentumAssembly assembleRelaxedCompressibleMomentumComponent(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& pressure,
    const SurfaceField& massFlux, const ScalarField& densityOld, const ScalarField& densityNew,
    Real dynamicViscosity, const BoundaryConditionSet& velocityBoundaries,
    const BoundaryConditionSet& pressureBoundaries, VelocityComponent component,
    const ScalarField& previousComponentValue, Real alpha, Real pseudoTimeStep) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n || pressure.size() != n) {
    throw InvalidArgumentError(
        "assembleRelaxedCompressibleMomentumComponent: velocity/pressure size does not match "
        "mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleRelaxedCompressibleMomentumComponent: massFlux size does not match mesh face "
        "count");
  }

  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  Vector rhs(n, 0.0);

  // Same three contributions assembleRelaxedMomentumComponent uses,
  // called identically -- no compressible-specific diffusion/convection/
  // pressure-source physics exists or is needed (the existing assemblers
  // already accept an arbitrary massFlux/SurfaceField regardless of how
  // it was computed -- P3-PHYS-006's own "no generalization was even
  // needed there" precedent, see CompressibleMomentum.hpp's own header
  // comment).
  assembleDiffusionContribution(mesh, dynamicViscosity, velocity, velocityBoundaries, component,
                                builder, rhs);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, builder,
                                 rhs);
  assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs);

  // compressibleMomentumTimeDerivative validates velocityOldComponent/
  // densityOld/densityNew/pseudoTimeStep itself.
  const CompressibleTimeDerivativeCoefficients timeTerm = compressibleMomentumTimeDerivative(
      mesh, previousComponentValue, densityOld, densityNew, pseudoTimeStep);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, timeTerm.diagonal[i]);
    rhs[i] += timeTerm.source[i];
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
        "assembleRelaxedCompressibleMomentumComponent: assembled system contains a non-finite "
        "value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::compressible
