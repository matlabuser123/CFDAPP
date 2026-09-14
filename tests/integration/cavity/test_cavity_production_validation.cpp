// P12-NUM-007 -- Production validation: lid-driven cavity at Re = 100 and
// Re = 1000 against Ghia, Ghia & Shin (1982) Tables I/II (both Reynolds
// numbers; provenance in validation/ghia/README.md).
//
// Configuration of record: QUICK convection (the bounded, TVD-limited
// higher-order scheme -- upwind's first-order error dominates the Ghia
// comparison at every practical grid; see SchemeValidation), the documented
// per-grid SIMPLE settings of test_cavity_ghia.cpp for every Reynolds
// number and scheme (cavitySettings), outer tolerances 1e-6, P12-NUM-004
// linear-solver fallback enabled. Nothing about the solver is tuned per
// case.
//
// Two separate questions, never mixed (P12-NUM-005 discipline):
//   1. solution convergence -- observed order / Richardson / GCI of the
//      sampled Ghia-station values, computed from OUR solutions only;
//   2. benchmark agreement -- L1/L2/Linf of the centerline profiles against
//      Ghia's tabulated values, which are themselves a 129x129 numerical
//      solution. Measured: at Re = 100 the higher-order schemes' distance to
//      Ghia stops decreasing beyond 40x40 (QUICK u L2 1.45e-3 at 40,
//      2.09e-3 at 80; central 2.01e-3 at 80 with outer tolerance 1e-6 AND
//      1e-8, 2.14e-3 at 160) -- a benchmark/comparison floor of ~2e-3
//      (u) / ~4.4e-3 (v), not iterative or discretisation error. The 40x40
//      value sits below the floor by error cancellation.
//
// ghia_error bounds are regression bounds: the measured value of each
// (Re, grid) with ~30 % margin, recorded in results/p12-num-007/ -- they
// lock in the measured agreement; they are not literature targets.
//
// Default suite: Re100Grid20 (the representative production validation in
// CI). Every other grid and both studies are DISABLED_ (tens of seconds to
// tens of minutes each; run with --gtest_also_run_disabled_tests, in the
// Release build for the 160x160 grids) -- the explicit runs are recorded
// in results/p12-num-007/.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

#include "CavityProductionCase.hpp"
#include "cfd/validation/ProductionValidation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::discretization::ConvectionScheme;
using cfd::validation::ValidationReport;
using cfd::validation::ValidationRun;
using cfd::validation::cavity::CavityRunSpec;

namespace {

const char* kOutputDirectory = "results/validation/production";

std::string ghiaReference(Real reynolds) {
  return "Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Table I (u(0.5, y)) and Table II "
         "(v(x, 0.5)), Re = " +
         std::to_string(static_cast<int>(reynolds)) +
         " column, 17 stations each (validation/ghia/ghia_re" +
         std::to_string(static_cast<int>(reynolds)) + "_{u,v}.csv)";
}

void describeCavity(ValidationReport& report, Real reynolds) {
  report.reference = ghiaReference(reynolds);
  report.coefficients = {{"reynolds", reynolds},
                         {"lid_velocity", cfd::validation::cavity::kLidVelocity},
                         {"density", cfd::validation::cavity::kDensity},
                         {"viscosity", cfd::validation::cavity::kDensity *
                                           cfd::validation::cavity::kLidVelocity / reynolds},
                         {"cavity_size", 1.0}};
  report.configuration = {
      {"geometry", "unit square, n x n uniform Cartesian cells"},
      {"boundaries", "no-slip walls, lid y = 1 moving at (1, 0); zero-gradient pressure"},
      {"algorithm", "SIMPLE, collocated, alpha_u 0.7, alpha_p 0.3, outer tolerances 1e-6"},
      {"linear_solvers",
       "BiCGSTAB; momentum 1e-10/1e-8; pressure per grid (20: 1e-10/1e-8, 40: 1e-8/1e-6, >= 80: "
       "1e-7/1e-5); P12-NUM-004 fallback enabled"},
      {"initial_condition", "u = 0, p = 0"},
      {"sampling",
       "centerlines by linear interpolation between the straddling cell columns/rows, "
       "exact wall/lid values as end anchors; errors at Ghia's stations"}};
}

void expectRunPassed(const ValidationRun& run) {
  EXPECT_TRUE(run.level.accepted) << run.id() << ": " << run.level.rejectionReason;
  for (const auto& check : run.checks) {
    EXPECT_TRUE(check.passed) << run.id() << " " << check.name << ": " << check.detail;
  }
}

void printRun(const ValidationRun& run) {
  std::printf("%s: %s, %llu iterations, %.1f s\n", run.id().c_str(), run.level.solverStatus.c_str(),
              static_cast<unsigned long long>(run.level.iterations), run.level.runtimeSeconds);
  for (const auto& [name, norms] : run.level.errors) {
    std::printf("  %s: L1 %.4e  L2 %.4e  Linf %.4e\n", name.c_str(), norms.l1, norms.l2,
                norms.linf);
  }
}

std::string meshDirectory(const CavityRunSpec& spec) {
  return std::string(kOutputDirectory) + "/" + cfd::validation::cavity::caseName(spec.reynolds) +
         "/" + std::to_string(spec.n) + "x" + std::to_string(spec.n) + "_" +
         std::string(cfd::discretization::convectionSchemeName(spec.scheme));
}

// One grid: solve, check, write a single-run report next to its CSVs.
ValidationRun validateGrid(const CavityRunSpec& spec, Real maxUL2, Real maxVL2) {
  const std::string directory = meshDirectory(spec);
  ValidationRun run = cfd::validation::cavity::runCavity(spec, directory);
  cfd::validation::cavity::addGhiaErrorCheck(run, maxUL2, maxVL2);
  cfd::validation::cavity::addBoundednessCheck(run);
  ValidationReport report;
  report.name = run.id();
  report.description = "Lid-driven cavity, single grid";
  describeCavity(report, spec.reynolds);
  report.runs.push_back(run);
  cfd::validation::writeValidationReport(directory + "/validation.json", report);
  printRun(run);
  return run;
}

void writeReport(const ValidationReport& report, const std::string& stem) {
  cfd::validation::writeValidationReport(std::string(kOutputDirectory) + "/" + stem + ".json",
                                         report);
  std::ofstream md(std::string(kOutputDirectory) + "/" + stem + ".md", std::ios::binary);
  md << cfd::validation::validationReportMarkdown(report);
  std::printf("\n%s", cfd::validation::validationReportMarkdown(report).c_str());
}

const cfd::validation::ErrorNorms& error(const ValidationRun& run, const char* name) {
  for (const auto& [key, norms] : run.level.errors) {
    if (key == name) return norms;
  }
  throw std::runtime_error(std::string("missing error ") + name);
}

}  // namespace

// ---------------------------------------------------------------- Re = 100

// Default suite. Measured (QUICK, 20x20): u L2 5.69e-3, v L2 4.82e-3.
TEST(CavityGhiaValidation, Re100Grid20) {
  const auto run = validateGrid({100.0, 20, ConvectionScheme::QUICK}, 0.0075, 0.0065);
  expectRunPassed(run);
}

// Measured: u L2 1.45e-3, v L2 2.99e-3 (~1 min Release, several Debug).
TEST(CavityGhiaValidation, DISABLED_Re100Grid40) {
  expectRunPassed(validateGrid({100.0, 40, ConvectionScheme::QUICK}, 0.002, 0.004));
}

// Measured: u L2 2.09e-3, v L2 4.51e-3 -- the benchmark floor (file header).
TEST(CavityGhiaValidation, DISABLED_Re100Grid80) {
  expectRunPassed(validateGrid({100.0, 80, ConvectionScheme::QUICK}, 0.003, 0.006));
}

TEST(CavityGhiaValidation, DISABLED_Re100Grid160) {
  expectRunPassed(validateGrid({100.0, 160, ConvectionScheme::QUICK}, 0.003, 0.006));
}

// --------------------------------------------------------------- Re = 1000

// Measured: u L2 5.44e-2, v L2 5.17e-2.
TEST(CavityGhiaValidation, DISABLED_Re1000Grid40) {
  expectRunPassed(validateGrid({1000.0, 40, ConvectionScheme::QUICK}, 0.07, 0.07));
}

// Measured: u L2 9.59e-3, v L2 7.63e-3 (~3 min Release).
TEST(CavityGhiaValidation, DISABLED_Re1000Grid80) {
  expectRunPassed(validateGrid({1000.0, 80, ConvectionScheme::QUICK}, 0.0125, 0.01));
}

// Measured: u L2 2.52e-3, v L2 5.41e-3 (~9 min Release).
TEST(CavityGhiaValidation, DISABLED_Re1000Grid160) {
  expectRunPassed(validateGrid({1000.0, 160, ConvectionScheme::QUICK}, 0.0035, 0.007));
}

// ------------------------------------------------------------------ studies

// cavity_re100.json: QUICK 20/40/80/160 (configuration of record) and
// upwind 20/40/80 (first-order reference sequence), each run with its own
// checks; NUM-005 grid convergence of the Ghia-station values for QUICK
// (20,40,80) and (40,80,160) and upwind (20,40,80); orders of the Ghia
// errors (reported, see gates).
TEST(CavityGhiaValidation, DISABLED_Re100Study) {
  using cfd::validation::NormKind;
  ValidationReport report;
  report.name = "cavity_re100";
  report.description =
      "Lid-driven cavity Re = 100: QUICK (configuration of record) 20/40/80/160 and first-order "
      "upwind 20/40/80";
  describeCavity(report, 100.0);
  const struct {
    Index n;
    ConvectionScheme scheme;
    Real maxU;
    Real maxV;
  } grids[] = {{20, ConvectionScheme::QUICK, 0.0075, 0.0065},
               {40, ConvectionScheme::QUICK, 0.002, 0.004},
               {80, ConvectionScheme::QUICK, 0.003, 0.006},
               {160, ConvectionScheme::QUICK, 0.003, 0.006},
               // upwind: measured 2.11e-2/1.45e-2, 1.02e-2/6.45e-3, 4.34e-3/3.57e-3
               {20, ConvectionScheme::Upwind, 0.0275, 0.019},
               {40, ConvectionScheme::Upwind, 0.0135, 0.0085},
               {80, ConvectionScheme::Upwind, 0.0057, 0.0047}};
  for (const auto& g : grids) {
    report.runs.push_back(validateGrid({100.0, g.n, g.scheme}, g.maxU, g.maxV));
  }
  for (const auto& run : report.runs) expectRunPassed(run);
  ASSERT_TRUE(report.runs.size() == 7);
  const ValidationRun* q[] = {&report.runs[0], &report.runs[1], &report.runs[2], &report.runs[3]};
  const ValidationRun* u[] = {&report.runs[4], &report.runs[5], &report.runs[6]};
  bool allAccepted = true;
  for (const auto& run : report.runs) allAccepted = allAccepted && run.level.accepted;
  ASSERT_TRUE(allAccepted);

  report.gridConvergence.push_back(cfd::validation::cavity::cavityGridConvergence(
      "cavity_re100_quick_20_40_80", "Re = 100, QUICK, grids 20/40/80", {q[0], q[1], q[2]}, 100.0,
      2.0));
  report.gridConvergence.push_back(cfd::validation::cavity::cavityGridConvergence(
      "cavity_re100_quick_40_80_160", "Re = 100, QUICK, grids 40/80/160", {q[1], q[2], q[3]}, 100.0,
      2.0));
  report.gridConvergence.push_back(cfd::validation::cavity::cavityGridConvergence(
      "cavity_re100_upwind_20_40_80", "Re = 100, first-order upwind, grids 20/40/80",
      {u[0], u[1], u[2]}, 100.0, 1.0));
  for (const char* quantity : {"u_centerline", "v_centerline"}) {
    report.orders.push_back(
        cfd::validation::computeRunSequenceOrder({q[0], q[1], q[2], q[3]}, quantity, NormKind::L2));
    cfd::validation::GridConvergenceOptions first;
    first.formalOrder = 1.0;
    report.orders.push_back(cfd::validation::computeRunSequenceOrder({u[0], u[1], u[2]}, quantity,
                                                                     NormKind::L2, first));
  }

  // Gates.
  // (1) The first-order sequence is far above the benchmark floor: its
  //     Ghia error must fall monotonically with refinement.
  // (2) At equal grid (80x80) the higher-order scheme has the smaller
  //     discretisation error: at every station QUICK 80 is closer than
  //     upwind 80 to the grid-converged value (QUICK 160x160, whose station
  //     uncertainty is gate 3). Benchmark-independent on purpose -- the
  //     first version of this gate compared distances to Ghia and failed
  //     for v (QUICK 160 4.37e-3 vs upwind 80 3.57e-3) because QUICK sits
  //     at the Ghia floor; that result is recorded in results/p12-num-007.
  // (3) Solution convergence of the QUICK sequence on (40,80,160): every
  //     station's fine-grid numerical uncertainty is below 1 % (the
  //     NUM-005 engineering target) -- GCI21 for a monotonic station, the
  //     NUM-005 oscillatory uncertainty 0.5 (max - min) / |phi_fine| (Stern
  //     et al. 2001) for an oscillatory one (GCI is undefined there). The
  //     remaining distance to Ghia is then not our discretisation error.
  for (const char* quantity : {"u_centerline", "v_centerline"}) {
    const Real e20 = error(*u[0], quantity).l2, e40 = error(*u[1], quantity).l2,
               e80 = error(*u[2], quantity).l2;
    report.gates.push_back({std::string("upwind_monotone_") + quantity, e40 < e20 && e80 < e40,
                            "upwind L2 " + std::to_string(e20) + " -> " + std::to_string(e40) +
                                " -> " + std::to_string(e80)});
  }
  {
    const auto value = [](const ValidationRun& run, const std::string& name) {
      for (const auto& [key, v] : run.level.diagnostics) {
        if (key == name) return v;
      }
      throw std::runtime_error("missing diagnostic " + name);
    };
    bool smaller = true;
    std::string detail;
    for (const auto& station : cfd::validation::cavity::convergenceStations(100.0)) {
      const Real converged = value(*q[3], station.name);
      const Real quickError = std::abs(value(*q[2], station.name) - converged);
      const Real upwindError = std::abs(value(*u[2], station.name) - converged);
      smaller = smaller && quickError < upwindError;
      detail += station.name + " |QUICK80 - QUICK160| " + std::to_string(quickError) +
                " vs |upwind80 - QUICK160| " + std::to_string(upwindError) + "; ";
    }
    report.gates.push_back({"higher_order_smaller_discretisation_error_80x80", smaller, detail});
  }
  {
    const auto& study = report.gridConvergence[1];
    bool within = study.allSolvesAccepted;
    std::string detail;
    for (const auto& quantity : study.quantities) {
      const auto& a = quantity.analysis;
      const bool oscillatory = a.status == cfd::validation::GridConvergenceStatus::Oscillatory;
      const auto& uncertainty = oscillatory ? a.relativeOscillationUncertainty : a.gci21;
      within = within && uncertainty.has_value() && *uncertainty < 0.01;
      detail += quantity.spec.name + (oscillatory ? " oscillatory U " : " GCI21 ") +
                (uncertainty.has_value() ? std::to_string(*uncertainty) : std::string("n/a")) +
                "; ";
    }
    report.gates.push_back({"quick_solution_converged_40_80_160", within, detail});
  }
  report.limitations = {
      "Ghia et al. is itself a 129x129 numerical solution tabulated to 5 digits; beyond 40x40 the "
      "higher-order error vs Ghia plateaus at ~2e-3 (u) / ~4.4e-3 (v): a benchmark/comparison "
      "floor, not solution error (tightening the outer tolerance 1e-6 -> 1e-8 changes it by < "
      "1e-5; 160x160 gives the same value).",
      "Collocated SIMPLE without Rhie-Chow interpolation (no pressure-checkerboard damping); the "
      "closed cavity's velocity is unaffected but the pressure field is not validated here.",
      "Runtimes are wall-clock of the build that ran the study (see summary) and are not "
      "deterministic."};
  writeReport(report, "cavity_re100");
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}

// cavity_re1000.json: QUICK 40/80/160; NUM-005 grid convergence of the
// Ghia-station values on (40,80,160); orders of the Ghia errors.
TEST(CavityGhiaValidation, DISABLED_Re1000Study) {
  using cfd::validation::NormKind;
  ValidationReport report;
  report.name = "cavity_re1000";
  report.description = "Lid-driven cavity Re = 1000: QUICK (configuration of record) 40/80/160";
  describeCavity(report, 1000.0);
  report.runs.push_back(validateGrid({1000.0, 40, ConvectionScheme::QUICK}, 0.07, 0.07));
  report.runs.push_back(validateGrid({1000.0, 80, ConvectionScheme::QUICK}, 0.0125, 0.01));
  report.runs.push_back(validateGrid({1000.0, 160, ConvectionScheme::QUICK}, 0.0035, 0.007));
  for (const auto& run : report.runs) expectRunPassed(run);
  for (const auto& run : report.runs) ASSERT_TRUE(run.level.accepted) << run.id();
  const ValidationRun* q[] = {&report.runs[0], &report.runs[1], &report.runs[2]};
  report.gridConvergence.push_back(cfd::validation::cavity::cavityGridConvergence(
      "cavity_re1000_quick_40_80_160", "Re = 1000, QUICK, grids 40/80/160", {q[0], q[1], q[2]},
      1000.0, 2.0));
  for (const char* quantity : {"u_centerline", "v_centerline"}) {
    report.orders.push_back(
        cfd::validation::computeRunSequenceOrder({q[0], q[1], q[2]}, quantity, NormKind::L2));
    const Real e40 = error(*q[0], quantity).l2, e80 = error(*q[1], quantity).l2,
               e160 = error(*q[2], quantity).l2;
    // At Re = 1000 the thin lid/wall layers keep every grid here far above
    // the benchmark floor: the Ghia error must fall with each refinement.
    report.gates.push_back({std::string("monotone_") + quantity, e80 < e40 && e160 < e80,
                            "QUICK L2 " + std::to_string(e40) + " -> " + std::to_string(e80) +
                                " -> " + std::to_string(e160)});
  }
  report.limitations = {
      "Ghia et al. is itself a 129x129 numerical solution; at Re = 1000 its own discretisation "
      "error is larger than at Re = 100, so agreement is judged as convergence toward the table, "
      "not as an exact target.",
      "Collocated SIMPLE without Rhie-Chow interpolation; the pressure field is not validated.",
      "Runtimes are wall-clock and not deterministic."};
  writeReport(report, "cavity_re1000");
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}
