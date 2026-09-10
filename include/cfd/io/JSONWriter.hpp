#pragma once

// P1 -- Result Export: deterministic run metadata as JSON. Deliberately
// independent of the case-loading pipeline (cfd::io::case::*) -- this
// header only needs the small set of descriptive fields a metadata.json
// actually carries (RunMetadata below), not a full CaseDefinition, so it
// stays usable (and unit-testable) without a case directory on disk.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

// The descriptive (non-solver-derived) fields metadata.json needs.
// Everything else in the file (mesh cell/face counts, solver status/
// iterations/residuals/mass imbalance) comes directly from Mesh/
// SIMPLEResult, never duplicated here.
struct RunMetadata {
  std::string caseName;
  cfd::Real density{};
  cfd::Real dynamicViscosity{};
  std::string solverType;
};

// P2-THERMAL-004: the small set of thermal::ThermalResult-derived fields
// metadata.json needs, kept as its own lightweight struct (not a
// cfd::thermal::ThermalResult/ThermalProperties reference) so this header
// stays independent of the thermal module the same way it already is of
// the case-loading pipeline -- the caller (CLI) is the one place that
// already has both a ThermalResult and a ThermalProperties, so it builds
// this from them.
struct ThermalRunMetadata {
  cfd::Real conductivity{};
  cfd::Real specificHeat{};
  std::string status;  // the exact ThermalStatus enum name (never just a boolean).
  bool converged{};
  cfd::Index iterations{};
  cfd::Real finalResidual{};
};

// P6-PHYS-001: the small set of cfd::species::SpeciesResult-derived
// fields metadata.json needs for one species, same "own lightweight
// struct, not a direct species-module reference" reasoning as
// ThermalRunMetadata above. One entry per physics.json-declared species,
// in declaration order -- see JSONWriter::writeMetadata's own header
// comment on why this is a plain vector (never std::optional-wrapped)
// the same way PhysicsConfig::species/SimulationSetup::species already
// are.
struct SpeciesRunMetadata {
  std::string name;
  cfd::Real diffusivity{};
  std::string status;  // the exact SpeciesStatus enum name (never just a boolean).
  bool converged{};
  cfd::Index iterations{};
  cfd::Real finalResidual{};
};

class JSONWriter {
 public:
  // Writes metadata.json (section 11-16): format_version, case/mesh/
  // physics/solver metadata, the explicit status enum name (never just a
  // boolean -- section 12), final residuals, global mass imbalance, and
  // a finite/non-finite flag -- as real JSON numbers, not stringified
  // (section 16), with stable 2-space-indented key ordering (section 15).
  // nx/ny are inferred from `mesh` itself (StructuredMeshInfo, same
  // convention VTKWriter uses), not re-supplied by the caller. No
  // timestamp/runtime/absolute-path fields are written (section 33/14:
  // those would make byte-identical repeated-run comparison meaningless).
  //
  // `thermal` (P2-THERMAL-004): when present, adds a "thermal" object
  // with conductivity/specific_heat/status/converged/iterations/
  // final_residual. "thermal.enabled" is always written (true iff
  // `thermal` has a value) -- a documented default for every nonthermal
  // case, matching this file's own "no hidden defaults" convention,
  // rather than the whole "thermal" key being absent.
  //
  // `species` (P6-PHYS-001): a JSON array under "species", one object per
  // entry (name/diffusivity/status/converged/iterations/final_residual),
  // in the same declaration order as `species` itself -- always written
  // (possibly empty), the same way a list-shaped field needs no separate
  // "enabled" flag the way thermal's single-optional-object shape does
  // (empty array already means "no species", unambiguously).
  // Throws IOError if `path` cannot be opened.
  static void writeMetadata(const std::filesystem::path& path, const RunMetadata& metadata,
                            const cfd::mesh::Mesh& mesh,
                            const cfd::pressure_velocity::SIMPLEResult& result,
                            const std::optional<ThermalRunMetadata>& thermal = std::nullopt,
                            const std::vector<SpeciesRunMetadata>& species = {});
};

}  // namespace cfd::io
