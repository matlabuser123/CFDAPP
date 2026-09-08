// P1 -- Case System, section 34: per-file content validation. Each test
// overwrites exactly one sub-file with an otherwise-valid case (see
// CaseFixture) to isolate the one constraint under test.
#include <gtest/gtest.h>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {
void expectRejected(const CaseFixture& fixture) {
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}
}  // namespace

// --- geometry.json -----------------------------------------------------

TEST(CaseValidationTest, UnsupportedGeometryTypeIsRejected) {
  CaseFixture fixture;
  fixture.write("geometry.json", R"({"type": "circle", "length": 1.0, "height": 1.0})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, NonPositiveGeometryLengthIsRejected) {
  CaseFixture fixture;
  fixture.write("geometry.json", R"({"type": "rectangle", "length": 0.0, "height": 1.0})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, NegativeGeometryHeightIsRejected) {
  CaseFixture fixture;
  fixture.write("geometry.json", R"({"type": "rectangle", "length": 1.0, "height": -2.0})");
  expectRejected(fixture);
}

// --- mesh.json -----------------------------------------------------------

TEST(CaseValidationTest, UnsupportedMeshTypeIsRejected) {
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "unstructured", "nx": 4, "ny": 4})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, ZeroNxIsRejected) {
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "structured_cartesian", "nx": 0, "ny": 4})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, FractionalNxIsRejected) {
  // TODO.md P1 section 40: reject 20.5 for an integer grid dimension
  // rather than silently truncating it.
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "structured_cartesian", "nx": 20.5, "ny": 4})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, StringNxIsRejected) {
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "structured_cartesian", "nx": "20", "ny": 4})");
  expectRejected(fixture);
}

// --- physics.json --------------------------------------------------------

TEST(CaseValidationTest, NonPositiveDensityIsRejected) {
  CaseFixture fixture;
  fixture.write(
      "physics.json",
      R"({"model": "incompressible_laminar", "density": -1.0, "dynamic_viscosity": 0.01})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, NonPositiveViscosityIsRejected) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.0})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, UnsupportedPhysicsModelIsRejected) {
  CaseFixture fixture;
  fixture.write(
      "physics.json",
      R"({"model": "compressible_turbulent", "density": 1.0, "dynamic_viscosity": 0.01})");
  expectRejected(fixture);
}

TEST(CaseValidationTest, UnknownPhysicsFieldIsRejected) {
  CaseFixture fixture;
  fixture.write(
      "physics.json",
      R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01, "denisty": 1.0})");
  expectRejected(fixture);
}

// --- boundaries.json -------------------------------------------------------

TEST(CaseValidationTest, UnknownBoundaryPatchIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "diagonal": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, MissingBoundaryPatchIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, UnsupportedVelocityBoundaryTypeIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "periodic"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, MovingWallWithoutVelocityValueIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall"},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, WallWithUnexpectedVelocityValueIsRejected) {
  // A "value" on a BC type that doesn't take one is a likely typo/
  // misunderstanding, not silently ignored (section 12/23).
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, VelocityValueWithWrongComponentCountIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0, 4.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, UnsupportedPressureBoundaryTypeIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "adiabatic"}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, MissingPressureBlockIsRejected) {
  CaseFixture fixture;
  fixture.write("boundaries.json", R"({
    "patches": {
      "left":   {"velocity": {"type": "wall"}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}
    }
  })");
  expectRejected(fixture);
}

// --- solver.json -----------------------------------------------------------

TEST(CaseValidationTest, UnsupportedSolverTypeIsRejected) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "PISO", "max_iterations": 100,
    "velocity_relaxation": 0.7, "pressure_relaxation": 0.3,
    "velocity_tolerance": 1e-6, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, RelaxationAboveOneIsRejected) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "SIMPLE", "max_iterations": 100,
    "velocity_relaxation": 0.7, "pressure_relaxation": 1.5,
    "velocity_tolerance": 1e-6, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, ZeroRelaxationIsRejected) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "SIMPLE", "max_iterations": 100,
    "velocity_relaxation": 0.0, "pressure_relaxation": 0.3,
    "velocity_tolerance": 1e-6, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, NonPositiveToleranceIsRejected) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "SIMPLE", "max_iterations": 100,
    "velocity_relaxation": 0.7, "pressure_relaxation": 0.3,
    "velocity_tolerance": 0.0, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, ZeroMaxIterationsIsRejected) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "SIMPLE", "max_iterations": 0,
    "velocity_relaxation": 0.7, "pressure_relaxation": 0.3,
    "velocity_tolerance": 1e-6, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");
  expectRejected(fixture);
}

TEST(CaseValidationTest, UnsupportedLinearSolverTypeIsRejected) {
  CaseFixture fixture;
  fixture.write("solver.json", R"({
    "type": "SIMPLE", "max_iterations": 100,
    "velocity_relaxation": 0.7, "pressure_relaxation": 0.3,
    "velocity_tolerance": 1e-6, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
    "momentum_linear_solver": {"type": "GMRES", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                "relative_tolerance": 1e-8, "max_iterations": 2000}
  })");
  expectRejected(fixture);
}
