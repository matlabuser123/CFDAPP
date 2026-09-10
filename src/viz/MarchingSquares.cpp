#include "cfd/viz/MarchingSquares.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "cfd/core/Exception.hpp"

namespace cfd::viz {

namespace {

enum class Edge { Left, Bottom, Right, Top };

using EdgePair = std::array<Edge, 2>;

std::size_t index(Index nx, Index i, Index j) {
  return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
}

bool allFinite(const std::array<Real, 4>& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::isfinite(v[3]);
}

// Linearly interpolates the crossing point of `level` along one edge,
// given that edge's two corner values/coordinates -- the two corner
// values must differ (only ever called for an edge whose corners are on
// opposite sides of `level`, per the case table below).
GridPoint interpolate(Real levelValue, Real va, Real vb, const GridPoint& pa, const GridPoint& pb) {
  const Real t = (levelValue - va) / (vb - va);
  return GridPoint{pa.x + (t * (pb.x - pa.x)), pa.y + (t * (pb.y - pa.y))};
}

// Corner-value/coordinate pair for one of the four cell edges, in a
// fixed (first, second) order matching interpolate()'s own pa/pb roles
// -- Left/Bottom go low->high in their own axis, Right/Top do too, so
// every edge's interpolation parameter t is measured the same
// consistent direction regardless of which case uses it.
GridPoint crossingPoint(Edge edge, Real levelValue, const std::array<Real, 4>& v,
                        const std::array<GridPoint, 4>& p) {
  // v/p index order: 0=BL(v00), 1=BR(v10), 2=TR(v11), 3=TL(v01).
  switch (edge) {
    case Edge::Left:
      return interpolate(levelValue, v[0], v[3], p[0], p[3]);
    case Edge::Bottom:
      return interpolate(levelValue, v[0], v[1], p[0], p[1]);
    case Edge::Right:
      return interpolate(levelValue, v[1], v[2], p[1], p[2]);
    case Edge::Top:
      return interpolate(levelValue, v[3], v[2], p[3], p[2]);
  }
  return GridPoint{};
}

// The 16-case marching-squares edge table (see this file's own header
// comment for the case-index/corner-bit convention). Cases 5 and 10 are
// the classic ambiguous saddle -- resolved by the caller comparing the
// cell's own average corner value to `level` (kept out of this table,
// which only ever describes the *unambiguous* 14 cases).
std::vector<EdgePair> edgePairsFor(int caseIndex, bool saddleAboveAverage) {
  switch (caseIndex) {
    case 0:
    case 15:
      return {};
    case 1:
    case 14:
      return {{Edge::Left, Edge::Bottom}};
    case 2:
    case 13:
      return {{Edge::Bottom, Edge::Right}};
    case 3:
    case 12:
      return {{Edge::Left, Edge::Right}};
    case 4:
    case 11:
      return {{Edge::Right, Edge::Top}};
    case 6:
    case 9:
      return {{Edge::Bottom, Edge::Top}};
    case 7:
    case 8:
      return {{Edge::Left, Edge::Top}};
    case 5:
      return saddleAboveAverage
                 ? std::vector<EdgePair>{{Edge::Bottom, Edge::Right}, {Edge::Left, Edge::Top}}
                 : std::vector<EdgePair>{{Edge::Left, Edge::Bottom}, {Edge::Right, Edge::Top}};
    case 10:
      return saddleAboveAverage
                 ? std::vector<EdgePair>{{Edge::Left, Edge::Bottom}, {Edge::Right, Edge::Top}}
                 : std::vector<EdgePair>{{Edge::Bottom, Edge::Right}, {Edge::Left, Edge::Top}};
    default:
      return {};
  }
}

}  // namespace

std::vector<ContourSegment> extractContourSegments(Index nx, Index ny,
                                                   const std::vector<Real>& values,
                                                   const std::vector<GridPoint>& coordinates,
                                                   Real level) {
  if (nx < 2 || ny < 2) {
    throw InvalidArgumentError("extractContourSegments: nx and ny must both be >= 2");
  }
  const std::size_t expected = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
  if (values.size() != expected || coordinates.size() != expected) {
    throw InvalidArgumentError(
        "extractContourSegments: values/coordinates size does not match nx*ny");
  }

  std::vector<ContourSegment> segments;
  for (Index j = 0; j + 1 < ny; ++j) {
    for (Index i = 0; i + 1 < nx; ++i) {
      const std::array<Real, 4> v{values[index(nx, i, j)], values[index(nx, i + 1, j)],
                                  values[index(nx, i + 1, j + 1)], values[index(nx, i, j + 1)]};
      if (!allFinite(v))
        continue;  // section 23: never a contour edge touching a non-finite sample.

      const std::array<GridPoint, 4> p{
          coordinates[index(nx, i, j)], coordinates[index(nx, i + 1, j)],
          coordinates[index(nx, i + 1, j + 1)], coordinates[index(nx, i, j + 1)]};

      int caseIndex = 0;
      if (v[0] > level) caseIndex |= 1;
      if (v[1] > level) caseIndex |= 2;
      if (v[2] > level) caseIndex |= 4;
      if (v[3] > level) caseIndex |= 8;

      const Real average = 0.25 * (v[0] + v[1] + v[2] + v[3]);
      for (const EdgePair& pair : edgePairsFor(caseIndex, average > level)) {
        segments.push_back(ContourSegment{crossingPoint(pair[0], level, v, p),
                                          crossingPoint(pair[1], level, v, p)});
      }
    }
  }
  return segments;
}

std::vector<Real> automaticContourLevels(const std::vector<Real>& values, Index numberOfLevels) {
  if (numberOfLevels <= 0) return {};

  Real minValue = std::numeric_limits<Real>::infinity();
  Real maxValue = -std::numeric_limits<Real>::infinity();
  for (const Real value : values) {
    if (!std::isfinite(value)) continue;
    minValue = std::min(minValue, value);
    maxValue = std::max(maxValue, value);
  }
  if (!std::isfinite(minValue) || !std::isfinite(maxValue) || minValue == maxValue) {
    return {};
  }

  std::vector<Real> levels;
  levels.reserve(static_cast<std::size_t>(numberOfLevels));
  // numberOfLevels values strictly inside (minValue, maxValue): dividing
  // the range into (numberOfLevels + 1) equal steps and taking the
  // interior step boundaries never lands exactly on either endpoint.
  const Real step = (maxValue - minValue) / static_cast<Real>(numberOfLevels + 1);
  for (Index k = 1; k <= numberOfLevels; ++k) {
    levels.push_back(minValue + (static_cast<Real>(k) * step));
  }
  return levels;
}

}  // namespace cfd::viz
