#pragma once

#include <mutex>
#include <optional>
#include <string_view>

namespace cfd {

enum class LogLevel : int {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warning = 3,
  Error = 4,
};

// P13-005: the one place a level name is parsed, so the CLI, the GUI and any
// future client share a vocabulary instead of each inventing one. Returns
// std::nullopt for an unrecognised name -- the caller decides whether that is
// a usage error, and can name the valid values from levelNames() rather than
// hardcoding a second copy of them.
[[nodiscard]] std::optional<LogLevel> parseLogLevel(std::string_view name) noexcept;
[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;
[[nodiscard]] std::string_view logLevelNames() noexcept;

// Process-wide logger singleton. Deliberately dependency-free (no Qt, no
// GUI, no JSON) so the numerical core stays usable headless -- from the
// CLI, from validation scripts, or from a future GUI that only displays
// what this emits. Thread-safe so later OpenMP/MPI code can log without
// interleaving output.
//
// Usage: cfd::Logger::instance().info("Starting SIMPLE solver");
//
// P13-005: EVERY level writes to stderr. stdout belongs to the program's
// results -- the CLI's report is parsed by scripts and asserted on by tests --
// and a diagnostic that lands there corrupts it. Keeping the two channels
// apart is what makes `cfdapp --case x > report.txt 2> diagnostics.log` work,
// and what let the diagnostics below be added at all.
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
