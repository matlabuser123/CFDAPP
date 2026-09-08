#include "JsonUtil.hpp"
#include "Parsers.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io::detail {

using cfd::io::CaseConfig;
using cfd::io::InitialConditions;

// case.json carries both this function's scalar fields and the six file
// references CaseReader resolves separately -- so the allowed-keys list
// here has to include the reference keys too, even though this function
// does not read them itself (rejecting them as "unknown" would be wrong;
// CaseReader.cpp reads them directly off the same parsed document).
CaseConfig parseCaseConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "case.json",
                    {"name", "description", "format_version", "geometry", "mesh", "physics",
                     "boundaries", "solver", "initial_conditions"});

  CaseConfig config;
  config.name = getRequiredString(json, path, "name");
  config.description = getOptionalString(json, path, "description", "");

  config.formatVersion = getOptionalInt(json, path, "format_version", 1);
  // TODO.md P1 section 44: only version 1 exists so far.
  if (config.formatVersion != 1) {
    throwConfigError(path, "format_version", "be one of: 1", std::to_string(config.formatVersion));
  }
  return config;
}

InitialConditions parseInitialConditions(const nlohmann::json& json,
                                         const std::filesystem::path& path) {
  InitialConditions config;
  if (!json.contains("initial_conditions")) {
    return config;  // zero velocity/pressure default (section 26).
  }
  const auto& block = json.at("initial_conditions");
  requireObject(block, path, "initial_conditions");
  rejectUnknownKeys(block, path, "initial_conditions", {"velocity", "pressure"});

  const auto velocity = getRequiredVector2(block, path, "velocity", "initial_conditions.velocity");
  config.velocity = Vector2{velocity[0], velocity[1]};
  config.pressure = getRequiredReal(block, path, "pressure", "initial_conditions.pressure");
  return config;
}

}  // namespace cfd::io::detail
