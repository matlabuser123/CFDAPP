#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::BuoyancyPhysicsConfig;
using cfd::io::PhysicsConfig;
using cfd::io::ThermalPhysicsConfig;
using cfd::io::TurbulencePhysicsConfig;

namespace {

// P2-THERMAL-004: physics.json's optional "thermal" object. Parsed the
// same "required fields, positive/finite" style as density/dynamic_
// viscosity above -- redundant with ThermalProperties's own construction-
// time validation (CaseBuilder builds one from this), but that
// redundancy is deliberate: two independent, consistent validation
// layers, matching this file's own precedent (see CaseBuilder.hpp's
// header comment) and giving a parse-time error that names the exact
// JSON field rather than a generic construction-time message.
ThermalPhysicsConfig parseThermalPhysicsConfig(const nlohmann::json& json,
                                               const std::filesystem::path& path) {
  const std::string context = "physics.json thermal";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"conductivity", "specific_heat", "initial_temperature"});

  ThermalPhysicsConfig thermal;
  thermal.conductivity = getRequiredReal(json, path, "conductivity", context + ".conductivity");
  if (!(thermal.conductivity > 0.0)) {
    throwConfigError(path, context + ".conductivity", "be > 0",
                     std::to_string(thermal.conductivity));
  }
  thermal.specificHeat = getRequiredReal(json, path, "specific_heat", context + ".specific_heat");
  if (!(thermal.specificHeat > 0.0)) {
    throwConfigError(path, context + ".specific_heat", "be > 0",
                     std::to_string(thermal.specificHeat));
  }
  // Any finite value is physically acceptable here (a chosen reference
  // scale, not a rho/cp/k-style quantity with a strict-positivity
  // requirement) -- getRequiredReal already enforces finiteness.
  thermal.initialTemperature =
      getRequiredReal(json, path, "initial_temperature", context + ".initial_temperature");
  return thermal;
}

// P3-PHYS-001: physics.json's optional "buoyancy" object -- presence is
// the enable flag (same convention as parseThermalPhysicsConfig above).
// "model" must be "boussinesq" (the only model this codebase
// implements). beta must be finite and >= 0 (negative thermal-expansion
// coefficients are rejected here too, redundant with
// BoussinesqBuoyancy's own construction-time check -- same "two
// independent, consistent validation layers" precedent as
// parseThermalPhysicsConfig). referenceTemperature must be finite (any
// value is physically acceptable, a chosen reference scale -- same
// reasoning as ThermalPhysicsConfig's own initialTemperature). gravity
// is a required 2-component finite array.
BuoyancyPhysicsConfig parseBuoyancyPhysicsConfig(const nlohmann::json& json,
                                                 const std::filesystem::path& path) {
  const std::string context = "physics.json buoyancy";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"model", "beta", "reference_temperature", "gravity"});

  const std::string model = getRequiredString(json, path, "model", context + ".model");
  if (model != "boussinesq") {
    throwConfigError(path, context + ".model", "be one of: boussinesq", model);
  }

  BuoyancyPhysicsConfig buoyancy;
  buoyancy.beta = getRequiredReal(json, path, "beta", context + ".beta");
  if (!(buoyancy.beta >= 0.0)) {
    throwConfigError(path, context + ".beta", "be >= 0", std::to_string(buoyancy.beta));
  }
  buoyancy.referenceTemperature =
      getRequiredReal(json, path, "reference_temperature", context + ".reference_temperature");

  const auto gravity = getRequiredVector2(json, path, "gravity", context + ".gravity");
  buoyancy.gravity = Vector2{gravity[0], gravity[1]};
  return buoyancy;
}

// Parses and validates a (0, 1]-range relaxation factor, shared by
// k_relaxation/epsilon_relaxation/omega_relaxation below -- one place
// for the "same range as SIMPLESettings::velocityRelaxation" rule
// (TODO.md P0 section 5) rather than three near-identical copies.
Real parseRelaxationFactor(const nlohmann::json& json, const std::filesystem::path& path,
                           std::string_view field, const std::string& label) {
  const Real value = getRequiredReal(json, path, field, label);
  if (!(value > 0.0) || value > 1.0) {
    throwConfigError(path, label, "be in (0, 1]", std::to_string(value));
  }
  return value;
}

// P2-TURB-004 (extended by P2-TURB-005 and P2-TURB-006): physics.json's
// optional "turbulence" object. "model" must be one of the names
// turbulence::createTurbulenceModel() actually supports ("laminar",
// "k_epsilon", "k_omega", "sst") -- rejecting anything else here, at
// parse time, gives a case-file-specific error naming the exact bad
// field rather than deferring to createTurbulenceModel()'s own generic
// InvalidArgumentError deep inside CaseBuilder (same "two independent,
// consistent validation layers" precedent as parseThermalPhysicsConfig
// above).
//
// initial_k is always required (k_epsilon, k_omega, and sst all
// transport a k equation). Exactly one of initial_epsilon/initial_omega
// must be present -- never both, never neither -- and it must match the
// chosen model when that model is "k_epsilon", "k_omega", or "sst" (an
// explicit "laminar" declaration accepts either, since neither is
// actually used once CaseBuilder collapses "laminar" to no turbulence
// config at all; see SimulationSetup.hpp's own comment). "sst" shares
// initial_omega/omega_relaxation with "k_omega" -- SST also transports
// an omega equation, not epsilon -- so it is grouped with k_omega in
// every hasOmega/hasEpsilon check below rather than needing a third
// field pair. This mirrors KEpsilonModel's/KOmegaModel's/SSTModel's own
// "must never start undefined" construction-time invariant for
// whichever second transported scalar applies. k_relaxation is always
// optional; epsilon_relaxation/omega_relaxation are optional and only
// accepted alongside the matching initial_epsilon/initial_omega field,
// to avoid a dead configuration value with no model to apply to.
TurbulencePhysicsConfig parseTurbulencePhysicsConfig(const nlohmann::json& json,
                                                     const std::filesystem::path& path) {
  const std::string context = "physics.json turbulence";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"model", "initial_k", "initial_epsilon", "initial_omega", "k_relaxation",
                     "epsilon_relaxation", "omega_relaxation"});

  TurbulencePhysicsConfig turbulence;
  turbulence.model = getRequiredString(json, path, "model", context + ".model");
  if (turbulence.model != "laminar" && turbulence.model != "k_epsilon" &&
      turbulence.model != "k_omega" && turbulence.model != "sst") {
    throwConfigError(path, context + ".model", "be one of: laminar, k_epsilon, k_omega, sst",
                     turbulence.model);
  }

  turbulence.initialK = getRequiredReal(json, path, "initial_k", context + ".initial_k");
  if (!(turbulence.initialK > 0.0)) {
    throwConfigError(path, context + ".initial_k", "be > 0", std::to_string(turbulence.initialK));
  }

  const bool hasEpsilon = json.contains("initial_epsilon");
  const bool hasOmega = json.contains("initial_omega");
  if (hasEpsilon == hasOmega) {
    throwConfigError(path, context, "have exactly one of initial_epsilon/initial_omega",
                     hasEpsilon ? "both present" : "neither present");
  }
  if (turbulence.model == "k_epsilon" && !hasEpsilon) {
    throwConfigError(path, context, "have initial_epsilon (model is k_epsilon)",
                     "initial_omega instead");
  }
  if ((turbulence.model == "k_omega" || turbulence.model == "sst") && !hasOmega) {
    throwConfigError(path, context, "have initial_omega (model is " + turbulence.model + ")",
                     "initial_epsilon instead");
  }

  if (hasEpsilon) {
    const Real value = getRequiredReal(json, path, "initial_epsilon", context + ".initial_epsilon");
    if (!(value > 0.0)) {
      throwConfigError(path, context + ".initial_epsilon", "be > 0", std::to_string(value));
    }
    turbulence.initialEpsilon = value;
  } else {
    const Real value = getRequiredReal(json, path, "initial_omega", context + ".initial_omega");
    if (!(value > 0.0)) {
      throwConfigError(path, context + ".initial_omega", "be > 0", std::to_string(value));
    }
    turbulence.initialOmega = value;
  }

  if (json.contains("k_relaxation")) {
    turbulence.kRelaxation =
        parseRelaxationFactor(json, path, "k_relaxation", context + ".k_relaxation");
  }
  if (json.contains("epsilon_relaxation")) {
    if (!hasEpsilon) {
      throwConfigError(path, context + ".epsilon_relaxation",
                       "be present only alongside initial_epsilon", "initial_omega was given");
    }
    turbulence.epsilonRelaxation =
        parseRelaxationFactor(json, path, "epsilon_relaxation", context + ".epsilon_relaxation");
  }
  if (json.contains("omega_relaxation")) {
    if (!hasOmega) {
      throwConfigError(path, context + ".omega_relaxation",
                       "be present only alongside initial_omega", "initial_epsilon was given");
    }
    turbulence.omegaRelaxation =
        parseRelaxationFactor(json, path, "omega_relaxation", context + ".omega_relaxation");
  }
  return turbulence;
}

}  // namespace

PhysicsConfig parsePhysicsConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "physics.json",
                    {"model", "density", "dynamic_viscosity", "reynolds_number", "thermal",
                     "turbulence", "buoyancy"});

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

  // P2-THERMAL-004: presence of "thermal" is the enable flag (see
  // PhysicsConfig.hpp's own header comment) -- absent means every
  // existing nonthermal case continues to parse identically.
  if (json.contains("thermal")) {
    config.thermal = parseThermalPhysicsConfig(json.at("thermal"), path);
  }
  // P2-TURB-004: presence of "turbulence" is the enable flag, same
  // convention as "thermal" above -- absent means laminar, every
  // pre-P2-TURB-004 case continues to parse identically.
  if (json.contains("turbulence")) {
    config.turbulence = parseTurbulencePhysicsConfig(json.at("turbulence"), path);
  }
  // P3-PHYS-001: presence of "buoyancy" is the enable flag, same
  // convention as "thermal"/"turbulence" above -- absent means no
  // buoyancy source, every pre-P3-PHYS-001 case continues to parse
  // identically. A buoyancy source needs a temperature field to
  // evaluate against, and this codebase's only source of one is the
  // "thermal" block, so "buoyancy" without "thermal" is rejected here
  // rather than silently parsing into a config CaseBuilder could never
  // actually drive with a real temperature field.
  if (json.contains("buoyancy")) {
    if (!config.thermal.has_value()) {
      throwConfigError(path, "buoyancy", "be present only alongside a \"thermal\" block",
                       "no \"thermal\" block was given");
    }
    config.buoyancy = parseBuoyancyPhysicsConfig(json.at("buoyancy"), path);
  }
  return config;
}

}  // namespace cfd::io::detail
