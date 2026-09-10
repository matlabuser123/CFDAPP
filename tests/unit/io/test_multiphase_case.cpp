// P6-PHYS-002: case-system multiphase configuration. Same isolation/
// structure convention as test_thermal_case.cpp / test_species_case.cpp.
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

constexpr const char* kMultiphaseBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "alpha": {"type": "fixed_value", "value": 1.0}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "alpha": {"type": "fixed_value", "value": 0.0}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "alpha": {"type": "fixed_gradient", "value": 0.0}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "alpha": {"type": "fixed_gradient", "value": 0.0}}
  }
})";

constexpr const char* kMultiphasePhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
        "multiphase": {
          "phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
          "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
          "initial_alpha": 0.5,
          "transport_time_step": 0.01
        }})";

void makeMultiphaseFixture(const CaseFixture& fixture) {
  fixture.write("physics.json", kMultiphasePhysics);
  fixture.write("boundaries.json", kMultiphaseBoundaries);
}

}  // namespace

TEST(MultiphaseCaseTest, NonMultiphaseFixtureHasNoMultiphaseConfig) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_FALSE(definition.physics.multiphase.has_value());
  for (const auto& [name, patch] : definition.boundaries.patches) {
    (void)name;
    EXPECT_FALSE(patch.alpha.has_value());
  }
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.multiphase.has_value());
}

TEST(MultiphaseCaseTest, ParsesMultiphaseConfigWhenEnabled) {
  CaseFixture fixture;
  makeMultiphaseFixture(fixture);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.multiphase.has_value());
  EXPECT_EQ(definition.physics.multiphase->phase1.name, "water");
  EXPECT_DOUBLE_EQ(definition.physics.multiphase->phase1.density, 1000.0);
  EXPECT_EQ(definition.physics.multiphase->phase2.name, "air");
  EXPECT_DOUBLE_EQ(definition.physics.multiphase->initialAlpha, 0.5);
  EXPECT_DOUBLE_EQ(definition.physics.multiphase->transportTimeStep, 0.01);

  ASSERT_TRUE(definition.boundaries.patches.at("left").alpha.has_value());
  EXPECT_EQ(definition.boundaries.patches.at("left").alpha->type, "fixed_value");
  EXPECT_DOUBLE_EQ(definition.boundaries.patches.at("left").alpha->value, 1.0);
}

TEST(MultiphaseCaseTest, BuilderConstructsMultiphaseRuntimeObjects) {
  CaseFixture fixture;
  makeMultiphaseFixture(fixture);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.multiphase.has_value());
  EXPECT_EQ(setup.multiphase->system.phase1().name(), "water");
  EXPECT_DOUBLE_EQ(setup.multiphase->system.phase2().viscosity(), 1.8e-5);
  ASSERT_EQ(setup.multiphase->initialAlpha.size(), setup.mesh.numberOfCells());
  for (cfd::Index i = 0; i < setup.multiphase->initialAlpha.size(); ++i) {
    EXPECT_DOUBLE_EQ(setup.multiphase->initialAlpha[i], 0.5);
  }
  EXPECT_EQ(setup.multiphase->alphaBoundaries.get("left").type(),
            BoundaryConditionType::FixedValue);
}

TEST(MultiphaseCaseTest, RejectsInitialAlphaOutOfRange) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
                    "multiphase": {"phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
                                   "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
                                   "initial_alpha": 1.5, "transport_time_step": 0.01}})");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}

TEST(MultiphaseCaseTest, RejectsNonPositivePhaseViscosity) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
                    "multiphase": {"phase1": {"name": "water", "density": 1000.0, "viscosity": 0.0},
                                   "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
                                   "initial_alpha": 0.5, "transport_time_step": 0.01}})");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}

TEST(MultiphaseCaseTest, RejectsIdenticalPhaseNames) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
                    "multiphase": {"phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
                                   "phase2": {"name": "water", "density": 1.0, "viscosity": 1.8e-5},
                                   "initial_alpha": 0.5, "transport_time_step": 0.01}})");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}

TEST(MultiphaseCaseTest, RejectsNonPositiveTransportTimeStep) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
                    "multiphase": {"phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
                                   "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
                                   "initial_alpha": 0.5, "transport_time_step": 0.0}})");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}

// The molecular-viscosity-baseline invariant (ProjectRunner.cpp's own
// mixture-viscosity adapter needs mu_t = mu_mix - dynamic_viscosity to
// never be negative) -- see PhysicsConfigParser.cpp's own comment.
TEST(MultiphaseCaseTest, RejectsTopLevelViscosityAboveBothPhases) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "multiphase": {"phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
                                   "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
                                   "initial_alpha": 0.5, "transport_time_step": 0.01}})");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}

TEST(MultiphaseCaseTest, RejectsMultiphaseTogetherWithTurbulence) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
                    "turbulence": {"model": "laminar", "initial_k": 0.1, "initial_epsilon": 0.01},
                    "multiphase": {"phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
                                   "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
                                   "initial_alpha": 0.5, "transport_time_step": 0.01}})");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}

TEST(MultiphaseCaseTest, RejectsMissingPerPatchAlphaWhenMultiphaseEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kMultiphasePhysics);
  // Default (nonmultiphase) boundaries.json -- no "alpha" key anywhere.
  expectRejected(fixture);
}

TEST(MultiphaseCaseTest, RejectsAlphaKeyWhenMultiphaseDisabled) {
  CaseFixture fixture;
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}
