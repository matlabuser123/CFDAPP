// P5-D -- Contours, section 22: the task's own worked example --
// phi(x,y) = x, a contour at phi = 0.5 should be a vertical line at
// x = 0.5 -- plus the edge cases section 23 explicitly calls out
// (constant field, NaN, and repeatability/determinism).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "cfd/core/Exception.hpp"
#include "cfd/viz/MarchingSquares.hpp"

using cfd::viz::ContourSegment;
using cfd::viz::extractContourSegments;
using cfd::viz::GridPoint;

namespace {

// A uniform nx*ny grid over [0,1]x[0,1] with values[j*nx+i] = fn(x, y).
struct SyntheticGrid {
  cfd::Index nx;
  cfd::Index ny;
  std::vector<cfd::Real> values;
  std::vector<GridPoint> coordinates;
};

template <typename Fn>
SyntheticGrid makeGrid(cfd::Index nx, cfd::Index ny, Fn fn) {
  SyntheticGrid grid{nx, ny, {}, {}};
  grid.values.resize(static_cast<std::size_t>(nx * ny));
  grid.coordinates.resize(static_cast<std::size_t>(nx * ny));
  for (cfd::Index j = 0; j < ny; ++j) {
    for (cfd::Index i = 0; i < nx; ++i) {
      const cfd::Real x = static_cast<cfd::Real>(i) / static_cast<cfd::Real>(nx - 1);
      const cfd::Real y = static_cast<cfd::Real>(j) / static_cast<cfd::Real>(ny - 1);
      const std::size_t idx = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                              static_cast<std::size_t>(i);
      grid.values[idx] = fn(x, y);
      grid.coordinates[idx] = GridPoint{x, y};
    }
  }
  return grid;
}

}  // namespace

TEST(MarchingSquaresTest, LinearFieldPhiEqualsXProducesAVerticalLineAtTheLevel) {
  const auto grid = makeGrid(5, 5, [](cfd::Real x, cfd::Real) { return x; });
  const auto segments = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 0.5);

  ASSERT_FALSE(segments.empty());
  for (const ContourSegment& segment : segments) {
    EXPECT_NEAR(segment.start.x, 0.5, 1e-9);
    EXPECT_NEAR(segment.end.x, 0.5, 1e-9);
  }
  // The vertical line spans the full [0,1] y-range -- every segment's y
  // extent, taken together, covers it (4 grid rows -> 4 unit-length
  // segments each spanning 0.25 of y).
  cfd::Real minY = 1.0;
  cfd::Real maxY = 0.0;
  for (const ContourSegment& segment : segments) {
    minY = std::min({minY, segment.start.y, segment.end.y});
    maxY = std::max({maxY, segment.start.y, segment.end.y});
  }
  EXPECT_NEAR(minY, 0.0, 1e-9);
  EXPECT_NEAR(maxY, 1.0, 1e-9);
}

TEST(MarchingSquaresTest, LinearFieldPhiEqualsYProducesAHorizontalLine) {
  const auto grid = makeGrid(4, 4, [](cfd::Real, cfd::Real y) { return y; });
  const auto segments = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 0.5);

  ASSERT_FALSE(segments.empty());
  for (const ContourSegment& segment : segments) {
    EXPECT_NEAR(segment.start.y, 0.5, 1e-9);
    EXPECT_NEAR(segment.end.y, 0.5, 1e-9);
  }
}

TEST(MarchingSquaresTest, ConstantFieldProducesNoSegments) {
  const auto grid = makeGrid(4, 4, [](cfd::Real, cfd::Real) { return 3.0; });
  const auto segments = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 3.0);
  EXPECT_TRUE(segments.empty());
}

TEST(MarchingSquaresTest, LevelOutsideRangeProducesNoSegments) {
  const auto grid = makeGrid(4, 4, [](cfd::Real x, cfd::Real) { return x; });
  const auto segments = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 5.0);
  EXPECT_TRUE(segments.empty());
}

TEST(MarchingSquaresTest, NonFiniteCellsContributeNoSegments) {
  auto grid = makeGrid(3, 3, [](cfd::Real x, cfd::Real) { return x; });
  grid.values[4] = std::nan("");  // center cell.
  const auto segments = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 0.5);
  for (const auto& segment : segments) {
    EXPECT_TRUE(std::isfinite(segment.start.x));
    EXPECT_TRUE(std::isfinite(segment.start.y));
    EXPECT_TRUE(std::isfinite(segment.end.x));
    EXPECT_TRUE(std::isfinite(segment.end.y));
  }
}

TEST(MarchingSquaresTest, RepeatedCallsAreDeterministic) {
  const auto grid = makeGrid(6, 6, [](cfd::Real x, cfd::Real y) { return (x * x) + (y * y); });
  const auto first = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 0.5);
  const auto second = extractContourSegments(grid.nx, grid.ny, grid.values, grid.coordinates, 0.5);

  ASSERT_EQ(first.size(), second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].start.x, second[i].start.x);
    EXPECT_EQ(first[i].start.y, second[i].start.y);
    EXPECT_EQ(first[i].end.x, second[i].end.x);
    EXPECT_EQ(first[i].end.y, second[i].end.y);
  }
}

TEST(MarchingSquaresTest, RejectsGridTooSmall) {
  std::vector<cfd::Real> values{1.0};
  std::vector<GridPoint> coordinates{GridPoint{0.0, 0.0}};
  EXPECT_THROW((void)extractContourSegments(1, 1, values, coordinates, 0.5), cfd::InvalidArgumentError);
}

TEST(MarchingSquaresTest, RejectsMismatchedSizes) {
  std::vector<cfd::Real> values{1.0, 2.0};
  std::vector<GridPoint> coordinates{GridPoint{0.0, 0.0}, GridPoint{1.0, 0.0}};
  EXPECT_THROW((void)extractContourSegments(2, 2, values, coordinates, 0.5), cfd::InvalidArgumentError);
}

TEST(AutomaticContourLevelsTest, ProducesEvenlySpacedInteriorLevels) {
  const std::vector<cfd::Real> values{0.0, 10.0, 5.0};
  const auto levels = cfd::viz::automaticContourLevels(values, 3);
  ASSERT_EQ(levels.size(), 3u);
  for (cfd::Real level : levels) {
    EXPECT_GT(level, 0.0);
    EXPECT_LT(level, 10.0);
  }
  EXPECT_LT(levels[0], levels[1]);
  EXPECT_LT(levels[1], levels[2]);
}

TEST(AutomaticContourLevelsTest, ConstantFieldProducesNoLevels) {
  const std::vector<cfd::Real> values{4.0, 4.0, 4.0};
  EXPECT_TRUE(cfd::viz::automaticContourLevels(values, 5).empty());
}

TEST(AutomaticContourLevelsTest, IgnoresNonFiniteSamples) {
  const std::vector<cfd::Real> values{0.0, std::nan(""), 10.0};
  const auto levels = cfd::viz::automaticContourLevels(values, 1);
  ASSERT_EQ(levels.size(), 1u);
  EXPECT_TRUE(std::isfinite(levels[0]));
}
