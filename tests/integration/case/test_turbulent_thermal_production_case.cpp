// P16 -- turbulent heat transfer through the REAL production path:
//
//   physics.json -> CaseReader -> CaseBuilder -> ProjectRunner -> SIMPLE +
//   turbulence -> converged mu_t -> ThermalSolver -> k_eff -> temperature
//
// No test-only constructor appears anywhere in this file. Every run starts from
// a case directory on disk, exactly as a user's would.
//
// THE DETECTOR, and why it is the right one.
// "capability vs reachability": a feature no case file can reach is not
// shipped, and unit tests never catch that. So the gate here is not "does the
// solver compute k_t" -- that is already proven at unit level -- but "does a
// case file changing ONLY turbulent_prandtl change the answer". If k_t never
// reached the energy equation, Pr_t would scale nothing and every run below
// would be identical. That makes the sensitivity test and the reachability
// test the same test, which is the point.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCaseDirectory = "cases/turbulent_heated_channel";

// Rewrites one key inside physics.json's turbulence block, leaving every other
// field of the committed case untouched. This is the ONLY thing that differs
// between the runs compared below.
void setTurbulentPrandtl(const CaseFixtureCopy& fixture, Real value) {
  const std::filesystem::path physicsPath = fixture.path() / "physics.json";
  nlohmann::json physics;
  {
    std::ifstream in(physicsPath);
    ASSERT_TRUE(in.good()) << "could not read " << physicsPath;
    in >> physics;
  }
  physics["turbulence"]["turbulent_prandtl"] = value;
  std::ofstream out(physicsPath);
  ASSERT_TRUE(out.good()) << "could not write " << physicsPath;
  out << physics.dump(2);
}

// Replaces the whole turbulence block with an explicit laminar one, keeping a
// turbulent_prandtl that must then be refused.
void makeLaminarWithTurbulentPrandtl(const CaseFixtureCopy& fixture) {
  const std::filesystem::path physicsPath = fixture.path() / "physics.json";
  nlohmann::json physics;
  {
    std::ifstream in(physicsPath);
    ASSERT_TRUE(in.good()) << "could not read " << physicsPath;
    in >> physics;
  }
  physics["turbulence"] = {{"model", "laminar"},
                           {"initial_k", 0.375},
                           {"initial_epsilon", 3.4},
                           {"turbulent_prandtl", 0.9}};
  std::ofstream out(physicsPath);
  ASSERT_TRUE(out.good()) << "could not write " << physicsPath;
  out << physics.dump(2);
}

Real meanTemperature(const ProjectRunResult& run) {
  Real sum = 0.0;
  for (const auto& cell : run.mesh->cells()) {
    sum += run.thermalResult->temperature[cell.id()];
  }
  return sum / static_cast<Real>(run.mesh->numberOfCells());
}

Real maximumTemperatureDifference(const ProjectRunResult& a, const ProjectRunResult& b) {
  Real worst = 0.0;
  for (const auto& cell : a.mesh->cells()) {
    worst = std::max(worst, std::abs(a.thermalResult->temperature[cell.id()] -
                                     b.thermalResult->temperature[cell.id()]));
  }
  return worst;
}

}  // namespace

TEST(TurbulentThermalProductionCase, RunsEndToEndWithTurbulenceAndThermal) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.thermalResult.has_value()) << "the case has a thermal block but no result";
  EXPECT_TRUE(run.thermalResult->converged());
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_TRUE(run.simpleResult->converged())
      << "the flow solve must converge for mu_t to be meaningful";

  // Hot walls, cold inlet: every cell must sit between the two, or the thermal
  // solve is not solving the case that was configured.
  for (const auto& cell : run.mesh->cells()) {
    const Real t = run.thermalResult->temperature[cell.id()];
    EXPECT_GE(t, 300.0 - 1e-6) << "cell " << cell.id();
    EXPECT_LE(t, 400.0 + 1e-6) << "cell " << cell.id();
  }
}

// --- THE REACHABILITY GATE ----------------------------------------------------

TEST(TurbulentThermalProductionCase, TurbulentPrandtlChangesTheThermalResult) {
  // If k_t = mu_t cp / Pr_t never reached the energy equation, Pr_t would scale
  // nothing and these two runs would be bitwise identical. They are the same
  // case file except for one number.
  const CaseFixtureCopy lowFixture(kCaseDirectory);
  setTurbulentPrandtl(lowFixture, 0.5);
  const ProjectRunResult low = ProjectRunner::run(lowFixture.path());

  const CaseFixtureCopy highFixture(kCaseDirectory);
  setTurbulentPrandtl(highFixture, 2.0);
  const ProjectRunResult high = ProjectRunner::run(highFixture.path());

  ASSERT_EQ(low.status, ProjectRunStatus::Converged);
  ASSERT_EQ(high.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(low.thermalResult.has_value());
  ASSERT_TRUE(high.thermalResult.has_value());

  const Real difference = maximumTemperatureDifference(low, high);
  EXPECT_GT(difference, 1.0)
      << "changing turbulent_prandtl from 0.5 to 2.0 moved the temperature field by only "
      << difference << " K. Either k_t is not reaching the energy equation through the case "
      << "path, or Pr_t is not reaching k_t.";

  // DIRECTION. k_t ~ 1/Pr_t, so a lower Pr_t means stronger turbulent thermal
  // diffusion, which on hot walls with a cold inlet means a warmer interior.
  // A coupling wired with Pr_t inverted would change the field and fail here.
  EXPECT_GT(meanTemperature(low), meanTemperature(high))
      << "lower Pr_t did not produce stronger turbulent heat transport";
}

TEST(TurbulentThermalProductionCase, TurbulentPrandtlIsMonotoneThroughTheCasePath) {
  // The same physical statement as the solver-level test, but driven entirely
  // from case files, so it also covers the parse -> build -> resolve chain.
  Real previousMean = std::numeric_limits<Real>::infinity();
  for (const Real prt : {0.5, 0.85, 1.5, 3.0}) {
    const CaseFixtureCopy fixture(kCaseDirectory);
    setTurbulentPrandtl(fixture, prt);
    const ProjectRunResult run = ProjectRunner::run(fixture.path());
    ASSERT_EQ(run.status, ProjectRunStatus::Converged) << "Pr_t = " << prt;
    ASSERT_TRUE(run.thermalResult.has_value());
    const Real mean = meanTemperature(run);
    EXPECT_LT(mean, previousMean) << "raising Pr_t to " << prt
                                  << " did not reduce turbulent thermal transport";
    previousMean = mean;
  }
}

TEST(TurbulentThermalProductionCase, OmittedTurbulentPrandtlUsesTheDocumentedDefault) {
  // The committed case has no turbulent_prandtl. Its result must equal the run
  // that states kDefaultTurbulentPrandtl explicitly, BITWISE -- which is what
  // proves the default is applied in exactly one place rather than approximated
  // somewhere downstream.
  const CaseFixtureCopy implicitFixture(kCaseDirectory);
  const ProjectRunResult implicitRun = ProjectRunner::run(implicitFixture.path());

  const CaseFixtureCopy explicitFixture(kCaseDirectory);
  setTurbulentPrandtl(explicitFixture, cfd::turbulence::kDefaultTurbulentPrandtl);
  const ProjectRunResult explicitRun = ProjectRunner::run(explicitFixture.path());

  ASSERT_EQ(implicitRun.status, ProjectRunStatus::Converged);
  ASSERT_EQ(explicitRun.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(implicitRun.thermalResult.has_value());
  ASSERT_TRUE(explicitRun.thermalResult.has_value());

  for (const auto& cell : implicitRun.mesh->cells()) {
    EXPECT_EQ(implicitRun.thermalResult->temperature[cell.id()],
              explicitRun.thermalResult->temperature[cell.id()])
        << "cell " << cell.id();
  }

  // NON-VACUITY for this test: a DIFFERENT explicit value must not match, or
  // the comparison above would pass whatever the default resolution did.
  const CaseFixtureCopy otherFixture(kCaseDirectory);
  setTurbulentPrandtl(otherFixture, 1.7);
  const ProjectRunResult otherRun = ProjectRunner::run(otherFixture.path());
  ASSERT_EQ(otherRun.status, ProjectRunStatus::Converged);
  EXPECT_GT(maximumTemperatureDifference(implicitRun, otherRun), 1.0)
      << "a different explicit Pr_t produced the same field as the default, so this test "
         "cannot tell whether the default is being applied at all";
}

// --- rejection, through the real parser ---------------------------------------

TEST(TurbulentThermalProductionCase, RejectsTurbulentPrandtlOnALaminarCase) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  makeLaminarWithTurbulentPrandtl(fixture);
  // Refused, not ignored: a laminar case has no mu_t, so a Pr_t there scales
  // nothing and can only mislead the reader of the case file.
  //
  // ProjectRunner reports a bad case through `status` rather than by throwing
  // -- the same convention every other solve outcome uses -- so the assertion
  // is on the status, and the run must NOT have produced a thermal result.
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  EXPECT_EQ(run.status, ProjectRunStatus::InvalidCase);
  EXPECT_FALSE(run.thermalResult.has_value()) << "a rejected case must not have solved anything";
}

TEST(TurbulentThermalProductionCase, RejectsNonPositiveTurbulentPrandtl) {
  for (const Real bad : {0.0, -0.85}) {
    const CaseFixtureCopy fixture(kCaseDirectory);
    setTurbulentPrandtl(fixture, bad);
    const ProjectRunResult run = ProjectRunner::run(fixture.path());
    EXPECT_EQ(run.status, ProjectRunStatus::InvalidCase)
        << "turbulent_prandtl = " << bad << " was accepted";
    // An explicitly invalid value must be REFUSED, never quietly replaced by
    // the default -- that is the difference between a rejection and a silent
    // substitution, and it is the whole reason this test exists.
    EXPECT_FALSE(run.thermalResult.has_value()) << "turbulent_prandtl = " << bad;
  }
}

TEST(TurbulentThermalProductionCase, IsDeterministicThroughTheCasePath) {
  const CaseFixtureCopy first(kCaseDirectory);
  setTurbulentPrandtl(first, 0.9);
  const ProjectRunResult a = ProjectRunner::run(first.path());

  const CaseFixtureCopy second(kCaseDirectory);
  setTurbulentPrandtl(second, 0.9);
  const ProjectRunResult b = ProjectRunner::run(second.path());

  ASSERT_EQ(a.status, ProjectRunStatus::Converged);
  ASSERT_EQ(b.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(a.simpleResult.has_value());
  ASSERT_TRUE(b.simpleResult.has_value());
  EXPECT_EQ(a.simpleResult->iterations, b.simpleResult->iterations);
  EXPECT_EQ(a.thermalResult->iterations, b.thermalResult->iterations);
  for (const auto& cell : a.mesh->cells()) {
    EXPECT_EQ(a.thermalResult->temperature[cell.id()], b.thermalResult->temperature[cell.id()])
        << "cell " << cell.id();
  }
}
