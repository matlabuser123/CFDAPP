// P12-NUM-004: solver.json's optional "robustness" block -- parsing,
// backward-compatible defaults, clear rejection of invalid values, the
// CaseBuilder mapping into SIMPLESettings, the CaseWriter round trip, and
// the GMRES linear-solver type.
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"

using cfd::CaseConfigurationError;
using cfd::io::CaseBuilder;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::solver::ConvergenceCriterion;
using cfd::solver::SolverRobustnessSettings;
using cfd::testutil::CaseFixture;

namespace {

std::string solverJson(const std::string& extra, const std::string& pressureType = "BiCGSTAB") {
  return R"({
    "type": "SIMPLE",
    "max_iterations": 1000,
    "velocity_relaxation": 0.7,
    "pressure_relaxation": 0.3,
    "velocity_tolerance": 1e-6,
    "pressure_tolerance": 1e-6,
    "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": ")" +
         pressureType + R"(", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000})" +
         (extra.empty() ? "" : ",\n" + extra) + "\n}";
}

CaseDefinition readWith(const std::string& extra, const std::string& pressureType = "BiCGSTAB") {
  CaseFixture fixture;
  fixture.write("solver.json", solverJson(extra, pressureType));
  return CaseReader{}.read(fixture.directory());
}

void expectRejected(const std::string& extra, const std::string& fieldFragment) {
  CaseFixture fixture;
  fixture.write("solver.json", solverJson(extra));
  try {
    (void)CaseReader{}.read(fixture.directory());
    ADD_FAILURE() << "accepted: " << extra;
  } catch (const CaseConfigurationError& error) {
    EXPECT_NE(std::string(error.what()).find(fieldFragment), std::string::npos)
        << "message '" << error.what() << "' does not name " << fieldFragment;
  }
}

const char* kFullBlock = R"("robustness": {
      "convergence_criterion": "normalized",
      "normalization": {"reference_iterations": 3, "velocity_tolerance": 1e-5,
                        "pressure_tolerance": 2e-5},
      "stagnation_detection": {"enabled": true, "window": 40, "min_relative_improvement": 0.02,
                               "start_iteration": 80},
      "divergence_detection": {"enabled": true, "window": 8, "growth_factor": 50.0,
                               "start_iteration": 5},
      "adaptive_relaxation": {"enabled": true, "min_velocity": 0.3, "max_velocity": 0.8,
                              "min_pressure": 0.1, "max_pressure": 0.5},
      "linear_solver_fallback": {"enabled": true, "max_attempts": 2}
    })";

}  // namespace

TEST(RobustnessConfigTest, AbsentBlockDefaultsToEveryFeatureOff) {
  CaseFixture fixture;  // the fixture's own solver.json has no robustness block
  const CaseDefinition definition = CaseReader{}.read(fixture.directory());
  EXPECT_EQ(definition.solver.robustness, SolverRobustnessSettings{});
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_EQ(setup.solverSettings.robustness, SolverRobustnessSettings{});
  // An empty block is the same.
  EXPECT_EQ(readWith(R"("robustness": {})").solver.robustness, SolverRobustnessSettings{});
}

TEST(RobustnessConfigTest, FullBlockParsesAndReachesSolverSettings) {
  const CaseDefinition definition = readWith(kFullBlock);
  const SolverRobustnessSettings& r = definition.solver.robustness;
  EXPECT_EQ(r.convergenceCriterion, ConvergenceCriterion::Normalized);
  EXPECT_EQ(r.normalization.referenceIterations, 3u);
  EXPECT_DOUBLE_EQ(r.normalization.velocityTolerance, 1e-5);
  EXPECT_DOUBLE_EQ(r.normalization.pressureTolerance, 2e-5);
  EXPECT_TRUE(r.stagnation.enabled);
  EXPECT_EQ(r.stagnation.window, 40u);
  EXPECT_DOUBLE_EQ(r.stagnation.minRelativeImprovement, 0.02);
  EXPECT_EQ(r.stagnation.startIteration, 80u);
  EXPECT_TRUE(r.divergence.enabled);
  EXPECT_EQ(r.divergence.window, 8u);
  EXPECT_DOUBLE_EQ(r.divergence.growthFactor, 50.0);
  EXPECT_EQ(r.divergence.startIteration, 5u);
  EXPECT_TRUE(r.adaptiveRelaxation.enabled);
  EXPECT_DOUBLE_EQ(r.adaptiveRelaxation.minVelocity, 0.3);
  EXPECT_DOUBLE_EQ(r.adaptiveRelaxation.maxVelocity, 0.8);
  EXPECT_DOUBLE_EQ(r.adaptiveRelaxation.minPressure, 0.1);
  EXPECT_DOUBLE_EQ(r.adaptiveRelaxation.maxPressure, 0.5);
  EXPECT_TRUE(r.linearSolverFallback.enabled);
  EXPECT_EQ(r.linearSolverFallback.maxAttempts, 2u);
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_EQ(setup.solverSettings.robustness, r);
}

TEST(RobustnessConfigTest, PartialBlockKeepsDefaultsForAbsentKeys) {
  const auto r =
      readWith(R"("robustness": {"stagnation_detection": {"enabled": true}})").solver.robustness;
  EXPECT_TRUE(r.stagnation.enabled);
  SolverRobustnessSettings expected;
  expected.stagnation.enabled = true;
  EXPECT_EQ(r, expected);
}

TEST(RobustnessConfigTest, InvalidValuesAreRejectedWithTheFieldNamed) {
  expectRejected(R"("robustness": {"stagnation_detection": {"window": 0}})",
                 "stagnation_detection.window");
  expectRejected(R"("robustness": {"stagnation_detection": {"window": 1}})",
                 "stagnation_detection.window");
  expectRejected(R"("robustness": {"stagnation_detection": {"window": -3}})",
                 "stagnation_detection.window");
  expectRejected(R"("robustness": {"divergence_detection": {"window": 0}})",
                 "divergence_detection.window");
  expectRejected(R"("robustness": {"divergence_detection": {"growth_factor": 1.0}})",
                 "divergence_detection.growth_factor");
  expectRejected(R"("robustness": {"divergence_detection": {"growth_factor": 0.5}})",
                 "divergence_detection.growth_factor");
  expectRejected(R"("robustness": {"adaptive_relaxation": {"min_velocity": 0.0}})",
                 "adaptive_relaxation.min_velocity");
  expectRejected(R"("robustness": {"adaptive_relaxation": {"max_pressure": 1.5}})",
                 "adaptive_relaxation.max_pressure");
  expectRejected(
      R"("robustness": {"adaptive_relaxation": {"min_velocity": 0.8, "max_velocity": 0.5}})",
      "adaptive_relaxation.min_velocity");
  expectRejected(R"("robustness": {"adaptive_relaxation": {"enabled": true, "min_velocity": 0.8}})",
                 "velocity_relaxation");  // 0.7 outside [0.8, 0.9]
  expectRejected(R"("robustness": {"linear_solver_fallback": {"max_attempts": -1}})",
                 "linear_solver_fallback.max_attempts");
  expectRejected(R"("robustness": {"linear_solver_fallback": {"max_attempts": 4}})",
                 "linear_solver_fallback.max_attempts");
  expectRejected(R"("robustness": {"linear_solver_fallback": {"enabled": 1}})",
                 "linear_solver_fallback.enabled");
  expectRejected(R"("robustness": {"normalization": {"reference_iterations": 0}})",
                 "normalization.reference_iterations");
  expectRejected(R"("robustness": {"normalization": {"velocity_tolerance": 1.0}})",
                 "normalization.velocity_tolerance");
  expectRejected(R"("robustness": {"stagnation_detection": {"min_relative_improvement": 0.0}})",
                 "stagnation_detection.min_relative_improvement");
  expectRejected(R"("robustness": {"convergence_criterion": "relative"})", "convergence_criterion");
  expectRejected(R"("robustness": {"stagnation": {"enabled": true}})",
                 "robustness");  // unknown key
  expectRejected(R"("robustness": {"divergence_detection": {"enabled": true, "factor": 5}})",
                 "divergence_detection");
  expectRejected(R"("robustness": 3)", "robustness");
}

TEST(RobustnessConfigTest, NonDefaultBlockRoundTripsAndDefaultIsNotWritten) {
  CaseFixture source;
  source.write("solver.json", solverJson(kFullBlock));
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  const CaseDefinition reread = CaseReader{}.read(target.directory());
  EXPECT_EQ(reread.solver.robustness, original.solver.robustness);

  CaseFixture plain;
  const CaseDefinition defaults = CaseReader{}.read(plain.directory());
  CaseFixture plainTarget;
  CaseWriter::write(plainTarget.directory(), defaults);
  std::ifstream in(plainTarget.directory() / "solver.json");
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(text.find("robustness"), std::string::npos);
}

TEST(RobustnessConfigTest, GmresLinearSolverTypeIsAccepted) {
  const CaseDefinition definition = readWith("", "GMRES");
  EXPECT_EQ(definition.solver.pressureSolver.type, "GMRES");
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_EQ(setup.solverSettings.pressureSolver.type, cfd::algebra::LinearSolverType::GMRES);
  CaseFixture fixture;
  fixture.write("solver.json", solverJson("", "MINRES"));
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}
