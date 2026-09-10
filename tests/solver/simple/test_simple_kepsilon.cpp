// P2-TURB-004 sections 20, 23-24, 36, 38, 40: the end-to-end solver-
// integration proof that k -> mu_t -> mu_eff -> momentum -> velocity ->
// P_k -> k/epsilon is actually one connected loop through SIMPLE, plus
// the mandated k-epsilon sanity case (not a published-benchmark
// validation -- that is a later task) and its determinism.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::MovingWall;
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
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;

namespace {

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// All four cavity patches are walls -- no inlet/outlet -- so k = 0
// (FixedValue) on three of them is the simplified wall treatment
// (P2-TURB-004 sections 18-19). The moving-lid patch ("top") instead
// gets a small fixed k, standing in for the ambient turbulence a real
// inlet/moving boundary would carry -- purely so this *test* has a
// persistent turbulence source to actually exercise the k -> mu_t ->
// mu_eff -> velocity -> P_k -> k feedback loop with (a cavity where
// every wall pins k to exactly zero and nothing else supplies any decays
// uninterestingly straight to the numerical floor, which would still
// pass but would not meaningfully demonstrate the loop is connected).
// This is a test-only scenario, not this task's standard case-derived
// wall mapping (CaseBuilder.cpp maps every velocity "wall"/"moving_wall"
// patch to k=0 uniformly -- see
// TurbulenceCaseTest.BuilderConstructsKEpsilonConfigAndBoundaries in
// tests/unit/io/test_turbulence_case.cpp for that).
BoundaryConditionSet makeWallKBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(0.02));
  return boundaries;
}

BoundaryConditionSet makeWallEpsilonBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

SIMPLESettings makeSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 1000;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.turbulenceTolerance = 1e-6;
  return settings;
}

}  // namespace

TEST(SIMPLEKEpsilonTest, EndToEndLoopIsConnectedAndProducesPhysicalFields) {
  // P2-TURB-004 section 36: proves the whole
  // k/epsilon -> mu_t -> mu_eff -> momentum -> velocity -> P_k ->
  // k/epsilon loop is genuinely wired together through SIMPLE, not just
  // independently-tested pieces -- run against a real (small) lid-driven
  // cavity and inspect the KEpsilonModel's own fields afterward (SIMPLE
  // does not own or copy them, so the same object reflects the solver's
  // last correct() call exactly).
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto epsilonBoundaries = makeWallEpsilonBoundaries(mesh);

  KEpsilonConfig config;
  config.initialK = 0.01;
  config.initialEpsilon = 0.001;
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config);

  const SIMPLE simple(makeSettings(), 0, &model);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  // P2-TURB-004 section 38 (sanity, not benchmark validation): no
  // solver failure, everything finite.
  ASSERT_NE(result.status, SIMPLEStatus::InvalidConfiguration);
  ASSERT_NE(result.status, SIMPLEStatus::MomentumFailure);
  ASSERT_NE(result.status, SIMPLEStatus::PressureCorrectionFailure);
  ASSERT_NE(result.status, SIMPLEStatus::NonFiniteState);
  for (Index i = 0; i < result.velocity.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.velocity[i].x)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.velocity[i].y)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.pressure[i])) << "cell " << i;
  }

  // The loop actually ran and fed back: k/epsilon/mu_t must have moved
  // away from the uniform initial guess (if momentum's mu_eff never
  // reached the model, or the model's correct() never used the solved
  // velocity, these would still be exactly config.initialK/
  // initialEpsilon in every cell).
  bool anyKChanged = false;
  bool anyMuTNonzero = false;
  for (Index i = 0; i < model.k().size(); ++i) {
    EXPECT_TRUE(std::isfinite(model.k()[i])) << "cell " << i;
    EXPECT_GE(model.k()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.epsilon()[i])) << "cell " << i;
    EXPECT_GT(model.epsilon()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.turbulentViscosity()[i])) << "cell " << i;
    EXPECT_GE(model.turbulentViscosity()[i], 0.0) << "cell " << i;
    if (model.k()[i] != config.initialK) anyKChanged = true;
    if (model.turbulentViscosity()[i] > 0.0) anyMuTNonzero = true;
  }
  EXPECT_TRUE(anyKChanged);
  EXPECT_TRUE(anyMuTNonzero);

  // Mass conservation: the converged/final face flux must be continuity-
  // consistent (same evaluateContinuity gate every other SIMPLE test
  // uses, via SIMPLEResult's own reported residual).
  EXPECT_LT(result.finalContinuityResidual, 1e-3);
  EXPECT_LT(result.globalMassImbalance, 1e-3);

  // Residual reduction: the first recorded iteration's residual must
  // exceed the last (the solve made genuine progress, not immediate
  // stagnation).
  ASSERT_FALSE(result.uResidualHistory.empty());
  if (result.uResidualHistory.size() > 1) {
    EXPECT_LT(result.uResidualHistory.back(), result.uResidualHistory.front());
  }
}

TEST(SIMPLEKEpsilonTest, RepeatedSolveIsBitIdentical) {
  // P2-TURB-004 section 40.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto epsilonBoundaries = makeWallEpsilonBoundaries(mesh);
  KEpsilonConfig config;
  config.initialK = 0.01;
  config.initialEpsilon = 0.001;

  auto runOnce = [&]() {
    KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config);
    const SIMPLE simple(makeSettings(), 0, &model);
    const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
    const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
    SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                       initialVelocity, initialPressure);
    return std::make_pair(std::move(result), std::move(model));
  };

  auto [resultA, modelA] = runOnce();
  auto [resultB, modelB] = runOnce();

  EXPECT_EQ(resultA.status, resultB.status);
  EXPECT_EQ(resultA.iterations, resultB.iterations);
  ASSERT_EQ(resultA.velocity.size(), resultB.velocity.size());
  for (Index i = 0; i < resultA.velocity.size(); ++i) {
    EXPECT_EQ(resultA.velocity[i].x, resultB.velocity[i].x);
    EXPECT_EQ(resultA.velocity[i].y, resultB.velocity[i].y);
    EXPECT_EQ(resultA.pressure[i], resultB.pressure[i]);
    EXPECT_EQ(modelA.k()[i], modelB.k()[i]);
    EXPECT_EQ(modelA.epsilon()[i], modelB.epsilon()[i]);
    EXPECT_EQ(modelA.turbulentViscosity()[i], modelB.turbulentViscosity()[i]);
  }
}

TEST(SIMPLEKEpsilonTest, ConvergedStatusImpliesTurbulenceResidualWithinTolerance) {
  // P2-TURB-004 section 24: whenever SIMPLE reports Converged with an
  // active KEpsilonModel, the reported turbulence residual (if any) must
  // itself be within SIMPLESettings::turbulenceTolerance -- U/V/P alone
  // settling is not sufficient.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto epsilonBoundaries = makeWallEpsilonBoundaries(mesh);
  KEpsilonConfig config;
  config.initialK = 0.01;
  config.initialEpsilon = 0.001;
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config);

  SIMPLESettings settings = makeSettings();
  settings.maxIterations = 2000;
  const SIMPLE simple(settings, 0, &model);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  if (result.status == SIMPLEStatus::Converged) {
    ASSERT_TRUE(result.finalTurbulenceResidual.has_value());
    EXPECT_LE(*result.finalTurbulenceResidual, settings.turbulenceTolerance);
  }
}
