#include <algorithm>
#include <cmath>
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
Real parseThermalProperty(const nlohmann::json& json, const std::filesystem::path& path,
                          const std::string& context, const char* key, Real initialTemperature,
                          std::optional<TemperaturePolynomialConfig>& model) {
  const std::string field = context + "." + key;
  if (!json.contains(key)) {
    throwConfigError(path, field, "be present", "absent");
  }
  const nlohmann::json& node = json.at(key);

  // Form 1: a number. The pre-existing constant, unchanged.
  if (node.is_number()) {
    const Real value = getRequiredReal(json, path, key, field);
    if (!(value > 0.0)) {
      throwConfigError(path, field, "be > 0", std::to_string(value));
    }
    return value;
  }

  // Form 2: an object selecting a model.
  if (!node.is_object()) {
    throwConfigError(path, field, "be a number (a constant) or an object (a property model)",
                     node.type_name());
  }
  requireObject(node, path, field);
  rejectUnknownKeys(node, path, field,
                    {"model", "coefficients", "min_temperature", "max_temperature"});

  if (!node.contains("model") || !node.at("model").is_string()) {
    throwConfigError(path, field + ".model", "be a string naming a property model", "missing");
  }
  const std::string modelName = node.at("model").get<std::string>();
  if (modelName != "polynomial") {
    // Named explicitly rather than silently falling back to a constant: a case
    // asking for a model this build does not have must fail, not quietly solve
    // a different problem.
    throwConfigError(path, field + ".model", "be \"polynomial\" (the only model implemented)",
                     modelName);
  }

  if (!node.contains("coefficients") || !node.at("coefficients").is_array()) {
    throwConfigError(path, field + ".coefficients", "be an array of numbers", "missing");
  }
  const nlohmann::json& coefficients = node.at("coefficients");
  if (coefficients.empty()) {
    throwConfigError(path, field + ".coefficients", "contain at least one coefficient", "[]");
  }
  TemperaturePolynomialConfig parsed;
  parsed.coefficients.reserve(coefficients.size());
  for (std::size_t i = 0; i < coefficients.size(); ++i) {
    const nlohmann::json& entry = coefficients.at(i);
    if (!entry.is_number()) {
      throwConfigError(path, field + ".coefficients[" + std::to_string(i) + "]", "be a number",
                       entry.type_name());
    }
    const Real value = entry.get<Real>();
    if (!std::isfinite(value)) {
      throwConfigError(path, field + ".coefficients[" + std::to_string(i) + "]", "be finite",
                       std::to_string(value));
    }
    parsed.coefficients.push_back(value);
  }

  parsed.minimumTemperature =
      getRequiredReal(node, path, "min_temperature", field + ".min_temperature");
  parsed.maximumTemperature =
      getRequiredReal(node, path, "max_temperature", field + ".max_temperature");
  if (!(parsed.maximumTemperature > parsed.minimumTemperature)) {
    throwConfigError(path, field + ".max_temperature", "exceed min_temperature",
                     std::to_string(parsed.maximumTemperature));
  }
  if (initialTemperature < parsed.minimumTemperature ||
      initialTemperature > parsed.maximumTemperature) {
    // Caught here rather than at the first solve: a model whose range excludes
    // the initial state cannot even be evaluated to start.
    throwConfigError(path, field + ".min_temperature/max_temperature",
                     "bracket initial_temperature (" + std::to_string(initialTemperature) + ")",
                     "[" + std::to_string(parsed.minimumTemperature) + ", " +
                         std::to_string(parsed.maximumTemperature) + "]");
  }

  // Evaluate the polynomial HERE by Horner rather than constructing a
  // cfd::thermal::TemperatureDependentProperty.
  //
  // Two reasons, and the second is the important one:
  //   * layering -- io/case must not depend on cfd::thermal. Every other
  //     parser in this file validates structure only and leaves the physical
  //     object to CaseBuilder;
  //   * that IS the second layer. TemperatureDependentProperty re-validates
  //     positivity over the whole range at construction and throws with its
  //     own diagnostic, exactly as this file's header comment describes for
  //     the pre-existing fields. Doing it twice here would not add a layer,
  //     it would move one.
  //
  // Same operand order as TemperatureDependentProperty's own Horner, so the
  // value a case reports and the value the model computes agree bit for bit.
  Real value = parsed.coefficients.back();
  for (std::size_t i = parsed.coefficients.size() - 1; i-- > 0;) {
    value = value * initialTemperature + parsed.coefficients[i];
  }
  if (!std::isfinite(value) || !(value > 0.0)) {
    throwConfigError(path, field + ".coefficients",
                     "give a finite, positive value at initial_temperature", std::to_string(value));
  }

  model = parsed;
  return value;
}

ThermalPhysicsConfig parseThermalPhysicsConfig(const nlohmann::json& json,
                                               const std::filesystem::path& path) {
  const std::string context = "physics.json thermal";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"conductivity", "specific_heat", "initial_temperature"});

  ThermalPhysicsConfig thermal;
  // P15: each of these accepts EITHER a number (constant, the pre-existing
  // form, byte-for-byte the same behaviour) OR an object selecting a
  // temperature-dependent model. parseThermalProperty below decides which and
  // rejects everything else with a field-naming diagnostic.
  // Parsed FIRST now: a property model is validated against it and evaluated
  // at it, so it has to be known before the properties are read.
  thermal.initialTemperature =
      getRequiredReal(json, path, "initial_temperature", context + ".initial_temperature");
  thermal.conductivity = parseThermalProperty(
      json, path, context, "conductivity", thermal.initialTemperature, thermal.conductivityModel);
  thermal.specificHeat = parseThermalProperty(
      json, path, context, "specific_heat", thermal.initialTemperature, thermal.specificHeatModel);
  // initial_temperature is parsed above, before the properties, because a
  // property model is range-checked and evaluated at it. Any finite value is
  // physically acceptable (a chosen reference scale, not a rho/cp/k-style
  // quantity with a strict-positivity requirement) -- getRequiredReal already
  // enforces finiteness.
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
  rejectUnknownKeys(
      json, path, context,
      {"model", "wall_functions", "wall_treatment", "initial_k", "initial_epsilon", "initial_omega",
       "k_relaxation", "epsilon_relaxation", "omega_relaxation", "turbulent_prandtl"});

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
  // P16: wall functions, k_epsilon only.
  if (json.contains("wall_functions")) {
    if (!json.at("wall_functions").is_boolean()) {
      throwConfigError(path, context + ".wall_functions", "be a boolean",
                       describeJsonValue(json.at("wall_functions")));
    }
    const bool requested = json.at("wall_functions").get<bool>();
    if (requested && turbulence.model != "k_epsilon" && turbulence.model != "sst") {
      // Rejected rather than ignored: a case asking for a near-wall treatment
      // this model does not have would otherwise run with a different wall
      // treatment than the one it asked for, and report success.
      throwConfigError(path, context + ".wall_functions",
                       "be true only for model \"k_epsilon\" or \"sst\" (k_omega keeps its own "
                       "near-wall treatment; its wall functions are not implemented)",
                       turbulence.model);
    }
    turbulence.wallFunctions = requested;
  }

  // P16b: which near-wall treatment. Absent means the validated high-Re
  // path, so no existing case changes behaviour.
  if (json.contains("wall_treatment")) {
    const std::string requested =
        getRequiredString(json, path, "wall_treatment", context + ".wall_treatment");
    if (requested != "high_re" && requested != "automatic" && requested != "automatic_spalding" &&
        requested != "automatic_momentum_flux") {
      throwConfigError(path, context + ".wall_treatment",
                       "be one of: high_re, automatic, automatic_spalding, automatic_momentum_flux",
                       requested);
    }
    if (requested != "high_re" && turbulence.model != "sst") {
      // Rejected, not ignored. The blend sublayer omega branch is
      // 6 nu/(beta1 y^2), built on SST own beta1; no equivalent is
      // implemented for k_epsilon or k_omega, so a case asking for it
      // there would silently run a different treatment and report success.
      throwConfigError(path, context + ".wall_treatment",
                       "be \"automatic\" only for model \"sst\" (the blended treatment is "
                       "not implemented for the other models)",
                       turbulence.model);
    }
    if (!turbulence.wallFunctions.value_or(false)) {
      // A treatment selector with no treatment to select is dead
      // configuration -- the same reason wall_functions itself is rejected
      // rather than ignored on a model that has none.
      throwConfigError(path, context + ".wall_treatment", "be accompanied by wall_functions = true",
                       turbulence.wallFunctions.has_value() ? "wall_functions = false"
                                                            : "wall_functions absent");
    }
    turbulence.wallTreatment = requested;
  }

  // P16: the turbulent Prandtl number, Pr_t, for k_t = mu_t c_p / Pr_t.
  //
  // Rejected -- not ignored, and not silently replaced by the default -- when
  // it is non-positive or when the model is laminar. A laminar case has no
  // mu_t, so a Pr_t there scales nothing and can only mislead the reader of
  // the case file into thinking turbulent heat transfer is active.
  //
  // Absent leaves the optional empty; the DEFAULT is applied in CaseBuilder
  // from cfd::turbulence::kDefaultTurbulentPrandtl, so there is exactly one
  // place that decides it.
  if (json.contains("turbulent_prandtl")) {
    if (turbulence.model == "laminar") {
      throwConfigError(path, context + ".turbulent_prandtl",
                       "be given only for a turbulence model that provides mu_t (laminar flow "
                       "has no turbulent heat flux to close)",
                       turbulence.model);
    }
    const Real value =
        getRequiredReal(json, path, "turbulent_prandtl", context + ".turbulent_prandtl");
    if (!(value > 0.0)) {
      throwConfigError(path, context + ".turbulent_prandtl", "be > 0", std::to_string(value));
    }
    turbulence.turbulentPrandtl = value;
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

// G8: one entry of physics.json's "reacting".species array.
//
// Two independent, consistent validation layers, the same precedent as every other physics
// block in this file: the parse-time check names the exact array index, and the production
// constructors (SpeciesRegistry, SpeciesThermodynamics, FormationEnthalpy) check again.
ReactingSpeciesConfig parseReactingSpeciesConfig(const nlohmann::json& json,
                                                 const std::filesystem::path& path,
                                                 std::size_t index) {
  const std::string context = "physics.json reacting.species[" + std::to_string(index) + "]";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"name", "molecular_weight", "specific_heat", "formation_enthalpy",
                     "initial_mass_fraction", "diffusivity"});

  ReactingSpeciesConfig species;
  species.name = getRequiredString(json, path, "name", context + ".name");
  if (species.name.empty()) {
    throwConfigError(path, context + ".name", "be non-empty", "\"\"");
  }
  species.molecularWeight =
      getRequiredReal(json, path, "molecular_weight", context + ".molecular_weight");
  if (!(species.molecularWeight > 0.0)) {
    throwConfigError(path, context + ".molecular_weight", "be > 0",
                     std::to_string(species.molecularWeight));
  }
  species.specificHeat = getRequiredReal(json, path, "specific_heat", context + ".specific_heat");
  if (!(species.specificHeat > 0.0)) {
    throwConfigError(path, context + ".specific_heat", "be > 0",
                     std::to_string(species.specificHeat));
  }
  // h_f may be any finite sign: 0 for an element in its reference state, negative for most
  // stable products, positive for an endothermic former. getRequiredReal enforces finiteness.
  species.formationEnthalpy =
      getRequiredReal(json, path, "formation_enthalpy", context + ".formation_enthalpy");
  species.initialMassFraction =
      getRequiredReal(json, path, "initial_mass_fraction", context + ".initial_mass_fraction");
  if (!(species.initialMassFraction >= 0.0) || !(species.initialMassFraction <= 1.0)) {
    throwConfigError(path, context + ".initial_mass_fraction", "be within [0, 1]",
                     std::to_string(species.initialMassFraction));
  }
  species.diffusivity = getRequiredReal(json, path, "diffusivity", context + ".diffusivity");
  if (!(species.diffusivity >= 0.0)) {
    throwConfigError(path, context + ".diffusivity", "be >= 0",
                     std::to_string(species.diffusivity));
  }
  return species;
}

// G8: one entry of a reaction's "reactants"/"products" array.
ReactionTermConfig parseReactionTermConfig(const nlohmann::json& json,
                                           const std::filesystem::path& path,
                                           const std::string& context) {
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"species", "coefficient"});
  ReactionTermConfig term;
  term.species = getRequiredString(json, path, "species", context + ".species");
  if (term.species.empty()) {
    throwConfigError(path, context + ".species", "be non-empty", "\"\"");
  }
  term.coefficient = getRequiredReal(json, path, "coefficient", context + ".coefficient");
  if (!(term.coefficient >= 0.0)) {
    throwConfigError(path, context + ".coefficient", "be >= 0", std::to_string(term.coefficient));
  }
  return term;
}

// G8: one entry of a reaction's optional "orders" array.
//
// A negative order is refused HERE as well as in Reaction's constructor, because the
// parse-time message can name the file and the array index. The reason is physical, not
// stylistic: a negative order makes the rate diverge as that species is consumed, and the
// usual remedy is to floor the concentration -- which this codebase forbids.
ReactionOrderConfig parseReactionOrderConfig(const nlohmann::json& json,
                                             const std::filesystem::path& path,
                                             const std::string& context) {
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context, {"species", "order"});
  ReactionOrderConfig order;
  order.species = getRequiredString(json, path, "species", context + ".species");
  if (order.species.empty()) {
    throwConfigError(path, context + ".species", "be non-empty", "\"\"");
  }
  order.order = getRequiredReal(json, path, "order", context + ".order");
  if (!(order.order >= 0.0)) {
    throwConfigError(path, context + ".order",
                     "be >= 0 (a negative order makes the rate diverge as the species is consumed, "
                     "and this codebase does not clip concentrations)",
                     std::to_string(order.order));
  }
  return order;
}

// G8: one entry of physics.json's "reacting".reactions array.
ReactingReactionConfig parseReactingReactionConfig(const nlohmann::json& json,
                                                   const std::filesystem::path& path,
                                                   std::size_t index) {
  const std::string context = "physics.json reacting.reactions[" + std::to_string(index) + "]";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"name", "reactants", "products", "orders", "reversible", "pre_exponential",
                     "temperature_exponent", "activation_energy"});

  ReactingReactionConfig reaction;
  reaction.name = getRequiredString(json, path, "name", context + ".name");
  if (reaction.name.empty()) {
    throwConfigError(path, context + ".name", "be non-empty", "\"\"");
  }

  for (const char* side : {"reactants", "products"}) {
    const std::string sideContext = context + "." + side;
    if (!json.contains(side)) {
      throwConfigError(path, sideContext, "be present", "absent");
    }
    if (!json.at(side).is_array() || json.at(side).empty()) {
      throwConfigError(path, sideContext, "be a non-empty array", json.at(side).type_name());
    }
    std::size_t i = 0;
    for (const auto& entry : json.at(side)) {
      ReactionTermConfig term =
          parseReactionTermConfig(entry, path, sideContext + "[" + std::to_string(i) + "]");
      if (std::string(side) == "reactants") {
        reaction.reactants.push_back(std::move(term));
      } else {
        reaction.products.push_back(std::move(term));
      }
      ++i;
    }
  }

  if (json.contains("orders")) {
    const std::string ordersContext = context + ".orders";
    if (!json.at("orders").is_array() || json.at("orders").empty()) {
      throwConfigError(path, ordersContext, "be a non-empty array when present",
                       json.at("orders").type_name());
    }
    std::size_t i = 0;
    for (const auto& entry : json.at("orders")) {
      reaction.orders.push_back(
          parseReactionOrderConfig(entry, path, ordersContext + "[" + std::to_string(i) + "]"));
      ++i;
    }
  }

  if (json.contains("reversible")) {
    if (!json.at("reversible").is_boolean()) {
      throwConfigError(path, context + ".reversible", "be a boolean",
                       describeJsonValue(json.at("reversible")));
    }
    reaction.reversible = json.at("reversible").get<bool>();
  }
  // Reaction's constructor refuses explicit orders on a reversible reaction because the reverse
  // order is not modelled. Caught here too so the message can name the file.
  if (reaction.reversible && !reaction.orders.empty()) {
    throwConfigError(path, context,
                     "not combine \"reversible\": true with explicit \"orders\" (the reverse "
                     "reaction order is not modelled, so the reverse side would silently use "
                     "product stoichiometry)",
                     "both present");
  }

  reaction.preExponential =
      getRequiredReal(json, path, "pre_exponential", context + ".pre_exponential");
  if (!(reaction.preExponential >= 0.0)) {
    throwConfigError(path, context + ".pre_exponential", "be >= 0",
                     std::to_string(reaction.preExponential));
  }
  reaction.temperatureExponent =
      getRequiredReal(json, path, "temperature_exponent", context + ".temperature_exponent");
  reaction.activationEnergy =
      getRequiredReal(json, path, "activation_energy", context + ".activation_energy");
  if (!(reaction.activationEnergy >= 0.0)) {
    throwConfigError(path, context + ".activation_energy",
                     "be >= 0 J/mol (MOLE based, never kmol -- a kmol value is wrong by exactly "
                     "1000)",
                     std::to_string(reaction.activationEnergy));
  }
  return reaction;
}

// G8: physics.json's optional "reacting" object.
ReactingPhysicsConfig parseReactingPhysicsConfig(const nlohmann::json& json,
                                                 const std::filesystem::path& path) {
  const std::string context = "physics.json reacting";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"species", "reactions", "initial_temperature", "time_step", "steps"});

  ReactingPhysicsConfig reacting;

  if (!json.contains("species") || !json.at("species").is_array() ||
      json.at("species").size() < 2) {
    throwConfigError(path, context + ".species",
                     "be an array of at least 2 species (a reacting mixture of one species has "
                     "nothing to react)",
                     json.contains("species") ? json.at("species").type_name() : "absent");
  }
  for (std::size_t i = 0; i < json.at("species").size(); ++i) {
    reacting.species.push_back(parseReactingSpeciesConfig(json.at("species").at(i), path, i));
  }

  if (!json.contains("reactions") || !json.at("reactions").is_array() ||
      json.at("reactions").empty()) {
    throwConfigError(path, context + ".reactions",
                     "be a non-empty array (a reacting case with no reactions is a passive "
                     "species case -- use the \"species\" block instead)",
                     json.contains("reactions") ? json.at("reactions").type_name() : "absent");
  }
  for (std::size_t i = 0; i < json.at("reactions").size(); ++i) {
    reacting.reactions.push_back(parseReactingReactionConfig(json.at("reactions").at(i), path, i));
  }

  // TD-001 amendment A1: the reacting advance is an operator-split TIME STEP, so the case must say
  // how far. See ReactingPhysicsConfig::timeStep for why a steady pass was abandoned -- measured,
  // in both source treatments. Required rather than defaulted: a silently chosen dt would make the
  // reported composition depend on a number the case never stated.
  reacting.timeStep = getRequiredReal(json, path, "time_step", context + ".time_step");
  if (!std::isfinite(reacting.timeStep) || !(reacting.timeStep > 0.0)) {
    throwConfigError(path, context + ".time_step", "be finite and > 0 s",
                     std::to_string(reacting.timeStep));
  }

  // TD-002: the number of operator-split steps. Required for the reason stated on
  // ReactingPhysicsConfig::steps -- the integration extent is a physics input, not a default.
  const Real stepsReal = getRequiredReal(json, path, "steps", context + ".steps");
  if (!std::isfinite(stepsReal) || !(stepsReal >= 1.0) || stepsReal != std::floor(stepsReal) ||
      stepsReal > 1.0e6) {
    throwConfigError(path, context + ".steps",
                     "be a whole number >= 1 and <= 1000000 (a fractional step count is not a "
                     "smaller step -- reacting.time_step is what sets the step size)",
                     std::to_string(stepsReal));
  }
  reacting.steps = static_cast<Index>(stepsReal);

  reacting.initialTemperature =
      getRequiredReal(json, path, "initial_temperature", context + ".initial_temperature");
  if (!(reacting.initialTemperature > 0.0)) {
    throwConfigError(path, context + ".initial_temperature", "be > 0 K",
                     std::to_string(reacting.initialTemperature));
  }

  // Duplicate species names: the registry refuses them, but the parse-time message can say which
  // index collided.
  for (std::size_t i = 0; i < reacting.species.size(); ++i) {
    for (std::size_t j = i + 1; j < reacting.species.size(); ++j) {
      if (reacting.species[i].name == reacting.species[j].name) {
        throwConfigError(
            path, context + ".species[" + std::to_string(j) + "].name",
            "be unique among the reacting species",
            "\"" + reacting.species[j].name + "\" duplicates index " + std::to_string(i));
      }
    }
  }

  // sum(Y) = 1 on the declared initial state. P18.1 REFUSES a state that does not sum to one and
  // never normalises, so a case file that does not sum to one would construct and then be
  // rejected deeper in; rejected here instead, where the message can name the file.
  Real sum = 0.0;
  for (const ReactingSpeciesConfig& species : reacting.species) {
    sum += species.initialMassFraction;
  }
  if (std::abs(sum - 1.0) > 1.0e-12) {
    throwConfigError(path, context + ".species",
                     "have initial_mass_fraction summing to 1 (this codebase refuses an "
                     "out-of-bounds composition and never normalises one)",
                     std::to_string(sum));
  }

  return reacting;
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
//
// P12-COMP-002: "coupled" is optional, defaulting to `false` (today's
// exact post-hoc behavior) -- absent or explicitly `false` means every
// existing case, including cases with no "coupled" key at all, parses
// identically to before this key existed.
CompressiblePhysicsConfig parseCompressiblePhysicsConfig(const nlohmann::json& json,
                                                         const std::filesystem::path& path) {
  const std::string context = "physics.json compressible";
  requireObject(json, path, context);
  rejectUnknownKeys(json, path, context,
                    {"gas_constant", "specific_heat_pressure", "reference_pressure", "temperature",
                     "thermal_coupled", "coupled"});

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

  if (json.contains("coupled")) {
    if (!json.at("coupled").is_boolean()) {
      throwConfigError(path, context + ".coupled", "be a boolean",
                       describeJsonValue(json.at("coupled")));
    }
    compressible.coupled = json.at("coupled").get<bool>();
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
//   Turbulence     no exclusions besides the Multiphase one above, UNLESS
//                  Compressible.coupled is also true (see below)
//   Thermal        no exclusions
//   Compressible.coupled=true (P12-COMP-002)
//                  excludes Turbulence and Buoyancy -- disclosed, deliberate
//                  scope limitation of this first coupled-solver
//                  implementation: cfd::compressible::CompressibleSIMPLE
//                  has no turbulence-model or buoyancy-source injection
//                  point yet (unlike plain SIMPLE, which accepts both),
//                  so silently allowing either would mean silently
//                  dropping that physics rather than solving with it --
//                  rejected here instead of producing a numerically
//                  quiet wrong answer. The non-coupled (post-hoc,
//                  default) Compressible mode is unaffected and keeps
//                  allowing both, exactly as before.
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
  // P12-COMP-002: CompressibleSIMPLE (dispatched when compressible.coupled
  // is true) has no turbulence-model or buoyancy-source injection point
  // yet -- a disclosed, deliberate scope limitation of this first coupled-
  // solver implementation (see this function's own header comment).
  // Rejected here rather than silently dropping that physics. The
  // default, non-coupled post-hoc compressible mode is unaffected.
  if (config.compressible.has_value() && config.compressible->coupled &&
      config.turbulence.has_value()) {
    throwConfigError(path, "compressible.coupled",
                     "be true only without a \"turbulence\" block (CompressibleSIMPLE has no "
                     "turbulence-model injection point yet)",
                     "a \"turbulence\" block was also given");
  }
  if (config.compressible.has_value() && config.compressible->coupled &&
      config.buoyancy.has_value()) {
    throwConfigError(path, "compressible.coupled",
                     "be true only without a \"buoyancy\" block (CompressibleSIMPLE has no "
                     "buoyancy-source injection point yet)",
                     "a \"buoyancy\" block was also given");
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
  rejectUnknownKeys(
      json, path, "physics.json",
      {"model", "density", "dynamic_viscosity", "reynolds_number", "thermal", "turbulence",
       "buoyancy", "species", "reacting", "multiphase", "compressible"});

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
  // G8: "reacting" and "species" are two different species representations. Allowing both would
  // give one solve two sources of truth for the same field.
  if (json.contains("reacting") && json.contains("species")) {
    throwConfigError(path, "physics.json",
                     "declare either \"species\" (passive, P6-PHYS-001) or \"reacting\" (the "
                     "reacting mixture), never both",
                     "both present");
  }
  if (json.contains("reacting")) {
    config.reacting = parseReactingPhysicsConfig(json.at("reacting"), path);

    // TD-001: a reacting case REQUIRES "thermal", and that is a design decision rather than an
    // oversight.
    //
    // Heat release is coupled to the P15 thermal path only -- P18.6 recorded exactly that. The
    // energy equation it feeds needs a CONDUCTIVITY, which the reacting block deliberately does
    // not carry: inventing a "reacting.conductivity" key would create a second, competing source of
    // truth for a property "thermal" already owns, and the per-patch "temperature" boundary
    // vocabulary (fixed_temperature / heat_flux / adiabatic) is thermal's too.
    //
    // So the reacting block supplies the CHEMISTRY and the thermal block supplies the TRANSPORT
    // PROPERTIES, each where it already belongs. Refused here rather than later because a reacting
    // case without conductivity cannot couple its heat release at all, and the user should learn
    // that from the file, not from an absent temperature field in the output.
    if (!json.contains("thermal")) {
      throwConfigError(path, "physics.json",
                       "declare a \"thermal\" block alongside \"reacting\" (heat release is "
                       "coupled through the thermal energy equation, which needs the conductivity "
                       "and specific heat that \"thermal\" owns)",
                       "\"reacting\" present, \"thermal\" absent");
    }
  }

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
