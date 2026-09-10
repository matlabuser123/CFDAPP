// P2-TURB-005 section 41 (extended by P2-TURB-006 section 50): cross-
// model plumbing check -- run the same geometry/case through SIMPLE with
// LaminarModel, KEpsilonModel, KOmegaModel, and SSTModel, and prove the
// solver actually selects a different model each time (distinct mu_t
// fields, distinct name() labels) rather than some aliasing where all
// four end up doing the same thing.
#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <utility>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/boundary/WallOmega.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/LaminarModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::boundary::WallOmega;
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
using cfd::turbulence::KOmegaConfig;
using cfd::turbulence::KOmegaModel;
using cfd::turbulence::LaminarModel;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;
using cfd::turbulence::TurbulenceModel;

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

BoundaryConditionSet makeWallKBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(0.02));
  return boundaries;
}

BoundaryConditionSet makeWallZeroGradientBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

BoundaryConditionSet makeWallOmegaBoundaries(const Mesh& mesh, Real kinematicViscosity,
                                             Real beta1) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<WallOmega>(kinematicViscosity, beta1));
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

struct RunOutcome {
  SIMPLEResult result;
  std::string_view modelName;
  ScalarField turbulentViscosity;
};

RunOutcome runWith(const Mesh& mesh, const FluidProperties& fluid,
                   const BoundaryConditionSet& velocityBoundaries,
                   const BoundaryConditionSet& pressureBoundaries, TurbulenceModel* model) {
  const SIMPLE simple(makeSettings(), 0, model);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                     initialVelocity, initialPressure);
  // model is non-null for laminar too here (an explicit LaminarModel),
  // so name()/turbulentViscosity() are always readable post-solve.
  return RunOutcome{std::move(result), model->name(), model->turbulentViscosity()};
}

bool anyDiffers(const ScalarField& a, const ScalarField& b) {
  for (Index i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return true;
  }
  return false;
}

}  // namespace

TEST(SIMPLETurbulenceModelsTest, LaminarKEpsilonKOmegaAndSSTProduceDistinctModelsAndMuT) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const auto kBoundaries = makeWallKBoundaries(mesh);
  const auto secondScalarBoundaries = makeWallZeroGradientBoundaries(mesh);
  const auto omegaBoundaries =
      makeWallOmegaBoundaries(mesh, fluid.kinematicViscosity(), SSTConfig{}.coefficients.beta1);

  LaminarModel laminar(mesh);
  KEpsilonConfig kEpsilonConfig;
  kEpsilonConfig.initialK = 0.01;
  kEpsilonConfig.initialEpsilon = 0.001;
  KEpsilonModel kEpsilon(mesh, fluid, velocityBoundaries, kBoundaries, secondScalarBoundaries,
                        kEpsilonConfig);
  KOmegaConfig kOmegaConfig;
  kOmegaConfig.initialK = 0.01;
  kOmegaConfig.initialOmega = 10.0;
  KOmegaModel kOmega(mesh, fluid, velocityBoundaries, kBoundaries, secondScalarBoundaries,
                     kOmegaConfig);
  SSTConfig sstConfig;
  sstConfig.initialK = 0.01;
  sstConfig.initialOmega = 10.0;
  SSTModel sst(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, sstConfig);

  const RunOutcome laminarRun =
      runWith(mesh, fluid, velocityBoundaries, pressureBoundaries, &laminar);
  const RunOutcome kEpsilonRun =
      runWith(mesh, fluid, velocityBoundaries, pressureBoundaries, &kEpsilon);
  const RunOutcome kOmegaRun =
      runWith(mesh, fluid, velocityBoundaries, pressureBoundaries, &kOmega);
  const RunOutcome sstRun = runWith(mesh, fluid, velocityBoundaries, pressureBoundaries, &sst);

  ASSERT_EQ(laminarRun.result.status, SIMPLEStatus::Converged);
  ASSERT_EQ(kEpsilonRun.result.status, SIMPLEStatus::Converged);
  ASSERT_EQ(kOmegaRun.result.status, SIMPLEStatus::Converged);
  ASSERT_EQ(sstRun.result.status, SIMPLEStatus::Converged);

  // No model-selection aliasing: each solver run's own model correctly
  // reports its own identity.
  EXPECT_EQ(laminarRun.modelName, "laminar");
  EXPECT_EQ(kEpsilonRun.modelName, "kEpsilon");
  EXPECT_EQ(kOmegaRun.modelName, "kOmega");
  EXPECT_EQ(sstRun.modelName, "SST");

  // Laminar mu_t is exactly zero everywhere; the three real models are
  // not (they all have a real turbulence source on the lid patch).
  for (Index i = 0; i < laminarRun.turbulentViscosity.size(); ++i) {
    EXPECT_DOUBLE_EQ(laminarRun.turbulentViscosity[i], 0.0) << "cell " << i;
  }
  bool kEpsilonHasNonzeroMuT = false;
  bool kOmegaHasNonzeroMuT = false;
  bool sstHasNonzeroMuT = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (kEpsilonRun.turbulentViscosity[i] > 0.0) kEpsilonHasNonzeroMuT = true;
    if (kOmegaRun.turbulentViscosity[i] > 0.0) kOmegaHasNonzeroMuT = true;
    if (sstRun.turbulentViscosity[i] > 0.0) sstHasNonzeroMuT = true;
  }
  EXPECT_TRUE(kEpsilonHasNonzeroMuT);
  EXPECT_TRUE(kOmegaHasNonzeroMuT);
  EXPECT_TRUE(sstHasNonzeroMuT);

  // Every pair of the three real turbulence models solves genuinely
  // different transport equations (different constants/formulas), so on
  // the same case they must not happen to produce identical mu_t fields
  // cell-by-cell.
  EXPECT_TRUE(anyDiffers(kEpsilonRun.turbulentViscosity, kOmegaRun.turbulentViscosity));
  EXPECT_TRUE(anyDiffers(kEpsilonRun.turbulentViscosity, sstRun.turbulentViscosity));
  EXPECT_TRUE(anyDiffers(kOmegaRun.turbulentViscosity, sstRun.turbulentViscosity));

  // And all three differ from the laminar (all-zero) field.
  EXPECT_TRUE(anyDiffers(kEpsilonRun.turbulentViscosity, laminarRun.turbulentViscosity));
  EXPECT_TRUE(anyDiffers(kOmegaRun.turbulentViscosity, laminarRun.turbulentViscosity));
  EXPECT_TRUE(anyDiffers(sstRun.turbulentViscosity, laminarRun.turbulentViscosity));
}
