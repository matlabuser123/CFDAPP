// P3-PHYS-005: TwoPhaseSystem mixture laws (section 23) + field
// evaluators, using the task's own analytically-obvious example values.
#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/multiphase/MultiphaseProperties.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::multiphase::PhaseProperties;
using cfd::multiphase::TwoPhaseSystem;

namespace {

TwoPhaseSystem makeExampleSystem() {
  // Section 23's own example values.
  return TwoPhaseSystem(PhaseProperties("phase1", 1000.0, 1.0), PhaseProperties("phase2", 100.0, 0.1));
}

}  // namespace

TEST(TwoPhaseSystemTest, StoresBothPhases) {
  const TwoPhaseSystem system = makeExampleSystem();
  EXPECT_EQ(system.phase1().name(), "phase1");
  EXPECT_EQ(system.phase2().name(), "phase2");
}

TEST(TwoPhaseSystemTest, MixtureDensityAtCanonicalAlphaValues) {
  const TwoPhaseSystem system = makeExampleSystem();
  EXPECT_DOUBLE_EQ(system.mixtureDensity(0.0), 100.0);
  EXPECT_DOUBLE_EQ(system.mixtureDensity(1.0), 1000.0);
  EXPECT_DOUBLE_EQ(system.mixtureDensity(0.25), 0.25 * 1000.0 + 0.75 * 100.0);   // 325
  EXPECT_DOUBLE_EQ(system.mixtureDensity(0.5), 0.5 * 1000.0 + 0.5 * 100.0);      // 550
  EXPECT_DOUBLE_EQ(system.mixtureDensity(0.75), 0.75 * 1000.0 + 0.25 * 100.0);   // 775
}

TEST(TwoPhaseSystemTest, MixtureViscosityAtCanonicalAlphaValues) {
  const TwoPhaseSystem system = makeExampleSystem();
  EXPECT_DOUBLE_EQ(system.mixtureViscosity(0.0), 0.1);
  EXPECT_DOUBLE_EQ(system.mixtureViscosity(1.0), 1.0);
  EXPECT_NEAR(system.mixtureViscosity(0.25), 0.25 * 1.0 + 0.75 * 0.1, 1e-12);
  EXPECT_NEAR(system.mixtureViscosity(0.5), 0.5 * 1.0 + 0.5 * 0.1, 1e-12);
  EXPECT_NEAR(system.mixtureViscosity(0.75), 0.75 * 1.0 + 0.25 * 0.1, 1e-12);
}

TEST(TwoPhaseSystemTest, DeterministicRepeatedEvaluation) {
  const TwoPhaseSystem system = makeExampleSystem();
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(system.mixtureDensity(0.37), system.mixtureDensity(0.37));
    EXPECT_EQ(system.mixtureViscosity(0.37), system.mixtureViscosity(0.37));
  }
}

TEST(TwoPhaseSystemTest, RejectsNonFiniteAlpha) {
  const TwoPhaseSystem system = makeExampleSystem();
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW((void)system.mixtureDensity(nan), InvalidArgumentError);
  EXPECT_THROW((void)system.mixtureViscosity(nan), InvalidArgumentError);
}

TEST(TwoPhaseSystemTest, DoesNotRejectOutOfRangeAlpha) {
  // Section 14: boundedness is reported, not enforced -- the linear
  // mixture law is well-defined algebraically for any finite alpha.
  const TwoPhaseSystem system = makeExampleSystem();
  EXPECT_NO_THROW((void)system.mixtureDensity(-0.1));
  EXPECT_NO_THROW((void)system.mixtureDensity(1.1));
}

// --- Field evaluators -------------------------------------------------

TEST(EvaluateMixtureFieldTest, EvaluatesEveryCellFromItsOwnAlpha) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const TwoPhaseSystem system = makeExampleSystem();
  ScalarField alpha(mesh.numberOfCells());
  alpha[0] = 0.0;
  alpha[1] = 0.5;
  alpha[2] = 1.0;
  alpha[3] = 0.25;

  const ScalarField density = cfd::multiphase::evaluateMixtureDensityField(mesh, alpha, system);
  const ScalarField viscosity = cfd::multiphase::evaluateMixtureViscosityField(mesh, alpha, system);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(density[i], system.mixtureDensity(alpha[i])) << "cell " << i;
    EXPECT_DOUBLE_EQ(viscosity[i], system.mixtureViscosity(alpha[i])) << "cell " << i;
  }
}

TEST(EvaluateMixtureFieldTest, MinMaxBoundsMatchTheLinearLawsRange) {
  // For linear mixture laws, rho_mix/mu_mix must stay within
  // [min(rho1,rho2), max(rho1,rho2)] / [min(mu1,mu2), max(mu1,mu2)] for
  // any alpha in [0,1] (section 33).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TwoPhaseSystem system = makeExampleSystem();
  ScalarField alpha(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    alpha[i] = static_cast<Real>(i) / static_cast<Real>(mesh.numberOfCells() - 1);  // spans [0,1].
  }
  const ScalarField density = cfd::multiphase::evaluateMixtureDensityField(mesh, alpha, system);
  const ScalarField viscosity = cfd::multiphase::evaluateMixtureViscosityField(mesh, alpha, system);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_GE(density[i], 100.0 - 1e-9);
    EXPECT_LE(density[i], 1000.0 + 1e-9);
    EXPECT_GE(viscosity[i], 0.1 - 1e-9);
    EXPECT_LE(viscosity[i], 1.0 + 1e-9);
  }
}

TEST(EvaluateMixtureFieldTest, MismatchedAlphaSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const TwoPhaseSystem system = makeExampleSystem();
  const ScalarField alpha(mesh.numberOfCells() + 1, 0.5);
  EXPECT_THROW((void)cfd::multiphase::evaluateMixtureDensityField(mesh, alpha, system),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::multiphase::evaluateMixtureViscosityField(mesh, alpha, system),
               InvalidArgumentError);
}
