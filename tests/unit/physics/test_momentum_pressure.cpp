#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assemblePressureSourceContribution;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// One singleton boundary patch per face, each given the *exact*
// analytical pressure value at that face -- needed because p=x/p=y vary
// along the top/bottom or left/right patches respectively, so a single
// FixedValue per side (using e.g. only its first face's coordinate)
// would not actually reproduce a linear field. Same technique as
// tests/unit/physics/test_continuity_equation.cpp's perFaceBoundaryMesh.
Mesh perFaceBoundaryMesh(Index nx, Index ny, Real lengthX, Real lengthY) {
  const Mesh base = MeshGeometry::createCartesian2D(nx, ny, lengthX, lengthY);
  std::vector<cfd::mesh::Cell> cells = base.cells();
  std::vector<cfd::mesh::Face> faces = base.faces();

  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }
  return Mesh(std::move(cells), std::move(faces), std::move(patches));
}

template <typename PressureFunction>
BoundaryConditionSet makeExactPressureBoundaries(const Mesh& mesh, PressureFunction p) {
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    bcs.set(mesh, patch.name(), std::make_unique<FixedValue>(p(mesh.face(faceId).centroid())));
  }
  return bcs;
}

}  // namespace

TEST(MomentumPressureTest, LinearPressureXGivesNegativeXSourceZeroYSource) {
  // TODO.md section 27: p=x -> grad(p)=(1,0) -> x source = -V, y source = 0.
  const Mesh mesh = perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto boundaries = makeExactPressureBoundaries(mesh, [](const Vector2& p) { return p.x; });
  ScalarField pressure(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) pressure[cell.id()] = cell.centroid().x;

  Vector rhsU(mesh.numberOfCells(), 0.0);
  Vector rhsV(mesh.numberOfCells(), 0.0);
  assemblePressureSourceContribution(mesh, pressure, boundaries, VelocityComponent::U, rhsU);
  assemblePressureSourceContribution(mesh, pressure, boundaries, VelocityComponent::V, rhsV);

  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(rhsU[cell.id()], -cell.volume(), 1e-9);
    EXPECT_NEAR(rhsV[cell.id()], 0.0, 1e-9);
  }
}

TEST(MomentumPressureTest, LinearPressureYGivesNegativeYSourceZeroXSource) {
  // TODO.md section 28: p=y -> x source = 0, y source = -V.
  const Mesh mesh = perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto boundaries = makeExactPressureBoundaries(mesh, [](const Vector2& p) { return p.y; });
  ScalarField pressure(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) pressure[cell.id()] = cell.centroid().y;

  Vector rhsU(mesh.numberOfCells(), 0.0);
  Vector rhsV(mesh.numberOfCells(), 0.0);
  assemblePressureSourceContribution(mesh, pressure, boundaries, VelocityComponent::U, rhsU);
  assemblePressureSourceContribution(mesh, pressure, boundaries, VelocityComponent::V, rhsV);

  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(rhsU[cell.id()], 0.0, 1e-9);
    EXPECT_NEAR(rhsV[cell.id()], -cell.volume(), 1e-9);
  }
}

TEST(MomentumPressureTest, ConstantPressureGivesZeroSource) {
  // TODO.md section 29: p=const -> grad(p)=0 -> zero source everywhere.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientPressureBoundaries(mesh);
  const ScalarField pressure(mesh.numberOfCells(), 42.0);

  Vector rhs(mesh.numberOfCells(), 0.0);
  assemblePressureSourceContribution(mesh, pressure, boundaries, VelocityComponent::U, rhs);
  for (Index i = 0; i < rhs.size(); ++i) {
    EXPECT_NEAR(rhs[i], 0.0, 1e-9);
  }
}

TEST(MomentumPressureTest, AddingConstantToPressureDoesNotChangeSource) {
  // TODO.md section 67: pressure gauge freedom -- p and p+C must produce
  // the same momentum pressure-gradient contribution.
  const Mesh mesh = perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto boundariesLow =
      makeExactPressureBoundaries(mesh, [](const Vector2& p) { return p.x; });
  const auto boundariesHigh =
      makeExactPressureBoundaries(mesh, [](const Vector2& p) { return p.x + 1000.0; });
  ScalarField pressureLow(mesh.numberOfCells());
  ScalarField pressureHigh(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    pressureLow[cell.id()] = cell.centroid().x;
    pressureHigh[cell.id()] = cell.centroid().x + 1000.0;
  }

  Vector rhsLow(mesh.numberOfCells(), 0.0);
  Vector rhsHigh(mesh.numberOfCells(), 0.0);
  assemblePressureSourceContribution(mesh, pressureLow, boundariesLow, VelocityComponent::U,
                                     rhsLow);
  assemblePressureSourceContribution(mesh, pressureHigh, boundariesHigh, VelocityComponent::U,
                                     rhsHigh);

  for (Index i = 0; i < rhsLow.size(); ++i) {
    EXPECT_NEAR(rhsLow[i], rhsHigh[i], 1e-8);
  }
}

TEST(MomentumPressureTest, MismatchedPressureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientPressureBoundaries(mesh);
  const ScalarField pressure(mesh.numberOfCells() + 1, 0.0);
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(
      assemblePressureSourceContribution(mesh, pressure, boundaries, VelocityComponent::U, rhs),
      InvalidArgumentError);
}
