// P16 -- proof that the wall treatment reaches the production solve.
//
// WallFunctions' tests prove the formulas. WallTreatment's prove the mesh
// layer. Neither proves that turning `wallFunctions` on actually changes what
// k-epsilon and momentum assemble -- and a wall treatment that computes
// correct numbers nobody consumes is a diagnostic, not a model. These tests
// close that gap by comparing WITH against WITHOUT on the same configuration.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;

namespace {

// A channel sized so the first cell lands inside the wall function's valid
// band at the velocity and k used below -- the configuration the treatment is
// actually meant for, rather than one where it would refuse to apply.
constexpr Index kNx = 8;
constexpr Index kNy = 8;
constexpr Real kLength = 4.0;
constexpr Real kHeight = 2.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 1e-4;
constexpr Real kBulkVelocity = 1.0;
constexpr Real kInitialK = 0.01;
constexpr Real kInitialEpsilon = 0.01;

struct Fixture {
  cfd::mesh::Mesh mesh;
  BoundaryConditionSet velocity;
  BoundaryConditionSet k;
  BoundaryConditionSet epsilon;
};

Fixture makeFixture() {
  Fixture fixture{MeshGeometry::createCartesian2D(kNx, kNy, kLength, kHeight), {}, {}, {}};
  fixture.velocity.set(fixture.mesh, "left",
                       std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{kBulkVelocity, 0.0}));
  fixture.velocity.set(fixture.mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  fixture.velocity.set(fixture.mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  fixture.velocity.set(fixture.mesh, "top", std::make_unique<cfd::boundary::Wall>());

  // k zero-gradient at the walls -- the arrangement the wall treatment
  // requires, since epsilon is the imposed quantity.
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    fixture.k.set(fixture.mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    fixture.epsilon.set(fixture.mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return fixture;
}

KEpsilonConfig makeConfig(bool wallFunctions) {
  KEpsilonConfig config;
  config.initialK = kInitialK;
  config.initialEpsilon = kInitialEpsilon;
  config.wallFunctions = wallFunctions;
  return config;
}

VectorField uniformFlow(Index cells) {
  return VectorField(cells, cfd::Vector2{kBulkVelocity, 0.0});
}

}  // namespace

// --- k-epsilon wiring --------------------------------------------------------

TEST(WallFunctionWiring, WallFunctionsOffLeavesTheModelExactlyAsItWas) {
  // The property that keeps every pre-P16 result reproducible: with the flag
  // off, nothing about the model changes, and it exposes no wall viscosity for
  // momentum to pick up.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  KEpsilonModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                      makeConfig(/*wallFunctions=*/false));

  EXPECT_EQ(model.wallViscosity(), nullptr)
      << "with no wall treatment the momentum bridge must be absent, not an all-zero field";

  model.correct(fixture.mesh, uniformFlow(fixture.mesh.numberOfCells()),
                ScalarField(fixture.mesh.numberOfCells(), 0.0));
  EXPECT_EQ(model.wallViscosity(), nullptr);
  EXPECT_EQ(model.wallFacesOutsideValidBand(), 0u);
}

TEST(WallFunctionWiring, WallFunctionsChangeTheSolvedEpsilonField) {
  // THE test for step 1. Same mesh, same fluid, same initial state, same
  // velocity -- only the flag differs. If the treatment were computed and not
  // consumed, these two fields would be identical.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  const VectorField velocity = uniformFlow(n);
  const ScalarField pressure(n, 0.0);

  KEpsilonModel without(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                        makeConfig(false));
  KEpsilonModel with(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                     makeConfig(true));

  without.correct(fixture.mesh, velocity, pressure);
  with.correct(fixture.mesh, velocity, pressure);

  Real maxEpsilonDifference = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxEpsilonDifference =
        std::max(maxEpsilonDifference, std::abs(with.epsilon()[i] - without.epsilon()[i]));
  }
  EXPECT_GT(maxEpsilonDifference, 0.0)
      << "enabling wall functions changed nothing -- the treatment is not reaching the solve";
}

TEST(WallFunctionWiring, ImposedEpsilonIsExactlyTheWallFunctionValue) {
  // Not merely "different": the wall cell must hold the value the wall function
  // prescribes, to the last bit the solve preserves. fixRow makes the row
  // phi = value, so this is an equality, not an approximation.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  KEpsilonModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                      makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  // k at the time the treatment ran was the INITIAL k (correct() reads the
  // pre-update state), so the expected value is reproducible here.
  const ScalarField kAtTreatment(n, kInitialK);
  const auto treatment = cfd::turbulence::computeWallTreatment(
      fixture.mesh, wallFaces, uniformFlow(n), kAtTreatment, kDensity, kViscosity,
      cfd::turbulence::WallFunctionCoefficients{});

  ASSERT_FALSE(treatment.wallCells.empty());
  for (std::size_t i = 0; i < treatment.wallCells.size(); ++i) {
    EXPECT_DOUBLE_EQ(model.epsilon()[treatment.wallCells[i]], treatment.wallCellDissipation[i])
        << "wall cell " << treatment.wallCells[i] << " does not hold its imposed epsilon";
  }
}

TEST(WallFunctionWiring, ImposedEpsilonSurvivesTheNumericalFloor) {
  // The floor is applied to every solved cell. An imposed value is a boundary
  // condition, not a solver output, so it must not be moved by it -- otherwise
  // a small wall epsilon would silently stop satisfying the wall function.
  Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  KEpsilonConfig config = makeConfig(true);
  // A floor far above any physical wall epsilon here.
  config.epsilonFloor = 1e6;
  KEpsilonModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon, config);
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  const auto treatment = cfd::turbulence::computeWallTreatment(
      fixture.mesh, wallFaces, uniformFlow(n), ScalarField(n, kInitialK), kDensity, kViscosity,
      cfd::turbulence::WallFunctionCoefficients{});

  for (std::size_t i = 0; i < treatment.wallCells.size(); ++i) {
    const Real imposed = treatment.wallCellDissipation[i];
    ASSERT_LT(imposed, config.epsilonFloor) << "this test needs the floor to be above the value";
    EXPECT_DOUBLE_EQ(model.epsilon()[treatment.wallCells[i]], imposed)
        << "the floor overwrote an imposed boundary value";
  }
}

TEST(WallFunctionWiring, WallViscosityIsExposedForMomentum) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  KEpsilonModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                      makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  const SurfaceField* wallViscosity = model.wallViscosity();
  ASSERT_NE(wallViscosity, nullptr);
  ASSERT_EQ(wallViscosity->size(), fixture.mesh.numberOfFaces());

  Index nonZero = 0;
  for (Index f = 0; f < fixture.mesh.numberOfFaces(); ++f) {
    if ((*wallViscosity)[f] != 0.0) {
      ++nonZero;
      EXPECT_GE((*wallViscosity)[f], kViscosity);
    }
  }
  EXPECT_EQ(nonZero, static_cast<Index>(2 * kNx)) << "one entry per wall face, zero elsewhere";
}

TEST(WallFunctionWiring, RejectsMismatchedCmu) {
  // The wall function forms u_tau* from C_mu; the model forms mu_t from C_mu.
  // Two different values would be two different models, so this is refused
  // rather than silently producing an inconsistent wall.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  KEpsilonConfig config = makeConfig(true);
  config.wallFunctionCoefficients.cMu = 0.085;
  EXPECT_THROW(
      KEpsilonModel(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon, config),
      InvalidArgumentError);
}

TEST(WallFunctionWiring, DiagnosticsAreReportedAfterCorrect) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  KEpsilonModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                      makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  EXPECT_GT(model.maximumWallYPlus(), 0.0) << "y+ must be reported, not left at zero";
  EXPECT_GE(model.maximumWallYPlus(), model.minimumWallYPlus());
  EXPECT_EQ(model.yPlus().size(), n);
  EXPECT_EQ(model.wallShearStress().size(), n);

  // Every wall cell carries a y+; interior cells do not.
  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  for (const auto& wallFace : wallFaces) {
    EXPECT_GT(model.yPlus()[wallFace.ownerCell], 0.0);
    EXPECT_GT(model.wallShearStress()[wallFace.ownerCell], 0.0);
  }
}

TEST(WallFunctionWiring, IsDeterministic) {
  // Two identical models, same inputs, must agree to the last bit. A wall
  // treatment that depends on iteration order over an unordered container
  // would show up here.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  const VectorField velocity = uniformFlow(n);
  const ScalarField pressure(n, 0.0);

  KEpsilonModel first(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                      makeConfig(true));
  KEpsilonModel second(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.epsilon,
                       makeConfig(true));
  for (int iteration = 0; iteration < 3; ++iteration) {
    first.correct(fixture.mesh, velocity, pressure);
    second.correct(fixture.mesh, velocity, pressure);
  }

  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(first.k()[i], second.k()[i]) << "cell " << i;
    EXPECT_EQ(first.epsilon()[i], second.epsilon()[i]) << "cell " << i;
    EXPECT_EQ(first.turbulentViscosity()[i], second.turbulentViscosity()[i]) << "cell " << i;
  }
  for (Index f = 0; f < fixture.mesh.numberOfFaces(); ++f) {
    EXPECT_EQ((*first.wallViscosity())[f], (*second.wallViscosity())[f]) << "face " << f;
  }
}

// --- momentum wiring ---------------------------------------------------------

namespace {

// The wall-face diagonal of the assembled U-momentum row for the owner of the
// first wall face -- the coefficient the wall shear actually lives in.
Real assembleWallRowDiagonal(const Fixture& fixture, const ScalarField& effectiveViscosity,
                             const SurfaceField* wallViscosity, Index* wallOwnerOut) {
  const Index n = fixture.mesh.numberOfCells();
  cfd::algebra::SparseMatrixBuilder builder(n, n);
  cfd::algebra::Vector rhs(n, 0.0);
  const VectorField velocity = uniformFlow(n);

  cfd::physics::assembleDiffusionContribution(fixture.mesh, effectiveViscosity, velocity,
                                              fixture.velocity, cfd::physics::VelocityComponent::U,
                                              builder, rhs, /*applyNonOrthogonalCorrection=*/false,
                                              cfd::discretization::GradientScheme::GreenGauss,
                                              /*correctionVelocity=*/nullptr, wallViscosity);

  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  *wallOwnerOut = wallFaces.front().ownerCell;
  return builder.build().diagonal(*wallOwnerOut);
}

}  // namespace

TEST(WallFunctionWiring, MomentumUsesTheWallViscosityOnWallFacesOnly) {
  // Step 2's core claim: the wall-function viscosity changes the momentum
  // coefficient at a wall face, and only there.
  const Fixture fixture = makeFixture();
  const Index n = fixture.mesh.numberOfCells();
  const ScalarField molecular(n, kViscosity);

  Index wallOwner = 0;
  const Real withoutWall = assembleWallRowDiagonal(fixture, molecular, nullptr, &wallOwner);

  // A wall viscosity ten times molecular on wall faces only.
  SurfaceField wallViscosity(fixture.mesh.numberOfFaces(), 0.0);
  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  for (const auto& wallFace : wallFaces) {
    wallViscosity[wallFace.faceId] = 10.0 * kViscosity;
  }
  Index sameOwner = 0;
  const Real withWall = assembleWallRowDiagonal(fixture, molecular, &wallViscosity, &sameOwner);

  ASSERT_EQ(wallOwner, sameOwner);
  EXPECT_GT(withWall, withoutWall)
      << "a more dissipative wall must raise the momentum diagonal of its own cell";

  // An interior cell far from any wall is untouched.
  cfd::algebra::SparseMatrixBuilder plain(n, n);
  cfd::algebra::Vector plainRhs(n, 0.0);
  cfd::physics::assembleDiffusionContribution(
      fixture.mesh, molecular, uniformFlow(n), fixture.velocity, cfd::physics::VelocityComponent::U,
      plain, plainRhs, false, cfd::discretization::GradientScheme::GreenGauss, nullptr, nullptr);
  cfd::algebra::SparseMatrixBuilder overridden(n, n);
  cfd::algebra::Vector overriddenRhs(n, 0.0);
  cfd::physics::assembleDiffusionContribution(
      fixture.mesh, molecular, uniformFlow(n), fixture.velocity, cfd::physics::VelocityComponent::U,
      overridden, overriddenRhs, false, cfd::discretization::GradientScheme::GreenGauss, nullptr,
      &wallViscosity);

  const auto plainMatrix = plain.build();
  const auto overriddenMatrix = overridden.build();
  const Index interior = (kNy / 2) * kNx + (kNx / 2);
  EXPECT_EQ(plainMatrix.diagonal(interior), overriddenMatrix.diagonal(interior))
      << "an interior cell must be bitwise unaffected by a wall-face override";
}

TEST(WallFunctionWiring, NullWallViscosityIsBitwiseThePreP16Assembly) {
  // The guarantee that keeps every laminar result and the SIMPLE fingerprint
  // unchanged: passing nullptr must reproduce the old assembly exactly.
  const Fixture fixture = makeFixture();
  const Index n = fixture.mesh.numberOfCells();
  const ScalarField molecular(n, kViscosity);

  cfd::algebra::SparseMatrixBuilder withArgument(n, n);
  cfd::algebra::Vector withArgumentRhs(n, 0.0);
  cfd::physics::assembleDiffusionContribution(
      fixture.mesh, molecular, uniformFlow(n), fixture.velocity, cfd::physics::VelocityComponent::U,
      withArgument, withArgumentRhs, false, cfd::discretization::GradientScheme::GreenGauss,
      nullptr, nullptr);

  cfd::algebra::SparseMatrixBuilder defaulted(n, n);
  cfd::algebra::Vector defaultedRhs(n, 0.0);
  cfd::physics::assembleDiffusionContribution(fixture.mesh, molecular, uniformFlow(n),
                                              fixture.velocity, cfd::physics::VelocityComponent::U,
                                              defaulted, defaultedRhs);

  const auto a = withArgument.build();
  const auto b = defaulted.build();
  ASSERT_EQ(a.nonZeros(), b.nonZeros());
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.diagonal(i), b.diagonal(i)) << "cell " << i;
    EXPECT_EQ(withArgumentRhs[i], defaultedRhs[i]) << "cell " << i;
  }
}

TEST(WallFunctionWiring, MomentumRejectsAWrongSizedWallViscosity) {
  const Fixture fixture = makeFixture();
  const Index n = fixture.mesh.numberOfCells();
  const ScalarField molecular(n, kViscosity);
  const SurfaceField wrongSize(3, 1.0);

  cfd::algebra::SparseMatrixBuilder builder(n, n);
  cfd::algebra::Vector rhs(n, 0.0);
  EXPECT_THROW(cfd::physics::assembleDiffusionContribution(
                   fixture.mesh, molecular, uniformFlow(n), fixture.velocity,
                   cfd::physics::VelocityComponent::U, builder, rhs, false,
                   cfd::discretization::GradientScheme::GreenGauss, nullptr, &wrongSize),
               InvalidArgumentError);
}
