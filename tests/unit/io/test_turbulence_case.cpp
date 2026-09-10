// P2-TURB-004 (extended by P2-TURB-005 and P2-TURB-006): case-system
// turbulence configuration. Mirrors test_thermal_case.cpp's own
// structure/conventions.
#include <gtest/gtest.h>

#include "CaseFixture.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/WallOmega.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::WallOmega;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {

void expectRejected(const CaseFixture& fixture) {
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

constexpr const char* kKEpsilonPhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005}})";

constexpr const char* kKOmegaPhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "turbulence": {"model": "k_omega", "initial_k": 0.02, "initial_omega": 12.0}})";

constexpr const char* kSSTPhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "turbulence": {"model": "sst", "initial_k": 0.02, "initial_omega": 12.0}})";

}  // namespace

// --- Backward compatibility --------------------------------------------

TEST(TurbulenceCaseTest, LegacyFixtureHasNoTurbulenceConfig) {
  CaseFixture fixture;  // default: no "turbulence" block.
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_FALSE(definition.physics.turbulence.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.kEpsilonConfig.has_value());
  EXPECT_FALSE(setup.kOmegaConfig.has_value());
  EXPECT_FALSE(setup.sstConfig.has_value());
  EXPECT_FALSE(setup.kBoundaries.has_value());
  EXPECT_FALSE(setup.epsilonBoundaries.has_value());
  EXPECT_FALSE(setup.omegaBoundaries.has_value());
}

TEST(TurbulenceCaseTest, ExplicitLaminarModelCollapsesToNoTurbulenceConfig) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "laminar", "initial_k": 0.01,
                                   "initial_epsilon": 0.001}})");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.turbulence.has_value());
  EXPECT_EQ(definition.physics.turbulence->model, "laminar");

  // SimulationSetup collapses "explicit laminar" and "no block at all"
  // to the same all-nullopt state -- see SimulationSetup.hpp's own
  // comment.
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.kEpsilonConfig.has_value());
  EXPECT_FALSE(setup.kOmegaConfig.has_value());
  EXPECT_FALSE(setup.sstConfig.has_value());
  EXPECT_FALSE(setup.kBoundaries.has_value());
}

TEST(TurbulenceCaseTest, ExplicitLaminarModelAcceptsInitialOmegaToo) {
  // Section 10's "laminar accepts either" (see PhysicsConfigParser.cpp's
  // own comment): an explicit laminar declaration with initial_omega
  // instead of initial_epsilon must parse just as validly.
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "laminar", "initial_k": 0.01,
                                   "initial_omega": 10.0}})");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.turbulence.has_value());
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.kEpsilonConfig.has_value());
  EXPECT_FALSE(setup.kOmegaConfig.has_value());
}

// --- Success path: k_epsilon ---------------------------------------------

TEST(TurbulenceCaseTest, ParsesKEpsilonConfigWhenEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kKEpsilonPhysics);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.turbulence.has_value());
  EXPECT_EQ(definition.physics.turbulence->model, "k_epsilon");
  EXPECT_DOUBLE_EQ(definition.physics.turbulence->initialK, 0.02);
  ASSERT_TRUE(definition.physics.turbulence->initialEpsilon.has_value());
  EXPECT_DOUBLE_EQ(*definition.physics.turbulence->initialEpsilon, 0.005);
  EXPECT_FALSE(definition.physics.turbulence->initialOmega.has_value());
  EXPECT_FALSE(definition.physics.turbulence->kRelaxation.has_value());
  EXPECT_FALSE(definition.physics.turbulence->epsilonRelaxation.has_value());
}

TEST(TurbulenceCaseTest, ParsesOptionalRelaxationOverrides) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.02,
                                   "initial_epsilon": 0.005, "k_relaxation": 0.5,
                                   "epsilon_relaxation": 0.6}})");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.turbulence->kRelaxation.has_value());
  EXPECT_DOUBLE_EQ(*definition.physics.turbulence->kRelaxation, 0.5);
  ASSERT_TRUE(definition.physics.turbulence->epsilonRelaxation.has_value());
  EXPECT_DOUBLE_EQ(*definition.physics.turbulence->epsilonRelaxation, 0.6);

  const auto setup = CaseBuilder{}.build(definition);
  ASSERT_TRUE(setup.kEpsilonConfig.has_value());
  EXPECT_DOUBLE_EQ(setup.kEpsilonConfig->kRelaxation, 0.5);
  EXPECT_DOUBLE_EQ(setup.kEpsilonConfig->epsilonRelaxation, 0.6);
}

TEST(TurbulenceCaseTest, BuilderConstructsKEpsilonConfigAndBoundaries) {
  CaseFixture fixture;
  fixture.write("physics.json", kKEpsilonPhysics);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.kEpsilonConfig.has_value());
  EXPECT_FALSE(setup.kOmegaConfig.has_value());
  EXPECT_DOUBLE_EQ(setup.kEpsilonConfig->initialK, 0.02);
  EXPECT_DOUBLE_EQ(setup.kEpsilonConfig->initialEpsilon, 0.005);
  // Relaxation defaults untouched (KEpsilonConfig's own 0.7) since
  // neither override was present in kKEpsilonPhysics.
  EXPECT_DOUBLE_EQ(setup.kEpsilonConfig->kRelaxation, 0.7);
  EXPECT_DOUBLE_EQ(setup.kEpsilonConfig->epsilonRelaxation, 0.7);

  // CaseFixture's default boundaries.json: left/right/bottom = "wall",
  // top = "moving_wall" -- both map to the same simplified wall
  // treatment (sections 18-19): k = 0, epsilon zero-gradient.
  ASSERT_TRUE(setup.kBoundaries.has_value());
  ASSERT_TRUE(setup.epsilonBoundaries.has_value());
  EXPECT_FALSE(setup.omegaBoundaries.has_value());
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    EXPECT_EQ(setup.kBoundaries->get(patch).type(), BoundaryConditionType::FixedValue) << patch;
    const auto& kBc = static_cast<const FixedValue&>(setup.kBoundaries->get(patch));
    EXPECT_DOUBLE_EQ(kBc.value(), 0.0) << patch;

    EXPECT_EQ(setup.epsilonBoundaries->get(patch).type(), BoundaryConditionType::FixedGradient)
        << patch;
    const auto& epsBc = static_cast<const FixedGradient&>(setup.epsilonBoundaries->get(patch));
    EXPECT_DOUBLE_EQ(epsBc.gradient(), 0.0) << patch;
  }
}

TEST(TurbulenceCaseTest, InletPatchGetsFixedKAndEpsilonFromCaseConfig) {
  CaseFixture fixture;
  fixture.write("physics.json", kKEpsilonPhysics);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "inlet", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "outlet"}, "pressure": {"type": "fixed_value", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  const auto& inletK = static_cast<const FixedValue&>(setup.kBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(inletK.value(), 0.02);
  const auto& inletEpsilon = static_cast<const FixedValue&>(setup.epsilonBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(inletEpsilon.value(), 0.005);

  // outlet: zero-gradient for both.
  EXPECT_EQ(setup.kBoundaries->get("right").type(), BoundaryConditionType::FixedGradient);
  EXPECT_EQ(setup.epsilonBoundaries->get("right").type(), BoundaryConditionType::FixedGradient);
}

// --- Success path: k_omega (P2-TURB-005) ----------------------------------

TEST(TurbulenceCaseTest, ParsesKOmegaConfigWhenEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kKOmegaPhysics);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.turbulence.has_value());
  EXPECT_EQ(definition.physics.turbulence->model, "k_omega");
  EXPECT_DOUBLE_EQ(definition.physics.turbulence->initialK, 0.02);
  ASSERT_TRUE(definition.physics.turbulence->initialOmega.has_value());
  EXPECT_DOUBLE_EQ(*definition.physics.turbulence->initialOmega, 12.0);
  EXPECT_FALSE(definition.physics.turbulence->initialEpsilon.has_value());
}

TEST(TurbulenceCaseTest, BuilderConstructsKOmegaConfigAndBoundaries) {
  CaseFixture fixture;
  fixture.write("physics.json", kKOmegaPhysics);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.kOmegaConfig.has_value());
  EXPECT_FALSE(setup.kEpsilonConfig.has_value());
  EXPECT_DOUBLE_EQ(setup.kOmegaConfig->initialK, 0.02);
  EXPECT_DOUBLE_EQ(setup.kOmegaConfig->initialOmega, 12.0);
  EXPECT_DOUBLE_EQ(setup.kOmegaConfig->kRelaxation, 0.7);
  EXPECT_DOUBLE_EQ(setup.kOmegaConfig->omegaRelaxation, 0.7);

  ASSERT_TRUE(setup.kBoundaries.has_value());
  ASSERT_TRUE(setup.omegaBoundaries.has_value());
  EXPECT_FALSE(setup.epsilonBoundaries.has_value());
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    EXPECT_EQ(setup.kBoundaries->get(patch).type(), BoundaryConditionType::FixedValue) << patch;
    EXPECT_EQ(setup.omegaBoundaries->get(patch).type(), BoundaryConditionType::FixedGradient)
        << patch;
  }
}

TEST(TurbulenceCaseTest, KOmegaInletPatchGetsFixedKAndOmegaFromCaseConfig) {
  CaseFixture fixture;
  fixture.write("physics.json", kKOmegaPhysics);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "inlet", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "outlet"}, "pressure": {"type": "fixed_value", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  const auto& inletK = static_cast<const FixedValue&>(setup.kBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(inletK.value(), 0.02);
  const auto& inletOmega = static_cast<const FixedValue&>(setup.omegaBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(inletOmega.value(), 12.0);
}

// --- Success path: sst (P2-TURB-006) ---------------------------------------

TEST(TurbulenceCaseTest, ParsesSSTConfigWhenEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kSSTPhysics);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.turbulence.has_value());
  EXPECT_EQ(definition.physics.turbulence->model, "sst");
  EXPECT_DOUBLE_EQ(definition.physics.turbulence->initialK, 0.02);
  ASSERT_TRUE(definition.physics.turbulence->initialOmega.has_value());
  EXPECT_DOUBLE_EQ(*definition.physics.turbulence->initialOmega, 12.0);
  EXPECT_FALSE(definition.physics.turbulence->initialEpsilon.has_value());
}

TEST(TurbulenceCaseTest, BuilderConstructsSSTConfigAndBoundaries) {
  CaseFixture fixture;
  fixture.write("physics.json", kSSTPhysics);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.sstConfig.has_value());
  EXPECT_FALSE(setup.kEpsilonConfig.has_value());
  EXPECT_FALSE(setup.kOmegaConfig.has_value());
  EXPECT_DOUBLE_EQ(setup.sstConfig->initialK, 0.02);
  EXPECT_DOUBLE_EQ(setup.sstConfig->initialOmega, 12.0);
  EXPECT_DOUBLE_EQ(setup.sstConfig->kRelaxation, 0.7);
  EXPECT_DOUBLE_EQ(setup.sstConfig->omegaRelaxation, 0.7);

  ASSERT_TRUE(setup.kBoundaries.has_value());
  ASSERT_TRUE(setup.omegaBoundaries.has_value());
  EXPECT_FALSE(setup.epsilonBoundaries.has_value());
  // CaseFixture's default boundaries.json: left/right/bottom = "wall",
  // top = "moving_wall" -- unlike k-omega's zero-gradient simplification,
  // SST's wall omega treatment is the real Wilcox near-wall value (see
  // CaseBuilder.cpp's buildTurbulenceOmegaBoundarySST comment).
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    EXPECT_EQ(setup.kBoundaries->get(patch).type(), BoundaryConditionType::FixedValue) << patch;
    EXPECT_EQ(setup.omegaBoundaries->get(patch).type(), BoundaryConditionType::WallOmega) << patch;
  }
}

TEST(TurbulenceCaseTest, SSTInletPatchGetsFixedKAndOmegaFromCaseConfig) {
  CaseFixture fixture;
  fixture.write("physics.json", kSSTPhysics);
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "inlet", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "outlet"}, "pressure": {"type": "fixed_value", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  const auto& inletK = static_cast<const FixedValue&>(setup.kBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(inletK.value(), 0.02);
  const auto& inletOmega = static_cast<const FixedValue&>(setup.omegaBoundaries->get("left"));
  EXPECT_DOUBLE_EQ(inletOmega.value(), 12.0);

  // outlet: zero-gradient; wall: real WallOmega, not zero-gradient.
  EXPECT_EQ(setup.omegaBoundaries->get("right").type(), BoundaryConditionType::FixedGradient);
  EXPECT_EQ(setup.omegaBoundaries->get("bottom").type(), BoundaryConditionType::WallOmega);
}

// --- Rejection ------------------------------------------------------------

TEST(TurbulenceCaseTest, RejectsUnsupportedModelName) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "spalart_allmaras", "initial_k": 0.01,
                                   "initial_epsilon": 0.001}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsSSTModelWithInitialEpsilonOnly) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "sst", "initial_k": 0.01,
                                   "initial_epsilon": 0.001}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsBothInitialEpsilonAndInitialOmega) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01,
                                   "initial_epsilon": 0.001, "initial_omega": 10.0}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsNeitherInitialEpsilonNorInitialOmega) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsKEpsilonModelWithInitialOmegaOnly) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01,
                                   "initial_omega": 10.0}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsKOmegaModelWithInitialEpsilonOnly) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_omega", "initial_k": 0.01,
                                   "initial_epsilon": 0.001}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsNonPositiveInitialK) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.0,
                                   "initial_epsilon": 0.001}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsNonPositiveInitialEpsilon) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01,
                                   "initial_epsilon": -1.0}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsNonPositiveInitialOmega) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_omega", "initial_k": 0.01,
                                   "initial_omega": -1.0}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsOutOfRangeRelaxation) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01,
                                   "initial_epsilon": 0.001, "k_relaxation": 1.5}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsOmegaRelaxationWithoutInitialOmega) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01,
                                   "initial_epsilon": 0.001, "omega_relaxation": 0.5}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsEpsilonRelaxationWithoutInitialEpsilon) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_omega", "initial_k": 0.01,
                                   "initial_omega": 10.0, "epsilon_relaxation": 0.5}})");
  expectRejected(fixture);
}

TEST(TurbulenceCaseTest, RejectsUnknownKey) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "turbulence": {"model": "k_epsilon", "initial_k": 0.01,
                                   "initial_epsilon": 0.001, "not_a_field": 1.0}})");
  expectRejected(fixture);
}
