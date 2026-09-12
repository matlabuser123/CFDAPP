#include <algorithm>
#include <unordered_set>

#include "JsonUtil.hpp"
#include "Parsers.hpp"
#include "cfd/io/case/PhysicsVocabulary.hpp"

namespace cfd::io::detail {

using cfd::io::BuoyancyPhysicsConfig;
using cfd::io::CompressiblePhysicsConfig;
using cfd::io::kTurbulenceModels;
using cfd::io::MultiphasePhysicsConfig;
using cfd::io::PhasePhysicsConfig;
using cfd::io::PhysicsConfig;
using cfd::io::SpeciesConfig;
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
  if (std::ranges::find(kTurbulenceModels, turbulence.model) == kTurbulenceModels.end()) {
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

// P6-PHYS-001: one entry of physics.json's optional "species" array.
// diffusivity may be 0 (SpeciesProperties.hpp's own "pure advection
// intentionally supported" allowance); initial_concentration is any
// finite value (same "no 0<=Y<=1 enforcement at this layer" reasoning as
// PhysicsConfig.hpp's own header comment on SpeciesConfig -- getRequiredReal
// already enforces finiteness, nothing more is a structural requirement
// here). "name" must be non-empty -- SpeciesProperties's own constructor
// already rejects an empty name, so this is a parse-time-specific error
// naming the exact array index rather than deferring to that generic
// construction-time message (same "two independent, consistent
// validation layers" precedent as parseThermalPhysicsConfig above).
SpeciesConfig parseSpeciesConfig(const nlohmann::json& json, const std::filesystem::path& path,
                                 std::size_t index) {
  const std::string context = "physics.json species[" + std::to_string(index) + "]";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"name", "diffusivity", "initial_concentration"});

  SpeciesConfig species;
  species.name = getRequiredString(json, path, "name", context + ".name");
  if (species.name.empty()) {
    throwConfigError(path, context + ".name", "be non-empty", "\"\"");
  }
  species.diffusivity = getRequiredReal(json, path, "diffusivity", context + ".diffusivity");
  if (!(species.diffusivity >= 0.0)) {
    throwConfigError(path, context + ".diffusivity", "be >= 0",
                     std::to_string(species.diffusivity));
  }
  species.initialConcentration =
      getRequiredReal(json, path, "initial_concentration", context + ".initial_concentration");
  return species;
}

// P6-PHYS-002: one phase entry ("phase1"/"phase2") of physics.json's
// "multiphase" block, mirroring cfd::multiphase::PhaseProperties's own
// construction-time validation (name non-empty, density/viscosity
// finite and > 0) -- same "two independent, consistent validation
// layers" precedent as every other physics block in this file.
PhasePhysicsConfig parsePhasePhysicsConfig(const nlohmann::json& json,
                                           const std::filesystem::path& path,
                                           const std::string& label) {
  const std::string context = "physics.json multiphase." + label;
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"name", "density", "viscosity"});

  PhasePhysicsConfig phase;
  phase.name = getRequiredString(json, path, "name", context + ".name");
  if (phase.name.empty()) {
    throwConfigError(path, context + ".name", "be non-empty", "\"\"");
  }
  phase.density = getRequiredReal(json, path, "density", context + ".density");
  if (!(phase.density > 0.0)) {
    throwConfigError(path, context + ".density", "be > 0", std::to_string(phase.density));
  }
  phase.viscosity = getRequiredReal(json, path, "viscosity", context + ".viscosity");
  if (!(phase.viscosity > 0.0)) {
    throwConfigError(path, context + ".viscosity", "be > 0", std::to_string(phase.viscosity));
  }
  return phase;
}

// P6-PHYS-002: physics.json's optional "multiphase" object -- presence
// is the enable flag (same convention as every other block above).
// initial_alpha must lie in [0, 1] (this is the one *configuration*
// input where boundedness is enforced -- unlike VolumeFractionEquation's
// own reporting-only volumeFractionBounds() diagnostic on the *solved*
// field, a deliberately out-of-range starting condition would just be a
// case-file typo, not a numerical-quality signal worth merely reporting).
// transport_time_step must be finite and > 0 (VolumeFractionSolver::step()'s
// own requirement). phase1.name and phase2.name must differ (two
// identically-named phases would make mixture-property reporting
// ambiguous).
MultiphasePhysicsConfig parseMultiphasePhysicsConfig(const nlohmann::json& json,
                                                     const std::filesystem::path& path) {
  const std::string context = "physics.json multiphase";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"phase1", "phase2", "initial_alpha", "transport_time_step"});
  requireField(json, path, "phase1");
  requireField(json, path, "phase2");

  MultiphasePhysicsConfig multiphase;
  multiphase.phase1 = parsePhasePhysicsConfig(json.at("phase1"), path, "phase1");
  multiphase.phase2 = parsePhasePhysicsConfig(json.at("phase2"), path, "phase2");
  if (multiphase.phase1.name == multiphase.phase2.name) {
    throwConfigError(path, context, "have two differently-named phases", multiphase.phase1.name);
  }

  multiphase.initialAlpha =
      getRequiredReal(json, path, "initial_alpha", context + ".initial_alpha");
  if (multiphase.initialAlpha < 0.0 || multiphase.initialAlpha > 1.0) {
    throwConfigError(path, context + ".initial_alpha", "be in [0, 1]",
                     std::to_string(multiphase.initialAlpha));
  }

  multiphase.transportTimeStep =
      getRequiredReal(json, path, "transport_time_step", context + ".transport_time_step");
  if (!(multiphase.transportTimeStep > 0.0)) {
    throwConfigError(path, context + ".transport_time_step", "be > 0",
                     std::to_string(multiphase.transportTimeStep));
  }
  return multiphase;
}

// P6-PHYS-003: physics.json's optional "compressible" object -- presence
// is the enable flag (same convention as every other block above).
// gas_constant/specific_heat_pressure/reference_pressure must be finite
// and > 0 (cfd::compressible::ThermodynamicProperties's own construction-
// time validation, checked here too -- same two-layer precedent). Exactly
// one of "temperature" (isothermal) or "thermal_coupled": true is
// required -- see CompressiblePhysicsConfig's own header comment; the
// caller (parsePhysicsConfig) checks thermal_coupled's own "thermal"
// block prerequisite, since that is a cross-block check this function
// does not have the rest of physics.json available to make.
CompressiblePhysicsConfig parseCompressiblePhysicsConfig(const nlohmann::json& json,
                                                         const std::filesystem::path& path) {
  const std::string context = "physics.json compressible";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"gas_constant", "specific_heat_pressure", "reference_pressure", "temperature",
                     "thermal_coupled"});

  CompressiblePhysicsConfig compressible;
  compressible.gasConstant = getRequiredReal(json, path, "gas_constant", context + ".gas_constant");
  if (!(compressible.gasConstant > 0.0)) {
    throwConfigError(path, context + ".gas_constant", "be > 0",
                     std::to_string(compressible.gasConstant));
  }
  compressible.specificHeatPressure =
      getRequiredReal(json, path, "specific_heat_pressure", context + ".specific_heat_pressure");
  if (!(compressible.specificHeatPressure > compressible.gasConstant)) {
    throwConfigError(path, context + ".specific_heat_pressure", "be > gas_constant (cv = cp - R)",
                     std::to_string(compressible.specificHeatPressure));
  }
  compressible.referencePressure =
      getRequiredReal(json, path, "reference_pressure", context + ".reference_pressure");
  if (!(compressible.referencePressure > 0.0)) {
    throwConfigError(path, context + ".reference_pressure", "be > 0",
                     std::to_string(compressible.referencePressure));
  }

  const bool hasTemperature = json.contains("temperature");
  const bool hasThermalCoupled = json.contains("thermal_coupled");
  if (hasTemperature == hasThermalCoupled) {
    throwConfigError(path, context, "have exactly one of temperature/thermal_coupled",
                     hasTemperature ? "both present" : "neither present");
  }
  if (hasTemperature) {
    const Real value = getRequiredReal(json, path, "temperature", context + ".temperature");
    if (!(value > 0.0)) {
      throwConfigError(path, context + ".temperature", "be > 0", std::to_string(value));
    }
    compressible.temperature = value;
  } else {
    if (json.at("thermal_coupled").is_boolean() && !json.at("thermal_coupled").get<bool>()) {
      throwConfigError(path, context, "have exactly one of temperature/thermal_coupled",
                       "thermal_coupled was false with no temperature given");
    }
    if (!json.at("thermal_coupled").is_boolean()) {
      throwConfigError(path, context + ".thermal_coupled", "be a boolean",
                       describeJsonValue(json.at("thermal_coupled")));
    }
    compressible.thermalCoupled = true;
  }
  return compressible;
}

// P10-APP-004: the one authoritative production-physics compatibility
// system. Every cross-block rule governing which physics.json modules may
// be combined lives here, and only here -- GUI validation and
// ProjectRunner never re-implement any of it; both reach physics.json
// exclusively through this same parser (GUI's "Validate"/"Save" round-
// trips through cfd::io::CaseBuilder, which calls parsePhysicsConfig
// below; ProjectRunner only ever consumes an already-validated
// SimulationSetup built from that same CaseBuilder pass). This function is
// called exactly once, from parsePhysicsConfig, after every individual
// block has been parsed -- so every field this function reads
// (config.thermal, config.turbulence, ...) already reflects the full,
// independently-validated physics.json.
//
// Compatibility matrix (Thermal/Turbulence/Buoyancy/Species/Multiphase/
// Compressible -- laminar-incompressible is always the implicit baseline,
// never itself a "module" to combine):
//
//   Buoyancy       requires Thermal (needs a real temperature field to
//                  evaluate its momentum source against; this codebase has
//                  no other source of one)
//   Compressible   requires Thermal only when thermal_coupled=true
//                  (isothermal compressible, the other mode, needs no
//                  thermal block at all)
//   Multiphase     excludes Turbulence (both want SIMPLE's one
//                  effective-viscosity injection point; accepting both
//                  would silently discard one)
//   Multiphase     excludes Compressible (multiphase's linear-mixture
//                  density/viscosity model and compressible's ideal-gas
//                  EOS reinterpretation describe two different, mutually
//                  incoherent fluids -- there is no single physical
//                  reading of "a two-phase liquid/gas mixture that is also
//                  an ideal gas")
//   Species        no exclusions -- a passive scalar riding the existing
//                  mass flux, compatible with every other module
//   Turbulence     no exclusions besides the Multiphase one above
//   Thermal        no exclusions
//
// Every combination not listed as excluded above is supported (see
// docs/user_guide/case_format.md's own copy of this table, which must be
// kept in sync with this comment) and has at least one production-path
// regression test exercising it -- see
// tests/unit/io/test_physics_compatibility.cpp.
void validatePhysicsCompatibility(const PhysicsConfig& config, const std::filesystem::path& path) {
  if (config.buoyancy.has_value() && !config.thermal.has_value()) {
    throwConfigError(path, "buoyancy", "be present only alongside a \"thermal\" block",
                     "no \"thermal\" block was given");
  }
  if (config.compressible.has_value() && config.compressible->thermalCoupled &&
      !config.thermal.has_value()) {
    throwConfigError(path, "compressible.thermal_coupled",
                     "be true only alongside a \"thermal\" block",
                     "no \"thermal\" block was given");
  }
  if (config.multiphase.has_value() && config.turbulence.has_value()) {
    throwConfigError(path, "multiphase",
                     "be present only without a \"turbulence\" block (both would need "
                     "SIMPLE's one effective-viscosity injection point)",
                     "a \"turbulence\" block was also given");
  }
  if (config.multiphase.has_value() && config.compressible.has_value()) {
    throwConfigError(path, "multiphase",
                     "be present only without a \"compressible\" block (a two-phase "
                     "mixture and an ideal-gas EOS reinterpretation describe incompatible "
                     "fluids)",
                     "a \"compressible\" block was also given");
  }
  if (config.multiphase.has_value()) {
    // P6-PHYS-002: ProjectRunner feeds mu_mix(alpha) into SIMPLE's
    // effective-viscosity injection point as mu_t = mu_mix - molecular,
    // where "molecular" is this same top-level "dynamic_viscosity" (the
    // constant FluidProperties passed to SIMPLE unchanged) -- see
    // ProjectRunner.cpp's own header comment. mu_t must never be negative
    // (cfd::turbulence::validateTurbulentViscosityField's own hard
    // requirement), and mu_mix is a convex combination of the two phase
    // viscosities (bounded below by their min) for any alpha in [0,1], so
    // requiring dynamic_viscosity <= min(phase1, phase2) here guarantees
    // that invariant at parse time -- a clear, immediate configuration
    // error instead of a hard-to-diagnose runtime validation throw deep
    // inside SIMPLE's first outer iteration.
    const Real minPhaseViscosity =
        std::min(config.multiphase->phase1.viscosity, config.multiphase->phase2.viscosity);
    if (config.dynamicViscosity > minPhaseViscosity) {
      throwConfigError(
          path, "dynamic_viscosity",
          "be <= the smaller of multiphase.phase1.viscosity/phase2.viscosity (" +
              std::to_string(minPhaseViscosity) +
              ") -- it is used as the molecular-viscosity baseline the mixture-viscosity "
              "coupling adds mu_mix-baseline on top of, and that difference must never be "
              "negative",
          std::to_string(config.dynamicViscosity));
    }
  }
}

}  // namespace

PhysicsConfig parsePhysicsConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "physics.json",
                    {"model", "density", "dynamic_viscosity", "reynolds_number", "thermal",
                     "turbulence", "buoyancy", "species", "multiphase", "compressible"});

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
  // identically. The "requires thermal" cross-block rule is enforced once,
  // by validatePhysicsCompatibility below, alongside every other
  // cross-block rule (P10-APP-004) -- not here.
  if (json.contains("buoyancy")) {
    config.buoyancy = parseBuoyancyPhysicsConfig(json.at("buoyancy"), path);
  }
  // P6-PHYS-001: physics.json's optional "species" array (a JSON array,
  // not an object -- see PhysicsConfig.hpp's own header comment on why
  // an empty vector, not std::optional<vector>, represents "disabled").
  // An absent key parses to the same empty vector requireObject/default-
  // construction already gives every pre-P6 case, so every existing
  // nonspecies case continues to parse identically.
  if (json.contains("species")) {
    const auto& speciesJson = json.at("species");
    if (!speciesJson.is_array()) {
      throwConfigError(path, "species", "be a JSON array", describeJsonValue(speciesJson));
    }
    std::unordered_set<std::string> seenNames;
    for (std::size_t i = 0; i < speciesJson.size(); ++i) {
      SpeciesConfig species = parseSpeciesConfig(speciesJson.at(i), path, i);
      if (!seenNames.insert(species.name).second) {
        throwConfigError(path, "species", "have unique names",
                         "duplicate name \"" + species.name + "\"");
      }
      config.species.push_back(std::move(species));
    }
  }
  // P6-PHYS-002: presence of "multiphase" is the enable flag, same
  // convention as every other block above. Cross-block rules (exclusion
  // with turbulence/compressible, the mixture-viscosity invariant) are
  // enforced once, by validatePhysicsCompatibility below (P10-APP-004) --
  // not here.
  if (json.contains("multiphase")) {
    config.multiphase = parseMultiphasePhysicsConfig(json.at("multiphase"), path);
  }
  // P6-PHYS-003: presence of "compressible" is the enable flag, same
  // convention as every other block above. The thermal_coupled/"thermal"
  // cross-block rule is enforced once, by validatePhysicsCompatibility
  // below (P10-APP-004) -- not here.
  if (json.contains("compressible")) {
    config.compressible = parseCompressiblePhysicsConfig(json.at("compressible"), path);
  }

  // P10-APP-004: every cross-block compatibility rule, enforced once, in
  // one place, after every individual block above has already been
  // parsed. See validatePhysicsCompatibility's own header comment for the
  // full matrix.
  validatePhysicsCompatibility(config, path);
  return config;
}

}  // namespace cfd::io::detail
