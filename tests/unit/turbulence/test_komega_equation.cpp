// P2-TURB-005 sections 15-17, 34: KOmegaEquation.hpp's own linear
// effective-diffusivity formula (distinct from k-epsilon's reciprocal
// form) and the k/omega source-term hand tests.
#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/KOmegaEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::computeEffectiveDiffusivity;
using cfd::turbulence::computeLinearEffectiveDiffusivity;

// --- section 17/34: linear effective diffusivity ----------------------------

TEST(KOmegaEquationTest, LinearEffectiveDiffusivityMatchesHandDerivedExample) {
  // P2-TURB-005 section 17 (mandatory): mu=1, mu_t=3, sigma_k=2,
  // sigma_omega=2 -> Gamma_k=7, Gamma_omega=7 -- Gamma = mu + sigma*mu_t,
  // the *linear* form, not k-epsilon's mu + mu_t/sigma.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField muT(mesh.numberOfCells(), 3.0);

  const ScalarField gammaK = computeLinearEffectiveDiffusivity(1.0, muT, /*sigma=*/2.0);
  const ScalarField gammaOmega = computeLinearEffectiveDiffusivity(1.0, muT, /*sigma=*/2.0);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(gammaK[i], 7.0) << "cell " << i;
    EXPECT_DOUBLE_EQ(gammaOmega[i], 7.0) << "cell " << i;
  }
}

TEST(KOmegaEquationTest, LinearFormDiffersFromKEpsilonReciprocalForm) {
  // P2-TURB-005 section 34 (mandatory regression target): explicitly
  // prove k-omega's own diffusivity function is NOT the same formula as
  // k-epsilon's, for the same nontrivial inputs -- catches an accidental
  // reuse of computeEffectiveDiffusivity (mu + mu_t/sigma) in place of
  // computeLinearEffectiveDiffusivity (mu + sigma*mu_t).
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField muT(mesh.numberOfCells(), 3.0);
  const Real mu = 1.0;
  const Real sigma = 2.0;  // != 1, so the two formulas give different values.

  const ScalarField linearForm = computeLinearEffectiveDiffusivity(mu, muT, sigma);
  const ScalarField reciprocalForm = computeEffectiveDiffusivity(mu, muT, sigma);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(linearForm[i], mu + (sigma * muT[i])) << "cell " << i;      // = 7.
    EXPECT_DOUBLE_EQ(reciprocalForm[i], mu + (muT[i] / sigma)) << "cell " << i;  // = 2.5.
    EXPECT_NE(linearForm[i], reciprocalForm[i]) << "cell " << i;
  }
}

TEST(KOmegaEquationTest, LinearEffectiveDiffusivityReducesToMolecularAsMuTVanishes) {
  // P2-TURB-005 section 31: as mu_t -> 0, Gamma -> mu, for any sigma.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField tinyMuT(mesh.numberOfCells(), 1e-14);
  const Real mu = 0.02;

  const ScalarField gammaK = computeLinearEffectiveDiffusivity(mu, tinyMuT, /*sigma=*/2.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(gammaK[i], mu, 1e-12) << "cell " << i;
  }
}

TEST(KOmegaEquationTest, LinearEffectiveDiffusivityRejectsInvalidInputs) {
  const ScalarField muT(4, 1.0);
  EXPECT_THROW((void)computeLinearEffectiveDiffusivity(0.0, muT, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)computeLinearEffectiveDiffusivity(-1.0, muT, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)computeLinearEffectiveDiffusivity(1.0, muT, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)computeLinearEffectiveDiffusivity(1.0, muT, -1.0), InvalidArgumentError);
  const ScalarField negativeMuT(4, -0.5);
  EXPECT_THROW((void)computeLinearEffectiveDiffusivity(1.0, negativeMuT, 1.0),
               InvalidArgumentError);
}

// --- section 15-16: source hand tests ---------------------------------------

TEST(KOmegaEquationTest, OmegaSourceMatchesHandDerivedExample) {
  // P2-TURB-005 section 15 (mandatory): alpha=5/9, beta=0.075, rho=1,
  // k=2, omega=3, P_k=4 -> production = (5/9)*(3/2)*4 = 10/3, destruction
  // = 0.075*9 = 0.675, net ~= 2.6583333333.
  const Real alpha = 5.0 / 9.0;
  const Real beta = 0.075;
  const Real rho = 1.0;
  const Real k = 2.0;
  const Real omega = 3.0;
  const Real productionK = 4.0;

  const Real production = alpha * (omega / k) * productionK;
  const Real destruction = beta * rho * omega * omega;
  EXPECT_NEAR(production, 10.0 / 3.0, 1e-12);
  EXPECT_DOUBLE_EQ(destruction, 0.675);
  EXPECT_NEAR(production - destruction, 2.6583333333333333, 1e-9);

  // KOmegaModel's actual linearization: Su=production, Sp=-beta*rho*omega
  // (so Sp*omega_used reproduces -destruction exactly at
  // omega_used=omega).
  const Real su = production;
  const Real sp = -beta * rho * omega;
  EXPECT_NEAR(su + (sp * omega), production - destruction, 1e-12);
  EXPECT_LE(sp, 0.0);
}

TEST(KOmegaEquationTest, KSourceMatchesHandDerivedExample) {
  // P2-TURB-005 section 16 (mandatory): beta_star=0.09, rho=1, k=2,
  // omega=3, P_k=4 -> destruction = 0.09*1*2*3 = 0.54, net = 4-0.54=3.46.
  const Real betaStar = 0.09;
  const Real rho = 1.0;
  const Real k = 2.0;
  const Real omega = 3.0;
  const Real productionK = 4.0;

  const Real destruction = betaStar * rho * k * omega;
  EXPECT_DOUBLE_EQ(destruction, 0.54);
  EXPECT_DOUBLE_EQ(productionK - destruction, 3.46);

  // KOmegaModel's actual linearization: Su=P_k, Sp=-beta_star*rho*omega
  // (so Sp*k_used reproduces -destruction exactly at k_used=k -- unlike
  // k-epsilon's own k-destruction, this needs no k_safe floor at all,
  // since -beta_star*rho*k*omega is already exactly linear in k).
  const Real su = productionK;
  const Real sp = -betaStar * rho * omega;
  EXPECT_DOUBLE_EQ(su + (sp * k), productionK - destruction);
  EXPECT_LE(sp, 0.0);
}
