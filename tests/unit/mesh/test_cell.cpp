#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/Cell.hpp"

TEST(MeshCell, StoresIdCentroidAndVolume) {
  const cfd::mesh::Cell cell(5, cfd::Vector2{1.5, 2.5}, 0.25);

  EXPECT_EQ(cell.id(), 5U);
  EXPECT_DOUBLE_EQ(cell.centroid().x, 1.5);
  EXPECT_DOUBLE_EQ(cell.centroid().y, 2.5);
  EXPECT_DOUBLE_EQ(cell.volume(), 0.25);
  EXPECT_TRUE(cell.faceIds().empty());
}

TEST(MeshCell, AddFaceAppendsFaceIds) {
  cfd::mesh::Cell cell(0, cfd::Vector2{0.0, 0.0}, 1.0);
  cell.addFace(3);
  cell.addFace(7);

  ASSERT_EQ(cell.faceIds().size(), 2U);
  EXPECT_EQ(cell.faceIds()[0], 3U);
  EXPECT_EQ(cell.faceIds()[1], 7U);
}

TEST(MeshCell, RejectsNonPositiveVolume) {
  EXPECT_THROW(cfd::mesh::Cell(0, (cfd::Vector2{0.0, 0.0}), 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::mesh::Cell(0, (cfd::Vector2{0.0, 0.0}), -1.0), cfd::InvalidArgumentError);
}

TEST(MeshCell, RejectsNonFiniteGeometry) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();

  EXPECT_THROW(cfd::mesh::Cell(0, (cfd::Vector2{nan, 0.0}), 1.0), cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::mesh::Cell(0, (cfd::Vector2{0.0, 0.0}), inf), cfd::InvalidArgumentError);
}
