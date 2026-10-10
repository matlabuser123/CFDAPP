#include "cfd/core/Logger.hpp"

#include <iostream>
#include <mutex>

namespace cfd {

namespace {

std::string_view levelTag(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warning:
      return "WARNING";
    case LogLevel::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

// P13-005: EVERY level goes to stderr, not just Warning and Error.
//
// Diagnostics and results must not share a channel. stdout is the CLI's
// report -- `Converged: yes`, the residuals, the paths of the exported files --
// and scripts parse it; tests/CMakeLists.txt asserts on it. Routing Trace,
// Debug and Info there too meant that turning logging on would corrupt a
// machine-readable report, which is why the brief's diagnostics could not
// simply be added at Info.
//
// Nothing changes for the two pre-existing call sites: both log at Warning,
// which already went to stderr. What changes is that the other three levels
// become usable at all.
bool targetsStderr(LogLevel) noexcept { return true; }

}  // namespace

std::optional<LogLevel> parseLogLevel(std::string_view name) noexcept {
  if (name == "trace") return LogLevel::Trace;
  if (name == "debug") return LogLevel::Debug;
  if (name == "info") return LogLevel::Info;
  if (name == "warning") return LogLevel::Warning;
  if (name == "error") return LogLevel::Error;
  return std::nullopt;
}

std::string_view logLevelName(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warning:
      return "warning";
    case LogLevel::Error:
      return "error";
  }
  return "unknown";
}

std::string_view logLevelNames() noexcept { return "trace, debug, info, warning, error"; }

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::setLevel(LogLevel level) {
  const std::lock_guard<std::mutex> lock(mutex_);
  level_ = level;
}

LogLevel Logger::level() const noexcept {
  const std::lock_guard<std::mutex> lock(mutex_);
  return level_;
}

void Logger::log(LogLevel level, std::string_view message) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (static_cast<int>(level) < static_cast<int>(level_)) {
    return;
  }
  std::ostream& stream = targetsStderr(level) ? std::cerr : std::cout;
  stream << '[' << levelTag(level) << "] " << message << '\n';
}

void Logger::trace(std::string_view message) { log(LogLevel::Trace, message); }
void Logger::debug(std::string_view message) { log(LogLevel::Debug, message); }
void Logger::info(std::string_view message) { log(LogLevel::Info, message); }
void Logger::warning(std::string_view message) { log(LogLevel::Warning, message); }
void Logger::error(std::string_view message) { log(LogLevel::Error, message); }

}  // namespace cfd
