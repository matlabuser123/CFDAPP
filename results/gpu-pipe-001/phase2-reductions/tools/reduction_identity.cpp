// GPU-PIPE-001 Phase 2 -- Gate 1 probe.
//
// The whole correctness argument for Phase 2B is that dot2(a0,b0,a1,b1) is
// BITWISE identical to dot(a0,b0) and dot(a1,b1) -- not "agrees to a
// tolerance". This probe tests that claim directly, on bit patterns, across
// sizes that exercise partial blocks and magnitudes that exercise cancellation.
//
// It also proves the probe itself is non-vacuous: a deliberately reassociated
// reference sum is included, and it MUST differ from dot() on at least one case.
// If it did not, the test could not tell a bitwise-identical implementation
// from a merely-close one, and would prove nothing.
//
// Build (out of tree, links the real libraries):
//   g++ -std=c++20 -O2 -I include reduction_identity.cpp \
//       build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a \
//       -L/usr/local/cuda-12.9/lib64 -lcudart -o /tmp/reduction_identity

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "cfd/algebra/Vector.hpp"
#include "cfd/gpu/DeviceVector.hpp"
#include "cfd/gpu/DeviceVectorOps.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

using cfd::Real;
using namespace cfd::gpu;

namespace {

int failures = 0;
int checks = 0;

bool bitwiseEqual(Real x, Real y) { return std::memcmp(&x, &y, sizeof(Real)) == 0; }

std::string bits(Real x) {
  unsigned long long u = 0;
  std::memcpy(&u, &x, sizeof(u));
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx", u);
  return buf;
}

void expectBitwise(const char* what, Real got, Real want) {
  ++checks;
  if (!bitwiseEqual(got, want)) {
    ++failures;
    std::printf("  FAIL %-46s got %.17g (%s) want %.17g (%s)\n", what, got, bits(got).c_str(), want,
                bits(want).c_str());
  }
}

// A deliberately different association order -- the negative control for the
// probe. Pairwise summation is at least as accurate as the in-order sum, so on
// a cancelling case it should land on different bits.
Real pairwiseSum(std::vector<Real> v) {
  while (v.size() > 1) {
    std::vector<Real> next((v.size() + 1) / 2);
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) next[i / 2] = v[i] + v[i + 1];
    if (v.size() % 2) next.back() = v.back();
    v.swap(next);
  }
  return v.empty() ? 0.0 : v[0];
}

void runCase(const std::string& label, const std::vector<Real>& a, const std::vector<Real>& b,
             const std::vector<Real>& c, const std::vector<Real>& d) {
  auto toHost = [](const std::vector<Real>& v) {
    cfd::algebra::Vector h(static_cast<cfd::Index>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) h[static_cast<cfd::Index>(i)] = v[i];
    return h;
  };
  DeviceVector da, db, dc, dd;
  da.uploadFrom(toHost(a));
  db.uploadFrom(toHost(b));
  dc.uploadFrom(toHost(c));
  dd.uploadFrom(toHost(d));

  const Real s0 = dot(da, db);
  const Real s1 = dot(dc, dd);

  Real f0 = 0.0, f1 = 0.0;
  dot2(da, db, dc, dd, f0, f1);

  expectBitwise((label + " dot2[0] == dot(a0,b0)").c_str(), f0, s0);
  expectBitwise((label + " dot2[1] == dot(a1,b1)").c_str(), f1, s1);

  // l2Norm is sqrt(dot(v,v)); fusing it with another product must not move it.
  Real g0 = 0.0, g1 = 0.0;
  dot2(da, da, dc, dd, g0, g1);
  expectBitwise((label + " fused l2Norm^2 == dot(a,a)").c_str(), g0, dot(da, da));
  expectBitwise((label + " sqrt matches l2Norm").c_str(), std::sqrt(g0), l2Norm(da));
}

}  // namespace

int main() {
  if (!cudaAvailable()) {
    std::printf("CUDA unavailable -- cannot run\n");
    return 77;
  }

  std::mt19937_64 rng(20260919);
  std::uniform_real_distribution<Real> uni(-1.0, 1.0);

  // Sizes chosen to straddle the 256-thread block boundary: exact multiples,
  // one over, one under, and sizes spanning many blocks.
  const std::vector<int> sizes = {1, 255, 256, 257, 511, 512, 1000, 25600, 102400, 409600};

  for (int n : sizes) {
    std::vector<Real> a(n), b(n), c(n), d(n);
    for (int i = 0; i < n; ++i) {
      a[i] = uni(rng);
      b[i] = uni(rng);
      c[i] = uni(rng);
      d[i] = uni(rng);
    }
    runCase("n=" + std::to_string(n) + " random", a, b, c, d);

    // Cancellation: terms of alternating sign and wildly different scale, the
    // regime GPU-PCORR-001's breakdown test exists for.
    std::vector<Real> p(n), q(n);
    for (int i = 0; i < n; ++i) {
      p[i] = ((i % 2) ? -1.0 : 1.0) * std::pow(10.0, (i % 17) - 8);
      q[i] = 1.0;
    }
    runCase("n=" + std::to_string(n) + " cancelling", p, q, q, p);

    // Zeros, and a mixed zero/non-zero pair.
    std::vector<Real> z(n, 0.0);
    runCase("n=" + std::to_string(n) + " zeros", z, z, a, z);
  }

  // --- probe non-vacuity -------------------------------------------------
  // Reassociating the sum must be detectable, or the bitwise checks above are
  // not actually testing anything.
  int reassociationDetected = 0;
  for (int n : {25600, 102400, 409600}) {
    std::vector<Real> p(n), q(n, 1.0);
    for (int i = 0; i < n; ++i) p[i] = ((i % 2) ? -1.0 : 1.0) * std::pow(10.0, (i % 17) - 8);
    cfd::algebra::Vector hp(n), hq(n);
    for (int i = 0; i < n; ++i) { hp[i] = p[i]; hq[i] = q[i]; }
    DeviceVector dp, dq;
    dp.uploadFrom(hp);
    dq.uploadFrom(hq);
    std::vector<Real> terms(n);
    for (int i = 0; i < n; ++i) terms[i] = p[i] * q[i];
    if (!bitwiseEqual(dot(dp, dq), pairwiseSum(terms))) ++reassociationDetected;
  }
  std::printf("\nnon-vacuity: reassociated reference differed on %d of 3 cancelling cases\n",
              reassociationDetected);
  if (reassociationDetected == 0) {
    std::printf("  FAIL probe is VACUOUS -- it cannot distinguish summation orders\n");
    ++failures;
  }

  const auto& st = gpuExecutionStats();
  std::printf("\nchecks: %d   failures: %d\n", checks, failures);
  std::printf("reduction groups: %llu   quantities: %llu   fusion factor: %.2f\n",
              (unsigned long long)st.reductionGroups, (unsigned long long)st.reductionQuantities,
              st.reductionGroups ? double(st.reductionQuantities) / double(st.reductionGroups) : 0.0);
  std::printf("D2H calls: %llu   D2H bytes: %llu   synchronizations: %llu\n",
              (unsigned long long)st.deviceToHostCalls, (unsigned long long)st.deviceToHostBytes,
              (unsigned long long)st.synchronizations);
  std::printf("\n%s\n", failures ? "REDUCTION IDENTITY: FAIL" : "REDUCTION IDENTITY: PASS");
  return failures ? 1 : 0;
}
