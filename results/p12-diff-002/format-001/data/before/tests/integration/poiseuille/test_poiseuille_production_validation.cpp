// P12-NUM-007 -- Production validation: planar Poiseuille flow (same case
// as test_poiseuille_validation.cpp: L = 8H, H = 1, rho = 1, mu = 0.1,
// uniform inlet U = 1, Re = 10, zero-gradient outlet with p = 0).
//
// Two references, kept apart:
//   * the continuous solution u = 6U (y/H)(1 - y/H), dp/dx = -12 mu U / H^2
//     -- the physics;
//   * the EXACT fully developed solution of this code's discretisation
//     (discretePoiseuilleVelocity / discretePressureGradient in
//     PoiseuilleValidationUtils.hpp): dp/dx scaled by 2ny^2/(2ny^2 + 1), i.e.
//     a relative discretisation error of exactly 1/(2ny^2 + 1) -- second
//     order, as the scheme's formal order predicts. Agreement with this one
//     shows every remaining difference from the physics is the predicted
//     discretisation error (not entrance, outlet or iterative error).
//     P12-DIFF-002-UC-001: this reference was migrated from the superseded
//     two-point wall treatment (ny^2/(ny^2+2), plus a +G dy^2/8 profile
//     offset) to the DIFF-002 second-order wall reconstruction. It is derived
//     from the stencil coefficients and verified in exact rational
//     arithmetic, and production's assembled matrix, RHS, solved profile,
//     centreline, dp/dx and wall flux were compared against it entry by
//     entry (worst 2.2e-16). Derivation, exact constants, the four wrong
//     controls every criterion must fail, and the reasons the two grid
//     studies use different triplets:
//     results/p12-diff-002/uc-001/acceptance_gate.md.
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

// P12-DIFF-002-UC-001: the discrete-exact identities, all computed from ny alone (never a
// stored production number). Derivation and exact-rational verification:
// results/p12-diff-002/uc-001/acceptance_gate.md section 1.

// |dp/dx - dp/dx_exact| / |dp/dx_exact| for the DIFF-002 wall treatment.
Real discreteGradientRelativeError(Index ny) {
  const Real n2 = static_cast<Real>(ny) * static_cast<Real>(ny);
  return 1.0 / (2.0 * n2 + 1.0);
}

// The pressure drop over a fully developed segment, as a fraction of the analytical drop.
Real discreteDropRatio(Index ny) {
  const Real n2 = static_cast<Real>(ny) * static_cast<Real>(ny);
  return 2.0 * n2 / (2.0 * n2 + 1.0);
}

// 1.5 U - interpolateProfile(profile, H/2) for the discrete solution: parity dependent, because
// for even ny the station falls midway between two cell centres and the linear interpolation
// cuts the parabola as a chord, while for odd ny it lands on a cell centre.
Real centerlineSamplingDeficit(Index ny) {
  const Real n2 = static_cast<Real>(ny) * static_cast<Real>(ny);
  const Real k = (ny % 2 == 0) ? 4.5 : 1.5;
  return k * kMeanVelocity / (2.0 * n2 + 1.0);
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
// P12-DIFF-002-UC-001 migrated the REFERENCE these compare against and left
// both bounds unchanged at 1e-4 / 1e-3. Measured against the derived
// DIFF-002 reference: profile L-inf 4.07e-06 / 3.59e-08 / 5.48e-09 /
// 8.23e-09 and dp/dx 8.34e-04 / 8.89e-05 / 1.78e-07 / 1.44e-07 on
// ny = 8 / 12 / 18 / 27. The superseded two-point reference gave 1.48e-02
// and 2.24e-02 on 64x8 -- 148x and 22x over these same bounds -- so the
// checks are not vacuous; nor are they satisfied by a corrupted far-cell
// coefficient (gate C1/C2 controls: >= 4.6x over bound).
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
      "scaled by 2ny^2/(2ny^2+1) with the profile equal to the continuum parabola at the cell "
      "centres (derived from the DIFF-002 wall stencil, PoiseuilleValidationUtils.hpp)";
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
  // P12-DIFF-002-UC-001 (gate C4): the centerline sampling deficit is the derived identity
  // k/(2 ny^2 + 1) with k = 4.5 for even ny (H/2 is the chord midpoint between the two centre
  // cells) and 1.5 for odd ny (H/2 is a cell centre) -- see PoiseuilleValidationUtils.hpp.
  // Computed from ny, never stored as a number.
  EXPECT_NEAR(1.5 - centerline, centerlineSamplingDeficit(8), 1e-4);
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
  // P12-DIFF-002-UC-001 (gate C5): relative error against the physics = the derived
  // discretisation error 1/(2 ny^2 + 1) = 0.775 % on ny = 8 (it was 2/(ny^2+2) = 3.03 % under
  // the superseded two-point wall treatment -- four times larger at the same ny).
  EXPECT_NEAR(std::abs(gradient - exact) / std::abs(exact), discreteGradientRelativeError(8),
              1e-3);
  const Real drop = required(run, "pressure_drop");
  const Real dropExact = required(run, "pressure_drop_exact");
  // Gate C6: the derived drop ratio 2 ny^2/(2 ny^2 + 1) (was ny^2/(ny^2+2)).
  EXPECT_NEAR(drop / dropExact, discreteDropRatio(8), 1e-3);
  std::printf(
      "dp/dx %.6f (exact %.6f, discrete exact %.6f), drop %.6f (exact %.6f), odd-even "
      "amplitude %.3e, legacy two-station %.6f\n",
      gradient, exact, required(run, "pressure_gradient_discrete_exact"), drop, dropExact,
      required(run, "pressure_odd_even_amplitude"),
      required(run, "pressure_gradient_two_station_legacy"));
}

// poiseuille.json: 64x8 / 96x12 / 144x18 / 216x27 (r = 1.5 throughout),
// every quantity per grid, grid convergence of the centerline velocity and
// the pressure gradient, and observed orders of the profile error norms
// against the physics.
//
// P12-DIFF-002-UC-001 -- the two quantities are analysed on DIFFERENT
// triplets, for two separately derived reasons; see
// results/p12-diff-002/uc-001/acceptance_gate.md sections 1-2.
//
//   centerline_velocity : 64x8 / 96x12 / 144x18   (ny 8/12/18, ALL EVEN)
//     The centerline is sampled by interpolating the cell-centre profile at
//     y = H/2, whose error constant is k/(2 ny^2 + 1) with k = 4.5 for even
//     ny and 1.5 for odd -- exactly 3x apart. A mixed-parity triplet is not
//     an h-refinement sequence for this quantity: the EXACT reference values
//     for 12/18/27 report p = 0.9376, asymptotic ratio 0.65, with no solver
//     involved at all. ny = 27 is therefore excluded here.
//
//   pressure_gradient   : 96x12 / 144x18 / 216x27  (ny 12/18/27)
//     The pair-averaged estimator carries a residual bias that tracks the
//     odd-even pressure amplitude: measured 9.93e-04 (ny 8), 1.06e-04 (12),
//     2.13e-07 (18), i.e. 19.3 % / 4.6 % / 0.01 % of the grid-to-grid
//     difference the Richardson estimator consumes. DIFF-002 cut the dp/dx
//     discretisation error 4x without changing that bias, so ny = 8 is now
//     below the resolution at which this estimator can support an
//     asymptotic-order claim: the exact DIFF-002 sequence on 8/12/18 is
//     asymptotic (p = 1.9846, ratio 0.9938) while the measured one reports
//     2.2652 / 1.1135 -- reproduced to all printed digits by adding the
//     measured offsets to the exact reference. The claim is kept, measured
//     where the instrument is valid; it is NOT relaxed (the band stays
//     1 +/- 0.1), and absoluteNoise cannot substitute -- raising it only
//     reclassifies the sequence as InsufficientSeparation, which also fails.
TEST(PoiseuilleValidation, ProductionGridConvergence) {
  using cfd::validation::GridConvergenceStatus;
  using cfd::validation::NormKind;
  ValidationReport report;
  report.name = "poiseuille";
  report.description =
      "Planar Poiseuille channel L = 8H, Re = 10: grids 64x8, 96x12, 144x18, 216x27";
  describe(report);
  for (const auto& [nx, ny] :
       {std::pair<Index, Index>{64, 8}, {96, 12}, {144, 18}, {216, 27}}) {
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
  // Each quantity on the triplet where its own instrument is valid (see the header comment).
  report.gridConvergence.push_back(cfd::validation::analyzeGridStudy(
      "poiseuille_production_centerline",
      "Poiseuille Re = 10, grids 64x8 / 96x12 / 144x18 (ny all even: centreline sampling parity)",
      {entries[0], entries[1], entries[2]}, {centerline}));
  report.gridConvergence.push_back(cfd::validation::analyzeGridStudy(
      "poiseuille_production_gradient",
      "Poiseuille Re = 10, grids 96x12 / 144x18 / 216x27 (pressure-extraction bias <= 4.6 % of "
      "the grid-to-grid difference)",
      {entries[1], entries[2], entries[3]}, {gradient}));
  cfd::validation::GridConvergenceOptions second;
  second.formalOrder = 2.0;
  // The profile norms are computed over the cell-centre samples, so they are free of the
  // centreline parity artifact; the original 8/12/18 triplet is kept for them.
  const std::vector<const ValidationRun*> runs = {&report.runs[0], &report.runs[1],
                                                  &report.runs[2]};
  for (const NormKind norm : {NormKind::L1, NormKind::L2, NormKind::Linf}) {
    report.orders.push_back(
        cfd::validation::computeRunSequenceOrder(runs, "u_profile", norm, second));
  }

  // Gates: both quantities in the asymptotic range of the formal second
  // order, the GCI bounds the true (known) error, and the profile L2 norm
  // converges at the formal order.
  for (const auto& study : report.gridConvergence) {
    for (const auto& q : study.quantities) {
      const auto& a = q.analysis;
      report.gates.push_back({q.spec.name + "_asymptotic",
                              a.status == GridConvergenceStatus::Asymptotic, a.diagnostic});
      const bool bounded = a.gci21.has_value() && q.relativeErrorVsReference[2].has_value() &&
                           *q.relativeErrorVsReference[2] <= *a.gci21;
      report.gates.push_back(
          {q.spec.name + "_gci_bounds_true_error", bounded,
           "fine relative error " +
               (q.relativeErrorVsReference[2] ? std::to_string(*q.relativeErrorVsReference[2])
                                              : "n/a") +
               " <= GCI21 " + (a.gci21 ? std::to_string(*a.gci21) : std::string("n/a"))});
    }
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
      "P12-DIFF-002-UC-001: the pair-averaged dp/dx carries a residual bias against the derived "
      "discrete-exact value of 9.93e-04 (64x8), 1.06e-04 (96x12), 2.13e-07 (144x18), tracking "
      "the odd-even amplitude. On 64x8 that is 8.3e-04 relative, so the discrete-exact gradient "
      "checks there pass with only 1.2x margin on their (unchanged) 1e-3 bound, and the dp/dx "
      "asymptotic-order study starts at 96x12 instead.",
      "P12-DIFF-002-UC-001: the centreline sampling error constant is 3x larger for even ny than "
      "for odd, so its grid triplet must be parity-consistent (64x8 / 96x12 / 144x18 here); "
      "216x27 is used only for the pressure gradient.",
      "Runtimes are wall-clock and not deterministic."};
  writeReport(report, "poiseuille");
  for (const auto& gate : report.gates)
    EXPECT_TRUE(gate.passed) << gate.name << ": " << gate.detail;
}
