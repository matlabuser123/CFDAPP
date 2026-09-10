// P6-PHYS-002: multiphase production-integration test -- drives the real
// production entry point (cfd::app::ProjectRunner::run()) against a real
// case directory (cases/multiphase_validation), the same "production
// path, not the underlying numerics" split
// test_species_production_case.cpp already establishes (that file's own
// equation-level counterpart is
// tests/integration/multiphase/test_uniform_alpha.cpp).
//
// cases/multiphase_validation is quiescent (every patch is a wall) with
// a uniform initial alpha=0.5 and Dirichlet alpha boundaries matching
// that same value pattern -- with massFlux == 0 everywhere, the volume-
// fraction convection contribution is identically zero (no diffusion
// term exists -- VolumeFractionEquation.hpp's own explicit scope), so
// the single VolumeFractionSolver step is an *exact* no-op: alpha stays
// uniformly 0.5, and mixture density/viscosity are the exact linear-
// mixture-law midpoint of the two phases. This gives exact, hand-
// checkable expected values, not just "small error" bounds.
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

#include "cfd/app/ProjectRunner.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::multiphase::VolumeFractionStatus;

namespace {

constexpr Real kPhase1Density = 1000.0;
constexpr Real kPhase1Viscosity = 0.001;
constexpr Real kPhase2Density = 1.0;
constexpr Real kPhase2Viscosity = 1.8e-5;
constexpr Real kInitialAlpha = 0.5;

// gtest_discover_tests sets WORKING_DIRECTORY to CFDApp_SOURCE_DIR (same
// convention as test_species_production_case.cpp), so this relative
// path resolves against the repository root.
constexpr const char* kCaseDirectory = "cases/multiphase_validation";

}  // namespace

TEST(MultiphaseProductionCaseTest, RunsEndToEndAndConverges) {
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
  ASSERT_EQ(run.status, ProjectRunStatus::Converged)
      << "flow did not converge: " << run.errorMessage;
  ASSERT_TRUE(run.multiphaseResult.has_value());
  EXPECT_EQ(run.multiphaseResult->alphaStep.status, VolumeFractionStatus::Converged);
  EXPECT_TRUE(run.multiphaseResult->alphaStep.converged());
}

// The core acceptance requirement: mixture properties are constructed
// correctly and alpha transport runs through the production case path,
// with an exact (not just bounded) expected outcome for this quiescent
// case.
TEST(MultiphaseProductionCaseTest, AlphaAndMixturePropertiesMatchExactExpectedValues) {
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.multiphaseResult.has_value());
  const auto& mp = *run.multiphaseResult;

  const Real expectedMixtureDensity =
      kInitialAlpha * kPhase1Density + (1.0 - kInitialAlpha) * kPhase2Density;
  const Real expectedMixtureViscosity =
      kInitialAlpha * kPhase1Viscosity + (1.0 - kInitialAlpha) * kPhase2Viscosity;

  for (Index i = 0; i < mp.alphaStep.alpha.size(); ++i) {
    EXPECT_NEAR(mp.alphaStep.alpha[i], kInitialAlpha, 1e-12) << "cell " << i;
    EXPECT_NEAR(mp.mixtureDensity[i], expectedMixtureDensity, 1e-9) << "cell " << i;
    EXPECT_NEAR(mp.mixtureViscosity[i], expectedMixtureViscosity, 1e-12) << "cell " << i;
  }
}

// Conservation (this task's own explicit acceptance criterion): with a
// uniform, unchanged alpha, phase1's own volume must equal
// alpha*domainVolume exactly.
TEST(MultiphaseProductionCaseTest, Phase1VolumeIsConserved) {
  const ProjectRunResult run = ProjectRunner::run(kCaseDirectory);
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.multiphaseResult.has_value());
  ASSERT_TRUE(run.caseDefinition.has_value());

  const Real domainVolume =
      run.caseDefinition->geometry.length * run.caseDefinition->geometry.height;
  EXPECT_NEAR(run.multiphaseResult->phase1Volume, kInitialAlpha * domainVolume, 1e-9);
}

TEST(MultiphaseProductionCaseTest, ExportsMixtureFieldsToCsvVtkAndJson) {
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
    EXPECT_NE(header.find("volume_fraction"), std::string::npos) << header;
    EXPECT_NE(header.find("mixture_density"), std::string::npos) << header;
    EXPECT_NE(header.find("mixture_viscosity"), std::string::npos) << header;
  }
  {
    std::ifstream vtk(*run.exportSummary->vtkPath);
    ASSERT_TRUE(vtk.is_open());
    const std::string content((std::istreambuf_iterator<char>(vtk)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("SCALARS volume_fraction"), std::string::npos);
    EXPECT_NE(content.find("SCALARS mixture_density"), std::string::npos);
    EXPECT_NE(content.find("SCALARS mixture_viscosity"), std::string::npos);
  }
  {
    std::ifstream json(run.exportSummary->metadataPath);
    ASSERT_TRUE(json.is_open());
    const std::string content((std::istreambuf_iterator<char>(json)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"water\""), std::string::npos);
    EXPECT_NE(content.find("\"air\""), std::string::npos);
  }
}

TEST(MultiphaseProductionCaseTest, MissingCaseDirectoryReportsInvalidCase) {
  const ProjectRunResult run = ProjectRunner::run("this/directory/does/not/exist");
  EXPECT_EQ(run.status, ProjectRunStatus::InvalidCase);
  EXPECT_FALSE(run.multiphaseResult.has_value());
}

// Existing single-phase cases remain unaffected.
TEST(MultiphaseProductionCaseTest, NonMultiphaseCaseStillRunsWithNoMultiphaseResult) {
  const ProjectRunResult run = ProjectRunner::run("tests/data/cases/valid_cavity");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  EXPECT_FALSE(run.multiphaseResult.has_value());
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  std::ifstream csv(*run.exportSummary->fieldsCsvPath);
  std::string header;
  std::getline(csv, header);
  EXPECT_EQ(header.find("volume_fraction"), std::string::npos) << header;
}

TEST(MultiphaseProductionCaseTest, RepeatedRunsAreDeterministic) {
  const ProjectRunResult first = ProjectRunner::run(kCaseDirectory);
  const ProjectRunResult second = ProjectRunner::run(kCaseDirectory);
  ASSERT_TRUE(first.multiphaseResult.has_value());
  ASSERT_TRUE(second.multiphaseResult.has_value());
  for (Index i = 0; i < first.multiphaseResult->alphaStep.alpha.size(); ++i) {
    EXPECT_EQ(first.multiphaseResult->alphaStep.alpha[i],
              second.multiphaseResult->alphaStep.alpha[i]);
  }
}
