#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "JsonUtil.hpp"
#include "Parsers.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/io/case/BoundaryVocabulary.hpp"

namespace cfd::io::detail {

using cfd::io::AlphaBoundarySpec;
using cfd::io::BoundaryConfig;
using cfd::io::ConcentrationBoundarySpec;
using cfd::io::kPressureTypes;
using cfd::io::kTemperatureTypes;
using cfd::io::kTemperatureTypesWithValue;
using cfd::io::kVelocityTypes;
using cfd::io::kVelocityTypesWithValue;
using cfd::io::PatchBoundaryConfig;
using cfd::io::PressureBoundarySpec;
using cfd::io::TemperatureBoundarySpec;
using cfd::io::VelocityBoundarySpec;

namespace {

bool isOneOf(std::string_view value, const auto& options) {
  return std::any_of(options.begin(), options.end(),
                     [&](std::string_view o) { return o == value; });
}

VelocityBoundarySpec parseVelocitySpec(const nlohmann::json& json,
                                       const std::filesystem::path& path,
                                       const std::string& patchName, int dimension) {
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
    if (dimension == 3) {  // P12-MESH-006: [u, v, w] in a 3D (box) case.
      const auto components = getRequiredVector3(json, path, "value", context + ".value");
      spec.value = Vector3{components[0], components[1], components[2]};
    } else {
      const auto components = getRequiredVector2(json, path, "value", context + ".value");
      spec.value = Vector2{components[0], components[1]};
    }
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

// P6-PHYS-002: phase-1 volume-fraction BC on one patch -- same two
// types, same always-required "value", as parsePressureSpec above (see
// AlphaBoundarySpec's own header comment for why).
AlphaBoundarySpec parseAlphaSpec(const nlohmann::json& json, const std::filesystem::path& path,
                                 const std::string& patchName) {
  const std::string context = "patches." + patchName + ".alpha";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"type", "value"});

  AlphaBoundarySpec spec;
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
                                   const std::vector<std::string>& speciesNames,
                                   bool multiphaseEnabled, int dimension, bool compositionSpecies) {
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
    if (multiphaseEnabled) allowedKeys.push_back("alpha");
    rejectUnknownKeys(patchJson, path, context, allowedKeys);
    requireField(patchJson, path, "velocity");
    requireField(patchJson, path, "pressure");
    if (thermalEnabled) {
      requireField(patchJson, path, "temperature");
    }
    if (speciesEnabled) {
      requireField(patchJson, path, "species");
    }
    if (multiphaseEnabled) {
      requireField(patchJson, path, "alpha");
    }

    PatchBoundaryConfig patchConfig;
    patchConfig.velocity = parseVelocitySpec(patchJson.at("velocity"), path, patchName, dimension);
    patchConfig.pressure = parsePressureSpec(patchJson.at("pressure"), path, patchName);
    if (thermalEnabled) {
      patchConfig.temperature = parseTemperatureSpec(patchJson.at("temperature"), path, patchName);
    }
    if (multiphaseEnabled) {
      patchConfig.alpha = parseAlphaSpec(patchJson.at("alpha"), path, patchName);
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

      // TD-001 / gate C11: THE SHARED MULTI-SPECIES BOUNDARY CONSTRAINT.
      //
      // For a reacting case the per-patch entries are a COMPOSITION, not a set of unrelated
      // tracers, so the `fixed_value` ones must sum to 1. P18.2 recorded the gap this closes:
      // "No multi-species boundary type, so sum Y_i = 1 is not enforced across a shared patch; the
      // per-species boundary sets are independent objects."
      //
      // WHY IT IS CHECKED HERE, at parse time, rather than left to P18.1. MassFractionState does
      // refuse an out-of-bounds composition -- but only at a STEP BOUNDARY, i.e. after a solve has
      // already consumed the patch values. A patch declaring 0.1/0.0/0.8 would be used by the
      // transport assembly first and refused afterwards, if at all. Refusing the FILE is the only
      // point at which the user learns which patch is wrong.
      //
      // Only `fixed_value` entries are summed. A `fixed_gradient` entry states a flux, not a mass
      // fraction, and summing it with compositions would be a category error -- an all-gradient
      // patch (the usual outlet/wall case) therefore has no constraint to satisfy, which is why
      // the all-gradient patches in the TD-001 case are accepted.
      if (compositionSpecies) {
        Real fixedSum = 0.0;
        std::size_t fixedCount = 0;
        for (const std::string& speciesName : speciesNames) {
          const ConcentrationBoundarySpec& spec = patchConfig.concentration.at(speciesName);
          if (spec.type == "fixed_value") {
            fixedSum += spec.value;
            ++fixedCount;
          }
        }
        // A composition is only declared when EVERY species is fixed on the patch. A partial set
        // (some fixed, some gradient) does not define one, and demanding it summed to 1 would
        // reject a legitimate mixed patch.
        if (fixedCount == speciesNames.size() && fixedCount > 0) {
          // 1e-12, matching PhysicsConfigParser's own bound on the initial composition, so the
          // boundary and the initial state are held to one tolerance rather than two.
          if (std::abs(fixedSum - 1.0) > 1.0e-12) {
            throwConfigError(path, speciesContext,
                             "have fixed_value mass fractions summing to 1 across all " +
                                 std::to_string(speciesNames.size()) +
                                 " reacting species (a patch composition is a mixture; this "
                                 "codebase refuses an out-of-bounds composition and never "
                                 "normalises one)",
                             std::to_string(fixedSum));
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
