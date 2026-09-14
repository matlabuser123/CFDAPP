// P12-NUM-007 -- convection-scheme accuracy / cost comparison on a
// production case: the lid-driven cavity against Ghia et al. (1982).
//
// Every scheme (upwind, central, linear_upwind, QUICK) runs the SAME mesh,
// physics, SIMPLE settings, tolerances, linear solvers and initial
// condition (cavitySettings in CavityProductionCase.cpp -- only
// SIMPLESettings::convectionScheme differs), one after the other in one
// process so wall-clock times are comparable. Recorded per run: Ghia
// errors (L1/L2/Linf), outer iterations, momentum / pressure linear
// iterations, runtime, solver status, linear-solver fallbacks, global mass
// imbalance, boundedness (max |U| vs the lid speed). A scheme that fails to
// converge is reported as such (its run is rejected), never dropped.
//
// No universal "best scheme" is declared: the reports rank nothing. What is
// asserted is only what the numerics guarantee: on a grid where the
// discretisation error dominates (20x20 at Re = 100; every grid at
// Re = 1000 -- see the monotone Re = 1000 study) the second-order-type
// schemes are closer to Ghia than first-order upwind; the bounded schemes
// (upwind; QUICK with its TVD limiter) never exceed the lid speed. On finer
// Re = 100 grids the higher-order schemes reach the Ghia comparison floor
// (test_cavity_production_validation.cpp header) and v can then be further
// from Ghia than upwind's (measured at 80x80) -- reported, not hidden.
//
// Default suite: the four single-scheme tests and the 20x20 comparison.
// DISABLED_AccuracyCostComparisonFull (Re = 100 20/40/80 + Re = 1000 40/80,
// all schemes, ~45 min Release) writes scheme_comparison.json.
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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

constexpr ConvectionScheme kSchemes[] = {ConvectionScheme::Upwind, ConvectionScheme::Central,
                                         ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK};

bool isBounded(ConvectionScheme scheme) {
  return scheme == ConvectionScheme::Upwind || scheme == ConvectionScheme::QUICK;
}

// Regression bounds on the 20x20 Re = 100 Ghia L2 errors: measured value
// + ~30 % (measured u / v: upwind 2.105e-2 / 1.446e-2, central 6.26e-3 /
// 5.63e-3, linear_upwind 5.55e-3 / 4.50e-3, QUICK 5.69e-3 / 4.82e-3).
struct Bounds {
  Real u;
  Real v;
};
Bounds bounds20(ConvectionScheme scheme) {
  switch (scheme) {
    case ConvectionScheme::Upwind:
      return {0.0275, 0.019};
    case ConvectionScheme::Central:
      return {0.0082, 0.0074};
    case ConvectionScheme::LinearUpwind:
      return {0.0073, 0.0059};
    case ConvectionScheme::QUICK:
      return {0.0075, 0.0065};
  }
  return {0.0, 0.0};
}

// A scheme's run with the checks every scheme must pass (accepted, no wall
// leakage) plus boundedness as a check for the bounded schemes; for
// central / linear_upwind boundedness is recorded (max_velocity_magnitude)
// but is not a property the scheme guarantees.
ValidationRun runScheme(Real reynolds, Index n, ConvectionScheme scheme) {
  ValidationRun run = cfd::validation::cavity::runCavity({reynolds, n, scheme});
  if (isBounded(scheme)) cfd::validation::cavity::addBoundednessCheck(run);
  std::printf("%s: %s, %llu it, %.1f s\n", run.id().c_str(), run.level.solverStatus.c_str(),
              static_cast<unsigned long long>(run.level.iterations), run.level.runtimeSeconds);
  return run;
}

const cfd::validation::ErrorNorms* error(const ValidationRun& run, const char* name) {
  for (const auto& [key, norms] : run.level.errors) {
    if (key == name) return &norms;
  }
  return nullptr;
}

void describe(ValidationReport& report) {
  report.reference =
      "Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Tables I/II (Re = 100 and 1000 "
      "columns; validation/ghia/)";
  report.coefficients = {{"lid_velocity", 1.0}, {"density", 1.0}, {"cavity_size", 1.0}};
  report.configuration = {
      {"identical_for_all_schemes",
       "mesh, physics, SIMPLE settings (alpha 0.7/0.3, outer tolerances 1e-6), linear solvers and "
       "tolerances, P12-NUM-004 fallback, initial condition u = 0, p = 0"},
      {"varied", "SIMPLESettings::convectionScheme only"},
      {"schemes",
       "upwind (first order); central (second order, unbounded); linear_upwind (second-order "
       "upwind-biased, unbounded); quick (QUICK via deferred correction with a TVD limiter)"},
      {"cost_metrics",
       "outer iterations; momentum_linear_iterations / pressure_linear_iterations (SIMPLEResult "
       "P12-NUM-007 counters, every inner BiCGSTAB iteration incl. fallback attempts); wall-clock "
       "runtime (runs executed sequentially in one process)"},
      {"ranking", "none -- no universal best scheme is declared"}};
}

void writeReport(const ValidationReport& report, const std::string& stem) {
  cfd::validation::writeValidationReport(std::string(kOutputDirectory) + "/" + stem + ".json",
                                         report);
  std::ofstream md(std::string(kOutputDirectory) + "/" + stem + ".md", std::ios::binary);
  md << cfd::validation::validationReportMarkdown(report);
  std::printf("\n%s", cfd::validation::validationReportMarkdown(report).c_str());
}

// Adds, for the runs of one (Re, grid) group, the gate "higher-order schemes
// closer to Ghia than upwind" (u and v L2). Only called where the
// discretisation error dominates (see file header).
void addHigherOrderGate(ValidationReport& report, const std::vector<const ValidationRun*>& group,
                        const std::string& label) {
  const ValidationRun* upwind = group.front();
  for (const char* quantity : {"u_centerline", "v_centerline"}) {
    const auto* reference = error(*upwind, quantity);
    for (std::size_t k = 1; k < group.size(); ++k) {
      const auto* e = error(*group[k], quantity);
      const bool passed = reference != nullptr && e != nullptr && e->l2 < reference->l2;
      report.gates.push_back(
          {label + "_" + group[k]->scheme + "_below_upwind_" + quantity, passed,
           (e ? std::to_string(e->l2) : std::string("n/a")) + " < upwind " +
               (reference ? std::to_string(reference->l2) : std::string("n/a"))});
    }
  }
}

void singleScheme(ConvectionScheme scheme) {
  ValidationRun run = runScheme(100.0, 20, scheme);
  const Bounds b = bounds20(scheme);
  cfd::validation::cavity::addGhiaErrorCheck(run, b.u, b.v);
  for (const auto& check : run.checks) {
    EXPECT_TRUE(check.passed) << run.id() << " " << check.name << ": " << check.detail;
  }
  EXPECT_TRUE(run.passed());
}

}  // namespace

TEST(SchemeValidation, Upwind) { singleScheme(ConvectionScheme::Upwind); }
TEST(SchemeValidation, Central) { singleScheme(ConvectionScheme::Central); }
TEST(SchemeValidation, LinearUpwind) { singleScheme(ConvectionScheme::LinearUpwind); }
TEST(SchemeValidation, Quick) { singleScheme(ConvectionScheme::QUICK); }

// Default suite: the four schemes on 20x20, Re = 100, sequentially.
TEST(SchemeValidation, AccuracyCostComparison) {
  ValidationReport report;
  report.name = "scheme_comparison_20x20";
  report.description = "Convection-scheme accuracy / cost, lid-driven cavity Re = 100, 20x20";
  describe(report);
  for (const ConvectionScheme scheme : kSchemes)
    report.runs.push_back(runScheme(100.0, 20, scheme));
  std::vector<const ValidationRun*> group;
  for (const auto& run : report.runs) group.push_back(&run);
  addHigherOrderGate(report, group, "re100_20x20");
  report.limitations = {
      "Runtimes in the default suite are measured in the Debug build under a parallel ctest "
      "load -- indicative only; the fair comparison is scheme_comparison.json (Release, "
      "sequential)."};
  writeReport(report, "scheme_comparison_20x20");
  for (const auto& run : report.runs) {
    EXPECT_TRUE(run.passed()) << run.id();
  }
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}

// scheme_comparison.json: Re = 100 on 20/40/80 and Re = 1000 on 40/80, all
// four schemes, plus a NUM-005 grid-convergence study per scheme at
// Re = 100 (20/40/80; formal order 1 for upwind, 2 otherwise).
TEST(SchemeValidation, DISABLED_AccuracyCostComparisonFull) {
  ValidationReport report;
  report.name = "scheme_comparison";
  report.description =
      "Convection-scheme accuracy / cost, lid-driven cavity: Re = 100 on 20/40/80, Re = 1000 on "
      "40/80";
  describe(report);
  const struct {
    Real reynolds;
    Index n;
  } groups[] = {{100.0, 20}, {100.0, 40}, {100.0, 80}, {1000.0, 40}, {1000.0, 80}};
  for (const auto& g : groups) {
    for (const ConvectionScheme scheme : kSchemes) {
      report.runs.push_back(runScheme(g.reynolds, g.n, scheme));
    }
  }
  // runs[4 * group + scheme]
  const auto at = [&](std::size_t group, std::size_t scheme) {
    return &report.runs[4 * group + scheme];
  };
  for (std::size_t gi = 0; gi < 5; ++gi) {
    // Gate where the discretisation error dominates: Re = 100 20x20 and
    // every Re = 1000 grid (see file header).
    if (gi == 0 || gi >= 3) {
      addHigherOrderGate(report, {at(gi, 0), at(gi, 1), at(gi, 2), at(gi, 3)},
                         "re" + std::to_string(static_cast<int>(groups[gi].reynolds)) + "_" +
                             std::to_string(groups[gi].n) + "x" + std::to_string(groups[gi].n));
    }
  }
  bool allAccepted = true;
  for (const auto& run : report.runs) allAccepted = allAccepted && run.level.accepted;
  if (allAccepted) {
    for (std::size_t s = 0; s < 4; ++s) {
      report.gridConvergence.push_back(cfd::validation::cavity::cavityGridConvergence(
          "cavity_re100_" + at(0, s)->scheme + "_20_40_80",
          "Re = 100, " + at(0, s)->scheme + ", grids 20/40/80", {at(0, s), at(1, s), at(2, s)},
          100.0, s == 0 ? 1.0 : 2.0));
    }
  }
  report.limitations = {
      "At Re = 100 beyond 40x40 the second-order-type schemes reach the Ghia comparison floor "
      "(~2e-3 u, ~4.4e-3 v); there v can be further from Ghia than first-order upwind's error -- "
      "an artefact of comparing against a numerical benchmark, not a scheme defect.",
      "central and linear_upwind are unbounded schemes: boundedness is recorded "
      "(max_velocity_magnitude), not required.",
      "Runtimes: Release build, runs sequential in one process on an otherwise idle machine "
      "(see summary.md); wall-clock, not deterministic."};
  writeReport(report, "scheme_comparison");
  for (const auto& run : report.runs) {
    for (const auto& check : run.checks) {
      EXPECT_TRUE(check.passed) << run.id() << " " << check.name << ": " << check.detail;
    }
  }
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}
