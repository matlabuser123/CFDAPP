#pragma once

#include <filesystem>

#include "cfd/io/case/CaseDefinition.hpp"

namespace cfd::io {

// Reads, parses, and validates a case directory into a CaseDefinition
// (TODO.md P1 -- Case System). Deliberately does nothing else: no mesh
// generation, no matrix assembly, no touching SIMPLE, no writing result
// files (section 4) -- see CaseBuilder for the conversion from
// CaseDefinition to actual runtime objects (Mesh, FluidProperties,
// BoundaryConditionSet, SIMPLESettings, initial fields).
//
// Throws cfd::IOError if the case directory or a file it (transitively)
// references does not exist or cannot be parsed as well-formed JSON;
// throws cfd::CaseConfigurationError if a file's *content* is invalid
// (missing/out-of-range field, unsupported type, a boundary patch the
// mesh doesn't have, a manifest reference escaping the case directory,
// ...). Every thrown message names the offending file and field (section
// 20) rather than a bare "Invalid case".
class CaseReader {
 public:
  [[nodiscard]] CaseDefinition read(const std::filesystem::path& caseDirectory) const;
};

}  // namespace cfd::io
