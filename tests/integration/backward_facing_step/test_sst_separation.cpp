// P16 -- the SST-DISCRIMINATING benchmark.
//
// WHY A CHANNEL IS NOT ENOUGH
// ---------------------------
// The channel sweeps show SST reproducing the log law to 0.03% in Re_tau. So
// does k-epsilon. So, very likely, would an SST with its blending flattened,
// its cross diffusion removed, or its eddy-viscosity limiter disabled -- an
// equilibrium boundary layer is exactly the flow where those mechanisms have
// nothing to do. Closing SST on channel agreement alone would certify a model
// whose SST-specific half could be entirely broken.
//
// A backward-facing step is the standard case where they matter: the shear
// layer separates, reattaches under an adverse pressure gradient, and the
// eddy-viscosity limiter is precisely what stops k-epsilon-like models
// over-producing mu_t in the recirculation and predicting reattachment far too
// early.
//
// WHAT THIS FILE ASSERTS
// ----------------------
//   1. SST and k-epsilon predict MATERIALLY DIFFERENT reattachment lengths --
//      if they agreed, the benchmark would not discriminate and nothing below
//      would mean anything;
//   2. breaking each SST mechanism MOVES the reattachment length -- so the
//      benchmark can detect the failure, which is the property the channel
//      lacks.
//
// The controls are applied through the PUBLIC SSTCoefficients, not by patching
// production source: a1 disables the limiter, sigmaOmega2 = 0 removes cross
// diffusion, and making the two coefficient sets identical makes F1's blending
// inert. Each is a configuration a user could legally write, which is what
// makes the detection meaningful rather than an artefact of a source hack.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BackwardFacingStepCase.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;

namespace {

using namespace cfd::validation::step;

// Turbulent, not the laminar Re = 800 the existing step tests use. The classic
// turbulent BFS experiments sit at Re_h of a few thousand upward; this is well
// into the turbulent regime while staying a size this harness solves quickly.
constexpr Real kReynolds = 5000.0;
constexpr Index kCellsPerHeight = 8;
constexpr Real kStepLength = 10.0;
constexpr Real kInletTurbulenceIntensity = 0.05;
constexpr Real kCmu = 0.09;

Real inletK() {
  // The inlet is normalised to a unit mean velocity by this harness.
  const Real fluct = kInletTurbulenceIntensity * 1.0;
  return 1.5 * fluct * fluct;
}
Real inletEpsilon() {
  const Real mixingLength = 0.07 * 1.0;  // 0.07 * step height
  return std::pow(kCmu, 0.75) * std::pow(inletK(), 1.5) / mixingLength;
}
Real inletOmega() { return inletEpsilon() / (kCmu * inletK()); }

// k/omega (or k/epsilon) boundary sets, derived from the velocity patch types
// the same way CaseBuilder derives them: walls zero-gradient (the wall-function
// arrangement), inlets fixed, outlet zero-gradient.
BoundaryConditionSet makeScalarBoundaries(const cfd::mesh::Mesh& mesh,
                                          const BoundaryConditionSet& velocity, Real inletValue) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& bc = velocity.get(patch.name());
    const bool isWall = bc.type() == cfd::boundary::BoundaryConditionType::Wall ||
                        bc.type() == cfd::boundary::BoundaryConditionType::MovingWall;
    const bool isInlet = bc.type() == cfd::boundary::BoundaryConditionType::Inlet;
    if (isWall) {
      // Zero-gradient: the wall function imposes the wall-cell value itself.
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (isInlet) {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(inletValue));
    } else {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }
  return boundaries;
}

SIMPLESettings makeSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 2000;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-5;
  settings.pressureTolerance = 5e-4;
  settings.continuityTolerance = 1e-6;
  settings.turbulenceTolerance = 1e-5;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-9;
  settings.pressureSolver.relativeTolerance = 1e-7;
  return settings;
}

struct StepOutcome {
  bool converged{};
  Index iterations{};
  std::optional<Real> reattachment;  // x_r / h
  Real maximumTurbulentViscosity{};
};

SSTConfig baseSSTConfig() {
  SSTConfig config;
  config.initialK = inletK();
  config.initialOmega = inletOmega();
  config.wallFunctions = true;
  config.wallFunctionCoefficients.cMu = config.coefficients.betaStar;
  return config;
}

// Everything one step run needs, kept alive together: the model holds
// references to its boundary sets, so the sets must outlive it.
struct ModelBundle {
  BoundaryConditionSet first;
  BoundaryConditionSet second;
  std::unique_ptr<cfd::turbulence::TurbulenceModel> model;
};

// Runs the step once and reads off the reattachment length with the harness's
// own algorithmic detector -- no visual estimate.
StepOutcome runStepWith(
    const std::function<ModelBundle(const cfd::mesh::Mesh&, const FluidProperties&,
                                    const BoundaryConditionSet&)>& build) {
  const StepSpec spec{kReynolds, kCellsPerHeight, kStepLength,
                      cfd::discretization::ConvectionScheme::Upwind};
  const auto mesh = makeStepMesh(spec);
  const auto boundaries = makeStepBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDensity * 1.0 * 1.0 / kReynolds);

  ModelBundle bundle = build(mesh, fluid, boundaries.velocity);
  const SIMPLE simple(makeSettings(), /*referenceCell=*/0, bundle.model.get());
  const VectorField initialVelocity(mesh.numberOfCells(), cfd::Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult result = simple.solve(mesh, fluid, boundaries.velocity, boundaries.pressure,
                                           initialVelocity, initialPressure);

  StepOutcome outcome;
  outcome.converged = result.converged();
  outcome.iterations = result.iterations;
  const Index nx = static_cast<Index>(kCellsPerHeight * kStepLength);
  const auto shear =
      wallShear(mesh, nx, kCellsPerHeight, result.velocity, fluid.dynamicViscosity());
  outcome.reattachment = separationTopology(shear).reattachment;
  const auto& muT = bundle.model->turbulentViscosity();
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    outcome.maximumTurbulentViscosity = std::max(outcome.maximumTurbulentViscosity, muT[i]);
  }
  return outcome;
}

StepOutcome runSST(const SSTConfig& config) {
  return runStepWith([&config](const cfd::mesh::Mesh& mesh, const FluidProperties& fluid,
                               const BoundaryConditionSet& velocity) {
    ModelBundle bundle{makeScalarBoundaries(mesh, velocity, inletK()),
                       makeScalarBoundaries(mesh, velocity, inletOmega()), nullptr};
    bundle.model =
        std::make_unique<SSTModel>(mesh, fluid, velocity, bundle.first, bundle.second, config);
    return bundle;
  });
}

StepOutcome runKEpsilon() {
  return runStepWith([](const cfd::mesh::Mesh& mesh, const FluidProperties& fluid,
                        const BoundaryConditionSet& velocity) {
    KEpsilonConfig config;
    config.initialK = inletK();
    config.initialEpsilon = inletEpsilon();
    config.wallFunctions = true;
    ModelBundle bundle{makeScalarBoundaries(mesh, velocity, inletK()),
                       makeScalarBoundaries(mesh, velocity, inletEpsilon()), nullptr};
    bundle.model =
        std::make_unique<KEpsilonModel>(mesh, fluid, velocity, bundle.first, bundle.second, config);
    return bundle;
  });
}

// The unperturbed SST run, computed ONCE. Every control compares against it,
// and re-solving it per control would multiply this benchmark's cost by four
// for no extra evidence.
const StepOutcome& baselineSST() {
  static const StepOutcome outcome = runSST(baseSSTConfig());
  return outcome;
}

void report(const char* label, const StepOutcome& outcome) {
  std::printf("%-28s converged=%d iters=%4zu  x_r/h=%s  max mu_t=%.6e\n", label,
              outcome.converged ? 1 : 0, outcome.iterations,
              outcome.reattachment ? std::to_string(*outcome.reattachment).c_str() : "none",
              outcome.maximumTurbulentViscosity);
}

}  // namespace

TEST(SSTSeparation, SSTAndKEpsilonPredictDifferentReattachment) {
  // If the two models agreed here, this benchmark would not discriminate and
  // every control below would be meaningless. Asserted FIRST for that reason.
  const StepOutcome& sst = baselineSST();
  const StepOutcome kEpsilon = runKEpsilon();
  report("SST", sst);
  report("k-epsilon", kEpsilon);

  ASSERT_TRUE(sst.reattachment.has_value()) << "SST produced no reattachment point";
  ASSERT_TRUE(kEpsilon.reattachment.has_value()) << "k-epsilon produced no reattachment point";

  const Real difference = std::abs(*sst.reattachment - *kEpsilon.reattachment);
  std::printf("reattachment difference: %.4f step heights\n", difference);
  EXPECT_GT(difference, 0.1)
      << "SST and k-epsilon reattach at the same place (" << *sst.reattachment << " vs "
      << *kEpsilon.reattachment
      << ") -- this case does not discriminate between them, so it cannot validate SST";
}

TEST(SSTSeparation, DisablingTheEddyViscosityLimiterMovesReattachment) {
  // The limiter is SST's headline advantage in separated flow: it caps mu_t
  // where S*F2 > a1*omega, which is exactly the shear layer. Raising a1 makes
  // max(a1 omega, S F2) always pick a1 omega, so the limiter can never bind --
  // the model degrades toward plain k-omega.
  SSTConfig broken = baseSSTConfig();
  broken.coefficients.a1 = 1.0e6;

  const StepOutcome& correct = baselineSST();
  const StepOutcome disabled = runSST(broken);
  report("SST (limiter on)", correct);
  report("SST (limiter disabled)", disabled);

  ASSERT_TRUE(correct.reattachment.has_value());
  ASSERT_TRUE(disabled.reattachment.has_value())
      << "the control did not even produce a reattachment point, so nothing is being compared";
  EXPECT_NE(*correct.reattachment, *disabled.reattachment)
      << "disabling the eddy-viscosity limiter changed nothing -- this benchmark cannot detect "
         "its failure, which is the one thing it exists to detect";
  // And the mechanism, not just the outcome: without the limiter mu_t must be
  // larger somewhere, because the limiter only ever reduces it.
  EXPECT_GT(disabled.maximumTurbulentViscosity, correct.maximumTurbulentViscosity)
      << "the limiter is supposed to CAP mu_t; removing it did not raise the maximum";
}

TEST(SSTSeparation, RemovingCrossDiffusionMovesReattachment) {
  // D_omega = 2(1-F1) rho sigma_omega2 grad(k).grad(omega)/omega, so
  // sigma_omega2 = 0 removes it. (It also removes sigma_omega2 from F1's third
  // argument term and from the outer sigma_omega -- stated rather than hidden;
  // the point is that a model without cross diffusion behaves differently, not
  // that exactly one term changed.)
  SSTConfig broken = baseSSTConfig();
  broken.coefficients.sigmaOmega2 = 1e-12;  // effectively zero, still > 0 for validation

  const StepOutcome& correct = baselineSST();
  const StepOutcome without = runSST(broken);
  report("SST (cross diffusion)", correct);
  report("SST (no cross diffusion)", without);

  ASSERT_TRUE(correct.reattachment.has_value());
  ASSERT_TRUE(without.reattachment.has_value());
  EXPECT_NE(*correct.reattachment, *without.reattachment)
      << "removing cross diffusion changed nothing detectable";
}

TEST(SSTSeparation, FlatteningTheBlendingMovesReattachment) {
  // F1 blends between the inner (k-omega) and outer (k-epsilon) coefficient
  // sets. Making the two sets IDENTICAL leaves F1 computed but inert -- the
  // model becomes a single-set k-omega. If that changes nothing, the blending
  // is doing nothing in this flow and the benchmark cannot police it.
  SSTConfig broken = baseSSTConfig();
  broken.coefficients.sigmaK2 = broken.coefficients.sigmaK1;
  broken.coefficients.sigmaOmega2 = broken.coefficients.sigmaOmega1;
  broken.coefficients.beta2 = broken.coefficients.beta1;

  const StepOutcome& correct = baselineSST();
  const StepOutcome flattened = runSST(broken);
  report("SST (blended)", correct);
  report("SST (blending inert)", flattened);

  ASSERT_TRUE(correct.reattachment.has_value());
  ASSERT_TRUE(flattened.reattachment.has_value());
  EXPECT_NE(*correct.reattachment, *flattened.reattachment)
      << "collapsing the two coefficient sets changed nothing -- F1's blending is inert in this "
         "flow, so this benchmark cannot detect its failure";
}

TEST(SSTSeparation, IsDeterministic) {
  const StepOutcome first = runSST(baseSSTConfig());
  const StepOutcome second = runSST(baseSSTConfig());
  EXPECT_EQ(first.iterations, second.iterations);
  ASSERT_EQ(first.reattachment.has_value(), second.reattachment.has_value());
  if (first.reattachment) {
    EXPECT_EQ(*first.reattachment, *second.reattachment);
  }
  EXPECT_EQ(first.maximumTurbulentViscosity, second.maximumTurbulentViscosity);
}
