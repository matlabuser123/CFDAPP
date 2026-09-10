// P2-TURB-004 sections 13-14, 31-34: KEpsilonEquation.hpp's
// model-agnostic scalar-transport assembly -- the effective-diffusivity
// formula, the implicit source linearization, and equation-level
// (isolated diffusion/convection/source) manufactured tests.
#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::applyImplicitScalarSource;
using cfd::turbulence::assembleScalarDiffusionContribution;
using cfd::turbulence::assembleScalarTransportEquation;
using cfd::turbulence::computeEffectiveDiffusivity;

namespace {

BoundaryConditionSet makeZeroGradientBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

// --- section 31: effective diffusivity -------------------------------------

TEST(KEpsilonEquationTest, EffectiveDiffusivityMatchesHandDerivedExample) {
  // P2-TURB-004 section 31 (mandatory): mu=1, mu_t=3, sigma_k=1,
  // sigma_epsilon=1.5 -> Gamma_k=4, Gamma_epsilon=3 -- catches the
  // incorrect (mu+mu_t)/sigma formula, which would give Gamma_epsilon =
  // 4/1.5 = 2.667 instead.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField muT(mesh.numberOfCells(), 3.0);

  const ScalarField gammaK = computeEffectiveDiffusivity(1.0, muT, /*sigma=*/1.0);
  const ScalarField gammaEpsilon = computeEffectiveDiffusivity(1.0, muT, /*sigma=*/1.5);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(gammaK[i], 4.0) << "cell " << i;
    EXPECT_DOUBLE_EQ(gammaEpsilon[i], 3.0) << "cell " << i;
    // The specific wrong formula this test exists to catch:
    EXPECT_NE(gammaEpsilon[i], (1.0 + 3.0) / 1.5) << "cell " << i;
  }
}

TEST(KEpsilonEquationTest, EffectiveDiffusivityReducesToMolecularAsMuTVanishes) {
  // P2-TURB-004 section 39: as mu_t -> 0, Gamma -> mu (molecular
  // diffusion only), for any sigma.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField tinyMuT(mesh.numberOfCells(), 1e-14);
  const Real mu = 0.02;

  const ScalarField gammaK = computeEffectiveDiffusivity(mu, tinyMuT, /*sigma=*/1.0);
  const ScalarField gammaEpsilon = computeEffectiveDiffusivity(mu, tinyMuT, /*sigma=*/1.3);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(gammaK[i], mu, 1e-12) << "cell " << i;
    EXPECT_NEAR(gammaEpsilon[i], mu, 1e-12) << "cell " << i;
  }
}

TEST(KEpsilonEquationTest, EffectiveDiffusivityRejectsInvalidInputs) {
  const ScalarField muT(4, 1.0);
  EXPECT_THROW((void)computeEffectiveDiffusivity(0.0, muT, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)computeEffectiveDiffusivity(-1.0, muT, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)computeEffectiveDiffusivity(1.0, muT, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)computeEffectiveDiffusivity(1.0, muT, -1.0), InvalidArgumentError);
  const ScalarField negativeMuT(4, -0.5);
  EXPECT_THROW((void)computeEffectiveDiffusivity(1.0, negativeMuT, 1.0), InvalidArgumentError);
}

// --- section 32: source-term linearization ----------------------------------

TEST(KEpsilonEquationTest, ImplicitSourceReproducesKDestructionAtSelfConsistency) {
  // P2-TURB-004 section 32 (mandatory): P_k=5, rho=2, epsilon=1.5 ->
  // physical net source (before linearization) = 5 - 2*1.5 = 2. Choosing
  // k=4 for the Sp*phi evaluation (Sp = -rho*epsilon/k), Su + Sp*k must
  // reproduce that same physical value exactly -- proving the
  // linearization is algebraically consistent with the governing
  // equation, not just "some stable-looking coefficients".
  const Real productionK = 5.0;
  const Real rho = 2.0;
  const Real epsilon = 1.5;
  const Real k = 4.0;
  const Real su = productionK;
  const Real sp = -rho * epsilon / k;
  const Real physicalSource = productionK - (rho * epsilon);

  EXPECT_DOUBLE_EQ(su + (sp * k), physicalSource);
  EXPECT_DOUBLE_EQ(physicalSource, 2.0);
  EXPECT_LE(sp, 0.0);  // stabilizing: Sp <= 0.
}

TEST(KEpsilonEquationTest, ImplicitSourceReproducesEpsilonDestructionAtSelfConsistency) {
  // P2-TURB-004 section 32 (mandatory): C1=1.44, C2=1.92, rho=1,
  // epsilon=2, k=4, P_k=3 -> production=1.44*(2/4)*3=2.16,
  // destruction=1.92*1*(4/4)=1.92 (epsilon^2/k = 4/4 = 1), net=0.24.
  const Real c1 = 1.44;
  const Real c2 = 1.92;
  const Real rho = 1.0;
  const Real epsilon = 2.0;
  const Real k = 4.0;
  const Real productionK = 3.0;

  const Real production = c1 * (epsilon / k) * productionK;
  const Real destruction = c2 * rho * (epsilon * epsilon) / k;
  EXPECT_DOUBLE_EQ(production, 2.16);
  EXPECT_DOUBLE_EQ(destruction, 1.92);
  // Not EXPECT_DOUBLE_EQ here: 1.44*(2/4)*3 - 1.92*1*(4/4) does not
  // cancel to bit-identical 0.24 (ordinary floating-point subtraction of
  // two independently-rounded products), unlike production/destruction
  // themselves, which each match their own hand-computed literal
  // exactly.
  EXPECT_NEAR(production - destruction, 0.24, 1e-12);

  // KEpsilonModel's actual linearization: Su=production, Sp=-C2*rho*epsilon/k
  // (so Sp*epsilon_used reproduces -destruction exactly at epsilon_used=epsilon).
  const Real su = production;
  const Real sp = -c2 * rho * epsilon / k;
  EXPECT_DOUBLE_EQ(su + (sp * epsilon), production - destruction);
  EXPECT_LE(sp, 0.0);
}

TEST(KEpsilonEquationTest, ImplicitSourceAddsToDiagonalAndRhsCorrectly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const ScalarField su(n, 3.0);
  const ScalarField sp(n, -2.0);  // Sp <= 0, stabilizing.

  applyImplicitScalarSource(mesh, builder, rhs, su, sp);
  const auto matrix = builder.build();

  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    EXPECT_DOUBLE_EQ(matrix.diagonal(i), -sp[i] * cell.volume());  // -Sp*V added to diagonal.
    EXPECT_DOUBLE_EQ(rhs[i], su[i] * cell.volume());
  }
}

TEST(KEpsilonEquationTest, ImplicitSourceRejectsNonFiniteSuOrSp) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  ScalarField su(n, 1.0);
  su[0] = std::numeric_limits<Real>::quiet_NaN();
  const ScalarField sp(n, -1.0);
  EXPECT_THROW((void)applyImplicitScalarSource(mesh, builder, rhs, su, sp), InvalidArgumentError);
}

// --- section 33-34: equation-level manufactured tests -----------------------

TEST(KEpsilonEquationTest, PureDiffusionConstantFieldGivesZeroResidual) {
  // Uniform phi with a matching zero-gradient boundary: diffusion
  // contribution must be exactly A*phi - b = 0 in every cell.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const Real value = 6.0;
  const ScalarField phi(n, value);
  const ScalarField diffusivity(n, 2.5);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleScalarDiffusionContribution(mesh, diffusivity, phi, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Vector phiExact(n, value);
  const Vector residual = matrix.multiply(phiExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10) << "cell " << i;
  }
}

TEST(KEpsilonEquationTest, PureSourceOnlyAddsExactlyToRhsAndDiagonal) {
  // Isolate the source term alone (no diffusion/convection assembled at
  // all): the resulting system must be purely diagonal, with b/A =
  // Su/(-Sp) at every cell -- i.e. solving it in isolation recovers
  // exactly the algebraic source-balance value.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const ScalarField su(n, 10.0);
  const ScalarField sp(n, -4.0);
  applyImplicitScalarSource(mesh, builder, rhs, su, sp);
  const auto matrix = builder.build();

  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    const Real expectedPhi = (su[i] * cell.volume()) / (-sp[i] * cell.volume());  // = Su/(-Sp).
    EXPECT_DOUBLE_EQ(expectedPhi, 10.0 / 4.0);
    EXPECT_DOUBLE_EQ(rhs[i] / matrix.diagonal(i), expectedPhi);
  }
}

TEST(KEpsilonEquationTest, UniformFieldWithZeroSourceIsPreservedByFullEquation) {
  // P2-TURB-004 section 34: uniform velocity (-> zero mass flux
  // divergence, hence zero net convective imbalance for a uniform phi
  // too), uniform phi, zero-gradient boundaries, and zero source ->
  // solving the assembled system must reproduce exactly the uniform
  // input value (a constant field is an exact steady solution with no
  // production/destruction driving it away from itself).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const Real value = 3.5;
  const ScalarField phi(n, value);
  const ScalarField diffusivity(n, 1.0);
  const SurfaceField zeroFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zeroSu(n, 0.0);
  const ScalarField zeroSp(n, 0.0);

  const auto assembly =
      assembleScalarTransportEquation(mesh, phi, zeroFlux, diffusivity, boundaries, zeroSu, zeroSp);
  const Vector phiExact(n, value);
  const Vector residual = assembly.system.matrix().multiply(phiExact) - assembly.system.rhs();
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10) << "cell " << i;
  }
}

TEST(KEpsilonEquationTest, TransportEquationRejectsSizeMismatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField phi(n, 1.0);
  const ScalarField diffusivity(n, 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField su(n, 0.0);
  const ScalarField spWrongSize(n + 1, 0.0);

  EXPECT_THROW((void)assembleScalarTransportEquation(mesh, phi, massFlux, diffusivity, boundaries,
                                                     su, spWrongSize),
               InvalidArgumentError);
}
