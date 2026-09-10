// P5 GUI Visualization Integration, section 1/9: VisualizationSnapshot
// is the one "solver results -> C++ visualization model" bridge --
// checked here to behave identically whether built live from a
// ProjectRunResult or reloaded from a previously-written results/
// directory (section 9's own "without rerunning the solver").
#include <gtest/gtest.h>

#include <algorithm>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/app/VisualizationSnapshot.hpp"

using cfd::app::buildSnapshot;
using cfd::app::loadSnapshotFromResults;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunStatus;
using cfd::app::VisualizationSnapshot;

TEST(BuildSnapshotTest, ValidRunProducesAUsableSnapshot) {
  const auto run = ProjectRunner::run("tests/data/cases/valid_cavity");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);

  const VisualizationSnapshot snapshot = buildSnapshot(run);
  EXPECT_TRUE(snapshot.valid);
  EXPECT_EQ(snapshot.nx, 4u);
  EXPECT_EQ(snapshot.ny, 4u);
  ASSERT_EQ(snapshot.points.size(), 16u);
  EXPECT_EQ(snapshot.pressure.size(), 16u);
  EXPECT_EQ(snapshot.velocityMagnitude.size(), 16u);
  EXPECT_FALSE(snapshot.temperature.has_value());  // valid_cavity has no thermal block.

  const auto fields = snapshot.availableScalarFields();
  EXPECT_NE(std::find(fields.begin(), fields.end(), "pressure"), fields.end());
  EXPECT_NE(std::find(fields.begin(), fields.end(), "velocity_magnitude"), fields.end());
  EXPECT_EQ(std::find(fields.begin(), fields.end(), "temperature"), fields.end());

  ASSERT_NE(snapshot.scalarField("pressure"), nullptr);
  EXPECT_EQ(snapshot.scalarField("nonexistent_field"), nullptr);

  const auto residualNames = snapshot.availableResidualSeries();
  EXPECT_EQ(residualNames.size(), 4u);
  ASSERT_NE(snapshot.residualSeries("continuity"), nullptr);
  EXPECT_FALSE(snapshot.residualSeries("continuity")->empty());
}

TEST(BuildSnapshotTest, IncompleteRunProducesAnInvalidSnapshot) {
  const auto run = ProjectRunner::run("this/directory/does/not/exist");
  ASSERT_EQ(run.status, ProjectRunStatus::InvalidCase);

  const VisualizationSnapshot snapshot = buildSnapshot(run);
  EXPECT_FALSE(snapshot.valid);
  EXPECT_TRUE(snapshot.availableScalarFields().empty());
  EXPECT_EQ(snapshot.scalarField("pressure"), nullptr);
}

TEST(LoadSnapshotFromResultsTest, ReloadsAPreviouslyWrittenResultsDirectory) {
  // Produce a real results/ directory first (this is the same
  // production pipeline the CLI uses), then reload it independently --
  // exactly the "open a completed case without rerunning" workflow.
  const auto run = ProjectRunner::run("tests/data/cases/valid_cavity");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.exportSummary.has_value());

  const VisualizationSnapshot live = buildSnapshot(run);
  const VisualizationSnapshot reloaded =
      loadSnapshotFromResults(std::filesystem::path("tests/data/cases/valid_cavity") / "results");

  ASSERT_TRUE(reloaded.valid);
  EXPECT_EQ(reloaded.nx, live.nx);
  EXPECT_EQ(reloaded.ny, live.ny);
  ASSERT_EQ(reloaded.points.size(), live.points.size());
  for (std::size_t i = 0; i < live.points.size(); ++i) {
    EXPECT_NEAR(reloaded.pressure[i], live.pressure[i], 1e-9);
    EXPECT_NEAR(reloaded.velocityMagnitude[i], live.velocityMagnitude[i], 1e-9);
  }
  ASSERT_EQ(reloaded.continuityResidualHistory.size(), live.continuityResidualHistory.size());
}

TEST(LoadSnapshotFromResultsTest, MissingDirectoryProducesAnInvalidSnapshot) {
  const auto snapshot = loadSnapshotFromResults("this/results/directory/does/not/exist");
  EXPECT_FALSE(snapshot.valid);
}

TEST(LoadSnapshotFromResultsTest, ThermalCaseExposesTemperatureField) {
  const auto run = ProjectRunner::run("cases/heated_cavity");
  ASSERT_TRUE(run.exportSummary.has_value());

  const auto reloaded =
      loadSnapshotFromResults(std::filesystem::path("cases/heated_cavity") / "results");
  ASSERT_TRUE(reloaded.valid);
  ASSERT_TRUE(reloaded.temperature.has_value());
  const auto fields = reloaded.availableScalarFields();
  EXPECT_NE(std::find(fields.begin(), fields.end(), "temperature"), fields.end());
}

// P6-PHYS-001/002/003: "Reload / GUI compatibility" acceptance
// criterion -- run a case, then reload its results/ directory
// independently (no solver rerun), and confirm the new physics'
// own fields are discoverable exactly like pressure/temperature, both
// live (buildSnapshot) and reloaded (loadSnapshotFromResults), and that
// the two agree with each other (same values, same field names) --
// SimulationController (apps/gui/) needs no code change per physics
// module because both paths already go through availableScalarFields()/
// scalarField() generically.
TEST(VisualizationSnapshotTest, SpeciesCaseExposesConcentrationFieldLiveAndReloaded) {
  const auto run = ProjectRunner::run("cases/species_diffusion");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  const VisualizationSnapshot live = buildSnapshot(run);
  const auto reloaded =
      loadSnapshotFromResults(std::filesystem::path("cases/species_diffusion") / "results");

  ASSERT_TRUE(live.valid);
  ASSERT_TRUE(reloaded.valid);
  for (const VisualizationSnapshot* snapshot : {&live, &reloaded}) {
    const auto fields = snapshot->availableScalarFields();
    EXPECT_NE(std::find(fields.begin(), fields.end(), "concentration_CO2"), fields.end());
    ASSERT_NE(snapshot->scalarField("concentration_CO2"), nullptr);
  }
  ASSERT_EQ(live.scalarField("concentration_CO2")->size(),
            reloaded.scalarField("concentration_CO2")->size());
  for (std::size_t i = 0; i < live.scalarField("concentration_CO2")->size(); ++i) {
    EXPECT_NEAR((*live.scalarField("concentration_CO2"))[i],
                (*reloaded.scalarField("concentration_CO2"))[i], 1e-9);
  }
}

TEST(VisualizationSnapshotTest, MultiphaseCaseExposesMixtureFieldsLiveAndReloaded) {
  const auto run = ProjectRunner::run("cases/multiphase_validation");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  const VisualizationSnapshot live = buildSnapshot(run);
  const auto reloaded =
      loadSnapshotFromResults(std::filesystem::path("cases/multiphase_validation") / "results");

  ASSERT_TRUE(live.valid);
  ASSERT_TRUE(reloaded.valid);
  for (const VisualizationSnapshot* snapshot : {&live, &reloaded}) {
    const auto fields = snapshot->availableScalarFields();
    EXPECT_NE(std::find(fields.begin(), fields.end(), "volume_fraction"), fields.end());
    EXPECT_NE(std::find(fields.begin(), fields.end(), "mixture_density"), fields.end());
    EXPECT_NE(std::find(fields.begin(), fields.end(), "mixture_viscosity"), fields.end());
  }
}

TEST(VisualizationSnapshotTest, CompressibleCaseExposesThermodynamicFieldsLiveAndReloaded) {
  const auto run = ProjectRunner::run("cases/compressible_validation");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  const VisualizationSnapshot live = buildSnapshot(run);
  const auto reloaded =
      loadSnapshotFromResults(std::filesystem::path("cases/compressible_validation") / "results");

  ASSERT_TRUE(live.valid);
  ASSERT_TRUE(reloaded.valid);
  for (const VisualizationSnapshot* snapshot : {&live, &reloaded}) {
    const auto fields = snapshot->availableScalarFields();
    EXPECT_NE(std::find(fields.begin(), fields.end(), "density"), fields.end());
    EXPECT_NE(std::find(fields.begin(), fields.end(), "mach_number"), fields.end());
  }
}
