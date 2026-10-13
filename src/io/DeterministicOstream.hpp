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

// P13-004: the other half, and the half that was missing.
//
// Opening a file successfully says nothing about whether the bytes reached it.
// A full disk, an exhausted quota or an I/O error part way through leaves
// std::ofstream in a failed state, and the destructor closes it without
// complaint -- so a writer that only checked the open returned normally, the
// exporter reported success, and the user got a `Results:` block listing empty
// files and exit code 0.
//
// Measured, not theorised: every export writer returned normally when written
// to /dev/full, which opens fine and fails every write with ENOSPC
// (results/p13/export/logs/00_partial_write_BASELINE.log -- 4 of 4).
//
// Call this after writing and before returning. flush() forces the stream's
// buffer at the OS, which is where a full disk actually reports itself; the
// state check then catches both that and any earlier failure, since a
// std::ios failure bit is sticky.
//
// Note what this does NOT promise: the bytes are in the operating system's
// hands, not necessarily on the physical device -- that needs fsync, which
// std::ofstream cannot express. The distinction matters for a power cut, not
// for a full disk, and a full disk is the failure this closes.
inline void requireDeterministicOutputWritten(std::ofstream& out,
                                              const std::filesystem::path& path) {
  out.flush();
  if (!out) {
    throw IOError("Failed while writing output file (the file may be incomplete): " +
                  path.string());
  }
}

}  // namespace cfd::io::detail
