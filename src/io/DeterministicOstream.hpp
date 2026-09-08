#pragma once

// Shared formatting convention for every deterministic export file (CSV,
// legacy VTK -- JSON uses nlohmann::json's own numeric serialization
// instead, see JSONWriter.cpp). P1 -- Result Export section 6-7: one
// explicit floating-point format everywhere (scientific, 17 significant
// digits -- enough to round-trip a double exactly), and the classic "C"
// locale so the decimal separator is always "." regardless of the
// running machine's locale.

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>

#include "cfd/core/Exception.hpp"

namespace cfd::io::detail {

// Opens `path` for writing and configures it with this project's one
// deterministic-export formatting convention. Throws IOError if the file
// cannot be opened (P1 section 10: reject an unwritable output path
// rather than silently producing a truncated/partial file).
inline std::ofstream openDeterministicOutput(const std::filesystem::path& path) {
  std::ofstream out(path);
  if (!out) {
    throw IOError("Could not open output file for writing: " + path.string());
  }
  out.imbue(std::locale::classic());
  out << std::scientific << std::setprecision(17);
  return out;
}

}  // namespace cfd::io::detail
