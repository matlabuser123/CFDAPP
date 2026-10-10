// P16 resident turbulence -- the device convergence reduction.
//
// WHAT IT REPLACED. `correctKEpsilonDevice` used to download kOld, epsilonOld,
// kNew and epsilonNew IN FULL -- 4 * sizeof(double) * N bytes per correction,
// more than the k/epsilon/mu_t publication -- purely to run KEpsilonModel's
// host loop and produce one scalar:
//
//     Real maxChange = 0.0;
//     for (Index i = 0; i < n; ++i) {
//       maxChange = std::max(maxChange, std::abs(newK[i] - k_[i]));
//       maxChange = std::max(maxChange, std::abs(newEpsilon[i] - epsilon_[i]));
//     }
//
// That loop, in `src/turbulence/KEpsilonModel.cpp`, is the reference. These
// tests hold `maxAbsDifference2` to it exactly -- including the parts that are
// easy to get wrong and impossible to notice.
//
// WHY BITWISE IS THE RIGHT BAR HERE, NOT A TOLERANCE
// --------------------------------------------------
// A dot product reduced in a different block order gives a different answer,
// because floating-point addition does not reassociate. A MAXIMUM does: it
// selects one of its inputs rather than computing a new value, so there is
// nothing to round and nothing to reassociate. And each term is bitwise equal
// to the host's -- subtraction is exactly rounded, fabs only clears a sign
// bit, and neither admits an FMA contraction. So the device result must equal
// the host result bit for bit, on any grid, for any partition. Anything less
// would mean a real difference in the operation, not in the arithmetic.
//
// THE NaN CONTRACT. `std::max(a, b)` is `(a < b) ? b : a`. With b a NaN,
// `a < NaN` is false and the accumulator is KEPT, so a NaN difference is
// silently ignored; since the accumulator is seeded at 0.0 it can never become
// NaN. This is not obviously desirable behaviour, but it IS the production
// behaviour, and a reduction that propagated NaN instead would change when a
// turbulence solve is declared converged. The tests below pin it as it is
// rather than as it might ideally be.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceVectorOps.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

using cfd::Index;
using cfd::Real;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no usable CUDA device at runtime";
}

// The reference, transcribed from KEpsilonModel.cpp:297-302 -- the interleaved
// single accumulator, not two separate maxima.
Real hostReference(const std::vector<Real>& kNew, const std::vector<Real>& kOld,
                   const std::vector<Real>& eNew, const std::vector<Real>& eOld) {
  Real maxChange = 0.0;
  for (std::size_t i = 0; i < kNew.size(); ++i) {
    maxChange = std::max(maxChange, std::abs(kNew[i] - kOld[i]));
    maxChange = std::max(maxChange, std::abs(eNew[i] - eOld[i]));
  }
  return maxChange;
}

struct Upload {
  cfd::gpu::DeviceBuffer<Real> kNew, kOld, eNew, eOld;
  void operator()(const std::vector<Real>& a, const std::vector<Real>& b,
                  const std::vector<Real>& c, const std::vector<Real>& d) {
    const Index n = static_cast<Index>(a.size());
    kNew.uploadFrom(a.data(), n);
    kOld.uploadFrom(b.data(), n);
    eNew.uploadFrom(c.data(), n);
    eOld.uploadFrom(d.data(), n);
  }
};

// Deterministic, deliberately spiky, and NOT monotone in the index -- so the
// maximum does not land in the first block, the last block, or a lane boundary
// by construction.
std::vector<Real> field(Index n, Real seed, Real scale) {
  std::vector<Real> v(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    const Real t = static_cast<Real>(i) * 0.37 + seed;
    v[static_cast<std::size_t>(i)] = scale * (std::sin(t) + 0.25 * std::cos(3.1 * t));
  }
  return v;
}

Real combined(Real a, Real b) { return (a < b) ? b : a; }

}  // namespace

TEST(DeviceMaxAbsDifference, MatchesTheHostLoopBitwiseAcrossBlockPartitions) {
  skipIfNoDevice();

  // 1 is degenerate; 255/257 straddle the 256-thread block; 1000 and 4096 span
  // several blocks, one of them exactly aligned and one of them not.
  for (const Index n : {Index{1}, Index{255}, Index{256}, Index{257}, Index{1000}, Index{4096}}) {
    const auto kNew = field(n, 0.0, 1.0);
    const auto kOld = field(n, 0.7, 1.0);
    const auto eNew = field(n, 1.3, 40.0);
    const auto eOld = field(n, 2.1, 40.0);

    Upload up;
    up(kNew, kOld, eNew, eOld);
    Real mk = 0.0;
    Real me = 0.0;
    cfd::gpu::maxAbsDifference2(up.kNew, up.kOld, up.eNew, up.eOld, mk, me);

    // BITWISE. See the header comment for why this is achievable and why a
    // tolerance here would be hiding something rather than allowing for it.
    EXPECT_EQ(combined(mk, me), hostReference(kNew, kOld, eNew, eOld))
        << "n = " << n << " (blocks = " << ((n + 255) / 256) << ")";
  }
}

TEST(DeviceMaxAbsDifference, EmptyFieldsReduceToZeroNotToAnError) {
  skipIfNoDevice();
  // The host loop runs zero iterations and leaves its 0.0 seed. A turbulence
  // model on an empty partition must report a residual of 0, not NaN and not
  // a throw.
  cfd::gpu::DeviceBuffer<Real> empty;
  Real mk = -1.0;
  Real me = -1.0;
  cfd::gpu::maxAbsDifference2(empty, empty, empty, empty, mk, me);
  EXPECT_EQ(mk, 0.0);
  EXPECT_EQ(me, 0.0);
}

TEST(DeviceMaxAbsDifference, NaNDifferencesAreIgnoredExactlyAsStdMaxIgnoresThem) {
  skipIfNoDevice();
  constexpr Index n = 1000;
  auto kNew = field(n, 0.0, 1.0);
  auto kOld = field(n, 0.7, 1.0);
  const auto eNew = field(n, 1.3, 40.0);
  const auto eOld = field(n, 2.1, 40.0);

  const Real reference = hostReference(kNew, kOld, eNew, eOld);

  // A NaN difference in the middle of a block, and another at a block edge.
  const Real quietNaN = std::numeric_limits<Real>::quiet_NaN();
  kNew[137] = quietNaN;
  kNew[256] = quietNaN;

  Upload up;
  up(kNew, kOld, eNew, eOld);
  Real mk = 0.0;
  Real me = 0.0;
  cfd::gpu::maxAbsDifference2(up.kNew, up.kOld, up.eNew, up.eOld, mk, me);
  const Real device = combined(mk, me);

  EXPECT_FALSE(std::isnan(device)) << "a NaN reached the accumulator; production would then never "
                                      "declare the turbulence solve converged";
  EXPECT_EQ(device, hostReference(kNew, kOld, eNew, eOld))
      << "the device disagrees with std::max's own NaN handling";
  // And the poisoned cells did not simply erase the real maximum.
  EXPECT_EQ(device, reference);
}

TEST(DeviceMaxAbsDifference, AnInfiniteDifferenceIsCarriedThroughLikeTheHostLoop) {
  skipIfNoDevice();
  constexpr Index n = 700;
  auto kNew = field(n, 0.0, 1.0);
  const auto kOld = field(n, 0.7, 1.0);
  const auto eNew = field(n, 1.3, 40.0);
  const auto eOld = field(n, 2.1, 40.0);
  kNew[500] = std::numeric_limits<Real>::infinity();

  Upload up;
  up(kNew, kOld, eNew, eOld);
  Real mk = 0.0;
  Real me = 0.0;
  cfd::gpu::maxAbsDifference2(up.kNew, up.kOld, up.eNew, up.eOld, mk, me);

  // Infinity compares greater, so unlike NaN it DOES win -- which is the host
  // behaviour, and is what makes a diverging solve visible instead of silent.
  EXPECT_TRUE(std::isinf(combined(mk, me)));
  EXPECT_EQ(combined(mk, me), hostReference(kNew, kOld, eNew, eOld));
}

// =============================================================================
// Negative controls -- the reduction is not reporting a constant.
// =============================================================================

TEST(DeviceMaxAbsDifference, PerturbingTheMaximumCellMovesTheMetricInEachField) {
  skipIfNoDevice();
  constexpr Index n = 1000;
  const auto kOld = field(n, 0.7, 1.0);
  const auto eOld = field(n, 2.1, 40.0);

  for (const bool perturbK : {true, false}) {
    auto kNew = field(n, 0.0, 1.0);
    auto eNew = field(n, 1.3, 40.0);
    Upload base;
    base(kNew, kOld, eNew, eOld);
    Real mk = 0.0;
    Real me = 0.0;
    cfd::gpu::maxAbsDifference2(base.kNew, base.kOld, base.eNew, base.eOld, mk, me);
    const Real before = combined(mk, me);

    // One cell, made the unique maximum by a wide margin.
    const Real spike = before * 10.0 + 1.0;
    if (perturbK) {
      kNew[613] = kOld[613] + spike;
    } else {
      eNew[613] = eOld[613] + spike;
    }

    Upload perturbed;
    perturbed(kNew, kOld, eNew, eOld);
    Real mk2 = 0.0;
    Real me2 = 0.0;
    cfd::gpu::maxAbsDifference2(perturbed.kNew, perturbed.kOld, perturbed.eNew, perturbed.eOld, mk2,
                                me2);

    EXPECT_EQ(combined(mk2, me2), spike)
        << (perturbK ? "k" : "epsilon") << ": the metric did not follow the new maximum";
    // And it landed in the RIGHT field's reduction, not just in the combination.
    EXPECT_EQ(perturbK ? mk2 : me2, spike) << "the spike was attributed to the wrong field";
    EXPECT_EQ(perturbK ? me2 : mk2, perturbK ? me : mk) << "the other field's maximum moved";
  }
}

TEST(DeviceMaxAbsDifference, PerturbingBelowTheMaximumLeavesTheMetricUnchanged) {
  skipIfNoDevice();
  constexpr Index n = 1000;
  auto kNew = field(n, 0.0, 1.0);
  const auto kOld = field(n, 0.7, 1.0);
  const auto eNew = field(n, 1.3, 40.0);
  const auto eOld = field(n, 2.1, 40.0);

  Upload base;
  base(kNew, kOld, eNew, eOld);
  Real mk = 0.0;
  Real me = 0.0;
  cfd::gpu::maxAbsDifference2(base.kNew, base.kOld, base.eNew, base.eOld, mk, me);

  // A change that is real but still strictly below the current k maximum: the
  // reported value must not move at all. A reduction that summed, averaged or
  // counted would fail here while passing the test above.
  kNew[222] = kOld[222] + 0.5 * mk;
  Upload perturbed;
  perturbed(kNew, kOld, eNew, eOld);
  Real mk2 = 0.0;
  Real me2 = 0.0;
  cfd::gpu::maxAbsDifference2(perturbed.kNew, perturbed.kOld, perturbed.eNew, perturbed.eOld, mk2,
                              me2);

  EXPECT_EQ(mk2, mk) << "a sub-maximal change moved the maximum";
  EXPECT_EQ(me2, me);
}

TEST(DeviceMaxAbsDifference, RepeatedReductionIsBitwiseReproducible) {
  skipIfNoDevice();
  constexpr Index n = 4096;
  const auto kNew = field(n, 0.0, 1.0);
  const auto kOld = field(n, 0.7, 1.0);
  const auto eNew = field(n, 1.3, 40.0);
  const auto eOld = field(n, 2.1, 40.0);

  Upload up;
  up(kNew, kOld, eNew, eOld);
  Real firstK = 0.0;
  Real firstE = 0.0;
  cfd::gpu::maxAbsDifference2(up.kNew, up.kOld, up.eNew, up.eOld, firstK, firstE);
  for (int repeat = 0; repeat < 4; ++repeat) {
    Real k = 0.0;
    Real e = 0.0;
    cfd::gpu::maxAbsDifference2(up.kNew, up.kOld, up.eNew, up.eOld, k, e);
    EXPECT_EQ(k, firstK);
    EXPECT_EQ(e, firstE);
  }
}

TEST(DeviceMaxAbsDifference, DownloadsOnlyTheReductionPartialsNotTheFields) {
  skipIfNoDevice();
  constexpr Index n = 4096;
  const auto kNew = field(n, 0.0, 1.0);
  const auto kOld = field(n, 0.7, 1.0);
  const auto eNew = field(n, 1.3, 40.0);
  const auto eOld = field(n, 2.1, 40.0);

  Upload up;
  up(kNew, kOld, eNew, eOld);
  Real k = 0.0;
  Real e = 0.0;
  cfd::gpu::resetGpuExecutionStats();
  cfd::gpu::maxAbsDifference2(up.kNew, up.kOld, up.eNew, up.eOld, k, e);
  const auto& stats = cfd::gpu::gpuExecutionStats();

  // ONE round trip of 2 * blocks partials, and nothing else. The whole point of
  // the change is that 4 * 8 * N bytes do not move; at n = 4096 that would be
  // 131 072 bytes against the 256 this actually costs.
  const std::uint64_t blocks = (n + 255) / 256;
  EXPECT_EQ(stats.deviceToHostCalls, 1u);
  EXPECT_EQ(stats.deviceToHostBytes, 2ull * blocks * sizeof(Real));
  EXPECT_LT(stats.deviceToHostBytes, 4ull * sizeof(Real) * static_cast<std::uint64_t>(n));
}
