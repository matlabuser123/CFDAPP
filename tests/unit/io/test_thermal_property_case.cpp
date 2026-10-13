// P15 -- case-file reachability for temperature-dependent properties.
//
// The property models do not count as production capability until a user can
// configure them from a real case file. These tests cover the three things
// that matter: the pre-existing scalar form still means exactly what it meant,
// the new object form reaches the typed config, and every malformed input is
// REJECTED with a structured diagnostic rather than silently defaulted.
//
// Same isolation convention as test_thermal_case.cpp: each test overwrites
// exactly the one field it wants to exercise on an otherwise-valid fixture.
#include <gtest/gtest.h>

#include <string>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {

// Matches CaseFixture's default 4-patch mesh; left/right prescribe a
// temperature so the thermal block is actually exercised.
constexpr const char* kThermalBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "fixed_temperature", "value": 310.0}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "fixed_temperature", "value": 290.0}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "adiabatic"}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "adiabatic"}}
  }
})";

// Writes a physics.json whose "thermal" block is exactly `thermalBody`.
void makeThermalFixture(const CaseFixture& fixture, const std::string& thermalBody) {
  fixture.write("physics.json",
                std::string(R"({"model": "incompressible_laminar", "density": 1.0,)") +
                    R"( "dynamic_viscosity": 0.01, "thermal": )" + thermalBody + "}");
  fixture.write("boundaries.json", kThermalBoundaries);
}

// A rejection must NAME the offending field, not merely fail. P13-002 made the
// diagnostic structured for exactly this reason, so both halves are checked:
// the human-readable message and the machine-readable CaseDiagnostic.
void expectRejectedNaming(const std::string& thermalBody, const std::string& expectedField) {
  CaseFixture fixture;
  makeThermalFixture(fixture, thermalBody);
  try {
    (void)CaseReader{}.read(fixture.directory());
    FAIL() << "accepted an invalid thermal property block: " << thermalBody;
  } catch (const CaseConfigurationError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find(expectedField), std::string::npos)
        << "message did not name the field: " << message;
    ASSERT_TRUE(error.diagnostic().has_value()) << "rejection carried no structured diagnostic";
    EXPECT_EQ(error.diagnostic()->file, "physics.json");
    EXPECT_EQ(error.diagnostic()->field, expectedField);
    EXPECT_FALSE(error.diagnostic()->constraint.empty());
  }
}

// rejectUnknownKeys names the OFFENDING KEY as the structured field and puts
// the containing context in the constraint -- the convention it has used at
// every one of its ~140 call sites since P13-002, and not something for P15 to
// change. So the parent field is checked where it actually appears.
void expectRejectedUnknownKey(const std::string& thermalBody, const std::string& expectedKey,
                              const std::string& expectedContext) {
  CaseFixture fixture;
  makeThermalFixture(fixture, thermalBody);
  try {
    (void)CaseReader{}.read(fixture.directory());
    FAIL() << "accepted an unknown key: " << thermalBody;
  } catch (const CaseConfigurationError& error) {
    ASSERT_TRUE(error.diagnostic().has_value());
    EXPECT_EQ(error.diagnostic()->file, "physics.json");
    EXPECT_EQ(error.diagnostic()->field, expectedKey);
    EXPECT_NE(error.diagnostic()->constraint.find(expectedContext), std::string::npos)
        << "constraint did not name the containing block: " << error.diagnostic()->constraint;
  }
}

}  // namespace

// --- backward compatibility: the scalar form is untouched --------------------

TEST(ThermalPropertyCase, ScalarFormStillMeansAConstant) {
  CaseFixture fixture;
  makeThermalFixture(
      fixture, R"({"conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0})");
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.thermal.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.thermal->conductivity, 0.6);
  EXPECT_DOUBLE_EQ(definition.physics.thermal->specificHeat, 4180.0);
  EXPECT_DOUBLE_EQ(definition.physics.thermal->initialTemperature, 300.0);
  // An absent model is what tells a consumer the value is a genuine constant
  // rather than a model that happens to be flat.
  EXPECT_FALSE(definition.physics.thermal->conductivityModel.has_value());
  EXPECT_FALSE(definition.physics.thermal->specificHeatModel.has_value());

  // And it still reaches the runtime objects unchanged.
  const auto setup = CaseBuilder{}.build(definition);
  ASSERT_TRUE(setup.thermal.has_value());
  EXPECT_DOUBLE_EQ(setup.thermal->conductivity(), 0.6);
  EXPECT_DOUBLE_EQ(setup.thermal->specificHeat(), 4180.0);
}

// --- the object form reaches the typed config --------------------------------

TEST(ThermalPropertyCase, PolynomialConductivityIsParsedAndEvaluated) {
  // k(T) = 2 + 0.01 T, initial T = 300 -> k = 5.0
  CaseFixture fixture;
  makeThermalFixture(fixture, R"({
      "conductivity": {"model": "polynomial", "coefficients": [2.0, 0.01],
                       "min_temperature": 200.0, "max_temperature": 1200.0},
      "specific_heat": 4180.0, "initial_temperature": 300.0})");
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.thermal->conductivityModel.has_value());
  const auto& model = *definition.physics.thermal->conductivityModel;
  ASSERT_EQ(model.coefficients.size(), 2u);
  EXPECT_DOUBLE_EQ(model.coefficients[0], 2.0);
  EXPECT_DOUBLE_EQ(model.coefficients[1], 0.01);
  EXPECT_DOUBLE_EQ(model.minimumTemperature, 200.0);
  EXPECT_DOUBLE_EQ(model.maximumTemperature, 1200.0);
  // The scalar carries the model evaluated at initial_temperature, so a
  // consumer that does not know about models still gets an in-range value.
  EXPECT_DOUBLE_EQ(definition.physics.thermal->conductivity, 5.0);
  // specific_heat was a plain number in the same block: the forms mix freely.
  EXPECT_FALSE(definition.physics.thermal->specificHeatModel.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.thermal->specificHeat, 4180.0);
}

TEST(ThermalPropertyCase, PolynomialSpecificHeatIsParsedAndEvaluated) {
  // cp(T) = 900 + 0.4 T at T = 400 -> 1060
  CaseFixture fixture;
  makeThermalFixture(fixture, R"({
      "conductivity": 0.6,
      "specific_heat": {"model": "polynomial", "coefficients": [900.0, 0.4],
                        "min_temperature": 300.0, "max_temperature": 1000.0},
      "initial_temperature": 400.0})");
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.thermal->specificHeatModel.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.thermal->specificHeat, 1060.0);
  EXPECT_FALSE(definition.physics.thermal->conductivityModel.has_value());
}

TEST(ThermalPropertyCase, HigherOrderPolynomialUsesTheSameHornerOrder) {
  // k(T) = 1 + 2T + 3T^2 at T = 10 -> 1 + 20 + 300 = 321. The parser runs
  // Horner in the same operand order as TemperatureDependentProperty, so the
  // value a case reports and the value the model computes agree bit for bit.
  // These operands are exact, so EXPECT_EQ is the right instrument here --
  // EXPECT_DOUBLE_EQ would tolerate a reassociation.
  CaseFixture fixture;
  makeThermalFixture(fixture, R"({
      "conductivity": {"model": "polynomial", "coefficients": [1.0, 2.0, 3.0],
                       "min_temperature": 1.0, "max_temperature": 100.0},
      "specific_heat": 4180.0, "initial_temperature": 10.0})");
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_EQ(definition.physics.thermal->conductivity, 321.0);
}

TEST(ThermalPropertyCase, ASingleCoefficientIsAConstantModel) {
  CaseFixture fixture;
  makeThermalFixture(fixture, R"({
      "conductivity": {"model": "polynomial", "coefficients": [7.5],
                       "min_temperature": 200.0, "max_temperature": 900.0},
      "specific_heat": 4180.0, "initial_temperature": 350.0})");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.thermal->conductivityModel.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.thermal->conductivity, 7.5);
}

TEST(ThermalPropertyCase, BothPropertiesMayBeModelsAtOnce) {
  CaseFixture fixture;
  makeThermalFixture(fixture, R"({
      "conductivity": {"model": "polynomial", "coefficients": [0.5, 0.001],
                       "min_temperature": 250.0, "max_temperature": 800.0},
      "specific_heat": {"model": "polynomial", "coefficients": [4000.0, 0.5],
                        "min_temperature": 250.0, "max_temperature": 800.0},
      "initial_temperature": 300.0})");
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_TRUE(definition.physics.thermal->conductivityModel.has_value());
  EXPECT_TRUE(definition.physics.thermal->specificHeatModel.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.thermal->conductivity, 0.8);
  EXPECT_DOUBLE_EQ(definition.physics.thermal->specificHeat, 4150.0);
}

// --- rejections, each naming its exact field ---------------------------------

TEST(ThermalPropertyCase, UnknownModelNameIsRejectedNotDefaulted) {
  // THE one that matters: a case asking for physics this build does not have
  // must FAIL, not quietly solve a different problem with a constant.
  expectRejectedNaming(
      R"({"conductivity": {"model": "arrhenius", "coefficients": [1.0],
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.model");
}

TEST(ThermalPropertyCase, MissingModelKeyIsRejected) {
  expectRejectedNaming(
      R"({"conductivity": {"coefficients": [1.0],
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.model");
}

TEST(ThermalPropertyCase, EmptyCoefficientListIsRejected) {
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [],
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.coefficients");
}

TEST(ThermalPropertyCase, CoefficientsMustBeAnArray) {
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": 2.0,
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.coefficients");
}

TEST(ThermalPropertyCase, NonNumericCoefficientIsRejectedNamingItsIndex) {
  // The index is the point: "a coefficient is bad" is not actionable on a
  // six-term polynomial.
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0, "oops", 1.0],
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.coefficients[1]");
}

TEST(ThermalPropertyCase, NullCoefficientIsRejectedNamingItsIndex) {
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0, null],
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.coefficients[1]");
}

TEST(ThermalPropertyCase, InvertedTemperatureRangeIsRejected) {
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0],
          "min_temperature": 900.0, "max_temperature": 200.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.max_temperature");
}

TEST(ThermalPropertyCase, DegenerateTemperatureRangeIsRejected) {
  // min == max is not a range; a property valid at exactly one temperature
  // cannot be integrated for enthalpy.
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0],
          "min_temperature": 300.0, "max_temperature": 300.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.max_temperature");
}

TEST(ThermalPropertyCase, MissingTemperatureRangeIsRejected) {
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0],
          "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.min_temperature");
}

TEST(ThermalPropertyCase, InitialTemperatureOutsideTheModelRangeIsRejected) {
  // Caught at parse time rather than at the first solve: a model whose range
  // excludes the initial state cannot even be evaluated to start.
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0, 0.01],
          "min_temperature": 500.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.min_temperature/max_temperature");
}

TEST(ThermalPropertyCase, NonPositiveValueAtInitialTemperatureIsRejected) {
  // k(T) = 10 - 0.05 T is negative at T = 300. Not a material.
  expectRejectedNaming(
      R"({"conductivity": {"model": "polynomial", "coefficients": [10.0, -0.05],
          "min_temperature": 200.0, "max_temperature": 900.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity.coefficients");
}

TEST(ThermalPropertyCase, UnknownKeyInsideTheModelIsRejected) {
  // A misspelt or invented key is not ignored: a model configured with a
  // setting this build does not read would otherwise run as if the user had
  // never written it.
  expectRejectedUnknownKey(
      R"({"conductivity": {"model": "polynomial", "coefficients": [2.0],
          "min_temperature": 200.0, "max_temperature": 900.0, "reference": 1.0},
          "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "reference", "physics.json thermal.conductivity");
}

TEST(ThermalPropertyCase, AStringWhereAPropertyBelongsIsRejected) {
  // "0.6" is not 0.6. Rejected rather than coerced.
  expectRejectedNaming(
      R"({"conductivity": "0.6", "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity");
}

TEST(ThermalPropertyCase, AnArrayWhereAPropertyBelongsIsRejected) {
  // A bare coefficient list without the model wrapper: plausible, and wrong.
  expectRejectedNaming(
      R"({"conductivity": [2.0, 0.01], "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity");
}

TEST(ThermalPropertyCase, NegativeConstantIsStillRejected) {
  // The pre-existing validation must not have been lost when the scalar form
  // became one branch of two.
  expectRejectedNaming(
      R"({"conductivity": -0.6, "specific_heat": 4180.0, "initial_temperature": 300.0})",
      "physics.json thermal.conductivity");
}

TEST(ThermalPropertyCase, ZeroConstantSpecificHeatIsStillRejected) {
  expectRejectedNaming(
      R"({"conductivity": 0.6, "specific_heat": 0.0, "initial_temperature": 300.0})",
      "physics.json thermal.specific_heat");
}

TEST(ThermalPropertyCase, SpecificHeatModelIsValidatedTheSameWayAsConductivity) {
  // Both properties run through one function; this pins that the second one
  // is not silently skipped.
  expectRejectedNaming(
      R"({"conductivity": 0.6,
          "specific_heat": {"model": "polynomial", "coefficients": [],
                            "min_temperature": 200.0, "max_temperature": 900.0},
          "initial_temperature": 300.0})",
      "physics.json thermal.specific_heat.coefficients");
}
