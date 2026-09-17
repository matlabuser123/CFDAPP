#include "cfd/mesh/MeshGrading.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

namespace {

std::string number(Real value) {
  std::ostringstream out;
  out.precision(6);
  out << value;
  return out.str();
}

// Start-clustered geometric nodes, x_k = length * expm1(k l) / expm1(n l).
std::vector<Real> startClusteredNodes(Index cells, Real length, Real logRatio) {
  const Real total = std::expm1(static_cast<Real>(cells) * logRatio);
  std::vector<Real> nodes(cells + 1);
  for (Index k = 0; k <= cells; ++k) {
    nodes[k] = length * (std::expm1(static_cast<Real>(k) * logRatio) / total);
  }
  return nodes;
}

std::vector<Real> uniformNodes(Index cells, Real length) {
  const Real delta = length / static_cast<Real>(cells);
  std::vector<Real> nodes(cells + 1);
  for (Index k = 0; k < cells; ++k) nodes[k] = static_cast<Real>(k) * delta;
  nodes[cells] = length;
  return nodes;
}

std::vector<Real> geometricNodes(Index cells, Real length, Real ratio, GradingCluster cluster) {
  const Real ratioMinusOne = ratio - 1.0;
  const Real logRatio = std::log1p(ratioMinusOne);
  switch (cluster) {
    case GradingCluster::Start:
      return startClusteredNodes(cells, length, logRatio);
    case GradingCluster::End: {
      const std::vector<Real> start = startClusteredNodes(cells, length, logRatio);
      std::vector<Real> nodes(cells + 1);
      for (Index k = 0; k <= cells; ++k) nodes[k] = length - start[cells - k];
      nodes[0] = 0.0;  // length - length, exactly 0 already; stated for clarity
      return nodes;
    }
    case GradingCluster::Both: {
      // Widths w_0 r^min(k, n-1-k). With m = n / 2 (integer division), the
      // lower half k = 0..m is x_k = length * S(k) / T, S(k) = expm1(k l) /
      // (r - 1), T = 2 S(m) (+ r^m for odd n) -- both scaled by (r - 1).
      const Index half = cells / 2;
      const bool odd = (cells % 2) == 1;
      Real scaledTotal = 2.0 * std::expm1(static_cast<Real>(half) * logRatio);
      if (odd) scaledTotal += std::exp(static_cast<Real>(half) * logRatio) * ratioMinusOne;
      std::vector<Real> nodes(cells + 1);
      for (Index k = 0; k <= half; ++k) {
        nodes[k] = length * (std::expm1(static_cast<Real>(k) * logRatio) / scaledTotal);
      }
      for (Index k = half + 1; k <= cells; ++k) nodes[k] = length - nodes[cells - k];
      return nodes;
    }
  }
  return uniformNodes(cells, length);  // unreachable for a valid enum value
}

}  // namespace

std::vector<Real> gradedNodeCoordinates(Index cells, Real length, const AxisGrading& grading) {
  if (cells == 0) {
    throw InvalidArgumentError("mesh grading: the axis needs at least one cell");
  }
  if (!std::isfinite(length) || !(length > 0.0)) {
    throw InvalidArgumentError("mesh grading: the axis length must be finite and > 0 (got " +
                               number(length) + ")");
  }
  if (grading.type == GradingType::Uniform) {
    return uniformNodes(cells, length);
  }
  if (!std::isfinite(grading.ratio) || !(grading.ratio >= 1.0)) {
    throw InvalidArgumentError(
        "mesh grading: the geometric ratio must be finite and >= 1 (the growth factor of adjacent "
        "cell widths away from the clustered boundary; choose the side with \"cluster\") (got " +
        number(grading.ratio) + ")");
  }
  if (grading.ratio == 1.0) {
    return uniformNodes(cells, length);
  }

  const std::vector<Real> nodes = geometricNodes(cells, length, grading.ratio, grading.cluster);

  Real smallest = length;
  for (Index k = 0; k < cells; ++k) {
    if (!std::isfinite(nodes[k + 1])) {
      throw InvalidArgumentError("mesh grading: ratio " + number(grading.ratio) + " over " +
                                 std::to_string(cells) +
                                 " cells overflows (non-finite node coordinate); reduce the ratio "
                                 "or the cell count");
    }
    const Real width = nodes[k + 1] - nodes[k];
    if (!(width > 0.0)) {
      throw InvalidArgumentError("mesh grading: ratio " + number(grading.ratio) + " over " +
                                 std::to_string(cells) + " cells produces a non-positive cell " +
                                 "width at cell " + std::to_string(k) +
                                 "; reduce the ratio or the cell count");
    }
    smallest = std::min(smallest, width);
  }
  if (smallest < kMinimumRelativeCellWidth * length) {
    throw InvalidArgumentError("mesh grading: ratio " + number(grading.ratio) + " over " +
                               std::to_string(cells) + " cells makes the smallest cell " +
                               number(smallest / length) + " of the axis length (minimum " +
                               number(kMinimumRelativeCellWidth) +
                               "); reduce the ratio or the cell count");
  }
  return nodes;
}

AxisSpacing AxisSpacing::uniform(Index cells, Real length) {
  AxisSpacing spacing;
  spacing.cells_ = cells;
  spacing.delta_ = length / static_cast<Real>(cells);
  return spacing;
}

AxisSpacing AxisSpacing::fromNodes(std::vector<Real> nodes) {
  if (nodes.size() < 2) {
    throw InvalidArgumentError("AxisSpacing: at least two nodes (one cell) are required");
  }
  if (nodes.front() != 0.0) {
    throw InvalidArgumentError("AxisSpacing: the first node must be exactly 0");
  }
  for (std::size_t k = 0; k + 1 < nodes.size(); ++k) {
    if (!std::isfinite(nodes[k + 1]) || !(nodes[k + 1] > nodes[k])) {
      throw InvalidArgumentError(
          "AxisSpacing: nodes must be finite and strictly increasing (node " +
          std::to_string(k + 1) + ")");
    }
  }
  AxisSpacing spacing;
  spacing.cells_ = nodes.size() - 1;
  spacing.nodes_ = std::move(nodes);
  return spacing;
}

AxisSpacing AxisSpacing::graded(Index cells, Real length, const AxisGrading& grading) {
  if (grading.type == GradingType::Uniform || grading.ratio == 1.0) {
    // Validated like any other grading (cells, length) -- then the exact
    // Cartesian arithmetic.
    (void)gradedNodeCoordinates(cells, length, AxisGrading{});
    return uniform(cells, length);
  }
  return fromNodes(gradedNodeCoordinates(cells, length, grading));
}

Real AxisSpacing::node(Index i) const noexcept {
  return nodes_.empty() ? static_cast<Real>(i) * delta_ : nodes_[i];
}

Real AxisSpacing::center(Index i) const noexcept {
  return nodes_.empty() ? (static_cast<Real>(i) + 0.5) * delta_ : 0.5 * (nodes_[i] + nodes_[i + 1]);
}

Real AxisSpacing::width(Index i) const noexcept {
  return nodes_.empty() ? delta_ : nodes_[i + 1] - nodes_[i];
}

}  // namespace cfd::mesh
