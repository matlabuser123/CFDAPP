// P1 -- Case System, section 28/48: CaseBuilder converts a CaseDefinition
// into the correct runtime objects, and a case-driven setup must be
// equivalent to (and, once solved, bit-identical with) an equivalent
// direct C++ setup -- proof that the case layer never alters the
// numerical problem, only how it gets described.
#include <gtest/gtest.h>

#include <memory>

#include "CaseFixture.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::testutil::CaseFixture;

TEST(CaseBuilderTest, BuildsExpectedMeshFluidAndInitialFields) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  EXPECT_EQ(setup.mesh.numberOfCells(), 16u);  // 4x4.
  EXPECT_DOUBLE_EQ(setup.fluid.density(), 1.0);
  EXPECT_DOUBLE_EQ(setup.fluid.dynamicViscosity(), 0.01);

  ASSERT_EQ(setup.initialVelocity.size(), setup.mesh.numberOfCells());
  for (Index i = 0; i < setup.initialVelocity.size(); ++i) {
    EXPECT_DOUBLE_EQ(setup.initialVelocity[i].x, 0.0);
    EXPECT_DOUBLE_EQ(setup.initialVelocity[i].y, 0.0);
  }
  for (Index i = 0; i < setup.initialPressure.size(); ++i) {
    EXPECT_DOUBLE_EQ(setup.initialPressure[i], 0.0);
  }

  EXPECT_EQ(setup.solverSettings.maxIterations, 1000u);
  EXPECT_DOUBLE_EQ(setup.solverSettings.velocityRelaxation, 0.7);
  EXPECT_DOUBLE_EQ(setup.solverSettings.pressureRelaxation, 0.3);
}

TEST(CaseBuilderTest, BuildsExpectedBoundaryConditionTypes) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  EXPECT_EQ(setup.velocityBoundaries.get("left").type(), BoundaryConditionType::Wall);
  EXPECT_EQ(setup.velocityBoundaries.get("top").type(), BoundaryConditionType::MovingWall);
  EXPECT_EQ(setup.pressureBoundaries.get("left").type(), BoundaryConditionType::FixedGradient);

  const auto& topWall = static_cast<const MovingWall&>(setup.velocityBoundaries.get("top"));
  EXPECT_DOUBLE_EQ(topWall.velocity().x, 1.0);
  EXPECT_DOUBLE_EQ(topWall.velocity().y, 0.0);
}

namespace {

// The exact direct C++ setup CaseFixture's default case.json/*.json
// describe -- kept in lock-step with CaseFixture.hpp by hand, same
// convention tests/integration/cavity/test_cavity_ghia.cpp uses for its
// own boundary-condition builders.
BoundaryConditionSet manualVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet manualPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

// TODO.md P1 section 28: manual vs case-driven setup must produce
// equivalent runtime objects -- checked here at the configuration level
// (geometry, cell count, fluid properties, solver settings); section 48
// then additionally requires solving both and comparing results.
TEST(CaseBuilderTest, ConfigurationMatchesManualSetup) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto caseSetup = CaseBuilder{}.build(definition);

  const Mesh manualMesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties manualFluid(1.0, 0.01);

  EXPECT_EQ(caseSetup.mesh.numberOfCells(), manualMesh.numberOfCells());
  EXPECT_EQ(caseSetup.mesh.numberOfFaces(), manualMesh.numberOfFaces());
  for (Index i = 0; i < manualMesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(caseSetup.mesh.cell(i).centroid().x, manualMesh.cell(i).centroid().x);
    EXPECT_DOUBLE_EQ(caseSetup.mesh.cell(i).centroid().y, manualMesh.cell(i).centroid().y);
    EXPECT_DOUBLE_EQ(caseSetup.mesh.cell(i).volume(), manualMesh.cell(i).volume());
  }
  EXPECT_DOUBLE_EQ(caseSetup.fluid.density(), manualFluid.density());
  EXPECT_DOUBLE_EQ(caseSetup.fluid.dynamicViscosity(), manualFluid.dynamicViscosity());
}

// TODO.md P1 section 48: solve both a manually-built setup and the
// case-driven one from identical initial conditions and require
// bit-identical results -- proof the case layer did not alter the
// numerical problem it describes.
TEST(CaseBuilderTest, SolvedResultMatchesManualSetupBitForBit) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto caseSetup = CaseBuilder{}.build(definition);

  const Mesh manualMesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto manualVelocityBC = manualVelocityBoundaries(manualMesh);
  const auto manualPressureBC = manualPressureBoundaries(manualMesh);
  const FluidProperties manualFluid(1.0, 0.01);
  const VectorField initialVelocity(manualMesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(manualMesh.numberOfCells(), 0.0);

  const SIMPLE caseSimple(caseSetup.solverSettings, /*referenceCell=*/0);
  const SIMPLE manualSimple(caseSetup.solverSettings, /*referenceCell=*/0);

  const SIMPLEResult caseResult = caseSimple.solve(
      caseSetup.mesh, caseSetup.fluid, caseSetup.velocityBoundaries, caseSetup.pressureBoundaries,
      caseSetup.initialVelocity, caseSetup.initialPressure);
  const SIMPLEResult manualResult =
      manualSimple.solve(manualMesh, manualFluid, manualVelocityBC, manualPressureBC,
                         initialVelocity, initialPressure);

  ASSERT_EQ(caseResult.status, SIMPLEStatus::Converged);
  ASSERT_EQ(caseResult.status, manualResult.status);
  ASSERT_EQ(caseResult.iterations, manualResult.iterations);
  ASSERT_EQ(caseResult.velocity.size(), manualResult.velocity.size());
  for (Index i = 0; i < caseResult.velocity.size(); ++i) {
    EXPECT_EQ(caseResult.velocity[i].x, manualResult.velocity[i].x);
    EXPECT_EQ(caseResult.velocity[i].y, manualResult.velocity[i].y);
  }
  for (Index i = 0; i < caseResult.pressure.size(); ++i) {
    EXPECT_EQ(caseResult.pressure[i], manualResult.pressure[i]);
  }
  for (Index i = 0; i < caseResult.massFlux.size(); ++i) {
    EXPECT_EQ(caseResult.massFlux[i], manualResult.massFlux[i]);
  }
}
