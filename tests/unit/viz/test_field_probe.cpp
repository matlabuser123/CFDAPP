// P5-G -- Post-processing, sections 31-32: probe (nearest-cell point
// inspection) and line sampling, tested against a known synthetic field
// on a real structured mesh (cfd::mesh::MeshGeometry::createCartesian2D)
// so the nearest-cell answer is independently checkable by hand.
#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/viz/FieldProbe.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::mesh::MeshGeometry;
using cfd::viz::nearestCell;
using cfd::viz::nearestIndex;
using cfd::viz::probeScalar;
using cfd::viz::probeScalarRaw;
using cfd::viz::sampleLine;
using cfd::viz::sampleLineRaw;

namespace {

// 4x4 cells over [0,1]x[0,1] -- cell centroids at (0.125+0.25*i,
// 0.125+0.25*j).
ScalarField xCoordinateField(const cfd::mesh::Mesh& mesh) {
  ScalarField field(static_cast<cfd::Index>(mesh.numberOfCells()));
  for (const auto& cell : mesh.cells()) field[cell.id()] = cell.centroid().x;
  return field;
}

}  // namespace

TEST(NearestCellTest, FindsTheCellWhoseCentroidIsClosest) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto id = nearestCell(mesh, Vector2{0.1, 0.1});
  ASSERT_TRUE(id.has_value());
  EXPECT_NEAR(mesh.cell(*id).centroid().x, 0.125, 1e-9);
  EXPECT_NEAR(mesh.cell(*id).centroid().y, 0.125, 1e-9);
}

TEST(ProbeScalarTest, ReturnsTheNearestCellsValue) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto field = xCoordinateField(mesh);
  EXPECT_NEAR(probeScalar(mesh, field, Vector2{0.1, 0.9}), 0.125, 1e-9);
  EXPECT_NEAR(probeScalar(mesh, field, Vector2{0.9, 0.1}), 0.875, 1e-9);
}

TEST(ProbeScalarTest, MismatchedFieldSizeThrows) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  ScalarField field(1);
  EXPECT_THROW((void)probeScalar(mesh, field, Vector2{0.0, 0.0}), cfd::InvalidArgumentError);
}

TEST(SampleLineTest, HorizontalLineTracksTheXCoordinateField) {
  const auto mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto field = xCoordinateField(mesh);

  const auto samples = sampleLine(mesh, field, Vector2{0.0, 0.5}, Vector2{1.0, 0.5}, 5);
  ASSERT_EQ(samples.size(), 5u);
  EXPECT_DOUBLE_EQ(samples.front().queryPoint.x, 0.0);
  EXPECT_DOUBLE_EQ(samples.back().queryPoint.x, 1.0);
  // The field is exactly the x-coordinate -- so each sample's own value
  // should track its query point's x monotonically nondecreasing.
  for (std::size_t i = 1; i < samples.size(); ++i) {
    EXPECT_GE(samples[i].value, samples[i - 1].value);
  }
}

TEST(ProbeScalarRawTest, MatchesMeshBasedProbeOnEquivalentData) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto field = xCoordinateField(mesh);

  std::vector<Vector2> points;
  std::vector<Real> values;
  for (const auto& cell : mesh.cells()) {
    points.push_back(cell.centroid());
    values.push_back(field[cell.id()]);
  }

  EXPECT_NEAR(probeScalarRaw(points, values, Vector2{0.1, 0.9}), 0.125, 1e-9);
  EXPECT_EQ(probeScalarRaw(points, values, Vector2{0.9, 0.1}), probeScalar(mesh, field, Vector2{0.9, 0.1}));
}

TEST(ProbeScalarRawTest, RejectsMismatchedSizes) {
  EXPECT_THROW((void)probeScalarRaw({Vector2{0, 0}}, {}, Vector2{0, 0}), cfd::InvalidArgumentError);
}

TEST(SampleLineRawTest, MatchesMeshBasedSampleLineOnEquivalentData) {
  const auto mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto field = xCoordinateField(mesh);

  std::vector<Vector2> points;
  std::vector<Real> values;
  for (const auto& cell : mesh.cells()) {
    points.push_back(cell.centroid());
    values.push_back(field[cell.id()]);
  }

  const auto meshSamples = sampleLine(mesh, field, Vector2{0.0, 0.5}, Vector2{1.0, 0.5}, 5);
  const auto rawSamples = sampleLineRaw(points, values, Vector2{0.0, 0.5}, Vector2{1.0, 0.5}, 5);
  ASSERT_EQ(meshSamples.size(), rawSamples.size());
  for (std::size_t i = 0; i < meshSamples.size(); ++i) {
    EXPECT_DOUBLE_EQ(meshSamples[i].value, rawSamples[i].value);
  }
}

TEST(NearestIndexTest, EmptyPointsReturnsNullopt) {
  EXPECT_FALSE(nearestIndex({}, Vector2{0, 0}).has_value());
}

TEST(SampleLineTest, RejectsTooFewSamples) {
  const auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto field = xCoordinateField(mesh);
  EXPECT_THROW((void)sampleLine(mesh, field, Vector2{0.0, 0.0}, Vector2{1.0, 0.0}, 1),
              cfd::InvalidArgumentError);
}
