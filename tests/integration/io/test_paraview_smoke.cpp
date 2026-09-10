// P5-H -- ParaView Workflow, section 37: "Create a small automated
// check that generated VTK exists / is non-empty / contains expected
// mesh structure / contains expected field names. Do not rely solely on
// manually opening ParaView." -- run a real case through the production
// backend (cfd::app::ProjectRunner, the same one apps/cli/main.cpp
// uses) and inspect the resulting solution.vtk exactly as ParaView's
// own legacy-VTK reader would need to.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "cfd/app/ProjectRunner.hpp"

using cfd::app::ProjectRunner;
using cfd::app::ProjectRunStatus;

TEST(ParaviewSmokeTest, SolutionVtkExistsIsNonEmptyAndHasExpectedStructureAndFields) {
  const auto run = ProjectRunner::run("tests/data/cases/valid_cavity");
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->vtkPath.has_value());

  const auto& vtkPath = *run.exportSummary->vtkPath;
  ASSERT_TRUE(std::filesystem::exists(vtkPath));
  ASSERT_GT(std::filesystem::file_size(vtkPath), 0u);

  std::ifstream in(vtkPath);
  ASSERT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string content = buffer.str();

  // Mesh structure ParaView's own legacy-VTK reader requires.
  EXPECT_NE(content.find("# vtk DataFile Version"), std::string::npos);
  EXPECT_NE(content.find("DATASET UNSTRUCTURED_GRID"), std::string::npos);
  EXPECT_NE(content.find("POINTS "), std::string::npos);
  EXPECT_NE(content.find("CELLS "), std::string::npos);
  EXPECT_NE(content.find("CELL_TYPES "), std::string::npos);

  // Expected field names (section 35-36): pressure, a real vector
  // field for velocity (not two separate scalars a ParaView user would
  // have to recombine by hand), and the derived magnitude.
  EXPECT_NE(content.find("SCALARS pressure"), std::string::npos);
  EXPECT_NE(content.find("VECTORS velocity"), std::string::npos);
  EXPECT_NE(content.find("SCALARS velocity_magnitude"), std::string::npos);
}
