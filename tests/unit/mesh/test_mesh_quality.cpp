#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

TEST(MeshQuality, UniformSquareGridIsValidWithUnitAspectRatio) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_NEAR(report.minimumVolume, report.maximumVolume, 1e-12);
  EXPECT_GT(report.minimumFaceArea, 0.0);
  EXPECT_NEAR(report.maximumAspectRatio, 1.0, 1e-12);
}

TEST(MeshQuality, NonSquareCellsReportCorrectAspectRatio) {
  // dx = 1/2 = 0.5, dy = 1/4 = 0.25 -> aspect ratio = 0.5 / 0.25 = 2.0
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 4, 1.0, 1.0);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_NEAR(report.maximumAspectRatio, 2.0, 1e-12);
}

TEST(MeshQuality, DetectsOutOfRangeOwner) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, cfd::Vector2{0.5, 0.5}, 1.0);

  std::vector<cfd::mesh::Face> faces;
  // Owner id 5 does not exist -- only cell 0 was created.
  faces.emplace_back(0, 5, std::nullopt, cfd::Vector2{0.0, 0.5}, cfd::Vector2{-1.0, 0.0});

  const cfd::mesh::Mesh mesh(std::move(cells), std::move(faces), {});
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_FALSE(report.valid);
}
