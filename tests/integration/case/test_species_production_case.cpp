// P6-PHYS-001: species-transport production-integration test. Unlike
// tests/integration/species/test_species_diffusion.cpp (which drives
// cfd::species::SpeciesSolver directly, at the equation/solver level),
// this test drives the real production entry point
// (cfd::app::ProjectRunner::run(), the exact function apps/cli/main.cpp
// and apps/gui/SimulationController both call -- see ProjectRunner.hpp's
// own header comment on "ONE SOLVER BACKEND") against a real case
// directory on disk (cases/species_diffusion), confirming
// physics.json/boundaries.json parsing, CaseBuilder wiring, and
// ProjectRunner's species dispatch all work together end to end -- not
// re-deriving the underlying numerics, which test_species_diffusion.cpp
// already covers.
//
// Same closed-form 1D slab solution as test_species_diffusion.cpp:
// Y(x) = Yleft + (Yright-Yleft)*x/L, independent of y -- cases/
// species_diffusion's own boundaries.json/physics.json encode exactly
// Yleft=1.0, Yright=0.0, L=1.0, D=2.0e-3 (test_species_diffusion.cpp's
// own validated Grid20 case), so this test's expected errors/flux values
// are the same known-good numbers, not independently re-derived.
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <sstream>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::app::SpeciesRunResult;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::species::SpeciesStatus;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr Real kLength = 1.0;
constexpr Real kHeight = 0.2;
constexpr Real kYLeft = 1.0;
constexpr Real kYRight = 0.0;
constexpr Real kDiffusivity = 2.0e-3;
constexpr Real kDensity = 1.0;

Real analyticalConcentration(Real x) { return kYLeft + (kYRight - kYLeft) * (x / kLength); }

// gtest_discover_tests below sets WORKING_DIRECTORY to CFDApp_SOURCE_DIR
// (same convention as tests/integration/case/test_case_lid_driven_cavity.cpp),
// so "cases/species_diffusion" resolves against the repository root
// regardless of where the test binary itself lives.
constexpr const char* kCaseDirectory = "cases/species_diffusion";

}  // namespace

TEST(SpeciesProductionCaseTest, RunsEndToEndAndConverges) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  ASSERT_EQ(run.status, ProjectRunStatus::Converged)
      << "flow did not converge: " << run.errorMessage;
  ASSERT_TRUE(run.caseDefinition.has_value());
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_TRUE(run.simpleResult->converged());

  // Quiescent case (every patch is a wall): velocity is identically zero,
  // so the flow solve should converge essentially immediately.
  for (Index i = 0; i < run.simpleResult->velocity.size(); ++i) {
    EXPECT_DOUBLE_EQ(run.simpleResult->velocity[i].x, 0.0);
    EXPECT_DOUBLE_EQ(run.simpleResult->velocity[i].y, 0.0);
  }

  ASSERT_EQ(run.speciesResults.size(), 1u);
  EXPECT_EQ(run.speciesResults[0].name, "CO2");
  EXPECT_EQ(run.speciesResults[0].result.status, SpeciesStatus::Converged);
  EXPECT_TRUE(run.speciesResults[0].result.converged());
}

// The core acceptance requirement: the species field the *production*
// path (CaseReader -> CaseBuilder -> ProjectRunner -> SpeciesSolver)
// produces matches the closed-form analytical profile, the same
// tolerance test_species_diffusion.cpp's own Grid20 case already
// achieves (FVM is exact for an affine field on an orthogonal mesh, so
// this is a tight tolerance, not a loose approximation).
TEST(SpeciesProductionCaseTest, ConcentrationMatchesAnalyticalProfile) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_EQ(run.speciesResults.size(), 1u);
  ASSERT_TRUE(run.mesh.has_value());
  const Mesh& mesh = *run.mesh;
  const ScalarField& concentration = run.speciesResults[0].result.concentration;

  Real sumSquares = 0.0;
  Real lInf = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact = analyticalConcentration(cell.centroid().x);
    const Real error = std::abs(concentration[cell.id()] - exact);
    sumSquares += error * error;
    lInf = std::max(lInf, error);
  }
  const Real l2 = std::sqrt(sumSquares / static_cast<Real>(mesh.numberOfCells()));

  EXPECT_LT(l2, 1e-6);
  EXPECT_LT(lInf, 1e-6);
}

// Conservation (this task's own explicit acceptance criterion, distinct
// from the analytical-profile check above): with zero-flux top/bottom
// and no source, the diffusive flux entering the domain at the left
// (Dirichlet Y=1.0) must equal the flux leaving at the right (Dirichlet
// Y=0.0) at steady state -- computed directly from the converged
// concentration field via Fourier's law against each boundary's known
// prescribed value (the same finite-difference-at-the-boundary technique
// tests/integration/thermal/test_heated_cavity_validation.cpp's own
// patchHeatLeaving() uses, generalized to species: flux = density *
// diffusivity * (Y_owner - Y_boundary) / distance * faceArea).
TEST(SpeciesProductionCaseTest, GlobalDiffusiveFluxBalanceIsConserved) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_EQ(run.speciesResults.size(), 1u);
  ASSERT_TRUE(run.mesh.has_value());
  const Mesh& mesh = *run.mesh;
  const ScalarField& concentration = run.speciesResults[0].result.concentration;

  auto fluxLeavingPatch = [&](const std::string& patchName, Real boundaryValue) {
    Real total = 0.0;
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      const auto& face = mesh.face(faceId);
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      total += kDensity * kDiffusivity * (concentration[ownerId] - boundaryValue) / distance *
               face.area();
    }
    return total;
  };

  const Real leftFluxLeaving = fluxLeavingPatch("left", kYLeft);
  const Real rightFluxLeaving = fluxLeavingPatch("right", kYRight);

  // Known-good analytical value (test_species_diffusion.cpp's own
  // GlobalDiffusiveFluxBalanceIsExact test uses the identical formula):
  // flux = density * D * |Yright - Yleft| / L * height.
  const Real expectedFluxMagnitude =
      kDensity * kDiffusivity * std::abs(kYRight - kYLeft) / kLength * kHeight;
  EXPECT_NEAR(std::abs(leftFluxLeaving), expectedFluxMagnitude, 1e-6);
  EXPECT_NEAR(std::abs(rightFluxLeaving), expectedFluxMagnitude, 1e-6);
  // Steady state, no source: flux entering the hot (left) side (negative
  // "leaving") must balance flux leaving the cold (right) side.
  EXPECT_NEAR(leftFluxLeaving + rightFluxLeaving, 0.0, 1e-6);
}

// Section 8 (P6 task acceptance): "invalid species configuration is
// rejected clearly" -- exercised at the CaseReader layer already by
// tests/unit/io/test_species_case.cpp's own rejection tests; here it is
// the *production* dispatch path (ProjectRunner::run()) that must also
// surface the same failure as InvalidCase, not crash or hang.
TEST(SpeciesProductionCaseTest, MissingCaseDirectoryReportsInvalidCase) {
  const ProjectRunResult run = ProjectRunner::run("this/directory/does/not/exist");
  EXPECT_EQ(run.status, ProjectRunStatus::InvalidCase);
  EXPECT_TRUE(run.speciesResults.empty());
}

// Section 7 (P6 task acceptance): "species field is exported" -- CSV/
// VTK/JSON, confirmed against the real files ProjectRunner/
// ResultExporter actually wrote for this case (not a synthetic
// ResultExporter::write() call the way tests/unit/io/test_csv_writer.cpp
// etc. already cover the writers themselves in isolation).
TEST(SpeciesProductionCaseTest, ExportsSpeciesFieldToCsvVtkAndJson) {
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
    EXPECT_NE(header.find("concentration_CO2"), std::string::npos) << header;
  }
  {
    std::ifstream vtk(*run.exportSummary->vtkPath);
    ASSERT_TRUE(vtk.is_open());
    const std::string content((std::istreambuf_iterator<char>(vtk)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("SCALARS concentration_CO2"), std::string::npos);
  }
  {
    std::ifstream json(run.exportSummary->metadataPath);
    ASSERT_TRUE(json.is_open());
    const std::string content((std::istreambuf_iterator<char>(json)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"CO2\""), std::string::npos);
    EXPECT_NE(content.find("\"Converged\""), std::string::npos);
  }
}

// Section 10 (P6 task acceptance): "existing cases remain unchanged" --
// the small nonspecies fixture (tests/data/cases/valid_cavity, already
// exercised by tests/unit/app/test_project_runner.cpp) still runs
// through the exact same ProjectRunner::run() entry point with an empty
// speciesResults, never a species-shaped export artifact leaking into a
// nonspecies case's output.
TEST(SpeciesProductionCaseTest, NonSpeciesCaseStillRunsWithEmptySpeciesResults) {
  // P7-TEST-001: a private copy -- see CaseFixtureCopy.hpp's own header
  // comment.
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  EXPECT_TRUE(run.speciesResults.empty());
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  std::ifstream csv(*run.exportSummary->fieldsCsvPath);
  std::string header;
  std::getline(csv, header);
  EXPECT_EQ(header.find("concentration_"), std::string::npos) << header;
}

// Determinism (this project's own standing requirement for every
// production solve path -- see e.g. test_project_runner.cpp's own
// RepeatedRunsAreDeterministic).
TEST(SpeciesProductionCaseTest, RepeatedRunsAreDeterministic) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult first = ProjectRunner::run(fixture.path());
  const ProjectRunResult second = ProjectRunner::run(fixture.path());
  ASSERT_EQ(first.speciesResults.size(), 1u);
  ASSERT_EQ(second.speciesResults.size(), 1u);
  EXPECT_EQ(first.speciesResults[0].result.iterations, second.speciesResults[0].result.iterations);
  for (Index i = 0; i < first.speciesResults[0].result.concentration.size(); ++i) {
    EXPECT_EQ(first.speciesResults[0].result.concentration[i],
              second.speciesResults[0].result.concentration[i]);
  }
}
