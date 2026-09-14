// P12-NUM-005: the automated three-grid study (GridConvergenceStudy.hpp) --
// solver-status gate, run order/early stop, reference errors, and the
// deterministic JSON / Markdown reports.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cfd/pressure_velocity/SIMPLEResult.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::validation::GridConvergenceStatus;
using cfd::validation::GridSolveOutput;
using cfd::validation::GridSpec;
using cfd::validation::QuantitySpec;

namespace {

// A synthetic "solver": phi = 1 + 0.5 h^2 with h from the grid, plus a
// second quantity psi = 2 - h.
GridSolveOutput syntheticSolve(const GridSpec& spec) {
  const Real h =
      cfd::validation::representativeGridSize(spec.lengthX * spec.lengthY, spec.nx * spec.ny);
  GridSolveOutput out;
  out.acceptance.accepted = true;
  out.acceptance.solverStatus = "Converged";
  out.solverIterations = 10 * spec.nx;
  out.quantities = {{"phi", 1.0 + (0.5 * h * h)}, {"psi", 2.0 - h}};
  return out;
}

std::array<GridSpec, 3> grids() {
  return {GridSpec{"coarse", 8, 4, 2.0, 1.0}, GridSpec{"medium", 16, 8, 2.0, 1.0},
          GridSpec{"fine", 32, 16, 2.0, 1.0}};
}

std::vector<QuantitySpec> quantities() {
  QuantitySpec phi;
  phi.name = "phi";
  phi.description = "synthetic second-order quantity";
  phi.reference = 1.0;
  phi.referenceKind = "analytical";
  phi.options.formalOrder = 2.0;
  phi.options.gridIndependenceThreshold = 0.01;
  QuantitySpec psi;
  psi.name = "psi";
  psi.options.formalOrder = 2.0;  // deliberately wrong: psi is first order
  return {phi, psi};
}

SIMPLEResult convergedResult() {
  SIMPLEResult r;
  r.status = SIMPLEStatus::Converged;
  r.velocity = cfd::fields::VectorField(4, cfd::Vector2{1.0, 0.0});
  r.pressure = cfd::fields::ScalarField(4, 0.0);
  r.massFlux = cfd::fields::SurfaceField(6, 0.0);
  r.globalMassImbalance = 1e-12;
  return r;
}

}  // namespace

TEST(GridConvergenceStudyTest, RunsCoarseToFineAndAnalysesEveryQuantity) {
  std::vector<std::string> order;
  const auto study = cfd::validation::runGridConvergenceStudy(
      "synthetic", "test study", grids(),
      [&](const GridSpec& spec) {
        order.push_back(spec.name);
        return syntheticSolve(spec);
      },
      quantities());
  ASSERT_EQ(order, (std::vector<std::string>{"coarse", "medium", "fine"}));
  ASSERT_TRUE(study.allSolvesAccepted);
  ASSERT_EQ(study.grids.size(), 3u);
  EXPECT_EQ(study.grids[2].cells, 512u);
  EXPECT_DOUBLE_EQ(study.grids[2].h, std::sqrt(2.0 / 512.0));
  EXPECT_GE(study.grids[0].runtimeSeconds, 0.0);

  const auto& phi = study.quantities[0];
  EXPECT_EQ(phi.analysis.status, GridConvergenceStatus::Asymptotic);
  EXPECT_NEAR(*phi.analysis.observedOrder, 2.0, 1e-9);
  EXPECT_NEAR(*phi.analysis.extrapolated21, 1.0, 1e-12);
  EXPECT_NEAR(*phi.extrapolatedErrorVsReference, 0.0, 1e-12);
  EXPECT_NEAR(*phi.errorVsReference[2], 0.5 * std::pow(study.grids[2].h, 2), 1e-15);
  EXPECT_TRUE(phi.analysis.gridIndependent);
  // psi is first order: honestly reported as not asymptotic w.r.t. pf = 2.
  const auto& psi = study.quantities[1];
  EXPECT_NEAR(*psi.analysis.observedOrder, 1.0, 1e-9);
  EXPECT_EQ(psi.analysis.status, GridConvergenceStatus::MonotonicNotAsymptotic);
  EXPECT_FALSE(psi.analysis.gridIndependent);
}

// Solver-status gate: a rejected grid stops the study -- the remaining grid
// is never solved and no statistics are computed.
TEST(GridConvergenceStudyTest, RejectedSolveInvalidatesTheStudy) {
  int fineSolves = 0;
  const auto study = cfd::validation::runGridConvergenceStudy(
      "rejected", "", grids(),
      [&](const GridSpec& spec) {
        GridSolveOutput out = syntheticSolve(spec);
        if (spec.name == "medium") {
          out.acceptance.accepted = false;
          out.acceptance.solverStatus = "Stagnated";
          out.acceptance.reason = "solver status Stagnated (not Converged)";
        }
        if (spec.name == "fine") ++fineSolves;
        return out;
      },
      quantities());
  EXPECT_EQ(fineSolves, 0);
  EXPECT_FALSE(study.allSolvesAccepted);
  EXPECT_EQ(study.grids.size(), 2u);
  EXPECT_NE(study.rejectionReason.find("Stagnated"), std::string::npos);
  for (const auto& q : study.quantities) {
    EXPECT_EQ(q.analysis.status, GridConvergenceStatus::Invalid);
    EXPECT_FALSE(q.analysis.observedOrder.has_value());
    EXPECT_FALSE(q.analysis.gci21.has_value());
    EXPECT_FALSE(q.analysis.gridIndependent);
  }
}

TEST(GridConvergenceStudyTest, MissingQuantityIsInvalid) {
  std::vector<QuantitySpec> specs = quantities();
  specs.push_back(QuantitySpec{"absent", "", std::nullopt, "", {}});
  const auto study =
      cfd::validation::runGridConvergenceStudy("missing", "", grids(), syntheticSolve, specs);
  EXPECT_TRUE(study.allSolvesAccepted);
  EXPECT_EQ(study.quantities[2].analysis.status, GridConvergenceStatus::Invalid);
  EXPECT_NE(study.quantities[2].analysis.diagnostic.find("missing"), std::string::npos);
}

TEST(GridConvergenceStudyTest, SimpleSolveGateRejectsInvalidSolves) {
  const auto accepted = cfd::validation::assessSimpleSolve(convergedResult(), 1e-6);
  EXPECT_TRUE(accepted.accepted);
  EXPECT_EQ(accepted.solverStatus, "Converged");
  EXPECT_TRUE(accepted.reason.empty());

  for (const auto status : {SIMPLEStatus::MaxIterations, SIMPLEStatus::Stagnated,
                            SIMPLEStatus::Diverging, SIMPLEStatus::MomentumFailure,
                            SIMPLEStatus::PressureCorrectionFailure, SIMPLEStatus::NonFiniteState,
                            SIMPLEStatus::InvalidConfiguration, SIMPLEStatus::Cancelled}) {
    SIMPLEResult r = convergedResult();
    r.status = status;
    r.robustness.statusDetail = "detail";
    const auto gate = cfd::validation::assessSimpleSolve(r, 1e-6);
    EXPECT_FALSE(gate.accepted) << gate.solverStatus;
    EXPECT_NE(gate.reason.find("not Converged"), std::string::npos);
  }
  SIMPLEResult nonFinite = convergedResult();
  nonFinite.velocity[2].y = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_FALSE(cfd::validation::assessSimpleSolve(nonFinite, 1e-6).accepted);
  SIMPLEResult badFlux = convergedResult();
  badFlux.massFlux[3] = std::numeric_limits<Real>::infinity();
  EXPECT_FALSE(cfd::validation::assessSimpleSolve(badFlux, 1e-6).accepted);
  SIMPLEResult leaky = convergedResult();
  leaky.globalMassImbalance = 1e-3;
  const auto massGate = cfd::validation::assessSimpleSolve(leaky, 1e-6);
  EXPECT_FALSE(massGate.accepted);
  EXPECT_NE(massGate.reason.find("mass imbalance"), std::string::npos);
}

TEST(GridConvergenceStudyTest, DeterministicReport) {
  const auto a = cfd::validation::runGridConvergenceStudy("synthetic", "d", grids(), syntheticSolve,
                                                          quantities());
  auto b = cfd::validation::runGridConvergenceStudy("synthetic", "d", grids(), syntheticSolve,
                                                    quantities());
  // Runtimes differ between runs; everything else is identical, so pin them
  // to show the report is a pure function of the study.
  for (std::size_t k = 0; k < 3; ++k) b.grids[k].runtimeSeconds = a.grids[k].runtimeSeconds;
  const std::string jsonA = cfd::validation::gridConvergenceReportJson(a);
  EXPECT_EQ(jsonA, cfd::validation::gridConvergenceReportJson(b));
  EXPECT_EQ(cfd::validation::gridConvergenceReportMarkdown(a),
            cfd::validation::gridConvergenceReportMarkdown(b));

  // The report parses and carries the analysis.
  const auto doc = nlohmann::json::parse(jsonA);
  EXPECT_EQ(doc["format_version"], 1);
  EXPECT_EQ(doc["study"], "synthetic");
  EXPECT_EQ(doc["method"]["indexing"], "1 = fine, 2 = medium, 3 = coarse");
  EXPECT_EQ(doc["method"]["relative_quantity_units"], "fraction");
  ASSERT_EQ(doc["grids"].size(), 3u);
  EXPECT_EQ(doc["grids"][0]["level"], "coarse");
  EXPECT_EQ(doc["grids"][2]["cells"], 512);
  ASSERT_EQ(doc["quantities"].size(), 2u);
  const auto& phi = doc["quantities"][0];
  EXPECT_EQ(phi["name"], "phi");
  EXPECT_EQ(phi["analysis"]["status"], "asymptotic");
  EXPECT_NEAR(phi["analysis"]["observed_order"].get<double>(), 2.0, 1e-9);
  EXPECT_NEAR(phi["analysis"]["richardson_extrapolated"].get<double>(), 1.0, 1e-12);
  EXPECT_TRUE(phi["analysis"]["gci_fine_medium"].is_number());
  EXPECT_TRUE(phi["analysis"]["grid_independent"].get<bool>());
  EXPECT_EQ(phi["reference"]["kind"], "analytical");
  // Unset optionals are null, never NaN (which JSON cannot represent).
  EXPECT_TRUE(doc["quantities"][1]["reference"].is_null());
  EXPECT_TRUE(phi["analysis"]["oscillation_uncertainty"].is_null());

  const std::string markdown = cfd::validation::gridConvergenceReportMarkdown(a);
  EXPECT_NE(markdown.find("| fine | 32 x 16 | 512 |"), std::string::npos);
  EXPECT_NE(markdown.find("asymptotic"), std::string::npos);
}

// The report validator accepts every report the library writes (including
// oscillatory and rejected studies) and pinpoints schema / consistency
// violations in tampered ones -- without throwing on wrong JSON types.
TEST(GridConvergenceStudyTest, ReportSchemaValidation) {
  using cfd::validation::validateGridConvergenceReport;
  const auto withOscillation = [](const GridSpec& spec) {
    GridSolveOutput out = syntheticSolve(spec);
    out.quantities.emplace_back("osc", spec.nx == 8 ? 1.0 : (spec.nx == 16 ? 1.2 : 1.1));
    return out;
  };
  std::vector<QuantitySpec> specs = quantities();
  specs.push_back(QuantitySpec{"osc", "oscillating", std::nullopt, "", {}});
  const auto study =
      cfd::validation::runGridConvergenceStudy("synthetic", "d", grids(), withOscillation, specs);
  ASSERT_EQ(study.quantities[2].analysis.status, GridConvergenceStatus::Oscillatory);
  const std::string json = cfd::validation::gridConvergenceReportJson(study);
  const auto clean = validateGridConvergenceReport(json);
  EXPECT_TRUE(clean.empty()) << clean.front();

  const auto rejected = cfd::validation::runGridConvergenceStudy(
      "rejected", "", grids(),
      [](const GridSpec& spec) {
        GridSolveOutput out = syntheticSolve(spec);
        if (spec.name == "medium") {
          out.acceptance.accepted = false;
          out.acceptance.solverStatus = "Diverging";
          out.acceptance.reason = "solver status Diverging (not Converged)";
        }
        return out;
      },
      quantities());
  const auto rejectedProblems =
      validateGridConvergenceReport(cfd::validation::gridConvergenceReportJson(rejected));
  EXPECT_TRUE(rejectedProblems.empty()) << rejectedProblems.front();

  // Each tampering must be reported, with a message naming the culprit.
  const auto tampered = [&](const auto& mutate) {
    auto doc = nlohmann::ordered_json::parse(json);
    mutate(doc);
    return validateGridConvergenceReport(doc.dump());
  };
  const auto mentions = [](const std::vector<std::string>& problems, const std::string& text) {
    for (const auto& p : problems) {
      if (p.find(text) != std::string::npos) return true;
    }
    return false;
  };
  // psi is monotonic_not_asymptotic: claiming grid independence is inconsistent.
  EXPECT_TRUE(
      mentions(tampered([](auto& d) { d["quantities"][1]["analysis"]["grid_independent"] = true; }),
               "grid_independent"));
  // An oscillatory quantity must not carry an observed order.
  EXPECT_TRUE(
      mentions(tampered([](auto& d) { d["quantities"][2]["analysis"]["observed_order"] = 1.0; }),
               "observed_order"));
  EXPECT_TRUE(
      mentions(tampered([](auto& d) { d["quantities"][0]["analysis"].erase("gci_fine_medium"); }),
               "missing key 'gci_fine_medium'"));
  EXPECT_TRUE(mentions(tampered([](auto& d) {
                         d["grids"][0]["level"] = "fine";
                         d["grids"][2]["level"] = "coarse";
                       }),
                       "level"));
  EXPECT_TRUE(mentions(tampered([](auto& d) { d["grids"][2]["h"] = 1.0; }), "must be smaller"));
  EXPECT_TRUE(mentions(tampered([](auto& d) { d["grids"][1]["cells"] = 7; }), "cells != nx * ny"));
  EXPECT_TRUE(mentions(tampered([](auto& d) { d["format_version"] = 2; }), "format_version"));
  EXPECT_TRUE(
      mentions(tampered([](auto& d) { d["quantities"][0]["analysis"]["status"] = "converged"; }),
               "unknown status"));
  EXPECT_TRUE(
      mentions(tampered([](auto& d) { d["quantities"][0]["analysis"]["status"] = 3; }), "status"));
  EXPECT_TRUE(
      mentions(tampered([](auto& d) { d["quantities"][0]["analysis"]["asymptotic_ratio"] = 1.5; }),
               "asymptotic_ratio"));
  EXPECT_TRUE(mentions(validateGridConvergenceReport("{ not json"), "not valid JSON"));

  // File round trip.
  const auto path =
      (std::filesystem::temp_directory_path() / "cfd_grid_convergence_report_test.json").string();
  cfd::validation::writeGridConvergenceReport(path, study);
  const auto fileProblems = cfd::validation::validateGridConvergenceReportFile(path);
  EXPECT_TRUE(fileProblems.empty()) << fileProblems.front();
  std::filesystem::remove(path);
  EXPECT_TRUE(mentions(cfd::validation::validateGridConvergenceReportFile(path), "cannot open"));
}
