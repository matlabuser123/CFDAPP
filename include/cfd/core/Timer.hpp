#pragma once

#include <chrono>

namespace cfd {

// Simple stopwatch built on std::chrono::steady_clock (never wall-clock,
// which can jump backwards/forwards e.g. on NTP sync). Used for solver
// runtime, matrix assembly, linear solves, flux calculations, validation,
// and benchmark measurements.
class Timer {
 public:
  Timer();

  void reset() noexcept;

  [[nodiscard]] double elapsedSeconds() const noexcept;
  [[nodiscard]] double elapsedMilliseconds() const noexcept;

 private:
  using Clock = std::chrono::steady_clock;

  Clock::time_point start_;
};

}  // namespace cfd
