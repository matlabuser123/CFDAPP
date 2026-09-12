// P10-APP-004: the first production case combining two advanced-physics
// modules at once (thermal + species), following exactly the same
// "drive the real production entry point against a real case directory"
// pattern as test_species_production_case.cpp/test_multiphase_production_case.cpp/
// test_compressible_production_case.cpp -- this is that same pattern's own
// combined-physics regression test.
//
// cases/heated_species_diffusion is quiescent (every patch is a wall), same
// 20x4 slab geometry as cases/species_diffusion, with both cases/heated_cavity's
// own thermal values and cases/species_diffusion's own species values enabled
// together. Thermal and species do not couple to each other or to momentum in
// this codebase (no buoyancy block here), so the two fields are independent,
// closed-form 1D linear profiles superimposed in one domain -- confirming that
// combining physics modules that this project's own compatibility matrix
// (P10-APP-004, PhysicsConfigParser.cpp's validatePhysicsCompatibility) allows
// together actually produces the numerically correct, independently-known-good
// result for each, not just that CaseBuilder/ProjectRunner accept the
// combination without crashing (test_physics_compatibility.cpp already covers
// that acceptance/parsing level).
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::species::SpeciesStatus;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr Real kLength = 1.0;
constexpr Real kHeight = 0.2;

// Same closed-form 1D slab solutions as test_heated_cavity_validation.cpp's
// analyticalTemperature() and test_species_diffusion.cpp's own species
// profile -- cases/heated_species_diffusion's own physics.json/
// boundaries.json encode exactly these same left/right values, so these
// expected numbers are the same known-good ones, not independently
// re-derived.
constexpr Real kTHot = 310.0;
constexpr Real kTCold = 290.0;
constexpr Real kConductivity = 0.6;

constexpr Real kYLeft = 1.0;
constexpr Real kYRight = 0.0;
constexpr Real kDiffusivity = 2.0e-3;
constexpr Real kDensity = 1.0;

Real analyticalTemperature(Real x) { return kTHot + (kTCold - kTHot) * (x / kLength); }
Real analyticalConcentration(Real x) { return kYLeft + (kYRight - kYLeft) * (x / kLength); }

// gtest_discover_tests sets WORKING_DIRECTORY to CFDApp_SOURCE_DIR (same
// convention as every other test in this directory), so this resolves
// against the repository root regardless of where the test binary lives.
constexpr const char* kCaseDirectory = "cases/heated_species_diffusion";

}  // namespace

TEST(HeatedSpeciesDiffusionProductionCaseTest, RunsEndToEndAndConverges) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << "did not converge: " << run.errorMessage;
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_TRUE(run.simpleResult->converged());

  // Quiescent case (every patch is a wall): velocity is identically zero.
  for (Index i = 0; i < run.simpleResult->velocity.size(); ++i) {
    EXPECT_DOUBLE_EQ(run.simpleResult->velocity[i].x, 0.0);
    EXPECT_DOUBLE_EQ(run.simpleResult->velocity[i].y, 0.0);
  }

  ASSERT_TRUE(run.thermalResult.has_value());
  EXPECT_TRUE(run.thermalResult->converged());

  ASSERT_EQ(run.speciesResults.size(), 1u);
  EXPECT_EQ(run.speciesResults[0].name, "CO2");
  EXPECT_TRUE(run.speciesResults[0].result.converged());
}

// The core acceptance requirement: both fields the production path
// produces match their own closed-form analytical profiles simultaneously
// -- neither module's presence perturbed the other's result (same tight
// FVM-is-exact-for-an-affine-field tolerance as the two single-physics
// cases this combines).
TEST(HeatedSpeciesDiffusionProductionCaseTest, BothFieldsMatchAnalyticalProfilesSimultaneously) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.thermalResult.has_value());
  ASSERT_EQ(run.speciesResults.size(), 1u);
  ASSERT_TRUE(run.mesh.has_value());
  const Mesh& mesh = *run.mesh;
  const ScalarField& temperature = run.thermalResult->temperature;
  const ScalarField& concentration = run.speciesResults[0].result.concentration;

  Real temperatureLInf = 0.0;
  Real concentrationLInf = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    temperatureLInf =
        std::max(temperatureLInf, std::abs(temperature[cell.id()] - analyticalTemperature(x)));
    concentrationLInf = std::max(
        concentrationLInf, std::abs(concentration[cell.id()] - analyticalConcentration(x)));
  }

  EXPECT_LT(temperatureLInf, 1e-6);
  EXPECT_LT(concentrationLInf, 1e-6);
}

// Conservation for both fields at once, using the exact same
// flux-leaving-a-patch technique as test_heated_cavity_validation.cpp's own
// patchHeatLeaving() (Fourier's law) and
// test_species_production_case.cpp's own fluxLeavingPatch() (Fick's law) --
// confirms combining the two modules didn't silently break either one's own
// steady-state conservation balance.
TEST(HeatedSpeciesDiffusionProductionCaseTest, BothFieldsConserveFluxIndependently) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.thermalResult.has_value());
  ASSERT_EQ(run.speciesResults.size(), 1u);
  ASSERT_TRUE(run.mesh.has_value());
  const Mesh& mesh = *run.mesh;

  auto heatLeavingPatch = [&](const std::string& patchName, Real boundaryValue) {
    Real total = 0.0;
    const ScalarField& temperature = run.thermalResult->temperature;
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      const auto& face = mesh.face(faceId);
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      total += kConductivity * face.area() * (temperature[ownerId] - boundaryValue) / distance;
    }
    return total;
  };
  auto speciesFluxLeavingPatch = [&](const std::string& patchName, Real boundaryValue) {
    Real total = 0.0;
    const ScalarField& concentration = run.speciesResults[0].result.concentration;
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      const auto& face = mesh.face(faceId);
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      total += kDensity * kDiffusivity * (concentration[ownerId] - boundaryValue) / distance *
               face.area();
    }
    return total;
  };

  const Real hotWallHeatLeaving = heatLeavingPatch("left", kTHot);
  const Real coldWallHeatLeaving = heatLeavingPatch("right", kTCold);
  const Real expectedHeatMagnitude = kConductivity * std::abs(kTCold - kTHot) / kLength * kHeight;
  EXPECT_NEAR(std::abs(hotWallHeatLeaving), expectedHeatMagnitude, 1e-6);
  EXPECT_NEAR(hotWallHeatLeaving + coldWallHeatLeaving, 0.0, 1e-6);

  const Real leftFluxLeaving = speciesFluxLeavingPatch("left", kYLeft);
  const Real rightFluxLeaving = speciesFluxLeavingPatch("right", kYRight);
  const Real expectedFluxMagnitude =
      kDensity * kDiffusivity * std::abs(kYRight - kYLeft) / kLength * kHeight;
  EXPECT_NEAR(std::abs(leftFluxLeaving), expectedFluxMagnitude, 1e-6);
  EXPECT_NEAR(leftFluxLeaving + rightFluxLeaving, 0.0, 1e-6);
}

TEST(HeatedSpeciesDiffusionProductionCaseTest, ExportsBothFieldsToCsvVtkAndJson) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  ASSERT_TRUE(run.exportSummary->vtkPath.has_value());

  {
    std::ifstream csv(*run.exportSummary->fieldsCsvPath);
    ASSERT_TRUE(csv.is_open());
    std::string header;
    std::getline(csv, header);
    EXPECT_NE(header.find("temperature"), std::string::npos) << header;
    EXPECT_NE(header.find("concentration_CO2"), std::string::npos) << header;
  }
  {
    std::ifstream vtk(*run.exportSummary->vtkPath);
    ASSERT_TRUE(vtk.is_open());
    const std::string content((std::istreambuf_iterator<char>(vtk)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("SCALARS temperature"), std::string::npos);
    EXPECT_NE(content.find("SCALARS concentration_CO2"), std::string::npos);
  }
  {
    std::ifstream json(run.exportSummary->metadataPath);
    ASSERT_TRUE(json.is_open());
    const std::string content((std::istreambuf_iterator<char>(json)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"thermal\""), std::string::npos);
    EXPECT_NE(content.find("\"CO2\""), std::string::npos);
  }
}

TEST(HeatedSpeciesDiffusionProductionCaseTest, RepeatedRunsAreDeterministic) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult first = ProjectRunner::run(fixture.path());
  const ProjectRunResult second = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(first.thermalResult.has_value());
  ASSERT_TRUE(second.thermalResult.has_value());
  ASSERT_EQ(first.speciesResults.size(), 1u);
  ASSERT_EQ(second.speciesResults.size(), 1u);

  EXPECT_EQ(first.thermalResult->iterations, second.thermalResult->iterations);
  EXPECT_EQ(first.speciesResults[0].result.iterations, second.speciesResults[0].result.iterations);
  for (Index i = 0; i < first.thermalResult->temperature.size(); ++i) {
    EXPECT_EQ(first.thermalResult->temperature[i], second.thermalResult->temperature[i]);
  }
  for (Index i = 0; i < first.speciesResults[0].result.concentration.size(); ++i) {
    EXPECT_EQ(first.speciesResults[0].result.concentration[i],
              second.speciesResults[0].result.concentration[i]);
  }
}
