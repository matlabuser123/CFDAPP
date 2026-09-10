// P3-PHYS-004 sections 25-27: coupling regressions.
//   25. Species transported by a genuine SIMPLE-solved (not analytically
//       prescribed) production velocity field, without altering it.
//   26. Species coexists with momentum+pressure+temperature without
//       corrupting the thermal/flow solution (a decoupling regression).
//   27. Two independent passive species on the same flow show no
//       accidental cross-coupling.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/species/SpeciesProperties.hpp"
#include "cfd/species/SpeciesSolver.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::species::SpeciesProperties;
using cfd::species::SpeciesResult;
using cfd::species::SpeciesSolver;
using cfd::species::SpeciesSolverSettings;
using cfd::species::SpeciesStatus;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalStatus;

namespace {

constexpr Real kChannelLength = 4.0;
constexpr Real kChannelHeight = 1.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;
constexpr Index kNx = 20;
constexpr Index kNy = 8;

Mesh makeChannelMesh() {
  return MeshGeometry::createCartesian2D(kNx, kNy, kChannelLength, kChannelHeight);
}

BoundaryConditionSet makeVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{kMeanVelocity, 0.0}));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

BoundaryConditionSet makePressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

BoundaryConditionSet makeConcentrationBoundaries(const Mesh& mesh, Real inletValue) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(inletValue));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

SIMPLESettings makeFlowSettings() {
  // Same order-of-magnitude settings as
  // tests/integration/poiseuille/test_poiseuille_validation.cpp's own
  // smallest (64x8) grid case -- this file's own channel is smaller
  // still (20x8), but reuses that already-diagnosed-working combination
  // rather than the tight SIMPLESettings defaults (velocityTolerance/
  // pressureTolerance/continuityTolerance=1e-8, an unreachable plateau
  // for fixed-relaxation SIMPLE per that file's own documented finding)
  // or the even-tighter default inner BiCGSTAB tolerance (1e-12), which
  // together produced a PressureCorrectionFailure here before this fix.
  SIMPLESettings settings;
  settings.maxIterations = 3000;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 2e-5;
  settings.pressureTolerance = 5e-4;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-8;
  settings.pressureSolver.relativeTolerance = 1e-6;
  return settings;
}

// Diagnosed: on the genuine (parabolic, non-uniform-across-y) SIMPLE-
// solved Poiseuille massFlux, the default inner BiCGSTAB tolerance is
// occasionally too tight for reliable convergence -- same class of
// finding as test_species_advection_diffusion.cpp's own diagnosed
// robustness limit, loosened here the same way.
SpeciesSolverSettings makeSpeciesSettings() {
  SpeciesSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-8;
  settings.linearSolver.relativeTolerance = 1e-7;
  return settings;
}

SIMPLEResult runChannelFlow(const Mesh& mesh, const FluidProperties& fluid) {
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  VectorField velocity(n, Vector2{kMeanVelocity, 0.0});
  ScalarField pressure(n, 0.0);
  const SIMPLE simple(makeFlowSettings());
  return simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
}

}  // namespace

// --- Section 25: production-flow coupling -------------------------------

TEST(SpeciesSolverCouplingTest, TransportsThroughAGenuineSIMPLESolvedPoiseuilleFlow) {
  const Mesh mesh = makeChannelMesh();
  const FluidProperties fluid(kDensity, kViscosity);
  const SIMPLEResult flow = runChannelFlow(mesh, fluid);
  ASSERT_EQ(flow.status, SIMPLEStatus::Converged);
  EXPECT_LT(std::abs(flow.globalMassImbalance), 1e-6);

  // Snapshot the flow fields before the species solve to prove they are
  // untouched by it (section 25: "velocity remains unchanged").
  const VectorField velocityBefore = flow.velocity;
  const ScalarField pressureBefore = flow.pressure;

  const auto concentrationBoundaries = makeConcentrationBoundaries(mesh, 1.0);
  const ScalarField initialConcentration(mesh.numberOfCells(), 0.0);
  // Diffusivity chosen to keep this grid's *cell* Peclet number (u*dx/D,
  // dx=kChannelLength/kNx=0.2) around 1 -- see makeSpeciesSettings's own
  // header comment on the diagnosed BiCGSTAB robustness limit that a much
  // smaller D (e.g. the 1e-3 used in the dedicated Peclet-regime
  // validation, where a much finer 200-cell grid keeps cell Pe small even
  // at a high *domain* Pe) triggers on this coarser production-flow mesh.
  const SpeciesProperties species("tracer", 0.2);
  const SpeciesSolver speciesSolver{makeSpeciesSettings()};
  const SpeciesResult species_result = speciesSolver.solve(
      mesh, initialConcentration, flow.massFlux, fluid, species, concentrationBoundaries);
  ASSERT_EQ(species_result.status, SpeciesStatus::Converged);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(flow.velocity[i].x, velocityBefore[i].x);
    EXPECT_EQ(flow.velocity[i].y, velocityBefore[i].y);
    EXPECT_EQ(flow.pressure[i], pressureBefore[i]);
  }

  // Species genuinely responds to the flow: concentration must be
  // materially higher near the inlet-fed core of the channel than it
  // would be with no transport at all (initial condition was uniformly
  // 0), and stay within [0,1] (Dirichlet inlet + zero-gradient elsewhere,
  // no source).
  Real maxConcentration = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    maxConcentration = std::max(maxConcentration, species_result.concentration[i]);
    EXPECT_GE(species_result.concentration[i], -1e-6);
    EXPECT_LE(species_result.concentration[i], 1.0 + 1e-6);
  }
  EXPECT_GT(maxConcentration, 0.5);
}

// --- Section 26: thermal coexistence decoupling regression --------------

TEST(SpeciesSolverCouplingTest, PassiveSpeciesDoesNotAlterVelocityOrThermalSolution) {
  const Mesh mesh = makeChannelMesh();
  const FluidProperties fluid(kDensity, kViscosity);
  const SIMPLEResult flow = runChannelFlow(mesh, fluid);
  ASSERT_EQ(flow.status, SIMPLEStatus::Converged);

  BoundaryConditionSet temperatureBoundaries;
  temperatureBoundaries.set(mesh, "left", std::make_unique<FixedTemperature>(300.0));
  temperatureBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  temperatureBoundaries.set(mesh, "bottom", std::make_unique<FixedTemperature>(320.0));
  temperatureBoundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const ThermalProperties thermalProps(0.6, 4180.0);
  const ThermalSolver thermalSolver{};
  const ThermalResult thermalResult = thermalSolver.solve(mesh, initialTemperature, flow.massFlux,
                                                          thermalProps, temperatureBoundaries);
  ASSERT_EQ(thermalResult.status, ThermalStatus::Converged);

  // Run species on top of the same flow/thermal state.
  const auto concentrationBoundaries = makeConcentrationBoundaries(mesh, 1.0);
  const ScalarField initialConcentration(mesh.numberOfCells(), 0.0);
  // Diffusivity chosen to keep this grid's *cell* Peclet number (u*dx/D,
  // dx=kChannelLength/kNx=0.2) around 1 -- see makeSpeciesSettings's own
  // header comment on the diagnosed BiCGSTAB robustness limit that a much
  // smaller D (e.g. the 1e-3 used in the dedicated Peclet-regime
  // validation, where a much finer 200-cell grid keeps cell Pe small even
  // at a high *domain* Pe) triggers on this coarser production-flow mesh.
  const SpeciesProperties species("tracer", 0.2);
  const SpeciesSolver speciesSolver{makeSpeciesSettings()};
  const SpeciesResult speciesResult = speciesSolver.solve(mesh, initialConcentration, flow.massFlux,
                                                          fluid, species, concentrationBoundaries);
  ASSERT_EQ(speciesResult.status, SpeciesStatus::Converged);

  // Re-run velocity/pressure/temperature *without ever invoking the
  // species solver* -- must be bit-identical to the run above, proving
  // species is architecturally a pure downstream consumer (it is only
  // ever given massFlux/temperature as *inputs*; nothing in SIMPLE or
  // ThermalSolver takes a species field).
  const SIMPLEResult flowAgain = runChannelFlow(mesh, fluid);
  ASSERT_EQ(flowAgain.status, SIMPLEStatus::Converged);
  const ThermalResult thermalAgain = thermalSolver.solve(
      mesh, initialTemperature, flowAgain.massFlux, thermalProps, temperatureBoundaries);
  ASSERT_EQ(thermalAgain.status, ThermalStatus::Converged);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(flow.velocity[i].x, flowAgain.velocity[i].x);
    EXPECT_EQ(flow.velocity[i].y, flowAgain.velocity[i].y);
    EXPECT_EQ(flow.pressure[i], flowAgain.pressure[i]);
    EXPECT_EQ(thermalResult.temperature[i], thermalAgain.temperature[i]);
  }
}

// --- Section 27: multiple independent passive species --------------------

TEST(SpeciesSolverCouplingTest, TwoIndependentSpeciesShowNoCrossCoupling) {
  const Mesh mesh = makeChannelMesh();
  const FluidProperties fluid(kDensity, kViscosity);
  const SIMPLEResult flow = runChannelFlow(mesh, fluid);
  ASSERT_EQ(flow.status, SIMPLEStatus::Converged);

  // Dirichlet at *both* ends here (unlike the zero-gradient-outlet
  // convention makeConcentrationBoundaries() uses elsewhere in this file)
  // -- deliberately, so the resulting profile is the classic D-sensitive
  // exponential shape (test_species_advection_diffusion.cpp's own
  // regime), not the "saturates near the inlet value almost everywhere
  // regardless of D" shape a zero-gradient outlet gives at these Pe
  // values -- needed so a later change in D_A actually produces a
  // detectable difference for this test to check.
  auto makeDirichletBothEnds = [&mesh](Real inletValue) {
    BoundaryConditionSet boundaries;
    boundaries.set(mesh, "left", std::make_unique<FixedValue>(inletValue));
    boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
    boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
    boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
    return boundaries;
  };
  const auto boundariesA = makeDirichletBothEnds(1.0);
  const auto boundariesB = makeDirichletBothEnds(0.3);
  const ScalarField initial(mesh.numberOfCells(), 0.0);
  const SpeciesSolver solver{makeSpeciesSettings()};

  const SpeciesProperties speciesA("A", 0.2);
  const SpeciesProperties speciesB("B", 0.5);
  const SpeciesResult resultA1 =
      solver.solve(mesh, initial, flow.massFlux, fluid, speciesA, boundariesA);
  const SpeciesResult resultB1 =
      solver.solve(mesh, initial, flow.massFlux, fluid, speciesB, boundariesB);
  ASSERT_EQ(resultA1.status, SpeciesStatus::Converged);
  ASSERT_EQ(resultB1.status, SpeciesStatus::Converged);

  // Fields must genuinely differ (different inlet values/diffusivities).
  bool anyDifferent = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (std::abs(resultA1.concentration[i] - resultB1.concentration[i]) > 1e-6) {
      anyDifferent = true;
      break;
    }
  }
  EXPECT_TRUE(anyDifferent);

  // Change species A's diffusivity and re-solve both -- B's result must
  // be completely unaffected (no shared/leaked state between independent
  // solve() calls), and re-solving in the opposite order changes nothing
  // either.
  const SpeciesProperties speciesAPerturbed("A", 0.8);
  const SpeciesResult resultB2 =
      solver.solve(mesh, initial, flow.massFlux, fluid, speciesB, boundariesB);
  const SpeciesResult resultA2 =
      solver.solve(mesh, initial, flow.massFlux, fluid, speciesAPerturbed, boundariesA);
  ASSERT_EQ(resultA2.status, SpeciesStatus::Converged);
  ASSERT_EQ(resultB2.status, SpeciesStatus::Converged);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(resultB1.concentration[i], resultB2.concentration[i]) << "cell " << i;
  }
  // A's own result must have actually changed (diffusivity genuinely
  // drives the solution, not silently ignored).
  bool aChanged = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (std::abs(resultA1.concentration[i] - resultA2.concentration[i]) > 1e-6) {
      aChanged = true;
      break;
    }
  }
  EXPECT_TRUE(aChanged);
}
