// P2-TURB-005 sections 22-24, 36 (via analogy), 40: the end-to-end
// solver-integration proof that k -> mu_t -> mu_eff -> momentum ->
// velocity -> P_k -> k/omega is one connected loop through SIMPLE, plus
// the mandated k-omega sanity case (not a published-benchmark validation
// -- that is a later task) and its determinism. Mirrors
// test_simple_kepsilon.cpp's own structure closely.
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
#include "cfd/turbulence/KOmegaModel.hpp"

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
using cfd::turbulence::KOmegaConfig;
using cfd::turbulence::KOmegaModel;

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

// Same test-only scenario as test_simple_kepsilon.cpp's own
// makeWallKBoundaries: three walls pin k=0, the moving-lid patch instead
// gets a small fixed k standing in for ambient turbulence, so this test
// has a persistent turbulence source to actually exercise the feedback
// loop (a cavity where every wall pins k to exactly zero decays
// uninterestingly straight to the numerical floor). Not this task's
// standard case-derived wall mapping (CaseBuilder.cpp maps every
// velocity "wall"/"moving_wall" patch to k=0 uniformly -- see
// TurbulenceCaseTest.BuilderConstructsKOmegaConfigAndBoundaries in
// tests/unit/io/test_turbulence_case.cpp for that).
BoundaryConditionSet makeWallKBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(0.02));
  return boundaries;
}

BoundaryConditionSet makeWallOmegaBoundaries(const Mesh& mesh) {
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

TEST(SIMPLEKOmegaTest, EndToEndLoopIsConnectedAndProducesPhysicalFields) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto omegaBoundaries = makeWallOmegaBoundaries(mesh);

  KOmegaConfig config;
  config.initialK = 0.01;
  config.initialOmega = 10.0;
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const SIMPLE simple(makeSettings(), 0, &model);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  ASSERT_NE(result.status, SIMPLEStatus::InvalidConfiguration);
  ASSERT_NE(result.status, SIMPLEStatus::MomentumFailure);
  ASSERT_NE(result.status, SIMPLEStatus::PressureCorrectionFailure);
  ASSERT_NE(result.status, SIMPLEStatus::NonFiniteState);
  for (Index i = 0; i < result.velocity.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.velocity[i].x)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.velocity[i].y)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.pressure[i])) << "cell " << i;
  }

  bool anyKChanged = false;
  bool anyMuTNonzero = false;
  for (Index i = 0; i < model.k().size(); ++i) {
    EXPECT_TRUE(std::isfinite(model.k()[i])) << "cell " << i;
    EXPECT_GE(model.k()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.omega()[i])) << "cell " << i;
    EXPECT_GT(model.omega()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.turbulentViscosity()[i])) << "cell " << i;
    EXPECT_GE(model.turbulentViscosity()[i], 0.0) << "cell " << i;
    if (model.k()[i] != config.initialK) anyKChanged = true;
    if (model.turbulentViscosity()[i] > 0.0) anyMuTNonzero = true;
  }
  EXPECT_TRUE(anyKChanged);
  EXPECT_TRUE(anyMuTNonzero);

  EXPECT_LT(result.finalContinuityResidual, 1e-3);
  EXPECT_LT(result.globalMassImbalance, 1e-3);

  ASSERT_FALSE(result.uResidualHistory.empty());
  if (result.uResidualHistory.size() > 1) {
    EXPECT_LT(result.uResidualHistory.back(), result.uResidualHistory.front());
  }
}

TEST(SIMPLEKOmegaTest, RepeatedSolveIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto omegaBoundaries = makeWallOmegaBoundaries(mesh);
  KOmegaConfig config;
  config.initialK = 0.01;
  config.initialOmega = 10.0;

  auto runOnce = [&]() {
    KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);
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
    EXPECT_EQ(modelA.omega()[i], modelB.omega()[i]);
    EXPECT_EQ(modelA.turbulentViscosity()[i], modelB.turbulentViscosity()[i]);
  }
}

TEST(SIMPLEKOmegaTest, ConvergedStatusImpliesTurbulenceResidualWithinTolerance) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto omegaBoundaries = makeWallOmegaBoundaries(mesh);
  KOmegaConfig config;
  config.initialK = 0.01;
  config.initialOmega = 10.0;
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

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
