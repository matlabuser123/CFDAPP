// P2-TURB-004 sections 7-12, 25, 29, 39: KEpsilonModel itself -- the
// mandatory hand-derived mu_t test, field ownership/initialization,
// positivity/failure semantics, and limiting behavior.
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
#include "cfd/turbulence/KEpsilonModel.hpp"

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

BoundaryConditionSet makeScalarBoundaries(const Mesh& mesh, Real wallValue, Real inletValue) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(wallValue));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(wallValue));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(inletValue));
  return boundaries;
}

}  // namespace

// --- section 29: mu_t hand test (mandatory) ---------------------------------

TEST(KEpsilonModelTest, TurbulentViscosityMatchesHandDerivedExample) {
  // P2-TURB-004 section 29 (mandatory): rho=1.2, k=2, epsilon=0.5 ->
  // mu_t = 1.2*0.09*4/0.5 = 0.864, exactly, in every cell (checked on a
  // multi-cell mesh, not just one).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.2, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 2.0);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.5);

  KEpsilonConfig config;
  config.initialK = 2.0;
  config.initialEpsilon = 0.5;
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries,
                            config);

  ASSERT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_DOUBLE_EQ(model.turbulentViscosity()[i], 0.864) << "cell " << i;
  }
}

// --- sections 7-9: identity/field ownership ---------------------------------

TEST(KEpsilonModelTest, NameIsKEpsilon) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, {});
  EXPECT_EQ(model.name(), "kEpsilon");
}

TEST(KEpsilonModelTest, FieldsSizedToMeshAtConstruction) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, {});

  EXPECT_EQ(model.k().size(), mesh.numberOfCells());
  EXPECT_EQ(model.epsilon().size(), mesh.numberOfCells());
  EXPECT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
}

TEST(KEpsilonModelTest, InitializedToConfiguredValuesEverywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.05);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.02);
  KEpsilonConfig config;
  config.initialK = 0.05;
  config.initialEpsilon = 0.02;
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries,
                            config);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(model.k()[i], 0.05) << "cell " << i;
    EXPECT_DOUBLE_EQ(model.epsilon()[i], 0.02) << "cell " << i;
  }
}

TEST(KEpsilonModelTest, ConstructionIsDeterministicAcrossInstances) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);
  const KEpsilonModel a(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, {});
  const KEpsilonModel b(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, {});

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(a.k()[i], b.k()[i]);
    EXPECT_EQ(a.epsilon()[i], b.epsilon()[i]);
    EXPECT_EQ(a.turbulentViscosity()[i], b.turbulentViscosity()[i]);
  }
}

// --- section 10: initialization validation ----------------------------------

TEST(KEpsilonModelTest, RejectsInvalidInitialK) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);

  for (const Real badK :
       {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    KEpsilonConfig config;
    config.initialK = badK;
    EXPECT_THROW(
        (KEpsilonModel(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config)),
        InvalidArgumentError)
        << "initialK = " << badK;
  }
}

TEST(KEpsilonModelTest, RejectsInvalidInitialEpsilon) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);

  for (const Real badEpsilon :
       {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    KEpsilonConfig config;
    config.initialEpsilon = badEpsilon;
    EXPECT_THROW(
        (KEpsilonModel(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config)),
        InvalidArgumentError)
        << "initialEpsilon = " << badEpsilon;
  }
}

TEST(KEpsilonModelTest, RejectsInvalidCoefficients) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);

  KEpsilonConfig config;
  config.coefficients.cMu = -0.09;
  EXPECT_THROW(
      (KEpsilonModel(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config)),
      InvalidArgumentError);
}

// --- section 39: limiting behavior ------------------------------------------

TEST(KEpsilonModelTest, TurbulentViscosityVanishesAsKVanishes) {
  // As k -> 0 (epsilon finite), mu_t = rho*Cmu*k^2/epsilon -> 0.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 1e-6);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.01);

  KEpsilonConfig config;
  config.initialK = 1e-6;  // tiny, but above kFloor (1e-10).
  config.initialEpsilon = 0.01;
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries,
                            config);

  const Real expected = 1.0 * 0.09 * (1e-6 * 1e-6) / 0.01;
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_NEAR(model.turbulentViscosity()[i], expected, 1e-18) << "cell " << i;
    EXPECT_LT(model.turbulentViscosity()[i], 1e-9) << "cell " << i;
  }
}

// --- section 25: correct() failure/validation semantics ---------------------

TEST(KEpsilonModelTest, CorrectRejectsMismatchedVelocitySize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, {});

  const VectorField wrongSizeVelocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(model.correct(mesh, wrongSizeVelocity, pressure), InvalidArgumentError);
}

TEST(KEpsilonModelTest, CorrectProducesPositiveFiniteFieldsForARealFlow) {
  // P2-TURB-004 sections 11/25: one correct() call against a physically
  // reasonable lid-driven-cavity velocity field must leave k>=0,
  // epsilon>0, mu_t>=0, all finite -- not just at construction.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);
  KEpsilonConfig config;
  config.initialK = 0.01;
  config.initialEpsilon = 0.001;
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, config);

  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    // A simple, smooth, nonzero synthetic flow field -- not a converged
    // SIMPLE solution (that is the solver-integration test's job), just
    // enough shear to give correct() real, nonzero production to work
    // with.
    velocity[cell.id()] = Vector2{cell.centroid().y, 0.0};
  }
  const ScalarField pressure(mesh.numberOfCells(), 0.0);

  model.correct(mesh, velocity, pressure);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_TRUE(std::isfinite(model.k()[i])) << "cell " << i;
    EXPECT_GE(model.k()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.epsilon()[i])) << "cell " << i;
    EXPECT_GT(model.epsilon()[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.turbulentViscosity()[i])) << "cell " << i;
    EXPECT_GE(model.turbulentViscosity()[i], 0.0) << "cell " << i;
  }
  ASSERT_TRUE(model.convergenceResidual().has_value());
  EXPECT_TRUE(std::isfinite(*model.convergenceResidual()));
  EXPECT_GE(*model.convergenceResidual(), 0.0);
}

TEST(KEpsilonModelTest, ConvergenceResidualIsNulloptBeforeFirstCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.001);
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries, {});
  EXPECT_FALSE(model.convergenceResidual().has_value());
}

TEST(KEpsilonModelTest, EffectiveViscosityEqualsMolecularPlusTurbulent) {
  // Base-class arithmetic (TurbulenceModel::effectiveViscosity), reused
  // unmodified -- proves KEpsilonModel plugs into the same shared
  // formula LaminarModel does, not a separate one.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.2, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 2.0);
  const auto epsilonBoundaries = makeScalarBoundaries(mesh, 1e-6, 0.5);
  KEpsilonConfig config;
  config.initialK = 2.0;
  config.initialEpsilon = 0.5;
  const KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsilonBoundaries,
                            config);

  const Real mu = 0.017;
  const ScalarField effective = model.effectiveViscosity(mu);
  for (Index i = 0; i < effective.size(); ++i) {
    EXPECT_DOUBLE_EQ(effective[i], mu + 0.864) << "cell " << i;
  }
}
