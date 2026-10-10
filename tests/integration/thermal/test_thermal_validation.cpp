// P15 -- the thermal validation suite: cases A-G of the brief.
//
// These SOLVE against closed-form references, they do not merely assemble.
//
// ONE THING LEARNED IN P14 GOVERNS THE WHOLE FILE: the Dirichlet boundary
// treatment (P12-DIFF-002) is a DEFERRED second-order reconstruction evaluated
// from the CURRENT field, so the assembled system depends on the iterate it was
// built around. Assembling once about T = 0 and solving gives a badly wrong
// answer (measured in P14 at max error 231 against an analytical range of 400).
// Production iterates; so does solveToConvergence below.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/ConvectiveHeatTransfer.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ConvectiveHeatTransfer;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;
using cfd::thermal::ThermalTimeState;

namespace {

struct Slab {
  Mesh mesh;
  BoundaryConditionSet boundaries;
};

// A 1D slab on a 2D mesh: Dirichlet ends, adiabatic sides.
Slab makeSlab(Index nx, Real length, Real tLeft, Real tRight) {
  Slab slab{MeshGeometry::createCartesian2D(nx, 1, length, 1.0), {}};
  slab.boundaries.set(slab.mesh, "left", std::make_unique<FixedTemperature>(tLeft));
  slab.boundaries.set(slab.mesh, "right", std::make_unique<FixedTemperature>(tRight));
  slab.boundaries.set(slab.mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  slab.boundaries.set(slab.mesh, "top", std::make_unique<FixedGradient>(0.0));
  return slab;
}

Vector solveOnce(const cfd::algebra::LinearSystem& system, Index n) {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-14;
  settings.relativeTolerance = 1e-13;
  settings.maxIterations = 5000;
  const BiCGSTAB solver(settings);
  const auto result = solver.solve(system, Vector(n, 0.0));
  EXPECT_EQ(result.status, cfd::algebra::SolverStatus::Converged)
      << "linear solve did not converge (residual " << result.finalResidual << ")";
  return result.solution;
}

// Outer (deferred-correction) iteration. Convergence is RELATIVE to the field's
// own magnitude: an absolute 1e-12 on a field of order 500 is about nine ULP
// and unreachable -- a trap P14 fell into and this avoids by construction.
ScalarField solveToConvergence(const Mesh& mesh, const BoundaryConditionSet& boundaries,
                               const ScalarField& conductivity, const ScalarField& specificHeat,
                               const SurfaceField& massFlux, Real source,
                               const ScalarField& initial, int maxOuter = 300,
                               Real tolerance = 1e-12) {
  const Index n = mesh.numberOfCells();
  ScalarField temperature = initial;
  for (int iteration = 0; iteration < maxOuter; ++iteration) {
    const auto assembly = cfd::thermal::assembleEnergyEquation(
        mesh, temperature, massFlux, conductivity, specificHeat, boundaries, source, {});
    const Vector next = solveOnce(assembly.system, n);
    Real change = 0.0;
    Real magnitude = 0.0;
    for (Index i = 0; i < n; ++i) {
      change = std::max(change, std::abs(next[i] - temperature[i]));
      magnitude = std::max(magnitude, std::abs(next[i]));
    }
    for (Index i = 0; i < n; ++i) temperature[i] = next[i];
    if (change < tolerance * std::max(1.0, magnitude)) return temperature;
  }
  ADD_FAILURE() << "the deferred-correction outer iteration did not converge";
  return temperature;
}

Real centreX(const Mesh& mesh, Index cell) { return mesh.cell(cell).centroid().x; }

// Relative to the field's own magnitude.
//
// An ABSOLUTE bound is the wrong instrument for a temperature field: 1e-9 on a
// 500 K field is 2e-12 relative, below what the linear solver's own 1e-13
// relative tolerance can deliver once the outer deferred-correction iteration
// has amplified it. Case A failed at 1.390e-9 for exactly that reason -- the
// physics was right and the yardstick was wrong. Same class of error as P14's
// absolute convergence criterion.
Real maxRelativeError(const Mesh& mesh, const ScalarField& numerical,
                      const std::function<Real(Real)>& exact) {
  Real worst = 0.0;
  Real scale = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    worst = std::max(worst, std::abs(numerical[i] - exact(mesh.cell(i).centroid().x)));
    scale = std::max(scale, std::abs(numerical[i]));
  }
  return scale > 0.0 ? worst / scale : worst;
}

// The physically correct face mass flux for a uniform flow of speed `u` in +x.
//
//   massFlux = rho (u . S_f)
//
// A boundary face's area vector points OUTWARD, so for a left-to-right flow
// the LEFT boundary carries a NEGATIVE flux (inflow) and the right a positive
// one. Setting a single positive value on every x-normal face -- as the first
// version of Case D did -- makes the inlet read as an outflow, so the inlet
// temperature is never applied and the field collapses (measured: 6.56 K where
// 300-400 was expected). Deriving it from the face's own area vector is the
// definition, not a workaround.
SurfaceField uniformFlowMassFlux(const Mesh& mesh, Real density, Real speedX) {
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    flux[f] = density * speedX * mesh.face(f).areaVector().x;
  }
  return flux;
}

Real maxError(const Mesh& mesh, const ScalarField& numerical,
              const std::function<Real(Real)>& exact) {
  Real worst = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    worst = std::max(worst, std::abs(numerical[i] - exact(centreX(mesh, i))));
  }
  return worst;
}

}  // namespace

// --- Case A: steady 1D conduction -------------------------------------------

TEST(ThermalValidation, CaseASteadyConductionIsLinear) {
  // d2T/dx2 = 0, T(0)=300, T(L)=500  ->  T(x) = 300 + 200 x/L, exactly
  // reproduced by a two-point central operator on a uniform mesh.
  constexpr Index nx = 25;
  constexpr Real length = 0.5;
  constexpr Real tLeft = 300.0;
  constexpr Real tRight = 500.0;
  auto slab = makeSlab(nx, length, tLeft, tRight);
  const Index n = slab.mesh.numberOfCells();

  const ScalarField k(n, 15.0), cp(n, 900.0);
  const SurfaceField noFlow(slab.mesh.numberOfFaces(), 0.0);
  const ScalarField initial(n, 0.0);

  const ScalarField T = solveToConvergence(slab.mesh, slab.boundaries, k, cp, noFlow, 0.0, initial);

  const Real error =
      maxRelativeError(slab.mesh, T, [&](Real x) { return tLeft + (tRight - tLeft) * x / length; });
  // 1e-11 RELATIVE. Tighter than an absolute 1e-9 would be for an order-one
  // field, and reachable for this one; an absolute bound is not.
  EXPECT_LT(error, 1e-11) << "max relative error " << error;
}

TEST(ThermalValidation, CaseAMeshRefinementDoesNotDegrade) {
  for (const Index nx : {10, 20, 40, 80}) {
    auto slab = makeSlab(nx, 1.0, 300.0, 400.0);
    const Index n = slab.mesh.numberOfCells();
    const ScalarField T =
        solveToConvergence(slab.mesh, slab.boundaries, ScalarField(n, 5.0), ScalarField(n, 1000.0),
                           SurfaceField(slab.mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, 0.0));
    const Real error = maxRelativeError(slab.mesh, T, [](Real x) { return 300.0 + 100.0 * x; });
    EXPECT_LT(error, 1e-11) << "nx = " << nx << ", relative error " << error;
  }
}

// --- Case B: internal heat generation ----------------------------------------

TEST(ThermalValidation, CaseBHeatGenerationIsParabolic) {
  // k d2T/dx2 + q = 0, T(0)=T(L)=T0  ->  T = T0 + q x (L-x) / (2k)
  constexpr Index nx = 50;
  constexpr Real length = 1.0;
  constexpr Real kValue = 4.0;
  constexpr Real q = 1000.0;
  constexpr Real tWall = 300.0;
  auto slab = makeSlab(nx, length, tWall, tWall);
  const Index n = slab.mesh.numberOfCells();

  const ScalarField T =
      solveToConvergence(slab.mesh, slab.boundaries, ScalarField(n, kValue), ScalarField(n, 1000.0),
                         SurfaceField(slab.mesh.numberOfFaces(), 0.0), q, ScalarField(n, tWall));

  const Real error =
      maxError(slab.mesh, T, [&](Real x) { return tWall + q * x * (length - x) / (2.0 * kValue); });
  // h^2/8 * |T''| is the cell-centre quadrature error of a quadratic:
  // |T''| = q/k = 250, h = 1/50 -> 250 * (0.02)^2 / 8 = 1.25e-2. The bound is
  // that estimate, not a number chosen after seeing the answer.
  EXPECT_LT(error, 1.3e-2) << "max error " << error;
  for (Index i = 0; i < n; ++i) EXPECT_GT(T[i], tWall) << "heating must raise the interior";
}

TEST(ThermalValidation, CaseBNegativeSourceCoolsTheInterior) {
  // The sign control: a heat SINK must lower the interior below the walls.
  constexpr Index nx = 20;
  auto slab = makeSlab(nx, 1.0, 300.0, 300.0);
  const Index n = slab.mesh.numberOfCells();
  const ScalarField T = solveToConvergence(
      slab.mesh, slab.boundaries, ScalarField(n, 4.0), ScalarField(n, 1000.0),
      SurfaceField(slab.mesh.numberOfFaces(), 0.0), -1000.0, ScalarField(n, 300.0));
  for (Index i = 0; i < n; ++i) EXPECT_LT(T[i], 300.0) << "cell " << i;
}

// --- Case C: transient conduction --------------------------------------------

TEST(ThermalValidation, CaseCTransientApproachesTheSteadyState) {
  // dT/dt = alpha d2T/dx2 with fixed ends must converge monotonically to the
  // Case A steady profile -- an independently known reference, not a stored
  // number.
  constexpr Index nx = 20;
  constexpr Real length = 1.0;
  constexpr Real tLeft = 300.0;
  constexpr Real tRight = 400.0;
  auto slab = makeSlab(nx, length, tLeft, tRight);
  const Index n = slab.mesh.numberOfCells();

  const ScalarField k(n, 10.0), cp(n, 500.0), rho(n, 2.0);
  const SurfaceField noFlow(slab.mesh.numberOfFaces(), 0.0);
  ScalarField T(n, tLeft);

  Real previousDistance = std::numeric_limits<Real>::infinity();
  for (int step = 0; step < 200; ++step) {
    ThermalTimeState time;
    time.transient = true;
    time.dt = 0.5;
    time.temperatureOld = &T;
    time.densityOld = &rho;
    time.specificHeatOld = &cp;
    time.densityNew = &rho;
    time.specificHeatNew = &cp;

    const auto assembly = cfd::thermal::assembleEnergyEquation(slab.mesh, T, noFlow, k, cp,
                                                               slab.boundaries, 0.0, {}, time);
    const Vector next = solveOnce(assembly.system, n);

    Real distance = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real exact = tLeft + (tRight - tLeft) * centreX(slab.mesh, i) / length;
      distance = std::max(distance, std::abs(next[i] - exact));
    }
    EXPECT_LE(distance, previousDistance + 1e-9) << "step " << step << " moved away from steady";
    previousDistance = distance;
    for (Index i = 0; i < n; ++i) T[i] = next[i];
  }
  EXPECT_LT(previousDistance, 1e-2) << "did not reach steady; distance " << previousDistance;
}

TEST(ThermalValidation, CaseCTimestepRefinementConvergesToTheSameState) {
  // Temporal convergence: halving dt (and doubling the step count) must
  // approach the same field. The steady limit cannot depend on dt.
  const auto after = [](Real dt, int steps) {
    auto slab = makeSlab(16, 1.0, 300.0, 400.0);
    const Index n = slab.mesh.numberOfCells();
    const ScalarField k(n, 10.0), cp(n, 500.0), rho(n, 2.0);
    const SurfaceField noFlow(slab.mesh.numberOfFaces(), 0.0);
    ScalarField T(n, 300.0);
    for (int s = 0; s < steps; ++s) {
      ThermalTimeState time;
      time.transient = true;
      time.dt = dt;
      time.temperatureOld = &T;
      time.densityOld = &rho;
      time.specificHeatOld = &cp;
      time.densityNew = &rho;
      time.specificHeatNew = &cp;
      const auto assembly = cfd::thermal::assembleEnergyEquation(slab.mesh, T, noFlow, k, cp,
                                                                 slab.boundaries, 0.0, {}, time);
      const Vector next = solveOnce(assembly.system, n);
      for (Index i = 0; i < n; ++i) T[i] = next[i];
    }
    return T;
  };

  const ScalarField coarse = after(1.0, 300);
  const ScalarField fine = after(0.25, 1200);
  for (Index i = 0; i < coarse.size(); ++i) {
    EXPECT_NEAR(coarse[i], fine[i], 1e-4) << "cell " << i;
  }
}

// --- Case D: advection-diffusion ----------------------------------------------

TEST(ThermalValidation, CaseDConvectionCarriesTemperatureDownstream) {
  // Upwind is first order, so this asserts what upwind genuinely guarantees and
  // a sign error would break: boundedness, monotonicity and direction.
  constexpr Index nx = 50;
  auto slab = makeSlab(nx, 1.0, 400.0, 300.0);
  const Index n = slab.mesh.numberOfCells();
  const ScalarField k(n, 0.5), cp(n, 1000.0);

  const SurfaceField massFlux = uniformFlowMassFlux(slab.mesh, /*density=*/1.0, /*speedX=*/2.0);

  const ScalarField T =
      solveToConvergence(slab.mesh, slab.boundaries, k, cp, massFlux, 0.0, ScalarField(n, 300.0));

  for (Index i = 0; i < n; ++i) {
    EXPECT_GE(T[i], 300.0 - 1e-9) << "undershoot at " << i;
    EXPECT_LE(T[i], 400.0 + 1e-9) << "overshoot at " << i;
  }
  for (Index i = 1; i < n; ++i) EXPECT_LE(T[i], T[i - 1] + 1e-9) << "not monotone at " << i;
  EXPECT_GT(T[n / 4], 350.0) << "convection did not carry the hot inlet downstream";
}

// --- Case E: the thermal boundary-condition suite -----------------------------

TEST(ThermalValidation, CaseEFixedHeatFluxGivesTheExactLinearProfile) {
  // Left Dirichlet T0, right prescribed gradient. Steady conduction with a
  // fixed wall gradient g gives T(x) = T0 + g x exactly, so a two-point
  // operator reproduces it to solver tolerance.
  constexpr Index nx = 25;
  constexpr Real length = 1.0;
  constexpr Real t0 = 400.0;
  constexpr Real kValue = 8.0;
  constexpr Real gradient = -30.0;  // K/m, temperature falling to the right

  Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, length, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(t0));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(gradient));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  const Index n = mesh.numberOfCells();
  const ScalarField T =
      solveToConvergence(mesh, boundaries, ScalarField(n, kValue), ScalarField(n, 1000.0),
                         SurfaceField(mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, t0));

  const Real error = maxError(mesh, T, [&](Real x) { return t0 + gradient * x; });
  EXPECT_LT(error, 1e-8) << "max error " << error;
}

TEST(ThermalValidation, CaseEAdiabaticWallsLeaveAUniformFieldExact) {
  // Every wall adiabatic, no source: a uniform field must be an EXACT discrete
  // solution. A spurious boundary flux would tilt it.
  Mesh mesh = MeshGeometry::createCartesian2D(12, 4, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Adiabatic>());
  }
  const Index n = mesh.numberOfCells();
  const ScalarField uniform(n, 350.0);

  const auto assembly = cfd::thermal::assembleEnergyEquation(
      mesh, uniform, SurfaceField(mesh.numberOfFaces(), 0.0), ScalarField(n, 5.0),
      ScalarField(n, 1000.0), boundaries, 0.0, {});
  Vector state(n, 0.0);
  for (Index i = 0; i < n; ++i) state[i] = uniform[i];
  const Vector residual = assembly.system.matrix().multiply(state);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i] - assembly.system.rhs()[i], 0.0, 1e-9)
        << "cell " << i << ": adiabatic walls perturbed a uniform field";
  }
}

TEST(ThermalValidation, CaseERobinMatchesTheSeriesResistanceSolution) {
  // Left Dirichlet T0; right convective to T_inf through h_c.
  //
  // Steady conduction gives T(x) = T0 + C x. Matching the wall flux to the
  // film flux,  -k C = h_c (T0 + C L - T_inf),  yields
  //
  //     C = -h_c (T0 - T_inf) / (k + h_c L)
  //
  // The discrete conductance G = 1/(d/k + 1/h_c) is the exact 1D
  // cell-centre-to-ambient series resistance for a linear profile, so the
  // numerical answer should reproduce this to solver tolerance.
  constexpr Index nx = 25;
  constexpr Real length = 1.0;
  constexpr Real t0 = 500.0;
  constexpr Real tInf = 300.0;
  constexpr Real kValue = 10.0;
  constexpr Real hc = 25.0;

  Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, length, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(t0));
  boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(hc, tInf));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  const Index n = mesh.numberOfCells();
  const ScalarField T =
      solveToConvergence(mesh, boundaries, ScalarField(n, kValue), ScalarField(n, 1000.0),
                         SurfaceField(mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, t0));

  const Real slope = -hc * (t0 - tInf) / (kValue + hc * length);
  const Real error = maxError(mesh, T, [&](Real x) { return t0 + slope * x; });
  EXPECT_LT(error, 1e-7) << "max error " << error << " (analytical slope " << slope << ")";

  for (Index i = 0; i < n; ++i) {
    EXPECT_LE(T[i], t0 + 1e-9) << "cell " << i;
    EXPECT_GE(T[i], tInf - 1e-9) << "cell " << i;
  }
  for (Index i = 1; i < n; ++i) EXPECT_LT(T[i], T[i - 1]) << "cell " << i;
}

TEST(ThermalValidation, CaseERobinLimitsBracketDirichletAndAdiabatic) {
  // Non-vacuity for the coefficient. If the conductance were mishandled --
  // k/d used instead of the series value, say -- these limits would not
  // separate correctly.
  const auto rightEndTemperature = [](Real hc) {
    Mesh mesh = MeshGeometry::createCartesian2D(20, 1, 1.0, 1.0);
    BoundaryConditionSet boundaries;
    boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(500.0));
    boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(hc, 300.0));
    boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
    boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());
    const Index n = mesh.numberOfCells();
    const ScalarField T =
        solveToConvergence(mesh, boundaries, ScalarField(n, 10.0), ScalarField(n, 1000.0),
                           SurfaceField(mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, 500.0));
    return T[n - 1];
  };

  // h_c -> 0: insulated, the slab equilibrates at the hot wall.
  EXPECT_NEAR(rightEndTemperature(1e-8), 500.0, 1e-3);
  // h_c -> large: the wall is pinned near T_inf.
  EXPECT_LT(rightEndTemperature(1e8), 310.0);
  // Monotone in between.
  EXPECT_GT(rightEndTemperature(1.0), rightEndTemperature(100.0));
}

TEST(ThermalValidation, CaseERobinWithVariableConductivity) {
  // G = 1/(d/k + 1/h_c) must use the LOCAL k. With k(T) the boundary cell's
  // conductivity moves as the solution develops, so an implementation that
  // captured k once would drift.
  constexpr Real tInf = 300.0;
  constexpr Real hc = 40.0;
  Mesh mesh = MeshGeometry::createCartesian2D(20, 1, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(900.0));
  boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(hc, tInf));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  const Index n = mesh.numberOfCells();
  const TemperatureDependentProperty kModel({2.0, 0.01}, 200.0, 1200.0, "k");
  const ScalarField cp(n, 1000.0);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);

  ScalarField T(n, 600.0);
  bool converged = false;
  for (int iteration = 0; iteration < 300; ++iteration) {
    const ScalarField k = kModel.evaluateField(T);
    const auto assembly =
        cfd::thermal::assembleEnergyEquation(mesh, T, noFlow, k, cp, boundaries, 0.0, {});
    const Vector next = solveOnce(assembly.system, n);
    Real change = 0.0;
    for (Index i = 0; i < n; ++i) change = std::max(change, std::abs(next[i] - T[i]));
    for (Index i = 0; i < n; ++i) T[i] = next[i];
    if (change < 1e-10 * 900.0) {
      converged = true;
      break;
    }
  }
  ASSERT_TRUE(converged) << "the k(T) Robin iteration did not converge";

  EXPECT_GT(T[n - 1], tInf);
  EXPECT_LT(T[n - 1], 900.0);
  for (Index i = 1; i < n; ++i) EXPECT_LT(T[i], T[i - 1]) << "cell " << i;

  // Non-vacuity: constant k at the model mid-range must give a measurably
  // different wall temperature, so this case can detect a k(T) regression.
  const ScalarField constantK(n, kModel.evaluate(600.0));
  const ScalarField constantSolution =
      solveToConvergence(mesh, boundaries, constantK, cp, noFlow, 0.0, ScalarField(n, 600.0));
  EXPECT_GT(std::abs(T[n - 1] - constantSolution[n - 1]), 1.0)
      << "k(T) and constant k agree at the Robin wall: this case cannot detect a regression";
}

// --- Case F: temperature-dependent conductivity -------------------------------

TEST(ThermalValidation, CaseFVariableConductivityDiffersFromConstant) {
  // Must be sensitive enough that replacing k(T) with a constant FAILS --
  // the brief's explicit requirement for this case.
  constexpr Index nx = 30;
  auto slab = makeSlab(nx, 1.0, 300.0, 900.0);
  const Index n = slab.mesh.numberOfCells();
  const ScalarField cp(n, 1000.0);
  const SurfaceField noFlow(slab.mesh.numberOfFaces(), 0.0);

  // k(T) = 1 + 0.01 T : k triples across 300..900 K.
  const TemperatureDependentProperty kModel({1.0, 0.01}, 200.0, 1200.0, "k");

  // Outer loop over the property: T -> k(T) -> A -> T, the nonlinear iteration
  // the brief requires. Convergence is measured, not assumed.
  ScalarField T(n, 600.0);
  Real change = 0.0;
  bool converged = false;
  for (int iteration = 0; iteration < 200; ++iteration) {
    const ScalarField k = kModel.evaluateField(T);
    const auto assembly =
        cfd::thermal::assembleEnergyEquation(slab.mesh, T, noFlow, k, cp, slab.boundaries, 0.0, {});
    const Vector next = solveOnce(assembly.system, n);
    change = 0.0;
    for (Index i = 0; i < n; ++i) change = std::max(change, std::abs(next[i] - T[i]));
    for (Index i = 0; i < n; ++i) T[i] = next[i];
    if (change < 1e-10 * 900.0) {
      converged = true;
      break;
    }
  }
  ASSERT_TRUE(converged) << "the k(T) Picard iteration did not converge; last change " << change;

  // With k increasing with T, the profile must bend AWAY from the straight line
  // a constant k would give: the hot end conducts better, so its gradient is
  // shallower and the mid-domain temperature sits BELOW the linear profile.
  const ScalarField constantK = solveToConvergence(slab.mesh, slab.boundaries, ScalarField(n, 6.0),
                                                   cp, noFlow, 0.0, ScalarField(n, 600.0));

  Real maximumGap = 0.0;
  for (Index i = 0; i < n; ++i) maximumGap = std::max(maximumGap, std::abs(T[i] - constantK[i]));
  EXPECT_GT(maximumGap, 1.0) << "k(T) produced essentially the constant-k field: this case cannot "
                                "detect a k(T) regression";
}

// --- Case G: variable cp and enthalpy ------------------------------------------

TEST(ThermalValidation, CaseGEnthalpyRoundTripOverTheSolvedField) {
  // T -> h(T) -> T over a real solved field, not just sampled points.
  constexpr Index nx = 20;
  auto slab = makeSlab(nx, 1.0, 400.0, 800.0);
  const Index n = slab.mesh.numberOfCells();
  const TemperatureDependentProperty cpModel({900.0, 0.4}, 300.0, 1000.0, "cp");
  const SensibleEnthalpy enthalpy(cpModel, 300.0);

  const ScalarField T =
      solveToConvergence(slab.mesh, slab.boundaries, ScalarField(n, 10.0),
                         cpModel.evaluateField(ScalarField(n, 600.0)),
                         SurfaceField(slab.mesh.numberOfFaces(), 0.0), 0.0, ScalarField(n, 600.0));

  for (Index i = 0; i < n; ++i) {
    const Real h = enthalpy.enthalpy(T[i]);
    EXPECT_NEAR(enthalpy.temperature(h), T[i], 1e-8) << "cell " << i << " T = " << T[i];
  }
}

// --- Conservation ---------------------------------------------------------------

TEST(ThermalConservation, DiscreteEnergyBalanceCloses) {
  // sum_P (A T - b)_P == 0 to solver tolerance. Internal faces cancel in pairs,
  // so this measures the boundary and source balance -- the thermal counterpart
  // of the mass-balance check SIMPLE already uses.
  struct Case {
    const char* name;
    Real conductivity;
    Real flux;
    Real source;
  };
  const Case cases[] = {
      {"pure conduction", 10.0, 0.0, 0.0},
      {"conduction + generation", 10.0, 0.0, 500.0},
      {"advection-diffusion", 1.0, 1.5, 0.0},
      {"all three", 5.0, 0.8, 250.0},
  };

  for (const auto& testCase : cases) {
    auto slab = makeSlab(25, 1.0, 300.0, 400.0);
    const Index n = slab.mesh.numberOfCells();
    const ScalarField k(n, testCase.conductivity), cp(n, 1000.0);
    const SurfaceField massFlux = uniformFlowMassFlux(slab.mesh, /*density=*/1.0, testCase.flux);
    const ScalarField T = solveToConvergence(slab.mesh, slab.boundaries, k, cp, massFlux,
                                             testCase.source, ScalarField(n, 300.0));

    const auto assembly = cfd::thermal::assembleEnergyEquation(
        slab.mesh, T, massFlux, k, cp, slab.boundaries, testCase.source, {});
    Vector solution(n, 0.0);
    for (Index i = 0; i < n; ++i) solution[i] = T[i];
    const Vector residual = assembly.system.matrix().multiply(solution);
    Real imbalance = 0.0;
    Real scale = 0.0;
    for (Index i = 0; i < n; ++i) {
      imbalance += residual[i] - assembly.system.rhs()[i];
      scale = std::max(scale, std::abs(assembly.system.rhs()[i]));
    }
    const Real normalised = scale > 0.0 ? std::abs(imbalance) / scale : std::abs(imbalance);
    EXPECT_LT(normalised, 1e-8) << testCase.name << ": normalised energy imbalance " << normalised;
  }
}

// --- timestep convergence ---------------------------------------------------

namespace {

// Integrates the transient energy equation from `initial` to `endTime` with a
// fixed step. Backward Euler: each step assembles at the new time level with
// the previous level on the right-hand side, so one linear solve per step and
// no outer iteration is needed (the properties here are constant, so the
// assembled system does not depend on the level being solved for).
ScalarField integrate(const Mesh& mesh, const BoundaryConditionSet& boundaries,
                      const ScalarField& conductivity, const ScalarField& specificHeat,
                      const ScalarField& density, const ScalarField& initial, Real endTime,
                      int steps) {
  const Index n = mesh.numberOfCells();
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const Real dt = endTime / static_cast<Real>(steps);

  ScalarField temperature = initial;
  for (int step = 0; step < steps; ++step) {
    const ScalarField previous = temperature;
    cfd::thermal::ThermalTimeState time;
    time.transient = true;
    time.dt = dt;
    time.temperatureOld = &previous;
    time.densityOld = &density;
    time.specificHeatOld = &specificHeat;
    time.densityNew = &density;
    time.specificHeatNew = &specificHeat;

    const auto assembly = cfd::thermal::assembleEnergyEquation(
        mesh, temperature, noFlow, conductivity, specificHeat, boundaries, 0.0, {}, time);
    const Vector next = solveOnce(assembly.system, n);
    for (Index i = 0; i < n; ++i) temperature[i] = next[i];
  }
  return temperature;
}

Real maxDifference(const ScalarField& a, const ScalarField& b) {
  Real worst = 0.0;
  for (Index i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
  return worst;
}

}  // namespace

// Slab initially at 300 K with both walls held at their own temperatures: a
// real transient with a nontrivial interior profile at the sampled time, so
// the temporal error is not accidentally small.
TEST(TimestepConvergence, ImplicitEulerIsFirstOrderInTime) {
  const auto slab = makeSlab(20, 1.0, 400.0, 300.0);
  const Index n = slab.mesh.numberOfCells();
  const ScalarField conductivity(n, 5.0);
  const ScalarField specificHeat(n, 1000.0);
  const ScalarField density(n, 1.0);
  const ScalarField initial(n, 300.0);

  // Short enough that the field is still visibly evolving -- integrating to
  // steady state would make every step size agree and measure nothing.
  const Real endTime = 2.0;

  // The reference: the same mesh, 16x finer than the finest step compared
  // against it, so its own temporal error is ~1/16 of the smallest measured
  // one and does not distort the ratios.
  const ScalarField reference =
      integrate(slab.mesh, slab.boundaries, conductivity, specificHeat, density, initial, endTime,
                /*steps=*/1280);

  const int stepCounts[] = {10, 20, 40, 80};
  Real errors[4] = {0.0, 0.0, 0.0, 0.0};
  for (int i = 0; i < 4; ++i) {
    const ScalarField solution = integrate(slab.mesh, slab.boundaries, conductivity, specificHeat,
                                           density, initial, endTime, stepCounts[i]);
    errors[i] = maxDifference(solution, reference);
    std::printf("steps = %4d   dt = %.5f   error = %.6e\n", stepCounts[i],
                endTime / static_cast<Real>(stepCounts[i]), errors[i]);
    EXPECT_GT(errors[i], 0.0) << "zero error means the reference and the run are the same "
                                 "integration -- the study measures nothing";
  }

  for (int i = 1; i < 4; ++i) {
    EXPECT_LT(errors[i], errors[i - 1]) << "halving dt did not reduce the error";
    const Real order = std::log(errors[i - 1] / errors[i]) / std::log(2.0);
    std::printf("steps %4d -> %4d   observed order p = %.4f\n", stepCounts[i - 1], stepCounts[i],
                order);
    // Bracketed on both sides. Below 0.8 would mean the transient term is not
    // doing what implicit Euler does; above 1.3 would mean the reference or
    // the metric is wrong, since a first-order scheme cannot converge faster
    // than first order.
    EXPECT_GT(order, 0.8) << "order " << order << " -- not first order in time";
    EXPECT_LT(order, 1.3) << "order " << order
                          << " exceeds first order, which implicit Euler "
                             "cannot do -- suspect the reference solution";
  }
}
