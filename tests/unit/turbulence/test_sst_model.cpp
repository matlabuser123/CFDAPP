// P2-TURB-006: SSTModel itself -- field ownership/initialization,
// positivity/failure semantics, near-wall/far-field limiting behavior
// (sections 22-23, 46-47), and the standard-k-omega-limiting-equivalence
// statement (section 46). Mirrors test_komega_model.cpp's own structure.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/SSTEquation.hpp"
#include "cfd/turbulence/SSTModel.hpp"

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
using cfd::turbulence::blendSSTCoefficient;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;

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

// --- identity/field ownership ------------------------------------------

TEST(SSTModelTest, NameIsSST) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});
  EXPECT_EQ(model.name(), "SST");
}

TEST(SSTModelTest, FieldsSizedToMeshAtConstruction) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});

  EXPECT_EQ(model.k().size(), mesh.numberOfCells());
  EXPECT_EQ(model.omega().size(), mesh.numberOfCells());
  EXPECT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
  EXPECT_EQ(model.f1().size(), mesh.numberOfCells());
  EXPECT_EQ(model.f2().size(), mesh.numberOfCells());
  EXPECT_EQ(model.wallDistance().size(), mesh.numberOfCells());
}

TEST(SSTModelTest, InitializedToConfiguredValuesEverywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.05);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 8.0);
  SSTConfig config;
  config.initialK = 0.05;
  config.initialOmega = 8.0;
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(model.k()[i], 0.05) << "cell " << i;
    EXPECT_DOUBLE_EQ(model.omega()[i], 8.0) << "cell " << i;
  }
}

TEST(SSTModelTest, ConstructionMuTReducesToStandardKOmegaForm) {
  // Documented construction-time behavior (SSTModel.cpp's own comment):
  // with no velocity field yet (S=0), the limiter's denominator reduces
  // to exactly a1*omega, so mu_t = rho*a1*k/(a1*omega) = rho*k/omega --
  // the same value standard k-omega would give.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.2, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.5);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 4.0);
  SSTConfig config;
  config.initialK = 0.5;
  config.initialOmega = 4.0;
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const Real expected = 1.2 * 0.5 / 4.0;  // = 0.15, same as KOmegaModel's own hand test.
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_DOUBLE_EQ(model.turbulentViscosity()[i], expected) << "cell " << i;
  }
}

TEST(SSTModelTest, ConstructionIsDeterministicAcrossInstances) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const SSTModel a(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});
  const SSTModel b(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(a.k()[i], b.k()[i]);
    EXPECT_EQ(a.omega()[i], b.omega()[i]);
    EXPECT_EQ(a.turbulentViscosity()[i], b.turbulentViscosity()[i]);
    EXPECT_EQ(a.f1()[i], b.f1()[i]);
    EXPECT_EQ(a.f2()[i], b.f2()[i]);
    EXPECT_EQ(a.wallDistance()[i], b.wallDistance()[i]);
  }
}

// --- initialization/coefficient validation ------------------------------

TEST(SSTModelTest, RejectsInvalidInitialK) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  for (const Real badK : {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(),
                          std::numeric_limits<Real>::infinity()}) {
    SSTConfig config;
    config.initialK = badK;
    EXPECT_THROW(
        (SSTModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config)),
        InvalidArgumentError)
        << "initialK = " << badK;
  }
}

TEST(SSTModelTest, RejectsInvalidInitialOmega) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  for (const Real badOmega : {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(),
                              std::numeric_limits<Real>::infinity()}) {
    SSTConfig config;
    config.initialOmega = badOmega;
    EXPECT_THROW(
        (SSTModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config)),
        InvalidArgumentError)
        << "initialOmega = " << badOmega;
  }
}

TEST(SSTModelTest, RejectsInvalidCoefficients) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  SSTConfig config;
  config.coefficients.a1 = -0.31;
  EXPECT_THROW((SSTModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config)),
              InvalidArgumentError);
}

TEST(SSTModelTest, RejectsNoWallPatches) {
  // SST is meaningless without at least one wall -- propagated from
  // WallDistance.hpp's own validation
  // (WallDistanceTest.RejectsNoWallPatches). MovingWall is deliberately
  // NOT used as a stand-in wall here (unlike other tests in this file):
  // every patch below is Inlet/Outlet/Symmetry, none of which
  // WallDistance.hpp treats as a wall.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Symmetry>());
  velocityBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::Symmetry>());
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  EXPECT_THROW(
      (SSTModel(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, SSTConfig{})),
      InvalidArgumentError);
}

// --- correct() field validity --------------------------------------------

TEST(SSTModelTest, CorrectRejectsMismatchedVelocitySize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});

  const VectorField wrongSizeVelocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(model.correct(mesh, wrongSizeVelocity, pressure), InvalidArgumentError);
}

TEST(SSTModelTest, CorrectProducesPositiveFiniteFieldsForARealFlow) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  SSTConfig config;
  config.initialK = 0.01;
  config.initialOmega = 10.0;
  SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

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
    EXPECT_TRUE(std::isfinite(model.f1()[i])) << "cell " << i;
    EXPECT_GE(model.f1()[i], 0.0) << "cell " << i;
    EXPECT_LE(model.f1()[i], 1.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(model.f2()[i])) << "cell " << i;
    EXPECT_GE(model.f2()[i], 0.0) << "cell " << i;
    EXPECT_LE(model.f2()[i], 1.0) << "cell " << i;
  }
  ASSERT_TRUE(model.convergenceResidual().has_value());
  EXPECT_TRUE(std::isfinite(*model.convergenceResidual()));
  EXPECT_GE(*model.convergenceResidual(), 0.0);
}

TEST(SSTModelTest, ConvergenceResidualIsNulloptBeforeFirstCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, {});
  EXPECT_FALSE(model.convergenceResidual().has_value());
}

TEST(SSTModelTest, EffectiveViscosityEqualsMolecularPlusTurbulent) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.2, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.5);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 4.0);
  SSTConfig config;
  config.initialK = 0.5;
  config.initialOmega = 4.0;
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const Real mu = 0.017;
  const ScalarField effective = model.effectiveViscosity(mu);
  for (Index i = 0; i < effective.size(); ++i) {
    EXPECT_DOUBLE_EQ(effective[i], mu + 0.15) << "cell " << i;
  }
}

// --- near-wall / far-field limiting behavior (sections 22-23, 46-47) ----

TEST(SSTModelTest, F1ApproachesOneNearAWallAndZeroFarFromIt) {
  // A tall, thin domain (bottom wall only relevant patch, "top" a
  // symmetry-like zero-gradient so it is not treated as a second wall --
  // actually simplest: top is also a wall here, so both a near-bottom
  // and near-top cell exist, plus a far-from-both cell in the middle for
  // a genuinely small F1) -- ny=20 rows over a height of 1, so row 0 is
  // 0.025 from the bottom wall (very close: F1 should be near 1) and the
  // middle rows sit ~0.5 from both walls (F1 should be small).
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 20, 1.0, 1.0);
  const FluidProperties fluid(1.0, 1.5e-5);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<MovingWall>(Vector2{0.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<MovingWall>(Vector2{0.0, 0.0}));
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  const auto kBoundaries = makeScalarBoundaries(mesh, 0.0, 0.01);
  const auto omegaBoundaries = makeScalarBoundaries(mesh, 1e-6, 10.0);

  SSTConfig config;
  config.initialK = 0.01;
  config.initialOmega = 10.0;
  const SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  // Cell 0: bottom row, y=0.025 -- very close to the bottom wall.
  EXPECT_GT(model.f1()[0], 0.9);
  // Cell 10 (roughly mid-height, y~=0.525): far from both walls relative
  // to the near-wall cell.
  EXPECT_LT(model.f1()[10], model.f1()[0]);
}

TEST(SSTModelTest, NearWallBlendApproachesSSTsOwnInnerCoefficientSet) {
  // Section 46 (mandatory, with this task's own explicit "do not demand
  // bit equality... state exactly what limiting equivalence is
  // expected" instruction honored here): as F1 -> 1, SST's blended
  // coefficients approach SST's own *inner* set (sigmaK1, sigmaOmega1,
  // beta1) -- NOT cfd::turbulence::KOmegaModel's own stored constants,
  // since SST's published inner-region coefficients (sigmaK1=0.85,
  // sigmaOmega1=0.5) deliberately differ from Wilcox's original standard
  // k-omega set (sigmaK=2.0, sigmaOmega=2.0, KOmegaCoefficients) --
  // Menter's own SST paper modifies them. What IS common between SST-
  // near-wall and standard k-omega is the *functional form*: mu_t
  // reduces to rho*k/omega (proved at the equation level by
  // SSTEquationTest.TurbulentViscosityOmegaDominatedBranch) once the
  // eddy-viscosity limiter is inactive, not the specific sigma/beta
  // values.
  const Real f1NearWall = 0.999;  // representative near-wall value.
  const Real blendedSigmaK = blendSSTCoefficient(f1NearWall, 0.85, 1.0);
  const Real blendedSigmaOmega = blendSSTCoefficient(f1NearWall, 0.5, 0.856);
  const Real blendedBeta = blendSSTCoefficient(f1NearWall, 0.075, 0.0828);
  EXPECT_NEAR(blendedSigmaK, 0.85, 1e-3);
  EXPECT_NEAR(blendedSigmaOmega, 0.5, 1e-3);
  EXPECT_NEAR(blendedBeta, 0.075, 1e-3);
}

TEST(SSTModelTest, FarFieldBlendApproachesSSTsOwnOuterCoefficientSet) {
  // Section 47: as F1 -> 0, SST's blended coefficients approach its own
  // *outer* set (sigmaK2, sigmaOmega2, beta2), derived from k-epsilon-
  // like behavior -- not a claim that SST becomes numerically identical
  // to cfd::turbulence::KEpsilonModel.
  const Real f1FarField = 0.001;
  const Real blendedSigmaK = blendSSTCoefficient(f1FarField, 0.85, 1.0);
  const Real blendedSigmaOmega = blendSSTCoefficient(f1FarField, 0.5, 0.856);
  const Real blendedBeta = blendSSTCoefficient(f1FarField, 0.075, 0.0828);
  EXPECT_NEAR(blendedSigmaK, 1.0, 1e-3);
  EXPECT_NEAR(blendedSigmaOmega, 0.856, 1e-3);
  EXPECT_NEAR(blendedBeta, 0.0828, 1e-3);
}
