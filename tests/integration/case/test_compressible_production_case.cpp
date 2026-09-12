// P6-PHYS-003: compressible production-integration test -- drives the
// real production entry point (cfd::app::ProjectRunner::run()) against a
// real case directory (cases/compressible_validation), confirming
// physics.json parsing, CaseBuilder wiring, and ProjectRunner's post-hoc
// low-Mach dispatch all work together end to end. Same physical
// reference constants and expected outcomes as
// tests/integration/compressible/test_low_mach_regression.cpp's own
// validated Grid24 case -- this test does not re-derive the underlying
// EOS/mass-flux/continuity numerics (that file already covers them at
// the equation level against the *identical* recipe), only that the
// production dispatch path reproduces the same real, physical result
// from a real case file.
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Vector2.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::compressible::ThermodynamicProperties;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr Real kGasConstant = 287.05;
constexpr Real kTemperature = 300.0;
constexpr Real kReferencePressure = 101325.0;

// gtest_discover_tests sets WORKING_DIRECTORY to CFDApp_SOURCE_DIR (same
// convention as test_species_production_case.cpp), so this relative
// path resolves against the repository root.
constexpr const char* kCaseDirectory = "cases/compressible_validation";

}  // namespace

TEST(CompressibleProductionCaseTest, RunsEndToEndAndConverges) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged)
      << "flow did not converge: " << run.errorMessage;
  ASSERT_TRUE(run.compressibleResult.has_value());
}

// The core acceptance requirement: real JSON case runs end to end and
// the low-Mach assumption genuinely holds -- same thresholds
// test_low_mach_regression.cpp's own Mach/EOS-consistency tests use.
TEST(CompressibleProductionCaseTest, MachNumberStaysWellBelowPointOneAndEosIsConsistent) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.compressibleResult.has_value());
  const auto& c = *run.compressibleResult;

  EXPECT_GT(c.machMax, 0.0);
  EXPECT_LT(c.machMax, 0.1);

  Real maxEosError = 0.0;
  for (Index i = 0; i < c.density.size(); ++i) {
    EXPECT_TRUE(std::isfinite(c.density[i])) << "cell " << i;
    EXPECT_GT(c.density[i], 0.0) << "cell " << i;
    const Real directFormula = c.pressureAbsolute[i] / (kGasConstant * kTemperature);
    maxEosError = std::max(maxEosError, std::abs(c.density[i] - directFormula));
    EXPECT_DOUBLE_EQ(c.temperature[i], kTemperature);
  }
  EXPECT_LT(maxEosError, 1e-9);
}

// Conservation (this task's own explicit acceptance criterion): the
// steady continuity imbalance from the compressible mass flux must stay
// small, same threshold test_low_mach_regression.cpp's own
// GlobalMassImbalanceIsSmall test uses (normalized the same way -- a raw
// imbalance is reported here; the normalized check mirrors that file's
// own reasoning without duplicating its exact private helper).
TEST(CompressibleProductionCaseTest, ContinuityImbalanceIsSmall) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.compressibleResult.has_value());
  // Reference mass-flow scale: rho * meanVelocity * height (same
  // normalization test_low_mach_regression.cpp's own fluxScale uses),
  // using the case's own reference density (~1.176 kg/m^3 at 300K/1atm)
  // and unit inlet velocity/height.
  const Real fluxScale = 1.176 * 1.0 * 1.0;
  EXPECT_LT(std::abs(run.compressibleResult->continuity.globalNetFlux) / fluxScale, 1e-3);
}

// P12-COMP-001: proves the new boundary-density treatment is actually
// exercised by the *production* dispatch path (ProjectRunner::run()),
// not just the equation-level function it's built from
// (test_compressible_mass_flux.cpp already covers that). This case's own
// "right" patch pairs an Outlet velocity BC with a Dirichlet
// (fixed_value=0.0) gauge-pressure BC (boundaries.json) -- so the
// outlet's absolute pressure is *exactly* kReferencePressure regardless
// of the interior's own (numerically slightly different) gauge pressure,
// meaning the outlet boundary density must be the EOS value at exactly
// kReferencePressure/kTemperature, not whatever the owner cell's own
// (interior) density happens to be -- which is exactly what the
// pre-P12-COMP-001 owner-cell-reuse simplification would have produced
// instead.
TEST(CompressibleProductionCaseTest, OutletBoundaryMassFluxUsesReferencePressureDensity) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.compressibleResult.has_value());
  ASSERT_TRUE(run.mesh.has_value());
  ASSERT_TRUE(run.simpleResult.has_value());
  const auto& mesh = *run.mesh;
  const auto& c = *run.compressibleResult;

  const ThermodynamicProperties thermo(kGasConstant, 1005.0);
  const Real expectedOutletDensity = thermo.density(kReferencePressure, kTemperature);

  const Index outletFaceId = mesh.boundaryPatch("right").faceIds().front();
  const auto& face = mesh.face(outletFaceId);
  const Vector2 ownerVelocity = run.simpleResult->velocity[face.owner()];
  const Real expectedFlux = expectedOutletDensity * dot(ownerVelocity, face.areaVector());

  ASSERT_EQ(c.massFlux.size(), mesh.numberOfFaces());
  EXPECT_NEAR(c.massFlux[outletFaceId], expectedFlux, 1e-6);

  // Confirms this genuinely differs from what the superseded owner-cell
  // approximation would have given (the interior cell's own density is
  // not exactly the reference-pressure EOS value, since its own gauge
  // pressure is not exactly 0 -- only the Dirichlet-BC outlet face is).
  const Real ownerCellDensity = c.density[face.owner()];
  EXPECT_NE(ownerCellDensity, expectedOutletDensity);
}

TEST(CompressibleProductionCaseTest, ExportsCompressibleFieldsToCsvVtkAndJson) {
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
    EXPECT_NE(header.find(",density"), std::string::npos) << header;
    EXPECT_NE(header.find("pressure_absolute"), std::string::npos) << header;
    EXPECT_NE(header.find("compressible_temperature"), std::string::npos) << header;
    EXPECT_NE(header.find("mach_number"), std::string::npos) << header;
  }
  {
    std::ifstream vtk(*run.exportSummary->vtkPath);
    ASSERT_TRUE(vtk.is_open());
    const std::string content((std::istreambuf_iterator<char>(vtk)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("SCALARS density"), std::string::npos);
    EXPECT_NE(content.find("SCALARS mach_number"), std::string::npos);
  }
  {
    std::ifstream json(run.exportSummary->metadataPath);
    ASSERT_TRUE(json.is_open());
    const std::string content((std::istreambuf_iterator<char>(json)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"Evaluated\""), std::string::npos);
    EXPECT_NE(content.find(std::to_string(kReferencePressure).substr(0, 6)), std::string::npos);
  }
}

TEST(CompressibleProductionCaseTest, MissingCaseDirectoryReportsInvalidCase) {
  const ProjectRunResult run = ProjectRunner::run("this/directory/does/not/exist");
  EXPECT_EQ(run.status, ProjectRunStatus::InvalidCase);
  EXPECT_FALSE(run.compressibleResult.has_value());
}

// Existing incompressible cases remain unaffected.
TEST(CompressibleProductionCaseTest, NonCompressibleCaseStillRunsWithNoCompressibleResult) {
  // P7-TEST-001: a private copy -- see CaseFixtureCopy.hpp's own header
  // comment; "tests/data/cases/valid_cavity" is shared by several other
  // test binaries this one can run concurrently against under `ctest
  // -j8`, and this test writes into its results/ subtree.
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  EXPECT_FALSE(run.compressibleResult.has_value());
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  std::ifstream csv(*run.exportSummary->fieldsCsvPath);
  std::string header;
  std::getline(csv, header);
  EXPECT_EQ(header.find("mach_number"), std::string::npos) << header;
}

TEST(CompressibleProductionCaseTest, RepeatedRunsAreDeterministic) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult first = ProjectRunner::run(fixture.path());
  const ProjectRunResult second = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(first.compressibleResult.has_value());
  ASSERT_TRUE(second.compressibleResult.has_value());
  EXPECT_EQ(first.compressibleResult->machMax, second.compressibleResult->machMax);
  for (Index i = 0; i < first.compressibleResult->density.size(); ++i) {
    EXPECT_EQ(first.compressibleResult->density[i], second.compressibleResult->density[i]);
  }
}
