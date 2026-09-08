#pragma once

#include <mutex>
#include <string_view>

namespace cfd {

enum class LogLevel : int {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warning = 3,
  Error = 4,
};

// Process-wide logger singleton. Deliberately dependency-free (no Qt, no
// GUI, no JSON) so the numerical core stays usable headless -- from the
// CLI, from validation scripts, or from a future GUI that only displays
// what this emits. Thread-safe so later OpenMP/MPI code can log without
// interleaving output.
//
// Usage: cfd::Logger::instance().info("Starting SIMPLE solver");
class Logger {
 public:
  static Logger& instance();

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void setLevel(LogLevel level);
  [[nodiscard]] LogLevel level() const noexcept;

  void log(LogLevel level, std::string_view message);

  void trace(std::string_view message);
  void debug(std::string_view message);
  void info(std::string_view message);
  void warning(std::string_view message);
  void error(std::string_view message);

 private:
  Logger() = default;

  mutable std::mutex mutex_;
  LogLevel level_{LogLevel::Info};
};

}  // namespace cfd
