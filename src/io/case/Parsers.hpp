#pragma once

// Internal per-file parse+validate functions, one per case sub-file.
// Each takes the already-parsed JSON document and the source path (for
// error messages only) and returns a fully-validated typed config or
// throws CaseConfigurationError. Not a public header -- CaseReader.cpp is
// the only caller.

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cfd/io/case/BoundaryConfig.hpp"
#include "cfd/io/case/CaseConfig.hpp"
#include "cfd/io/case/GeometryConfig.hpp"
#include "cfd/io/case/InitialConditions.hpp"
#include "cfd/io/case/MeshConfig.hpp"
#include "cfd/io/case/PhysicsConfig.hpp"
#include "cfd/io/case/SolverConfig.hpp"

namespace cfd::io::detail {

// case.json's own scalar fields (name/description/format_version) --
// file references ("mesh": "mesh.json" etc.) are resolved by CaseReader
// itself, since only it knows the case directory to resolve them against.
[[nodiscard]] CaseConfig parseCaseConfig(const nlohmann::json& json,
                                         const std::filesystem::path& path);

// case.json's optional "initial_conditions" object. Returns the zero
// default (TODO.md P1 section 26) if the key is absent.
[[nodiscard]] InitialConditions parseInitialConditions(const nlohmann::json& json,
                                                       const std::filesystem::path& path);

[[nodiscard]] cfd::io::GeometryConfig parseGeometryConfig(const nlohmann::json& json,
                                                          const std::filesystem::path& path);

[[nodiscard]] cfd::io::MeshConfig parseMeshConfig(const nlohmann::json& json,
                                                  const std::filesystem::path& path);

[[nodiscard]] cfd::io::PhysicsConfig parsePhysicsConfig(const nlohmann::json& json,
                                                        const std::filesystem::path& path);

// The full boundaries.json object, keyed by patch name. Only *per-patch*
// structural validation happens here (known BC type, required
// parameters, no unknown fields) -- checking that the configured patch
// set matches the mesh's actual patches is cross-file validation, done by
// CaseReader after this returns (TODO.md P1 section 42). thermalEnabled
// (P2-THERMAL-004, sourced from physics.json's "thermal" presence,
// already parsed by the time CaseReader reaches this file) decides
// whether each patch's "temperature" key is required or forbidden -- an
// existing nonthermal case has no such key today and must continue to
// parse identically, so this is not an optional/ignored field either way.
// speciesNames (P6-PHYS-001, sourced from physics.json's "species" array,
// already parsed by the time CaseReader reaches this file) is the exact
// set of per-patch "species" object keys required -- empty means no
// patch may have a "species" key at all (same "required/forbidden, never
// silently ignored" convention as thermalEnabled, generalized from one
// boolean to a required key set). multiphaseEnabled (P6-PHYS-002,
// sourced from physics.json's "multiphase" presence) decides whether
// each patch's "alpha" key is required or forbidden, same convention as
// thermalEnabled.
[[nodiscard]] cfd::io::BoundaryConfig parseBoundaryConfig(
    const nlohmann::json& json, const std::filesystem::path& path, bool thermalEnabled,
    const std::vector<std::string>& speciesNames, bool multiphaseEnabled);

[[nodiscard]] cfd::io::SolverConfig parseSolverConfig(const nlohmann::json& json,
                                                      const std::filesystem::path& path);

}  // namespace cfd::io::detail
