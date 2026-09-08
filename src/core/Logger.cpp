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

bool targetsStderr(LogLevel level) noexcept {
  return level == LogLevel::Warning || level == LogLevel::Error;
}

}  // namespace

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
