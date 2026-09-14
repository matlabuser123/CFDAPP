// P12-NUM-007: unit-level checks of the production-validation building
// blocks -- sample-point error norms (cfd/validation/ErrorNorms.hpp), the
// validation record / report (cfd/validation/ProductionValidation.hpp) and
// the SIMPLEResult linear-iteration counters used as the cost metric of the
// scheme comparison.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/ProductionValidation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
namespace validation = cfd::validation;

namespace {

// A converged 8x8 lid-driven cavity at Re = 10 (fast).
cfd::pressure_velocity::SIMPLEResult smallCavity() {
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  cfd::boundary::BoundaryConditionSet velocity;
  velocity.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));
  cfd::boundary::BoundaryConditionSet pressure;
  for (const auto& patch : mesh.boundaryPatches()) {
    pressure.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  cfd::pressure_velocity::SIMPLESettings settings;
  settings.maxIterations = 3000;
  settings.velocityTolerance = settings.pressureTolerance = settings.continuityTolerance = 1e-6;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  settings.robustness.linearSolverFallback.enabled = true;
  const cfd::pressure_velocity::SIMPLE simple(settings, 0);
  return simple.solve(mesh, cfd::physics::FluidProperties(1.0, 0.1), velocity, pressure,
                      cfd::fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                      cfd::fields::ScalarField(mesh.numberOfCells(), 0.0));
}

validation::ValidationRun sampleRun(const std::string& mesh, Index n, Real l2, double seconds) {
  validation::ValidationRun run;
  run.caseName = "case";
  run.reynolds = 100.0;
  run.scheme = "quick";
  run.level.name = mesh;
  run.level.nx = n;
  run.level.ny = n;
  run.level.cells = n * n;
  run.level.h = 1.0 / static_cast<Real>(n);
  run.level.solverStatus = "Converged";
  run.level.accepted = true;
  run.level.iterations = 10 * n;
  run.level.massImbalance = 0.0;
  validation::ErrorNorms e;
  e.l1 = 0.5 * l2;
  e.l2 = l2;
  e.linf = 2.0 * l2;
  e.cells = 17;
  e.volume = 17.0;
  run.level.errors.emplace_back("u_centerline", e);
  run.level.diagnostics.emplace_back("u_center", -0.2 + l2);
  run.level.runtimeSeconds = seconds;
  run.checks.push_back({"solve_accepted", true, "Converged"});
  return run;
}

}  // namespace

TEST(SampleErrorNormsTest, KnownValues) {
  // errors 1, -2, 2 -> L1 5/3, L2 sqrt(3), Linf 2.
  const auto norms = validation::computeSampleErrorNorms({1.0, 0.0, 5.0}, {0.0, 2.0, 3.0});
  EXPECT_DOUBLE_EQ(norms.l1, 5.0 / 3.0);
  EXPECT_DOUBLE_EQ(norms.l2, std::sqrt(3.0));
  EXPECT_DOUBLE_EQ(norms.linf, 2.0);
  EXPECT_EQ(norms.cells, 3u);
  EXPECT_DOUBLE_EQ(norms.volume, 3.0);
  EXPECT_THROW((void)validation::computeSampleErrorNorms({1.0}, {1.0, 2.0}),
               cfd::InvalidArgumentError);
  EXPECT_THROW((void)validation::computeSampleErrorNorms({}, {}), cfd::InvalidArgumentError);
  EXPECT_THROW(
      (void)validation::computeSampleErrorNorms({std::numeric_limits<Real>::quiet_NaN()}, {0.0}),
      cfd::InvalidArgumentError);
}

TEST(ProductionValidationTest, PassedRequiresAcceptanceAndEveryCheck) {
  auto run = sampleRun("20x20", 20, 1e-2, 1.0);
  EXPECT_EQ(run.id(), "case/20x20/quick");
  EXPECT_TRUE(run.passed());
  run.checks.push_back({"ghia_error", false, "too far"});
  EXPECT_FALSE(run.passed());
  run.checks.pop_back();
  run.level.accepted = false;
  EXPECT_FALSE(run.passed());

  validation::ValidationReport report;
  EXPECT_FALSE(report.passed()) << "an empty report never passes";
  report.runs.push_back(sampleRun("20x20", 20, 1e-2, 1.0));
  EXPECT_TRUE(report.passed());
  report.gates.push_back({"trend", false, ""});
  EXPECT_FALSE(report.passed());
  EXPECT_EQ(report.run("case/20x20/quick").level.nx, 20u);
  EXPECT_THROW((void)report.run("nope"), cfd::InvalidArgumentError);
}

TEST(ProductionValidationTest, MakeSimpleRunRecordsSolverAndCost) {
  const auto result = smallCavity();
  ASSERT_TRUE(result.converged());
  const auto run = validation::makeSimpleValidationRun(
      "cavity", 10.0, "upwind", validation::GridSpec{"", 8, 8, 1.0, 1.0}, result, 1e-6, 0.25);
  EXPECT_EQ(run.level.name, "8x8");
  EXPECT_EQ(run.level.cells, 64u);
  EXPECT_DOUBLE_EQ(run.level.h, 1.0 / 8.0);
  EXPECT_TRUE(run.level.accepted);
  EXPECT_EQ(run.level.solverStatus, "Converged");
  EXPECT_EQ(run.level.iterations, result.iterations);
  ASSERT_TRUE(run.level.massImbalance.has_value());
  bool foundMomentum = false;
  for (const auto& [name, value] : run.level.diagnostics) {
    if (name == "momentum_linear_iterations") {
      foundMomentum = true;
      EXPECT_DOUBLE_EQ(value, static_cast<Real>(result.momentumLinearIterations));
    }
  }
  EXPECT_TRUE(foundMomentum);
  // A rejected solve (mass gate impossible to meet) is recorded as rejected.
  const auto rejected = validation::makeSimpleValidationRun(
      "cavity", 10.0, "upwind", validation::GridSpec{"", 8, 8, 1.0, 1.0}, result, -1.0, 0.25);
  EXPECT_FALSE(rejected.level.accepted);
  EXPECT_FALSE(rejected.level.rejectionReason.empty());
}

// The counters are pure observability: every successful linear solve adds
// its iterations, so a converged run has both > 0 and at least one
// pressure-correction iteration per outer iteration.
TEST(ProductionValidationTest, SimpleCountsLinearIterations) {
  const auto a = smallCavity();
  ASSERT_TRUE(a.converged());
  EXPECT_GT(a.momentumLinearIterations, 0u);
  EXPECT_GE(a.pressureLinearIterations, a.iterations);
  const auto b = smallCavity();
  EXPECT_EQ(a.momentumLinearIterations, b.momentumLinearIterations);
  EXPECT_EQ(a.pressureLinearIterations, b.pressureLinearIterations);
}

TEST(ProductionValidationTest, GridStudyEntryAndSequenceOrder) {
  // Errors halving per refinement with h halving: observed order 1.
  const auto r1 = sampleRun("20x20", 20, 8e-3, 1.0);
  const auto r2 = sampleRun("40x40", 40, 4e-3, 2.0);
  const auto r3 = sampleRun("80x80", 80, 2e-3, 4.0);
  const auto entry = validation::toGridStudyEntry(r2, 1.0, 1.0, {"u_center", "absent"});
  EXPECT_EQ(entry.spec.name, "40x40");
  EXPECT_DOUBLE_EQ(entry.h, 1.0 / 40.0);
  EXPECT_TRUE(entry.output.acceptance.accepted);
  ASSERT_EQ(entry.output.quantities.size(), 1u);
  EXPECT_EQ(entry.output.quantities[0].first, "u_center");
  EXPECT_DOUBLE_EQ(entry.runtimeSeconds, 2.0);

  validation::GridConvergenceOptions options;
  options.formalOrder = 1.0;
  const auto order = validation::computeRunSequenceOrder({&r1, &r2, &r3}, "u_centerline",
                                                         validation::NormKind::L2, options);
  ASSERT_TRUE(order.finestOrder().has_value());
  EXPECT_NEAR(*order.finestOrder(), 1.0, 1e-9);
  ASSERT_EQ(order.reductionFactors.size(), 2u);
  EXPECT_NEAR(*order.reductionFactors[0], 2.0, 1e-12);
}

TEST(ProductionValidationTest, ReportIsDeterministicExceptRuntime) {
  validation::ValidationReport report;
  report.name = "study";
  report.reference = "reference";
  report.coefficients = {{"reynolds", 100.0}};
  report.configuration = {{"scheme", "quick"}};
  report.runs = {sampleRun("20x20", 20, 8e-3, 1.0), sampleRun("40x40", 40, 4e-3, 2.0),
                 sampleRun("80x80", 80, 2e-3, 3.0)};
  report.runs[1].level.massImbalance.reset();  // unset -> null
  std::vector<validation::GridStudyEntry> entries;
  for (const auto& run : report.runs)
    entries.push_back(validation::toGridStudyEntry(run, 1, 1, {"u_center"}));
  validation::QuantitySpec q;
  q.name = "u_center";
  report.gridConvergence.push_back(validation::analyzeGridStudy("gc", "", std::move(entries), {q}));
  report.limitations = {"none"};

  const std::string first = validation::validationReportJson(report);
  EXPECT_EQ(first, validation::validationReportJson(report));
  const auto doc = nlohmann::json::parse(first);
  EXPECT_EQ(doc["kind"], "production_validation");
  EXPECT_EQ(doc["runs"].size(), 3u);
  EXPECT_TRUE(doc["runs"][1]["mass_imbalance"].is_null());
  EXPECT_EQ(doc["runs"][0]["errors"]["u_centerline"]["samples"], 17);
  EXPECT_EQ(doc["grid_convergence"].size(), 1u);
  EXPECT_TRUE(doc["passed"].get<bool>());
  EXPECT_EQ(doc["runtime"]["runs"].size(), 3u);

  // Changing only the runtimes changes only the runtime fields.
  auto slower = report;
  for (auto& run : slower.runs) run.level.runtimeSeconds *= 10.0;
  for (auto& study : slower.gridConvergence) {
    for (auto& grid : study.grids) grid.runtimeSeconds *= 10.0;
  }
  auto a = nlohmann::json::parse(first);
  auto b = nlohmann::json::parse(validation::validationReportJson(slower));
  EXPECT_NE(a, b);
  a.erase("runtime");
  b.erase("runtime");
  for (auto* d : {&a, &b}) {
    for (auto& study : (*d)["grid_convergence"]) {
      for (auto& grid : study["grids"]) grid.erase("runtime_seconds");
    }
  }
  EXPECT_EQ(a, b);
  EXPECT_NE(validation::validationReportMarkdown(report).find("Overall: PASSED"),
            std::string::npos);

  // Report hygiene (committed reports must pass `git diff --check`): no
  // trailing whitespace on any line, exactly one newline at the end -- for
  // the validation report and the embedded grid-convergence report alike.
  for (const std::string& md :
       {validation::validationReportMarkdown(report),
        validation::gridConvergenceReportMarkdown(report.gridConvergence[0])}) {
    ASSERT_FALSE(md.empty());
    EXPECT_EQ(md.back(), '\n');
    EXPECT_EQ(md.find("\n\n", md.size() - 2), std::string::npos) << "trailing blank line";
    EXPECT_EQ(md.find(" \n"), std::string::npos) << "trailing space";
    EXPECT_EQ(md.find("\t\n"), std::string::npos) << "trailing tab";
  }
}
