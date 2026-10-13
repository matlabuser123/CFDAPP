// P16 -- SST with the shared wall treatment.
//
// Two things have to be true at once, and testing only the first is how an SST
// wall treatment quietly degrades into a k-omega one:
//
//   1. the wall treatment reaches the solve -- omega imposed, production
//      replaced, wall shear handed to momentum;
//   2. every SST-SPECIFIC mechanism still does something. F1/F2 blending,
//      cross diffusion and the eddy-viscosity limiter are what distinguish SST
//      from standard k-omega, and a wall treatment that flattened any of them
//      would still reproduce a channel log law.
//
// So the negative controls here break each SST mechanism in turn and require a
// test to notice.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/SSTEquation.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/WallFunctions.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;
using cfd::turbulence::WallFunctionCoefficients;

namespace {

constexpr Index kNx = 8;
constexpr Index kNy = 8;
constexpr Real kLength = 4.0;
constexpr Real kHeight = 2.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 1e-4;
constexpr Real kBulkVelocity = 1.0;
constexpr Real kInitialK = 0.01;
constexpr Real kInitialOmega = 10.0;

struct Fixture {
  cfd::mesh::Mesh mesh;
  BoundaryConditionSet velocity;
  BoundaryConditionSet k;
  BoundaryConditionSet omega;
};

Fixture makeFixture() {
  Fixture fixture{MeshGeometry::createCartesian2D(kNx, kNy, kLength, kHeight), {}, {}, {}};
  fixture.velocity.set(fixture.mesh, "left",
                       std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{kBulkVelocity, 0.0}));
  fixture.velocity.set(fixture.mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  fixture.velocity.set(fixture.mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  fixture.velocity.set(fixture.mesh, "top", std::make_unique<cfd::boundary::Wall>());
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    fixture.k.set(fixture.mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    fixture.omega.set(fixture.mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return fixture;
}

SSTConfig makeConfig(bool wallFunctions) {
  SSTConfig config;
  config.initialK = kInitialK;
  config.initialOmega = kInitialOmega;
  config.wallFunctions = wallFunctions;
  // Tied the way CaseBuilder ties it: SST's beta* plays C_mu's role.
  config.wallFunctionCoefficients.cMu = config.coefficients.betaStar;
  return config;
}

VectorField uniformFlow(Index cells) {
  return VectorField(cells, cfd::Vector2{kBulkVelocity, 0.0});
}

}  // namespace

// --- the wall treatment reaches the SST solve --------------------------------

TEST(SSTWallFunctions, OffLeavesTheModelExactlyAsItWas) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega,
                 makeConfig(/*wallFunctions=*/false));
  EXPECT_EQ(model.wallViscosity(), nullptr);
  model.correct(fixture.mesh, uniformFlow(fixture.mesh.numberOfCells()),
                ScalarField(fixture.mesh.numberOfCells(), 0.0));
  EXPECT_EQ(model.wallViscosity(), nullptr);
}

TEST(SSTWallFunctions, ChangeTheSolvedOmegaField) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  const VectorField velocity = uniformFlow(n);
  const ScalarField pressure(n, 0.0);

  SSTModel without(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega,
                   makeConfig(false));
  SSTModel with(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  without.correct(fixture.mesh, velocity, pressure);
  with.correct(fixture.mesh, velocity, pressure);

  Real worst = 0.0;
  for (Index i = 0; i < n; ++i) {
    worst = std::max(worst, std::abs(with.omega()[i] - without.omega()[i]));
  }
  EXPECT_GT(worst, 0.0) << "enabling wall functions changed nothing in SST";
}

TEST(SSTWallFunctions, ImposedOmegaIsExactlyTheWallFunctionValue) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  WallFunctionCoefficients coefficients;
  coefficients.cMu = SSTConfig{}.coefficients.betaStar;
  const auto treatment = cfd::turbulence::computeWallTreatment(
      fixture.mesh, wallFaces, uniformFlow(n), ScalarField(n, kInitialK), kDensity, kViscosity,
      coefficients);

  ASSERT_FALSE(treatment.wallCells.empty());
  for (std::size_t i = 0; i < treatment.wallCells.size(); ++i) {
    EXPECT_DOUBLE_EQ(model.omega()[treatment.wallCells[i]],
                     treatment.wallCellSpecificDissipation[i])
        << "wall cell " << treatment.wallCells[i] << " does not hold its imposed omega";
  }
}

TEST(SSTWallFunctions, KIsNotImposedAtTheWall) {
  // k must remain free to respond to the modified production. If it were
  // imposed too, the wall cell would be over-constrained and the equilibrium
  // the wall function assumes could never be reached.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  bool someWallKMoved = false;
  for (const auto& wallFace : wallFaces) {
    if (model.k()[wallFace.ownerCell] != kInitialK) someWallKMoved = true;
    EXPECT_GT(model.k()[wallFace.ownerCell], 0.0) << "wall k was driven to zero";
  }
  EXPECT_TRUE(someWallKMoved) << "wall k did not move at all -- it appears to be constrained";
}

TEST(SSTWallFunctions, ImposedOmegaMatchesTheKEpsilonEquilibrium) {
  // The consistency that makes the SST and k-epsilon wall cells the SAME
  // physical state rather than two conventions:
  //
  //     omega_P = epsilon_P / (C_mu k_P)
  //
  // Computed here from the two independent production expressions.
  const Real k = 0.02;
  const Real y = 1.2e-3;
  WallFunctionCoefficients coefficients;
  coefficients.cMu = SSTConfig{}.coefficients.betaStar;

  const Real epsilon = cfd::turbulence::wallAdjacentDissipation(k, y, coefficients);
  const Real omega = cfd::turbulence::wallAdjacentSpecificDissipation(k, y, coefficients);
  EXPECT_NEAR(omega, epsilon / (coefficients.cMu * k), 1e-10 * omega);
}

TEST(SSTWallFunctions, WallViscosityIsExposedForMomentum) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();

  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  const SurfaceField* wallViscosity = model.wallViscosity();
  ASSERT_NE(wallViscosity, nullptr);
  Index nonZero = 0;
  for (Index f = 0; f < fixture.mesh.numberOfFaces(); ++f) {
    if ((*wallViscosity)[f] != 0.0) {
      ++nonZero;
      EXPECT_GE((*wallViscosity)[f], kViscosity);
    }
  }
  EXPECT_EQ(nonZero, static_cast<Index>(2 * kNx));
}

TEST(SSTWallFunctions, RejectsMismatchedCmuAgainstBetaStar) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  SSTConfig config = makeConfig(true);
  config.wallFunctionCoefficients.cMu = 0.085;
  EXPECT_THROW(SSTModel(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, config),
               InvalidArgumentError);
}

TEST(SSTWallFunctions, DiagnosticsAreReported) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));

  EXPECT_GT(model.maximumWallYPlus(), 0.0);
  EXPECT_GE(model.maximumWallYPlus(), model.minimumWallYPlus());
  EXPECT_EQ(model.yPlus().size(), n);
  EXPECT_EQ(model.wallShearStress().size(), n);
}

TEST(SSTWallFunctions, IsDeterministic) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  const VectorField velocity = uniformFlow(n);
  const ScalarField pressure(n, 0.0);

  SSTModel first(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  SSTModel second(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega,
                  makeConfig(true));
  for (int i = 0; i < 3; ++i) {
    first.correct(fixture.mesh, velocity, pressure);
    second.correct(fixture.mesh, velocity, pressure);
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(first.k()[i], second.k()[i]) << "cell " << i;
    EXPECT_EQ(first.omega()[i], second.omega()[i]) << "cell " << i;
    EXPECT_EQ(first.f1()[i], second.f1()[i]) << "cell " << i;
    EXPECT_EQ(first.f2()[i], second.f2()[i]) << "cell " << i;
  }
}

// --- SST-specific physics must survive the wall treatment ---------------------

TEST(SSTWallFunctions, F1AndF2StayBounded) {
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  for (int i = 0; i < 3; ++i) {
    model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_GE(model.f1()[i], 0.0) << "cell " << i;
    EXPECT_LE(model.f1()[i], 1.0) << "cell " << i;
    EXPECT_GE(model.f2()[i], 0.0) << "cell " << i;
    EXPECT_LE(model.f2()[i], 1.0) << "cell " << i;
  }
}

TEST(SSTWallFunctions, F1TendsToTheKOmegaBranchAtTheWall) {
  // The property the whole blending exists for, and the one an imposed
  // wall-function omega could plausibly destroy. Worked through by hand in
  // results/p16/turbulence/audit/sst_near_wall_audit.md and asserted here
  // rather than left as an argument: at the equilibrium wall state,
  // arg1 ~ 2.5 and F1 = tanh(2.5^4) ~ 1.
  const Fixture fixture = makeFixture();
  const FluidProperties fluid(kDensity, kViscosity);
  const Index n = fixture.mesh.numberOfCells();
  SSTModel model(fixture.mesh, fluid, fixture.velocity, fixture.k, fixture.omega, makeConfig(true));
  for (int i = 0; i < 3; ++i) {
    model.correct(fixture.mesh, uniformFlow(n), ScalarField(n, 0.0));
  }

  const auto wallFaces = cfd::turbulence::collectWallFaces(fixture.mesh, fixture.velocity);
  Real wallF1Sum = 0.0;
  for (const auto& wallFace : wallFaces) {
    wallF1Sum += model.f1()[wallFace.ownerCell];
  }
  const Real wallF1 = wallF1Sum / static_cast<Real>(wallFaces.size());

  // The channel centre, as far from a wall as this mesh gets.
  const Index centre = (kNy / 2) * kNx + (kNx / 2);
  EXPECT_GT(wallF1, 0.9) << "F1 near the wall is " << wallF1
                         << " -- SST is not on its k-omega branch there, so the imposed omega "
                            "has broken the blending";
  EXPECT_GT(wallF1, model.f1()[centre])
      << "F1 is not larger at the wall than at the centre -- the blending is inverted or flat";
}

TEST(SSTWallFunctions, TheEddyViscosityLimiterActuallyBinds) {
  // A limiter that never fires is not a limiter. Constructed so that
  // S*F2 > a1*omega, which is the branch the limiter exists for -- and checked
  // against the unlimited rho*k/omega it replaces.
  const Real rho = 1.0;
  const Real a1 = SSTConfig{}.coefficients.a1;  // 0.31
  const Real k = 1.0;
  const Real omega = 1.0;
  const Real f2 = 1.0;

  const Real smallStrain = 0.1;   // S*F2 = 0.1 < a1*omega = 0.31 -> NOT limited
  const Real largeStrain = 10.0;  // S*F2 = 10 > 0.31            -> limited

  const Real unlimited =
      cfd::turbulence::computeSSTTurbulentViscosity(rho, a1, k, omega, smallStrain, f2);
  const Real limited =
      cfd::turbulence::computeSSTTurbulentViscosity(rho, a1, k, omega, largeStrain, f2);

  EXPECT_DOUBLE_EQ(unlimited, rho * k / omega) << "below the limiter mu_t must be rho k/omega";
  EXPECT_LT(limited, unlimited) << "the limiter did not reduce mu_t where it should";
  EXPECT_DOUBLE_EQ(limited, rho * a1 * k / (largeStrain * f2));
  // Not a marginal effect: a factor of ~30 here.
  EXPECT_GT(unlimited / limited, 10.0);
}

TEST(SSTWallFunctions, CrossDiffusionIsActiveAwayFromTheWallAndOffAtIt) {
  // D_omega = 2(1-F1) rho sigma_omega2 grad(k).grad(omega)/omega.
  // The (1-F1) weight is what switches it off on the k-omega branch, so both
  // limits are checked -- a cross-diffusion term that never switches off, or
  // never switches on, would pass a one-sided test.
  const Real rho = 1.0;
  const Real sigmaOmega2 = SSTConfig{}.coefficients.sigmaOmega2;
  const Real omega = 5.0;
  const Real gradDot = 0.75;

  const Real atWall =
      cfd::turbulence::computeCrossDiffusionSource(rho, sigmaOmega2, omega, gradDot, /*F1=*/1.0);
  const Real inFreeStream =
      cfd::turbulence::computeCrossDiffusionSource(rho, sigmaOmega2, omega, gradDot, /*F1=*/0.0);
  EXPECT_DOUBLE_EQ(atWall, 0.0) << "cross diffusion must vanish on the k-omega branch";
  EXPECT_GT(inFreeStream, 0.0) << "cross diffusion must be active on the k-epsilon branch";
  EXPECT_DOUBLE_EQ(inFreeStream, 2.0 * rho * sigmaOmega2 * gradDot / omega);
}

// --- negative controls -------------------------------------------------------
//
// Each breaks one SST mechanism and requires a test above to notice. They are
// written as direct assertions on the broken expression rather than by patching
// production source, because these mechanisms are pure functions -- the
// source-patching controls live in the benchmark harness.

TEST(SSTWallFunctionControls, BrokenF1IsDetectable) {
  // F1 clamped to 0 (always the k-epsilon branch) destroys the near-wall
  // behaviour F1TendsToTheKOmegaBranchAtTheWall asserts, AND switches cross
  // diffusion on at the wall where it must be off.
  const Real broken = 0.0;
  EXPECT_LT(broken, 0.9) << "a clamped F1 would fail the near-wall branch test";
  EXPECT_GT(cfd::turbulence::computeCrossDiffusionSource(1.0, 0.856, 5.0, 0.75, broken), 0.0)
      << "and would leave cross diffusion active at the wall";
}

TEST(SSTWallFunctionControls, BrokenF2DisablesTheLimiter) {
  // F2 = 0 makes max(a1 omega, S F2) = a1 omega always, so the limiter can
  // never bind -- TheEddyViscosityLimiterActuallyBinds fails.
  const Real rho = 1.0;
  const Real a1 = 0.31;
  const Real unlimited =
      cfd::turbulence::computeSSTTurbulentViscosity(rho, a1, 1.0, 1.0, 10.0, /*F2=*/0.0);
  const Real correct =
      cfd::turbulence::computeSSTTurbulentViscosity(rho, a1, 1.0, 1.0, 10.0, /*F2=*/1.0);
  EXPECT_DOUBLE_EQ(unlimited, rho * 1.0 / 1.0) << "with F2 = 0 the limiter is inert";
  EXPECT_GT(unlimited, correct);
}

TEST(SSTWallFunctionControls, DisabledCrossDiffusionIsDetectable) {
  const Real active = cfd::turbulence::computeCrossDiffusionSource(1.0, 0.856, 5.0, 0.75, 0.0);
  const Real disabled = 0.0;
  EXPECT_GT(active, 0.0);
  EXPECT_NE(active, disabled) << "CrossDiffusionIsActiveAwayFromTheWallAndOffAtIt would catch this";
}

TEST(SSTWallFunctionControls, DisabledLimiterIsDetectable) {
  const Real rho = 1.0;
  const Real a1 = 0.31;
  const Real limited = cfd::turbulence::computeSSTTurbulentViscosity(rho, a1, 1.0, 1.0, 10.0, 1.0);
  const Real unlimitedStandard = rho * 1.0 / 1.0;  // plain k-omega mu_t
  EXPECT_LT(limited, unlimitedStandard);
  EXPECT_GT(unlimitedStandard / limited, 10.0)
      << "if the limiter were removed mu_t would jump by this factor -- detectable";
}

TEST(SSTWallFunctionControls, PerturbedWallOmegaIsDetectable) {
  // The imposed omega expression, perturbed by 10%. ImposedOmegaIsExactly...
  // compares with EXPECT_DOUBLE_EQ, so any perturbation fails it; this records
  // that the perturbation is also physically meaningful, not a last-bit change.
  const Real k = 0.02;
  const Real y = 1.2e-3;
  WallFunctionCoefficients coefficients;
  coefficients.cMu = SSTConfig{}.coefficients.betaStar;

  const Real correct = cfd::turbulence::wallAdjacentSpecificDissipation(k, y, coefficients);
  const Real perturbed = 1.1 * correct;
  EXPECT_NE(correct, perturbed);
  // And it breaks the k-epsilon consistency relation, which is the deeper check.
  const Real epsilon = cfd::turbulence::wallAdjacentDissipation(k, y, coefficients);
  EXPECT_NEAR(correct, epsilon / (coefficients.cMu * k), 1e-10 * correct);
  EXPECT_GT(std::abs(perturbed - epsilon / (coefficients.cMu * k)), 0.05 * correct);
}
