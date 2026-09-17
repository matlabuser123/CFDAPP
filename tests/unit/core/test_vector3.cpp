// P12-MESH-005 -- the three-component geometry vector (Vector3; Vector2 is
// its alias): arithmetic on all three components, dot/cross/magnitude, and
// the guarantee that every 2D operation returns exactly the former
// two-component result (including the sign of a zero dot product).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "cfd/core/Vector2.hpp"
#include "cfd/core/Vector3.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::Vector3;

namespace {

std::uint64_t bits(Real v) {
  std::uint64_t b = 0;
  std::memcpy(&b, &v, sizeof b);
  return b;
}

}  // namespace

TEST(Vector3Test, Vector2IsTheSameTypeWithZeroZ) {
  static_assert(std::is_same_v<Vector2, Vector3>);
  const Vector2 v{1.5, -2.5};
  EXPECT_EQ(v.x, 1.5);
  EXPECT_EQ(v.y, -2.5);
  EXPECT_EQ(v.z, 0.0);
  const Vector3 zero{};
  EXPECT_EQ(zero.x, 0.0);
  EXPECT_EQ(zero.y, 0.0);
  EXPECT_EQ(zero.z, 0.0);
}

TEST(Vector3Test, ArithmeticActsOnAllThreeComponents) {
  const Vector3 a{1.0, 2.0, 3.0};
  const Vector3 b{-4.0, 0.5, 8.0};
  EXPECT_TRUE((a + b) == (Vector3{-3.0, 2.5, 11.0}));
  EXPECT_TRUE((a - b) == (Vector3{5.0, 1.5, -5.0}));
  EXPECT_TRUE((-a) == (Vector3{-1.0, -2.0, -3.0}));
  EXPECT_TRUE((a * 2.0) == (Vector3{2.0, 4.0, 6.0}));
  EXPECT_TRUE((0.5 * b) == (Vector3{-2.0, 0.25, 4.0}));
  Vector3 c = a;
  c += b;
  EXPECT_TRUE(c == (Vector3{-3.0, 2.5, 11.0}));
  c -= b;
  EXPECT_TRUE(c == a);
  c *= -1.0;
  EXPECT_TRUE(c == -a);
  EXPECT_FALSE((Vector3{1.0, 2.0, 3.0}) == (Vector3{1.0, 2.0, 3.5}));
}

TEST(Vector3Test, DotCrossAndMagnitudeUseZ) {
  const Vector3 a{1.0, 2.0, 3.0};
  const Vector3 b{4.0, -5.0, 6.0};
  EXPECT_EQ(dot(a, b), 12.0);  // 4 - 10 + 18
  EXPECT_EQ(magnitude(Vector3{2.0, 3.0, 6.0}), 7.0);
  EXPECT_EQ(magnitude(Vector3{0.0, 0.0, -4.0}), 4.0);
  EXPECT_TRUE(cross(Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}) == (Vector3{0.0, 0.0, 1.0}));
  EXPECT_TRUE(cross(Vector3{0.0, 1.0, 0.0}, Vector3{0.0, 0.0, 1.0}) == (Vector3{1.0, 0.0, 0.0}));
  EXPECT_TRUE(cross(Vector3{0.0, 0.0, 1.0}, Vector3{1.0, 0.0, 0.0}) == (Vector3{0.0, 1.0, 0.0}));
  const Vector3 c = cross(a, b);  // (2*6 - 3*-5, 3*4 - 1*6, 1*-5 - 2*4)
  EXPECT_TRUE(c == (Vector3{27.0, 6.0, -13.0}));
  EXPECT_EQ(dot(c, a), 0.0);
  EXPECT_EQ(dot(c, b), 0.0);
  EXPECT_TRUE(cross(b, a) == -c);
  EXPECT_TRUE(cross(a, a) == Vector3{});
}

TEST(Vector3Test, IsFiniteChecksEveryComponent) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_TRUE(isFinite(Vector3{1.0, 2.0, 3.0}));
  EXPECT_FALSE(isFinite(Vector3{nan, 0.0, 0.0}));
  EXPECT_FALSE(isFinite(Vector3{0.0, inf, 0.0}));
  EXPECT_FALSE(isFinite(Vector3{0.0, 0.0, nan}));
}

// Every 2D value (z = 0) gives bit for bit the former two-component results:
// dot = (a.x b.x) + (a.y b.y), the 2D cross product in the z component, and
// magnitude = sqrt(x^2 + y^2) -- over a sweep including signed zeros.
TEST(Vector3Test, TwoDimensionalResultsAreBitwiseTheFormerFormulas) {
  const std::vector<Real> values = {0.0, -0.0, 1.0, -1.0, 0.1, -2.75, 3e-300, -7e150, 12345.678};
  long long checked = 0;
  for (const Real ax : values) {
    for (const Real ay : values) {
      for (const Real bx : values) {
        for (const Real by : values) {
          const Vector2 a{ax, ay};
          const Vector2 b{bx, by};
          const Real formerDot = (ax * bx) + (ay * by);
          EXPECT_EQ(bits(dot(a, b)), bits(formerDot)) << ax << ' ' << ay << ' ' << bx << ' ' << by;
          const Real formerCross = (ax * by) - (ay * bx);
          EXPECT_EQ(bits(cross(a, b).z), bits(formerCross));
          EXPECT_EQ(cross(a, b).x, 0.0);
          EXPECT_EQ(cross(a, b).y, 0.0);
          ++checked;
        }
      }
      EXPECT_EQ(bits(magnitude(Vector2{ax, ay})), bits(std::sqrt((ax * ax) + (ay * ay))));
    }
  }
  EXPECT_EQ(checked, 6561);
  // The case that motivates dot's zero-z rule: both products -0.0.
  const Real d = dot(Vector2{-1.0, 0.0}, Vector2{0.0, -2.0});
  EXPECT_EQ(d, 0.0);
  EXPECT_TRUE(std::signbit(d));
}
