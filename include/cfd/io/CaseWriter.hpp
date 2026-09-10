#pragma once

// P5-A -- Production Case Manager: the write-side counterpart to
// CaseReader (TODO.md P5 section 4's "a case created by the GUI must
// also be runnable from the CLI" / "a CLI-created case must be loadable
// by the GUI" round-trip requirement). CaseWriter serializes a
// CaseDefinition to the exact same six-file case-directory schema
// CaseReader::read() parses (case.json/geometry.json/mesh.json/
// physics.json/boundaries.json/solver.json) -- there is deliberately no
// second, GUI-only case format (section 4's explicit prohibition).
//
// Round-trip identity: for any CaseDefinition `d` produced by
// CaseReader::read(), `CaseReader{}.read(CaseWriter{}.write(dir, d))`
// reproduces `d` field-for-field (tested directly, not just "looks
// right" -- tests/unit/io/test_case_writer.cpp).

#include <filesystem>

#include "cfd/io/case/CaseDefinition.hpp"

namespace cfd::io {

class CaseWriter {
 public:
  // Creates `caseDirectory` if missing, then writes all six files.
  // Overwrites any files already there (Save/Save-As both call this the
  // same way -- Save-As only differs in which directory the caller
  // passes). Throws cfd::IOError if `caseDirectory` cannot be created or
  // a file cannot be opened for writing.
  static void write(const std::filesystem::path& caseDirectory, const CaseDefinition& definition);
};

}  // namespace cfd::io
