// P12-COMP-002: assembleRelaxedCompressibleMomentumComponent -- the
// compressible analog of cfd::pressure_velocity::assembleRelaxedMomentumComponent,
// composed from three *existing, unmodified* pieces (diffusion/convection/
// pressure-source assemblers, compressibleMomentumTimeDerivative,
// applyImplicitUnderRelaxation). The mandatory reduction check here is
// exact, not tolerance-based: with alpha=1 (relaxation disabled), this
// function's assembly is bit-for-bit the same formula
// assembleCompressibleMomentumComponent already computes (this function
// just adds a relaxation step on top) -- so the two must agree exactly,
// not merely approximately.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/compressible/CompressibleMomentum.hpp"
#include "cfd/compressible/CompressibleRelaxedMomentum.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::compressible::assembleCompressibleMomentumComponent;
using cfd::compressible::assembleRelaxedCompressibleMomentumComponent;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

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

}  // namespace

TEST(CompressibleRelaxedMomentumTest, AlphaOneMatchesUnrelaxedCompressibleMomentumComponentExactly) {
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
  ScalarField densityOld(n, 1.1);
  ScalarField densityNew(n);
  for (Index i = 0; i < n; ++i) densityNew[i] = 1.1 + 0.01 * static_cast<Real>(i);  // non-uniform.
  const Real dynamicViscosity = 0.02;
  const Real pseudoTimeStep = 0.05;
  const VelocityComponent component = VelocityComponent::U;
  const ScalarField previousU(n, 0.3);

  const MomentumAssembly reference = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, densityOld, densityNew, dynamicViscosity,
      velocityBoundaries, pressureBoundaries, component, pseudoTimeStep);

  const MomentumAssembly relaxed = assembleRelaxedCompressibleMomentumComponent(
      mesh, velocity, pressure, massFlux, densityOld, densityNew, dynamicViscosity,
      velocityBoundaries, pressureBoundaries, component, previousU, /*alpha=*/1.0, pseudoTimeStep);

  for (Index row = 0; row < n; ++row) {
    EXPECT_DOUBLE_EQ(relaxed.diagonal[row], reference.diagonal[row]) << "row " << row;
    EXPECT_DOUBLE_EQ(relaxed.system.rhs()[row], reference.system.rhs()[row]) << "row " << row;
  }
}

// Patankar implicit under-relaxation's own hand-derivable effect (same
// formula UnderRelaxation.hpp documents): aP_relaxed = aP/alpha,
// b_relaxed = b + (1-alpha)/alpha * aP * phiOld -- checked here on top of
// the compressible assembly to confirm relaxation is genuinely applied,
// not just present as an unused parameter.
TEST(CompressibleRelaxedMomentumTest, RelaxationMatchesHandDerivedPatankarFormula) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.2, 0.0});
  const ScalarField pressure(n, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField density(n, 1.0);
  const Real dynamicViscosity = 0.01;
  const Real pseudoTimeStep = 1.0;
  const ScalarField previousU(n, 0.2);
  const Real alpha = 0.7;

  const MomentumAssembly unrelaxed = assembleRelaxedCompressibleMomentumComponent(
      mesh, velocity, pressure, massFlux, density, density, dynamicViscosity, velocityBoundaries,
      pressureBoundaries, VelocityComponent::U, previousU, /*alpha=*/1.0, pseudoTimeStep);
  const MomentumAssembly relaxed = assembleRelaxedCompressibleMomentumComponent(
      mesh, velocity, pressure, massFlux, density, density, dynamicViscosity, velocityBoundaries,
      pressureBoundaries, VelocityComponent::U, previousU, alpha, pseudoTimeStep);

  for (Index row = 0; row < n; ++row) {
    const Real expectedDiagonal = unrelaxed.diagonal[row] / alpha;
    const Real expectedRhs = unrelaxed.system.rhs()[row] +
                             (1.0 - alpha) / alpha * unrelaxed.diagonal[row] * previousU[row];
    EXPECT_NEAR(relaxed.diagonal[row], expectedDiagonal, 1e-9) << "row " << row;
    EXPECT_NEAR(relaxed.system.rhs()[row], expectedRhs, 1e-9) << "row " << row;
  }
}

TEST(CompressibleRelaxedMomentumTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);

  EXPECT_THROW((void)assembleRelaxedCompressibleMomentumComponent(
                   mesh, velocity, pressure, massFlux, density, density, 0.01, velocityBoundaries,
                   pressureBoundaries, VelocityComponent::U, previousU, 0.7, 1.0),
               InvalidArgumentError);
}

TEST(CompressibleRelaxedMomentumTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);

  EXPECT_THROW((void)assembleRelaxedCompressibleMomentumComponent(
                   mesh, velocity, pressure, massFlux, density, density, 0.01, velocityBoundaries,
                   pressureBoundaries, VelocityComponent::U, previousU, 0.7, 1.0),
               InvalidArgumentError);
}

TEST(CompressibleRelaxedMomentumTest, RejectsNonPositivePseudoTimeStep) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);

  EXPECT_THROW((void)assembleRelaxedCompressibleMomentumComponent(
                   mesh, velocity, pressure, massFlux, density, density, 0.01, velocityBoundaries,
                   pressureBoundaries, VelocityComponent::U, previousU, 0.7, 0.0),
               InvalidArgumentError);
}
