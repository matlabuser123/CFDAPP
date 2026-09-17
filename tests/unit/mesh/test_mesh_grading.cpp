// P12-MESH-002: cfd::mesh::gradedNodeCoordinates / AxisSpacing -- the
// one-dimensional cell-size grading. Verification against distributions
// computable by hand (integer ratios give integer widths), the invariants
// every distribution must satisfy, and the rejection of unusable input.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGrading.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::mesh::AxisGrading;
using cfd::mesh::AxisSpacing;
using cfd::mesh::gradedNodeCoordinates;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;

namespace {

AxisGrading geometric(Real ratio, GradingCluster cluster) {
  return AxisGrading{GradingType::Geometric, ratio, cluster};
}

void expectNodes(const std::vector<Real>& nodes, const std::vector<Real>& expected, Real tol) {
  ASSERT_EQ(nodes.size(), expected.size());
  for (std::size_t k = 0; k < nodes.size(); ++k) EXPECT_NEAR(nodes[k], expected[k], tol) << k;
}

std::string messageOf(Index cells, Real length, const AxisGrading& grading) {
  try {
    (void)gradedNodeCoordinates(cells, length, grading);
  } catch (const InvalidArgumentError& e) {
    return e.what();
  }
  return "";
}

}  // namespace

// --- Distributions computable by hand -------------------------------------------

TEST(MeshGrading, UniformIsEqualWidths) {
  expectNodes(gradedNodeCoordinates(4, 2.0, AxisGrading{}), {0.0, 0.5, 1.0, 1.5, 2.0}, 0.0);
}

// r = 2 over 4 cells, length 15: widths 1, 2, 4, 8.
TEST(MeshGrading, OneSidedStartClusteringMatchesGeometricSeries) {
  expectNodes(gradedNodeCoordinates(4, 15.0, geometric(2.0, GradingCluster::Start)),
              {0.0, 1.0, 3.0, 7.0, 15.0}, 1e-13);
}

// The mirror: widths 8, 4, 2, 1.
TEST(MeshGrading, OneSidedEndClusteringMirrorsStart) {
  expectNodes(gradedNodeCoordinates(4, 15.0, geometric(2.0, GradingCluster::End)),
              {0.0, 8.0, 12.0, 14.0, 15.0}, 1e-13);
}

// Even count, r = 2 over 6 cells, length 14: widths 1, 2, 4, 4, 2, 1 (two
// equal largest cells at the centre).
TEST(MeshGrading, TwoSidedClusteringEvenCellCount) {
  expectNodes(gradedNodeCoordinates(6, 14.0, geometric(2.0, GradingCluster::Both)),
              {0.0, 1.0, 3.0, 7.0, 11.0, 13.0, 14.0}, 1e-13);
}

// Odd count, r = 3 over 5 cells, length 17: widths 1, 3, 9, 3, 1 (one
// largest centre cell).
TEST(MeshGrading, TwoSidedClusteringOddCellCount) {
  expectNodes(gradedNodeCoordinates(5, 17.0, geometric(3.0, GradingCluster::Both)),
              {0.0, 1.0, 4.0, 13.0, 16.0, 17.0}, 1e-13);
}

// --- Invariants over many distributions -------------------------------------------

TEST(MeshGrading, InvariantsHoldForEveryDistribution) {
  for (const Index cells :
       {Index{1}, Index{2}, Index{3}, Index{7}, Index{16}, Index{25}, Index{64}}) {
    for (const Real length : {1.0, 0.05, 8.0, 123.456}) {
      for (const Real ratio : {1.0 + 1e-12, 1.0001, 1.05, 1.2, 1.5}) {
        for (const auto cluster :
             {GradingCluster::Start, GradingCluster::End, GradingCluster::Both}) {
          // Distributions whose smallest cell falls below
          // kMinimumRelativeCellWidth are rejected by design (tested below).
          if (std::pow(ratio, static_cast<Real>(cells)) > 1e6) continue;
          const auto nodes = gradedNodeCoordinates(cells, length, geometric(ratio, cluster));
          ASSERT_EQ(nodes.size(), cells + 1);
          EXPECT_EQ(nodes.front(), 0.0);    // exactly the domain minimum
          EXPECT_EQ(nodes.back(), length);  // exactly the domain maximum
          Real sum = 0.0;
          for (Index k = 0; k < cells; ++k) {
            const Real w = nodes[k + 1] - nodes[k];
            ASSERT_TRUE(std::isfinite(nodes[k + 1]));
            ASSERT_GT(w, 0.0) << cells << " " << ratio;
            sum += w;
          }
          EXPECT_NEAR(sum, length, 1e-13 * length);
          // Width of cell k is w_0 r^e(k), e(k) = k (Start), n-1-k (End),
          // min(k, n-1-k) (Both): adjacent ratio r^(e(k+1) - e(k)).
          const auto exponent = [&](Index k) -> int {
            const Index mirrored = cells - 1 - k;
            if (cluster == GradingCluster::Start) return static_cast<int>(k);
            if (cluster == GradingCluster::End) return static_cast<int>(mirrored);
            return static_cast<int>(std::min(k, mirrored));
          };
          for (Index k = 0; k + 1 < cells; ++k) {
            const Real a = nodes[k + 1] - nodes[k];
            const Real b = nodes[k + 2] - nodes[k + 1];
            const Real expected = std::pow(ratio, exponent(k + 1) - exponent(k));
            EXPECT_NEAR(b / a, expected, 1e-9) << cells << " r " << ratio << " k " << k;
          }
          if (cluster == GradingCluster::Both) {
            for (Index k = 0; k <= cells; ++k) {
              EXPECT_NEAR(nodes[k] + nodes[cells - k], length, 1e-14 * length);  // symmetric
            }
          }
        }
      }
    }
  }
}

// ratio exactly 1 is the uniform distribution, bit for bit.
TEST(MeshGrading, RatioOneIsExactlyUniform) {
  for (const auto cluster : {GradingCluster::Start, GradingCluster::End, GradingCluster::Both}) {
    EXPECT_EQ(gradedNodeCoordinates(9, 3.7, geometric(1.0, cluster)),
              gradedNodeCoordinates(9, 3.7, AxisGrading{}));
  }
}

// r -> 1 is well conditioned (expm1/log1p), tending smoothly to uniform.
TEST(MeshGrading, NearUnityRatioTendsToUniform) {
  const auto uniform = gradedNodeCoordinates(40, 2.0, AxisGrading{});
  for (const Real ratio : {1.0 + 1e-15, 1.0 + 1e-12, 1.0 + 1e-9}) {
    for (const auto cluster : {GradingCluster::Start, GradingCluster::End, GradingCluster::Both}) {
      const auto nodes = gradedNodeCoordinates(40, 2.0, geometric(ratio, cluster));
      for (Index k = 0; k <= 40; ++k) {
        // Exact deviation: x_k - L k/n ~ L (r - 1) (k/n)(k - n)/2, at most
        // L (r - 1) n / 8; bound L (r - 1) n plus a round-off floor.
        EXPECT_NEAR(nodes[k], uniform[k], (2.0 * (ratio - 1.0) * 40.0) + 1e-13)
            << ratio << " " << k;
      }
    }
  }
}

TEST(MeshGrading, ConstructionIsDeterministic) {
  const AxisGrading g = geometric(1.137, GradingCluster::Both);
  EXPECT_EQ(gradedNodeCoordinates(37, 0.73, g), gradedNodeCoordinates(37, 0.73, g));
}

// --- Rejection of unusable input -------------------------------------------------

TEST(MeshGrading, RejectsZeroCells) {
  EXPECT_NE(messageOf(0, 1.0, AxisGrading{}).find("at least one cell"), std::string::npos);
}

TEST(MeshGrading, RejectsNonPositiveOrNonFiniteLength) {
  for (const Real length :
       {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    EXPECT_NE(messageOf(8, length, geometric(1.2, GradingCluster::Start)).find("axis length"),
              std::string::npos)
        << length;
  }
}

TEST(MeshGrading, RejectsInvalidRatios) {
  for (const Real ratio : {0.0, -1.2, 0.5, 0.999999, std::numeric_limits<Real>::quiet_NaN(),
                           std::numeric_limits<Real>::infinity()}) {
    EXPECT_NE(messageOf(8, 1.0, geometric(ratio, GradingCluster::Both)).find("finite and >= 1"),
              std::string::npos)
        << ratio;
  }
}

// r^n overflowing (10^400) is rejected, not turned into NaN/Inf nodes.
TEST(MeshGrading, RejectsOverflowingRatio) {
  const std::string message = messageOf(400, 1.0, geometric(10.0, GradingCluster::Start));
  EXPECT_FALSE(message.empty());
  EXPECT_NE(message.find("reduce the ratio or the cell count"), std::string::npos) << message;
}

// A usable-looking ratio over many cells shrinks the smallest cell below
// kMinimumRelativeCellWidth (2^-40 ~ 9e-13 of the length): rejected, with
// the offending size in the message.
TEST(MeshGrading, RejectsVanishingCells) {
  const std::string message = messageOf(40, 1.0, geometric(2.0, GradingCluster::Start));
  EXPECT_NE(message.find("smallest cell"), std::string::npos) << message;
  EXPECT_NE(message.find("reduce the ratio or the cell count"), std::string::npos) << message;
  // Just inside the limit is accepted: 2^-26 ~ 1.5e-8.
  EXPECT_NO_THROW((void)gradedNodeCoordinates(26, 1.0, geometric(2.0, GradingCluster::Start)));
}

// --- AxisSpacing -----------------------------------------------------------------

// The uniform spacing evaluates exactly the Cartesian expressions
// (delta = L / n, node i * delta, centre (i + 0.5) * delta).
TEST(AxisSpacing, UniformUsesTheCartesianArithmetic) {
  const auto s = AxisSpacing::uniform(7, 0.3);
  const Real delta = 0.3 / 7.0;
  EXPECT_TRUE(s.isUniform());
  for (Index i = 0; i < 7; ++i) {
    EXPECT_EQ(s.node(i), static_cast<Real>(i) * delta);
    EXPECT_EQ(s.center(i), (static_cast<Real>(i) + 0.5) * delta);
    EXPECT_EQ(s.width(i), delta);
  }
}

TEST(AxisSpacing, GradedUniformOrUnitRatioIsTheUniformSpacing) {
  EXPECT_TRUE(AxisSpacing::graded(5, 1.0, AxisGrading{}).isUniform());
  EXPECT_TRUE(AxisSpacing::graded(5, 1.0, geometric(1.0, GradingCluster::End)).isUniform());
  EXPECT_FALSE(AxisSpacing::graded(5, 1.0, geometric(1.1, GradingCluster::End)).isUniform());
  EXPECT_THROW((void)AxisSpacing::graded(0, 1.0, AxisGrading{}), InvalidArgumentError);
  EXPECT_THROW((void)AxisSpacing::graded(5, -1.0, AxisGrading{}), InvalidArgumentError);
}

TEST(AxisSpacing, FromNodesGivesCentresAndWidths) {
  const auto s = AxisSpacing::fromNodes({0.0, 1.0, 3.0, 7.0});
  EXPECT_EQ(s.cells(), 3u);
  EXPECT_EQ(s.center(1), 2.0);
  EXPECT_EQ(s.width(2), 4.0);
  EXPECT_EQ(s.node(3), 7.0);
}

TEST(AxisSpacing, FromNodesRejectsInvalidNodes) {
  EXPECT_THROW((void)AxisSpacing::fromNodes({0.0}), InvalidArgumentError);
  EXPECT_THROW((void)AxisSpacing::fromNodes({0.1, 1.0}), InvalidArgumentError);
  EXPECT_THROW((void)AxisSpacing::fromNodes({0.0, 1.0, 1.0}), InvalidArgumentError);
  EXPECT_THROW((void)AxisSpacing::fromNodes({0.0, 2.0, 1.0}), InvalidArgumentError);
  EXPECT_THROW((void)AxisSpacing::fromNodes({0.0, std::numeric_limits<Real>::infinity()}),
               InvalidArgumentError);
}
