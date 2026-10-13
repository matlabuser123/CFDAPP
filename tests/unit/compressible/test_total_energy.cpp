// P17.5 -- the total-energy state, its conservative equation, and its conservation.
//
// Every test here is named in the CRITERION -> TEST coverage map of
// results/p17/total_energy/acceptance_gate.md section 13. That map exists because P17.4 closed with
// seven frozen criteria that were measured by nothing or by the wrong instrument, five of them
// found by hand and late. A criterion with no test named against it is not verified, whatever the
// suite total says.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/compressible/TotalEnergyEquation.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyBalance;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace p17_5 {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;

// The ZERO reference: T_ref = 0, h_ref = 0. This is the calorically perfect convention in which
// e = cv T, h = cp T and the gamma pressure form are EXACT -- measured exactly 0 in
// results/p17/total_energy/logs/01_dryrun.log sections A, C and F.
TotalEnergyState zeroReferenceState() {
  const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(kR, kCp), SensibleEnthalpy(cp, 0.0, 0.0));
}

// A NONZERO reference. P15 supports it, so the contract must too -- and the shortcuts above must
// then be measurably wrong rather than quietly applied.
TotalEnergyState shiftedReferenceState(Real referenceTemperature = 298.15) {
  const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(kR, kCp),
                          SensibleEnthalpy(cp, referenceTemperature, 0.0));
}

BoundaryConditionSet zeroGradientScalar(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// A closed box: every boundary face carries zero mass flux, internal faces carry a deliberately
// IRREGULAR flux. The irregularity matters -- a smooth or divergence-free flux could make a
// telescoping test pass for the wrong reason.
SurfaceField closedBoxIrregularFlux(const Mesh& mesh) {
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Real f = static_cast<Real>(face.id());
    flux[face.id()] = 0.41 * std::sin(0.7 * f) + 0.13 * static_cast<Real>(face.id() % 7);
  }
  return flux;
}

}  // namespace p17_5

// ===========================================================================
// Group Z -- the dual-cp decision is itself gated
// ===========================================================================

// Z2: a variable cp cannot be made consistent with an EOS block that stores one cp.
TEST(TotalEnergyContractTest, RejectsAVariableSpecificHeatModel) {
  // cp(T) = 900 + 0.15 T -- exactly the model whose naive cp*T transport the dry-run measured at
  // 26-103% error.
  const TemperatureDependentProperty variableCp(std::vector<Real>{900.0, 0.15}, 200.0, 2000.0,
                                                "cp_linear");
  EXPECT_THROW((void)TotalEnergyState(ThermodynamicProperties(p17_5::kR, p17_5::kCp),
                                      SensibleEnthalpy(variableCp, 300.0, 0.0)),
               InvalidArgumentError);
}

// Z2: the two cp sources disagreeing is the hazard audit.md section 4 measured. It must throw.
TEST(TotalEnergyContractTest, RejectsDisagreeingSpecificHeatSources) {
  const TemperatureDependentProperty thermalCp =
      TemperatureDependentProperty::constant(1005.0, "cp");
  // The EOS block declares a DIFFERENT gas: cp = 1200 against the thermal block's 1005.
  EXPECT_THROW((void)TotalEnergyState(ThermodynamicProperties(p17_5::kR, 1200.0),
                                      SensibleEnthalpy(thermalCp, 298.15, 0.0)),
               InvalidArgumentError);

  // Non-vacuity of the check itself: a difference just inside the tolerance must be ACCEPTED, so
  // the test is not simply asserting that the constructor always throws.
  const Real nudged = 1005.0 * (1.0 + 0.1 * TotalEnergyState::kSpecificHeatRelativeTolerance);
  EXPECT_NO_THROW((void)TotalEnergyState(ThermodynamicProperties(p17_5::kR, nudged),
                                         SensibleEnthalpy(thermalCp, 298.15, 0.0)));
}

// Z3
TEST(TotalEnergyContractTest, AcceptsConsistentSpecificHeatSources) {
  EXPECT_NO_THROW((void)p17_5::zeroReferenceState());
  EXPECT_NO_THROW((void)p17_5::shiftedReferenceState());
}

// Z1: structural -- the energy path must never take cp from the EOS block. Verified by the
// behaviour that would break if it did: under a nonzero reference, h comes from the enthalpy model
// and so differs from cp*T by exactly the reference offset. If the state read cp from
// ThermodynamicProperties and multiplied, this difference would be zero.
TEST(TotalEnergyContractTest, EnthalpyComesFromTheModelNotTheEosSpecificHeat) {
  const TotalEnergyState state = p17_5::shiftedReferenceState();
  const Real T = 500.0;
  const Real h = state.specificStaticEnthalpy(T);
  EXPECT_NEAR(h, p17_5::kCp * (T - 298.15), 1e-9);
  EXPECT_NEAR(p17_5::kCp * T - h, p17_5::kCp * 298.15, 1e-9)
      << "h must differ from cp*T by exactly the reference offset";
}

// ===========================================================================
// Group T -- the thermodynamic state
// ===========================================================================

// T1
TEST(TotalEnergyStateTest, KineticEnergyIsHalfVelocityMagnitudeSquared) {
  // Three components on purpose: Vector2 is an alias for Vector3 with z = 0, so |U|^2 is
  // u^2+v^2+w^2 with no special case, and this pins that the z term is really included.
  EXPECT_EQ(TotalEnergyState::specificKineticEnergy(Vector2{3.0, 4.0}), 12.5);
  EXPECT_EQ(TotalEnergyState::specificKineticEnergy(Vector2{3.0, 4.0, 12.0}), 84.5);
  EXPECT_EQ(TotalEnergyState::specificKineticEnergy(Vector2{0.0, 0.0}), 0.0);
}

// T2, T3, T4
TEST(TotalEnergyStateTest, StateComponentsComposeExactly) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Real T = 450.0;
  const Vector2 U{30.0, -40.0};
  const Real rho = 1.4;

  const Real e = state.specificInternalEnergy(T);
  const Real h = state.specificStaticEnthalpy(T);
  const Real ke = TotalEnergyState::specificKineticEnergy(U);

  EXPECT_EQ(state.specificTotalEnergy(T, U), e + ke);                // T2
  EXPECT_EQ(state.conservedTotalEnergy(rho, T, U), rho * (e + ke));  // T3
  EXPECT_EQ(state.specificTotalEnthalpy(T, U), h + ke);              // T4
}

// T5 -- the defining identity, for BOTH references. Exactly zero, because e := h - R T.
TEST(TotalEnergyStateTest, EnthalpyMinusInternalEnergyIsExactlyRT) {
  for (const TotalEnergyState& state :
       {p17_5::zeroReferenceState(), p17_5::shiftedReferenceState()}) {
    for (const Real T : {300.0, 500.0, 1000.0, 2000.0}) {
      const Real h = state.specificStaticEnthalpy(T);
      const Real e = state.specificInternalEnergy(T);
      EXPECT_EQ(h - e, p17_5::kR * T)
          << "h - e must be EXACTLY R T at T = " << T << " -- it is algebraic, not approximate";
    }
  }
}

// T6
TEST(TotalEnergyStateTest, StaticEnthalpyEqualsInternalEnergyPlusPOverRho) {
  const TotalEnergyState state = p17_5::shiftedReferenceState();
  for (const Real T : {320.0, 780.0}) {
    for (const Real rho : {0.4, 1.2, 7.5}) {
      const Real p = state.thermodynamics().equationOfState().pressure(rho, T);
      const Real h = state.specificStaticEnthalpy(T);
      const Real e = state.specificInternalEnergy(T);
      // Passes through a division, so a few eps rather than exactly 0.
      EXPECT_NEAR(h, e + p / rho, 4.0 * p17_5::kEps * std::abs(h));
    }
  }
}

// T7 -- rho hTotal = rhoE + p, the identity that lets the convective flux be written with hTotal.
TEST(TotalEnergyStateTest, DensityTimesTotalEnthalpyEqualsRhoEPlusPressure) {
  const TotalEnergyState state = p17_5::shiftedReferenceState();
  struct Case {
    Real rho, T, u, v;
  };
  for (const Case& c : {Case{1.2, 300.0, 10.0, 5.0}, Case{0.35, 1200.0, 250.0, -80.0},
                        Case{8.0, 250.0, 0.5, 0.0}, Case{1.0, 500.0, 0.0, 0.0}}) {
    const Vector2 U{c.u, c.v};
    const Real rhoE = state.conservedTotalEnergy(c.rho, c.T, U);
    const Real p = state.thermodynamics().equationOfState().pressure(c.rho, c.T);
    const Real lhs = c.rho * state.specificTotalEnthalpy(c.T, U);
    // THE BOUND IS DERIVED FROM THE SUBTRACTION, NOT FROM THE RESULT. Under a shifted reference
    // rhoE and p are both O(1e5) while their sum is O(1e3), so this is a cancellation: the absolute
    // error floor is eps times the LARGER OPERAND, not eps times the (small) answer. The first
    // version of this test bounded it by |lhs| and failed at 5.91e-12 against 2.05e-12 -- the
    // threshold was wrong, not the code. See AMENDMENT_thresholds.md.
    const Real operand = std::max(std::abs(rhoE), std::abs(p));
    EXPECT_NEAR(lhs, rhoE + p, 4.0 * p17_5::kEps * operand)
        << "rho hTotal must equal rhoE + p for rho=" << c.rho << " T=" << c.T;
  }
}

// T8 -- the calorically perfect shortcuts, asserted ONLY where they are exact.
TEST(TotalEnergyStateTest, CaloricallyPerfectShortcutsHoldUnderTheZeroReference) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  ASSERT_TRUE(state.hasZeroReference());
  const Real cv = state.thermodynamics().specificHeatVolume();
  for (const Real T : {200.0, 700.0, 1800.0}) {
    // h = cp T is ONE product, so it is exact.
    EXPECT_EQ(state.specificStaticEnthalpy(T), p17_5::kCp * T) << "h = cp T at T = " << T;
    // e = cv T is NOT exact, and the first version of this test asserting equality was a
    // mis-derived threshold rather than a code defect: e is computed as cp*T - R*T while cv is the
    // ROUNDED difference cp - R, and (cp-R)*T differs from cp*T - R*T by about one ulp. Measured at
    // T = 700: 502565 against 502565.00000000006, i.e. 0.52 eps relative. The bound is therefore
    // eps-scaled. It still discriminates by twelve orders of magnitude: using cp instead of cv
    // would be wrong by R*T ~ 2e5. See AMENDMENT_thresholds.md.
    const Real e = state.specificInternalEnergy(T);
    EXPECT_NEAR(e, cv * T, 4.0 * p17_5::kEps * std::abs(e)) << "e = cv T at T = " << T;
  }
}

// T9 -- and the same shortcuts must be MEASURABLY wrong under a nonzero reference, so that a test
// suite cannot silently pass by applying them everywhere.
TEST(TotalEnergyStateTest, CaloricallyPerfectShortcutsAreWrongUnderANonzeroReference) {
  const TotalEnergyState state = p17_5::shiftedReferenceState();
  ASSERT_FALSE(state.hasZeroReference());
  const Real cv = state.thermodynamics().specificHeatVolume();
  const Real T = 500.0;
  // The offset is cp*T_ref = 1005 * 298.15 = 299640.75 -- far above the >= 1e3 the gate demands,
  // so this cannot pass by accident.
  EXPECT_GT(std::abs(state.specificStaticEnthalpy(T) - p17_5::kCp * T), 1.0e3);
  EXPECT_GT(std::abs(state.specificInternalEnergy(T) - cv * T), 1.0e3);
}

// D1
TEST(TotalEnergyStateTest, RepeatedEvaluationIsBitwiseIdentical) {
  const TotalEnergyState state = p17_5::shiftedReferenceState();
  const Real T = 613.25;
  const Vector2 U{17.5, -3.25};
  const Real rho = 1.17;
  const Real first = state.conservedTotalEnergy(rho, T, U);
  const auto firstRecovered = state.recover(rho, Vector2{rho * U.x, rho * U.y}, first);
  for (int repeat = 0; repeat < 8; ++repeat) {
    EXPECT_EQ(state.conservedTotalEnergy(rho, T, U), first);
    const auto again = state.recover(rho, Vector2{rho * U.x, rho * U.y}, first);
    EXPECT_EQ(again.temperature, firstRecovered.temperature);
    EXPECT_EQ(again.pressure, firstRecovered.pressure);
    EXPECT_EQ(again.specificInternalEnergy, firstRecovered.specificInternalEnergy);
  }
}

// Supports C3: the closed-form inverse and P15's forward model must agree. Neither is trusted
// alone -- the forward direction is P15's and the inverse is closed form, so checking them against
// each other is what makes reusing P15 rather than duplicating its bisection safe.
TEST(TotalEnergyStateTest, InternalEnergyInverseAgreesWithTheForwardModel) {
  for (const TotalEnergyState& state :
       {p17_5::zeroReferenceState(), p17_5::shiftedReferenceState()}) {
    for (const Real T : {250.0, 300.0, 700.0, 1500.0, 2500.0}) {
      const Real e = state.specificInternalEnergy(T);
      const Real back = state.temperatureFromInternalEnergy(e);
      EXPECT_NEAR(back, T, SensibleEnthalpy::kInversionToleranceKelvin)
          << "T -> e -> T at T = " << T;
    }
  }
}

// ===========================================================================
// Group C -- conversion and recovery
// ===========================================================================

// C1
TEST(TotalEnergyConversionTest, PrimitiveToConservedMatchesHandComputedValue) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Real rho = 1.25;
  const Real T = 400.0;
  const Vector2 U{60.0, 80.0};  // |U| = 100 exactly, so ke = 5000 exactly
  const Real cv = state.thermodynamics().specificHeatVolume();
  // Hand-computed: rhoE = rho (cv T + 0.5|U|^2) under the zero reference.
  const Real expected = rho * (cv * T + 5000.0);
  EXPECT_EQ(state.conservedTotalEnergy(rho, T, U), expected);
}

// C2, C3, C4
TEST(TotalEnergyConversionTest, ConservedToPrimitiveRecoversTheWholeState) {
  const TotalEnergyState state = p17_5::shiftedReferenceState();
  const Real rho = 0.8;
  const Real T = 650.0;
  const Vector2 U{45.0, -20.0};
  const Real rhoE = state.conservedTotalEnergy(rho, T, U);
  const Vector2 momentum{rho * U.x, rho * U.y};

  const auto out = state.recover(rho, momentum, rhoE);

  EXPECT_NEAR(out.velocity.x, U.x, 8.0 * p17_5::kEps * std::abs(U.x));  // C2
  EXPECT_NEAR(out.velocity.y, U.y, 8.0 * p17_5::kEps * std::abs(U.y));  // C2
  const Real e = state.specificInternalEnergy(T);
  EXPECT_NEAR(out.specificInternalEnergy, e, 8.0 * p17_5::kEps * std::abs(e));   // C2
  EXPECT_NEAR(out.temperature, T, SensibleEnthalpy::kInversionToleranceKelvin);  // C3
  // C4: pressure must come from P17.2's EOS, not a private formula.
  EXPECT_EQ(out.pressure, state.thermodynamics().equationOfState().pressure(rho, out.temperature));
}

// C5
TEST(TotalEnergyConversionTest, PrimitiveConservedPrimitiveRoundTrip) {
  struct Case {
    Real rho, T, u, v;
  };
  const Case cases[] = {{1.2, 300.0, 10.0, 5.0},
                        {0.35, 1200.0, 250.0, -80.0},
                        {8.0, 250.0, 0.5, 0.0},
                        {1.0, 500.0, 0.0, 0.0}};
  for (const TotalEnergyState& state :
       {p17_5::zeroReferenceState(), p17_5::shiftedReferenceState()}) {
    for (const Case& c : cases) {
      const Vector2 U{c.u, c.v};
      const Real rhoE = state.conservedTotalEnergy(c.rho, c.T, U);
      const auto out = state.recover(c.rho, Vector2{c.rho * c.u, c.rho * c.v}, rhoE);
      EXPECT_NEAR(out.temperature, c.T, SensibleEnthalpy::kInversionToleranceKelvin);
      EXPECT_NEAR(out.velocity.x, c.u, 8.0 * p17_5::kEps * std::max(1.0, std::abs(c.u)));
      EXPECT_NEAR(out.velocity.y, c.v, 8.0 * p17_5::kEps * std::max(1.0, std::abs(c.v)));
    }
  }
}

// C6
TEST(TotalEnergyConversionTest, ConservedPrimitiveConservedRoundTrip) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  struct Case {
    Real rho, T, u, v;
  };
  for (const Case& c : {Case{1.2, 300.0, 10.0, 5.0}, Case{0.35, 1200.0, 250.0, -80.0},
                        Case{8.0, 250.0, 0.5, 0.0}}) {
    const Vector2 U{c.u, c.v};
    const Real rhoE = state.conservedTotalEnergy(c.rho, c.T, U);
    const auto out = state.recover(c.rho, Vector2{c.rho * c.u, c.rho * c.v}, rhoE);
    const Real rhoEBack = state.conservedTotalEnergy(c.rho, out.temperature, out.velocity);
    EXPECT_NEAR(rhoEBack, rhoE, 8.0 * p17_5::kEps * std::abs(rhoE));
  }
}

// C7 -- the gamma pressure form is a VERIFICATION identity, gated on the zero reference.
TEST(TotalEnergyConversionTest, GammaPressureFormAgreesUnderTheZeroReference) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  ASSERT_TRUE(state.hasZeroReference());
  const Real gamma = state.thermodynamics().specificHeatRatio();
  const Real rho = 1.2;
  const Real T = 500.0;
  const Vector2 U{80.0, 30.0};
  const Real rhoE = state.conservedTotalEnergy(rho, T, U);
  const Real pGamma = (gamma - 1.0) * (rhoE - 0.5 * rho * dot(U, U));
  const Real pEos = state.thermodynamics().equationOfState().pressure(rho, T);
  EXPECT_NEAR(pGamma, pEos, 4.0 * p17_5::kEps * std::abs(pEos));

  // And under a SHIFTED reference the same form is wrong by far more than any tolerance -- which is
  // why it is never used as the pressure path. Measured at 83% in logs/01_dryrun.log section F.
  const TotalEnergyState shifted = p17_5::shiftedReferenceState();
  const Real rhoEShifted = shifted.conservedTotalEnergy(rho, T, U);
  const Real pGammaShifted = (gamma - 1.0) * (rhoEShifted - 0.5 * rho * dot(U, U));
  EXPECT_GT(std::abs(pGammaShifted - pEos) / pEos, 0.1)
      << "the gamma form must be grossly wrong here, or this gate is not protecting anything";
}

// ===========================================================================
// Group E -- the conservative equation
// ===========================================================================

// E1 -- d(rho eTotal)/dt, not rho d(eTotal)/dt.
TEST(TotalEnergyEquationTest, TransientTermUsesSeparateOldAndNewDensity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cell volume 1
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Index n = mesh.numberOfCells();
  const Real dt = 0.25;

  // BOTH rho and eTotal change -- a constant-density test cannot tell the two forms apart.
  const Real rhoOld = 1.0, rhoNew = 2.0;
  const Real tOld = 300.0, tNew = 500.0;
  const Vector2 U{10.0, 0.0};

  ScalarField rhoEOld(n), rhoENew(n);
  for (Index i = 0; i < n; ++i) {
    rhoEOld[i] = state.conservedTotalEnergy(rhoOld, tOld, U);
    rhoENew[i] = state.conservedTotalEnergy(rhoNew, tNew, U);
  }

  const ScalarField accumulation =
      cfd::compressible::totalEnergyAccumulation(mesh, rhoEOld, rhoENew, dt);

  const Real expected = (rhoENew[0] - rhoEOld[0]) * 1.0 / dt;
  EXPECT_NEAR(accumulation[0], expected, 1e-6);

  // The WRONG form, rho_new (eTotal_new - eTotal_old) V/dt, must differ substantially.
  const Real eTotalOld = state.specificTotalEnergy(tOld, U);
  const Real eTotalNew = state.specificTotalEnergy(tNew, U);
  const Real wrong = rhoNew * (eTotalNew - eTotalOld) * 1.0 / dt;
  EXPECT_GT(std::abs(accumulation[0] - wrong), 1.0)
      << "the two transient forms must be distinguishable on this case";
}

// E3, E4 -- the convective flux carries pressure work, and matches (rhoE + p) U.
TEST(TotalEnergyEquationTest, ConvectiveFluxCarriesPressureWorkAndMatchesRhoEPlusP) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 5.0, 4.0);
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Index n = mesh.numberOfCells();
  const Real rho = 1.3;

  // Non-uniform temperature so hTotal varies and pressure work is genuinely non-zero.
  ScalarField temperature(n);
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    temperature[cell.id()] = 300.0 + 20.0 * cell.centroid().x;
    velocity[cell.id()] = Vector2{2.0, 0.0};
  }
  const ScalarField density(n, rho);
  const ScalarField hTotal =
      cfd::compressible::specificTotalEnthalpyField(mesh, temperature, velocity, state);
  const ScalarField rhoE =
      cfd::compressible::conservedTotalEnergyField(mesh, density, temperature, velocity, state);

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    massFlux[face.id()] = rho * (velocity[face.owner()].x * face.areaVector().x);
  }
  const SurfaceField hBoundary(mesh.numberOfFaces(), hTotal[0]);

  const ScalarField convective =
      cfd::compressible::totalEnergyConvectiveDivergence(mesh, massFlux, hTotal, hBoundary);

  // E3: on an INTERNAL face, m_dot * hTotal_upwind must equal (rhoE + p)_upwind * (U.n) A / rho.
  Index checked = 0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Real mdot = massFlux[face.id()];
    const Index up = (mdot >= 0.0) ? face.owner() : *face.neighbor();
    const Real p = state.thermodynamics().equationOfState().pressure(rho, temperature[up]);
    const Real viaHTotal = mdot * hTotal[up];
    const Real viaRhoEPlusP = (rhoE[up] + p) * (velocity[up].x * face.areaVector().x);
    EXPECT_NEAR(viaHTotal, viaRhoEPlusP, 8.0 * p17_5::kEps * std::abs(viaHTotal) + 1e-12);
    ++checked;
  }
  ASSERT_GT(checked, 0) << "no internal faces -- nothing was compared";

  // E4: dropping p from the flux, i.e. transporting rhoE/rho instead of hTotal, must CHANGE the
  // answer. This is the test that would fail if pressure work were forgotten.
  ScalarField eTotalOnly(n);
  for (Index i = 0; i < n; ++i) eTotalOnly[i] = rhoE[i] / rho;
  const ScalarField withoutPressure = cfd::compressible::totalEnergyConvectiveDivergence(
      mesh, massFlux, eTotalOnly, SurfaceField(mesh.numberOfFaces(), eTotalOnly[0]));
  Real worstRelative = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real scale = std::max(std::abs(convective[i]), 1.0);
    worstRelative = std::max(worstRelative, std::abs(convective[i] - withoutPressure[i]) / scale);
  }
  EXPECT_GT(worstRelative, 1e-6) << "pressure work is absent from the convective flux";
}

// E5, E6 -- conduction: magnitude AND sign against an analytical field, reusing the qualified
// operator rather than a private discretisation.
TEST(TotalEnergyEquationTest, ConductiveTermMatchesAnalyticalQuadraticTemperature) {
  // T = c x^2 gives div(k grad T) = 2 k c exactly on a uniform Cartesian mesh, because the
  // three-point second difference is exact for a quadratic. A LINEAR field would give exactly
  // zero and so could not test the SIGN, which is the one thing easy to get backwards here.
  const Index nx = 8;
  const Index ny = 4;
  const Real lx = 8.0;
  const Real ly = 4.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
  const Real h = lx / static_cast<Real>(nx);
  const Real k = 0.6;
  const Real c = 3.0;

  ScalarField temperature(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    temperature[cell.id()] = c * cell.centroid().x * cell.centroid().x;
  }
  const auto bcs = p17_5::zeroGradientScalar(mesh);

  const ScalarField conductive =
      cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, k, bcs);

  // Interior in x only: the first and last columns see a zero-gradient boundary instead of a
  // second neighbour. In y the field is constant, and zero-gradient makes that flux exactly zero.
  Index checked = 0;
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    if (x < h || x > lx - h) continue;
    // OUTFLOW convention: div(k grad T) = +2kc adds energy, so the outflow is NEGATIVE.
    const Real expected = -2.0 * k * c * cell.volume();
    EXPECT_NEAR(conductive[cell.id()], expected, 1e-9 * std::abs(expected))
        << "cell " << cell.id() << " at x = " << x;
    ++checked;
  }
  ASSERT_GT(checked, 0) << "no x-interior cells -- the mesh is too small to verify anything";

  // E6: a PRESCRIBED ZERO GRADIENT must give EXACTLY zero conductive flux. This is the property the
  // whole conduction implementation hinges on, and it is why discretization::diffusion is not used
  // here: that operator fits a quadratic through (boundary, owner, opposite-neighbour) and so
  // returns a NON-ZERO flux for a zero-gradient condition, which leaked 5.64 W out of a closed
  // "adiabatic" domain. Measured in logs/04_diagnose_g2.log; see AMENDMENT_conduction_boundary.md.
  //
  // A uniform temperature has zero gradient everywhere, so EVERY face must carry exactly zero.
  const ScalarField uniform(mesh.numberOfCells(), 350.0);
  const ScalarField uniformConduction =
      cfd::compressible::totalEnergyConductiveDivergence(mesh, uniform, k, bcs);
  for (const auto& cell : mesh.cells()) {
    EXPECT_EQ(uniformConduction[cell.id()], 0.0)
        << "cell " << cell.id() << ": a uniform temperature must conduct exactly nothing";
  }

  // And the generic operator does NOT have that property -- recorded here so the difference is a
  // measured fact in the suite rather than a claim in a comment.
  const ScalarField genericUniform = cfd::discretization::diffusion(mesh, uniform, k, bcs);
  Real genericWorst = 0.0;
  for (const auto& cell : mesh.cells()) {
    genericWorst = std::max(genericWorst, std::abs(genericUniform[cell.id()]));
  }
  EXPECT_EQ(genericWorst, 0.0) << "for a UNIFORM field even the quadratic fit gives zero; the "
                                  "divergence between the two operators appears only for a curved "
                                  "profile, which the closed-domain test exercises";
}

// E7
TEST(TotalEnergyEquationTest, BodyForcePowerIsExactlyZeroForZeroVelocity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 3.0, 3.0);
  const Index n = mesh.numberOfCells();
  const ScalarField density(n, 1.2);
  const VectorField still(n, Vector2{0.0, 0.0});
  const ScalarField zero =
      cfd::compressible::bodyForcePowerField(mesh, density, still, Vector2{0.0, -9.81});
  for (Index i = 0; i < n; ++i) EXPECT_EQ(zero[i], 0.0);

  // Non-vacuity: a moving field must give non-zero power, and it must scale with rho and with U.
  const VectorField moving(n, Vector2{0.0, -2.0});
  const ScalarField power =
      cfd::compressible::bodyForcePowerField(mesh, density, moving, Vector2{0.0, -9.81});
  EXPECT_GT(power[0], 0.0) << "falling with gravity must ADD energy";
  const ScalarField denser(n, 2.4);
  const ScalarField doubled =
      cfd::compressible::bodyForcePowerField(mesh, denser, moving, Vector2{0.0, -9.81});
  EXPECT_NEAR(doubled[0], 2.0 * power[0], 1e-12 * std::abs(power[0]));
}

// E8 -- a uniform source advances energy by exactly S_E V dt.
TEST(TotalEnergyEquationTest, VolumetricSourceAdvancesEnergyExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 4.0, 4.0);
  const Index n = mesh.numberOfCells();
  const Real sourceDensity = 250.0;  // W/m^3
  const Real dt = 0.02;

  const ScalarField rhoEOld(n, 3.0e5);
  const ScalarField noFlux(n, 0.0);
  ScalarField source(n);
  for (const auto& cell : mesh.cells()) source[cell.id()] = sourceDensity * cell.volume();

  const ScalarField rhoENew = cfd::compressible::advanceTotalEnergyExplicit(
      mesh, rhoEOld, noFlux, noFlux, nullptr, nullptr, &source, dt);

  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(rhoENew[i] - rhoEOld[i], sourceDensity * dt,
                8.0 * p17_5::kEps * std::abs(rhoEOld[i]));
  }
}

// E9
TEST(TotalEnergyEquationTest, ViscousWorkLimitationIsReportedNotHidden) {
  const auto limitation = cfd::compressible::viscousWorkLimitation();
  EXPECT_FALSE(limitation.stressTensorAvailable)
      << "P17.4 assembles mu lap(U), not tau_ij; claiming otherwise would over-state the model";
  ASSERT_NE(limitation.description, nullptr);
  EXPECT_GT(std::string(limitation.description).size(), 50u);
}

// ===========================================================================
// Group G -- global conservation
// ===========================================================================

// G1, G4
TEST(TotalEnergyConservationTest, BalanceReportsEveryTermAndAUniformStateIsAtRoundOff) {
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 5, 6.0, 5.0);
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Index n = mesh.numberOfCells();
  const Real rho = 1.3;
  const Real T = 350.0;
  const Vector2 U{4.0, 0.0};

  const ScalarField density(n, rho);
  const ScalarField temperature(n, T);
  const VectorField velocity(n, U);
  const ScalarField rhoE =
      cfd::compressible::conservedTotalEnergyField(mesh, density, temperature, velocity, state);
  const ScalarField hTotal =
      cfd::compressible::specificTotalEnthalpyField(mesh, temperature, velocity, state);

  // The flux a uniform field actually produces: net per cell is zero on a Cartesian mesh.
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    massFlux[face.id()] = rho * (U.x * face.areaVector().x);
  }
  const SurfaceField hBoundary(mesh.numberOfFaces(), hTotal[0]);
  const auto bcs = p17_5::zeroGradientScalar(mesh);

  const ScalarField accumulation =
      cfd::compressible::totalEnergyAccumulation(mesh, rhoE, rhoE, 0.1);
  const ScalarField convective =
      cfd::compressible::totalEnergyConvectiveDivergence(mesh, massFlux, hTotal, hBoundary);
  const ScalarField conductive =
      cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, 0.5, bcs);

  const TotalEnergyBalance balance = cfd::compressible::evaluateTotalEnergyBalance(
      mesh, accumulation, convective, conductive, nullptr, nullptr, nullptr);

  // G1: every term is reported. The absent ones are exactly 0, not folded into the imbalance.
  EXPECT_EQ(balance.accumulation, 0.0) << "rhoE_new == rhoE_old, so accumulation is exactly 0";
  EXPECT_EQ(balance.viscousWorkInflow, 0.0);
  EXPECT_EQ(balance.bodyForcePower, 0.0);
  EXPECT_EQ(balance.volumetricSource, 0.0);
  // G4: a uniform state has no net conduction and no net convection.
  EXPECT_TRUE(balance.withinBound())
      << "imbalance " << balance.imbalance << " exceeds bound " << balance.bound;
}

// G2 -- an adiabatic closed domain conserves total energy across many steps.
TEST(TotalEnergyConservationTest, AdiabaticClosedDomainConservesTotalEnergy) {
  const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 7.0, 5.0);
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Index n = mesh.numberOfCells();

  // A non-uniform state, so the fluxes are genuinely non-zero and the conservation is not trivial.
  ScalarField temperature(n);
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    temperature[cell.id()] = 320.0 + 15.0 * std::sin(cell.centroid().x);
    velocity[cell.id()] = Vector2{1.5 + 0.2 * cell.centroid().y, 0.0};
  }
  const ScalarField density(n, 1.25);
  ScalarField rhoE =
      cfd::compressible::conservedTotalEnergyField(mesh, density, temperature, velocity, state);

  const SurfaceField massFlux = p17_5::closedBoxIrregularFlux(mesh);
  const auto bcs = p17_5::zeroGradientScalar(mesh);
  const Real dt = 1.0e-4;
  const int steps = 20;

  const auto domainEnergy = [&](const ScalarField& field) {
    Real total = 0.0;
    for (const auto& cell : mesh.cells()) total += field[cell.id()] * cell.volume();
    return total;
  };
  const Real initial = domainEnergy(rhoE);

  for (int step = 0; step < steps; ++step) {
    const ScalarField hTotal =
        cfd::compressible::specificTotalEnthalpyField(mesh, temperature, velocity, state);
    const ScalarField convective = cfd::compressible::totalEnergyConvectiveDivergence(
        mesh, massFlux, hTotal, SurfaceField(mesh.numberOfFaces(), 0.0));
    const ScalarField conductive =
        cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, 0.4, bcs);
    rhoE = cfd::compressible::advanceTotalEnergyExplicit(mesh, rhoE, convective, conductive,
                                                         nullptr, nullptr, nullptr, dt);
    // Recover temperature so the next step's hTotal is consistent with the advanced energy.
    VectorField momentum(n);
    for (Index i = 0; i < n; ++i) {
      momentum[i] = Vector2{density[i] * velocity[i].x, density[i] * velocity[i].y};
    }
    const auto recovered =
        cfd::compressible::recoverPrimitiveFields(mesh, density, momentum, rhoE, state);
    temperature = recovered.temperature;
  }

  const Real finalEnergy = domainEnergy(rhoE);
  // The bound scales with the number of steps, the cell count and the magnitude -- never an
  // absolute constant. kAccum = 8 is EMPIRICAL and inherited from P17.1/P17.3/P17.4.
  const Real bound = cfd::compressible::kEnergyAccumulationFactor * p17_5::kEps *
                     static_cast<Real>(steps) * static_cast<Real>(n) * std::abs(initial);
  EXPECT_NEAR(finalEnergy, initial, bound)
      << "closed adiabatic domain: initial " << initial << " final " << finalEnergy << " drift "
      << (finalEnergy - initial) << " bound " << bound;
}

// G3
TEST(TotalEnergyConservationTest, UniformHeatingRaisesDomainEnergyByExactlySourceTimesVolumeDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 5.0, 5.0);
  const Index n = mesh.numberOfCells();
  const Real sourceDensity = 1000.0;
  const Real dt = 1.0e-3;
  const int steps = 10;

  ScalarField rhoE(n, 2.5e5);
  const ScalarField noFlux(n, 0.0);
  ScalarField source(n);
  Real domainVolume = 0.0;
  for (const auto& cell : mesh.cells()) {
    source[cell.id()] = sourceDensity * cell.volume();
    domainVolume += cell.volume();
  }

  const auto domainEnergy = [&](const ScalarField& field) {
    Real total = 0.0;
    for (const auto& cell : mesh.cells()) total += field[cell.id()] * cell.volume();
    return total;
  };
  const Real initial = domainEnergy(rhoE);

  for (int step = 0; step < steps; ++step) {
    rhoE = cfd::compressible::advanceTotalEnergyExplicit(mesh, rhoE, noFlux, noFlux, nullptr,
                                                         nullptr, &source, dt);
  }

  const Real expectedGain = sourceDensity * domainVolume * dt * static_cast<Real>(steps);
  const Real bound = cfd::compressible::kEnergyAccumulationFactor * p17_5::kEps *
                     static_cast<Real>(steps) * static_cast<Real>(n) * std::abs(initial);
  EXPECT_NEAR(domainEnergy(rhoE) - initial, expectedGain, bound);
}

// G5 -- interior convective faces telescope: the global sum is zero on a closed box for ANY field
// and ANY internal flux, while the per-cell values are O(1).
TEST(TotalEnergyConservationTest, InteriorConvectiveFacesTelescope) {
  const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 7.0, 5.0);
  const Index n = mesh.numberOfCells();

  ScalarField hTotal(n);
  for (const auto& cell : mesh.cells()) {
    hTotal[cell.id()] = 3.0e5 + 1.0e4 * std::sin(cell.centroid().x) + 500.0 * cell.centroid().y;
  }
  const SurfaceField massFlux = p17_5::closedBoxIrregularFlux(mesh);

  const ScalarField divergence = cfd::compressible::totalEnergyConvectiveDivergence(
      mesh, massFlux, hTotal, SurfaceField(mesh.numberOfFaces(), 0.0));

  Real total = 0.0;
  Real worstCell = 0.0;
  for (Index i = 0; i < n; ++i) {
    total += divergence[i];
    worstCell = std::max(worstCell, std::abs(divergence[i]));
  }
  // Non-vacuity: the per-cell values must be large, so a near-zero sum is real cancellation and
  // not an operator that is simply zero.
  EXPECT_GT(worstCell, 1.0e3) << "per-cell contributions are tiny -- the sum cancels nothing";
  const Real bound = cfd::compressible::kEnergyAccumulationFactor * p17_5::kEps *
                     static_cast<Real>(mesh.numberOfFaces()) * worstCell;
  EXPECT_LE(std::abs(total), bound)
      << "global sum " << total << " exceeds " << bound << " -- interior faces are not cancelling";

  // And with the box OPENED, the sum must NOT be zero -- otherwise the check above proves nothing.
  SurfaceField opened = massFlux;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      opened[face.id()] = 2.0;
      break;
    }
  }
  const ScalarField openedDivergence = cfd::compressible::totalEnergyConvectiveDivergence(
      mesh, opened, hTotal, SurfaceField(mesh.numberOfFaces(), 0.0));
  Real openedTotal = 0.0;
  for (Index i = 0; i < n; ++i) openedTotal += openedDivergence[i];
  EXPECT_GT(std::abs(openedTotal), bound)
      << "an unbalanced boundary flux must break the global sum";
}

// ===========================================================================
// Group V -- invalid states. Each must THROW; nothing is clamped.
// ===========================================================================

TEST(TotalEnergyInvalidStateTest, RejectsNonPositiveOrNonFiniteDensity) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  EXPECT_THROW((void)state.conservedTotalEnergy(0.0, 300.0, Vector2{1.0, 0.0}),
               InvalidArgumentError);  // V1
  EXPECT_THROW((void)state.conservedTotalEnergy(-1.2, 300.0, Vector2{1.0, 0.0}),
               InvalidArgumentError);                                                      // V1
  EXPECT_THROW((void)state.recover(0.0, Vector2{0.0, 0.0}, 1.0e5), InvalidArgumentError);  // V1
  EXPECT_THROW(
      (void)state.recover(std::numeric_limits<Real>::quiet_NaN(), Vector2{0.0, 0.0}, 1.0e5),
      InvalidArgumentError);  // V1
}

TEST(TotalEnergyInvalidStateTest, RejectsNonFiniteConservedEnergyOrMomentum) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  EXPECT_THROW((void)state.recover(1.2, Vector2{0.0, 0.0}, std::numeric_limits<Real>::quiet_NaN()),
               InvalidArgumentError);  // V2
  EXPECT_THROW((void)state.recover(1.2, Vector2{0.0, 0.0}, std::numeric_limits<Real>::infinity()),
               InvalidArgumentError);  // V2
  EXPECT_THROW(
      (void)state.recover(1.2, Vector2{std::numeric_limits<Real>::quiet_NaN(), 0.0}, 1.0e5),
      InvalidArgumentError);  // V2
}

// V3, V6 -- the physically important one. Kinetic energy exceeding the total must THROW rather than
// silently produce a negative internal energy, a negative temperature and a NaN pressure.
TEST(TotalEnergyInvalidStateTest, RejectsKineticEnergyExceedingTotalEnergy) {
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Real rho = 1.2;
  const Vector2 U{500.0, 0.0};  // ke = 125000
  const Real ke = TotalEnergyState::specificKineticEnergy(U);
  // A conserved energy BELOW the kinetic content: physically impossible.
  const Real rhoE = rho * (ke * 0.5);
  EXPECT_THROW((void)state.recover(rho, Vector2{rho * U.x, rho * U.y}, rhoE), NumericalError);

  // Non-vacuity: the SAME call with enough energy must succeed, so the throw is caused by the
  // deficit and not by the fixture.
  const Real valid = rho * (ke + state.specificInternalEnergy(400.0));
  EXPECT_NO_THROW((void)state.recover(rho, Vector2{rho * U.x, rho * U.y}, valid));
}

// V4 -- an out-of-range recovered temperature is an ERROR, never a clamp.
TEST(TotalEnergyInvalidStateTest, RejectsOutOfRangeRecoveredTemperature) {
  // A narrow validity window, so exceeding it is easy to construct.
  const TemperatureDependentProperty cp(std::vector<Real>{p17_5::kCp}, 200.0, 2000.0, "cp");
  const TotalEnergyState state(ThermodynamicProperties(p17_5::kR, p17_5::kCp),
                               SensibleEnthalpy(cp, 200.0, 0.0));
  const Real rho = 1.0;
  // An energy corresponding to far more than 2000 K.
  const Real rhoE = rho * state.specificInternalEnergy(1999.0) * 5.0;
  EXPECT_THROW((void)state.recover(rho, Vector2{0.0, 0.0}, rhoE), cfd::Error);

  // In range, it must succeed -- the window is real, not a blanket rejection.
  EXPECT_NO_THROW(
      (void)state.recover(rho, Vector2{0.0, 0.0}, rho * state.specificInternalEnergy(1000.0)));
}

// V5
TEST(TotalEnergyInvalidStateTest, RejectsNonFiniteConductivityAndBadSizes) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 3.0, 3.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const auto bcs = p17_5::zeroGradientScalar(mesh);

  EXPECT_THROW((void)cfd::compressible::totalEnergyConductiveDivergence(
                   mesh, temperature, std::numeric_limits<Real>::quiet_NaN(), bcs),
               InvalidArgumentError);
  EXPECT_THROW(
      (void)cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, 0.0, bcs),
      InvalidArgumentError);
  EXPECT_THROW(
      (void)cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, -1.0, bcs),
      InvalidArgumentError);

  const ScalarField wrongSize(n + 1, 300.0);
  EXPECT_THROW((void)cfd::compressible::totalEnergyConductiveDivergence(mesh, wrongSize, 0.5, bcs),
               InvalidArgumentError);

  const ScalarField rhoE(n, 1.0e5);
  EXPECT_THROW((void)cfd::compressible::totalEnergyAccumulation(mesh, rhoE, rhoE, 0.0),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::compressible::totalEnergyAccumulation(mesh, rhoE, rhoE, -1.0),
               InvalidArgumentError);
}

// V6 -- the field-level path names the offending cell instead of returning a quietly repaired
// field.
TEST(TotalEnergyInvalidStateTest, FieldRecoveryNamesTheOffendingCellAndClampsNothing) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 3.0, 2.0);
  const TotalEnergyState state = p17_5::zeroReferenceState();
  const Index n = mesh.numberOfCells();
  const ScalarField density(n, 1.2);
  const VectorField momentum(n, Vector2{0.0, 0.0});
  ScalarField rhoE(n, 1.2 * state.specificInternalEnergy(400.0));
  rhoE[2] = -1.0;  // below the admissible floor for the zero reference

  try {
    (void)cfd::compressible::recoverPrimitiveFields(mesh, density, momentum, rhoE, state);
    FAIL() << "an inadmissible cell must not be silently repaired";
  } catch (const cfd::Error& error) {
    EXPECT_NE(std::string(error.what()).find("cell 2"), std::string::npos)
        << "the message must name the offending cell, got: " << error.what();
  }
}
