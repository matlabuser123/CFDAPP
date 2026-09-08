// NOTE: none of these tests instantiate SIMPLE, MomentumEquation, or
// PressureCorrection -- BC behavior and BC/patch association are verified
// using only core + mesh + boundary, per the P0 -- Boundary Conditions
// gate ("BC behavior verified independently of SIMPLE").

#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::boundary::VectorBoundaryCondition;
using cfd::boundary::Wall;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(BoundaryConditionSetTest, RejectsUnknownPatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet bcs;
  EXPECT_THROW(bcs.set(mesh, "upperWall", std::make_unique<Wall>()), cfd::InvalidArgumentError);
}

TEST(BoundaryConditionSetTest, RejectsDuplicateAssignment) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet bcs;
  bcs.set(mesh, "top", std::make_unique<Wall>());
  EXPECT_THROW(bcs.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0})),
               cfd::InvalidArgumentError);
}

TEST(BoundaryConditionSetTest, ReplaceOverwritesExistingAssignment) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet bcs;
  bcs.set(mesh, "top", std::make_unique<Wall>());
  bcs.replace(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));

  EXPECT_EQ(bcs.get("top").type(), BoundaryConditionType::MovingWall);
}

TEST(BoundaryConditionSetTest, MissingPatchesReportsUnassignedPatches) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet bcs;
  bcs.set(mesh, "left", std::make_unique<Wall>());

  EXPECT_EQ(bcs.missingPatches(mesh).size(), 3U);
}

TEST(BoundaryConditionSetTest, GetThrowsForUnassignedPatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet bcs;
  EXPECT_THROW((void)bcs.get("left"), cfd::InvalidArgumentError);
}

// --- Cavity configuration (lid-driven cavity) -----------------------------

TEST(CavityBoundaryConfigurationTest, LidDrivenCavitySetup) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);

  BoundaryConditionSet bcs;
  bcs.set(mesh, "left", std::make_unique<Wall>());
  bcs.set(mesh, "right", std::make_unique<Wall>());
  bcs.set(mesh, "bottom", std::make_unique<Wall>());
  bcs.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));

  EXPECT_TRUE(bcs.missingPatches(mesh).empty());

  struct StationaryWallCheck {
    const char* patch;
    Vector2 normal;
  };
  const StationaryWallCheck stationaryWalls[3] = {
      {"left", Vector2{-1.0, 0.0}},
      {"right", Vector2{1.0, 0.0}},
      {"bottom", Vector2{0.0, -1.0}},
  };
  const Vector2 owner{0.0, 0.0};
  for (const auto& check : stationaryWalls) {
    const auto& bc = static_cast<const VectorBoundaryCondition&>(bcs.get(check.patch));
    const Vector2 u = bc.boundaryValue(owner, 1.0, check.normal);
    EXPECT_DOUBLE_EQ(u.x, 0.0);
    EXPECT_DOUBLE_EQ(u.y, 0.0);
  }

  const auto& top = static_cast<const VectorBoundaryCondition&>(bcs.get("top"));
  const Vector2 topNormal{0.0, 1.0};
  const Vector2 uTop = top.boundaryValue(owner, 1.0, topNormal);
  EXPECT_DOUBLE_EQ(cfd::dot(uTop, topNormal), 0.0);  // impermeable
  EXPECT_DOUBLE_EQ(uTop.x, 1.0);
  EXPECT_DOUBLE_EQ(uTop.y, 0.0);
}

// --- Channel configuration -------------------------------------------------

TEST(ChannelBoundaryConfigurationTest, InletOutletWallSetup) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 2, 2.0, 1.0);

  BoundaryConditionSet bcs;
  bcs.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  bcs.set(mesh, "right", std::make_unique<Outlet>());
  bcs.set(mesh, "bottom", std::make_unique<Wall>());
  bcs.set(mesh, "top", std::make_unique<Wall>());

  EXPECT_TRUE(bcs.missingPatches(mesh).empty());

  const auto& inlet = static_cast<const VectorBoundaryCondition&>(bcs.get("left"));
  const Vector2 uInlet = inlet.boundaryValue(Vector2{0.0, 0.0}, 1.0, Vector2{-1.0, 0.0});
  EXPECT_DOUBLE_EQ(uInlet.x, 1.0);
  EXPECT_DOUBLE_EQ(uInlet.y, 0.0);

  const auto& outlet = static_cast<const VectorBoundaryCondition&>(bcs.get("right"));
  const Vector2 uOwner{0.8, 0.1};
  const Vector2 uOutlet = outlet.boundaryValue(uOwner, 1.0, Vector2{1.0, 0.0});
  EXPECT_DOUBLE_EQ(uOutlet.x, uOwner.x);
  EXPECT_DOUBLE_EQ(uOutlet.y, uOwner.y);

  const auto& bottomWall = static_cast<const VectorBoundaryCondition&>(bcs.get("bottom"));
  const Vector2 uBottom = bottomWall.boundaryValue(Vector2{5.0, 5.0}, 1.0, Vector2{0.0, -1.0});
  EXPECT_DOUBLE_EQ(uBottom.x, 0.0);
  EXPECT_DOUBLE_EQ(uBottom.y, 0.0);
}
