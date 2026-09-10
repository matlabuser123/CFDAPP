// P5-G -- Post-processing, section 30: derived fields tested against a
// known analytical velocity field (solid-body rotation, u=-y, v=x),
// whose exact 2D vorticity is the constant omega_z = dv/dx - du/dy =
// 1-(-1) = 2 everywhere -- not just "looks plausible".
#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/viz/DerivedFields.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::MeshGeometry;
using cfd::viz::velocityMagnitude;
using cfd::viz::vorticity2D;

TEST(FieldStatisticsTest, ComputesMinMaxAverageIgnoringNonFinite) {
  ScalarField field(4);
  field[0] = 1.0;
  field[1] = 5.0;
  field[2] = 3.0;
  field[3] = std::numeric_limits<Real>::quiet_NaN();
  const auto stats = cfd::viz::computeFieldStatistics(field);
  EXPECT_TRUE(stats.hasData);
  EXPECT_DOUBLE_EQ(stats.minimum, 1.0);
  EXPECT_DOUBLE_EQ(stats.maximum, 5.0);
  EXPECT_DOUBLE_EQ(stats.average, 3.0);  // (1+5+3)/3, NaN excluded.
}

TEST(FieldStatisticsTest, EmptyFieldReportsNoData) {
  const auto stats = cfd::viz::computeFieldStatistics(ScalarField(0));
  EXPECT_FALSE(stats.hasData);
}

TEST(VelocityMagnitudeTest, MatchesPythagoreanFormula) {
  VectorField velocity(2);
  velocity[0] = Vector2{3.0, 4.0};
  velocity[1] = Vector2{0.0, 0.0};
  const auto magnitude = velocityMagnitude(velocity);
  EXPECT_DOUBLE_EQ(magnitude[0], 5.0);
  EXPECT_DOUBLE_EQ(magnitude[1], 0.0);
}

TEST(Vorticity2DTest, SolidBodyRotationMatchesAnalyticalConstantAwayFromBoundaries) {
  const auto mesh = MeshGeometry::createCartesian2D(20, 20, 2.0, 2.0);  // [0,2]x[0,2].
  VectorField velocity(static_cast<cfd::Index>(mesh.numberOfCells()));
  for (const auto& cell : mesh.cells()) {
    // Centered at the domain's own center (1,1) so "interior" cells are
    // symmetric on all sides -- u = -(y-1), v = (x-1).
    const Real x = cell.centroid().x - 1.0;
    const Real y = cell.centroid().y - 1.0;
    velocity[cell.id()] = Vector2{-y, x};
  }

  const auto vorticity = vorticity2D(mesh, velocity);

  // Interior cells (this diagnostic's own boundary-face simplification,
  // documented in DerivedFields.hpp, only affects accuracy near the
  // domain edge) -- a structured 20x20 grid's own row-major cell-id
  // convention (cell id = j*nx+i) lets a plain 3-cell interior margin be
  // selected by id arithmetic without needing a separate nx/ny query.
  constexpr cfd::Index nx = 20;
  int interiorChecked = 0;
  for (const auto& cell : mesh.cells()) {
    const cfd::Index i = cell.id() % nx;
    const cfd::Index j = cell.id() / nx;
    if (i < 3 || i >= nx - 3 || j < 3 || j >= nx - 3) continue;
    EXPECT_NEAR(vorticity[cell.id()], 2.0, 0.05);
    ++interiorChecked;
  }
  EXPECT_GT(interiorChecked, 0);
}

TEST(Vorticity2DTest, UniformFlowHasZeroVorticity) {
  const auto mesh = MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0);
  VectorField velocity(static_cast<cfd::Index>(mesh.numberOfCells()), Vector2{1.5, -0.5});
  const auto vorticity = vorticity2D(mesh, velocity);
  for (cfd::Index i = 0; i < vorticity.size(); ++i) {
    EXPECT_NEAR(vorticity[i], 0.0, 1e-9);
  }
}

TEST(Vorticity2DTest, MismatchedSizeThrows) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  VectorField velocity(1);
  EXPECT_THROW((void)vorticity2D(mesh, velocity), cfd::InvalidArgumentError);
}
