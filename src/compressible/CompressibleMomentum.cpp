#include "cfd/compressible/CompressibleMomentum.hpp"

#include <cmath>
#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::compressible {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

CompressibleTimeDerivativeCoefficients compressibleMomentumTimeDerivative(
    const Mesh& mesh, const ScalarField& velocityOldComponent, const ScalarField& densityOld,
    const ScalarField& densityNew, Real dt) {
  if (velocityOldComponent.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "compressibleMomentumTimeDerivative: velocityOldComponent size does not match mesh cell "
        "count");
  }
  if (densityOld.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "compressibleMomentumTimeDerivative: densityOld size does not match mesh cell count");
  }
  if (densityNew.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "compressibleMomentumTimeDerivative: densityNew size does not match mesh cell count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("compressibleMomentumTimeDerivative: dt must be finite and > 0");
  }

  CompressibleTimeDerivativeCoefficients result;
  result.diagonal = ScalarField(mesh.numberOfCells());
  result.source = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Real volumeOverDt = cell.volume() / dt;
    result.diagonal[id] = densityNew[id] * volumeOverDt;
    result.source[id] = densityOld[id] * volumeOverDt * velocityOldComponent[id];
  }
  return result;
}

MomentumAssembly assembleCompressibleMomentumComponent(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& velocityOldComponent,
    const ScalarField& pressure, const SurfaceField& massFlux, const ScalarField& densityOld,
    const ScalarField& densityNew, Real dynamicViscosity, const BoundaryConditionSet& velocityBoundaries,
    const BoundaryConditionSet& pressureBoundaries, VelocityComponent component, Real dt) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleCompressibleMomentumComponent: velocity size does not match mesh cell count");
  }
  if (pressure.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleCompressibleMomentumComponent: pressure size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleCompressibleMomentumComponent: massFlux size does not match mesh face count");
  }

  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  cfd::physics::assembleDiffusionContribution(mesh, dynamicViscosity, velocity, velocityBoundaries,
                                              component, builder, rhs);
  cfd::physics::assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries,
                                               component, builder, rhs);
  cfd::physics::assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component,
                                                   rhs);

  // compressibleMomentumTimeDerivative validates velocityOldComponent/
  // densityOld/densityNew/dt itself.
  const CompressibleTimeDerivativeCoefficients timeTerm =
      compressibleMomentumTimeDerivative(mesh, velocityOldComponent, densityOld, densityNew, dt);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, timeTerm.diagonal[i]);
    rhs[i] += timeTerm.source[i];
  }

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleCompressibleMomentumComponent: assembled system contains a non-finite value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace cfd::compressible
