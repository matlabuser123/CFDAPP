#include <algorithm>
#include <array>

#include "JsonUtil.hpp"
#include "Parsers.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io::detail {

using cfd::io::BoundaryConfig;
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

TemperatureBoundarySpec parseTemperatureSpec(const nlohmann::json& json,
                                             const std::filesystem::path& path,
                                             const std::string& patchName) {
  const std::string context = "patches." + patchName + ".temperature";
  requireObject(json, path, context);

  TemperatureBoundarySpec spec;
  spec.type = getRequiredString(json, path, "type", context + ".type");
  if (!isOneOf(spec.type, kTemperatureTypes)) {
    throwConfigError(path, context + ".type",
                     "be one of: fixed_temperature, heat_flux, adiabatic", spec.type);
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
                                   bool thermalEnabled) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "boundaries.json", {"patches"});
  requireField(json, path, "patches");
  const auto& patches = json.at("patches");
  requireObject(patches, path, "patches");

  BoundaryConfig config;
  for (const auto& [patchName, patchJson] : patches.items()) {
    const std::string context = "boundaries.json patch \"" + patchName + "\"";
    requireObject(patchJson, path, context);
    rejectUnknownKeys(patchJson, path, context,
                      thermalEnabled ? std::vector<std::string_view>{"velocity", "pressure",
                                                                     "temperature"}
                                     : std::vector<std::string_view>{"velocity", "pressure"});
    requireField(patchJson, path, "velocity");
    requireField(patchJson, path, "pressure");
    if (thermalEnabled) {
      requireField(patchJson, path, "temperature");
    }

    PatchBoundaryConfig patchConfig;
    patchConfig.velocity = parseVelocitySpec(patchJson.at("velocity"), path, patchName);
    patchConfig.pressure = parsePressureSpec(patchJson.at("pressure"), path, patchName);
    if (thermalEnabled) {
      patchConfig.temperature =
          parseTemperatureSpec(patchJson.at("temperature"), path, patchName);
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
