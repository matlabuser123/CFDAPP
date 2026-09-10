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

#include "cfd/app/ProjectRunner.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;

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
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
  ASSERT_EQ(run.status, ProjectRunStatus::Converged)
      << "flow did not converge: " << run.errorMessage;
  ASSERT_TRUE(run.compressibleResult.has_value());
}

// The core acceptance requirement: real JSON case runs end to end and
// the low-Mach assumption genuinely holds -- same thresholds
// test_low_mach_regression.cpp's own Mach/EOS-consistency tests use.
TEST(CompressibleProductionCaseTest, MachNumberStaysWellBelowPointOneAndEosIsConsistent) {
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
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
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.compressibleResult.has_value());
  // Reference mass-flow scale: rho * meanVelocity * height (same
  // normalization test_low_mach_regression.cpp's own fluxScale uses),
  // using the case's own reference density (~1.176 kg/m^3 at 300K/1atm)
  // and unit inlet velocity/height.
  const Real fluxScale = 1.176 * 1.0 * 1.0;
  EXPECT_LT(std::abs(run.compressibleResult->continuity.globalNetFlux) / fluxScale, 1e-3);
}

TEST(CompressibleProductionCaseTest, ExportsCompressibleFieldsToCsvVtkAndJson) {
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
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
  const ProjectRunResult run = ProjectRunner::run("tests/data/cases/valid_cavity");
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
  const ProjectRunResult first = ProjectRunner::run(kCaseDirectory);
  const ProjectRunResult second = ProjectRunner::run(kCaseDirectory);
  ASSERT_TRUE(first.compressibleResult.has_value());
  ASSERT_TRUE(second.compressibleResult.has_value());
  EXPECT_EQ(first.compressibleResult->machMax, second.compressibleResult->machMax);
  for (Index i = 0; i < first.compressibleResult->density.size(); ++i) {
    EXPECT_EQ(first.compressibleResult->density[i], second.compressibleResult->density[i]);
  }
}
