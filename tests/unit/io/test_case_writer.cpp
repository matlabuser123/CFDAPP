// P5-A -- Production Case Manager, section 4: "a case created by the GUI
// must also be runnable from the CLI" / "a CLI-created case must be
// loadable by the GUI" -- CaseWriter's own round-trip identity, checked
// directly (read -> write -> read again -> compare field-for-field),
// not just "looks plausible". Covers every optional block CaseReader
// supports (thermal/turbulence/buoyancy, every velocity/pressure/
// temperature BC type with and without a "value") so a case built by a
// future GUI case editor round-trips regardless of which optional
// features it uses.
#include <gtest/gtest.h>

#include "CaseFixture.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"

using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::testutil::CaseFixture;

namespace {

// Round-trips `definition` through a fresh temporary directory (borrowed
// from a second CaseFixture purely for its auto-cleaned temp path --
// CaseWriter::write() overwrites every file that fixture initially wrote
// with valid content of its own) and returns what CaseReader reads back.
CaseDefinition roundTrip(const CaseDefinition& definition) {
  CaseFixture target;
  CaseWriter::write(target.directory(), definition);
  return CaseReader{}.read(target.directory());
}

}  // namespace

TEST(CaseWriterTest, BasicCaseRoundTripsExactly) {
  CaseFixture source;
  const CaseDefinition original = CaseReader{}.read(source.directory());
  const CaseDefinition reread = roundTrip(original);

  EXPECT_EQ(reread.caseConfig.name, original.caseConfig.name);
  EXPECT_EQ(reread.caseConfig.formatVersion, original.caseConfig.formatVersion);

  EXPECT_EQ(reread.geometry.type, original.geometry.type);
  EXPECT_DOUBLE_EQ(reread.geometry.length, original.geometry.length);
  EXPECT_DOUBLE_EQ(reread.geometry.height, original.geometry.height);

  EXPECT_EQ(reread.mesh.nx, original.mesh.nx);
  EXPECT_EQ(reread.mesh.ny, original.mesh.ny);

  EXPECT_EQ(reread.physics.model, original.physics.model);
  EXPECT_DOUBLE_EQ(reread.physics.density, original.physics.density);
  EXPECT_DOUBLE_EQ(reread.physics.dynamicViscosity, original.physics.dynamicViscosity);
  EXPECT_FALSE(reread.physics.thermal.has_value());

  ASSERT_EQ(reread.boundaries.patches.size(), original.boundaries.patches.size());
  EXPECT_EQ(reread.boundaries.patches.at("top").velocity.type, "moving_wall");
  EXPECT_DOUBLE_EQ(reread.boundaries.patches.at("top").velocity.value.x, 1.0);
  EXPECT_EQ(reread.boundaries.patches.at("left").velocity.type, "wall");
  EXPECT_EQ(reread.boundaries.patches.at("left").pressure.type, "fixed_gradient");
  EXPECT_DOUBLE_EQ(reread.boundaries.patches.at("left").pressure.value, 0.0);

  EXPECT_EQ(reread.solver.type, original.solver.type);
  EXPECT_EQ(reread.solver.maxIterations, original.solver.maxIterations);
  EXPECT_DOUBLE_EQ(reread.solver.velocityRelaxation, original.solver.velocityRelaxation);
  EXPECT_EQ(reread.solver.momentumSolver.type, original.solver.momentumSolver.type);
  EXPECT_DOUBLE_EQ(reread.solver.momentumSolver.absoluteTolerance,
                   original.solver.momentumSolver.absoluteTolerance);

  EXPECT_DOUBLE_EQ(reread.initialConditions.velocity.x, 0.0);
  EXPECT_DOUBLE_EQ(reread.initialConditions.pressure, 0.0);
}

TEST(CaseWriterTest, ExplicitInitialConditionsRoundTrip) {
  CaseFixture source;
  CaseDefinition original = CaseReader{}.read(source.directory());
  original.initialConditions.velocity = cfd::Vector2{0.5, -0.25};
  original.initialConditions.pressure = 1.5;

  const CaseDefinition reread = roundTrip(original);
  EXPECT_DOUBLE_EQ(reread.initialConditions.velocity.x, 0.5);
  EXPECT_DOUBLE_EQ(reread.initialConditions.velocity.y, -0.25);
  EXPECT_DOUBLE_EQ(reread.initialConditions.pressure, 1.5);
}

// Covers every optional physics.json block (thermal/turbulence/buoyancy)
// together, plus the temperature BC's own value/no-value split
// (fixed_temperature has a value, adiabatic does not) and the
// inlet/outlet velocity types (inlet has a value, outlet does not) --
// none of these appear in CaseFixture's own default (non-thermal,
// wall/moving_wall-only) case.
TEST(CaseWriterTest, ThermalTurbulenceBuoyancyRoundTrip) {
  CaseFixture source;
  CaseDefinition original = CaseReader{}.read(source.directory());

  original.physics.thermal = cfd::io::ThermalPhysicsConfig{0.6, 4180.0, 300.0};
  original.physics.turbulence = cfd::io::TurbulencePhysicsConfig{"k_epsilon",
                                                                 0.1,
                                                                 /*initialEpsilon=*/0.01,
                                                                 std::nullopt,
                                                                 /*kRelaxation=*/0.6,
                                                                 /*epsilonRelaxation=*/0.6,
                                                                 std::nullopt};
  original.physics.buoyancy =
      cfd::io::BuoyancyPhysicsConfig{0.0003, 300.0, cfd::Vector2{0.0, -9.81}};

  for (auto& [name, patch] : original.boundaries.patches) {
    (void)name;
    patch.temperature = cfd::io::TemperatureBoundarySpec{"adiabatic", 0.0};
  }
  original.boundaries.patches.at("left").temperature =
      cfd::io::TemperatureBoundarySpec{"fixed_temperature", 310.0};
  original.boundaries.patches.at("right").velocity =
      cfd::io::VelocityBoundarySpec{"inlet", cfd::Vector2{2.0, 0.0}};
  original.boundaries.patches.at("right").temperature =
      cfd::io::TemperatureBoundarySpec{"heat_flux", 500.0};
  original.boundaries.patches.at("bottom").velocity =
      cfd::io::VelocityBoundarySpec{"outlet", cfd::Vector2{}};
  original.boundaries.patches.at("bottom").pressure =
      cfd::io::PressureBoundarySpec{"fixed_value", 0.0};

  const CaseDefinition reread = roundTrip(original);

  ASSERT_TRUE(reread.physics.thermal.has_value());
  EXPECT_DOUBLE_EQ(reread.physics.thermal->conductivity, 0.6);
  EXPECT_DOUBLE_EQ(reread.physics.thermal->initialTemperature, 300.0);

  ASSERT_TRUE(reread.physics.turbulence.has_value());
  EXPECT_EQ(reread.physics.turbulence->model, "k_epsilon");
  ASSERT_TRUE(reread.physics.turbulence->initialEpsilon.has_value());
  EXPECT_DOUBLE_EQ(*reread.physics.turbulence->initialEpsilon, 0.01);
  EXPECT_FALSE(reread.physics.turbulence->initialOmega.has_value());
  ASSERT_TRUE(reread.physics.turbulence->kRelaxation.has_value());
  EXPECT_DOUBLE_EQ(*reread.physics.turbulence->kRelaxation, 0.6);

  ASSERT_TRUE(reread.physics.buoyancy.has_value());
  EXPECT_DOUBLE_EQ(reread.physics.buoyancy->beta, 0.0003);
  EXPECT_DOUBLE_EQ(reread.physics.buoyancy->gravity.y, -9.81);

  ASSERT_TRUE(reread.boundaries.patches.at("left").temperature.has_value());
  EXPECT_EQ(reread.boundaries.patches.at("left").temperature->type, "fixed_temperature");
  EXPECT_DOUBLE_EQ(reread.boundaries.patches.at("left").temperature->value, 310.0);

  ASSERT_TRUE(reread.boundaries.patches.at("top").temperature.has_value());
  EXPECT_EQ(reread.boundaries.patches.at("top").temperature->type, "adiabatic");

  EXPECT_EQ(reread.boundaries.patches.at("right").velocity.type, "inlet");
  EXPECT_DOUBLE_EQ(reread.boundaries.patches.at("right").velocity.value.x, 2.0);
  ASSERT_TRUE(reread.boundaries.patches.at("right").temperature.has_value());
  EXPECT_EQ(reread.boundaries.patches.at("right").temperature->type, "heat_flux");
  EXPECT_DOUBLE_EQ(reread.boundaries.patches.at("right").temperature->value, 500.0);

  EXPECT_EQ(reread.boundaries.patches.at("bottom").velocity.type, "outlet");
  EXPECT_EQ(reread.boundaries.patches.at("bottom").pressure.type, "fixed_value");
}

// P6-PHYS-001: physics.json's "species" array and each patch's
// "species" concentration-BC object round-trip through CaseWriter --
// none of these appear in CaseFixture's own default (nonspecies) case.
TEST(CaseWriterTest, SpeciesRoundTrip) {
  CaseFixture source;
  CaseDefinition original = CaseReader{}.read(source.directory());

  original.physics.species = {
      {"CO2", 1.6e-5, 0.0},
      {"O2", 2.0e-5, 0.21},
  };
  for (auto& [name, patch] : original.boundaries.patches) {
    (void)name;
    patch.concentration.emplace("CO2", cfd::io::ConcentrationBoundarySpec{"fixed_gradient", 0.0});
    patch.concentration.emplace("O2", cfd::io::ConcentrationBoundarySpec{"fixed_gradient", 0.0});
  }
  original.boundaries.patches.at("left").concentration.at("CO2") =
      cfd::io::ConcentrationBoundarySpec{"fixed_value", 1.0};

  const CaseDefinition reread = roundTrip(original);

  ASSERT_EQ(reread.physics.species.size(), 2u);
  EXPECT_EQ(reread.physics.species[0].name, "CO2");
  EXPECT_DOUBLE_EQ(reread.physics.species[0].diffusivity, 1.6e-5);
  EXPECT_DOUBLE_EQ(reread.physics.species[0].initialConcentration, 0.0);
  EXPECT_EQ(reread.physics.species[1].name, "O2");
  EXPECT_DOUBLE_EQ(reread.physics.species[1].initialConcentration, 0.21);

  ASSERT_EQ(reread.boundaries.patches.at("left").concentration.size(), 2u);
  EXPECT_EQ(reread.boundaries.patches.at("left").concentration.at("CO2").type, "fixed_value");
  EXPECT_DOUBLE_EQ(reread.boundaries.patches.at("left").concentration.at("CO2").value, 1.0);
  EXPECT_EQ(reread.boundaries.patches.at("right").concentration.at("O2").type, "fixed_gradient");
}
