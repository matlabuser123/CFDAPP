// P3-PHYS-006 sections 13-14, 35: compressible momentum transient-
// storage term (hand-derived, and the mandatory constant-density-
// reduces-to-the-existing-formulation regression) plus the full
// component assembly.
#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/compressible/CompressibleMomentum.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::compressible::assembleCompressibleMomentumComponent;
using cfd::compressible::compressibleMomentumTimeDerivative;
using cfd::compressible::CompressibleTimeDerivativeCoefficients;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::assembleTransientMomentumComponent;

namespace {

BoundaryConditionSet makeConstantVelocityBoundaries(const Mesh& mesh, Vector2 velocity) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(velocity));
  }
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

ScalarField selectComponent(const VectorField& velocity, VelocityComponent component) {
  ScalarField result(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    result[i] = (component == VelocityComponent::U) ? velocity[i].x : velocity[i].y;
  }
  return result;
}

}  // namespace

// --- compressibleMomentumTimeDerivative -----------------------------------

TEST(CompressibleMomentumTimeDerivativeTest, HandDerivedDiagonalAndSource) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cell volume = 1 each.
  const Index n = mesh.numberOfCells();
  ScalarField velocityOld(n);
  velocityOld[0] = 2.0;
  velocityOld[1] = -1.0;
  ScalarField densityOld(n);
  densityOld[0] = 1.0;
  densityOld[1] = 1.2;
  ScalarField densityNew(n);
  densityNew[0] = 1.1;
  densityNew[1] = 1.3;
  const Real dt = 0.5;

  const CompressibleTimeDerivativeCoefficients result =
      compressibleMomentumTimeDerivative(mesh, velocityOld, densityOld, densityNew, dt);

  // diagonal = rho_new*V/dt.
  EXPECT_NEAR(result.diagonal[0], 1.1 * 1.0 / 0.5, 1e-12);
  EXPECT_NEAR(result.diagonal[1], 1.3 * 1.0 / 0.5, 1e-12);
  // source = rho_old*V/dt*u_old.
  EXPECT_NEAR(result.source[0], 1.0 * 1.0 / 0.5 * 2.0, 1e-12);
  EXPECT_NEAR(result.source[1], 1.2 * 1.0 / 0.5 * (-1.0), 1e-12);
}

TEST(CompressibleMomentumTimeDerivativeTest, ConstantDensityMatchesIncompressibleFormula) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  ScalarField velocityOld(n, 2.5);
  const Real rho = 1.2;
  const ScalarField densityOld(n, rho);
  const ScalarField densityNew(n, rho);
  const Real dt = 0.02;

  const CompressibleTimeDerivativeCoefficients result =
      compressibleMomentumTimeDerivative(mesh, velocityOld, densityOld, densityNew, dt);

  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Real expectedDiagonal = rho * cell.volume() / dt;
    const Real expectedSource = expectedDiagonal * velocityOld[id];
    EXPECT_NEAR(result.diagonal[id], expectedDiagonal, 1e-9) << "cell " << id;
    EXPECT_NEAR(result.source[id], expectedSource, 1e-9) << "cell " << id;
  }
}

TEST(CompressibleMomentumTimeDerivativeTest, MismatchedSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField velocityOld(mesh.numberOfCells() + 1, 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  EXPECT_THROW((void)compressibleMomentumTimeDerivative(mesh, velocityOld, density, density, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleMomentumTimeDerivativeTest, RejectsNonPositiveDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField velocityOld(mesh.numberOfCells(), 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  EXPECT_THROW((void)compressibleMomentumTimeDerivative(mesh, velocityOld, density, density, 0.0),
               InvalidArgumentError);
}

// --- assembleCompressibleMomentumComponent (section 35) --------------------

TEST(CompressibleMomentumComponentTest, ConstantDensityReproducesTransientMomentumExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.5, -0.2});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  VectorField velocity(n, Vector2{0.3, 0.1});
  ScalarField pressure(n, 0.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.15;
  }
  const FluidProperties fluid(1.3, 0.02);
  const Real dt = 0.05;
  const VelocityComponent component = VelocityComponent::U;

  const ScalarField previousU = selectComponent(velocity, component);
  const MomentumAssembly reference = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, component,
      previousU, dt);

  const ScalarField densityField(n, fluid.density());
  const MomentumAssembly compressible = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, densityField, densityField,
      fluid.dynamicViscosity(), velocityBoundaries, pressureBoundaries, component, dt);

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(compressible.diagonal[row], reference.diagonal[row], 1e-9) << "row " << row;
    EXPECT_NEAR(compressible.system.rhs()[row], reference.system.rhs()[row], 1e-9) << "row " << row;
  }
}

TEST(CompressibleMomentumComponentTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);

  EXPECT_THROW((void)assembleCompressibleMomentumComponent(
                   mesh, velocity, previousU, pressure, massFlux, density, density, 0.01,
                   velocityBoundaries, pressureBoundaries, VelocityComponent::U, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleMomentumComponentTest, NonUniformDensityChangesTheAssembledDiagonal) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});
  const ScalarField previousU(n, 0.0);
  const ScalarField pressure(n, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const Real dt = 0.01;

  const ScalarField uniformDensity(n, 1.0);
  const MomentumAssembly baseline = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, uniformDensity, uniformDensity, 0.02,
      velocityBoundaries, pressureBoundaries, VelocityComponent::U, dt);

  ScalarField perturbedDensity(n, 1.0);
  perturbedDensity[0] = 5.0;  // cell 0's rho_new only.
  const MomentumAssembly perturbed = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, uniformDensity, perturbedDensity, 0.02,
      velocityBoundaries, pressureBoundaries, VelocityComponent::U, dt);

  EXPECT_GT(perturbed.diagonal[0], baseline.diagonal[0]);
  // A cell sharing no face with cell 0 and with unchanged density must
  // have an unchanged diagonal.
  EXPECT_DOUBLE_EQ(perturbed.diagonal[8], baseline.diagonal[8]);
}
