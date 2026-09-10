#include <algorithm>
#include <array>

#include "JsonUtil.hpp"
#include "Parsers.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io::detail {

using cfd::io::BoundaryConfig;
using cfd::io::ConcentrationBoundarySpec;
using cfd::io::PatchBoundaryConfig;
using cfd::io::PressureBoundarySpec;
using cfd::io::TemperatureBoundarySpec;
using cfd::io::VelocityBoundarySpec;

namespace {

constexpr std::array<std::string_view, 5> kVelocityTypes{"wall", "moving_wall", "inlet", "outlet",
                                                         "symmetry"};
constexpr std::array<std::string_view, 2> kPressureTypes{"fixed_value", "fixed_gradient"};
// Only these two velocity types are a prescribed vector, so only they
// take a "value" array (TODO.md P1 section 12: "required parameters
// present" is a per-type constraint, not a blanket one).
constexpr std::array<std::string_view, 2> kVelocityTypesWithValue{"moving_wall", "inlet"};
// P2-THERMAL-004: temperature's three thermal-specific BC types
// (boundary::FixedTemperature/HeatFlux/Adiabatic -- P2-THERMAL-003).
// Only fixed_temperature/heat_flux take a "value" -- adiabatic has none,
// same shape as kVelocityTypesWithValue above.
constexpr std::array<std::string_view, 3> kTemperatureTypes{"fixed_temperature", "heat_flux",
                                                            "adiabatic"};
constexpr std::array<std::string_view, 2> kTemperatureTypesWithValue{"fixed_temperature",
                                                                     "heat_flux"};

bool isOneOf(std::string_view value, const auto& options) {
  return std::any_of(options.begin(), options.end(),
                     [&](std::string_view o) { return o == value; });
}

VelocityBoundarySpec parseVelocitySpec(const nlohmann::json& json,
                                       const std::filesystem::path& path,
                                       const std::string& patchName) {
  const std::string context = "patches." + patchName + ".velocity";
  requireObject(json, path, context);

  VelocityBoundarySpec spec;
  spec.type = getRequiredString(json, path, "type", context + ".type");
  if (!isOneOf(spec.type, kVelocityTypes)) {
    throwConfigError(path, context + ".type",
                     "be one of: wall, moving_wall, inlet, outlet, symmetry", spec.type);
  }

  const bool needsValue = isOneOf(spec.type, kVelocityTypesWithValue);
  rejectUnknownKeys(json, path, context,
                    needsValue ? std::vector<std::string_view>{"type", "value"}
                               : std::vector<std::string_view>{"type"});
  if (needsValue) {
    const auto components = getRequiredVector2(json, path, "value", context + ".value");
    spec.value = Vector2{components[0], components[1]};
  }
  return spec;
}

PressureBoundarySpec parsePressureSpec(const nlohmann::json& json,
                                       const std::filesystem::path& path,
                                       const std::string& patchName) {
  const std::string context = "patches." + patchName + ".pressure";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"type", "value"});

  PressureBoundarySpec spec;
  spec.type = getRequiredString(json, path, "type", context + ".type");
  if (!isOneOf(spec.type, kPressureTypes)) {
    throwConfigError(path, context + ".type", "be one of: fixed_value, fixed_gradient", spec.type);
  }
  spec.value = getRequiredReal(json, path, "value", context + ".value");
  return spec;
}

// P6-PHYS-001: one species's concentration BC on one patch -- same two
// types, same always-required "value", as parsePressureSpec above (see
// ConcentrationBoundarySpec's own header comment for why); deliberately
// reuses kPressureTypes rather than a byte-identical second constant.
ConcentrationBoundarySpec parseConcentrationSpec(const nlohmann::json& json,
                                                 const std::filesystem::path& path,
                                                 const std::string& patchName,
                                                 const std::string& speciesName) {
  const std::string context = "patches." + patchName + ".species." + speciesName;
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"type", "value"});

  ConcentrationBoundarySpec spec;
  spec.type = getRequiredString(json, path, "type", context + ".type");
  if (!isOneOf(spec.type, kPressureTypes)) {
    throwConfigError(path, context + ".type", "be one of: fixed_value, fixed_gradient", spec.type);
  }
  spec.value = getRequiredReal(json, path, "value", context + ".value");
  return spec;
}

TemperatureBoundarySpec parseTemperatureSpec(const nlohmann::json& json,
                                             const std::filesystem::path& path,
                                             const std::string& patchName) {
  const std::string context = "patches." + patchName + ".temperature";
  requireObject(json, path, context);

  TemperatureBoundarySpec spec;
  spec.type = getRequiredString(json, path, "type", context + ".type");
  if (!isOneOf(spec.type, kTemperatureTypes)) {
    throwConfigError(path, context + ".type", "be one of: fixed_temperature, heat_flux, adiabatic",
                     spec.type);
  }

  const bool needsValue = isOneOf(spec.type, kTemperatureTypesWithValue);
  rejectUnknownKeys(json, path, context,
                    needsValue ? std::vector<std::string_view>{"type", "value"}
                               : std::vector<std::string_view>{"type"});
  if (needsValue) {
    spec.value = getRequiredReal(json, path, "value", context + ".value");
  }
  return spec;
}

}  // namespace

BoundaryConfig parseBoundaryConfig(const nlohmann::json& json, const std::filesystem::path& path,
                                   bool thermalEnabled,
                                   const std::vector<std::string>& speciesNames) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "boundaries.json", {"patches"});
  requireField(json, path, "patches");
  const auto& patches = json.at("patches");
  requireObject(patches, path, "patches");

  const bool speciesEnabled = !speciesNames.empty();

  BoundaryConfig config;
  for (const auto& [patchName, patchJson] : patches.items()) {
    const std::string context = "boundaries.json patch \"" + patchName + "\"";
    requireObject(patchJson, path, context);
    std::vector<std::string_view> allowedKeys{"velocity", "pressure"};
    if (thermalEnabled) allowedKeys.push_back("temperature");
    if (speciesEnabled) allowedKeys.push_back("species");
    rejectUnknownKeys(patchJson, path, context, allowedKeys);
    requireField(patchJson, path, "velocity");
    requireField(patchJson, path, "pressure");
    if (thermalEnabled) {
      requireField(patchJson, path, "temperature");
    }
    if (speciesEnabled) {
      requireField(patchJson, path, "species");
    }

    PatchBoundaryConfig patchConfig;
    patchConfig.velocity = parseVelocitySpec(patchJson.at("velocity"), path, patchName);
    patchConfig.pressure = parsePressureSpec(patchJson.at("pressure"), path, patchName);
    if (thermalEnabled) {
      patchConfig.temperature = parseTemperatureSpec(patchJson.at("temperature"), path, patchName);
    }
    // P6-PHYS-001: "species" must supply exactly the declared name set --
    // no fewer (an unconfigured species would otherwise reach
    // SpeciesSolver with no BC at all), no more (a typo'd name that will
    // never match a declared species) -- same reasoning as CaseReader.cpp's
    // own "exactly the four mesh patches" cross-check, applied one level
    // deeper.
    if (speciesEnabled) {
      const std::string speciesContext = context + ".species";
      const auto& speciesJson = patchJson.at("species");
      requireObject(speciesJson, path, speciesContext);
      for (const std::string& speciesName : speciesNames) {
        requireField(speciesJson, path, speciesName, speciesContext + "." + speciesName);
        patchConfig.concentration.emplace(
            speciesName,
            parseConcentrationSpec(speciesJson.at(speciesName), path, patchName, speciesName));
      }
      if (speciesJson.size() != speciesNames.size()) {
        for (const auto& [name, unused] : speciesJson.items()) {
          (void)unused;
          const bool known =
              std::find(speciesNames.begin(), speciesNames.end(), name) != speciesNames.end();
          if (!known) {
            throwConfigError(path, speciesContext + "." + name,
                             "name a species declared in physics.json's \"species\" array", name);
          }
        }
      }
    }
    // A duplicate JSON key within one object is not representable once
    // parsed (the underlying library keeps only the last occurrence), so
    // there is no separate "duplicate patch" case to detect here --
    // .items() already yields each patch name once.
    config.patches.emplace(patchName, std::move(patchConfig));
  }
  return config;
}

}  // namespace cfd::io::detail
