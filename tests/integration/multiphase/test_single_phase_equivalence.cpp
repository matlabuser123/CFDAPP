// P3-PHYS-005 sections 37-38: single-phase equivalence (alpha=1 -> pure
// phase 1, alpha=0 -> pure phase 2) and the equal-phase-property
// decoupling regression, both proven through the *actual* momentum
// diffusion path mu_mix feeds (section 19's own "use mixture viscosity
// through the existing momentum diffusion path") -- not just the
// standalone TwoPhaseSystem formula (already covered at unit level in
// test_multiphase_properties.cpp), but the assembled system it produces.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/multiphase/MultiphaseProperties.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::multiphase::evaluateMixtureViscosityField;
using cfd::multiphase::PhaseProperties;
using cfd::multiphase::TwoPhaseSystem;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet makeConstantVelocityBoundaries(const Mesh& mesh, Vector2 velocity) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(velocity));
  }
  return boundaries;
}

TwoPhaseSystem makeSystem(Real rho1, Real mu1, Real rho2, Real mu2) {
  return TwoPhaseSystem(PhaseProperties("phase1", rho1, mu1), PhaseProperties("phase2", rho2, mu2));
}

}  // namespace

TEST(SinglePhaseEquivalenceTest, AlphaOneReducesToPurePhase1MomentumAssemblyExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 1.0, 1.0);
  const TwoPhaseSystem system = makeSystem(/*rho1=*/1000.0, /*mu1=*/0.02, /*rho2=*/1.0,
                                           /*mu2=*/1.8e-5);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{3.0, -1.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.5});

  SparseMatrixBuilder scalarBuilder(n, n);
  Vector scalarRhs(n, 0.0);
  assembleDiffusionContribution(mesh, system.phase1().viscosity(), velocity, boundaries,
                                VelocityComponent::U, scalarBuilder, scalarRhs);
  const auto scalarMatrix = scalarBuilder.build();

  const ScalarField alpha(n, 1.0);
  const ScalarField muMix = evaluateMixtureViscosityField(mesh, alpha, system);
  SparseMatrixBuilder fieldBuilder(n, n);
  Vector fieldRhs(n, 0.0);
  assembleDiffusionContribution(mesh, muMix, velocity, boundaries, VelocityComponent::U,
                                fieldBuilder, fieldRhs);
  const auto fieldMatrix = fieldBuilder.build();

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(fieldMatrix.diagonal(row), scalarMatrix.diagonal(row), 1e-9) << "row " << row;
    EXPECT_NEAR(fieldRhs[row], scalarRhs[row], 1e-9) << "row " << row;
  }
}

TEST(SinglePhaseEquivalenceTest, AlphaZeroReducesToPurePhase2MomentumAssemblyExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 1.0, 1.0);
  const TwoPhaseSystem system = makeSystem(/*rho1=*/1000.0, /*mu1=*/0.02, /*rho2=*/1.0,
                                           /*mu2=*/1.8e-5);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{3.0, -1.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.5});

  SparseMatrixBuilder scalarBuilder(n, n);
  Vector scalarRhs(n, 0.0);
  assembleDiffusionContribution(mesh, system.phase2().viscosity(), velocity, boundaries,
                                VelocityComponent::U, scalarBuilder, scalarRhs);
  const auto scalarMatrix = scalarBuilder.build();

  const ScalarField alpha(n, 0.0);
  const ScalarField muMix = evaluateMixtureViscosityField(mesh, alpha, system);
  SparseMatrixBuilder fieldBuilder(n, n);
  Vector fieldRhs(n, 0.0);
  assembleDiffusionContribution(mesh, muMix, velocity, boundaries, VelocityComponent::U,
                                fieldBuilder, fieldRhs);
  const auto fieldMatrix = fieldBuilder.build();

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(fieldMatrix.diagonal(row), scalarMatrix.diagonal(row), 1e-9) << "row " << row;
    EXPECT_NEAR(fieldRhs[row], scalarRhs[row], 1e-9) << "row " << row;
  }
}

TEST(SinglePhaseEquivalenceTest, EqualPhasePropertiesDecoupleMomentumFromAlpha) {
  // Section 38: with rho1==rho2 and mu1==mu2, mu_mix is the same
  // constant regardless of alpha -- so two genuinely different, non-
  // uniform alpha fields must produce *identical* momentum assemblies.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TwoPhaseSystem system = makeSystem(/*rho1=*/1.0, /*mu1=*/0.05, /*rho2=*/1.0, /*mu2=*/0.05);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.2, -0.1});

  ScalarField alphaA(n);
  ScalarField alphaB(n);
  for (Index i = 0; i < n; ++i) {
    alphaA[i] = 0.1 + 0.05 * static_cast<Real>(i % 4);
    alphaB[i] = 1.0 - alphaA[i];  // deliberately different pattern.
  }
  ASSERT_NE(alphaA[0], alphaB[0]);

  const ScalarField muMixA = evaluateMixtureViscosityField(mesh, alphaA, system);
  const ScalarField muMixB = evaluateMixtureViscosityField(mesh, alphaB, system);
  for (Index i = 0; i < n; ++i) {
    EXPECT_DOUBLE_EQ(muMixA[i], 0.05) << "cell " << i;
    EXPECT_DOUBLE_EQ(muMixB[i], 0.05) << "cell " << i;
  }

  SparseMatrixBuilder builderA(n, n);
  Vector rhsA(n, 0.0);
  assembleDiffusionContribution(mesh, muMixA, velocity, boundaries, VelocityComponent::U, builderA,
                                rhsA);
  const auto matrixA = builderA.build();

  SparseMatrixBuilder builderB(n, n);
  Vector rhsB(n, 0.0);
  assembleDiffusionContribution(mesh, muMixB, velocity, boundaries, VelocityComponent::U, builderB,
                                rhsB);
  const auto matrixB = builderB.build();

  for (Index row = 0; row < n; ++row) {
    EXPECT_DOUBLE_EQ(matrixA.diagonal(row), matrixB.diagonal(row)) << "row " << row;
    EXPECT_DOUBLE_EQ(rhsA[row], rhsB[row]) << "row " << row;
  }
}
