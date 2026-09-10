#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "cfd/core/Timer.hpp"

namespace cfd {

// P4 -- Performance: minimal category-based profiling accumulator. Not a
// sampling profiler or a replacement for perf/VTune -- a very-low-
// overhead way to answer "how much total time did phase X take, across
// however many times it ran" (TODO.md P4 section 5: "Avoid sprinkling ad
// hoc std::cout timestamps everywhere... Aggregate measurements by
// subsystem"). Built on the existing cfd::Timer (steady_clock, never
// wall-clock) the same way every other timing need in this codebase
// already is.
//
// Process-wide singleton, same convention as cfd::Logger (mutex-
// protected for the same reason: later OpenMP-parallel code must be able
// to record without corrupting the accumulator) -- deliberately NOT
// intended to be called from inside a hot per-cell/per-iteration loop
// (mutex contention would dominate); intended granularity is "one phase
// of one outer iteration" (an assembly call, a linear solve, a flux
// calculation), matching TODO.md P4 section 6's own category list.
struct ProfileCategoryStats {
  double totalSeconds{};
  std::uint64_t callCount{};
};

class Profiler {
 public:
  static Profiler& instance();

  Profiler(const Profiler&) = delete;
  Profiler& operator=(const Profiler&) = delete;

  // Adds one measurement to `category`'s running total/count.
  void record(std::string_view category, double seconds);

  // Clears all recorded categories -- call before a fresh benchmark run
  // so results from an earlier run/case do not bleed into the next.
  void reset();

  [[nodiscard]] std::map<std::string, ProfileCategoryStats> snapshot() const;

 private:
  Profiler() = default;

  mutable std::mutex mutex_;
  std::map<std::string, ProfileCategoryStats> stats_;
};

// RAII scoped timer: records elapsed wall time into
// Profiler::instance() under `category` when it goes out of scope.
// Usage:
//   { cfd::ScopedTimer timer("momentum_assembly"); assembleMomentum(...); }
class ScopedTimer {
 public:
  explicit ScopedTimer(std::string_view category);
  ~ScopedTimer();

  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  std::string category_;
  Timer timer_;
};

}  // namespace cfd
