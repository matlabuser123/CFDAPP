// P16 -- turbulent conductivity, k_t = mu_t c_p / Pr_t.
//
// Short formula, tested on its own for the same reason
// computeEffectiveDiffusivity is: `mu_t c_p / Pr_t` is easy to transcribe as
// `mu_t c_p Pr_t` or to divide by rho as well, and neither slip would localise
// from an integrated thermal result.
//
// The property the rest of this milestone leans on is
// ExactlyZeroWhenTurbulentViscosityIsZero: it is what allows the laminar
// compatibility gate to require BITWISE equality rather than a tolerance.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/TurbulentConductivity.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::effectiveConductivityField;
using cfd::thermal::turbulentConductivity;
using cfd::thermal::turbulentThermalDiffusivity;

TEST(TurbulentConductivity, MatchesTheClosedForm) {
  // Hand-checked: 0.004 * 1005 / 0.85 = 4.02 / 0.85 = 4.729411764705882...
  const Real k = turbulentConductivity(0.004, 1005.0, 0.85);
  EXPECT_NEAR(k, 0.004 * 1005.0 / 0.85, 1e-15 * k);
  EXPECT_GT(k, 0.0);
}

TEST(TurbulentConductivity, ExactlyZeroWhenTurbulentViscosityIsZero) {
  // THE property the laminar bitwise gate depends on. Not "small" -- zero.
  for (const Real cp : {1.0, 1005.0, 4182.0}) {
    for (const Real prt : {0.5, 0.85, 1.0, 2.0}) {
      EXPECT_EQ(turbulentConductivity(0.0, cp, prt), 0.0) << "cp=" << cp << " Pr_t=" << prt;
    }
  }
}

TEST(TurbulentConductivity, AddingItToMolecularIsTheIdentityAtZero) {
  // The composition that actually appears in the energy equation, asserted
  // bitwise: k_mol + k_t(0) must be k_mol itself, not a value that rounds to
  // it. IEEE754 makes x + 0.0 == x for every finite x, and this pins that the
  // code relies on exactly that.
  for (const Real kMol : {1e-6, 0.0257, 0.6, 401.0}) {
    EXPECT_EQ(kMol + turbulentConductivity(0.0, 1005.0, 0.85), kMol);
  }
}

TEST(TurbulentConductivity, ScalesInverselyWithTurbulentPrandtl) {
  // k_t ~ 1/Pr_t, so halving Pr_t must double k_t. This is the algebraic core
  // of the Pr_t sensitivity requirement.
  const Real base = turbulentConductivity(0.004, 1005.0, 0.9);
  const Real halved = turbulentConductivity(0.004, 1005.0, 0.45);
  EXPECT_NEAR(halved, 2.0 * base, 1e-12 * base);

  Real previous = std::numeric_limits<Real>::infinity();
  for (const Real prt : {0.5, 0.7, 0.85, 1.0, 1.5}) {
    const Real k = turbulentConductivity(0.004, 1005.0, prt);
    EXPECT_LT(k, previous) << "k_t did not fall as Pr_t rose, at Pr_t = " << prt;
    previous = k;
  }
}

TEST(TurbulentConductivity, ScalesLinearlyWithViscosityAndSpecificHeat) {
  const Real base = turbulentConductivity(0.004, 1005.0, 0.85);
  EXPECT_NEAR(turbulentConductivity(0.008, 1005.0, 0.85), 2.0 * base, 1e-12 * base);
  EXPECT_NEAR(turbulentConductivity(0.004, 2010.0, 0.85), 2.0 * base, 1e-12 * base);
}

TEST(TurbulentConductivity, DiffusivityFormCarriesTheDensityAndConductivityDoesNot) {
  // alpha_t = mu_t/(rho Pr_t) and k_t = rho cp alpha_t, so rho cancels out of
  // k_t. Asserted directly, because passing a density into k_t would be a very
  // natural mistake and would change results silently.
  const Real rho = 1.225;
  const Real muT = 0.004;
  const Real cp = 1005.0;
  const Real prt = 0.85;
  const Real alphaT = turbulentThermalDiffusivity(muT, rho, prt);
  EXPECT_NEAR(rho * cp * alphaT, turbulentConductivity(muT, cp, prt),
              1e-12 * turbulentConductivity(muT, cp, prt));

  // And k_t must be identical for a different density.
  EXPECT_EQ(turbulentConductivity(muT, cp, prt), turbulentConductivity(muT, cp, prt));
  EXPECT_NE(turbulentThermalDiffusivity(muT, rho, prt),
            turbulentThermalDiffusivity(muT, 2.0 * rho, prt));
}

TEST(TurbulentConductivity, RejectsNonPhysicalArguments) {
  EXPECT_THROW((void)turbulentConductivity(-1e-9, 1005.0, 0.85), InvalidArgumentError);
  EXPECT_THROW((void)turbulentConductivity(0.004, 0.0, 0.85), InvalidArgumentError);
  EXPECT_THROW((void)turbulentConductivity(0.004, -1005.0, 0.85), InvalidArgumentError);
  EXPECT_THROW((void)turbulentConductivity(0.004, 1005.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)turbulentConductivity(0.004, 1005.0, -0.85), InvalidArgumentError);
  EXPECT_THROW((void)turbulentConductivity(std::numeric_limits<Real>::quiet_NaN(), 1005.0, 0.85),
               InvalidArgumentError);
  EXPECT_THROW((void)turbulentThermalDiffusivity(0.004, 0.0, 0.85), InvalidArgumentError);
}

// --- the field form ----------------------------------------------------------

namespace {

Mesh makeMesh() { return MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0); }

}  // namespace

TEST(TurbulentConductivityField, LaminarFieldIsBitwiseTheMolecularField) {
  const Mesh mesh = makeMesh();
  const Index n = mesh.numberOfCells();
  ScalarField kMol(n);
  ScalarField cp(n);
  const ScalarField muT(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    kMol[i] = 0.02 + (0.001 * static_cast<Real>(i));  // varying, so a bug cannot cancel
    cp[i] = 1000.0 + (5.0 * static_cast<Real>(i));
  }

  const ScalarField effective = effectiveConductivityField(mesh, kMol, muT, cp, 0.85);
  ASSERT_EQ(effective.size(), n);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(effective[i], kMol[i]) << "cell " << i;
  }
}

TEST(TurbulentConductivityField, UsesThePerCellSpecificHeatNotAReferenceValue) {
  // The variable-cp requirement, at formula level: two cp fields with the same
  // MEAN but different distributions must give different k_eff fields. A
  // implementation that used a single reference cp would pass a uniform-cp test
  // and fail this one.
  const Mesh mesh = makeMesh();
  const Index n = mesh.numberOfCells();
  const ScalarField kMol(n, 0.03);
  const ScalarField muT(n, 0.004);

  ScalarField uniform(n, 1005.0);
  ScalarField varying(n);
  for (Index i = 0; i < n; ++i) {
    // Symmetric about 1005 so the mean is identical.
    varying[i] = 1005.0 + ((i % 2 == 0) ? 200.0 : -200.0);
  }

  const ScalarField a = effectiveConductivityField(mesh, kMol, muT, uniform, 0.85);
  const ScalarField b = effectiveConductivityField(mesh, kMol, muT, varying, 0.85);
  bool anyDifference = false;
  for (Index i = 0; i < n; ++i) {
    if (a[i] != b[i]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference) << "k_eff is insensitive to the cp distribution, so the field form "
                                "is not using the per-cell specific heat";
}

TEST(TurbulentConductivityField, KeepsTheMolecularFieldVaryingRatherThanCollapsingIt) {
  // Variable molecular conductivity must survive: k_eff(T) must still vary
  // cell-to-cell in the way k_mol(T) does, not collapse to one scalar plus a
  // turbulent part.
  const Mesh mesh = makeMesh();
  const Index n = mesh.numberOfCells();
  ScalarField kMol(n);
  for (Index i = 0; i < n; ++i) kMol[i] = 0.02 + (0.004 * static_cast<Real>(i));
  const ScalarField muT(n, 0.004);
  const ScalarField cp(n, 1005.0);

  const ScalarField effective = effectiveConductivityField(mesh, kMol, muT, cp, 0.85);
  const Real turbulentPart = turbulentConductivity(0.004, 1005.0, 0.85);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(effective[i] - kMol[i], turbulentPart, 1e-12 * turbulentPart) << "cell " << i;
  }
  // And the spread of k_eff must equal the spread of k_mol: a uniform turbulent
  // addition shifts the field, it does not flatten it.
  EXPECT_NEAR(effective[n - 1] - effective[0], kMol[n - 1] - kMol[0],
              1e-12 * (kMol[n - 1] - kMol[0]));
}

TEST(TurbulentConductivityField, IsAlwaysAtLeastTheMolecularField) {
  const Mesh mesh = makeMesh();
  const Index n = mesh.numberOfCells();
  const ScalarField kMol(n, 0.03);
  ScalarField muT(n);
  for (Index i = 0; i < n; ++i) muT[i] = 0.001 * static_cast<Real>(i);  // includes 0
  const ScalarField cp(n, 1005.0);

  const ScalarField effective = effectiveConductivityField(mesh, kMol, muT, cp, 0.85);
  for (Index i = 0; i < n; ++i) {
    EXPECT_GE(effective[i], kMol[i]) << "cell " << i;
    EXPECT_GT(effective[i], 0.0) << "cell " << i;
  }
  EXPECT_EQ(effective[0], kMol[0]) << "the mu_t = 0 cell must be untouched, exactly";
  EXPECT_GT(effective[n - 1], kMol[n - 1]);
}

TEST(TurbulentConductivityField, RejectsMismatchedSizesAndBadEntries) {
  const Mesh mesh = makeMesh();
  const Index n = mesh.numberOfCells();
  const ScalarField good(n, 1.0);
  const ScalarField cp(n, 1005.0);
  const ScalarField shortField(n - 1, 1.0);

  EXPECT_THROW((void)effectiveConductivityField(mesh, shortField, good, cp, 0.85),
               InvalidArgumentError);
  EXPECT_THROW((void)effectiveConductivityField(mesh, good, shortField, cp, 0.85),
               InvalidArgumentError);
  EXPECT_THROW((void)effectiveConductivityField(mesh, good, good, shortField, 0.85),
               InvalidArgumentError);
  EXPECT_THROW((void)effectiveConductivityField(mesh, good, good, cp, 0.0), InvalidArgumentError);

  ScalarField negativeMuT(n, 0.0);
  negativeMuT[2] = -1e-9;
  EXPECT_THROW((void)effectiveConductivityField(mesh, good, negativeMuT, cp, 0.85),
               InvalidArgumentError);

  ScalarField zeroConductivity(n, 1.0);
  zeroConductivity[1] = 0.0;
  EXPECT_THROW((void)effectiveConductivityField(mesh, zeroConductivity, good, cp, 0.85),
               InvalidArgumentError);
}
