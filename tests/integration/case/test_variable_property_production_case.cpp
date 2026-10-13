// P15 -- a temperature-dependent property, end to end through the production
// path: physics.json -> CaseReader -> CaseBuilder -> ProjectRunner -> solve ->
// ResultExporter.
//
// WHY THIS TEST EXISTS
// --------------------
// The unit tests prove physics.json PARSES a property model. That is not the
// same as the model reaching the solver, and the difference is exactly the
// failure mode P15 forbids: a case file that names a varying conductivity and
// is then solved with a constant one would pass every parse test while
// silently producing the wrong physics. Before the wiring this test was
// written for, that is precisely what happened -- CaseBuilder read the
// evaluated scalar and dropped the model.
//
// So this file asserts three separate things, and the second is the one that
// actually detects that failure:
//
//   1. the run converges and exports (reachability);
//   2. the field matches the CLOSED-FORM solution for k(T), and does NOT
//      match the constant-k solution (the model is genuinely in the solve);
//   3. a case using the plain-number form is unaffected.
//
// THE CLOSED FORM
// ---------------
// cases/heated_species_diffusion is quiescent -- every patch is a wall -- so
// the temperature field is one-dimensional steady conduction with no
// convection and no source:
//
//   d/dx ( k(T) dT/dx ) = 0
//
// Integrating once, k dT/dx is constant, so the KIRCHHOFF transform
// F(T) = integral of k dT is linear in x:
//
//   F(T(x)) = F(T_left) + [F(T_right) - F(T_left)] x/L
//
// For k(T) = a + bT this is F(T) = aT + bT^2/2, and inverting the quadratic
// gives T(x) in closed form. The discriminant is an exact square,
//
//   a^2 + 2b F(T) = (a + bT)^2
//
// so the inversion below is exact rather than a fitted approximation.
//
// Note this is NOT the linear profile the constant-k version of this same case
// satisfies (test_heated_species_diffusion_production_case.cpp asserts that
// one). Using coefficients whose value at the initial temperature equals the
// constant case's 0.6 makes the two runs agree at T0 and disagree everywhere
// else -- which is what turns "the model reached the solver" into something a
// test can see.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCaseDirectory = "cases/heated_species_diffusion";

constexpr Real kLength = 1.0;
constexpr Real kTLeft = 310.0;
constexpr Real kTRight = 290.0;

// k(T) = a + bT, chosen so that k(300) = 0.6 -- exactly the constant the
// unmodified case uses. k ranges from 0.3 at the cold wall to 0.9 at the hot
// one, a 3x variation across the domain, so the resulting profile departs
// from the linear one by a couple of kelvin: far above solver noise, and far
// below anything that would destabilise the solve.
constexpr Real kA = -8.4;
constexpr Real kB = 0.03;
constexpr Real kMinT = 282.0;  // k(282) = 0.06 > 0
constexpr Real kMaxT = 500.0;

// F(T) = integral of k dT = aT + bT^2/2.
Real kirchhoff(Real temperature) { return kA * temperature + 0.5 * kB * temperature * temperature; }

// The exact T(x) for k(T) = a + bT with the two Dirichlet walls above.
Real analyticalTemperature(Real x) {
  const Real target = kirchhoff(kTLeft) + (kirchhoff(kTRight) - kirchhoff(kTLeft)) * (x / kLength);
  // Positive root: a + bT > 0 over the validity range, so the discriminant is
  // the exact square (a + bT)^2 and this inversion is exact.
  return (-kA + std::sqrt(kA * kA + 2.0 * kB * target)) / kB;
}

// What the SAME case produces with the constant conductivity -- the linear
// profile. Used as the thing the variable-property result must NOT match.
Real constantPropertyTemperature(Real x) { return kTLeft + (kTRight - kTLeft) * (x / kLength); }

// Rewrites physics.json's thermal.conductivity as a polynomial model,
// leaving every other field of the case exactly as the committed one.
void useConductivityModel(const CaseFixtureCopy& fixture) {
  const std::filesystem::path physicsPath = fixture.path() / "physics.json";
  nlohmann::json physics;
  {
    std::ifstream in(physicsPath);
    ASSERT_TRUE(in.good()) << "could not read " << physicsPath;
    in >> physics;
  }
  physics["thermal"]["conductivity"] = {{"model", "polynomial"},
                                        {"coefficients", {kA, kB}},
                                        {"min_temperature", kMinT},
                                        {"max_temperature", kMaxT}};
  std::ofstream out(physicsPath);
  ASSERT_TRUE(out.good()) << "could not write " << physicsPath;
  out << physics.dump(2);
}

// Rewrites mesh.json's nx, for the refinement study.
void useCellCount(const CaseFixtureCopy& fixture, int nx) {
  const std::filesystem::path meshPath = fixture.path() / "mesh.json";
  nlohmann::json mesh;
  {
    std::ifstream in(meshPath);
    ASSERT_TRUE(in.good()) << "could not read " << meshPath;
    in >> mesh;
  }
  mesh["nx"] = nx;
  std::ofstream out(meshPath);
  ASSERT_TRUE(out.good()) << "could not write " << meshPath;
  out << mesh.dump(2);
}

// Max |T - T_exact| over the cells of one run.
Real closedFormError(const ProjectRunResult& run) {
  Real worst = 0.0;
  for (const auto& cell : run.mesh->cells()) {
    worst = std::max(worst, std::abs(run.thermalResult->temperature[cell.id()] -
                                     analyticalTemperature(cell.centroid().x)));
  }
  return worst;
}

}  // namespace

TEST(VariablePropertyProductionCase, RunsEndToEndAndExports) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  useConductivityModel(fixture);

  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << "did not converge: " << run.errorMessage;
  ASSERT_TRUE(run.thermalResult.has_value());
  EXPECT_TRUE(run.thermalResult->converged());

  // Export is part of "reachable from a case file": a capability that solves
  // but cannot be written out is not usable.
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  EXPECT_TRUE(std::filesystem::exists(*run.exportSummary->fieldsCsvPath));
  EXPECT_TRUE(std::filesystem::exists(run.exportSummary->metadataPath));
}

// THE DETECTOR. If CaseBuilder or ProjectRunner dropped the model, the solve
// would run with k = 0.6 everywhere and produce the LINEAR profile -- which
// this test would catch twice over: the closed-form match would fail, and the
// "differs from constant-k" assertion would fail.
TEST(VariablePropertyProductionCase, MatchesTheClosedFormForVariableConductivity) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  useConductivityModel(fixture);

  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.thermalResult.has_value());
  ASSERT_TRUE(run.mesh.has_value());

  const Mesh& mesh = *run.mesh;
  const ScalarField& temperature = run.thermalResult->temperature;

  Real errorAgainstClosedForm = 0.0;
  Real departureFromConstantK = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    errorAgainstClosedForm = std::max(errorAgainstClosedForm,
                                      std::abs(temperature[cell.id()] - analyticalTemperature(x)));
    departureFromConstantK = std::max(
        departureFromConstantK, std::abs(temperature[cell.id()] - constantPropertyTemperature(x)));
  }

  // DERIVED, not chosen. Not the 1e-6 the constant-k case uses: that case's
  // solution is affine and the FVM reproduces an affine field exactly, while
  // this one is genuinely nonlinear and carries a real truncation error.
  //
  // The first version of this test guessed 2e-2 and failed at 5.266e-2. The
  // guess was replaced by a measurement rather than by a larger guess:
  // ErrorConvergesAtSecondOrderUnderRefinement below establishes that the
  // error sequence is second order (5.266e-2, 1.548e-2, 4.240e-3, 1.113e-3
  // for nx = 20, 40, 80, 160), so 5.266e-2 at nx = 20 is what a correct
  // second-order discretisation produces here. The bound is that measured
  // value plus ~14% for platform floating-point variation.
  //
  // It stays a real detector at that size: a constant-k solve sits on the
  // linear profile, which departs from the closed form by ~2.4 K at
  // mid-domain -- forty times this bound.
  EXPECT_LT(errorAgainstClosedForm, 6e-2) << "max error vs closed form " << errorAgainstClosedForm;

  // The non-vacuity half. A constant-k run would sit exactly on the linear
  // profile, giving 0 here; the closed form departs from it by ~2.4 K at
  // mid-domain.
  EXPECT_GT(departureFromConstantK, 1.0)
      << "variable-property result is indistinguishable from the constant-k one ("
      << departureFromConstantK << " K) -- the model is not reaching the solver";
}

// The committed case is untouched by P15: plain numbers still mean constants,
// and still produce the linear profile this case has always produced.
TEST(VariablePropertyProductionCase, ConstantFormIsUnaffected) {
  const CaseFixtureCopy fixture(kCaseDirectory);  // no patch applied
  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.thermalResult.has_value());
  ASSERT_TRUE(run.mesh.has_value());

  Real error = 0.0;
  for (const auto& cell : run.mesh->cells()) {
    error = std::max(error, std::abs(run.thermalResult->temperature[cell.id()] -
                                     constantPropertyTemperature(cell.centroid().x)));
  }
  EXPECT_LT(error, 1e-6) << "max error " << error;
}

// A model whose validity range does not cover the temperatures the case
// actually reaches must FAIL the run, not extrapolate. The range is narrowed
// so the hot wall at 310 K sits outside it.
TEST(VariablePropertyProductionCase, OutOfRangeExcursionIsReportedNotExtrapolated) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const std::filesystem::path physicsPath = fixture.path() / "physics.json";
  nlohmann::json physics;
  {
    std::ifstream in(physicsPath);
    ASSERT_TRUE(in.good());
    in >> physics;
  }
  // Brackets initial_temperature (300), so the case PARSES -- the excursion
  // only happens once the solve approaches the 310 K wall. That is the point:
  // this exercises the runtime channel, not the parse-time one.
  physics["thermal"]["conductivity"] = {{"model", "polynomial"},
                                        {"coefficients", {kA, kB}},
                                        {"min_temperature", 295.0},
                                        {"max_temperature", 305.0}};
  {
    std::ofstream out(physicsPath);
    ASSERT_TRUE(out.good());
    out << physics.dump(2);
  }

  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  // Whatever the run reports, it must not report a converged temperature
  // field: the solve could not evaluate its own conductivity.
  if (run.thermalResult.has_value()) {
    EXPECT_FALSE(run.thermalResult->converged())
        << "a solve that left its property's validity range reported convergence";
  }
}

// MESH CONVERGENCE -- and the derivation of the absolute bound above.
//
// The 0.02 K first guessed for the nx = 20 bound was not derived from
// anything; the measured error was 0.053 K. Rather than move a number until
// it passes -- which would make the bound meaningless -- the question is
// whether 0.053 K is the DISCRETISATION error of a second-order scheme or a
// defect. That is answerable: refine and measure the observed order
//
//   p = ln(E_h / E_{h/2}) / ln 2
//
// A second-order scheme gives p -> 2. A dropped model, a first-order
// boundary treatment, or a wrong face interpolation would all show up here as
// p near 1 or as an error that stops falling. The absolute bound above is
// then whatever the h = L/20 level of that established sequence actually is,
// which is a derived quantity rather than a preference.
TEST(VariablePropertyProductionCase, ErrorConvergesAtSecondOrderUnderRefinement) {
  const int cellCounts[] = {20, 40, 80, 160};
  Real errors[4] = {0.0, 0.0, 0.0, 0.0};
  Real orders[3] = {0.0, 0.0, 0.0};

  for (int i = 0; i < 4; ++i) {
    const CaseFixtureCopy fixture(kCaseDirectory);
    useConductivityModel(fixture);
    useCellCount(fixture, cellCounts[i]);
    const ProjectRunResult run = ProjectRunner::run(fixture.path());
    ASSERT_EQ(run.status, ProjectRunStatus::Converged) << "nx = " << cellCounts[i];
    ASSERT_TRUE(run.thermalResult.has_value());
    ASSERT_TRUE(run.mesh.has_value());
    errors[i] = closedFormError(run);
    std::printf("nx = %3d   error = %.6e\n", cellCounts[i], errors[i]);
  }

  for (int i = 1; i < 4; ++i) {
    const Real order = std::log(errors[i - 1] / errors[i]) / std::log(2.0);
    std::printf("nx %3d -> %3d   observed order p = %.4f\n", cellCounts[i - 1], cellCounts[i],
                order);
    orders[i - 1] = order;
  }

  // The criterion is the ASYMPTOTIC order, and the coarse meshes are not in
  // the asymptotic range -- the measured sequence is 1.766, 1.868, 1.930,
  // approaching 2 from below exactly as a second-order scheme does when h is
  // still large. Demanding p > 1.8 on the 20 -> 40 pair, as the first version
  // of this test did, demands asymptotic behaviour where there is none.
  //
  // So the assertions below use the whole sequence, which is a SHARPER
  // detector than a single threshold, not a looser one:
  //
  //   * every refinement reduces the error (a diverging or stalling scheme
  //     fails here);
  //   * every order is comfortably above 1 (a first-order boundary treatment
  //     or a dropped model would land near 1, or near 0 if the field stopped
  //     depending on h);
  //   * the orders increase monotonically toward 2 (this is what
  //     distinguishes genuine second-order convergence from a number that
  //     merely happens to exceed a threshold once);
  //   * the finest pair is within 0.1 of 2.
  for (int i = 1; i < 4; ++i) {
    EXPECT_LT(errors[i], errors[i - 1])
        << "error did not fall from nx = " << cellCounts[i - 1] << " to " << cellCounts[i];
  }
  for (int i = 0; i < 3; ++i) {
    EXPECT_GT(orders[i], 1.5) << "order " << orders[i] << " at step " << i
                              << " is not second-order behaviour";
    EXPECT_LT(orders[i], 2.2) << "order " << orders[i]
                              << " exceeds second order -- suspect the "
                                 "error metric rather than celebrating it";
  }
  EXPECT_GT(orders[1], orders[0]) << "orders are not approaching 2 monotonically";
  EXPECT_GT(orders[2], orders[1]) << "orders are not approaching 2 monotonically";
  EXPECT_GT(orders[2], 1.9) << "finest-pair order " << orders[2] << " is not asymptotically 2";
}
