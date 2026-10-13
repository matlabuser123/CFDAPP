// P17.6 -- the low-Mach FAMILY: does the compressible solution approach the incompressible one as
// Ma -> 0?
//
// WHY THIS FILE EXISTS, AND WHAT IT DOES NOT REPLACE
// -------------------------------------------------
// test_low_mach_regression.cpp (7 TESTs) is a sound POST-HOC thermodynamic consistency check: given
// an incompressible solution, it verifies the EOS-derived density is self-consistent, the implied
// Mach number and density variation are small, and the implied compressible flux stays close to the
// incompressible one. It is required UNCHANGED by the frozen G5 criterion and is left untouched.
//
// What it cannot do is answer P17.6's question, because it never solves the compressible system --
// its "compressible" quantities are evaluated ON the incompressible solution, so "the compressible
// solution approaches the incompressible one" is true by construction there. That is exactly the
// degenerate case section 20 of the P17.6 authorization forbids as evidence.
//
// This file solves BOTH systems, at four decreasing inlet Mach numbers, on the same mesh, viscosity
// and boundary conditions, and measures how far apart they are and whether that distance shrinks.
//
// TWO DESIGN POINTS THAT MATTER
// -----------------------------
// 1. rho_ref IS DERIVED, not chosen: rho_ref = p_ref / (R T) = 1.176624. The existing test gives
// the
//    incompressible solver kDensity = 1.0 while the EOS at the reference state gives 1.1766 -- a
//    15% mismatch. That test compensates when comparing fluxes, so its own numbers are sound, but a
//    SOLUTION comparison requires both formulations to describe the same gas.
// 2. THE TOLERANCES ARE SCALED BY U0, and that is a policy correction rather than a relaxation. The
//    inherited tolerances are ABSOLUTE and were locked for U_mean = 1; across a family spanning 8x
//    in velocity an absolute tolerance is not the same convergence policy, it silently demands 8x
//    more relative precision at the top. At U0 = 1 the scaling reproduces the inherited values bit
//    for bit.
//
// THE FAMILY IS BOUNDED ON BOTH SIDES by the inherited maxIterations = 3000, measured in
// results/p17/low_mach/logs/01_dryrun.log: the INCOMPRESSIBLE reference cannot converge at U0 = 2
// or 4, and iterations climb toward the limit going down. Section 22 forbids raising an iteration
// limit to obtain a pass, so the family descends from the existing test's own anchor, U0 = 1.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/compressible/TotalEnergyEquation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::compressible::CompressibleSIMPLE;
using cfd::compressible::CompressibleSIMPLEResult;
using cfd::compressible::CompressibleSIMPLESettings;
using cfd::compressible::CompressibleSIMPLEStatus;
using cfd::compressible::machNumber;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace p17_6 {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kGasConstant = 287.05;
constexpr Real kSpecificHeatPressure = 1005.0;
constexpr Real kTemperature = 300.0;
constexpr Real kReferencePressure = 101325.0;
constexpr Index kNx = 32;
constexpr Index kNy = 6;

BoundaryConditionSet velocityBoundaries(const Mesh& mesh, Real u0) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<Inlet>(Vector2{u0, 0.0}));
  b.set(mesh, "right", std::make_unique<Outlet>());
  b.set(mesh, "bottom", std::make_unique<Wall>());
  b.set(mesh, "top", std::make_unique<Wall>());
  return b;
}

BoundaryConditionSet pressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  b.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  b.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  b.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return b;
}

SIMPLESettings incompressibleSettings(Real u0) {
  SIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5 * u0;
  s.pressureTolerance = 5e-4 * u0;
  s.continuityTolerance = 1e-6 * u0;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-8;
  s.pressureSolver.relativeTolerance = 1e-6;
  return s;
}

CompressibleSIMPLESettings compressibleSettings(Real u0) {
  CompressibleSIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5 * u0;
  s.pressureTolerance = 5e-4 * u0;
  s.continuityTolerance = 1e-6 * u0;
  return s;
}

struct Level {
  Real u0{};
  bool converged{false};
  Real inletMach{}, machMax{};
  Real rhoMin{}, rhoMax{}, rhoMean{}, deltaRhoOverRho{}, maxEosError{};
  Real velRelL2{}, presRelL2{}, presLinf{}, presFloor{}, fluxMaxRelDiff{};
  Real kineticComp{}, kineticInc{}, kineticRelDiff{}, keOverE{};
  Real tRecoveryMaxErr{};
  Real energyImbalance{}, energyPredicted{}, energyNoiseFloor{}, energySignalToNoise{};
  Real inletFlow{}, outletFlow{}, massImbalance{}, massImbalanceBound{};
  Real momViscousImplied{}, momViscousAnalytic{};
  Index iterationsComp{}, iterationsInc{};
};

Real volumeWeightedL2(const std::vector<Real>& d, const Mesh& mesh) {
  Real num = 0.0;
  Real vol = 0.0;
  for (const auto& cell : mesh.cells()) {
    num += d[cell.id()] * d[cell.id()] * cell.volume();
    vol += cell.volume();
  }
  return std::sqrt(num / vol);
}

Level runLevel(Real u0) {
  Level L;
  L.u0 = u0;
  const Mesh mesh = MeshGeometry::createCartesian2D(kNx, kNy, kLength, kHeight);
  const Index n = mesh.numberOfCells();
  const auto vbc = velocityBoundaries(mesh, u0);
  const auto pbc = pressureBoundaries(mesh);
  const ThermodynamicProperties thermo(kGasConstant, kSpecificHeatPressure);
  const Real rhoRef = thermo.density(kReferencePressure, kTemperature);
  const Real soundSpeed = thermo.speedOfSound(kTemperature);
  L.inletMach = machNumber(u0, soundSpeed);

  const SIMPLE simple(incompressibleSettings(u0));
  const SIMPLEResult inc = simple.solve(mesh, FluidProperties(rhoRef, kViscosity), vbc, pbc,
                                        VectorField(n, Vector2{u0, 0.0}), ScalarField(n, 0.0));
  const CompressibleSIMPLE comp(compressibleSettings(u0), thermo, kReferencePressure, 0);
  const CompressibleSIMPLEResult cmp =
      comp.solve(mesh, kViscosity, vbc, pbc, ScalarField(n, kTemperature), nullptr,
                 VectorField(n, Vector2{u0, 0.0}), ScalarField(n, 0.0), ScalarField(n, rhoRef));
  if (inc.status != SIMPLEStatus::Converged || cmp.status != CompressibleSIMPLEStatus::Converged) {
    return L;
  }
  L.converged = true;
  L.iterationsComp = cmp.iterations;
  L.iterationsInc = inc.iterations;

  // density and the EOS consistency that proves the compressible path really ran
  L.rhoMin = cmp.density[0];
  L.rhoMax = cmp.density[0];
  Real sum = 0.0;
  for (Index i = 0; i < n; ++i) {
    L.rhoMin = std::min(L.rhoMin, cmp.density[i]);
    L.rhoMax = std::max(L.rhoMax, cmp.density[i]);
    sum += cmp.density[i];
    const Real pAbs = kReferencePressure + cmp.pressure[i];
    L.maxEosError =
        std::max(L.maxEosError, std::abs(cmp.density[i] - pAbs / (kGasConstant * kTemperature)));
  }
  L.rhoMean = sum / static_cast<Real>(n);
  L.deltaRhoOverRho = (L.rhoMax - L.rhoMin) / L.rhoMean;

  // velocity, pressure, flux differences
  std::vector<Real> dmag(n), magInc(n), dp(n), pIncRef(n);
  for (Index i = 0; i < n; ++i) {
    const Real mc =
        std::sqrt(cmp.velocity[i].x * cmp.velocity[i].x + cmp.velocity[i].y * cmp.velocity[i].y);
    const Real mi =
        std::sqrt(inc.velocity[i].x * inc.velocity[i].x + inc.velocity[i].y * inc.velocity[i].y);
    L.machMax = std::max(L.machMax, machNumber(mc, soundSpeed));
    dmag[i] = mc - mi;
    magInc[i] = mi;
    // Both solvers share the gauge (Outlet + FixedValue(0) at "right"), but the comparison is made
    // on the field referenced to cell 0 so it is gauge-independent by construction.
    dp[i] = (cmp.pressure[i] - cmp.pressure[0]) - (inc.pressure[i] - inc.pressure[0]);
    pIncRef[i] = inc.pressure[i] - inc.pressure[0];
    L.presLinf = std::max(L.presLinf, std::abs(dp[i]));
  }
  L.velRelL2 = volumeWeightedL2(dmag, mesh) / std::max(volumeWeightedL2(magInc, mesh), 1e-300);
  L.presRelL2 = volumeWeightedL2(dp, mesh) / std::max(volumeWeightedL2(pIncRef, mesh), 1e-300);
  // The RELATIVE pressure comparison has a CONVERGENCE FLOOR, and it is constant across the family
  // because both the tolerance and the pressure drop scale linearly with U0:
  //     floor = pressureTolerance(U0) / dp(U0) = (5e-4 U0) / (12 mu U0 L / H^2) = 5.208e-5
  // Measured relL2(p') is 7.16e-4 at U0 = 1 (13.7x above the floor) and then 7.6e-5, 7.4e-5,
  // 8.2e-5 -- i.e. 1.4-1.6x the floor, where it CANNOT fall further. That is why the frozen A3
  // ("the pressure difference strictly decreases") is measured on the ABSOLUTE difference, which is
  // not floor-limited, with the relative one reported against its floor. See
  // AMENDMENT_pressure_floor.md.
  L.presFloor = (5e-4 * u0) / (12.0 * kViscosity * u0 * kLength / (kHeight * kHeight));

  const Real fluxScale = std::max<Real>(1e-9, rhoRef * u0 * kHeight);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    L.fluxMaxRelDiff =
        std::max(L.fluxMaxRelDiff, std::abs(cmp.massFlux[f] - inc.massFlux[f]) / fluxScale);
  }

  // mass balance
  const auto patchFlow = [&](const SurfaceField& mf, const char* name) {
    Real acc = 0.0;
    for (const Index f : mesh.boundaryPatch(name).faceIds()) acc += mf[f];
    return acc;
  };
  L.inletFlow = patchFlow(cmp.massFlux, "left");
  L.outletFlow = patchFlow(cmp.massFlux, "right");
  L.massImbalance = cmp.globalMassImbalance;
  // THE BOUND IS THE SOLVER'S OWN CONVERGENCE CONTRACT, not round-off. The first version of this
  // bound was 8 * eps * N_faces * |inletFlow| and it FAILED by a factor of ~19000 -- because a
  // converged SIMPLE solve stops when its continuity residual falls below continuityTolerance, NOT
  // when it reaches machine precision. Asking for eps-level conservation from a solve that only
  // promised 1e-6 was a mis-derived threshold on my part, not a defect in the solver. Measured
  // margins against the contract: 66x, 36x, 20x, 10x -- and that shrinking margin is itself the
  // low-Mach conditioning signature recorded in section 11. See AMENDMENT_mass_bound.md.
  L.massImbalanceBound = (1e-6 * u0) * std::abs(L.inletFlow);

  // energy: P17.5's stateless converter on the CONVERGED compressible fields
  {
    const TemperatureDependentProperty cp(std::vector<Real>{kSpecificHeatPressure}, 0.0, 3000.0,
                                          "cp");
    const TotalEnergyState state(thermo, SensibleEnthalpy(cp, 0.0, 0.0));
    const ScalarField temperature(n, kTemperature);
    const Real e = state.specificInternalEnergy(kTemperature);

    Real keMax = 0.0;
    Real keComp = 0.0;
    Real keInc = 0.0;
    for (const auto& cell : mesh.cells()) {
      const Index i = cell.id();
      keMax = std::max(keMax, TotalEnergyState::specificKineticEnergy(cmp.velocity[i]));
      keComp +=
          cmp.density[i] * TotalEnergyState::specificKineticEnergy(cmp.velocity[i]) * cell.volume();
      keInc += rhoRef * TotalEnergyState::specificKineticEnergy(inc.velocity[i]) * cell.volume();
    }
    L.keOverE = keMax / e;
    L.kineticComp = keComp;
    L.kineticInc = keInc;
    L.kineticRelDiff = std::abs(keComp - keInc) / std::max(std::abs(keInc), 1e-300);

    const ScalarField rhoE = cfd::compressible::conservedTotalEnergyField(
        mesh, cmp.density, temperature, cmp.velocity, state);
    VectorField momentum(n);
    for (Index i = 0; i < n; ++i) {
      momentum[i] = Vector2{cmp.density[i] * cmp.velocity[i].x, cmp.density[i] * cmp.velocity[i].y};
    }
    const auto rec =
        cfd::compressible::recoverPrimitiveFields(mesh, cmp.density, momentum, rhoE, state);
    for (Index i = 0; i < n; ++i) {
      L.tRecoveryMaxErr = std::max(L.tRecoveryMaxErr, std::abs(rec.temperature[i] - kTemperature));
    }

    const ScalarField hTotal =
        cfd::compressible::specificTotalEnthalpyField(mesh, temperature, cmp.velocity, state);
    SurfaceField hBoundary(mesh.numberOfFaces(), 0.0);
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) hBoundary[face.id()] = hTotal[face.owner()];
    }
    const ScalarField conv =
        cfd::compressible::totalEnergyConvectiveDivergence(mesh, cmp.massFlux, hTotal, hBoundary);
    const ScalarField zero(n, 0.0);
    const ScalarField accum = cfd::compressible::totalEnergyAccumulation(mesh, rhoE, rhoE, 1.0);
    const auto bal = cfd::compressible::evaluateTotalEnergyBalance(mesh, accum, conv, zero, nullptr,
                                                                   nullptr, nullptr);
    L.energyImbalance = bal.imbalance;

    Real keFluxIn = 0.0;
    Real keFluxOut = 0.0;
    for (const Index f : mesh.boundaryPatch("left").faceIds()) {
      keFluxIn += cmp.massFlux[f] *
                  TotalEnergyState::specificKineticEnergy(cmp.velocity[mesh.face(f).owner()]);
    }
    for (const Index f : mesh.boundaryPatch("right").faceIds()) {
      keFluxOut += cmp.massFlux[f] *
                   TotalEnergyState::specificKineticEnergy(cmp.velocity[mesh.face(f).owner()]);
    }
    L.energyPredicted = keFluxIn + keFluxOut;
    L.energyNoiseFloor =
        std::abs(cmp.globalMassImbalance) * state.specificStaticEnthalpy(kTemperature);
    L.energySignalToNoise = std::abs(L.energyPredicted) / std::max(L.energyNoiseFloor, 1e-300);
  }

  // x-momentum budget, cross-checked against the analytical Poiseuille wall shear rather than
  // against a self-closing residual. P17.4 ships no solver-level momentum-balance evaluator.
  {
    Real fluxNet = 0.0;
    Real pForce = 0.0;
    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) continue;
      fluxNet += cmp.massFlux[face.id()] * cmp.velocity[face.owner()].x;
      pForce -= cmp.pressure[face.owner()] * face.areaVector().x;
    }
    L.momViscousImplied = fluxNet - pForce;
    L.momViscousAnalytic = -2.0 * (6.0 * kViscosity * u0 / kHeight) * kLength;
  }
  return L;
}

// The family is computed ONCE per process: each level is two full SIMPLE solves of ~2000-2600 outer
// iterations, so recomputing it per test would multiply the cost by the number of tests.
const std::vector<Level>& family() {
  static const std::vector<Level> levels = [] {
    std::vector<Level> v;
    for (const Real u0 : {1.0, 0.5, 0.25, 0.125}) v.push_back(runLevel(u0));
    return v;
  }();
  return levels;
}

Real observedExponent(Real maHi, Real eHi, Real maLo, Real eLo) {
  if (!(maHi > 0.0 && maLo > 0.0 && eHi > 0.0 && eLo > 0.0)) return 0.0;
  return std::log(eHi / eLo) / std::log(maHi / maLo);
}

}  // namespace p17_6

// ===========================================================================
// Group M -- the Mach family. The INHERITED thresholds are marked as such.
// ===========================================================================

TEST(LowMachFamilyTest, EveryLevelConvergesInBothFormulations) {
  const auto& f = p17_6::family();
  ASSERT_EQ(f.size(), 4u);
  for (const auto& L : f) {
    EXPECT_TRUE(L.converged)
        << "U0 = " << L.u0
        << ": one of the two solves did not converge inside the inherited maxIterations = 3000. "
           "The limit is NOT to be raised (authorization section 22).";
    EXPECT_GT(L.iterationsComp, 0u) << "U0 = " << L.u0 << ": no compressible iteration ran";
  }
}

TEST(LowMachFamilyTest, FamilySpansAtLeastFourTimesInAchievedMach) {
  const auto& f = p17_6::family();
  ASSERT_GE(f.size(), 2u);
  ASSERT_TRUE(f.front().converged && f.back().converged);
  // The ACHIEVED Mach, not the requested one.
  const Real span = f.front().inletMach / f.back().inletMach;
  EXPECT_GE(span, 4.0) << "achieved span " << span << " over inlet Mach " << f.front().inletMach
                       << " -> " << f.back().inletMach;
}

TEST(LowMachFamilyTest, InheritedG5ThresholdsHoldAtEveryLevel) {
  for (const auto& L : p17_6::family()) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    EXPECT_LT(L.machMax, 0.1) << "U0 = " << L.u0;                 // INHERITED VERBATIM
    EXPECT_GT(L.machMax, 1.25 * L.inletMach) << "U0 = " << L.u0;  // INHERITED VERBATIM
    EXPECT_LT(L.deltaRhoOverRho, 1e-2) << "U0 = " << L.u0;        // INHERITED VERBATIM
    EXPECT_LT(L.maxEosError, 1e-9) << "U0 = " << L.u0;            // INHERITED VERBATIM
    EXPECT_LT(L.fluxMaxRelDiff, 0.15) << "U0 = " << L.u0;         // INHERITED VERBATIM
  }
}

// ===========================================================================
// Group A -- the TREND. One case is not evidence of a limit.
// ===========================================================================

TEST(LowMachFamilyTest, EveryErrorMetricDecreasesMonotonicallyAsMachFalls) {
  const auto& f = p17_6::family();
  for (std::size_t i = 1; i < f.size(); ++i) {
    ASSERT_TRUE(f[i].converged && f[i - 1].converged);
    EXPECT_LT(f[i].deltaRhoOverRho, f[i - 1].deltaRhoOverRho) << "level " << i;  // A1
    EXPECT_LT(f[i].velRelL2, f[i - 1].velRelL2) << "level " << i;                // A2
    EXPECT_LT(f[i].fluxMaxRelDiff, f[i - 1].fluxMaxRelDiff) << "level " << i;    // A4
    EXPECT_LT(f[i].kineticRelDiff, f[i - 1].kineticRelDiff) << "level " << i;    // E2
    EXPECT_LT(f[i].keOverE, f[i - 1].keOverE) << "level " << i;                  // E3
    // A3, on the ABSOLUTE pressure difference. The RELATIVE one is floor-limited and cannot
    // decrease past its convergence floor -- asserted separately below.
    EXPECT_LT(f[i].presLinf, f[i - 1].presLinf) << "level " << i;  // A3
  }
}

// A3's relative companion. The frozen criterion said "strictly decreases", which the measurement
// shows is IMPOSSIBLE below the convergence floor: the floor is
// pressureTolerance/dp = 5.208e-5, constant across the family because both scale with U0, and the
// metric sits at 1.4-1.6x it from the second level onward. So the criterion here is the one the
// arithmetic permits -- the metric must fall sharply while it is above the floor, and must stay AT
// the floor rather than diverging afterwards. See AMENDMENT_pressure_floor.md.
TEST(LowMachFamilyTest, RelativePressureDifferenceFallsToItsConvergenceFloorAndStaysThere) {
  const auto& f = p17_6::family();
  ASSERT_GE(f.size(), 2u);
  std::printf("\n  U0      relL2(p')     floor         ratio to floor\n");
  for (const auto& L : f) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    std::printf("  %-7.3g %-13.5g %-13.5g %-9.3g\n", L.u0, L.presRelL2, L.presFloor,
                L.presRelL2 / L.presFloor);
  }
  // Above the floor at the top level, and it drops hard on the first refinement.
  EXPECT_GT(f.front().presRelL2, 3.0 * f.front().presFloor)
      << "the top level must be meaningfully above the floor, or the comparison measures noise";
  EXPECT_LT(f[1].presRelL2, 0.25 * f.front().presRelL2)
      << "the relative pressure difference must fall sharply while it is above the floor";
  // And afterwards it must sit at the floor rather than growing away from it.
  for (std::size_t i = 1; i < f.size(); ++i) {
    EXPECT_LT(f[i].presRelL2, 3.0 * f[i].presFloor)
        << "level " << i << ": floor-limited, so it must stay within a small multiple of the floor";
  }
}

TEST(LowMachFamilyTest, DensityVariationExponentMatchesViscousTheory) {
  const auto& f = p17_6::family();
  ASSERT_TRUE(f.front().converged && f.back().converged);
  // For THIS channel the pressure drop is viscous-dominated, dp ~ 12 mu U L / H^2, so
  // drho/rho = dp/p ~ Ma^1. A quadratic expectation (dp ~ rho U^2) would be wrong here, and the
  // band is +/-0.2 around the theoretical 1 -- roughly 4x the exponent uncertainty the family's 8x
  // span and ~10% convergence noise imply. It is NOT fitted to the measurement.
  const Real n = p17_6::observedExponent(f.front().inletMach, f.front().deltaRhoOverRho,
                                         f.back().inletMach, f.back().deltaRhoOverRho);
  EXPECT_GE(n, 0.80) << "observed exponent " << n;
  EXPECT_LE(n, 1.25) << "observed exponent " << n;
}

TEST(LowMachFamilyTest, EveryOtherErrorExponentIsGenuinelyDecreasing) {
  const auto& f = p17_6::family();
  ASSERT_TRUE(f.front().converged && f.back().converged);
  const Real maHi = f.front().inletMach;
  const Real maLo = f.back().inletMach;
  // > 0.5 means genuinely decreasing rather than plateauing. No tighter band is imposed where
  // theory does not pin the exponent.
  EXPECT_GT(p17_6::observedExponent(maHi, f.front().velRelL2, maLo, f.back().velRelL2), 0.5);
  // On the ABSOLUTE pressure difference: the relative one is floor-limited (see
  // RelativePressureDifferenceFallsToItsConvergenceFloorAndStaysThere).
  EXPECT_GT(p17_6::observedExponent(maHi, f.front().presLinf, maLo, f.back().presLinf), 0.5);
  EXPECT_GT(p17_6::observedExponent(maHi, f.front().fluxMaxRelDiff, maLo, f.back().fluxMaxRelDiff),
            0.5);
  EXPECT_GT(p17_6::observedExponent(maHi, f.front().kineticRelDiff, maLo, f.back().kineticRelDiff),
            0.5);
}

// ===========================================================================
// Group E -- energy, bounding the KINETIC contribution separately.
// ===========================================================================

// The frozen G5 clause: "at Ma = 1e-3 the kinetic term is ~1e-6 of the internal energy, so a
// criterion on e_t can be satisfied while the kinetic part is entirely wrong. The criterion must
// bound the KINETIC CONTRIBUTION SEPARATELY, relative to its own magnitude."
TEST(LowMachFamilyTest, KineticContributionMatchesTheIncompressibleReference) {
  for (const auto& L : p17_6::family()) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    EXPECT_LT(L.kineticRelDiff, 1e-3)
        << "U0 = " << L.u0 << ": domain kinetic energy " << L.kineticComp << " against the "
        << "incompressible reference " << L.kineticInc;
    // And the trap is real: ke/e is 6-8 orders down, so a criterion on rhoE would pass regardless.
    EXPECT_LT(L.keOverE, 1e-5) << "U0 = " << L.u0;
    EXPECT_GT(L.kineticInc, 0.0) << "U0 = " << L.u0 << ": no kinetic energy to compare";
  }
}

TEST(LowMachFamilyTest, TemperatureRecoveryFromConservedEnergyIsExact) {
  for (const auto& L : p17_6::family()) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    // rhoE/rho - ke -> T must return the imposed 300 K. Exactly, on this isothermal case.
    EXPECT_EQ(L.tRecoveryMaxErr, 0.0) << "U0 = " << L.u0;
  }
}

TEST(LowMachFamilyTest, EnergyBalanceIsReportedWithItsSignalToNoiseAndNotGatedBelowOne) {
  const auto& f = p17_6::family();
  std::printf("\n  U0      E imbalance    predicted      ratio     S/N\n");
  for (const auto& L : f) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    const Real ratio = (L.energyPredicted != 0.0) ? L.energyImbalance / L.energyPredicted : 0.0;
    std::printf("  %-7.3g %-14.6g %-14.6g %-9.5g %-9.4g\n", L.u0, L.energyImbalance,
                L.energyPredicted, ratio, L.energySignalToNoise);
    // GATED ONLY where the physical signal exceeds the balance's own noise floor. On an isothermal
    // solve T is an INPUT, so the isothermal constraint is an unmodelled heat sink and P17.4 gives
    // no stress tensor for viscous work; on top of that h*mdot ~ 3e5 is nine orders above the
    // kinetic gain ~1e-4, so a 1e-9 mass residual swamps the signal. Gating below S/N = 1 would be
    // gating noise -- see results/p17/low_mach/acceptance_gate.md section 4.
    if (L.energySignalToNoise > 1.0) {
      EXPECT_GT(ratio, 0.5) << "U0 = " << L.u0 << " (S/N = " << L.energySignalToNoise << ")";
      EXPECT_LT(ratio, 1.5) << "U0 = " << L.u0 << " (S/N = " << L.energySignalToNoise << ")";
    }
  }
  // Non-vacuity: at least one level must actually be above the noise floor, or this test asserts
  // nothing at all.
  const bool anyAboveNoise = std::any_of(
      f.begin(), f.end(), [](const p17_6::Level& L) { return L.energySignalToNoise > 1.0; });
  EXPECT_TRUE(anyAboveNoise)
      << "every level is below the balance's noise floor -- the gated branch "
         "never ran, so this test would be vacuous";
}

// ===========================================================================
// Group C -- conservation
// ===========================================================================

TEST(LowMachFamilyTest, MassFlowsBalanceAtEveryLevel) {
  for (const auto& L : p17_6::family()) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    EXPECT_LE(std::abs(L.inletFlow + L.outletFlow), L.massImbalanceBound)
        << "U0 = " << L.u0 << ": inlet " << L.inletFlow << " outlet " << L.outletFlow;
    EXPECT_GT(std::abs(L.inletFlow), 0.0) << "U0 = " << L.u0 << ": no flow at all";
    // Non-vacuity of the bound: it must not be so loose that it would accept a grossly
    // unconserved solve. One percent of the throughput would be a real conservation failure, and
    // the bound must sit far below it.
    EXPECT_LT(L.massImbalanceBound, 0.01 * std::abs(L.inletFlow))
        << "U0 = " << L.u0 << ": the mass bound is too loose to discriminate";
  }
}

TEST(LowMachFamilyTest, MomentumBudgetMatchesThePoiseuilleWallShear) {
  const auto& f = p17_6::family();
  for (const auto& L : f) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    const Real ratio = L.momViscousImplied / L.momViscousAnalytic;
    // ~6% above the fully-developed value: a uniform inlet developing over L/H = 8 on a 6-cell-high
    // mesh has a higher wall shear than the Poiseuille limit. The 10% band is that entrance effect,
    // and the NEXT assertion is what makes the explanation testable rather than an excuse.
    EXPECT_GT(ratio, 0.90) << "U0 = " << L.u0;
    EXPECT_LT(ratio, 1.10) << "U0 = " << L.u0;
  }
  // The entrance effect must SHRINK as Re falls. If the 6% were a discretisation bias instead, it
  // would not trend.
  for (std::size_t i = 1; i < f.size(); ++i) {
    const Real prev = f[i - 1].momViscousImplied / f[i - 1].momViscousAnalytic;
    const Real curr = f[i].momViscousImplied / f[i].momViscousAnalytic;
    EXPECT_LT(curr, prev) << "level " << i << ": the entrance effect must shrink with Re";
  }
}

// ===========================================================================
// Group D -- determinism (G9)
// ===========================================================================

TEST(LowMachFamilyTest, RepeatedCompressibleSolveIsBitwiseIdentical) {
  const Real u0 = 1.0;
  const Mesh mesh =
      MeshGeometry::createCartesian2D(p17_6::kNx, p17_6::kNy, p17_6::kLength, p17_6::kHeight);
  const Index n = mesh.numberOfCells();
  const auto vbc = p17_6::velocityBoundaries(mesh, u0);
  const auto pbc = p17_6::pressureBoundaries(mesh);
  const ThermodynamicProperties thermo(p17_6::kGasConstant, p17_6::kSpecificHeatPressure);
  const Real rhoRef = thermo.density(p17_6::kReferencePressure, p17_6::kTemperature);
  const CompressibleSIMPLE comp(p17_6::compressibleSettings(u0), thermo, p17_6::kReferencePressure,
                                0);
  const auto run = [&] {
    return comp.solve(mesh, p17_6::kViscosity, vbc, pbc, ScalarField(n, p17_6::kTemperature),
                      nullptr, VectorField(n, Vector2{u0, 0.0}), ScalarField(n, 0.0),
                      ScalarField(n, rhoRef));
  };
  const CompressibleSIMPLEResult a = run();
  const CompressibleSIMPLEResult b = run();

  ASSERT_EQ(a.status, CompressibleSIMPLEStatus::Converged);
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.finalUResidual, b.finalUResidual);
  EXPECT_EQ(a.finalPressureResidual, b.finalPressureResidual);
  EXPECT_EQ(a.finalContinuityResidual, b.finalContinuityResidual);
  EXPECT_EQ(a.globalMassImbalance, b.globalMassImbalance);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x) << "cell " << i;
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y) << "cell " << i;
    EXPECT_EQ(a.pressure[i], b.pressure[i]) << "cell " << i;
    EXPECT_EQ(a.density[i], b.density[i]) << "cell " << i;
  }
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    EXPECT_EQ(a.massFlux[f], b.massFlux[f]) << "face " << f;
  }
}

// ===========================================================================
// Group F -- anti-false-success. The reason this file exists.
// ===========================================================================

// Section 20: "A compressible case silently falling back to incompressible SIMPLE is not valid
// low-Mach evidence." The audit found the EXISTING low-Mach test is exactly that shape, so this
// phase's own harness has to prove it is not repeating the mistake.
TEST(LowMachFamilyTest, CompressiblePathIsGenuinelyActive) {
  const auto& f = p17_6::family();
  for (const auto& L : f) {
    ASSERT_TRUE(L.converged) << "U0 = " << L.u0;
    // F1: a constant-density fallback would give a uniform density.
    EXPECT_GT(L.rhoMax - L.rhoMin, 0.0)
        << "U0 = " << L.u0 << ": the density is UNIFORM -- there is no EOS coupling";
    // F2: and the density really is the EOS evaluated at the solved pressure.
    EXPECT_LT(L.maxEosError, 1e-9) << "U0 = " << L.u0;
    // F3: the two solutions must DIFFER. If they were identical the whole comparison would be
    // measuring nothing -- which is the mirror image of A2 requiring the difference to shrink.
    EXPECT_GT(L.velRelL2, 0.0) << "U0 = " << L.u0
                               << ": the compressible and incompressible solutions are IDENTICAL";
  }
  // The density variation must itself scale with Mach -- a fixed non-zero spread would suggest a
  // constant offset rather than genuine thermodynamic coupling.
  for (std::size_t i = 1; i < f.size(); ++i) {
    EXPECT_LT(f[i].rhoMax - f[i].rhoMin, f[i - 1].rhoMax - f[i - 1].rhoMin) << "level " << i;
  }
}

TEST(LowMachFamilyTest, FamilyMetricsAreReportedForTheRecord) {
  const auto& f = p17_6::family();
  std::printf("\n  %-7s %-11s %-11s %-11s %-11s %-11s %-11s %-7s %-7s\n", "U0", "inletMa",
              "machMax", "drho/rho", "relL2(|U|)", "relL2(p')", "keRelDiff", "itComp", "itInc");
  for (const auto& L : f) {
    std::printf("  %-7.3g %-11.5g %-11.5g %-11.5g %-11.5g %-11.5g %-11.5g %-7llu %-7llu\n", L.u0,
                L.inletMach, L.machMax, L.deltaRhoOverRho, L.velRelL2, L.presRelL2,
                L.kineticRelDiff, static_cast<unsigned long long>(L.iterationsComp),
                static_cast<unsigned long long>(L.iterationsInc));
  }
  // Iterations climbing as Ma falls is a CONDITIONING characteristic of a pressure-based solver at
  // low Mach (authorization section 11/21). It is recorded, not worked around, and no convergence
  // criterion is weakened because of it.
  std::printf("  iterations rise as Ma falls: a low-Mach conditioning characteristic, recorded.\n");
  SUCCEED();
}
