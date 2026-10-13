"""P15 -- case-file reachability for temperature-dependent properties.

The property models do not count as production capability until a user can
configure them from a real case, so physics.json gains an OBJECT form for
conductivity and specific_heat alongside the existing scalar:

    "conductivity": 0.6                           <- unchanged, still valid

    "conductivity": {                             <- new
        "model": "polynomial",
        "coefficients": [2.0, 0.01],
        "min_temperature": 200.0,
        "max_temperature": 1200.0
    }

BACKWARD COMPATIBILITY IS THE POINT. Every existing case file supplies a
number, and every existing consumer reads ThermalPhysicsConfig::conductivity as
a scalar. So when a model is given, the scalar is populated by evaluating the
model AT initial_temperature -- consumers that do not know about models still
get a sensible, in-range value rather than a zero, and consumers that do know
read the model.

Diagnostics use the structured system from P13-002 (throwConfigError ->
CaseConfigurationError carrying file/field/constraint/received), so every
rejection names the exact JSON field.
"""

import io

# --- 1. the config struct ---------------------------------------------------

p = 'include/cfd/io/case/PhysicsConfig.hpp'
s = io.open(p, encoding='utf-8', newline='').read()

old = """struct ThermalPhysicsConfig {
  Real conductivity{};
  Real specificHeat{};
  Real initialTemperature{};
};"""

new = """// P15: a temperature-dependent property as configured in physics.json.
// Mirrors cfd::thermal::TemperatureDependentProperty, which validates it
// again at construction -- two independent layers, the same deliberate
// redundancy this file's other parsers already use.
struct TemperaturePolynomialConfig {
  std::vector<Real> coefficients;  // ascending: a0, a1, a2, ...
  Real minimumTemperature{};
  Real maximumTemperature{};
};

struct ThermalPhysicsConfig {
  // Always populated. When the corresponding model below is present, this
  // holds the model evaluated at initialTemperature, so a consumer that does
  // not know about models still receives a valid in-range value rather than a
  // zero. Consumers that do know read the model instead.
  Real conductivity{};
  Real specificHeat{};
  Real initialTemperature{};

  // Present only when physics.json gave the object form.
  std::optional<TemperaturePolynomialConfig> conductivityModel;
  std::optional<TemperaturePolynomialConfig> specificHeatModel;
};"""

assert s.count(old) == 1, ('PhysicsConfig struct', s.count(old))
s = s.replace(old, new, 1)

for header in ('#include <optional>', '#include <vector>'):
    if header not in s:
        anchor = '#include "cfd/core/Types.hpp"'
        assert s.count(anchor) >= 1
        s = s.replace(anchor, header + '\n' + anchor, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('PhysicsConfig.hpp: model fields added')

# --- 2. the parser ----------------------------------------------------------

p = 'src/io/case/PhysicsConfigParser.cpp'
s = io.open(p, encoding='utf-8', newline='').read()

old = """  ThermalPhysicsConfig thermal;
  thermal.conductivity = getRequiredReal(json, path, "conductivity", context + ".conductivity");
  if (!(thermal.conductivity > 0.0)) {
    throwConfigError(path, context + ".conductivity", "be > 0",
                     std::to_string(thermal.conductivity));
  }
  thermal.specificHeat = getRequiredReal(json, path, "specific_heat", context + ".specific_heat");
  if (!(thermal.specificHeat > 0.0)) {
    throwConfigError(path, context + ".specific_heat", "be > 0",
                     std::to_string(thermal.specificHeat));
  }"""

new = """  ThermalPhysicsConfig thermal;
  // P15: each of these accepts EITHER a number (constant, the pre-existing
  // form, byte-for-byte the same behaviour) OR an object selecting a
  // temperature-dependent model. parseThermalProperty below decides which and
  // rejects everything else with a field-naming diagnostic.
  thermal.conductivity = parseThermalProperty(json, path, context, "conductivity",
                                              thermal.conductivityModel);
  thermal.specificHeat = parseThermalProperty(json, path, context, "specific_heat",
                                              thermal.specificHeatModel);"""

assert s.count(old) == 1, ('parser body', s.count(old))
s = s.replace(old, new, 1)

# The helper, inserted just before parseThermalPhysicsConfig.
anchor = 'ThermalPhysicsConfig parseThermalPhysicsConfig(const nlohmann::json& json,'
assert s.count(anchor) == 1

helper = '''// P15: one temperature-dependent property, in either of its two forms.
//
// A NUMBER is the pre-existing constant and is parsed exactly as before, so no
// existing case file changes meaning. An OBJECT selects a model. Anything else
// -- a string, an array, a bool -- is rejected naming the field, rather than
// being coerced.
//
// Returns the scalar value the rest of the system reads. For a model that is
// the polynomial evaluated at `initialTemperature`, which is why this is
// called after that field is known.
Real parseThermalProperty(const nlohmann::json& json, const std::filesystem::path& path,
                          const std::string& context, const char* key,
                          std::optional<TemperaturePolynomialConfig>& model);

'''
s = s.replace(anchor, helper + anchor, 1)

# Implementation goes after parseThermalPhysicsConfig's closing brace; append
# before the anonymous-namespace close is not reliable, so define it fully
# in place of the forward declaration instead.
s = s.replace(helper, '', 1)

implementation = '''Real parseThermalProperty(const nlohmann::json& json, const std::filesystem::path& path,
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
    throwConfigError(path, field + ".model", "be \\"polynomial\\" (the only model implemented)",
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

  // Construct the real model: it re-validates positivity over the whole range
  // and throws with its own diagnostic if the polynomial is unphysical. The
  // second layer is deliberate, matching this file's existing convention.
  const cfd::thermal::TemperatureDependentProperty property(
      parsed.coefficients, parsed.minimumTemperature, parsed.maximumTemperature, field);

  model = parsed;
  return property.evaluate(initialTemperature);
}

'''
s = s.replace(anchor, implementation + anchor, 1)

# The two call sites need initialTemperature, which is parsed AFTER them in the
# original order. Move the initial_temperature parse above them.
old_calls = """  thermal.conductivity = parseThermalProperty(json, path, context, "conductivity",
                                              thermal.conductivityModel);
  thermal.specificHeat = parseThermalProperty(json, path, context, "specific_heat",
                                              thermal.specificHeatModel);"""
new_calls = """  // Parsed FIRST now: a property model is validated against it and evaluated
  // at it, so it has to be known before the properties are read.
  thermal.initialTemperature =
      getRequiredReal(json, path, "initial_temperature", context + ".initial_temperature");
  thermal.conductivity = parseThermalProperty(json, path, context, "conductivity",
                                              thermal.initialTemperature,
                                              thermal.conductivityModel);
  thermal.specificHeat = parseThermalProperty(json, path, context, "specific_heat",
                                              thermal.initialTemperature,
                                              thermal.specificHeatModel);"""
assert s.count(old_calls) == 1
s = s.replace(old_calls, new_calls, 1)

# Remove the now-duplicated later initial_temperature parse.
old_tail = """  // Any finite value is physically acceptable here (a chosen reference
  // scale, not a rho/cp/k-style quantity with a strict-positivity
  // requirement) -- getRequiredReal already enforces finiteness.
  thermal.initialTemperature =
      getRequiredReal(json, path, "initial_temperature", context + ".initial_temperature");
  return thermal;"""
new_tail = """  // initial_temperature is parsed above, before the properties, because a
  // property model is range-checked and evaluated at it. Any finite value is
  // physically acceptable (a chosen reference scale, not a rho/cp/k-style
  // quantity with a strict-positivity requirement) -- getRequiredReal already
  // enforces finiteness.
  return thermal;"""
assert s.count(old_tail) == 1
s = s.replace(old_tail, new_tail, 1)

for header in ('#include <cmath>', '#include <optional>',
               '#include "cfd/thermal/TemperatureDependentProperty.hpp"'):
    if header not in s:
        anchor2 = '#include "cfd/core/Exception.hpp"'
        if s.count(anchor2) >= 1:
            s = s.replace(anchor2, header + '\n' + anchor2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('PhysicsConfigParser.cpp: object form parsed with structured diagnostics')
