#pragma once

// P18.8 amendment A2 -- THE comparison instrument, shared by the gate and by the dry-run that
// qualified it.
//
// It lives in a header rather than in each test file because A2's non-vacuity argument rests on
// this code being SELF-TESTED (test_device_species_a2_dryrun.cpp, DRYRUN_1B) and then USED by the
// gate. Two copies would mean the tested one and the trusted one are different code -- the same
// mistake as naming a helper after what it is assumed to compute. P17.10 shares its
// EquivalenceCompare.hpp with its own pre-freeze dry-run for the same reason.
//
// WHAT ITS FIRST VERSION GOT WRONG, and why the self-test exists. ULP distance computed by
// memcpy-ing two doubles to int64 and subtracting is correct only while the signs agree. For +X
// and -X the difference is 2^63, which overflows a signed int64; the wrapped value read as 0, so
// control N6 -- which flips the sign of S_E, the largest error the suite can inject -- was scored
// as a PERFECT MATCH. Mapping each double to a monotonic unsigned key first makes the subtraction
// exact for every finite pair, and unsigned arithmetic has no overflow to wrap.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

namespace cfd::species_equivalence {

using cfd::Index;
using cfd::Real;

inline constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// A2's derived bound: 2 ULP for pow + 2 for exp + 1 for each of two multiplies = 5, taken to the
// next power of two. Derived in libm_divergence.md section 7 and frozen in acceptance_gate_A2.md
// section 2. Defined once so no test can drift from the frozen value.
inline constexpr std::uint64_t kDerivedUlpBound = 8;

// Maps a double to an unsigned key that increases monotonically with the real value, so the
// distance between two keys is the number of representable doubles between them -- across zero and
// across the sign.
inline std::uint64_t monotonicKey(Real v) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  constexpr std::uint64_t kSign = 0x8000000000000000ULL;
  return (bits & kSign) != 0 ? (kSign - (bits & ~kSign)) : (kSign + bits);
}

inline std::uint64_t ulpDistance(Real a, Real b) {
  const std::uint64_t x = monotonicKey(a);
  const std::uint64_t y = monotonicKey(b);
  return x > y ? (x - y) : (y - x);
}

struct Divergence {
  Index differing{};
  Index compared{};
  std::uint64_t worstUlp{};
  Real worstRelative{};
  Index nonFinite{};
  Index worstIndex{};
  bool sawWorst{false};
};

// Element-by-element comparison of two equal-length fields. `a` is the measured side, `b` the
// reference, so `worstRelative` is relative to the reference.
inline Divergence compare(const std::vector<Real>& a, const std::vector<Real>& b) {
  Divergence d;
  d.compared = static_cast<Index>(std::min(a.size(), b.size()));
  if (a.size() != b.size()) {
    // Fail CLOSED: a length mismatch means the two sides are not the same quantity, and silently
    // comparing the common prefix would report a clean result for incomparable inputs.
    d.differing = d.compared;
    d.worstUlp = std::numeric_limits<std::uint64_t>::max();
    d.worstRelative = std::numeric_limits<Real>::infinity();
    return d;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] == b[i]) {
      continue;
    }
    ++d.differing;
    if (!std::isfinite(a[i]) || !std::isfinite(b[i])) {
      // Counted, never skipped: a non-finite pair is the largest possible disagreement and must
      // not be able to leave the worst-case figures looking clean.
      ++d.nonFinite;
      d.worstUlp = std::numeric_limits<std::uint64_t>::max();
      d.worstRelative = std::numeric_limits<Real>::infinity();
      d.worstIndex = static_cast<Index>(i);
      d.sawWorst = true;
      continue;
    }
    const std::uint64_t ulp = ulpDistance(a[i], b[i]);
    if (!d.sawWorst || ulp > d.worstUlp) {
      d.worstUlp = ulp;
      d.worstIndex = static_cast<Index>(i);
      d.sawWorst = true;
    }
    if (b[i] != 0.0) {
      d.worstRelative = std::max(d.worstRelative, std::abs(a[i] - b[i]) / std::abs(b[i]));
    }
  }
  return d;
}

inline void report(const char* label, const Divergence& d) {
  std::cout << "    " << label << ": " << d.differing << " / " << d.compared
            << " differ,  worst ULP " << d.worstUlp << ",  worst relative " << d.worstRelative;
  if (d.sawWorst) {
    std::cout << ",  at index " << d.worstIndex;
  }
  if (d.nonFinite > 0) {
    std::cout << ",  NON-FINITE pairs " << d.nonFinite;
  }
  std::cout << "\n";
}

// |k_f Pi C'| + |k_r Pi C''| -- the sum of the progress rate's own term magnitudes, which is the
// scale A2 section 2 states the bound on q against. Bounding by eps*|q| instead would tighten
// without limit as a reversible reaction approaches equilibrium and its two terms cancel, which is
// a property of the cancellation and not of the two implementations.
inline Real progressRateTermScale(const cfd::gpu::DeviceMechanism& mechanism, Index reactionIndex,
                                  const Real* concentrations, Real temperature) {
  const cfd::gpu::DeviceSpeciesControls production{};
  cfd::gpu::DeviceSpeciesStatus status = cfd::gpu::DeviceSpeciesStatus::Ok;
  const Real forward = std::abs(
      cfd::gpu::deviceRateCoefficient(mechanism.forward[reactionIndex], temperature, production,
                                      &status, nullptr) *
      cfd::gpu::deviceConcentrationProduct(mechanism, reactionIndex, concentrations, true));
  Real reverse = 0.0;
  if (mechanism.reactions[reactionIndex].reversible) {
    reverse = std::abs(
        cfd::gpu::deviceRateCoefficient(mechanism.reverse[reactionIndex], temperature, production,
                                        &status, nullptr) *
        cfd::gpu::deviceConcentrationProduct(mechanism, reactionIndex, concentrations, false));
  }
  return forward + reverse;
}

}  // namespace cfd::species_equivalence
