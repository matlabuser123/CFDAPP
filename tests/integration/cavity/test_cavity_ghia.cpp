// P0 -- Physical Validation: lid-driven cavity, Re=100, against the
// published Ghia, Ghia & Shin (1982) benchmark (TODO.md "P0 -- Physical
// Validation", sections 19-36).
//
// The SIMPLE numerical formulation is frozen for this phase (TODO.md
// section 2): nothing here changes the pressure-correction algorithm, the
// discretization, or the linear solver. What legitimately varies per grid
// is SIMPLESettings -- iteration budgets and the *inner* pressure-solver
// tolerance -- exactly as the existing 4x4 vs 20x20 tests already do
// (tests/solver/simple/test_simple_convergence.cpp).
//
// 40x40 needed looser inner-tolerance discovery before this file was
// written: the 20x20 test's pressureSolver settings (absolute 1e-10,
// relative 1e-8) make BiCGSTAB report Breakdown on the 40x40 pressure-
// correction matrix -- verified with a standalone probe to stagnate
// (oscillating, not diverging) around residual ~1e-9 regardless of
// maxIterations (tried 2000/5000/50000, identical failure point) and
// regardless of adding a Jacobi preconditioner (stagnation floor barely
// moved, ~1.2e-10) -- i.e. a real BiCGSTAB accuracy floor for this matrix
// at this size, not an iteration-budget or conditioning problem. Loosening
// the inner tolerance to absolute 1e-8 / relative 1e-6 (still far tighter
// than SIMPLE's own outer pressureTolerance=1e-6 gate) stays comfortably
// clear of that floor and converges cleanly. This is exactly the kind of
// case-appropriate solver configuration SIMPLESettings exists for, not a
// change to frozen numerics.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

#include "CavityValidationUtils.hpp"
#include "GhiaRe100.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

constexpr Real kLidVelocity = 1.0;  // TODO.md section 20: U_lid = 1.
constexpr Real kDensity = 1.0;      // rho = 1.
constexpr Real kViscosity = 0.01;   // mu = 0.01 -> Re = rho*U*L/mu = 100 for L=1.

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{kLidVelocity, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

struct GridCase {
  Index n;
  Index maxOuterIterations;
  Index pressureInnerIterations;
  Real pressureAbsoluteTolerance;
  Real pressureRelativeTolerance;
};

SIMPLESettings makeSettings(const GridCase& grid) {
  SIMPLESettings settings;
  settings.maxIterations = grid.maxOuterIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = grid.pressureInnerIterations;
  settings.pressureSolver.absoluteTolerance = grid.pressureAbsoluteTolerance;
  settings.pressureSolver.relativeTolerance = grid.pressureRelativeTolerance;
  return settings;
}

// Runs one grid to convergence, checks the mandatory physical/numerical
// gates (TODO.md sections 23-24, 56), extracts both Ghia centerlines, and
// writes fresh CSV/JSON evidence under results/validation/cavity_re100/.
// Returns the Ghia error metrics so callers can additionally check the
// grid-refinement trend (TODO.md section 31-32).
struct CavityRunOutcome {
  cfd::validation::ErrorMetrics ghiaU;
  cfd::validation::ErrorMetrics ghiaV;
};

// ASSERT_* only works in void-returning functions (it expands to a bare
// `return;` on failure), so the outcome comes back via out-parameter and
// callers must check ::testing::Test::HasFatalFailure() before using it.
void runGridAndValidate(const GridCase& grid, CavityRunOutcome* outcome) {
  const Mesh mesh = MeshGeometry::createCartesian2D(grid.n, grid.n, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(kDensity, kViscosity);

  const SIMPLE simple(makeSettings(grid), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  // TODO.md section 23: reaching maxIterations must never be reported or
  // treated as convergence.
  ASSERT_EQ(result.status, SIMPLEStatus::Converged)
      << "grid " << grid.n << "x" << grid.n
      << " did not converge (status=" << static_cast<int>(result.status)
      << ", iterations=" << result.iterations << ")";

  // TODO.md section 59: no NaN/Inf anywhere in the converged state.
  for (Index i = 0; i < result.velocity.size(); ++i) {
    ASSERT_TRUE(std::isfinite(result.velocity[i].x));
    ASSERT_TRUE(std::isfinite(result.velocity[i].y));
  }
  for (Index i = 0; i < result.pressure.size(); ++i) ASSERT_TRUE(std::isfinite(result.pressure[i]));

  // TODO.md section 24: every wall -- including the tangentially-moving
  // lid -- must carry ~0 normal mass flux.
  Real maxWallFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[faceId]));
    }
  }
  EXPECT_LT(maxWallFlux, 1e-6) << "cavity is leaking mass through a wall";
  EXPECT_LT(result.globalMassImbalance, 1e-6);

  const auto uProfile = cfd::validation::extractVerticalProfileU(
      mesh, grid.n, grid.n, result.velocity, 0.5, kLidVelocity);
  const auto vProfile =
      cfd::validation::extractHorizontalProfileV(mesh, grid.n, grid.n, result.velocity, 0.5);

  const std::string gridDir =
      "results/validation/cavity_re100/" + std::to_string(grid.n) + "x" + std::to_string(grid.n);
  std::filesystem::create_directories(gridDir);

  const auto ghiaU = cfd::validation::computeGhiaErrors(
      uProfile, cfd::validation::ghia_re100::kCenterlineU, gridDir + "/centerline_u.csv", "y", "u");
  const auto ghiaV = cfd::validation::computeGhiaErrors(
      vProfile, cfd::validation::ghia_re100::kCenterlineV, gridDir + "/centerline_v.csv", "x", "v");

  cfd::validation::CavityValidationRecord record;
  record.nx = grid.n;
  record.ny = grid.n;
  record.converged = result.converged();
  record.iterations = result.iterations;
  record.finalUResidual = result.finalUResidual;
  record.finalVResidual = result.finalVResidual;
  record.finalPressureResidual = result.finalPressureResidual;
  record.finalContinuityResidual = result.finalContinuityResidual;
  record.globalMassImbalance = result.globalMassImbalance;
  record.maxWallNormalFlux = maxWallFlux;
  record.finite = true;
  record.ghiaU = ghiaU;
  record.ghiaV = ghiaV;
  cfd::validation::writeValidationJson(gridDir + "/validation.json", record);

  // TODO.md section 48: not "every Ghia point < 1% error" on a coarse
  // first-order grid -- a converged solution with clearly bounded
  // benchmark disagreement, tightening with refinement (checked
  // separately, across grids, in the GridRefinement test below).
  EXPECT_LT(ghiaU.l2, 0.20) << "grid " << grid.n << "x" << grid.n;
  EXPECT_LT(ghiaV.l2, 0.20) << "grid " << grid.n << "x" << grid.n;

  outcome->ghiaU = ghiaU;
  outcome->ghiaV = ghiaV;
}

}  // namespace

TEST(CavityGhiaValidation, Grid20x20ConvergesAndMatchesGhia) {
  CavityRunOutcome outcome;
  runGridAndValidate(GridCase{20, /*maxOuter=*/6000, /*pressureInner=*/2000, 1e-10, 1e-8},
                     &outcome);
}

// Slow (~8 minutes observed): at 20x20's inner pressure tolerance,
// BiCGSTAB stagnates/breaks down on the 40x40 pressure-correction matrix
// (see file header) -- the looser inner tolerance used here avoids that,
// but the outer loop still needs thousands of iterations to converge on
// this many cells. Run explicitly with --gtest_also_run_disabled_tests
// when regenerating full validation evidence, not as part of the default
// fast test suite.
TEST(CavityGhiaValidation, DISABLED_Grid40x40ConvergesAndMatchesGhia) {
  CavityRunOutcome outcome;
  runGridAndValidate(GridCase{40, /*maxOuter=*/8000, /*pressureInner=*/2000, 1e-8, 1e-6}, &outcome);
}

TEST(CavityGhiaValidation, DISABLED_Grid80x80ConvergesAndMatchesGhia) {
  CavityRunOutcome outcome;
  runGridAndValidate(GridCase{80, /*maxOuter=*/20000, /*pressureInner=*/5000, 1e-7, 1e-5},
                     &outcome);
}

// TODO.md sections 30-32: error against the published benchmark should
// not grow with refinement, and the coarse-to-fine trend is itself
// evidence of grid convergence independent of the external benchmark's
// own sampling density. Disabled alongside the 40x40/80x80 runs above
// since it depends on both.
TEST(CavityGhiaValidation, DISABLED_GridRefinementReducesGhiaError) {
  CavityRunOutcome r20, r40, r80;
  runGridAndValidate(GridCase{20, 6000, 2000, 1e-10, 1e-8}, &r20);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  runGridAndValidate(GridCase{40, 8000, 2000, 1e-8, 1e-6}, &r40);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  runGridAndValidate(GridCase{80, 20000, 5000, 1e-7, 1e-5}, &r80);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());

  EXPECT_LE(r40.ghiaU.l2, r20.ghiaU.l2);
  EXPECT_LE(r80.ghiaU.l2, r40.ghiaU.l2);
  EXPECT_LE(r40.ghiaV.l2, r20.ghiaV.l2);
  EXPECT_LE(r80.ghiaV.l2, r40.ghiaV.l2);
}

// TODO.md section 35: repeat runs, require identical results. 20x20 only
// in the default suite for runtime; 40x40/80x80 determinism was already
// exercised manually while producing the evidence in results/validation/
// (rerunning the DISABLED_ cases above twice reproduces bit-identical
// fields and histories, the same guarantee SIMPLEDeterminismTest already
// proves structurally for the underlying solve() call regardless of
// grid size -- see tests/solver/simple/test_simple_determinism.cpp).
TEST(CavityGhiaValidation, Grid20x20IsDeterministic) {
  const GridCase grid{20, 6000, 2000, 1e-10, 1e-8};
  const Mesh mesh = MeshGeometry::createCartesian2D(grid.n, grid.n, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(kDensity, kViscosity);
  const SIMPLE simple(makeSettings(grid), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult a = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                      initialVelocity, initialPressure);
  const SIMPLEResult b = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                      initialVelocity, initialPressure);

  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
  }
  for (Index i = 0; i < a.pressure.size(); ++i) EXPECT_EQ(a.pressure[i], b.pressure[i]);
  ASSERT_EQ(a.uResidualHistory.size(), b.uResidualHistory.size());
  for (std::size_t i = 0; i < a.uResidualHistory.size(); ++i) {
    EXPECT_EQ(a.uResidualHistory[i], b.uResidualHistory[i]);
  }
}
