// P12-NUM-007 -- Production validation: laminar 2D backward-facing step,
// Gartling (1990) configuration (BackwardFacingStepCase.hpp): ER = 2,
// domain [0, 15H] x [0, H] starting at the step, parabolic inflow of mean
// 1 over the upper half, zero-gradient outlet with p = 0, Re = U_mean H/nu.
//
// Benchmark (Re = 800): the published 2D numerical reattachment lengths of
// the lower-wall primary eddy, x_r/h (h = step height), compiled in Table 2
// of arXiv:2507.16509 (A Finite Volume and Levenberg-Marquardt Optimization
// Framework for Benchmarking MHD Flows over Backward-Facing Steps, 2025):
//   Rogers & Kwak 11.48, Barton 11.51, Erturk 11.834, Kim & Moin 11.90,
//   Lee & Mateescu 12.00, Guj & Stella 12.05, Grigoriev & Dargush 12.18,
//   Keskar & Lyn 12.19, Gartling 12.20, Gresho et al. 12.20
// -- range [11.48, 12.20]; Gartling's own value x_r = 6.10 H (= 12.20 h) is
// independently restated in arXiv:1906.05387 ("L_lower ~ 6.1"). Upper-wall
// bubble length (x_rs - x_s)/h in the same table: [10.60, 11.52].
// Low-Re check (Re = 100): x_r/h = 3.00, arXiv:2507.16509 Table 1 -- a
// SINGLE source with an upstream inlet section (x_step = 1.35 h), reported
// for context only, never gated.
//
// Reattachment is computed algorithmically: the downstream-most negative
// -> positive zero crossing of the bottom-wall shear, linearly
// interpolated between the wall-adjacent cell centres (no visual
// estimate); likewise the upper-wall separation / reattachment.
//
// Measured domain-length independence (Re = 800, 20 cells/H, QUICK): L =
// 15H and L = 30H give x_r/h 10.0852 / 10.0852 and upper-wall crossings
// 7.6699 / 19.8124 vs 7.6699 / 19.8122 -- the 15H outlet does not
// influence the separation topology (4 digits).
//
// Default suite: Geometry and ReattachmentDetection (no solve). Every solve
// is DISABLED_ (Re = 100, 10 cells/H: ~2 min Release, ~15-20 min Debug;
// Re = 800: 10-60 min Release per grid) and recorded in
// results/p12-num-007/.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include "BackwardFacingStepCase.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"
#include "cfd/validation/ProductionValidation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::validation::ValidationReport;
using cfd::validation::ValidationRun;
namespace step = cfd::validation::step;

namespace {

constexpr Real kPublishedMin = 11.48;  // Rogers & Kwak
constexpr Real kPublishedMax = 12.20;  // Gartling; Gresho et al.
constexpr Real kUpperBubbleMin = 10.60;
constexpr Real kUpperBubbleMax = 11.52;
constexpr Real kSingleSourceRe100 = 3.00;

std::optional<Real> diagnostic(const ValidationRun& run, const std::string& name) {
  for (const auto& [key, value] : run.level.diagnostics) {
    if (key == name) return value;
  }
  return std::nullopt;
}

void expectPassed(const ValidationRun& run) {
  EXPECT_TRUE(run.level.accepted) << run.id() << ": " << run.level.rejectionReason;
  for (const auto& check : run.checks) {
    EXPECT_TRUE(check.passed) << run.id() << " " << check.name << ": " << check.detail;
  }
}

void printRun(const ValidationRun& run) {
  std::printf("%s: %s, %llu it, %.1f s", run.id().c_str(), run.level.solverStatus.c_str(),
              static_cast<unsigned long long>(run.level.iterations), run.level.runtimeSeconds);
  for (const char* name :
       {"reattachment_length_over_h", "corner_eddy_end_over_h", "upper_separation_over_h",
        "upper_reattachment_over_h", "upper_bubble_length_over_h"}) {
    const auto value = diagnostic(run, name);
    if (value) std::printf(", %s %.4f", name, *value);
  }
  std::printf("\n");
}

void describe(ValidationReport& report) {
  report.reference =
      "Re = 800: published 2D numerical x_r/h in [11.48, 12.20] and (x_rs - x_s)/h in [10.60, "
      "11.52] (ten studies, compiled in arXiv:2507.16509 Table 2; Gartling (1990) Int. J. Numer. "
      "Meth. Fluids 11, 953-967: x_r/h = 12.20). Re = 100: x_r/h = 3.00 (arXiv:2507.16509 Table "
      "1, single source, context only)";
  report.coefficients = {{"channel_height", step::kChannelHeight},
                         {"step_height", step::kStepHeight},
                         {"expansion_ratio", step::kChannelHeight / step::kStepHeight},
                         {"mean_inlet_velocity", step::kMeanInletVelocity},
                         {"density", step::kDensity}};
  report.configuration = {
      {"geometry",
       "fluid domain [0, L] x [0, H] downstream of the step, uniform Cartesian cells; the left "
       "boundary split into a no-slip step face (y < h) and per-face inlet patches (y > h)"},
      {"inflow", "u = 24 (y - h)(H - y) (mean 1, max 1.5) as exact face averages; v = 0"},
      {"outflow", "zero-gradient velocity, p = 0"},
      {"algorithm",
       "SIMPLE, QUICK convection, alpha 0.7/0.3, outer tolerances 1e-6, BiCGSTAB (pressure "
       "Jacobi 1e-12/1e-6, momentum 1e-12/1e-8), P12-NUM-004 fallback"},
      {"reattachment",
       "downstream-most negative -> positive zero crossing of the bottom-wall shear mu u_P/(dy/2), "
       "linear interpolation between wall-adjacent cell centres"}};
}

void writeReport(const ValidationReport& report, const std::string& stem) {
  const std::string base = "results/validation/production/" + stem;
  cfd::validation::writeValidationReport(base + ".json", report);
  std::ofstream md(base + ".md", std::ios::binary);
  md << cfd::validation::validationReportMarkdown(report);
  std::printf("\n%s", cfd::validation::validationReportMarkdown(report).c_str());
}

}  // namespace

// Default suite: the re-patched mesh is the Gartling geometry.
TEST(BackwardFacingStep, Geometry) {
  const step::StepSpec spec{800.0, 20, 15.0, cfd::discretization::ConvectionScheme::QUICK};
  const auto mesh = step::makeStepMesh(spec);
  EXPECT_EQ(mesh.numberOfCells(), 300u * 20u);
  // Every boundary face in exactly one patch; no internal face in a patch.
  std::set<Index> seen;
  Index stepFaces = 0;
  Index inletFaces = 0;
  Real inflow = 0.0;
  Real maxInlet = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) {
      EXPECT_TRUE(mesh.face(f).isBoundary());
      EXPECT_TRUE(seen.insert(f).second) << "face " << f << " in two patches";
      const auto& face = mesh.face(f);
      if (patch.name() == "step") {
        ++stepFaces;
        EXPECT_LT(face.centroid().y, step::kStepHeight);
        EXPECT_NEAR(face.centroid().x, 0.0, 1e-12);
      } else if (patch.name().rfind("inlet_", 0) == 0) {
        ++inletFaces;
        EXPECT_GT(face.centroid().y, step::kStepHeight);
        EXPECT_NEAR(face.centroid().x, 0.0, 1e-12);
        EXPECT_EQ(patch.faceIds().size(), 1u);
        const Real half = 0.5 * face.area();
        const Real u =
            step::averagedInletVelocity(face.centroid().y - half, face.centroid().y + half);
        EXPECT_GT(u, 0.0);
        inflow += u * face.area();
        maxInlet = std::max(maxInlet, u);
      }
    }
  }
  Index boundaryFaces = 0;
  for (const auto& face : mesh.faces()) boundaryFaces += face.isBoundary() ? 1 : 0;
  EXPECT_EQ(seen.size(), boundaryFaces);
  EXPECT_EQ(stepFaces, 10u);
  EXPECT_EQ(inletFaces, 10u);
  // Exact face averages: the inflow rate is U_mean (H - h) = 0.5 on any grid,
  // and no face average exceeds the parabola's maximum 1.5.
  EXPECT_NEAR(inflow, 0.5, 1e-14);
  EXPECT_LT(maxInlet, 1.5);
  EXPECT_GT(maxInlet, 1.45);
  // Boundary conditions exist for every patch.
  EXPECT_NO_THROW({
    const auto b = step::makeStepBoundaries(mesh);
    (void)b;
  });
  EXPECT_THROW((void)step::makeStepMesh({800.0, 21, 15.0}), std::invalid_argument);
}

// Default suite: the algorithmic reattachment / separation detection on
// sampled shear distributions with known zeros.
TEST(BackwardFacingStep, ReattachmentDetection) {
  // Linear pieces: interpolation must be exact.
  const std::vector<Real> x = {0.5, 1.5, 2.5, 3.5, 4.5};
  const auto zeros = step::shearZeroCrossings(x, {-2.0, -1.0, 1.0, 3.0, 5.0});
  ASSERT_EQ(zeros.size(), 1u);
  EXPECT_DOUBLE_EQ(zeros[0].x, 2.0);
  EXPECT_TRUE(zeros[0].negativeToPositive);
  EXPECT_THROW((void)step::shearZeroCrossings({0.0}, {1.0}), std::invalid_argument);
  // Touching zero without a sign change is not a crossing.
  EXPECT_TRUE(step::shearZeroCrossings(x, {1.0, 0.0, 1.0, 2.0, 3.0}).empty());

  // A synthetic step flow in units of h = 0.5: corner eddy to x = 0.1
  // (x/h 0.2), primary eddy reattaching at x = 5 (x/h 10), upper bubble
  // x = 4 .. 9 (x/h 8 .. 18), smooth quadratics sampled on 0.05-spaced
  // "cell centres" -- linear interpolation of a quadratic's simple root on
  // this spacing is accurate to < 2e-3 in x/h.
  step::WallShear shear;
  for (Index k = 0; k < 300; ++k) {
    const Real xc = 0.025 + 0.05 * static_cast<Real>(k);
    shear.x.push_back(xc);
    shear.bottom.push_back((xc - 0.1) * (xc - 5.0) / 10.0);
    shear.top.push_back((xc - 4.0) * (9.0 - xc));
  }
  const auto topology = step::separationTopology(shear);
  EXPECT_EQ(topology.bottomCrossings, 2u);
  EXPECT_EQ(topology.topCrossings, 2u);
  ASSERT_TRUE(topology.reattachment.has_value());
  EXPECT_NEAR(*topology.reattachment, 10.0, 2e-3);
  ASSERT_TRUE(topology.cornerEddyEnd.has_value());
  EXPECT_NEAR(*topology.cornerEddyEnd, 0.2, 2e-3);
  ASSERT_TRUE(topology.upperSeparation.has_value() && topology.upperReattachment.has_value());
  EXPECT_NEAR(*topology.upperSeparation, 8.0, 2e-3);
  EXPECT_NEAR(*topology.upperReattachment, 18.0, 2e-3);
  // No eddy at all: no reattachment is reported (never a fabricated value).
  step::WallShear attached;
  attached.x = x;
  attached.bottom = {1.0, 1.0, 1.0, 1.0, 1.0};
  attached.top = {-1.0, -1.0, -1.0, -1.0, -1.0};
  EXPECT_FALSE(step::separationTopology(attached).reattachment.has_value());
}

// Re = 100, 10 cells/H, L = 10H: converges from rest.
TEST(BackwardFacingStep, DISABLED_Converges) {
  const auto run = step::runStep({100.0, 10, 10.0});
  printRun(run);
  expectPassed(run);
}

// Re = 100, 10 cells/H: inflow, outflow and four sections carry 0.5.
TEST(BackwardFacingStep, DISABLED_MassConservation) {
  const auto run = step::runStep({100.0, 10, 10.0});
  expectPassed(run);
  ASSERT_TRUE(run.level.accepted);
  EXPECT_LE(*diagnostic(run, "max_flow_error"), 1e-6);
  EXPECT_LT(*run.level.massImbalance, 1e-6);
}

// backward_facing_step.json: Re = 800 on 20 / 30 / 40 cells per H (L =
// 15H, QUICK), NUM-005 grid convergence (observed order, Richardson
// extrapolation, GCI) of x_r/h and of the upper bubble, compared with the
// published range; plus Re = 100 on 20 cells per H against the single
// Table 1 value (reported only).
TEST(BackwardFacingStep, DISABLED_ReattachmentLength) {
  using cfd::validation::GridConvergenceStatus;
  ValidationReport report;
  report.name = "backward_facing_step";
  report.description =
      "Laminar backward-facing step (Gartling 1990, ER = 2): Re = 800 on 20/30/40 cells per H, "
      "L = 15H, QUICK; Re = 100 on 20 cells per H";
  describe(report);
  for (const Index perH : std::initializer_list<Index>{20, 30, 40}) {
    report.runs.push_back(step::runStep({800.0, perH, 15.0}));
    printRun(report.runs.back());
  }
  report.runs.push_back(step::runStep({100.0, 20, 15.0}));
  printRun(report.runs.back());
  for (const auto& run : report.runs) expectPassed(run);
  for (const auto& run : report.runs) ASSERT_TRUE(run.level.accepted) << run.id();

  cfd::validation::QuantitySpec reattachment;
  reattachment.name = "reattachment_length_over_h";
  reattachment.description = "x_r/h, lower-wall primary eddy (published range [11.48, 12.20])";
  reattachment.reference = 0.5 * (kPublishedMin + kPublishedMax);
  reattachment.referenceKind = "benchmark";
  reattachment.options.formalOrder = 2.0;
  reattachment.options.absoluteNoise = 1e-4;
  reattachment.options.gridIndependenceThreshold = 0.01;
  cfd::validation::QuantitySpec bubble = reattachment;
  bubble.name = "upper_bubble_length_over_h";
  bubble.description = "(x_rs - x_s)/h, upper-wall bubble (published range [10.60, 11.52])";
  bubble.reference = 0.5 * (kUpperBubbleMin + kUpperBubbleMax);
  std::vector<cfd::validation::GridStudyEntry> entries;
  for (std::size_t k = 0; k < 3; ++k) {
    entries.push_back(cfd::validation::toGridStudyEntry(
        report.runs[k], 15.0, 1.0, {"reattachment_length_over_h", "upper_bubble_length_over_h"}));
  }
  report.gridConvergence.push_back(cfd::validation::analyzeGridStudy(
      "backward_facing_step_re800", "Re = 800, QUICK, 20/30/40 cells per H", std::move(entries),
      {reattachment, bubble}));

  // Gates: the published range is the benchmark (the reference midpoint is
  // only the report's anchor). The Richardson-extrapolated (grid-converged)
  // x_r/h must lie in it; if the sequence is not in its asymptotic range,
  // the finest grid's value is judged instead and the report says so.
  const auto& study = report.gridConvergence[0];
  for (const auto& q : study.quantities) {
    const bool isReattachment = q.spec.name == reattachment.name;
    const Real lo = isReattachment ? kPublishedMin : kUpperBubbleMin;
    const Real hi = isReattachment ? kPublishedMax : kUpperBubbleMax;
    const auto& a = q.analysis;
    const bool useExtrapolated =
        a.extrapolated21.has_value() && a.status == GridConvergenceStatus::Asymptotic;
    const std::optional<Real> value = useExtrapolated ? a.extrapolated21 : q.values[2];
    const bool inRange = value.has_value() && *value >= lo && *value <= hi;
    char detail[256];
    std::snprintf(detail, sizeof(detail), "%s %.4f vs published [%.2f, %.2f] (status %s)",
                  useExtrapolated ? "Richardson-extrapolated" : "finest-grid",
                  value.value_or(std::nan("")), lo, hi,
                  std::string(cfd::validation::gridConvergenceStatusName(a.status)).c_str());
    report.gates.push_back({q.spec.name + "_within_published_range", inRange, detail});
  }
  const auto re100 = diagnostic(report.runs[3], "reattachment_length_over_h");
  char note[200];
  std::snprintf(note, sizeof(note),
                "Re = 100 (20 cells/H): x_r/h = %.4f vs %.2f in arXiv:2507.16509 Table 1 (single "
                "source with an upstream inlet section; reported, not gated)",
                re100.value_or(std::nan("")), kSingleSourceRe100);
  report.limitations = {
      note,
      "Uniform Cartesian grids only (no clustering at the step corner or walls); the finest grid "
      "here has 40 cells per H.",
      "Collocated SIMPLE without Rhie-Chow interpolation; the pressure field is not validated.",
      "Reattachment from the wall-adjacent cell velocity (first-order one-sided wall shear): the "
      "zero crossing itself is independent of that approximation's magnitude error.",
      "Runtimes are wall-clock and not deterministic."};
  writeReport(report, "backward_facing_step");
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}
