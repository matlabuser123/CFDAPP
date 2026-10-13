"""P15 -- close the parse -> solve gap for temperature-dependent properties.

Idempotent: every step is guarded, so re-running it is a no-op.

WHAT THE CASE-REACHABILITY WORK FOUND
-------------------------------------
physics.json now accepts a polynomial k(T)/cp(T), the parser validates it and
CaseReader hands back a populated TemperaturePolynomialConfig. And then it
stopped: CaseBuilder read only the evaluated scalar and constructed a CONSTANT
ThermalProperties, so a case asking for a varying property was solved with a
fixed one. That is precisely the "silently fall back to constants" failure the
P15 brief prohibits -- worse than not parsing the block at all, because the
case file would claim a capability the run does not deliver.

WHAT THIS DOES NOT DO: INVENT ANYTHING
--------------------------------------
Every piece already existed and was already validated:

  * cfd::physics::TemperatureProperty (P3-PHYS-003) -- the property interface
    the solver consumes, with evaluatePropertyField's positivity checking;
  * ThermalSolver::solve's variable-property overload (P3-PHYS-003) -- same
    Picard loop, models re-evaluated each outer iteration, property failures
    already translated to ThermalStatus::NonFiniteState rather than escaping;
  * cfd::thermal::TemperatureDependentProperty (P15) -- the validated
    polynomial.

The only thing missing was that the last of those did not implement the first,
so nothing could connect them. That is the change: ONE inheritance edge, plus
the CaseBuilder/ProjectRunner routing it unlocks. No new numerical convention,
which is the standing P14/P15 constraint.

For the audit trail: the variable-property solve overload has existed since
P3-PHYS-003 and NO case file could reach it. This closes a pre-existing
reachability gap; it is not one P15 introduced.

OUT-OF-RANGE POLICY: THROW, AND THAT IS ALREADY HANDLED
-------------------------------------------------------
value() delegates to evaluate(), which throws outside the validity range.
Deliberately NOT the clamping TabulatedProperty uses: ThermalSolver's
variable-property overload already wraps evaluatePropertyField in a try/catch
that turns InvalidArgumentError into NumericalError, which runPicardLoop
reports as ThermalStatus::NonFiniteState. So a mid-iteration excursion past the
fitted range is REPORTED as a failed solve naming the property -- not an
uncaught exception, and not a confidently-extrapolated wrong answer. No third
policy is introduced; the existing channel is used.

THE CONSTANT PATH IS UNTOUCHED
------------------------------
A case with plain numbers produces no models, so CaseBuilder leaves both
shared_ptrs empty and ProjectRunner takes the byte-for-byte identical constant
branch it takes today. That is what the regression has to confirm.
"""

import io


def read(path):
    return io.open(path, encoding='utf-8', newline='').read()


def write(path, text):
    io.open(path, 'w', encoding='utf-8', newline='').write(text)


def apply(text, old, new, label):
    """Replace exactly once, asserting the anchor is unique."""
    assert text.count(old) == 1, (label, text.count(old))
    return text.replace(old, new, 1)


# --- 1. TemperatureDependentProperty implements the solver's interface -------

P = 'include/cfd/thermal/TemperatureDependentProperty.hpp'
s = read(P)
if 'cfd/physics/TemperatureProperty.hpp' not in s:
    s = apply(s,
              '#include "cfd/fields/ScalarField.hpp"\n',
              '#include "cfd/fields/ScalarField.hpp"\n'
              '#include "cfd/physics/TemperatureProperty.hpp"\n',
              'hpp include')
if 'final : public cfd::physics::TemperatureProperty' not in s:
    s = apply(s,
              'class TemperatureDependentProperty {\n public:',
              '// Implements cfd::physics::TemperatureProperty (P3-PHYS-003) so the solver can\n'
              '// consume it directly.\n'
              '//\n'
              "// That interface, evaluatePropertyField, and ThermalSolver::solve's\n"
              '// variable-property overload all predate P15 and are already validated; what\n'
              '// did not exist was a model with an integrable closed form (SensibleEnthalpy\n'
              '// needs the coefficients symbolically, which a virtual value(T) cannot give).\n'
              '// Deriving here rather than adding a parallel property hierarchy is the whole\n'
              '// point: one property interface, one solver path, one Picard loop.\n'
              'class TemperatureDependentProperty final '
              ': public cfd::physics::TemperatureProperty {\n'
              ' public:',
              'hpp class')
if 'value(Real temperature) const override' not in s:
    s = apply(s,
              '  [[nodiscard]] Real evaluate(Real temperature) const;\n',
              '  [[nodiscard]] Real evaluate(Real temperature) const;\n'
              '\n'
              '  // cfd::physics::TemperatureProperty. Exactly evaluate() -- same bits, same\n'
              '  // out-of-range rejection, so a caller cannot get one answer through the\n'
              '  // interface and a different one through the concrete type.\n'
              '  //\n'
              '  // Throwing out of range is deliberate and is NOT the clamping policy\n'
              "  // TabulatedProperty documents. ThermalSolver's variable-property overload\n"
              '  // already catches InvalidArgumentError out of evaluatePropertyField and\n'
              '  // reports it as ThermalStatus::NonFiniteState, so an excursion past the\n'
              '  // fitted range surfaces as a failed solve naming the property -- rather\n'
              '  // than as a silently extrapolated value, which for a fitted polynomial can\n'
              '  // be negative and makes the enthalpy inversion ill-posed.\n'
              '  [[nodiscard]] Real value(Real temperature) const override;\n',
              'hpp evaluate')
write(P, s)
print('1. TemperatureDependentProperty : cfd::physics::TemperatureProperty')

# --- 2. its definition -------------------------------------------------------

P = 'src/thermal/TemperatureDependentProperty.cpp'
s = read(P)
if 'TemperatureDependentProperty::value(' not in s:
    s = apply(s,
              'ScalarField TemperatureDependentProperty::evaluateField(',
              'Real TemperatureDependentProperty::value(Real temperature) const {\n'
              '  return evaluate(temperature);\n'
              '}\n'
              '\n'
              'ScalarField TemperatureDependentProperty::evaluateField(',
              'cpp anchor')
write(P, s)
print('2. value() defined')

# --- 3. SimulationSetup carries the models -----------------------------------

P = 'include/cfd/io/SimulationSetup.hpp'
s = read(P)
if '#include <memory>' not in s:
    s = apply(s, '#include <optional>\n', '#include <memory>\n#include <optional>\n',
              'setup std includes')
if 'cfd/physics/TemperatureProperty.hpp' not in s:
    s = apply(s,
              '#include "cfd/physics/FluidProperties.hpp"\n',
              '#include "cfd/physics/FluidProperties.hpp"\n'
              '#include "cfd/physics/TemperatureProperty.hpp"\n',
              'setup physics include')
if 'conductivityModel' not in s:
    s = apply(s,
              '  std::optional<cfd::fields::ScalarField> initialTemperature;\n',
              '  std::optional<cfd::fields::ScalarField> initialTemperature;\n'
              '\n'
              '  // P15: present iff physics.json gave the OBJECT form for that property\n'
              '  // (a temperature-dependent model) rather than a plain number. Independent\n'
              '  // of each other -- a case may make conductivity vary and leave specific\n'
              '  // heat constant.\n'
              '  //\n'
              '  // Empty is the pre-existing constant case and must stay bit-for-bit what it\n'
              '  // was: `thermal` above already holds the value, and a caller that ignores\n'
              '  // these two members entirely behaves exactly as it did before P15.\n'
              '  //\n'
              '  // shared_ptr, not optional-by-value: cfd::physics::TemperatureProperty is a\n'
              '  // polymorphic interface, and a case may later select a linear or tabulated\n'
              '  // model alongside the polynomial one. Const because a built setup is\n'
              '  // configuration, never mutated by a solve.\n'
              '  std::shared_ptr<const cfd::physics::TemperatureProperty> conductivityModel;\n'
              '  std::shared_ptr<const cfd::physics::TemperatureProperty> specificHeatModel;\n',
              'setup members')
write(P, s)
print('3. SimulationSetup carries the models')

# --- 4. CaseBuilder builds them, and the built setup carries them ------------

P = 'src/io/CaseBuilder.cpp'
s = read(P)
if 'cfd/thermal/TemperatureDependentProperty.hpp' not in s:
    s = apply(s,
              '#include "cfd/mesh/MeshQuality.hpp"\n',
              '#include "cfd/mesh/MeshQuality.hpp"\n'
              '#include "cfd/thermal/TemperatureDependentProperty.hpp"\n',
              'builder include')
if 'buildModel' not in s:
    s = apply(s,
              '  if (thermalEnabled) {\n'
              '    thermal.emplace(definition.physics.thermal->conductivity,\n'
              '                    definition.physics.thermal->specificHeat);\n'
              '    temperatureBoundaries.emplace();\n'
              '  }\n',
              '  std::shared_ptr<const cfd::physics::TemperatureProperty> conductivityModel;\n'
              '  std::shared_ptr<const cfd::physics::TemperatureProperty> specificHeatModel;\n'
              '  if (thermalEnabled) {\n'
              '    thermal.emplace(definition.physics.thermal->conductivity,\n'
              '                    definition.physics.thermal->specificHeat);\n'
              '    temperatureBoundaries.emplace();\n'
              '    // P15: a property given as an object is a MODEL, and dropping it here\n'
              '    // would leave the case claiming a capability the solve does not deliver\n'
              '    // -- ProjectRunner selects the variable-property solve on exactly these\n'
              '    // two being present. A property given as a number produces no model, and\n'
              '    // that path is unchanged: `thermal` above already holds its value.\n'
              '    //\n'
              '    // TemperatureDependentProperty re-validates the coefficients and the\n'
              '    // range at construction (positivity over the WHOLE range, which the\n'
              '    // parser cannot check -- it only evaluated at the initial temperature)\n'
              '    // and throws InvalidArgumentError naming the property; converted here to\n'
              '    // the case-facing error type. That is the second of the two validation\n'
              "    // layers this file's header describes.\n"
              '    const auto buildModel =\n'
              '        [](const std::optional<cfd::io::TemperaturePolynomialConfig>& config,\n'
              '           const char* propertyName)\n'
              '        -> std::shared_ptr<const cfd::physics::TemperatureProperty> {\n'
              '      if (!config.has_value()) return nullptr;\n'
              '      try {\n'
              '        return std::make_shared<const cfd::thermal::TemperatureDependentProperty>(\n'
              '            config->coefficients, config->minimumTemperature,\n'
              '            config->maximumTemperature, propertyName);\n'
              '      } catch (const InvalidArgumentError& error) {\n'
              '        throw CaseConfigurationError(std::string("physics.json thermal.") +\n'
              '                                     propertyName + ": " + error.what());\n'
              '      }\n'
              '    };\n'
              '    conductivityModel =\n'
              '        buildModel(definition.physics.thermal->conductivityModel, "conductivity");\n'
              '    specificHeatModel =\n'
              '        buildModel(definition.physics.thermal->specificHeatModel, "specific_heat");\n'
              '  }\n',
              'builder thermal')
if '.conductivityModel = nullptr' not in s:
    # Listed explicitly rather than left to value-initialization: this aggregate
    # spells out every field on purpose (see its own -Wmissing-field-initializers
    # comment), and a silently-skipped member is how a new field gets forgotten.
    s = apply(s,
              '                        .initialTemperature = std::nullopt,\n'
              '                        .buoyancy = std::nullopt,',
              '                        .initialTemperature = std::nullopt,\n'
              '                        .conductivityModel = nullptr,\n'
              '                        .specificHeatModel = nullptr,\n'
              '                        .buoyancy = std::nullopt,',
              'builder aggregate')
if 'setup.conductivityModel' not in s:
    s = apply(s,
              '    setup.thermal = thermal;\n',
              '    setup.thermal = thermal;\n'
              '    setup.conductivityModel = std::move(conductivityModel);\n'
              '    setup.specificHeatModel = std::move(specificHeatModel);\n',
              'builder assign')
write(P, s)
print('4. CaseBuilder builds the models and the setup carries them')

# --- 5. ProjectRunner selects the variable-property solve --------------------

P = 'src/app/ProjectRunner.cpp'
s = read(P)
if 'cfd/physics/TemperatureProperty.hpp' not in s:
    # ProjectRunner reaches cfd::physics through SimulationSetup.hpp today; the
    # ConstantProperty wrapper below names cfd::physics directly, so the header
    # it comes from is named directly too.
    s = apply(s,
              '#include "cfd/physics/MassFlux.hpp"\n',
              '#include "cfd/physics/MassFlux.hpp"\n'
              '#include "cfd/physics/TemperatureProperty.hpp"\n',
              'runner include')
if 'setup.conductivityModel' not in s:
    s = apply(s,
              '    thermalResult = thermalSolver.solve(setup.mesh, *setup.initialTemperature, '
              '*massFlux,\n'
              '                                        *setup.thermal, '
              '*setup.temperatureBoundaries);\n',
              '    // P15: a case whose physics.json gave a property MODEL takes the\n'
              '    // variable-property overload (P3-PHYS-003), which re-evaluates k(T)/cp(T)\n'
              '    // from the current iterate inside the SAME Picard loop -- not a second\n'
              '    // nonlinear loop, and not a solve with the model quietly replaced by its\n'
              '    // value at the initial temperature.\n'
              '    //\n'
              '    // A model may be given for one property and not the other, so the\n'
              '    // constant side is wrapped in a ConstantProperty: the overload then\n'
              '    // reproduces the fixed-property solve exactly for that property, which is\n'
              "    // that class's documented purpose (P3-PHYS-003 section 4) rather than a\n"
              '    // substitution invented here.\n'
              '    //\n'
              '    // Neither model present -> the original call, unchanged. That is the\n'
              '    // overwhelmingly common path and it must stay bit-for-bit what it was.\n'
              '    if (setup.conductivityModel != nullptr || setup.specificHeatModel != nullptr) {\n'
              '      const cfd::physics::ConstantProperty constantConductivity{\n'
              '          setup.thermal->conductivity()};\n'
              '      const cfd::physics::ConstantProperty constantSpecificHeat{\n'
              '          setup.thermal->specificHeat()};\n'
              '      const cfd::physics::TemperatureProperty& conductivity =\n'
              '          setup.conductivityModel != nullptr\n'
              '              ? static_cast<const cfd::physics::TemperatureProperty&>(\n'
              '                    *setup.conductivityModel)\n'
              '              : static_cast<const cfd::physics::TemperatureProperty&>(\n'
              '                    constantConductivity);\n'
              '      const cfd::physics::TemperatureProperty& specificHeat =\n'
              '          setup.specificHeatModel != nullptr\n'
              '              ? static_cast<const cfd::physics::TemperatureProperty&>(\n'
              '                    *setup.specificHeatModel)\n'
              '              : static_cast<const cfd::physics::TemperatureProperty&>(\n'
              '                    constantSpecificHeat);\n'
              '      thermalResult =\n'
              '          thermalSolver.solve(setup.mesh, *setup.initialTemperature, *massFlux,\n'
              '                              conductivity, specificHeat, '
              '*setup.temperatureBoundaries);\n'
              '    } else {\n'
              '      thermalResult =\n'
              '          thermalSolver.solve(setup.mesh, *setup.initialTemperature, *massFlux,\n'
              '                              *setup.thermal, *setup.temperatureBoundaries);\n'
              '    }\n',
              'runner solve')
write(P, s)
print('5. ProjectRunner selects the variable-property solve')
