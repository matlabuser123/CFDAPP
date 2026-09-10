// P2-TURB-004 section 27 (extended by P2-TURB-005 section 28 and
// P2-TURB-006 section 36): createTurbulenceModel -- "laminar" ->
// LaminarModel (any overload), "k_epsilon" -> KEpsilonModel
// (KEpsilonConfig overload), "k_omega" -> KOmegaModel (KOmegaConfig
// overload), "sst" -> SSTModel (SSTConfig overload); the "wrong" model
// name for a given overload (e.g. "k_omega" through the KEpsilonConfig
// overload) fails explicitly rather than silently redirecting to another
// overload.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/LaminarModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/TurbulenceModelFactory.hpp"

using cfd::InvalidArgumentError;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::turbulence::createTurbulenceModel;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;
using cfd::turbulence::KOmegaConfig;
using cfd::turbulence::KOmegaModel;
using cfd::turbulence::LaminarModel;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;

namespace {

BoundaryConditionSet makeVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeScalarBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

// --- KEpsilonConfig overload -------------------------------------------

TEST(TurbulenceModelFactoryTest, LaminarNameCreatesLaminarModelViaKEpsilonOverload) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);

  const auto model = createTurbulenceModel("laminar", mesh, fluid, velocityBoundaries,
                                           scalarBoundaries, scalarBoundaries, KEpsilonConfig{});
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name(), "laminar");
  EXPECT_NE(dynamic_cast<LaminarModel*>(model.get()), nullptr);
}

TEST(TurbulenceModelFactoryTest, KEpsilonNameCreatesKEpsilonModel) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);
  KEpsilonConfig config;
  config.initialK = 0.02;
  config.initialEpsilon = 0.005;

  const auto model = createTurbulenceModel("k_epsilon", mesh, fluid, velocityBoundaries,
                                           scalarBoundaries, scalarBoundaries, config);
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name(), "kEpsilon");
  auto* kEpsilon = dynamic_cast<KEpsilonModel*>(model.get());
  ASSERT_NE(kEpsilon, nullptr);
  for (cfd::Index i = 0; i < kEpsilon->k().size(); ++i) {
    EXPECT_DOUBLE_EQ(kEpsilon->k()[i], 0.02);
    EXPECT_DOUBLE_EQ(kEpsilon->epsilon()[i], 0.005);
  }
}

TEST(TurbulenceModelFactoryTest, KEpsilonOverloadRejectsUnsupportedModelNames) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);

  // "k_omega" included deliberately: this overload only knows how to
  // build a KEpsilonConfig-shaped model, so requesting k_omega through
  // it must fail, not silently redirect to the other overload.
  for (const char* name : {"k_omega", "sst", "not_a_model", ""}) {
    EXPECT_THROW((void)createTurbulenceModel(name, mesh, fluid, velocityBoundaries,
                                             scalarBoundaries, scalarBoundaries, KEpsilonConfig{}),
                 InvalidArgumentError)
        << "model name = \"" << name << "\"";
  }
}

// --- KOmegaConfig overload (P2-TURB-005) --------------------------------

TEST(TurbulenceModelFactoryTest, LaminarNameCreatesLaminarModelViaKOmegaOverload) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);

  const auto model = createTurbulenceModel("laminar", mesh, fluid, velocityBoundaries,
                                           scalarBoundaries, scalarBoundaries, KOmegaConfig{});
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name(), "laminar");
  EXPECT_NE(dynamic_cast<LaminarModel*>(model.get()), nullptr);
}

TEST(TurbulenceModelFactoryTest, KOmegaNameCreatesKOmegaModel) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);
  KOmegaConfig config;
  config.initialK = 0.02;
  config.initialOmega = 12.0;

  const auto model = createTurbulenceModel("k_omega", mesh, fluid, velocityBoundaries,
                                           scalarBoundaries, scalarBoundaries, config);
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name(), "kOmega");
  auto* kOmega = dynamic_cast<KOmegaModel*>(model.get());
  ASSERT_NE(kOmega, nullptr);
  for (cfd::Index i = 0; i < kOmega->k().size(); ++i) {
    EXPECT_DOUBLE_EQ(kOmega->k()[i], 0.02);
    EXPECT_DOUBLE_EQ(kOmega->omega()[i], 12.0);
  }
}

TEST(TurbulenceModelFactoryTest, KOmegaOverloadRejectsUnsupportedModelNames) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);

  // "k_epsilon" included deliberately: this overload only knows how to
  // build a KOmegaConfig-shaped model.
  for (const char* name : {"k_epsilon", "sst", "not_a_model", ""}) {
    EXPECT_THROW((void)createTurbulenceModel(name, mesh, fluid, velocityBoundaries,
                                             scalarBoundaries, scalarBoundaries, KOmegaConfig{}),
                 InvalidArgumentError)
        << "model name = \"" << name << "\"";
  }
}

// --- SSTConfig overload (P2-TURB-006) -----------------------------------

TEST(TurbulenceModelFactoryTest, LaminarNameCreatesLaminarModelViaSSTOverload) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);

  const auto model = createTurbulenceModel("laminar", mesh, fluid, velocityBoundaries,
                                           scalarBoundaries, scalarBoundaries, SSTConfig{});
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name(), "laminar");
  EXPECT_NE(dynamic_cast<LaminarModel*>(model.get()), nullptr);
}

TEST(TurbulenceModelFactoryTest, SSTNameCreatesSSTModel) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);  // includes Wall/MovingWall.
  const auto scalarBoundaries = makeScalarBoundaries(mesh);
  SSTConfig config;
  config.initialK = 0.02;
  config.initialOmega = 12.0;

  const auto model = createTurbulenceModel("sst", mesh, fluid, velocityBoundaries, scalarBoundaries,
                                           scalarBoundaries, config);
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name(), "SST");
  auto* sst = dynamic_cast<SSTModel*>(model.get());
  ASSERT_NE(sst, nullptr);
  for (cfd::Index i = 0; i < sst->k().size(); ++i) {
    EXPECT_DOUBLE_EQ(sst->k()[i], 0.02);
    EXPECT_DOUBLE_EQ(sst->omega()[i], 12.0);
  }
}

TEST(TurbulenceModelFactoryTest, SSTOverloadRejectsUnsupportedModelNames) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto scalarBoundaries = makeScalarBoundaries(mesh);

  // "k_epsilon"/"k_omega" included deliberately: this overload only
  // knows how to build an SSTConfig-shaped model.
  for (const char* name : {"k_epsilon", "k_omega", "not_a_model", ""}) {
    EXPECT_THROW((void)createTurbulenceModel(name, mesh, fluid, velocityBoundaries,
                                             scalarBoundaries, scalarBoundaries, SSTConfig{}),
                 InvalidArgumentError)
        << "model name = \"" << name << "\"";
  }
}
