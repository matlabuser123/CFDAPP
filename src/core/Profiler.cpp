#include "cfd/core/Profiler.hpp"

namespace cfd {

Profiler& Profiler::instance() {
  static Profiler profiler;
  return profiler;
}

void Profiler::record(std::string_view category, double seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& entry = stats_[std::string(category)];
  entry.totalSeconds += seconds;
  entry.callCount += 1;
}

void Profiler::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  stats_.clear();
}

std::map<std::string, ProfileCategoryStats> Profiler::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

ScopedTimer::ScopedTimer(std::string_view category) : category_(category) {}

ScopedTimer::~ScopedTimer() { Profiler::instance().record(category_, timer_.elapsedSeconds()); }

}  // namespace cfd
