#include "cfd/core/Timer.hpp"

namespace cfd {

Timer::Timer() : start_(Clock::now()) {}

void Timer::reset() noexcept { start_ = Clock::now(); }

double Timer::elapsedSeconds() const noexcept {
  return std::chrono::duration<double>(Clock::now() - start_).count();
}

double Timer::elapsedMilliseconds() const noexcept {
  return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
}

}  // namespace cfd
