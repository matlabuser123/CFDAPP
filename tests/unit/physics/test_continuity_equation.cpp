#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::BoundaryPatch;
using cfd::mesh::Cell;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::ContinuityResult;
using cfd::physics::evaluateContinuity;
using cfd::physics::FluidProperties;

namespace {

// One singleton boundary patch per face, each given the *exact*
// analytical velocity at that face via MovingWall -- same technique as
// tests/unit/discretization/ManufacturedFields.hpp's
// perFaceBoundaryMesh/makeExactBoundaries, reimplemented locally here to
// keep the physics test target self-contained (no cross-directory test
// dependency). Lets a linear/rotational velocity field be reproduced
// exactly at every boundary face instead of approximated by one constant
// value per side.
Mesh perFaceBoundaryMesh(Index nx, Index ny, Real lengthX, Real lengthY) {
  const Mesh base = MeshGeometry::createCartesian2D(nx, ny, lengthX, lengthY);
  std::vector<Cell> cells = base.cells();
  std::vector<Face> faces = base.faces();

  std::vector<BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }
  return Mesh(std::move(cells), std::move(faces), std::move(patches));
}

template <typename VelocityFunction>
BoundaryConditionSet makeExactVelocityBoundaries(const Mesh& mesh, VelocityFunction u) {
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    bcs.set(mesh, patch.name(), std::make_unique<MovingWall>(u(mesh.face(faceId).centroid())));
  }
  return bcs;
}

}  // namespace

TEST(ContinuityEquationTest, ClosedZeroVelocityCavityHasZeroImbalance) {
  // TODO.md section 53: impermeable walls, zero velocity -> every face
  // flux is 0, so every cell imbalance and the global imbalance are 0.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<Wall>());
  }
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const ContinuityResult result = evaluateContinuity(mesh, massFlux);
  for (Index i = 0; i < result.cellImbalance.size(); ++i) {
    EXPECT_NEAR(result.cellImbalance[i], 0.0, 1e-14);
  }
  EXPECT_NEAR(result.globalNetFlux, 0.0, 1e-14);
  EXPECT_NEAR(result.totalAbsoluteImbalance, 0.0, 1e-14);
  EXPECT_NEAR(result.maxCellImbalance, 0.0, 1e-14);
}

TEST(ContinuityEquationTest, InternalFacePairCancelsExactly) {
  // TODO.md section 52: a 2x1 mesh with the internal face given F=3
  // directly -- cell 0 gets +3, cell 1 gets -3, before boundary faces
  // (here left at 0/right at 0, since only the internal face is set).
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      massFlux[face.id()] = 3.0;
    }
  }

  const ContinuityResult result = evaluateContinuity(mesh, massFlux);
  EXPECT_NEAR(result.cellImbalance[0], 3.0, 1e-14);
  EXPECT_NEAR(result.cellImbalance[1], -3.0, 1e-14);
  EXPECT_NEAR(result.globalNetFlux, 0.0, 1e-14);
}

TEST(ContinuityEquationTest, UniformChannelHasLocalBalanceEverywhere) {
  // TODO.md section 22: uniform U=(1,0) channel (inlet/outlet/walls) on
  // a uniform mesh -> continuity imbalance is exactly 0 in every cell,
  // and inlet/outlet mass flow cancel globally.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 4, 2.0, 1.0);
  BoundaryConditionSet boundaries = makeExactVelocityBoundaries(mesh, [](const Vector2&) {
    return Vector2{1.0, 0.0};
  });
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const ContinuityResult result = evaluateContinuity(mesh, massFlux);
  for (Index i = 0; i < result.cellImbalance.size(); ++i) {
    EXPECT_NEAR(result.cellImbalance[i], 0.0, 1e-12);
  }
}

TEST(ContinuityEquationTest, GlobalDivergenceTheoremIdentityHolds) {
  // TODO.md section 21: sum(cellImbalance) == sum(boundary flux) for any
  // massFlux field -- a pure algebraic/conservation identity of
  // evaluateContinuity's face-once accumulation, independent of whether
  // the velocity itself is physically sensible. Deliberately uses a
  // non-solenoidal field (see NonSolenoidalFieldIsDetected below) so
  // this test cannot pass merely because every imbalance happens to be
  // zero already.
  const Mesh mesh = perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto boundaries = makeExactVelocityBoundaries(mesh, [](const Vector2& p) {
    return Vector2{p.x, p.y};
  });
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector2{cell.centroid().x, cell.centroid().y};
  }
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const ContinuityResult result = evaluateContinuity(mesh, massFlux);
  Real sumCellImbalance = 0.0;
  for (Index i = 0; i < result.cellImbalance.size(); ++i) {
    sumCellImbalance += result.cellImbalance[i];
  }
  EXPECT_NEAR(sumCellImbalance, result.globalNetFlux, 1e-9);
}

TEST(ContinuityEquationTest, DivergenceFreeRotationalFieldGivesNearZeroImbalance) {
  // TODO.md section 24: U=(-y,x) is exactly divergence-free (linear in
  // x/y, so both the interior linear interpolation and the exact
  // per-face boundary values are exact) -> every cell imbalance is ~0.
  const Mesh mesh = perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = makeExactVelocityBoundaries(mesh, [](const Vector2& p) {
    return Vector2{-p.y, p.x};
  });
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    velocity[cell.id()] = Vector2{-c.y, c.x};
  }
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const ContinuityResult result = evaluateContinuity(mesh, massFlux);
  EXPECT_NEAR(result.maxCellImbalance, 0.0, 1e-10);
  EXPECT_NEAR(result.globalNetFlux, 0.0, 1e-10);
}

TEST(ContinuityEquationTest, NonSolenoidalFieldIsDetected) {
  // TODO.md section 25: U=(x,y) -> div(U)=2, so mass "imbalance" per
  // cell should be 2*rho*V_cell (not zero) -- this proves the evaluator
  // is not simply returning zero for every input.
  const Mesh mesh = perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = makeExactVelocityBoundaries(mesh, [](const Vector2& p) {
    return Vector2{p.x, p.y};
  });
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector2{cell.centroid().x, cell.centroid().y};
  }
  const Real rho = 3.0;
  const FluidProperties fluid(rho, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const ContinuityResult result = evaluateContinuity(mesh, massFlux);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(result.cellImbalance[cell.id()], 2.0 * rho * cell.volume(), 1e-9);
  }
  EXPECT_GT(result.totalAbsoluteImbalance, 0.0);
}

TEST(ContinuityEquationTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  EXPECT_THROW((void)evaluateContinuity(mesh, massFlux), InvalidArgumentError);
}
