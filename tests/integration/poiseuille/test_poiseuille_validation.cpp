// P0 -- Physical Validation: planar Poiseuille (pressure-driven, fully
// developed channel) flow against the closed-form analytical solution
// (TODO.md "P0 -- Physical Validation" > "Poiseuille Flow").
//
// This case was documented as blocked in TODO.md: the original
// PressureCorrectionEquation gave every boundary face zero pressure-
// correction coupling, which is correct for a closed cavity but leaves an
// open (inlet/outlet) domain with no degree of freedom to absorb a global
// mass-flow mismatch (see the header comment in
// PressureCorrectionEquation.hpp and tests/solver/simple/
// test_simple_continuity.cpp's comment for the original finding). That
// blocker is resolved -- PressureCorrectionEquation now supports a
// Dirichlet (FixedValue) pressure outlet, proven end-to-end by
// tests/solver/simple/test_simple_open_boundary.cpp. This file reuses
// that exact boundary treatment (uniform-velocity Inlet, zero-gradient
// Outlet, Wall top/bottom, FixedValue(0) outlet pressure) and adds the
// actual physical comparison against the analytical Poiseuille profile.
//
// Same SIMPLE-numerics-frozen discipline as
// tests/integration/cavity/test_cavity_ghia.cpp: nothing here changes the
// pressure-correction algorithm, discretization, or linear solver -- only
// SIMPLESettings (iteration budgets, inner pressure-solver tolerance) is
// varied per grid.
//
// Geometry/physics: channel height H=1, length L=8H, rho=1, mu=0.1,
// uniform inlet velocity Uavg=1 => Re = rho*Uavg*H/mu = 10. Entrance
// length for laminar channel flow is Le/H ~ 0.06*Re (Boussinesq), i.e.
// ~0.6H here -- comfortably resolved well before the sampling stations
// below, which sit in the back half of an 8H-long channel.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

#include "PoiseuilleValidationUtils.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
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

constexpr Real kChannelHeight = 1.0;
constexpr Real kChannelLength = 8.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;  // uniform inlet speed = bulk/mean velocity.
constexpr Real kReynolds = kDensity * kMeanVelocity * kChannelHeight / kViscosity;  // = 10.

// Sampling stations, both safely inside the fully-developed region: past
// the ~0.6H entrance length, and away from the outlet's boundary-adjacent
// cells.
constexpr Real kProfileStationX = 0.75 * kChannelLength;
constexpr Real kPressureStationX1 = 0.40 * kChannelLength;
constexpr Real kPressureStationX2 = 0.75 * kChannelLength;

BoundaryConditionSet makeChannelVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{kMeanVelocity, 0.0}));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

// Neumann everywhere except a Dirichlet (fixed-pressure) outlet -- the
// open-boundary treatment PressureCorrectionEquation supports (see file
// header).
BoundaryConditionSet makeChannelPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

struct GridCase {
  Index nx;
  Index ny;
  Index maxOuterIterations;
  Index pressureInnerIterations;
  Real pressureAbsoluteTolerance;
  Real pressureRelativeTolerance;
  // Diagnosed empirically per grid (SIMPLESettings.hpp / TODO.md section
  // 44 explicitly allow case-specific plain absolute tolerances in this
  // phase, same allowance cavity's tests already use for the *inner*
  // pressure-solver tolerance -- here it's the outer gates instead):
  // with fixed under-relaxation factors (alpha_u=0.7, alpha_p=0.3), this
  // open channel's u/p residuals settle into a smooth, non-oscillating
  // plateau above 1e-6 rather than continuing to shrink toward it -- a
  // known property of fixed-relaxation SIMPLE (the residual definition
  // measures distance from the *previous* iterate to the newly
  // reassembled equation, which under-relaxation keeps from ever being
  // exactly zero). Continuity (evaluated on the *corrected*, mass-
  // consistent flux) always reaches its own 1e-6 gate comfortably --
  // enforced essentially exactly by flux correction every iteration, same
  // as the cavity case -- so it is not varied per grid. On the 64x8 grid
  // the observed floors were u ~7e-6, v ~1e-7, p ~1e-4; p's floor is
  // ~1e-5 of the actual channel pressure drop (dp/dx*L ~ -9.6 for this
  // case's parameters), i.e. physically negligible.
  Real outerVelocityTolerance;
  Real outerPressureTolerance;
};

SIMPLESettings makeSettings(const GridCase& grid) {
  SIMPLESettings settings;
  settings.maxIterations = grid.maxOuterIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = grid.outerVelocityTolerance;
  settings.pressureTolerance = grid.outerPressureTolerance;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = grid.pressureInnerIterations;
  settings.pressureSolver.absoluteTolerance = grid.pressureAbsoluteTolerance;
  settings.pressureSolver.relativeTolerance = grid.pressureRelativeTolerance;
  return settings;
}

// Runs one grid to convergence, checks the mandatory convergence/
// mass-conservation gates, extracts the velocity profile and pressure
// gradient in the fully-developed region, and writes fresh CSV/JSON
// evidence under results/validation/poiseuille_flow/. Returns the
// velocity error metrics so callers can additionally check the
// grid-refinement trend.
struct PoiseuilleRunOutcome {
  cfd::validation::ErrorMetrics velocityError;
  Real pressureGradientRelativeError{0.0};
};

// ASSERT_* only works in void-returning functions (it expands to a bare
// `return;` on failure), so the outcome comes back via out-parameter and
// callers must check ::testing::Test::HasFatalFailure() before using it
// (same convention as tests/integration/cavity/test_cavity_ghia.cpp).
void runGridAndValidate(const GridCase& grid, PoiseuilleRunOutcome* outcome) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(grid.nx, grid.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeChannelPressureBoundaries(mesh);
  const FluidProperties fluid(kDensity, kViscosity);

  const SIMPLE simple(makeSettings(grid), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  ASSERT_EQ(result.status, SIMPLEStatus::Converged)
      << "grid " << grid.nx << "x" << grid.ny
      << " did not converge (status=" << static_cast<int>(result.status)
      << ", iterations=" << result.iterations << ")";

  bool finite = true;
  for (Index i = 0; i < result.velocity.size(); ++i) {
    finite = finite && std::isfinite(result.velocity[i].x) && std::isfinite(result.velocity[i].y);
  }
  for (Index i = 0; i < result.pressure.size(); ++i)
    finite = finite && std::isfinite(result.pressure[i]);
  ASSERT_TRUE(finite) << "grid " << grid.nx << "x" << grid.ny << " produced a non-finite field";

  // Walls (top/bottom) must carry ~0 normal mass flux; the inlet/outlet
  // are expected to carry real, non-zero flux.
  Real maxWallFlux = 0.0;
  Real inletFlux = 0.0;
  Real outletFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == "top" || patch.name() == "bottom") {
      for (const Index faceId : patch.faceIds()) {
        maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[faceId]));
      }
    } else if (patch.name() == "left") {
      for (const Index faceId : patch.faceIds()) inletFlux += result.massFlux[faceId];
    } else if (patch.name() == "right") {
      for (const Index faceId : patch.faceIds()) outletFlux += result.massFlux[faceId];
    }
  }
  EXPECT_LT(maxWallFlux, 1e-6) << "channel is leaking mass through a wall";
  // Inflow (outward normal points in -x at the inlet, so inlet flux is
  // negative) must balance outflow.
  EXPECT_NEAR(inletFlux + outletFlux, 0.0, 1e-6);
  EXPECT_LT(result.globalMassImbalance, 1e-6);
  EXPECT_GT(outletFlux, 0.5 * kMeanVelocity * kChannelHeight)
      << "sanity: net flow should be the prescribed inlet flow rate, not a solver artifact";

  const auto profile = cfd::validation::extractVerticalProfileU(
      mesh, grid.nx, grid.ny, result.velocity, kProfileStationX, kChannelHeight);
  const Real pGradNumerical = cfd::validation::numericalPressureGradient(
      mesh, grid.nx, grid.ny, result.pressure, kPressureStationX1, kPressureStationX2);
  const Real pGradAnalytical =
      cfd::validation::analyticalPressureGradient(kViscosity, kMeanVelocity, kChannelHeight);
  const Real pGradRelativeError =
      std::abs(pGradNumerical - pGradAnalytical) / std::abs(pGradAnalytical);

  const std::string gridDir = "results/validation/poiseuille_flow/" + std::to_string(grid.nx) +
                              "x" + std::to_string(grid.ny);
  std::filesystem::create_directories(gridDir);

  const auto velocityError = cfd::validation::computeVelocityProfileErrors(
      profile, kChannelHeight, kMeanVelocity, gridDir + "/velocity_profile.csv");

  cfd::validation::PoiseuilleValidationRecord record;
  record.nx = grid.nx;
  record.ny = grid.ny;
  record.channelLength = kChannelLength;
  record.channelHeight = kChannelHeight;
  record.reynoldsNumber = kReynolds;
  record.converged = result.converged();
  record.iterations = result.iterations;
  record.finalUResidual = result.finalUResidual;
  record.finalVResidual = result.finalVResidual;
  record.finalPressureResidual = result.finalPressureResidual;
  record.finalContinuityResidual = result.finalContinuityResidual;
  record.globalMassImbalance = result.globalMassImbalance;
  record.maxWallNormalFlux = maxWallFlux;
  record.inletFlux = inletFlux;
  record.outletFlux = outletFlux;
  record.finite = true;
  record.velocityError = velocityError;
  record.pressureGradientNumerical = pGradNumerical;
  record.pressureGradientAnalytical = pGradAnalytical;
  record.pressureGradientRelativeError = pGradRelativeError;
  cfd::validation::writeValidationJson(gridDir + "/validation.json", record);

  // Thresholds hold comfortable margin above the worst (coarsest, 64x8)
  // observed values -- velocity_l2 0.0136/pressure-gradient error 3.74%
  // there, tightening to 0.000968/0.195% by 256x32 (see the TODO.md
  // status note for the full three-grid trend).
  EXPECT_LT(velocityError.l2, 0.02) << "grid " << grid.nx << "x" << grid.ny;
  EXPECT_LT(pGradRelativeError, 0.06) << "grid " << grid.nx << "x" << grid.ny;

  outcome->velocityError = velocityError;
  outcome->pressureGradientRelativeError = pGradRelativeError;
}

}  // namespace

TEST(PoiseuilleValidation, Grid64x8ConvergesAndMatchesAnalyticalProfile) {
  PoiseuilleRunOutcome outcome;
  runGridAndValidate(
      GridCase{64, 8, /*maxOuter=*/3000, /*pressureInner=*/2000, 1e-8, 1e-6, 2e-5, 5e-4}, &outcome);
}

// Slower (larger system, more outer iterations): run explicitly with
// --gtest_also_run_disabled_tests when regenerating full validation
// evidence, not as part of the default fast test suite -- same rationale
// as the DISABLED_ 40x40/80x80 cavity grids.
TEST(PoiseuilleValidation, DISABLED_Grid128x16ConvergesAndMatchesAnalyticalProfile) {
  PoiseuilleRunOutcome outcome;
  runGridAndValidate(
      GridCase{128, 16, /*maxOuter=*/8000, /*pressureInner=*/2000, 1e-8, 1e-6, 2e-5, 5e-4},
      &outcome);
}

TEST(PoiseuilleValidation, DISABLED_Grid256x32ConvergesAndMatchesAnalyticalProfile) {
  PoiseuilleRunOutcome outcome;
  runGridAndValidate(
      GridCase{256, 32, /*maxOuter=*/20000, /*pressureInner=*/5000, 1e-7, 1e-5, 2e-5, 5e-4},
      &outcome);
}

// P12-NUM-005: three-grid convergence study (replaces the former
// DISABLED_GridRefinementReducesVelocityError, which only checked that the
// error did not grow). Grids 64x8 / 96x12 / 144x18 on the same channel
// (r21 = r32 = 1.5, square cells, h = sqrt(A/N) = H/ny), run through the
// shared cfd::validation::runGridConvergenceStudy with its solver-status
// gate. Enabled in the default suite (~3 min Debug).
//
// Quantity: the fully-developed centerline velocity u(0.75 L, H/2), exact
// value 1.5 U_mean (analytical), formal order 2 (second-order central
// diffusion; the upwind convection term vanishes for fully developed flow).
// Measured iterative sensitivity: stopping at the u-residual gate 2e-5 vs.
// 40000 iterations changes it by 1.2e-6 on 64x8 -- far below the grid
// differences (~1e-2), hence absoluteNoise = 1e-5. The pressure gradient is
// deliberately NOT used: the same comparison moves it by 7 % on 64x8 (the
// pressure level drifts on this case's residual plateau), i.e. it is not
// iteratively converged at the validation gates (results/p12-num-005).
// Second quantity: the L2 error of the profile against the analytical one
// (exact limit 0) -- its Richardson extrapolation must approach 0.
//
// Pressure solve: Jacobi-preconditioned BiCGSTAB with the P12-NUM-004
// linear-solver fallback -- unpreconditioned BiCGSTAB broke down 70 times
// on the 144x18 grid (all recovered by the fallback; measured), a linear-
// solver issue, not part of the discretization being studied.
TEST(PoiseuilleValidation, GridConvergence) {
  using cfd::validation::GridConvergenceStatus;
  using cfd::validation::GridSolveOutput;
  using cfd::validation::GridSpec;

  const auto solve = [](const GridSpec& spec) {
    // Inner pressure tolerance 1e-10 / 1e-8 (tighter than the per-grid
    // tests above) -- the setting the iterative sensitivity was measured with.
    GridCase grid{spec.nx, spec.ny, /*maxOuter=*/8000, /*pressureInner=*/5000, 1e-10, 1e-8,
                  2e-5,    5e-4};
    SIMPLESettings settings = makeSettings(grid);
    settings.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
    settings.robustness.linearSolverFallback.enabled = true;
    const Mesh mesh = MeshGeometry::createCartesian2D(spec.nx, spec.ny, spec.lengthX, spec.lengthY);
    const SIMPLE simple(settings, /*referenceCell=*/0);
    const SIMPLEResult result = simple.solve(
        mesh, FluidProperties(kDensity, kViscosity), makeChannelVelocityBoundaries(mesh),
        makeChannelPressureBoundaries(mesh), VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
        ScalarField(mesh.numberOfCells(), 0.0));
    GridSolveOutput out;
    out.acceptance = cfd::validation::assessSimpleSolve(result, 1e-6);
    out.solverIterations = result.iterations;
    if (!out.acceptance.accepted) return out;
    const auto profile = cfd::validation::extractVerticalProfileU(
        mesh, spec.nx, spec.ny, result.velocity, kProfileStationX, kChannelHeight);
    out.quantities.emplace_back("centerline_velocity",
                                cfd::validation::interpolateProfile(profile, 0.5 * kChannelHeight));
    out.quantities.emplace_back(
        "velocity_profile_l2_error",
        cfd::validation::computeVelocityProfileErrors(profile, kChannelHeight, kMeanVelocity).l2);
    return out;
  };

  cfd::validation::QuantitySpec centerline;
  centerline.name = "centerline_velocity";
  centerline.description = "u(0.75 L, H/2), fully developed; exact 1.5 U_mean";
  centerline.reference = 1.5 * kMeanVelocity;
  centerline.referenceKind = "analytical";
  centerline.options.formalOrder = 2.0;
  centerline.options.absoluteNoise = 1e-5;
  // 1 %: the customary engineering target for the numerical uncertainty of
  // a validation quantity; fixed with the method, not fitted to the result.
  centerline.options.gridIndependenceThreshold = 0.01;
  cfd::validation::QuantitySpec l2;
  l2.name = "velocity_profile_l2_error";
  l2.description = "RMS error of u(0.75 L, y) vs. the analytical profile (exact limit 0)";
  l2.reference = 0.0;
  l2.referenceKind = "analytical";
  l2.options.formalOrder = 2.0;
  l2.options.absoluteNoise = 1e-5;

  const auto study = cfd::validation::runGridConvergenceStudy(
      "poiseuille_flow",
      "Planar Poiseuille channel L = 8H, Re = 10, SIMPLE (upwind convection, central diffusion)",
      {GridSpec{"coarse", 64, 8, kChannelLength, kChannelHeight},
       GridSpec{"medium", 96, 12, kChannelLength, kChannelHeight},
       GridSpec{"fine", 144, 18, kChannelLength, kChannelHeight}},
      solve, {centerline, l2});
  cfd::validation::writeGridConvergenceReport(
      "results/validation/grid_convergence/poiseuille_flow.json", study);
  {
    std::ofstream md("results/validation/grid_convergence/poiseuille_flow.md");
    md << cfd::validation::gridConvergenceReportMarkdown(study);
  }
  std::printf("\n%s", cfd::validation::gridConvergenceReportMarkdown(study).c_str());
  // The written report passes the schema / consistency validator.
  const auto reportProblems = cfd::validation::validateGridConvergenceReportFile(
      "results/validation/grid_convergence/poiseuille_flow.json");
  EXPECT_TRUE(reportProblems.empty()) << reportProblems.front();

  ASSERT_TRUE(study.allSolvesAccepted) << study.rejectionReason;
  const auto& u = study.quantities[0];
  const auto& a = u.analysis;
  // In the asymptotic range of the formal second order (ratio within
  // 1 +/- 0.1 -- the library default band, equivalent to p in [1.74, 2.24]
  // at r = 1.5).
  ASSERT_EQ(a.status, GridConvergenceStatus::Asymptotic) << a.diagnostic;
  ASSERT_TRUE(a.observedOrder.has_value() && a.gci21.has_value() && a.gci32.has_value());
  EXPECT_LT(*a.gci21, *a.gci32);
  // Richardson extrapolation removes most of the fine-grid error against
  // the exact value: for a sequence of order p the extrapolated error is of
  // higher order, so requiring it to be below 20 % of the fine error is a
  // loose, method-derived bound.
  const Real fineError = std::abs(*u.errorVsReference[2]);
  EXPECT_LT(std::abs(*u.extrapolatedErrorVsReference), 0.2 * fineError);
  // The GCI is a conservative estimate: it must bound the TRUE relative
  // fine-grid error (known here, since the exact value is known).
  EXPECT_LE(*u.relativeErrorVsReference[2], *a.gci21);
  // Errors against the exact value decrease monotonically.
  EXPECT_LT(std::abs(*u.errorVsReference[2]), std::abs(*u.errorVsReference[1]));
  EXPECT_LT(std::abs(*u.errorVsReference[1]), std::abs(*u.errorVsReference[0]));
  EXPECT_TRUE(a.gridIndependent) << a.gridIndependenceReason;

  // The profile error norm converges and extrapolates toward its exact
  // limit 0 (well below the fine-grid norm).
  const auto& e = study.quantities[1];
  ASSERT_TRUE(e.analysis.observedOrder.has_value()) << e.analysis.diagnostic;
  ASSERT_TRUE(e.analysis.extrapolated21.has_value());
  EXPECT_LT(std::abs(*e.analysis.extrapolated21), 0.2 * *e.values[2]);
}

// Repeat runs, require identical results -- same determinism guarantee
// SIMPLEDeterminismTest / SIMPLEOpenBoundaryTest.OpenChannelIsDeterministic
// already prove structurally for the underlying solve() call regardless
// of grid size or open/closed boundaries.
TEST(PoiseuilleValidation, Grid64x8IsDeterministic) {
  const GridCase grid{64, 8, 6000, 2000, 1e-8, 1e-6, 2e-5, 5e-4};
  const Mesh mesh =
      MeshGeometry::createCartesian2D(grid.nx, grid.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeChannelPressureBoundaries(mesh);
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
