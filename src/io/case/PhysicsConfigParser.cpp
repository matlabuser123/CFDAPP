#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::PhysicsConfig;

PhysicsConfig parsePhysicsConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "physics.json",
                    {"model", "density", "dynamic_viscosity", "reynolds_number"});

  PhysicsConfig config;
  config.model = getRequiredString(json, path, "model");
  if (config.model != "incompressible_laminar") {
    throwConfigError(path, "model", "be one of: incompressible_laminar", config.model);
  }

  config.density = getRequiredReal(json, path, "density");
  if (!(config.density > 0.0)) {
    throwConfigError(path, "density", "be > 0", std::to_string(config.density));
  }
  config.dynamicViscosity = getRequiredReal(json, path, "dynamic_viscosity");
  if (!(config.dynamicViscosity > 0.0)) {
    throwConfigError(path, "dynamic_viscosity", "be > 0", std::to_string(config.dynamicViscosity));
  }

  // Reporting-only metadata (TODO.md P1 section 10): never used to derive
  // density/viscosity, so no relationship to them is enforced here --
  // only that, if present, it is itself a finite number.
  if (json.contains("reynolds_number")) {
    config.reynoldsNumber = getRequiredReal(json, path, "reynolds_number");
  }
  return config;
}

}  // namespace cfd::io::detail
