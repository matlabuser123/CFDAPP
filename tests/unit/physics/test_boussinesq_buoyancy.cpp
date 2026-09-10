// P3-PHYS-001: Boussinesq buoyancy body-force model -- equation-level
// tests for both BoussinesqBuoyancy::source() itself (Phase 6) and its
// integration into momentum assembly via
// assembleBuoyancySourceContribution (Phase 3/7).
#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <utility>

#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleBuoyancySourceContribution;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet makeZeroGradientBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

// --- Construction validation ---------------------------------------------

TEST(BoussinesqBuoyancyTest, ConstructsWithValidParameters) {
  EXPECT_NO_THROW(BoussinesqBuoyancy(1.0, 0.0034, 300.0, Vector2{0.0, -9.81}));
}

TEST(BoussinesqBuoyancyTest, RejectsNonPositiveReferenceDensity) {
  EXPECT_THROW(BoussinesqBuoyancy(0.0, 0.0034, 300.0, Vector2{0.0, -9.81}), InvalidArgumentError);
  EXPECT_THROW(BoussinesqBuoyancy(-1.0, 0.0034, 300.0, Vector2{0.0, -9.81}), InvalidArgumentError);
}

TEST(BoussinesqBuoyancyTest, RejectsNegativeBeta) {
  EXPECT_THROW(BoussinesqBuoyancy(1.0, -0.001, 300.0, Vector2{0.0, -9.81}), InvalidArgumentError);
}

TEST(BoussinesqBuoyancyTest, AcceptsZeroBeta) {
  EXPECT_NO_THROW(BoussinesqBuoyancy(1.0, 0.0, 300.0, Vector2{0.0, -9.81}));
}

TEST(BoussinesqBuoyancyTest, RejectsNonFiniteInputs) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(BoussinesqBuoyancy(nan, 0.0034, 300.0, Vector2{0.0, -9.81}), InvalidArgumentError);
  EXPECT_THROW(BoussinesqBuoyancy(1.0, nan, 300.0, Vector2{0.0, -9.81}), InvalidArgumentError);
  EXPECT_THROW(BoussinesqBuoyancy(1.0, 0.0034, nan, Vector2{0.0, -9.81}), InvalidArgumentError);
  EXPECT_THROW(BoussinesqBuoyancy(1.0, 0.0034, 300.0, Vector2{nan, -9.81}), InvalidArgumentError);
  EXPECT_THROW(BoussinesqBuoyancy(1.0, 0.0034, 300.0, Vector2{0.0, inf}), InvalidArgumentError);
}

TEST(BoussinesqBuoyancyTest, RejectsNonFiniteTemperature) {
  const BoussinesqBuoyancy buoyancy(1.0, 0.0034, 300.0, Vector2{0.0, -9.81});
  EXPECT_THROW((void)buoyancy.source(std::numeric_limits<Real>::quiet_NaN()),
              InvalidArgumentError);
}

// --- Zero-buoyancy equivalence cases (Phase 5/6) --------------------------

TEST(BoussinesqBuoyancyTest, ReferenceTemperatureGivesZeroSource) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const Vector2 s = buoyancy.source(300.0);
  EXPECT_DOUBLE_EQ(s.x, 0.0);
  EXPECT_DOUBLE_EQ(s.y, 0.0);
}

TEST(BoussinesqBuoyancyTest, ZeroBetaGivesZeroSourceForAnyTemperature) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0, 300.0, Vector2{0.0, -9.81});
  for (Real t : {200.0, 300.0, 350.0, 1000.0}) {
    const Vector2 s = buoyancy.source(t);
    EXPECT_DOUBLE_EQ(s.x, 0.0) << "T=" << t;
    EXPECT_DOUBLE_EQ(s.y, 0.0) << "T=" << t;
  }
}

TEST(BoussinesqBuoyancyTest, ZeroGravityGivesZeroSourceForAnyTemperature) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, 0.0});
  for (Real t : {200.0, 300.0, 350.0, 1000.0}) {
    const Vector2 s = buoyancy.source(t);
    EXPECT_DOUBLE_EQ(s.x, 0.0) << "T=" << t;
    EXPECT_DOUBLE_EQ(s.y, 0.0) << "T=" << t;
  }
}

// --- Sign convention (Phase 7) ---------------------------------------------
//
// Hand-derivation (also in BoussinesqBuoyancy.hpp's own header comment):
// S_b = -rho_ref*beta*(T-Tref)*g. With g=(0,-9.81), T=310 > Tref=300:
//   S_b.y = -(1.2*0.0034*(310-300))*(-9.81) = -(0.04080)*(-9.81)
//         = +0.400248  (upward, +y).
// A naive S_b = +rho_ref*beta*(T-Tref)*g (missing the leading minus) would
// instead give S_b.y = -0.400248 -- hotter fluid pushed *down*, the exact
// wrong-sign bug this test exists to catch.

TEST(BoussinesqBuoyancyTest, HotCellWithDownwardGravityDrivesFluidUpward) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const Vector2 s = buoyancy.source(310.0);
  EXPECT_NEAR(s.y, 0.400248, 1e-6);
  EXPECT_GT(s.y, 0.0) << "hot fluid must be pushed upward (+y), opposite gravity's -y direction";
  EXPECT_DOUBLE_EQ(s.x, 0.0);
}

TEST(BoussinesqBuoyancyTest, ColdCellWithDownwardGravityDrivesFluidDownward) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const Vector2 s = buoyancy.source(290.0);
  EXPECT_NEAR(s.y, -0.400248, 1e-6);
  EXPECT_LT(s.y, 0.0) << "cold fluid must sink (-y), same direction as gravity";
}

TEST(BoussinesqBuoyancyTest, ReversingGravityReversesTheHotCellDirection) {
  const BoussinesqBuoyancy downward(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const BoussinesqBuoyancy upward(1.2, 0.0034, 300.0, Vector2{0.0, 9.81});
  const Vector2 sDown = downward.source(310.0);
  const Vector2 sUp = upward.source(310.0);
  EXPECT_DOUBLE_EQ(sDown.y, -sUp.y);
}

// --- Linear temperature dependence ------------------------------------------

TEST(BoussinesqBuoyancyTest, SourceIsLinearInTemperatureDeviation) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const Vector2 sOneDelta = buoyancy.source(305.0);   // deltaT = 5.
  const Vector2 sTwoDelta = buoyancy.source(310.0);   // deltaT = 10 = 2*5.
  EXPECT_NEAR(sTwoDelta.y, 2.0 * sOneDelta.y, 1e-12);
  EXPECT_NEAR(sTwoDelta.x, 2.0 * sOneDelta.x, 1e-12);
}

// --- Horizontal gravity component sanity ------------------------------------

TEST(BoussinesqBuoyancyTest, HorizontalGravityProducesHorizontalSourceOnly) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{-9.81, 0.0});
  const Vector2 s = buoyancy.source(310.0);
  EXPECT_NEAR(s.x, 0.400248, 1e-6);
  EXPECT_DOUBLE_EQ(s.y, 0.0);
}

// --- Momentum-assembly integration (Phase 3): cell-volume scaling ----------

TEST(BoussinesqBuoyancyTest, MomentumContributionScalesWithCellVolume) {
  // Two meshes covering the same physical domain at different resolutions
  // -- a uniform temperature deviation must integrate to (source * cell
  // volume) at every cell, regardless of how many cells the domain is
  // divided into (Phase 3/6's "grid-volume scaling" requirement).
  const BoussinesqBuoyancy buoyancy(1.0, 0.01, 300.0, Vector2{0.0, -10.0});
  const Real temperatureValue = 310.0;  // uniform, deltaT = 10.
  const Vector2 expectedSourcePerVolume = buoyancy.source(temperatureValue);

  for (const auto& grid : {std::pair<cfd::Index, cfd::Index>{4, 4},
                          std::pair<cfd::Index, cfd::Index>{8, 8}}) {
    const Mesh mesh = MeshGeometry::createCartesian2D(grid.first, grid.second, 1.0, 1.0);
    const auto boundaries = makeZeroGradientBoundaries(mesh);
    const ScalarField temperature(mesh.numberOfCells(), temperatureValue);

    Vector rhsU(mesh.numberOfCells(), 0.0);
    Vector rhsV(mesh.numberOfCells(), 0.0);
    assembleBuoyancySourceContribution(mesh, temperature, buoyancy, VelocityComponent::U, rhsU);
    assembleBuoyancySourceContribution(mesh, temperature, buoyancy, VelocityComponent::V, rhsV);

    for (const auto& cell : mesh.cells()) {
      EXPECT_NEAR(rhsU[cell.id()], expectedSourcePerVolume.x * cell.volume(), 1e-9)
          << "grid " << grid.first << "x" << grid.second << " cell " << cell.id();
      EXPECT_NEAR(rhsV[cell.id()], expectedSourcePerVolume.y * cell.volume(), 1e-9)
          << "grid " << grid.first << "x" << grid.second << " cell " << cell.id();
    }
  }
}

TEST(BoussinesqBuoyancyTest, DoublingCellVolumeDoublesIntegratedSource) {
  // Direct restatement of Phase 6's "grid-volume scaling" example: same
  // buoyancy/temperature, a domain twice the area with the same cell
  // count doubles each cell's volume and must double the integrated RHS.
  const BoussinesqBuoyancy buoyancy(1.0, 0.01, 300.0, Vector2{0.0, -10.0});
  const ScalarField temperature4x4(16, 310.0);

  const Mesh unitMesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh doubledMesh = MeshGeometry::createCartesian2D(4, 4, 2.0, 1.0);  // double the area.
  const auto unitBoundaries = makeZeroGradientBoundaries(unitMesh);
  const auto doubledBoundaries = makeZeroGradientBoundaries(doubledMesh);

  Vector rhsUnit(16, 0.0);
  Vector rhsDoubled(16, 0.0);
  assembleBuoyancySourceContribution(unitMesh, temperature4x4, buoyancy, VelocityComponent::V,
                                     rhsUnit);
  assembleBuoyancySourceContribution(doubledMesh, temperature4x4, buoyancy, VelocityComponent::V,
                                     rhsDoubled);

  for (cfd::Index i = 0; i < 16; ++i) {
    EXPECT_NEAR(rhsDoubled[i], 2.0 * rhsUnit[i], 1e-9) << "cell " << i;
  }
}

TEST(BoussinesqBuoyancyTest, UniformReferenceTemperatureGivesZeroMomentumContribution) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells(), 300.0);

  Vector rhsU(mesh.numberOfCells(), 0.0);
  Vector rhsV(mesh.numberOfCells(), 0.0);
  assembleBuoyancySourceContribution(mesh, temperature, buoyancy, VelocityComponent::U, rhsU);
  assembleBuoyancySourceContribution(mesh, temperature, buoyancy, VelocityComponent::V, rhsV);
  for (cfd::Index i = 0; i < rhsU.size(); ++i) {
    EXPECT_DOUBLE_EQ(rhsU[i], 0.0);
    EXPECT_DOUBLE_EQ(rhsV[i], 0.0);
  }
}

TEST(BoussinesqBuoyancyTest, MismatchedTemperatureSizeThrows) {
  const BoussinesqBuoyancy buoyancy(1.2, 0.0034, 300.0, Vector2{0.0, -9.81});
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField temperature(mesh.numberOfCells() + 1, 300.0);
  Vector rhs(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(
      assembleBuoyancySourceContribution(mesh, temperature, buoyancy, VelocityComponent::U, rhs),
      InvalidArgumentError);
}
