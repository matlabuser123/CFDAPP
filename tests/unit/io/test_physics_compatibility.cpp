// P10-APP-004: the one authoritative production-physics compatibility
// system (see PhysicsConfigParser.cpp's validatePhysicsCompatibility for
// the matrix itself). This file is the matrix's own test suite -- every
// supported combination it claims and every combination it rejects has a
// test here, plus diagnostic-message-content checks. Per-block parsing
// details (individual field validation, defaults, etc.) stay in each
// block's own test_<x>_case.cpp; this file only exercises cross-block
// combinations.
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

void expectRejected(const CaseFixture& fixture) {
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

// Fails the test unless reading `fixture` throws CaseConfigurationError
// whose message contains `expectedSubstring` -- confirms not just that an
// unsupported combination is rejected, but that the diagnostic actually
// names the right thing (P10-APP-004 task 4/6: "fail early with clear
// diagnostics").
void expectRejectedWithMessage(const CaseFixture& fixture, const std::string& expectedSubstring) {
  try {
    (void)CaseReader{}.read(fixture.directory());
    FAIL() << "expected CaseConfigurationError mentioning \"" << expectedSubstring << "\"";
  } catch (const CaseConfigurationError& e) {
    const std::string message = e.what();
    EXPECT_NE(message.find(expectedSubstring), std::string::npos)
        << "error message was: " << message;
  }
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

constexpr const char* kMultiphaseBlock =
    R"("multiphase": {
          "phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
          "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
          "initial_alpha": 0.5,
          "transport_time_step": 0.01
        })";

// Species-enabled boundaries.json (matches test_species_case.cpp's own
// kSpeciesBoundaries).
constexpr const char* kSpeciesBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_value", "value": 1.0}}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}}
  }
})";

// Species AND multiphase both enabled per patch (needed by the
// SpeciesSupportedAlongsideMultiphase combined test below).
constexpr const char* kSpeciesAndMultiphaseBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_value", "value": 1.0}},
               "alpha": {"type": "fixed_value", "value": 1.0}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}},
               "alpha": {"type": "fixed_value", "value": 0.0}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}},
               "alpha": {"type": "fixed_gradient", "value": 0.0}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}},
               "alpha": {"type": "fixed_gradient", "value": 0.0}}
  }
})";

// Thermal-enabled boundaries.json (matches test_thermal_case.cpp's own
// kThermalBoundaries) -- needed by CompressibleSupportedAlongsideBuoyancy
// below, which enables thermal/buoyancy/compressible but not species.
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

// Temperature AND species both enabled per patch (needed by
// AllCompatibleModulesTogether below, which enables both).
constexpr const char* kThermalAndSpeciesBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "fixed_temperature", "value": 310.0},
               "species": {"CO2": {"type": "fixed_value", "value": 1.0}}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "fixed_temperature", "value": 290.0},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "adiabatic"},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "adiabatic"},
               "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}}}
  }
})";

}  // namespace

// --- Supported combinations (not covered by any single block's own
//     test_<x>_case.cpp, since those only exercise one block at a time)
//     -----------------------------------------------------------------

TEST(PhysicsCompatibilityTest, SpeciesSupportedAlongsideMultiphase) {
  CaseFixture fixture;
  fixture.write("physics.json",
                std::string(R"({"model": "incompressible_laminar", "density": 1.0,
                    "dynamic_viscosity": 1.0e-5,
                    "species": [{"name": "CO2", "diffusivity": 1.6e-5, "initial_concentration": 0.0}],
                    )") +
                    kMultiphaseBlock + "}");
  fixture.write("boundaries.json", kSpeciesAndMultiphaseBoundaries);

  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_EQ(definition.physics.species.size(), 1u);
  ASSERT_TRUE(definition.physics.multiphase.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_EQ(setup.species.size(), 1u);
  EXPECT_TRUE(setup.multiphase.has_value());
}

TEST(PhysicsCompatibilityTest, SpeciesSupportedAlongsideCompressible) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5,
                    "species": [{"name": "CO2", "diffusivity": 1.6e-5, "initial_concentration": 0.0}],
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0}})");
  fixture.write("boundaries.json", kSpeciesBoundaries);

  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_EQ(definition.physics.species.size(), 1u);
  ASSERT_TRUE(definition.physics.compressible.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_EQ(setup.species.size(), 1u);
  EXPECT_TRUE(setup.compressible.has_value());
}

TEST(PhysicsCompatibilityTest, CompressibleSupportedAlongsideBuoyancy) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.2, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                                "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "thermal_coupled": true}})");
  fixture.write("boundaries.json", kThermalBoundaries);

  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.buoyancy.has_value());
  ASSERT_TRUE(definition.physics.compressible.has_value());
  EXPECT_TRUE(definition.physics.compressible->thermalCoupled);

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_TRUE(setup.buoyancy.has_value());
  EXPECT_TRUE(setup.compressible.has_value());
}

TEST(PhysicsCompatibilityTest, CompressibleSupportedAlongsideTurbulence) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0}})");
  // Neither turbulence nor isothermal compressible needs any per-patch
  // boundary key beyond velocity/pressure -- CaseFixture's own default
  // boundaries.json is enough.

  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.turbulence.has_value());
  ASSERT_TRUE(definition.physics.compressible.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_TRUE(setup.compressible.has_value());
}

// The maximal supported combination: every module except multiphase (which
// excludes both turbulence and compressible) enabled at once -- thermal,
// turbulence, buoyancy, species, and compressible(thermal_coupled) all
// together. If any pairwise rule above were accidentally too strict, this
// is the test that would catch it.
TEST(PhysicsCompatibilityTest, AllCompatibleModulesTogether) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.2, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                                "initial_temperature": 300.0},
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]},
                    "species": [{"name": "CO2", "diffusivity": 1.6e-5, "initial_concentration": 0.0}],
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "thermal_coupled": true}})");
  fixture.write("boundaries.json", kThermalAndSpeciesBoundaries);

  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_TRUE(definition.physics.thermal.has_value());
  EXPECT_TRUE(definition.physics.turbulence.has_value());
  EXPECT_TRUE(definition.physics.buoyancy.has_value());
  EXPECT_EQ(definition.physics.species.size(), 1u);
  EXPECT_TRUE(definition.physics.compressible.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_TRUE(setup.thermal.has_value());
  EXPECT_TRUE(setup.buoyancy.has_value());
  EXPECT_EQ(setup.species.size(), 1u);
  EXPECT_TRUE(setup.compressible.has_value());
}

// --- Unsupported combinations, with diagnostic-message-content checks ---

TEST(PhysicsCompatibilityTest, RejectsMultiphaseTogetherWithCompressible) {
  CaseFixture fixture;
  fixture.write("physics.json",
                std::string(R"({"model": "incompressible_laminar", "density": 1.0,
                    "dynamic_viscosity": 1.0e-5,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0},
                    )") +
                    kMultiphaseBlock + "}");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejectedWithMessage(fixture, "compressible");
}

TEST(PhysicsCompatibilityTest, RejectsMultiphaseTogetherWithTurbulence) {
  CaseFixture fixture;
  fixture.write("physics.json",
                std::string(R"({"model": "incompressible_laminar", "density": 1.0,
                    "dynamic_viscosity": 1.0e-5,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005},
                    )") +
                    kMultiphaseBlock + "}");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejectedWithMessage(fixture, "turbulence");
}

TEST(PhysicsCompatibilityTest, RejectsBuoyancyWithoutThermal) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.2, "dynamic_viscosity": 0.01,
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]}})");
  expectRejectedWithMessage(fixture, "thermal");
}

TEST(PhysicsCompatibilityTest, RejectsCompressibleThermalCoupledWithoutThermal) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "thermal_coupled": true}})");
  expectRejectedWithMessage(fixture, "thermal");
}

// P12-COMP-002: CompressibleSIMPLE (compressible.coupled: true) has no
// turbulence-model/buoyancy-source injection point yet -- both rejected
// at parse time. The non-coupled combinations above
// (CompressibleSupportedAlongsideTurbulence/Buoyancy) confirm the
// default (coupled absent/false) mode is unaffected.
TEST(PhysicsCompatibilityTest, RejectsCoupledCompressibleTogetherWithTurbulence) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0,
                                     "coupled": true}})");
  expectRejectedWithMessage(fixture, "turbulence");
}

TEST(PhysicsCompatibilityTest, RejectsCoupledCompressibleTogetherWithBuoyancy) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.2, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                                "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "thermal_coupled": true,
                                     "coupled": true}})");
  fixture.write("boundaries.json", kThermalBoundaries);
  expectRejectedWithMessage(fixture, "buoyancy");
}

// Conflicting/malformed: two independently-sufficient rejection reasons at
// once (multiphase+turbulence AND multiphase+compressible) -- must still
// reject, regardless of which rule fires first.
TEST(PhysicsCompatibilityTest, RejectsMultiphaseCompressibleAndTurbulenceAllTogether) {
  CaseFixture fixture;
  fixture.write("physics.json",
                std::string(R"({"model": "incompressible_laminar", "density": 1.0,
                    "dynamic_viscosity": 1.0e-5,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0},
                    )") +
                    kMultiphaseBlock + "}");
  fixture.write("boundaries.json", kMultiphaseBoundaries);
  expectRejected(fixture);
}
