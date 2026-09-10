// P6-PHYS-001: case-system species configuration. Same isolation/
// structure convention as test_thermal_case.cpp -- each rejection test
// overwrites exactly one file/field of an otherwise-valid species
// fixture, and the success-path tests confirm CaseReader/CaseBuilder
// produce the correct typed config and runtime objects.
#include <gtest/gtest.h>

#include "CaseFixture.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {

void expectRejected(const CaseFixture& fixture) {
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

// Two-species-enabled boundaries.json matching CaseFixture's default
// 4-patch (left/right/bottom/top) mesh -- left prescribes both species,
// right/bottom/top all zero-gradient.
constexpr const char* kSpeciesBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_value", "value": 1.0},
                           "O2": {"type": "fixed_value", "value": 0.21}}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                           "O2": {"type": "fixed_gradient", "value": 0.0}}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                           "O2": {"type": "fixed_gradient", "value": 0.0}}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                           "O2": {"type": "fixed_gradient", "value": 0.0}}}
  }
})";

constexpr const char* kSpeciesPhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "species": [
          {"name": "CO2", "diffusivity": 1.6e-5, "initial_concentration": 0.0},
          {"name": "O2", "diffusivity": 2.0e-5, "initial_concentration": 0.0}
        ]})";

void makeSpeciesFixture(const CaseFixture& fixture) {
  fixture.write("physics.json", kSpeciesPhysics);
  fixture.write("boundaries.json", kSpeciesBoundaries);
}

}  // namespace

// --- Backward compatibility -----------------------------------------------

TEST(SpeciesCaseTest, NonSpeciesFixtureHasNoSpeciesConfig) {
  CaseFixture fixture;  // default: no "species" key anywhere.
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_TRUE(definition.physics.species.empty());
  for (const auto& [name, patch] : definition.boundaries.patches) {
    (void)name;
    EXPECT_TRUE(patch.concentration.empty());
  }

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_TRUE(setup.species.empty());
}

// --- Success path -----------------------------------------------------------

TEST(SpeciesCaseTest, ParsesSpeciesListWhenEnabled) {
  CaseFixture fixture;
  makeSpeciesFixture(fixture);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_EQ(definition.physics.species.size(), 2u);
  EXPECT_EQ(definition.physics.species[0].name, "CO2");
  EXPECT_DOUBLE_EQ(definition.physics.species[0].diffusivity, 1.6e-5);
  EXPECT_DOUBLE_EQ(definition.physics.species[0].initialConcentration, 0.0);
  EXPECT_EQ(definition.physics.species[1].name, "O2");
  EXPECT_DOUBLE_EQ(definition.physics.species[1].diffusivity, 2.0e-5);

  ASSERT_EQ(definition.boundaries.patches.at("left").concentration.size(), 2u);
  EXPECT_EQ(definition.boundaries.patches.at("left").concentration.at("CO2").type, "fixed_value");
  EXPECT_DOUBLE_EQ(definition.boundaries.patches.at("left").concentration.at("CO2").value, 1.0);
  EXPECT_EQ(definition.boundaries.patches.at("right").concentration.at("O2").type,
            "fixed_gradient");
}

TEST(SpeciesCaseTest, BuilderConstructsSpeciesRuntimeObjects) {
  CaseFixture fixture;
  makeSpeciesFixture(fixture);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_EQ(setup.species.size(), 2u);
  EXPECT_EQ(setup.species[0].properties.name(), "CO2");
  EXPECT_DOUBLE_EQ(setup.species[0].properties.diffusivity(), 1.6e-5);
  ASSERT_EQ(setup.species[0].initialConcentration.size(), setup.mesh.numberOfCells());
  for (cfd::Index i = 0; i < setup.species[0].initialConcentration.size(); ++i) {
    EXPECT_DOUBLE_EQ(setup.species[0].initialConcentration[i], 0.0);
  }

  EXPECT_EQ(setup.species[0].concentrationBoundaries.get("left").type(),
            BoundaryConditionType::FixedValue);
  const auto& leftCo2Bc =
      static_cast<const FixedValue&>(setup.species[0].concentrationBoundaries.get("left"));
  EXPECT_DOUBLE_EQ(leftCo2Bc.value(), 1.0);

  EXPECT_EQ(setup.species[1].properties.name(), "O2");
  EXPECT_EQ(setup.species[1].concentrationBoundaries.get("right").type(),
            BoundaryConditionType::FixedGradient);
}

TEST(SpeciesCaseTest, ZeroDiffusivityIsAccepted) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "Tracer", "diffusivity": 0.0,
                                 "initial_concentration": 0.5}]})");
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"Tracer": {"type": "fixed_gradient", "value": 0.0}}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"Tracer": {"type": "fixed_gradient", "value": 0.0}}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"Tracer": {"type": "fixed_gradient", "value": 0.0}}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"Tracer": {"type": "fixed_gradient", "value": 0.0}}}
    }
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_DOUBLE_EQ(setup.species[0].properties.diffusivity(), 0.0);
}

// --- Rejections ---------------------------------------------------------------

TEST(SpeciesCaseTest, RejectsNegativeDiffusivity) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "CO2", "diffusivity": -1.0,
                                 "initial_concentration": 0.0}]})");
  fixture.write("boundaries.json", kSpeciesBoundaries);
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsEmptyName) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "", "diffusivity": 1.0,
                                 "initial_concentration": 0.0}]})");
  fixture.write("boundaries.json", kSpeciesBoundaries);
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsDuplicateSpeciesNames) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "CO2", "diffusivity": 1.0, "initial_concentration": 0.0},
                                {"name": "CO2", "diffusivity": 2.0, "initial_concentration": 0.0}]})");
  fixture.write("boundaries.json", kSpeciesBoundaries);
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsUnknownSpeciesKey) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "CO2", "diffusivity": 1.0, "initial_concentration": 0.0,
                                 "molar_mass": 44.0}]})");
  fixture.write("boundaries.json", kSpeciesBoundaries);
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsMissingPerPatchSpeciesWhenSpeciesEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kSpeciesPhysics);
  // Default (nonspecies) boundaries.json -- no "species" key anywhere.
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsSpeciesKeyWhenSpeciesDisabled) {
  CaseFixture fixture;
  // Default (nonspecies) physics.json, but a boundaries.json that adds a
  // "species" key anyway -- must be rejected as an unknown field, not
  // silently accepted/ignored.
  fixture.write("boundaries.json", kSpeciesBoundaries);
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsPatchMissingOneDeclaredSpecies) {
  CaseFixture fixture;
  fixture.write("physics.json", kSpeciesPhysics);
  // "left" is missing the "O2" entry -- only two species declared, but
  // this patch only supplies one.
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_value", "value": 1.0}}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                             "O2": {"type": "fixed_gradient", "value": 0.0}}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                             "O2": {"type": "fixed_gradient", "value": 0.0}}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                             "O2": {"type": "fixed_gradient", "value": 0.0}}}
    }
  })");
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsUnknownSpeciesNameOnPatch) {
  CaseFixture fixture;
  fixture.write("physics.json", kSpeciesPhysics);
  // "left" has a "N2" entry -- not one of the two declared species.
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_value", "value": 1.0},
                             "O2": {"type": "fixed_value", "value": 0.21},
                             "N2": {"type": "fixed_gradient", "value": 0.0}}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                             "O2": {"type": "fixed_gradient", "value": 0.0}}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                             "O2": {"type": "fixed_gradient", "value": 0.0}}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0},
                             "O2": {"type": "fixed_gradient", "value": 0.0}}}
    }
  })");
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsUnknownConcentrationBoundaryType) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "CO2", "diffusivity": 1.0,
                                 "initial_concentration": 0.0}]})");
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "inlet_concentration", "value": 1.0}}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}}
    }
  })");
  expectRejected(fixture);
}

TEST(SpeciesCaseTest, RejectsMissingValueForConcentrationBoundary) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "species": [{"name": "CO2", "diffusivity": 1.0,
                                 "initial_concentration": 0.0}]})");
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_value"}}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}}
    }
  })");
  expectRejected(fixture);
}
