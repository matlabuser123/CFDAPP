// P1 -- Case System, section 47: end-to-end flow -- load a case
// directory, validate, build the runtime simulation, run SIMPLE, and
// assert convergence. Uses the small 4x4 fixture
// (tests/data/cases/valid_cavity) for the default fast suite; the full
// production cases/lid_driven_cavity (20x20, the P1 target command
// itself) is exercised as a DISABLED_ stronger gate, same convention as
// the cavity/Poiseuille validation suites' DISABLED_ larger grids.
#include <gtest/gtest.h>

#include <cmath>

#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// gtest_discover_tests below sets WORKING_DIRECTORY to CFDApp_SOURCE_DIR
// (same convention as tests/integration/cavity and .../poiseuille), so
// these relative paths resolve against the repository root regardless of
// where the test binary itself lives.
void runCaseAndAssertConverged(const std::filesystem::path& caseDirectory, Index expectedNx,
                               Index expectedNy) {
  const auto definition = CaseReader{}.read(caseDirectory);
  EXPECT_EQ(definition.mesh.nx, expectedNx);
  EXPECT_EQ(definition.mesh.ny, expectedNy);

  const auto setup = CaseBuilder{}.build(definition);
  const SIMPLE simple(setup.solverSettings, /*referenceCell=*/0);
  const SIMPLEResult result =
      simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                   setup.initialVelocity, setup.initialPressure);

  ASSERT_EQ(result.status, SIMPLEStatus::Converged)
      << "case " << caseDirectory << " did not converge (status=" << static_cast<int>(result.status)
      << ", iterations=" << result.iterations << ")";

  for (Index i = 0; i < result.velocity.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.velocity[i].x));
    EXPECT_TRUE(std::isfinite(result.velocity[i].y));
  }
  for (Index i = 0; i < result.pressure.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.pressure[i]));
  }

  // Closed cavity: every boundary is impermeable.
  EXPECT_NEAR(result.globalMassImbalance, 0.0, 1e-6);
  for (const auto& patch : setup.mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-6);
    }
  }
}

}  // namespace

TEST(CaseLidDrivenCavityIntegrationTest, SmallFixtureLoadsBuildsAndConverges) {
  runCaseAndAssertConverged("tests/data/cases/valid_cavity", 4, 4);
}

// The literal P1 target command (TODO.md section 50): "cfdapp --case
// cases/lid_driven_cavity". Slower (thousands of outer iterations on a
// 20x20 mesh, ~seconds not milliseconds) -- run explicitly with
// --gtest_also_run_disabled_tests, same rationale as every other
// DISABLED_ larger-grid case in this codebase.
TEST(CaseLidDrivenCavityIntegrationTest, DISABLED_ProductionCaseLoadsBuildsAndConverges) {
  runCaseAndAssertConverged("cases/lid_driven_cavity", 20, 20);
}
