// P2-THERMAL-004: case-system thermal configuration. Each rejection test
// overwrites exactly one file/field of an otherwise-valid thermal fixture
// (same isolation convention as test_case_validation.cpp), and the
// success-path tests confirm CaseReader/CaseBuilder produce the correct
// typed config and runtime objects.
#include <gtest/gtest.h>

#include "CaseFixture.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedTemperature;
using cfd::boundary::HeatFlux;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {

void expectRejected(const CaseFixture& fixture) {
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

// Thermal-enabled boundaries.json matching CaseFixture's default 4-patch
// (left/right/bottom/top) mesh -- left/right prescribe temperature,
// top/bottom are adiabatic.
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

constexpr const char* kThermalPhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "thermal": {"conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0}})";

void makeThermalFixture(const CaseFixture& fixture) {
  fixture.write("physics.json", kThermalPhysics);
  fixture.write("boundaries.json", kThermalBoundaries);
}

}  // namespace

// --- Backward compatibility ---------------------------------------------

TEST(ThermalCaseTest, NonThermalFixtureHasNoThermalConfig) {
  CaseFixture fixture;  // default: no "thermal" block, no "temperature" keys.
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_FALSE(definition.physics.thermal.has_value());
  for (const auto& [name, patch] : definition.boundaries.patches) {
    (void)name;
    EXPECT_FALSE(patch.temperature.has_value());
  }

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.thermal.has_value());
  EXPECT_FALSE(setup.temperatureBoundaries.has_value());
  EXPECT_FALSE(setup.initialTemperature.has_value());
}

// --- Success path ---------------------------------------------------------

TEST(ThermalCaseTest, ParsesThermalPropertiesWhenEnabled) {
  CaseFixture fixture;
  makeThermalFixture(fixture);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.thermal.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.thermal->conductivity, 0.6);
  EXPECT_DOUBLE_EQ(definition.physics.thermal->specificHeat, 4180.0);
  EXPECT_DOUBLE_EQ(definition.physics.thermal->initialTemperature, 300.0);

  ASSERT_TRUE(definition.boundaries.patches.at("left").temperature.has_value());
  EXPECT_EQ(definition.boundaries.patches.at("left").temperature->type, "fixed_temperature");
  EXPECT_DOUBLE_EQ(definition.boundaries.patches.at("left").temperature->value, 310.0);
  EXPECT_EQ(definition.boundaries.patches.at("bottom").temperature->type, "adiabatic");
}

TEST(ThermalCaseTest, BuilderConstructsThermalRuntimeObjects) {
  CaseFixture fixture;
  makeThermalFixture(fixture);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.thermal.has_value());
  EXPECT_DOUBLE_EQ(setup.thermal->conductivity(), 0.6);
  EXPECT_DOUBLE_EQ(setup.thermal->specificHeat(), 4180.0);

  ASSERT_TRUE(setup.temperatureBoundaries.has_value());
  EXPECT_EQ(setup.temperatureBoundaries->get("left").type(),
            BoundaryConditionType::FixedTemperature);
  EXPECT_EQ(setup.temperatureBoundaries->get("bottom").type(), BoundaryConditionType::Adiabatic);
  const auto& leftBc =
      static_cast<const FixedTemperature&>(setup.temperatureBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(leftBc.temperature(), 310.0);

  ASSERT_TRUE(setup.initialTemperature.has_value());
  ASSERT_EQ(setup.initialTemperature->size(), setup.mesh.numberOfCells());
  for (cfd::Index i = 0; i < setup.initialTemperature->size(); ++i) {
    EXPECT_DOUBLE_EQ((*setup.initialTemperature)[i], 300.0);
  }
}

TEST(ThermalCaseTest, BuilderConstructsHeatFluxWithCaseConductivity) {
  CaseFixture fixture;
  makeThermalFixture(fixture);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "heat_flux", "value": 12.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}}
    }
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  const auto& heatFluxBc = static_cast<const HeatFlux&>(setup.temperatureBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(heatFluxBc.heatFlux(), 12.0);
  // Conductivity is single-sourced from physics.json's "thermal.
  // conductivity", not a separately-configured per-patch value.
  EXPECT_DOUBLE_EQ(heatFluxBc.conductivity(), 0.6);
}

// --- Rejections -------------------------------------------------------------

TEST(ThermalCaseTest, RejectsNonPositiveConductivity) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.0, "specific_heat": 1000.0,
                                "initial_temperature": 300.0}})");
  fixture.write("boundaries.json", kThermalBoundaries);
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsNegativeSpecificHeat) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": -1.0,
                                "initial_temperature": 300.0}})");
  fixture.write("boundaries.json", kThermalBoundaries);
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsMissingThermalField) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "initial_temperature": 300.0}})");
  fixture.write("boundaries.json", kThermalBoundaries);
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsUnknownThermalKey) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 1000.0,
                                "initial_temperature": 300.0, "density": 1000.0}})");
  fixture.write("boundaries.json", kThermalBoundaries);
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsUnknownTemperatureBoundaryType) {
  CaseFixture fixture;
  makeThermalFixture(fixture);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "convective", "value": 1.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}}
    }
  })");
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsMissingValueForFixedTemperature) {
  CaseFixture fixture;
  makeThermalFixture(fixture);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "fixed_temperature"}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}}
    }
  })");
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsExtraneousValueForAdiabatic) {
  CaseFixture fixture;
  makeThermalFixture(fixture);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"}}
    }
  })");
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsMissingPerPatchTemperatureWhenThermalEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kThermalPhysics);
  // Default (nonthermal) boundaries.json -- no "temperature" key anywhere.
  expectRejected(fixture);
}

TEST(ThermalCaseTest, RejectsTemperatureKeyWhenThermalDisabled) {
  CaseFixture fixture;
  // Default (nonthermal) physics.json, but a boundaries.json that adds a
  // "temperature" key anyway -- must be rejected as an unknown field, not
  // silently accepted/ignored.
  fixture.write("boundaries.json", kThermalBoundaries);
  expectRejected(fixture);
}
