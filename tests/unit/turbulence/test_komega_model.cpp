// P2-TURB-005 sections 5, 8-11, 30-32, 45: KOmegaModel itself -- the
// mandatory hand-derived mu_t test, field ownership/initialization,
// positivity/failure semantics, and limiting behavior. Mirrors
// test_kepsilon_model.cpp's own structure.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
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

BoundaryConditionSet makeScalarBoundaries(const Mesh& mesh, Real wallValue, Real inletValue) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(wallValue));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(wallValue));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(inletValue));
  return boundaries;
}

}  // namespace

// --- section 5/30: mu_t hand test (mandatory) -------------------------------

TEST(KOmegaModelTest, TurbulentViscosityMatchesHandDerivedExample) {
  // P2-TURB-005 section 5/30 (mandatory): rho=1.2, k=0.5, omega=4 ->
  // mu_t = 1.2*0.5/4 = 0.15, exactly, in every cell of a multi-cell mesh.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.2, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.5);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 4.0);

  KOmegaConfig config;
  config.initialK = 0.5;
  config.initialOmega = 4.0;
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  ASSERT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_DOUBLE_EQ(model.turbulentViscosity()[i], 0.15) << "cell " << i;
  }
}

// --- sections 8-9: identity/field ownership ---------------------------------

TEST(KOmegaModelTest, NameIsKOmega) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});
  EXPECT_EQ(model.name(), "kOmega");
}

TEST(KOmegaModelTest, FieldsSizedToMeshAtConstruction) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});

  EXPECT_EQ(model.k().size(), mesh.numberOfCells());
  EXPECT_EQ(model.omega().size(), mesh.numberOfCells());
  EXPECT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
}

TEST(KOmegaModelTest, InitializedToConfiguredValuesEverywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.05);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 8.0);
  KOmegaConfig config;
  config.initialK = 0.05;
  config.initialOmega = 8.0;
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(model.k()[i], 0.05) << "cell " << i;
    EXPECT_DOUBLE_EQ(model.omega()[i], 8.0) << "cell " << i;
  }
}

TEST(KOmegaModelTest, ConstructionIsDeterministicAcrossInstances) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const KOmegaModel a(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});
  const KOmegaModel b(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(a.k()[i], b.k()[i]);
    EXPECT_EQ(a.omega()[i], b.omega()[i]);
    EXPECT_EQ(a.turbulentViscosity()[i], b.turbulentViscosity()[i]);
  }
}

// --- section 10: initialization validation ----------------------------------

TEST(KOmegaModelTest, RejectsInvalidInitialK) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  for (const Real badK :
       {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    KOmegaConfig config;
    config.initialK = badK;
    EXPECT_THROW(
        (KOmegaModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config)),
        InvalidArgumentError)
        << "initialK = " << badK;
  }
}

TEST(KOmegaModelTest, RejectsInvalidInitialOmega) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  for (const Real badOmega :
       {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    KOmegaConfig config;
    config.initialOmega = badOmega;
    EXPECT_THROW(
        (KOmegaModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config)),
        InvalidArgumentError)
        << "initialOmega = " << badOmega;
  }
}

TEST(KOmegaModelTest, RejectsInvalidCoefficients) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  KOmegaConfig config;
  config.coefficients.betaStar = -0.09;
  EXPECT_THROW((KOmegaModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config)),
               InvalidArgumentError);
}

// --- section 31: limiting behavior ------------------------------------------

TEST(KOmegaModelTest, TurbulentViscosityVanishesAsKVanishes) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 1e-6);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  KOmegaConfig config;
  config.initialK = 1e-6;  // tiny, but above kFloor (1e-10).
  config.initialOmega = 10.0;
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const Real expected = 1.0 * 1e-6 / 10.0;
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_NEAR(model.turbulentViscosity()[i], expected, 1e-15) << "cell " << i;
    EXPECT_LT(model.turbulentViscosity()[i], 1e-6) << "cell " << i;
  }
}

// --- section 45: correct() failure/validation semantics ---------------------

TEST(KOmegaModelTest, CorrectRejectsMismatchedVelocitySize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});

  const VectorField wrongSizeVelocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(model.correct(mesh, wrongSizeVelocity, pressure), InvalidArgumentError);
}

TEST(KOmegaModelTest, CorrectProducesPositiveFiniteFieldsForARealFlow) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  KOmegaConfig config;
  config.initialK = 0.01;
  config.initialOmega = 10.0;
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector2{cell.centroid().y, 0.0};
  }
  const ScalarField pressure(mesh.numberOfCells(), 0.0);

  model.correct(mesh, velocity, pressure);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_TRUE(std::isfinite(model.k()[i])) << "cell " << i;
    EXPECT_GE(model.k()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.omega()[i])) << "cell " << i;
    EXPECT_GT(model.omega()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.turbulentViscosity()[i])) << "cell " << i;
    EXPECT_GE(model.turbulentViscosity()[i], 0.0) << "cell " << i;
  }
  ASSERT_TRUE(model.convergenceResidual().has_value());
  EXPECT_TRUE(std::isfinite(*model.convergenceResidual()));
  EXPECT_GE(*model.convergenceResidual(), 0.0);
}

TEST(KOmegaModelTest, ConvergenceResidualIsNulloptBeforeFirstCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});
  EXPECT_FALSE(model.convergenceResidual().has_value());
}

TEST(KOmegaModelTest, EffectiveViscosityEqualsMolecularPlusTurbulent) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.2, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.5);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 4.0);
  KOmegaConfig config;
  config.initialK = 0.5;
  config.initialOmega = 4.0;
  const KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const Real mu = 0.017;
  const ScalarField effective = model.effectiveViscosity(mu);
  for (Index i = 0; i < effective.size(); ++i) {
    EXPECT_DOUBLE_EQ(effective[i], mu + 0.15) << "cell " << i;
  }
}
