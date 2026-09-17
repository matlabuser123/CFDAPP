#pragma once

#include <vector>

#include "cfd/core/Types.hpp"

namespace cfd::mesh {

// P12-MESH-002 -- one-dimensional cell-size grading along one axis of a
// structured mesh (the cell widths of the columns along x, or of the rows
// along y).
//
// Uniform: every width = length / cells.
//
// Geometric, `ratio` = r >= 1: adjacent cell widths grow by the factor r
// moving AWAY from the clustered boundary, so the smallest cells sit at
// that boundary:
//   cluster Start (x: "left",  y: "bottom"): w_k = w_0 r^k,           k = 0..n-1
//   cluster End   (x: "right", y: "top"):    w_k = w_0 r^(n-1-k)
//   cluster Both  (two walls, symmetric):    w_k = w_0 r^min(k, n-1-k)
// with w_0 fixed by sum_k w_k = length. For Both, an even n has two equal
// largest cells at the centre, an odd n one largest centre cell.
//
// Node coordinates (x_0 = 0 .. x_n = length) are evaluated in closed form,
// not by accumulating widths, via
//   S(k) = sum_{m<k} r^m = expm1(k log1p(r - 1)) / (r - 1),
// written as ratios of expm1() values so r -> 1 is well conditioned (it
// tends to the uniform k / n smoothly, no 0/0). Start:
// x_k = length * expm1(k l) / expm1(n l), l = log1p(r - 1); End mirrors it
// (x_k = length - x'_(n-k)); Both evaluates the lower half the same way
// with the total T = 2 S(m) (+ r^m for odd n = 2m + 1) and mirrors the
// upper half, so the distribution is exactly symmetric about length / 2.
// ratio == 1 exactly is the uniform distribution.
enum class GradingType { Uniform, Geometric };
enum class GradingCluster { Start, End, Both };

struct AxisGrading {
  GradingType type{GradingType::Uniform};
  Real ratio{1.0};
  GradingCluster cluster{GradingCluster::Both};

  bool operator==(const AxisGrading&) const = default;
};

// Smallest admissible cell width relative to the axis length: each width is
// then resolved to better than ~2e-8 relative in double precision (node
// coordinates are O(length)), so the cell geometry stays accurate.
inline constexpr Real kMinimumRelativeCellWidth = 1e-8;

// Node coordinates x_0 = 0 < x_1 < ... < x_cells = length for `grading`.
// Throws InvalidArgumentError (a message naming the violated condition)
// if cells == 0, length is not finite and > 0, a geometric ratio is not
// finite or < 1, or the generated distribution is not usable: a
// non-finite coordinate (e.g. r^cells overflows), a width that is not
// strictly positive, or a width below kMinimumRelativeCellWidth * length.
// Never repairs an input into a different distribution.
[[nodiscard]] std::vector<Real> gradedNodeCoordinates(Index cells, Real length,
                                                      const AxisGrading& grading);

// The cell widths/centres/nodes of one axis, as consumed by
// MeshGeometry::createRectilinear2D. `uniform` evaluates exactly the
// expressions MeshGeometry::createCartesian2D always used (delta =
// length / cells, node i * delta, centre (i + 0.5) * delta), so a uniform
// axis reproduces the Cartesian mesh bit for bit; `fromNodes` takes a
// validated node list (centre = (x_i + x_(i+1)) / 2, width = x_(i+1) - x_i).
class AxisSpacing {
 public:
  [[nodiscard]] static AxisSpacing uniform(Index cells, Real length);
  // Throws InvalidArgumentError unless nodes has >= 2 entries, starts at
  // exactly 0, and is finite and strictly increasing.
  [[nodiscard]] static AxisSpacing fromNodes(std::vector<Real> nodes);
  // `grading` realised: uniform() for a Uniform axis or a ratio of exactly
  // 1, fromNodes(gradedNodeCoordinates(...)) otherwise.
  [[nodiscard]] static AxisSpacing graded(Index cells, Real length, const AxisGrading& grading);

  [[nodiscard]] Index cells() const noexcept { return cells_; }
  [[nodiscard]] Real node(Index i) const noexcept;
  [[nodiscard]] Real center(Index i) const noexcept;
  [[nodiscard]] Real width(Index i) const noexcept;
  [[nodiscard]] bool isUniform() const noexcept { return nodes_.empty(); }

 private:
  AxisSpacing() = default;
  Index cells_{0};
  Real delta_{0.0};
  std::vector<Real> nodes_;  // empty for uniform
};

}  // namespace cfd::mesh
