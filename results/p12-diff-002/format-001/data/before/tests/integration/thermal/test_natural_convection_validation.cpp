// P3-PHYS-002 -- Natural Convection Validation.
// Differentially heated square cavity vs. the de Vahl Davis (1983)
// benchmark (DeVahlDavis1983.hpp -- see
// validation/literature/natural_convection/README.md for the full
// provenance disclosure). Composes SIMPLE (temperature/buoyancy, P3-
// PHYS-001) and ThermalSolver in the same small, explicit outer Picard
// loop test_boussinesq_coupling.cpp already established, extended here
// with real quantitative benchmark comparison.
//
// Same "SIMPLE-numerics-frozen, diagnose-then-lock-thresholds"
// discipline as every prior validation task: nothing here changes
// SIMPLE, ThermalSolver, or BoussinesqBuoyancy's own formulas -- only
// SIMPLESettings/the outer-loop relaxation are configured per grid,
// diagnosed empirically (temporary standalone compilation) before these
// settings were written into this file.
//
// **Diagnosed solver-robustness finding (read before changing any
// setting below)**: at this benchmark's physically-required source
// magnitude (beta=Ra*Pr with the nondimensionalization this file uses,
// beta=710 for Ra=1e3), the *inner* pressure-correction BiCGSTAB solve
// needs a *looser* tolerance (absoluteTolerance=1e-7, not the tighter
// 1e-9 that works fine for non-buoyant cases) and a much larger
// iteration budget (30000) to converge reliably -- the exact same
// "diagnose, then loosen the *inner* solver tolerance, not the physics"
// finding TODO.md's own Lid-Driven-Cavity 40x40/80x80 status note
// already documents for a different (pure lid-driven, no buoyancy)
// case; this is a second, independent instance of the same known
// BiCGSTAB-without-preconditioning limitation, not a defect in the
// Boussinesq implementation (confirmed: the *identical* physics
// converges cleanly at lower Ra/beta with the default tighter
// tolerance -- see BoussinesqCouplingTest's own passing tests). Grid
// resolution is also diagnosed-and-limited for the same reason: 10x10/
// 15x15/20x20 all converge reliably with the settings below; 40x40 was
// attempted and, even after the same tolerance loosening, becomes
// unreliable (converges some outer iterations, diverges on others)
// within this session's available diagnostic time -- a genuine,
// disclosed scope limit (P3-PHYS-002 section 22's own "produce
// numerical evidence... first" was followed: this is solver-tuning
// sensitivity, not a physics defect, and fixing it further would need
// BiCGSTAB preconditioning support this task does not add). The three
// grids used here (10x10/15x15/20x20) are still a genuine, systematic
// refinement sequence, just smaller than the commonly-used 20/40/80
// triplet -- documented explicitly, not silently substituted.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

#include "DeVahlDavis1983.hpp"
#include "NaturalConvectionValidationUtils.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/thermal/ThermalSolver.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalStatus;

namespace {

// Nondimensionalization (see NaturalConvectionValidationUtils.hpp's own
// header comment and validation/literature/natural_convection/README.md):
// rho=cp=k=1 (alpha=1), nu=Pr, L=1, T_hot=1, T_cold=0, gravity
// magnitude=1, beta=Ra*Pr -- chosen so the raw dimensional solver output
// already *is* the de Vahl Davis nondimensional convention.
constexpr Real kLength = 1.0;
constexpr Real kHeight = 1.0;
constexpr Real kHotTemperature = 1.0;
constexpr Real kColdTemperature = 0.0;
constexpr Real kReferenceTemperature = 0.5;
constexpr Real kGravityMagnitude = 1.0;
constexpr Real kPrandtl = cfd::validation::de_vahl_davis_1983::kPrandtlNumber;  // 0.71.
constexpr Real kRa = 1.0e3;
constexpr Real kBeta = kRa * kPrandtl;  // = 710, since g=deltaT=L=1.

BoundaryConditionSet makeVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

BoundaryConditionSet makePressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

BoundaryConditionSet makeTemperatureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(kHotTemperature));
  boundaries.set(mesh, "right", std::make_unique<FixedTemperature>(kColdTemperature));
  boundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "bottom", std::make_unique<Adiabatic>());
  return boundaries;
}

// Diagnosed empirically (temporary standalone compilation, documented in
// this file's own header comment): the looser *inner* pressure-solver
// tolerance/iteration budget this benchmark's physically-required
// source magnitude needs, at every grid used here.
SIMPLESettings makeFlowSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 10000;
  settings.velocityRelaxation = 0.2;
  settings.pressureRelaxation = 0.1;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-5;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 1000;
  settings.momentumSolver.absoluteTolerance = 1e-9;
  settings.momentumSolver.relativeTolerance = 1e-7;
  settings.pressureSolver.maxIterations = 30000;
  settings.pressureSolver.absoluteTolerance = 1e-7;
  settings.pressureSolver.relativeTolerance = 1e-5;
  return settings;
}

struct GridCase {
  const char* label;
  Index n;  // n x n.
  Index maxOuterIterations;
};

constexpr GridCase kGrid10{"10x10", 10, 60};
constexpr GridCase kGrid15{"15x15", 15, 60};
constexpr GridCase kGrid20{"20x20", 20, 60};

// Every field a completed (Ra, grid) run needs beyond the
// NaturalConvectionValidationRecord itself, for tests that check
// something the record does not already carry (e.g. the circulation-
// direction sign check).
struct CoupledOutcome {
  cfd::validation::NaturalConvectionValidationRecord record;
  SIMPLEResult flow;
  ScalarField temperature;
};

// The outer Picard loop: uniform-T_ref initial guess -> (buoyancy from
// current T -> SIMPLE, warm-started from the previous outer iteration's
// velocity/pressure -> ThermalSolver, from the newly-converged mass
// flux) -> outer-relaxed temperature update -> repeat until the
// temperature stops changing or maxOuterIterations is reached. Outer
// temperature relaxation (0.3, diagnosed) exists specifically to ramp
// the buoyancy source up gradually from zero rather than applying it at
// full strength in one step -- see this file's own header comment on
// why (a genuine, diagnosed robustness requirement at this Ra, not
// stylistic).
CoupledOutcome runNaturalConvectionCavity(const GridCase& grid, Real beta,
                                          const std::string& gridDirName,
                                          bool useVariablePropertyThermalSolve = false) {
  using namespace cfd::validation;
  using namespace cfd::validation::de_vahl_davis_1983;

  const Mesh mesh = MeshGeometry::createCartesian2D(grid.n, grid.n, kLength, kHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto temperatureBoundaries = makeTemperatureBoundaries(mesh);
  const FluidProperties fluid(1.0, kPrandtl);      // rho=1, mu=nu=Pr (since rho=1).
  const ThermalProperties thermalProps(1.0, 1.0);  // k=1, cp=1 -> alpha=1.
  // P3-PHYS-003 section 21: the exact same k=1/cp=1 physics, expressed
  // through the new TemperatureProperty-based ThermalSolver::solve()
  // overload instead -- used below only when
  // useVariablePropertyThermalSolve is set, so the pre-existing (and
  // already fully validated against de Vahl Davis) code path above is
  // completely untouched for every already-passing test in this file.
  const cfd::physics::ConstantProperty conductivityModel(1.0);
  const cfd::physics::ConstantProperty specificHeatModel(1.0);
  const ThermalSolver thermalSolver{};

  ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  ScalarField pressure(mesh.numberOfCells(), 0.0);
  SIMPLEResult flowResult;
  ThermalResult thermalResult;
  constexpr Real kOuterRelaxation = 0.3;
  constexpr Real kOuterTolerance = 1e-8;
  Index outerIterationsUsed = 0;
  Real finalOuterChange = 0.0;

  const auto t0 = std::chrono::steady_clock::now();
  for (Index outer = 0; outer < grid.maxOuterIterations; ++outer) {
    const BoussinesqBuoyancy buoyancy(fluid.density(), beta, kReferenceTemperature,
                                      Vector2{0.0, -kGravityMagnitude});
    const SIMPLE simple(makeFlowSettings(), 0, nullptr, &temperature, &buoyancy);
    flowResult =
        simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
    outerIterationsUsed = outer + 1;
    if (flowResult.status != SIMPLEStatus::Converged) break;
    velocity = flowResult.velocity;
    pressure = flowResult.pressure;

    thermalResult =
        useVariablePropertyThermalSolve
            ? thermalSolver.solve(mesh, temperature, flowResult.massFlux, conductivityModel,
                                  specificHeatModel, temperatureBoundaries)
            : thermalSolver.solve(mesh, temperature, flowResult.massFlux, thermalProps,
                                  temperatureBoundaries);
    if (thermalResult.status != ThermalStatus::Converged) break;

    ScalarField blended(temperature.size());
    Real maxChange = 0.0;
    for (Index i = 0; i < temperature.size(); ++i) {
      blended[i] =
          temperature[i] + kOuterRelaxation * (thermalResult.temperature[i] - temperature[i]);
      maxChange = std::max(maxChange, std::abs(blended[i] - temperature[i]));
    }
    temperature = blended;
    finalOuterChange = maxChange;
    if (maxChange < kOuterTolerance) break;
  }
  const auto t1 = std::chrono::steady_clock::now();

  CoupledOutcome outcome;
  outcome.flow = flowResult;
  outcome.temperature = temperature;

  NaturalConvectionValidationRecord& record = outcome.record;
  record.ra = beta / kPrandtl;  // = Ra, restated from beta (g=deltaT=L=1).
  record.pr = kPrandtl;
  record.nx = grid.n;
  record.ny = grid.n;
  record.flowConverged = (flowResult.status == SIMPLEStatus::Converged);
  record.flowIterations = flowResult.iterations;
  record.thermalConverged = (thermalResult.status == ThermalStatus::Converged);
  record.thermalIterations = thermalResult.iterations;
  record.outerIterations = outerIterationsUsed;
  record.finalOuterTemperatureChange = finalOuterChange;
  record.globalMassImbalance = flowResult.globalMassImbalance;
  record.runtimeSeconds = std::chrono::duration<double>(t1 - t0).count();

  if (!record.flowConverged || !record.thermalConverged) return outcome;

  Real maxWallFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      maxWallFlux = std::max(maxWallFlux, std::abs(flowResult.massFlux[faceId]));
    }
  }
  record.maxWallNormalFlux = maxWallFlux;

  record.qHot = computeWallHeatFluxIntoFluid(mesh, grid.n, grid.n, temperature, 0, kHotTemperature,
                                             kLength, kHeight);
  record.qCold = computeWallHeatFluxIntoFluid(mesh, grid.n, grid.n, temperature, grid.n - 1,
                                              kColdTemperature, kLength, kHeight);
  record.heatImbalance = std::abs(record.qHot + record.qCold) /
                         std::max(std::abs(record.qHot), std::abs(record.qCold));

  const auto nusseltProfile =
      computeLocalNusseltAtHotWall(mesh, grid.n, grid.n, temperature, kHotTemperature, kLength);
  record.nuAvgComputed = computeAverageNusselt(nusseltProfile);
  record.nuAvgReference = kRa1e3.nuAvg;
  record.nuAvgError = relativeError(record.nuAvgComputed, record.nuAvgReference);

  const auto uProfile =
      extractUProfileAtMidWidth(mesh, grid.n, grid.n, velocity, kLength / 2.0, kHeight);
  const auto uMax = findMax(uProfile);
  record.uMaxComputed = uMax.value;
  record.uMaxComputedY = uMax.coordinate;
  record.uMaxReference = kRa1e3.uMax;
  record.uMaxError = relativeError(record.uMaxComputed, record.uMaxReference);

  const auto vProfile =
      extractVProfileAtMidHeight(mesh, grid.n, grid.n, velocity, kHeight / 2.0, kLength);
  const auto vMax = findMax(vProfile);
  record.vMaxComputed = vMax.value;
  record.vMaxComputedX = vMax.coordinate;
  record.vMaxReference = kRa1e3.vMax;
  record.vMaxError = relativeError(record.vMaxComputed, record.vMaxReference);

  Real minTheta = 1e300, maxTheta = -1e300;
  for (Index i = 0; i < temperature.size(); ++i) {
    minTheta = std::min(minTheta, temperature[i]);
    maxTheta = std::max(maxTheta, temperature[i]);
  }
  record.minTheta = minTheta;
  record.maxTheta = maxTheta;

  const std::string gridDir = "results/validation/natural_convection/Ra1e3/" + gridDirName;
  std::filesystem::create_directories(gridDir);
  {
    std::vector<std::tuple<Real, Real, Real>> rows;
    rows.reserve(uProfile.size());
    for (const auto& s : uProfile) rows.emplace_back(s.coordinate, s.value, 0.0);
    writeProfileCsv(gridDir + "/u_centerline.csv", rows, "y", "u");
  }
  {
    std::vector<std::tuple<Real, Real, Real>> rows;
    rows.reserve(vProfile.size());
    for (const auto& s : vProfile) rows.emplace_back(s.coordinate, s.value, 0.0);
    writeProfileCsv(gridDir + "/v_centerline.csv", rows, "x", "v");
  }
  {
    std::vector<std::tuple<Real, Real, Real>> rows;
    rows.reserve(nusseltProfile.size());
    for (const auto& s : nusseltProfile) rows.emplace_back(s.coordinate, s.value, 0.0);
    writeProfileCsv(gridDir + "/nusselt_hot_wall.csv", rows, "y", "nu");
  }
  writeValidationJson(gridDir + "/validation.json", record);

  return outcome;
}

// P12-DIFF-002-UF-001 -- how the de Vahl Davis benchmark is applied.
//
// The literature values are FIXED and are never changed here. What UF-001
// changed is WHERE the agreement claim is made.
//
// The superseded criterion compared a single 10x10 solve against the
// benchmark with tolerances its own comment recorded as "locked from
// diagnosed evidence (coarsest grid, largest error): u_max 10.1%, v_max
// 6.7%, Nu_avg 4.9% -- margin above each". Those are the pre-DIFF-002
// production numbers (measured: v_max 6.714%, Nu_avg 4.856%), so the bound
// was a regression lock on one non-asymptotic grid rather than an
// application of the benchmark -- and de Vahl Davis's tabulated values are
// themselves h->0 Richardson-extrapolated, not values on any mesh. Two
// measured consequences, both in
// results/p12-diff-002/uf-001/acceptance_gate.md:
//   * 10x10 is not in the asymptotic range (v_max error 13.0%, Nu_avg 2.2%);
//   * on the v_max assertion specifically the old bound is ANTI-correlated
//     with wall-operator correctness -- a deliberately far-cell-doubled
//     operator scores 0.01855 there against the correct code's 0.13026.
//
// The claim is therefore made against a refinement PAIR, and strengthened:
//   P1  each error must SHRINK toward the fixed literature value;
//   P2  by at least 1.5x per doubling (formal order 1 -- first-order upwind
//       convection -- predicts 2.0; 1.5 admits a 25% pre-asymptotic
//       shortfall);
//   P3  the 20x20 errors must meet the bounds THIS FILE already carried in
//       DISABLED_Grid20x20MatchesDeVahlDavisRa1e3 (0.08/0.08/0.05) -- no
//       threshold is loosened, the precision claim simply moves to the grid
//       that can support it;
//   P4  each extremum's LOCATION must sit within one cell of the location
//       the literature reports (u_max y=0.813, v_max x=0.179) -- benchmark
//       data that DeVahlDavis1983.hpp carried and no test used.
//
// Non-vacuity, measured before this was written: a far-cell-doubled operator
// fails 8 of 13 criteria and a sign-flipped one 7 of 13, while the
// superseded two-point operator PASSES -- correctly, since it converges to
// the same continuum limit and is a legitimate lower-order scheme, not a
// defect. These criteria deliberately do NOT discriminate the wall operator;
// that is pinned by the wall-shear order study (two-point 1.000 vs DIFF-002
// 2.000, quadratic-exact) in uf-001/logs/07,08.
//
// n = 10 and n = 20 are both EVEN on purpose: the mid-plane is a
// cell-centre line for odd n and interpolated between two lines for even n,
// so a mixed-parity sequence is not an h-refinement sequence for these
// sampled extrema (this file's own GridConvergence comment already records
// that u_max "oscillates" on the mixed 10/15/20 family).
void expectApproachesDeVahlDavis(
    const cfd::validation::NaturalConvectionValidationRecord& coarse,
    const cfd::validation::NaturalConvectionValidationRecord& fine) {
  using cfd::validation::de_vahl_davis_1983::kRa1e3;
  ASSERT_EQ(coarse.nx, 10);
  ASSERT_EQ(fine.nx, 20);
  ASSERT_EQ(coarse.nx % 2, 0) << "parity rule: both grids must be even";
  ASSERT_EQ(fine.nx % 2, 0) << "parity rule: both grids must be even";

  struct Quantity {
    const char* name;
    Real coarseError;
    Real fineError;
    Real fineEnvelope;  // this file's own pre-existing 20x20 bound
    Real reference;     // the FIXED literature value, kept visible
  };
  const Quantity quantities[] = {
      {"nu_avg", coarse.nuAvgError, fine.nuAvgError, 0.05, kRa1e3.nuAvg},
      {"u_max", coarse.uMaxError, fine.uMaxError, 0.08, kRa1e3.uMax},
      {"v_max", coarse.vMaxError, fine.vMaxError, 0.08, kRa1e3.vMax}};

  constexpr Real kRateFloor = 1.5;
  for (const auto& q : quantities) {
    // P1 -- refinement moves toward the fixed literature value.
    EXPECT_LT(q.fineError, q.coarseError)
        << q.name << ": refining 10x10 -> 20x20 must move TOWARD the de Vahl Davis value "
        << q.reference << " (errors " << q.coarseError << " -> " << q.fineError << ")";
    // P2 -- at a rate consistent with the scheme's formal order of 1.
    ASSERT_GT(q.fineError, 0.0) << q.name << ": zero fine error, rate undefined";
    const Real rate = q.coarseError / q.fineError;
    EXPECT_GE(rate, kRateFloor)
        << q.name << ": error reduction per grid doubling " << rate << " is below the " << kRateFloor
        << " floor (formal order 1 predicts 2.0)";
    // P3 -- the 20x20 precision envelope this file already carried.
    EXPECT_LT(q.fineError, q.fineEnvelope)
        << q.name << ": 20x20 relative error against " << q.reference << " exceeds the envelope";
  }

  // P4 -- the extremum LOCATIONS against the locations the literature reports.
  for (const auto& r : {coarse, fine}) {
    const Real cell = 1.0 / static_cast<Real>(r.nx);
    EXPECT_LE(std::abs(r.uMaxComputedY - kRa1e3.uMaxY), cell)
        << r.nx << "x" << r.ny << ": u_max is at y=" << r.uMaxComputedY
        << ", literature y=" << kRa1e3.uMaxY << " (tolerance one cell, " << cell << ")";
    EXPECT_LE(std::abs(r.vMaxComputedX - kRa1e3.vMaxX), cell)
        << r.nx << "x" << r.ny << ": v_max is at x=" << r.vMaxComputedX
        << ", literature x=" << kRa1e3.vMaxX << " (tolerance one cell, " << cell << ")";
  }

  // The 10x10 benchmark errors are PRESERVED as diagnostic evidence; they are
  // no longer an acceptance bound. (Measured when UF-001 froze its gate:
  // Nu_avg 0.02246, u_max 0.13703, v_max 0.13026.)
  std::printf(
      "de Vahl Davis Ra=1e3 -- 10x10 errors: Nu_avg %.5f u_max %.5f v_max %.5f | "
      "20x20 errors: Nu_avg %.5f u_max %.5f v_max %.5f | reduction factors %.3f/%.3f/%.3f\n",
      coarse.nuAvgError, coarse.uMaxError, coarse.vMaxError, fine.nuAvgError, fine.uMaxError,
      fine.vMaxError, coarse.nuAvgError / fine.nuAvgError, coarse.uMaxError / fine.uMaxError,
      coarse.vMaxError / fine.vMaxError);
}

}  // namespace

// =====================================================================
// Primary benchmark: Ra=1e3, three grids.
// =====================================================================

TEST(NaturalConvectionValidation, Grid10x10MatchesDeVahlDavisRa1e3) {
  const auto outcome = runNaturalConvectionCavity(kGrid10, kBeta, "10x10");
  const auto& r = outcome.record;
  ASSERT_TRUE(r.flowConverged) << "flow did not converge";
  ASSERT_TRUE(r.thermalConverged) << "thermal did not converge";
  EXPECT_LT(r.globalMassImbalance, 1e-6);
  EXPECT_LT(r.maxWallNormalFlux, 1e-6);
  EXPECT_LT(r.heatImbalance, 0.05);
  EXPECT_GE(r.minTheta, -1e-6);
  EXPECT_LE(r.maxTheta, 1.0 + 1e-6);
  // P12-DIFF-002-UF-001: the benchmark-agreement claim is made against the
  // refinement pair, not against this single coarse grid -- see
  // expectApproachesDeVahlDavis and
  // results/p12-diff-002/uf-001/acceptance_gate.md.
  const auto fine = runNaturalConvectionCavity(kGrid20, kBeta, "10x10_refinement_pair");
  ASSERT_TRUE(fine.record.flowConverged) << "20x20 flow did not converge";
  ASSERT_TRUE(fine.record.thermalConverged) << "20x20 thermal did not converge";
  expectApproachesDeVahlDavis(r, fine.record);
}

// P3-PHYS-003 section 21: the same 10x10/Ra=1e3 benchmark case above, but
// routed through the new TemperatureProperty-based ThermalSolver::solve()
// overload (k=1/cp=1 expressed as ConstantProperty models instead of a
// fixed ThermalProperties) -- must clear the exact same gates as
// Grid10x10MatchesDeVahlDavisRa1e3 above, proving the variable-property
// abstraction is a genuine drop-in replacement for the already-validated
// natural-convection coupling, not just at the equation-assembly unit
// level (already proven exactly in
// ThermalSolverVariablePropertiesTest.ConstantModelsReproduceThermalPropertiesOverloadExactly)
// but through this full nonlinear buoyancy-coupled solve too.
TEST(NaturalConvectionValidation, Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3) {
  const auto outcome =
      runNaturalConvectionCavity(kGrid10, kBeta, "10x10_variable_property_regression",
                                 /*useVariablePropertyThermalSolve=*/true);
  const auto& r = outcome.record;
  ASSERT_TRUE(r.flowConverged) << "flow did not converge";
  ASSERT_TRUE(r.thermalConverged) << "thermal did not converge";
  EXPECT_LT(r.globalMassImbalance, 1e-6);
  EXPECT_LT(r.maxWallNormalFlux, 1e-6);
  EXPECT_LT(r.heatImbalance, 0.05);
  EXPECT_GE(r.minTheta, -1e-6);
  EXPECT_LE(r.maxTheta, 1.0 + 1e-6);
  // P12-DIFF-002-UF-001: the same refinement-pair claim as
  // Grid10x10MatchesDeVahlDavisRa1e3 above -- this is the identical physics
  // through a different (field-based) code path, so it must clear the
  // identical criteria.
  const auto fine = runNaturalConvectionCavity(
      kGrid20, kBeta, "20x20_variable_property_regression",
      /*useVariablePropertyThermalSolve=*/true);
  ASSERT_TRUE(fine.record.flowConverged) << "20x20 flow did not converge";
  ASSERT_TRUE(fine.record.thermalConverged) << "20x20 thermal did not converge";
  expectApproachesDeVahlDavis(r, fine.record);
}

TEST(NaturalConvectionValidation, Grid15x15MatchesDeVahlDavisRa1e3) {
  const auto outcome = runNaturalConvectionCavity(kGrid15, kBeta, "15x15");
  const auto& r = outcome.record;
  ASSERT_TRUE(r.flowConverged) << "flow did not converge";
  ASSERT_TRUE(r.thermalConverged) << "thermal did not converge";
  EXPECT_LT(r.globalMassImbalance, 1e-6);
  EXPECT_LT(r.maxWallNormalFlux, 1e-6);
  EXPECT_LT(r.heatImbalance, 0.05);
  EXPECT_LT(r.uMaxError, 0.10);
  EXPECT_LT(r.vMaxError, 0.10);
  EXPECT_LT(r.nuAvgError, 0.08);
  EXPECT_GE(r.minTheta, -1e-6);
  EXPECT_LE(r.maxTheta, 1.0 + 1e-6);
}

TEST(NaturalConvectionValidation, DISABLED_Grid20x20MatchesDeVahlDavisRa1e3) {
  const auto outcome = runNaturalConvectionCavity(kGrid20, kBeta, "20x20");
  const auto& r = outcome.record;
  ASSERT_TRUE(r.flowConverged) << "flow did not converge";
  ASSERT_TRUE(r.thermalConverged) << "thermal did not converge";
  EXPECT_LT(r.globalMassImbalance, 1e-6);
  EXPECT_LT(r.maxWallNormalFlux, 1e-6);
  EXPECT_LT(r.heatImbalance, 0.05);
  EXPECT_LT(r.uMaxError, 0.08);
  EXPECT_LT(r.vMaxError, 0.08);
  EXPECT_LT(r.nuAvgError, 0.05);
  EXPECT_GE(r.minTheta, -1e-6);
  EXPECT_LE(r.maxTheta, 1.0 + 1e-6);
}

// =====================================================================
// P12-NUM-005: three-grid convergence study (replaces the former
// DISABLED_GridRefinementReducesNusseltError, which only checked that the
// benchmark error did not grow). Grids 10 / 15 / 20 -- UNEQUAL refinement
// ratios r21 = 20/15 = 1.333, r32 = 15/10 = 1.5, solved exactly by the
// shared analysis -- through cfd::validation::runGridConvergenceStudy.
// A grid is accepted only if the final SIMPLE solve passes
// assessSimpleSolve, the thermal solve converged, and the outer Picard loop
// met its own 1e-8 temperature-change tolerance.
//
// Grid convergence is computed only from this code's three solutions
// (formal order 1: first-order upwind convection); the de Vahl Davis
// (1983) values are benchmark data, reported separately. Enabled in the
// default suite: ~400 s in Debug (10x10 24 s, 15x15 133 s, 20x20 247 s
// measured), the same order as the already-enabled per-grid 15x15 test
// (275 s). Measured result (results/p12-num-005/): Nu_avg converges
// monotonically but is NOT yet asymptotic on these grids (p = 1.56 vs
// formal 1); u_max (a cell-centre-sampled extremum) oscillates -- both
// reported, not hidden.
// =====================================================================

TEST(NaturalConvectionValidation, GridConvergence) {
  using cfd::validation::GridSolveOutput;
  using cfd::validation::GridSpec;
  const auto solve = [](const GridSpec& spec) {
    const GridCase grid = spec.nx == 10 ? kGrid10 : (spec.nx == 15 ? kGrid15 : kGrid20);
    const auto outcome = runNaturalConvectionCavity(grid, kBeta, grid.label);
    GridSolveOutput out;
    out.acceptance = cfd::validation::assessSimpleSolve(outcome.flow, 1e-6);
    out.solverIterations = outcome.record.outerIterations;
    if (out.acceptance.accepted && !outcome.record.thermalConverged) {
      out.acceptance.accepted = false;
      out.acceptance.reason = "thermal solve did not converge";
    }
    if (out.acceptance.accepted && !(outcome.record.finalOuterTemperatureChange < 1e-8)) {
      out.acceptance.accepted = false;
      out.acceptance.reason = "outer Picard loop did not converge";
    }
    if (!out.acceptance.accepted) return out;
    out.quantities.emplace_back("nu_avg", outcome.record.nuAvgComputed);
    out.quantities.emplace_back("u_max", outcome.record.uMaxComputed);
    out.quantities.emplace_back("v_max", outcome.record.vMaxComputed);
    out.quantities.emplace_back("heat_imbalance", outcome.record.heatImbalance);
    return out;
  };
  const auto quantity = [](const char* name, const char* description, Real reference) {
    cfd::validation::QuantitySpec q;
    q.name = name;
    q.description = description;
    q.reference = reference;
    q.referenceKind = "benchmark";
    q.options.formalOrder = 1.0;
    q.options.absoluteNoise = 1e-6;  // outer Picard tolerance 1e-8 on theta
    q.options.gridIndependenceThreshold = 0.01;
    return q;
  };
  using namespace cfd::validation::de_vahl_davis_1983;
  const auto study = cfd::validation::runGridConvergenceStudy(
      "natural_convection_ra1e3",
      "Differentially heated square cavity, Ra = 1e3, Pr = 0.71, SIMPLE + ThermalSolver Picard "
      "coupling (first-order upwind convection); references: de Vahl Davis (1983) benchmark data",
      {GridSpec{"coarse", 10, 10, kLength, kHeight}, GridSpec{"medium", 15, 15, kLength, kHeight},
       GridSpec{"fine", 20, 20, kLength, kHeight}},
      solve,
      {quantity("nu_avg", "average hot-wall Nusselt number", kRa1e3.nuAvg),
       quantity("u_max", "max u on the vertical mid-line (sampled at cell centres)", kRa1e3.uMax),
       quantity("v_max", "max v on the horizontal mid-line (sampled at cell centres)",
                kRa1e3.vMax)});
  cfd::validation::writeGridConvergenceReport(
      "results/validation/grid_convergence/natural_convection_ra1e3.json", study);
  {
    std::ofstream md("results/validation/grid_convergence/natural_convection_ra1e3.md");
    md << cfd::validation::gridConvergenceReportMarkdown(study);
  }
  std::printf("\n%s", cfd::validation::gridConvergenceReportMarkdown(study).c_str());
  // The written report passes the schema / consistency validator.
  const auto reportProblems = cfd::validation::validateGridConvergenceReportFile(
      "results/validation/grid_convergence/natural_convection_ra1e3.json");
  EXPECT_TRUE(reportProblems.empty()) << reportProblems.front();

  ASSERT_TRUE(study.allSolvesAccepted) << study.rejectionReason;
  const auto& nu = study.quantities[0];
  EXPECT_NE(nu.analysis.status, cfd::validation::GridConvergenceStatus::Invalid)
      << nu.analysis.diagnostic;
  EXPECT_NEAR(nu.analysis.r21, 20.0 / 15.0, 1e-12);
  EXPECT_NEAR(nu.analysis.r32, 1.5, 1e-12);
  for (const auto& q : study.quantities) {
    const auto& a = q.analysis;
    if (!a.observedOrder.has_value()) continue;
    ASSERT_TRUE(a.gci21.has_value() && a.gci32.has_value() && a.extrapolated21.has_value());
    EXPECT_LT(std::abs(*q.values[2] - *a.extrapolated21),
              std::abs(*q.values[0] - *a.extrapolated21))
        << q.spec.name;
  }
  // Benchmark comparison, separately: Nu_avg moves toward de Vahl Davis.
  EXPECT_LT(std::abs(*nu.errorVsReference[2]), std::abs(*nu.errorVsReference[0]));
}

// =====================================================================
// Circulation-direction sign check (section 15) -- solver-level
// manifestation of BoussinesqBuoyancyTest's own pure-physics sign
// proof: hot fluid near the hot (left) wall must rise.
// =====================================================================

TEST(NaturalConvectionValidation, HotFluidRisesNearHotWallColdFluidSinksNearColdWall) {
  const auto outcome = runNaturalConvectionCavity(kGrid10, kBeta, "10x10_sign_check");
  ASSERT_TRUE(outcome.record.flowConverged);
  const Index n = kGrid10.n;
  const Index midRow = n / 2;
  // Column 0 (hot wall side): v > 0 (rising). Column n-1 (cold wall
  // side): v < 0 (sinking).
  EXPECT_GT(outcome.flow.velocity[(midRow * n) + 0].y, 0.0) << "fluid near the hot wall must rise";
  EXPECT_LT(outcome.flow.velocity[(midRow * n) + (n - 1)].y, 0.0)
      << "fluid near the cold wall must sink";
}

// =====================================================================
// Physical sanity: beta=0 removes natural convection, leaving the pure
// conduction solution (section 15's own explicit requirement).
// =====================================================================

TEST(NaturalConvectionValidation, ZeroBetaRecoversPureConductionProfile) {
  const auto outcome = runNaturalConvectionCavity(kGrid10, 0.0, "10x10_zero_beta");
  ASSERT_TRUE(outcome.record.flowConverged);
  ASSERT_TRUE(outcome.record.thermalConverged);
  for (Index i = 0; i < outcome.flow.velocity.size(); ++i) {
    EXPECT_DOUBLE_EQ(outcome.flow.velocity[i].x, 0.0) << "cell " << i;
    EXPECT_DOUBLE_EQ(outcome.flow.velocity[i].y, 0.0) << "cell " << i;
  }
  // Pure 1D conduction: T(x) = Th + (Tc-Th)*x/L, exact for this FVM
  // scheme on an orthogonal Cartesian mesh (same analytical-exactness
  // precedent as ThermalSolverTest.ConvergesAndMatchesAnalytical1DConductionProfile).
  const Mesh mesh = MeshGeometry::createCartesian2D(kGrid10.n, kGrid10.n, kLength, kHeight);
  Real maxAbsError = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact =
        kHotTemperature + (kColdTemperature - kHotTemperature) * (cell.centroid().x / kLength);
    maxAbsError = std::max(maxAbsError, std::abs(outcome.temperature[cell.id()] - exact));
  }
  EXPECT_LT(maxAbsError, 1e-6);
}

// =====================================================================
// Determinism.
// =====================================================================

TEST(NaturalConvectionValidation, RepeatedCoupledSolveIsBitIdentical) {
  const auto a = runNaturalConvectionCavity(kGrid10, kBeta, "10x10_det_a");
  const auto b = runNaturalConvectionCavity(kGrid10, kBeta, "10x10_det_b");
  ASSERT_TRUE(a.record.flowConverged);
  ASSERT_EQ(a.record.flowConverged, b.record.flowConverged);
  ASSERT_EQ(a.record.outerIterations, b.record.outerIterations);
  ASSERT_EQ(a.flow.iterations, b.flow.iterations);
  for (Index i = 0; i < a.flow.velocity.size(); ++i) {
    EXPECT_EQ(a.flow.velocity[i].x, b.flow.velocity[i].x) << "cell " << i;
    EXPECT_EQ(a.flow.velocity[i].y, b.flow.velocity[i].y) << "cell " << i;
    EXPECT_EQ(a.flow.pressure[i], b.flow.pressure[i]) << "cell " << i;
    EXPECT_EQ(a.temperature[i], b.temperature[i]) << "cell " << i;
  }
}
