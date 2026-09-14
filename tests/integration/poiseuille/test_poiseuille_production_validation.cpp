// P12-NUM-007 -- Production validation: planar Poiseuille flow (same case
// as test_poiseuille_validation.cpp: L = 8H, H = 1, rho = 1, mu = 0.1,
// uniform inlet U = 1, Re = 10, zero-gradient outlet with p = 0).
//
// Two references, kept apart:
//   * the continuous solution u = 6U (y/H)(1 - y/H), dp/dx = -12 mu U / H^2
//     -- the physics;
//   * the EXACT fully developed solution of this code's discretisation
//     (discretePoiseuilleVelocity / discretePressureGradient in
//     PoiseuilleValidationUtils.hpp): dp/dx scaled by ny^2/(ny^2 + 2), i.e.
//     a relative discretisation error of exactly 2/(ny^2 + 2) -- second
//     order, as the scheme's formal order predicts. Agreement with this one
//     shows every remaining difference from the physics is the predicted
//     discretisation error (not entrance, outlet or iterative error).
//
// Pressure: the checkerboard-immune pair-averaged gradient (the collocated
// SIMPLE has no Rhie-Chow interpolation, and an odd-even pressure mode
// grows on this case's residual plateau -- its amplitude is reported per
// run; see PoiseuilleValidationUtils.hpp). The legacy two-column estimate
// is reported alongside for comparison only.
//
// Settings: those of the P12-NUM-005 PoiseuilleValidation.GridConvergence
// study (outer u/p gates 2e-5 / 5e-4 -- this open channel's documented
// residual plateau --, continuity 1e-6, Jacobi-preconditioned pressure
// BiCGSTAB 1e-10/1e-8, P12-NUM-004 fallback). All four tests are in the
// default suite (64x8 each; the grid study 64x8/96x12/144x18, ~3 min Debug).
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "PoiseuilleValidationUtils.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/ProductionValidation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::validation::ValidationReport;
using cfd::validation::ValidationRun;

namespace {

constexpr Real kHeight = 1.0;
constexpr Real kLength = 8.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kReynolds = kDensity * kMeanVelocity * kHeight / kViscosity;  // 10
constexpr Real kProfileStation = 0.75 * kLength;
constexpr Real kPressureStation1 = 0.40 * kLength;
constexpr Real kPressureStation2 = 0.75 * kLength;
constexpr Real kFlowRate = kDensity * kMeanVelocity * kHeight;  // per unit depth
const char* kOutputDirectory = "results/validation/production";

cfd::pressure_velocity::SIMPLESettings settings() {
  cfd::pressure_velocity::SIMPLESettings s;
  s.maxIterations = 8000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5;
  s.pressureTolerance = 5e-4;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  s.robustness.linearSolverFallback.enabled = true;
  return s;
}

std::optional<Real> diagnostic(const ValidationRun& run, const std::string& name) {
  for (const auto& [key, value] : run.level.diagnostics) {
    if (key == name) return value;
  }
  return std::nullopt;
}

Real required(const ValidationRun& run, const std::string& name) {
  const auto value = diagnostic(run, name);
  if (!value.has_value()) throw std::runtime_error("missing diagnostic " + name);
  return *value;
}

std::string fmt(const char* pattern, Real a, Real b = 0.0) {
  char buffer[200];
  std::snprintf(buffer, sizeof(buffer), pattern, a, b);
  return buffer;
}

// Solves one grid and records every Poiseuille quantity; the checks that
// follow from conservation and from the discrete exact solution are added
// here, grid-independent.
ValidationRun runPoiseuille(Index nx, Index ny) {
  using cfd::boundary::BoundaryConditionSet;
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(nx, ny, kLength, kHeight);
  BoundaryConditionSet velocity;
  velocity.set(mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{kMeanVelocity, 0.0}));
  velocity.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  velocity.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());
  BoundaryConditionSet pressure;
  pressure.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  pressure.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  const cfd::pressure_velocity::SIMPLE simple(settings(), /*referenceCell=*/0);
  const auto start = std::chrono::steady_clock::now();
  const auto result =
      simple.solve(mesh, cfd::physics::FluidProperties(kDensity, kViscosity), velocity, pressure,
                   cfd::fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                   cfd::fields::ScalarField(mesh.numberOfCells(), 0.0));
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  ValidationRun run = cfd::validation::makeSimpleValidationRun(
      "poiseuille_re10", kReynolds, "upwind",
      cfd::validation::GridSpec{"", nx, ny, kLength, kHeight}, result, 1e-6, seconds);
  run.checks.push_back({"solve_accepted", run.level.accepted,
                        run.level.solverStatus + " " + run.level.rejectionReason});
  if (!run.level.accepted) return run;

  // --- profile at the fully developed station (cell-centre samples) ---
  const auto profile = cfd::validation::extractVerticalProfileU(mesh, nx, ny, result.velocity,
                                                                kProfileStation, kHeight);
  std::vector<Real> numeric, exact, discrete;
  // The discrete exact solution sampled exactly like the numerical one (cell
  // centres, wall anchors u = 0), so the centerline value is interpolated
  // the same way.
  std::vector<cfd::validation::ProfileSample> discreteProfile;
  for (const auto& sample : profile) {
    if (sample.coordinate <= 0.0 || sample.coordinate >= kHeight) {  // wall anchors
      discreteProfile.push_back(sample);
      continue;
    }
    numeric.push_back(sample.value);
    exact.push_back(
        cfd::validation::analyticalPoiseuilleVelocity(sample.coordinate, kHeight, kMeanVelocity));
    discrete.push_back(
        cfd::validation::discretePoiseuilleVelocity(sample.coordinate, kHeight, kMeanVelocity, ny));
    discreteProfile.push_back({sample.coordinate, discrete.back()});
  }
  // Uniform rows: unweighted over the ny cell centres == volume-weighted.
  run.level.errors.emplace_back("u_profile",
                                cfd::validation::computeSampleErrorNorms(numeric, exact));
  run.level.errors.emplace_back("u_profile_vs_discrete_exact",
                                cfd::validation::computeSampleErrorNorms(numeric, discrete));
  const Real centerline = cfd::validation::interpolateProfile(profile, 0.5 * kHeight);
  const Real centerlineDiscrete =
      cfd::validation::interpolateProfile(discreteProfile, 0.5 * kHeight);

  // --- mass flow ---
  Real inletFlow = 0.0, outletFlow = 0.0, maxWallFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) {
      if (patch.name() == "left") inletFlow -= result.massFlux[f];  // outward normal is -x
      if (patch.name() == "right") outletFlow += result.massFlux[f];
      if (patch.name() == "top" || patch.name() == "bottom") {
        maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[f]));
      }
    }
  }
  Real maxSectionError = std::abs(outletFlow - kFlowRate);
  maxSectionError = std::max(maxSectionError, std::abs(inletFlow - kFlowRate));
  std::vector<std::pair<std::string, Real>> sections;
  for (const Real fraction : {0.25, 0.5, 0.75}) {
    const Real flow = cfd::validation::sectionMassFlow(mesh, result.massFlux, fraction * kLength);
    sections.emplace_back(fmt("section_flow_x%.2fL", fraction), flow);
    maxSectionError = std::max(maxSectionError, std::abs(flow - kFlowRate));
  }

  // --- pressure ---
  const auto gradient = cfd::validation::pairAveragedPressureGradient(
      mesh, nx, ny, result.pressure, kPressureStation1, kPressureStation2);
  const Real exactGradient =
      cfd::validation::analyticalPressureGradient(kViscosity, kMeanVelocity, kHeight);
  const Real discreteGradient =
      cfd::validation::discretePressureGradient(kViscosity, kMeanVelocity, kHeight, ny);
  const Real twoStation = cfd::validation::numericalPressureGradient(
      mesh, nx, ny, result.pressure, kPressureStation1, kPressureStation2);
  const Real oddEven = cfd::validation::pressureOddEvenAmplitude(
      mesh, nx, ny, result.pressure, kPressureStation1, kPressureStation2);

  auto& d = run.level.diagnostics;
  d.emplace_back("centerline_velocity", centerline);
  d.emplace_back("centerline_velocity_exact", 1.5 * kMeanVelocity);
  d.emplace_back("centerline_velocity_discrete_exact", centerlineDiscrete);
  d.emplace_back("inlet_flow", inletFlow);
  d.emplace_back("outlet_flow", outletFlow);
  for (const auto& s : sections) d.push_back(s);
  d.emplace_back("max_flow_error", maxSectionError);
  d.emplace_back("max_wall_normal_flux", maxWallFlux);
  d.emplace_back("pressure_gradient", gradient.gradient);
  d.emplace_back("pressure_gradient_exact", exactGradient);
  d.emplace_back("pressure_gradient_discrete_exact", discreteGradient);
  d.emplace_back("pressure_gradient_relative_error",
                 std::abs(gradient.gradient - exactGradient) / std::abs(exactGradient));
  d.emplace_back("pressure_gradient_relative_error_vs_discrete",
                 std::abs(gradient.gradient - discreteGradient) / std::abs(discreteGradient));
  d.emplace_back("pressure_drop", gradient.drop);
  d.emplace_back("pressure_drop_exact", exactGradient * (gradient.x2Face - gradient.x1Face));
  d.emplace_back("pressure_drop_x1", gradient.x1Face);
  d.emplace_back("pressure_drop_x2", gradient.x2Face);
  d.emplace_back("pressure_gradient_two_station_legacy", twoStation);
  d.emplace_back("pressure_odd_even_amplitude", oddEven);

  run.checks.push_back({"wall_normal_flux", maxWallFlux <= 1e-6,
                        fmt("max |wall mass flux| %.3g (bound 1e-6)", maxWallFlux)});
  // Conservation: inlet, outlet and three internal sections all carry the
  // prescribed flow (same 1e-6 as the P0 inlet/outlet gate).
  run.checks.push_back(
      {"mass_flow", maxSectionError <= 1e-6,
       fmt("max |Q - U H| over inlet, outlet and sections %.3g (bound 1e-6)", maxSectionError)});
  return run;
}

// Checks against the discrete exact solution. Bounds: the measured
// differences (entrance / outlet influence and the outer u-gate 2e-5 at
// the station; see results/p12-num-007) with margin -- they are 2-3 orders
// of magnitude below the discretisation error against the physics.
void addDiscreteExactChecks(ValidationRun& run) {
  if (!run.level.accepted) return;
  const auto& d = run.level.errors[1].second;  // u_profile_vs_discrete_exact
  run.checks.push_back({"profile_matches_discrete_exact", d.linf <= 1e-4,
                        fmt("max |u - u_discrete| %.3g (bound 1e-4)", d.linf)});
  const Real gradientError = required(run, "pressure_gradient_relative_error_vs_discrete");
  run.checks.push_back(
      {"pressure_gradient_matches_discrete_exact", gradientError <= 1e-3,
       fmt("|dp/dx - discrete exact| / |discrete exact| %.3g (bound 1e-3)", gradientError)});
}

void expectPassed(const ValidationRun& run) {
  EXPECT_TRUE(run.level.accepted) << run.level.rejectionReason;
  for (const auto& check : run.checks) {
    EXPECT_TRUE(check.passed) << run.id() << " " << check.name << ": " << check.detail;
  }
}

void describe(ValidationReport& report) {
  report.reference =
      "Continuous planar Poiseuille solution u = 6U(y/H)(1-y/H), dp/dx = -12 mu U/H^2 "
      "(analytical); and the exact fully developed solution of the discretisation, dp/dx "
      "scaled by ny^2/(ny^2+2) (derived, PoiseuilleValidationUtils.hpp)";
  report.coefficients = {{"reynolds", kReynolds},   {"length", kLength},
                         {"height", kHeight},       {"density", kDensity},
                         {"viscosity", kViscosity}, {"mean_velocity", kMeanVelocity}};
  report.configuration = {
      {"boundaries",
       "uniform Inlet U = 1 (left), zero-gradient Outlet with p = 0 (right), no-slip walls"},
      {"algorithm",
       "SIMPLE, upwind convection, central diffusion, alpha 0.7/0.3, outer gates u 2e-5, p 5e-4, "
       "continuity 1e-6"},
      {"stations",
       "profile at x = 0.75 L; pressure gradient between the faces nearest 0.40 L "
       "and 0.75 L; mass flow at 0.25/0.50/0.75 L and both ends"},
      {"pressure_estimator", "pair-averaged column pressures (odd-even mode cancels exactly)"}};
}

void writeReport(const ValidationReport& report, const std::string& stem) {
  cfd::validation::writeValidationReport(std::string(kOutputDirectory) + "/" + stem + ".json",
                                         report);
  std::ofstream md(std::string(kOutputDirectory) + "/" + stem + ".md", std::ios::binary);
  md << cfd::validation::validationReportMarkdown(report);
  std::printf("\n%s", cfd::validation::validationReportMarkdown(report).c_str());
}

}  // namespace

// The profile against the physics and against the discrete exact solution.
TEST(PoiseuilleValidation, Profile) {
  ValidationRun run = runPoiseuille(64, 8);
  addDiscreteExactChecks(run);
  expectPassed(run);
  ASSERT_TRUE(run.level.accepted);
  const auto& physics = run.level.errors[0].second;
  // The profile error against the physics IS the discrete solution's
  // (difference below the discrete-exact bound): measured L2 vs Linf.
  const Real centerline = required(run, "centerline_velocity");
  const Real centerlineDiscrete = required(run, "centerline_velocity_discrete_exact");
  EXPECT_NEAR(centerline, centerlineDiscrete, 1e-4);
  // Discretisation error of the centerline value: exactly 1.5 * 2/(ny^2+2).
  EXPECT_NEAR(1.5 - centerline, 1.5 * 2.0 / 66.0, 1e-4);
  std::printf(
      "u_profile vs physics: L1 %.4e L2 %.4e Linf %.4e; centerline %.8f (discrete exact %.8f)\n",
      physics.l1, physics.l2, physics.linf, centerline, centerlineDiscrete);
}

// Inlet, outlet and internal sections carry the prescribed flow rate.
TEST(PoiseuilleValidation, MassFlow) {
  const ValidationRun run = runPoiseuille(64, 8);
  expectPassed(run);
  ASSERT_TRUE(run.level.accepted);
  EXPECT_LE(required(run, "max_flow_error"), 1e-6);
  EXPECT_LT(*run.level.massImbalance, 1e-6);
}

// Pressure gradient and drop over the developed segment (checkerboard-
// immune estimator), against the discrete exact and the physical values.
TEST(PoiseuilleValidation, PressureDrop) {
  ValidationRun run = runPoiseuille(64, 8);
  addDiscreteExactChecks(run);
  expectPassed(run);
  ASSERT_TRUE(run.level.accepted);
  const Real gradient = required(run, "pressure_gradient");
  const Real exact = required(run, "pressure_gradient_exact");
  // Relative error against the physics = the predicted discretisation error
  // 2/(ny^2+2) = 3.03 % on ny = 8 (to within the discrete-exact bound).
  EXPECT_NEAR(std::abs(gradient - exact) / std::abs(exact), 2.0 / 66.0, 1e-3);
  const Real drop = required(run, "pressure_drop");
  const Real dropExact = required(run, "pressure_drop_exact");
  EXPECT_NEAR(drop / dropExact, 64.0 / 66.0, 1e-3);
  std::printf(
      "dp/dx %.6f (exact %.6f, discrete exact %.6f), drop %.6f (exact %.6f), odd-even "
      "amplitude %.3e, legacy two-station %.6f\n",
      gradient, exact, required(run, "pressure_gradient_discrete_exact"), drop, dropExact,
      required(run, "pressure_odd_even_amplitude"),
      required(run, "pressure_gradient_two_station_legacy"));
}

// poiseuille.json: 64x8 / 96x12 / 144x18 (the NUM-005 grids, r = 1.5),
// every quantity per grid, NUM-005 grid convergence of the centerline
// velocity and the pressure gradient, and observed orders of the profile
// error norms against the physics.
TEST(PoiseuilleValidation, ProductionGridConvergence) {
  using cfd::validation::GridConvergenceStatus;
  using cfd::validation::NormKind;
  ValidationReport report;
  report.name = "poiseuille";
  report.description = "Planar Poiseuille channel L = 8H, Re = 10: grids 64x8, 96x12, 144x18";
  describe(report);
  for (const auto& [nx, ny] : {std::pair<Index, Index>{64, 8}, {96, 12}, {144, 18}}) {
    report.runs.push_back(runPoiseuille(nx, ny));
    addDiscreteExactChecks(report.runs.back());
  }
  for (const auto& run : report.runs) expectPassed(run);
  for (const auto& run : report.runs) ASSERT_TRUE(run.level.accepted) << run.id();

  cfd::validation::QuantitySpec centerline;
  centerline.name = "centerline_velocity";
  centerline.description = "u(0.75 L, H/2); exact 1.5 U";
  centerline.reference = 1.5 * kMeanVelocity;
  centerline.referenceKind = "analytical";
  centerline.options.formalOrder = 2.0;
  centerline.options.absoluteNoise = 1e-5;
  centerline.options.gridIndependenceThreshold = 0.01;
  cfd::validation::QuantitySpec gradient = centerline;
  gradient.name = "pressure_gradient";
  gradient.description = "dp/dx, pair-averaged between 0.40 L and 0.75 L; exact -12 mu U / H^2";
  gradient.reference =
      cfd::validation::analyticalPressureGradient(kViscosity, kMeanVelocity, kHeight);
  std::vector<cfd::validation::GridStudyEntry> entries;
  for (const auto& run : report.runs) {
    entries.push_back(cfd::validation::toGridStudyEntry(
        run, kLength, kHeight, {"centerline_velocity", "pressure_gradient"}));
  }
  report.gridConvergence.push_back(cfd::validation::analyzeGridStudy(
      "poiseuille_production", "Poiseuille Re = 10, grids 64x8 / 96x12 / 144x18",
      std::move(entries), {centerline, gradient}));
  cfd::validation::GridConvergenceOptions second;
  second.formalOrder = 2.0;
  const std::vector<const ValidationRun*> runs = {&report.runs[0], &report.runs[1],
                                                  &report.runs[2]};
  for (const NormKind norm : {NormKind::L1, NormKind::L2, NormKind::Linf}) {
    report.orders.push_back(
        cfd::validation::computeRunSequenceOrder(runs, "u_profile", norm, second));
  }

  // Gates: both quantities in the asymptotic range of the formal second
  // order, the GCI bounds the true (known) error, and the profile L2 norm
  // converges at the formal order.
  for (const auto& q : report.gridConvergence[0].quantities) {
    const auto& a = q.analysis;
    report.gates.push_back(
        {q.spec.name + "_asymptotic", a.status == GridConvergenceStatus::Asymptotic, a.diagnostic});
    const bool bounded = a.gci21.has_value() && q.relativeErrorVsReference[2].has_value() &&
                         *q.relativeErrorVsReference[2] <= *a.gci21;
    report.gates.push_back(
        {q.spec.name + "_gci_bounds_true_error", bounded,
         "fine relative error " +
             (q.relativeErrorVsReference[2] ? std::to_string(*q.relativeErrorVsReference[2])
                                            : "n/a") +
             " <= GCI21 " + (a.gci21 ? std::to_string(*a.gci21) : std::string("n/a"))});
  }
  const auto& l2 = report.orders[1];
  report.gates.push_back(
      {"u_profile_l2_asymptotic",
       !l2.triplets.empty() && l2.triplets.back().status == GridConvergenceStatus::Asymptotic,
       l2.triplets.empty() ? "no triplet" : l2.triplets.back().diagnostic});
  report.limitations = {
      "Collocated SIMPLE without Rhie-Chow interpolation: an odd-even pressure mode grows on the "
      "residual plateau (amplitude reported per run as pressure_odd_even_amplitude). The "
      "pair-averaged estimator is immune to it; the legacy two-column estimate is not (reported "
      "as pressure_gradient_two_station_legacy).",
      "Outer u/p gates 2e-5 / 5e-4 (the case's documented residual plateau); continuity and mass "
      "flow are converged to 1e-6.",
      "Runtimes are wall-clock and not deterministic."};
  writeReport(report, "poiseuille");
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}
