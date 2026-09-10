// P5-E -- Vector Plots, section 25: "every Nth cell" subsampling.
#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/viz/VectorSampling.hpp"

using cfd::Vector2;
using cfd::fields::VectorField;
using cfd::mesh::MeshGeometry;
using cfd::viz::sampleVectorField;

TEST(SampleVectorFieldTest, StrideOneSamplesEveryCell) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField velocity(static_cast<cfd::Index>(mesh.numberOfCells()), Vector2{1.0, 0.0});
  const auto samples = sampleVectorField(mesh, velocity, 1);
  EXPECT_EQ(samples.size(), mesh.numberOfCells());
}

TEST(SampleVectorFieldTest, StrideNSamplesEveryNthCellById) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);  // 16 cells.
  const VectorField velocity(static_cast<cfd::Index>(mesh.numberOfCells()), Vector2{2.0, -1.0});
  const auto samples = sampleVectorField(mesh, velocity, 4);
  EXPECT_EQ(samples.size(), 4u);  // ids 0, 4, 8, 12.
  for (const auto& sample : samples) {
    EXPECT_DOUBLE_EQ(sample.vector.x, 2.0);
    EXPECT_DOUBLE_EQ(sample.vector.y, -1.0);
  }
}

TEST(SampleVectorFieldTest, RejectsZeroStride) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField velocity(static_cast<cfd::Index>(mesh.numberOfCells()));
  EXPECT_THROW((void)sampleVectorField(mesh, velocity, 0), cfd::InvalidArgumentError);
}

TEST(SampleVectorFieldTest, MismatchedSizeThrows) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField velocity(1);
  EXPECT_THROW((void)sampleVectorField(mesh, velocity, 1), cfd::InvalidArgumentError);
}

TEST(SampleVectorFieldRawTest, MatchesMeshBasedSamplingOnEquivalentData) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField velocity(static_cast<cfd::Index>(mesh.numberOfCells()), Vector2{2.0, -1.0});

  std::vector<Vector2> points;
  std::vector<cfd::Real> vx, vy;
  for (const auto& cell : mesh.cells()) {
    points.push_back(cell.centroid());
    vx.push_back(velocity[cell.id()].x);
    vy.push_back(velocity[cell.id()].y);
  }

  const auto meshSamples = cfd::viz::sampleVectorField(mesh, velocity, 4);
  const auto rawSamples = cfd::viz::sampleVectorFieldRaw(points, vx, vy, 4);
  ASSERT_EQ(meshSamples.size(), rawSamples.size());
  for (std::size_t i = 0; i < meshSamples.size(); ++i) {
    EXPECT_DOUBLE_EQ(meshSamples[i].vector.x, rawSamples[i].vector.x);
    EXPECT_DOUBLE_EQ(meshSamples[i].vector.y, rawSamples[i].vector.y);
  }
}

TEST(SampleVectorFieldRawTest, RejectsZeroStride) {
  EXPECT_THROW((void)cfd::viz::sampleVectorFieldRaw({}, {}, {}, 0), cfd::InvalidArgumentError);
}
