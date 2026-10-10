#pragma once

// P17.10 -- the CPU/GPU comparison instrument.
//
// It is a separate header, and it is DRY-RUN BEFORE THE GATE IS FROZEN
// (tests/integration/compressible/test_p17_10_dryrun.cpp), because this file is the one thing
// standing between "the GPU agrees with the CPU" and "nothing was measured". The frozen
// tolerance_policy.md asks for BITWISE, so this compares BIT PATTERNS, not values.
//
// EVERY WAY THIS COULD REPORT A FALSE "EQUAL" IS CLOSED DELIBERATELY:
//
//   * an empty comparison is NOT equal. Zero cells compared is the signature of a probe whose
//     inputs never arrived, and it is exactly what "0 differences" looks like.
//   * a size mismatch is NOT equal, and is reported as such rather than comparing the overlap.
//   * a side that was never populated is NOT equal. `populated` must be set by the caller AFTER the
//     backend actually produced the field; a default-constructed report is a failure.
//   * a comparison that never ran -- because the GPU was absent, or a CUDA call failed -- is NOT
//     equal. It is the caller's job to say so, and ComparisonReport has no state that can be
//     confused with success.
//   * NaN == NaN bitwise. That is the right answer for "did the two paths compute the same thing",
//     and the WRONG answer for "is the result usable", so nonFinite counts are carried out
//     separately and the suite asserts on them independently. A pair of matching NaNs must never be
//     reported as a clean pass on its own.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::p17_10 {

using cfd::Index;
using cfd::Real;

[[nodiscard]] inline std::uint64_t bits(Real v) {
  std::uint64_t u = 0;
  static_assert(sizeof(u) == sizeof(v), "Real is not 64-bit");
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

// Signed-magnitude to biased-offset, so that a subtraction counts representable steps. Used ONLY to
// describe a difference in a failure message -- never to decide whether one is acceptable.
[[nodiscard]] inline std::uint64_t ulpDistance(Real a, Real b) {
  if (std::isnan(a) || std::isnan(b)) return UINT64_MAX;
  auto order = [](std::uint64_t u) -> std::uint64_t {
    return (u & 0x8000000000000000ULL) != 0 ? (0x8000000000000000ULL - (u & 0x7FFFFFFFFFFFFFFFULL))
                                            : (u + 0x8000000000000000ULL);
  };
  const std::uint64_t oa = order(bits(a));
  const std::uint64_t ob = order(bits(b));
  return oa > ob ? oa - ob : ob - oa;
}

struct FieldDifference {
  std::string quantity;
  Index cell{};
  Real cpu{};
  Real gpu{};
  std::uint64_t ulps{};
};

class ComparisonReport {
 public:
  // The default state is FAILURE. Nothing about "I have not compared anything yet" resembles
  // success, and no code path can reach a pass without calling compare() on populated inputs.
  ComparisonReport() = default;

  [[nodiscard]] bool bitwiseEqual() const {
    return ran_ && cpuPopulated_ && gpuPopulated_ && !sizeMismatch_ && comparedValues_ > 0 &&
           differences_.empty();
  }

  [[nodiscard]] Index comparedValues() const { return comparedValues_; }
  [[nodiscard]] Index differingValues() const { return differingValues_; }
  [[nodiscard]] Index nonFiniteCpu() const { return nonFiniteCpu_; }
  [[nodiscard]] Index nonFiniteGpu() const { return nonFiniteGpu_; }
  [[nodiscard]] const std::vector<FieldDifference>& differences() const { return differences_; }
  [[nodiscard]] bool ran() const { return ran_; }

  // Why it is not equal, in words, for the failure message. Never consulted on success.
  [[nodiscard]] std::string reason() const {
    if (!ran_) return "the comparison never ran (no backend produced a result)";
    if (!cpuPopulated_) return "the CPU side was never populated";
    if (!gpuPopulated_) return "the GPU side was never populated";
    if (sizeMismatch_)
      return "the two sides have different sizes: " + std::to_string(cpuSize_) + " vs " +
             std::to_string(gpuSize_);
    if (comparedValues_ == 0) return "zero values were compared -- the inputs were empty";
    if (differences_.empty()) return "bitwise equal";
    std::string s = std::to_string(differingValues_) + " of " + std::to_string(comparedValues_) +
                    " values differ; first " + std::to_string(differences_.size()) + ":";
    for (const auto& d : differences_) {
      s += "\n  " + d.quantity + " cell " + std::to_string(d.cell) +
           ": cpu=" + std::to_string(d.cpu) + " gpu=" + std::to_string(d.gpu) + " (" +
           std::to_string(d.ulps) + " ulp)";
    }
    return s;
  }

  void markCpuPopulated() { cpuPopulated_ = true; }
  void markGpuPopulated() { gpuPopulated_ = true; }

  void compareScalar(const std::string& quantity, Index cell, Real cpu, Real gpu) {
    ran_ = true;
    ++comparedValues_;
    if (!std::isfinite(cpu)) ++nonFiniteCpu_;
    if (!std::isfinite(gpu)) ++nonFiniteGpu_;
    if (bits(cpu) != bits(gpu)) {
      ++differingValues_;
      if (differences_.size() < kMaxReported) {
        differences_.push_back({quantity, cell, cpu, gpu, ulpDistance(cpu, gpu)});
      }
    }
  }

  void noteSizes(Index cpuSize, Index gpuSize) {
    ran_ = true;
    cpuSize_ = cpuSize;
    gpuSize_ = gpuSize;
    if (cpuSize != gpuSize) sizeMismatch_ = true;
  }

 private:
  static constexpr std::size_t kMaxReported = 8;
  bool ran_{false};
  bool cpuPopulated_{false};
  bool gpuPopulated_{false};
  bool sizeMismatch_{false};
  Index cpuSize_{};
  Index gpuSize_{};
  Index comparedValues_{};
  Index differingValues_{};
  Index nonFiniteCpu_{};
  Index nonFiniteGpu_{};
  std::vector<FieldDifference> differences_;
};

// Compares two conserved states bitwise, component by component, cell by cell. `cpuPopulated` and
// `gpuPopulated` are the caller's assertion that each side actually came out of its backend --
// passing `true` for a side that did not run is the one way to defeat this instrument, which is why
// the call sites pass the backend's own success flag and never a literal.
[[nodiscard]] inline ComparisonReport compareConserved(const cfd::compressible::ConservedField& cpu,
                                                       bool cpuPopulated,
                                                       const cfd::compressible::ConservedField& gpu,
                                                       bool gpuPopulated) {
  ComparisonReport r;
  if (cpuPopulated) r.markCpuPopulated();
  if (gpuPopulated) r.markGpuPopulated();
  r.noteSizes(cpu.size(), gpu.size());
  if (cpu.size() != gpu.size()) return r;
  for (Index i = 0; i < cpu.size(); ++i) {
    r.compareScalar("density", i, cpu.density[i], gpu.density[i]);
    r.compareScalar("momentumX", i, cpu.momentumDensity[i].x, gpu.momentumDensity[i].x);
    r.compareScalar("momentumY", i, cpu.momentumDensity[i].y, gpu.momentumDensity[i].y);
    r.compareScalar("momentumZ", i, cpu.momentumDensity[i].z, gpu.momentumDensity[i].z);
    r.compareScalar("totalEnergy", i, cpu.totalEnergyDensity[i], gpu.totalEnergyDensity[i]);
  }
  return r;
}

[[nodiscard]] inline ComparisonReport compareResidual(const cfd::compressible::ResidualField& cpu,
                                                      bool cpuPopulated,
                                                      const cfd::compressible::ResidualField& gpu,
                                                      bool gpuPopulated) {
  ComparisonReport r;
  if (cpuPopulated) r.markCpuPopulated();
  if (gpuPopulated) r.markGpuPopulated();
  const Index n = static_cast<Index>(cpu.density.size());
  const Index m = static_cast<Index>(gpu.density.size());
  r.noteSizes(n, m);
  if (n != m) return r;
  for (Index i = 0; i < n; ++i) {
    r.compareScalar("R.density", i, cpu.density[i], gpu.density[i]);
    r.compareScalar("R.momentumX", i, cpu.momentum[i].x, gpu.momentum[i].x);
    r.compareScalar("R.momentumY", i, cpu.momentum[i].y, gpu.momentum[i].y);
    r.compareScalar("R.momentumZ", i, cpu.momentum[i].z, gpu.momentum[i].z);
    r.compareScalar("R.totalEnergy", i, cpu.totalEnergy[i], gpu.totalEnergy[i]);
  }
  return r;
}

[[nodiscard]] inline ComparisonReport comparePrimitives(
    const std::vector<cfd::compressible::PrimitiveState>& cpu, bool cpuPopulated,
    const std::vector<cfd::compressible::PrimitiveState>& gpu, bool gpuPopulated) {
  ComparisonReport r;
  if (cpuPopulated) r.markCpuPopulated();
  if (gpuPopulated) r.markGpuPopulated();
  const Index n = static_cast<Index>(cpu.size());
  const Index m = static_cast<Index>(gpu.size());
  r.noteSizes(n, m);
  if (n != m) return r;
  for (Index i = 0; i < n; ++i) {
    r.compareScalar("p.density", i, cpu[i].density, gpu[i].density);
    r.compareScalar("p.velocityX", i, cpu[i].velocity.x, gpu[i].velocity.x);
    r.compareScalar("p.velocityY", i, cpu[i].velocity.y, gpu[i].velocity.y);
    r.compareScalar("p.velocityZ", i, cpu[i].velocity.z, gpu[i].velocity.z);
    r.compareScalar("p.pressure", i, cpu[i].pressure, gpu[i].pressure);
    r.compareScalar("p.temperature", i, cpu[i].temperature, gpu[i].temperature);
    r.compareScalar("p.speedOfSound", i, cpu[i].speedOfSound, gpu[i].speedOfSound);
    r.compareScalar("p.totalEnergyDensity", i, cpu[i].totalEnergyDensity,
                    gpu[i].totalEnergyDensity);
  }
  return r;
}

[[nodiscard]] inline ComparisonReport compareVector(const std::string& quantity,
                                                    const std::vector<Real>& cpu, bool cpuPopulated,
                                                    const std::vector<Real>& gpu,
                                                    bool gpuPopulated) {
  ComparisonReport r;
  if (cpuPopulated) r.markCpuPopulated();
  if (gpuPopulated) r.markGpuPopulated();
  const Index n = static_cast<Index>(cpu.size());
  const Index m = static_cast<Index>(gpu.size());
  r.noteSizes(n, m);
  if (n != m) return r;
  for (Index i = 0; i < n; ++i) r.compareScalar(quantity, i, cpu[i], gpu[i]);
  return r;
}

}  // namespace cfd::p17_10
