// P1 -- Case System, section 34: CaseReader basics -- a valid case
// parses into the expected typed configuration, and missing files/
// malformed JSON produce the correct exception type (TODO.md section
// 21-22: never a generic third-party parser stack trace or a raw null
// dereference).
#include <gtest/gtest.h>

#include <fstream>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::IOError;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

// P6-GPU-002: "CG" (newly constructible from case config, previously
// only "BiCGSTAB" was accepted) and an explicit "GPU" backend both parse
// correctly and independently per linear-solver block.
TEST(CaseReaderTest, CgTypeAndGpuBackendParseCorrectly) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "SIMPLE",
    "max_iterations": 1000,
    "velocity_relaxation": 0.7,
    "pressure_relaxation": 0.3,
    "velocity_tolerance": 1e-6,
    "pressure_tolerance": 1e-6,
    "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "CG", "backend": "GPU", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");

  const auto definition = CaseReader{}.read(fixture.directory());

  EXPECT_EQ(definition.solver.momentumSolver.type, "CG");
  EXPECT_EQ(definition.solver.momentumSolver.backend, "GPU");
  // The pressure block independently keeps its own type/backend --
  // proves these are per-block, not a single case-wide setting.
  EXPECT_EQ(definition.solver.pressureSolver.type, "BiCGSTAB");
  EXPECT_EQ(definition.solver.pressureSolver.backend, "CPU");
}

TEST(CaseReaderTest, ValidCaseParsesIntoExpectedConfiguration) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());

  EXPECT_EQ(definition.caseConfig.name, "Test Case");
  EXPECT_EQ(definition.caseConfig.formatVersion, 1);

  EXPECT_EQ(definition.geometry.type, "rectangle");
  EXPECT_DOUBLE_EQ(definition.geometry.length, 1.0);
  EXPECT_DOUBLE_EQ(definition.geometry.height, 1.0);

  EXPECT_EQ(definition.mesh.type, "structured_cartesian");
  EXPECT_EQ(definition.mesh.nx, 4u);
  EXPECT_EQ(definition.mesh.ny, 4u);

  EXPECT_EQ(definition.physics.model, "incompressible_laminar");
  EXPECT_DOUBLE_EQ(definition.physics.density, 1.0);
  EXPECT_DOUBLE_EQ(definition.physics.dynamicViscosity, 0.01);
  EXPECT_FALSE(definition.physics.reynoldsNumber.has_value());

  ASSERT_EQ(definition.boundaries.patches.size(), 4u);
  EXPECT_EQ(definition.boundaries.patches.at("top").velocity.type, "moving_wall");
  EXPECT_DOUBLE_EQ(definition.boundaries.patches.at("top").velocity.value.x, 1.0);
  EXPECT_EQ(definition.boundaries.patches.at("left").velocity.type, "wall");
  EXPECT_EQ(definition.boundaries.patches.at("top").pressure.type, "fixed_gradient");

  EXPECT_EQ(definition.solver.type, "SIMPLE");
  EXPECT_EQ(definition.solver.maxIterations, 1000u);
  EXPECT_DOUBLE_EQ(definition.solver.velocityRelaxation, 0.7);
  EXPECT_EQ(definition.solver.momentumSolver.type, "BiCGSTAB");
  // P6-GPU-002: absent from the fixture's solver.json -- must default to
  // "CPU" so every pre-P6-GPU-002 case file keeps parsing into the exact
  // same (CPU-only) configuration it always did.
  EXPECT_EQ(definition.solver.momentumSolver.backend, "CPU");

  // No "initial_conditions" block in the fixture -- zero default (section
  // 26).
  EXPECT_DOUBLE_EQ(definition.initialConditions.velocity.x, 0.0);
  EXPECT_DOUBLE_EQ(definition.initialConditions.velocity.y, 0.0);
  EXPECT_DOUBLE_EQ(definition.initialConditions.pressure, 0.0);
}

TEST(CaseReaderTest, ExplicitInitialConditionsAreParsed) {
  CaseFixture fixture;
  fixture.write("case.json", R"({
    "name": "Test Case",
    "geometry": "geometry.json", "mesh": "mesh.json", "physics": "physics.json",
    "boundaries": "boundaries.json", "solver": "solver.json",
    "initial_conditions": {"velocity": [0.5, -0.25], "pressure": 1.5}
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_DOUBLE_EQ(definition.initialConditions.velocity.x, 0.5);
  EXPECT_DOUBLE_EQ(definition.initialConditions.velocity.y, -0.25);
  EXPECT_DOUBLE_EQ(definition.initialConditions.pressure, 1.5);
}

TEST(CaseReaderTest, MissingCaseDirectoryThrowsIOError) {
  EXPECT_THROW((void)CaseReader{}.read("this/directory/does/not/exist"), IOError);
}

TEST(CaseReaderTest, MissingManifestThrowsIOError) {
  CaseFixture fixture;
  fixture.remove("case.json");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), IOError);
}

TEST(CaseReaderTest, MissingReferencedFileThrowsIOError) {
  CaseFixture fixture;
  fixture.remove("mesh.json");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), IOError);
}

TEST(CaseReaderTest, MalformedManifestJsonThrowsIOError) {
  CaseFixture fixture;
  fixture.write("case.json", "{ this is not valid JSON");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), IOError);
}

TEST(CaseReaderTest, MalformedReferencedJsonThrowsIOError) {
  CaseFixture fixture;
  fixture.write("physics.json", "{ \"model\": ");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), IOError);
}

TEST(CaseReaderTest, ManifestReferencingFileOutsideCaseDirectoryIsRejected) {
  // TODO.md P1 section 37: case-local configuration only.
  CaseFixture fixture;
  fixture.write("case.json", R"({
    "name": "Test Case",
    "geometry": "../../../etc/passwd", "mesh": "mesh.json", "physics": "physics.json",
    "boundaries": "boundaries.json", "solver": "solver.json"
  })");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(CaseReaderTest, ManifestCanReferenceAlternativeFilenames) {
  // TODO.md P1 section 36: if the manifest advertises file paths, they
  // must actually be honored.
  CaseFixture fixture;
  fixture.write("fine_mesh.json", R"({"type": "structured_cartesian", "nx": 8, "ny": 8})");
  fixture.write("case.json", R"({
    "name": "Test Case",
    "geometry": "geometry.json", "mesh": "fine_mesh.json", "physics": "physics.json",
    "boundaries": "boundaries.json", "solver": "solver.json"
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_EQ(definition.mesh.nx, 8u);
  EXPECT_EQ(definition.mesh.ny, 8u);
}

TEST(CaseReaderTest, UnsupportedFormatVersionThrowsCaseConfigurationError) {
  CaseFixture fixture;
  fixture.write("case.json", R"({
    "name": "Test Case", "format_version": 2,
    "geometry": "geometry.json", "mesh": "mesh.json", "physics": "physics.json",
    "boundaries": "boundaries.json", "solver": "solver.json"
  })");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(CaseReaderTest, UnknownTopLevelFieldInManifestIsRejected) {
  // TODO.md P1 section 23: unknown fields are a hard error (a typo like
  // "denisty" must not look like it was accepted).
  CaseFixture fixture;
  fixture.write("case.json", R"({
    "name": "Test Case", "extra_unexpected_field": true,
    "geometry": "geometry.json", "mesh": "mesh.json", "physics": "physics.json",
    "boundaries": "boundaries.json", "solver": "solver.json"
  })");
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}
