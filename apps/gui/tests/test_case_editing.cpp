// P7-GUI -- GUI Case Editing: SimulationController's editing surface
// (SimulationControllerEditing.cpp/CaseModelAdapter.hpp) -- the
// controller/model layer the mesh/physics/boundary/solver editor QML
// pages bind to. Exercises get/set round trips through the real
// CaseSession (never a mock), validateDraft()'s real CaseWriter/
// CaseReader/CaseBuilder round trip, and the BC/turbulence vocabulary
// exposed for QML dropdowns. Same bare-QCoreApplication convention as
// test_simulation_controller.cpp (whose QtEnvironment this binary
// shares -- see its own header comment; not redeclared here).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGrading.hpp"

#include "../SimulationController.hpp"

using cfd::testutil::CaseFixtureCopy;

namespace {

QString fixtureQString(const CaseFixtureCopy& fixture) {
  return QString::fromStdString(fixture.path().string());
}

}  // namespace

TEST(CaseEditingTest, GettersReflectTheOpenedCaseBeforeAnyEdit) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(fixture)));

  const QVariantMap mesh = controller.meshConfig();
  EXPECT_EQ(mesh.value("nx").toInt(), 4);
  EXPECT_EQ(mesh.value("ny").toInt(), 4);

  const QVariantMap geometry = controller.geometryConfig();
  EXPECT_DOUBLE_EQ(geometry.value("length").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(geometry.value("height").toDouble(), 1.0);

  const QVariantMap physics = controller.physicsConfig();
  EXPECT_DOUBLE_EQ(physics.value("density").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(physics.value("dynamicViscosity").toDouble(), 0.01);
  EXPECT_FALSE(physics.contains("thermal"));  // absent means off -- header comment convention.

  const QVariantMap boundaries = controller.boundaryConfig();
  ASSERT_TRUE(boundaries.contains("top"));
  EXPECT_EQ(boundaries.value("top").toMap().value("velocity").toMap().value("type").toString(),
            QStringLiteral("moving_wall"));

  const QVariantMap solver = controller.solverConfig();
  EXPECT_EQ(solver.value("type").toString(), QStringLiteral("SIMPLE"));
  EXPECT_EQ(solver.value("maxIterations").toInt(), 6000);

  const QVariantMap metadata = controller.caseMetadata();
  EXPECT_EQ(metadata.value("name").toString(), QStringLiteral("Test Cavity 4x4"));
}

TEST(CaseEditingTest, GettersOnAnUnloadedControllerReturnEmptyMaps) {
  SimulationController controller;
  EXPECT_TRUE(controller.meshConfig().isEmpty());
  EXPECT_TRUE(controller.physicsConfig().isEmpty());
  EXPECT_TRUE(controller.boundaryConfig().isEmpty());
  EXPECT_TRUE(controller.solverConfig().isEmpty());
}

TEST(CaseEditingTest, SetMeshAndGeometryCommitsAndEmitsCaseChanged) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));

  QSignalSpy caseChangedSpy(&controller, &SimulationController::caseChanged);
  QVariantMap mesh = controller.meshConfig();
  mesh["nx"] = 8;
  mesh["ny"] = 8;
  QVariantMap geometry = controller.geometryConfig();
  geometry["length"] = 2.0;

  EXPECT_TRUE(controller.setMeshAndGeometry(mesh, geometry));
  EXPECT_GE(caseChangedSpy.count(), 1);
  EXPECT_EQ(controller.meshConfig().value("nx").toInt(), 8);
  EXPECT_EQ(controller.meshConfig().value("ny").toInt(), 8);
  EXPECT_DOUBLE_EQ(controller.geometryConfig().value("length").toDouble(), 2.0);
}

TEST(CaseEditingTest, MeshEditRoundTripsThroughSaveReopenAndRunsThroughProjectRunner) {
  // Mesh editor acceptance gate: "GUI edit -> save -> reopen -> values
  // identical -> CLI solve" -- exercised here through the exact same
  // CaseWriter/CaseReader/ProjectRunner pipeline the CLI uses (never a
  // second GUI-only path), on a private fixture copy (P7-TEST-001).
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));

  QVariantMap mesh = editor.meshConfig();
  mesh["nx"] = 6;
  mesh["ny"] = 6;
  QVariantMap geometry = editor.geometryConfig();
  geometry["length"] = 1.5;
  geometry["height"] = 1.5;
  ASSERT_TRUE(editor.setMeshAndGeometry(mesh, geometry));
  ASSERT_TRUE(editor.validateDraft().value("valid").toBool());
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));

  // A second, independent controller (standing in for "close the GUI,
  // reopen the case") sees the exact edited values -- not the editor's
  // own in-memory copy.
  SimulationController reopened;
  ASSERT_TRUE(reopened.openCase(fixtureQString(fixture)));
  EXPECT_EQ(reopened.meshConfig().value("nx").toInt(), 6);
  EXPECT_EQ(reopened.meshConfig().value("ny").toInt(), 6);
  EXPECT_DOUBLE_EQ(reopened.geometryConfig().value("length").toDouble(), 1.5);
  EXPECT_DOUBLE_EQ(reopened.geometryConfig().value("height").toDouble(), 1.5);

  QSignalSpy completedSpy(&reopened, &SimulationController::completed);
  reopened.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  EXPECT_TRUE(reopened.hasResults());
}

TEST(CaseEditingTest, SetOnAnUnloadedControllerIsANoOp) {
  SimulationController controller;
  EXPECT_FALSE(controller.setMeshAndGeometry(QVariantMap{}, QVariantMap{}));
  EXPECT_FALSE(controller.setPhysicsConfig(QVariantMap{}));
  EXPECT_FALSE(controller.setBoundaryConfig(QVariantMap{}));
  EXPECT_FALSE(controller.setSolverConfig(QVariantMap{}));
  EXPECT_FALSE(controller.setInitialConditions(QVariantMap{}));
  EXPECT_FALSE(controller.setCaseMetadata(QVariantMap{}));
}

TEST(CaseEditingTest, SetPhysicsConfigRoundTripsAThermalBlock) {
  SimulationController controller;
  controller.newCase();

  QVariantMap physics = controller.physicsConfig();
  physics["density"] = 1.0;
  physics["dynamicViscosity"] = 0.01;
  QVariantMap thermal;
  thermal["conductivity"] = 0.6;
  thermal["specificHeat"] = 4180.0;
  thermal["initialTemperature"] = 300.0;
  physics["thermal"] = thermal;

  ASSERT_TRUE(controller.setPhysicsConfig(physics));
  const QVariantMap reread = controller.physicsConfig();
  ASSERT_TRUE(reread.contains("thermal"));
  EXPECT_DOUBLE_EQ(reread.value("thermal").toMap().value("conductivity").toDouble(), 0.6);
}

// (P12-MESH-003: the adapter takes back every patch of the map -- a
// Cartesian case's map holds exactly its four.)
TEST(CaseEditingTest, SetBoundaryConfigOnlyKeepsTheFourCanonicalPatches) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));

  QVariantMap boundaries = controller.boundaryConfig();
  QVariantMap top = boundaries.value("top").toMap();
  QVariantMap topVelocity = top.value("velocity").toMap();
  topVelocity["valueX"] = 2.0;
  top["velocity"] = topVelocity;
  boundaries["top"] = top;

  ASSERT_TRUE(controller.setBoundaryConfig(boundaries));
  const QVariantMap reread = controller.boundaryConfig();
  EXPECT_EQ(reread.size(), 4);
  EXPECT_DOUBLE_EQ(reread.value("top").toMap().value("velocity").toMap().value("valueX").toDouble(),
                   2.0);
}

TEST(CaseEditingTest, SetSolverConfigRoundTripsToleranceAndLinearSolver) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));

  QVariantMap solver = controller.solverConfig();
  solver["maxIterations"] = 1234;
  QVariantMap momentum = solver.value("momentumSolver").toMap();
  momentum["maxIterations"] = 42;
  solver["momentumSolver"] = momentum;

  ASSERT_TRUE(controller.setSolverConfig(solver));
  const QVariantMap reread = controller.solverConfig();
  EXPECT_EQ(reread.value("maxIterations").toInt(), 1234);
  EXPECT_EQ(reread.value("momentumSolver").toMap().value("maxIterations").toInt(), 42);
}

TEST(CaseEditingTest, SetInitialConditionsRoundTrips) {
  SimulationController controller;
  controller.newCase();

  QVariantMap initial;
  initial["velocityX"] = 0.5;
  initial["velocityY"] = -0.25;
  initial["pressure"] = 101325.0;
  ASSERT_TRUE(controller.setInitialConditions(initial));

  const QVariantMap reread = controller.initialConditions();
  EXPECT_DOUBLE_EQ(reread.value("velocityX").toDouble(), 0.5);
  EXPECT_DOUBLE_EQ(reread.value("velocityY").toDouble(), -0.25);
  EXPECT_DOUBLE_EQ(reread.value("pressure").toDouble(), 101325.0);
}

TEST(CaseEditingTest, SetCaseMetadataUpdatesNameAndDescriptionOnly) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));

  QVariantMap metadata;
  metadata["name"] = QStringLiteral("Renamed Case");
  metadata["description"] = QStringLiteral("Edited via the GUI");
  ASSERT_TRUE(controller.setCaseMetadata(metadata));

  const QVariantMap reread = controller.caseMetadata();
  EXPECT_EQ(reread.value("name").toString(), QStringLiteral("Renamed Case"));
  EXPECT_EQ(reread.value("description").toString(), QStringLiteral("Edited via the GUI"));
}

TEST(CaseEditingTest, ValidateDraftOnAValidOpenedCaseReportsValid) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));

  QSignalSpy validationSpy(&controller, &SimulationController::validationChanged);
  const QVariantMap result = controller.validateDraft();
  EXPECT_TRUE(result.value("valid").toBool());
  EXPECT_EQ(validationSpy.count(), 1);
  EXPECT_TRUE(controller.validationStatus().value("valid").toBool());
}

TEST(CaseEditingTest, ValidateDraftOnAFreshEmptyCaseReportsInvalidWithASection) {
  SimulationController controller;
  controller.newCase();  // zero density/viscosity, no boundary patches -- same as
                         // CaseSessionTest.ValidateInvalidCaseLeavesStateAndReportsError.

  const QVariantMap result = controller.validateDraft();
  EXPECT_FALSE(result.value("valid").toBool());
  EXPECT_FALSE(result.value("message").toString().isEmpty());
  EXPECT_FALSE(controller.validationStatus().value("valid").toBool());
}

TEST(CaseEditingTest, ValidateDraftExtractsTheOffendingFieldName) {
  // fieldForMessage() (SimulationControllerEditing.cpp) parses
  // throwConfigError()'s own canonical "field \"<name>\" must satisfy
  // ..." message shape (JsonUtil.cpp) -- checked here against a
  // controlled, known failure (mesh.json's own "nx must be > 0" check,
  // MeshConfigParser.cpp) rather than relying on whichever field a fresh
  // empty case happens to fail on first.
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));
  QVariantMap mesh = controller.meshConfig();
  mesh["nx"] = 0;
  ASSERT_TRUE(controller.setMeshAndGeometry(mesh, controller.geometryConfig()));

  const QVariantMap result = controller.validateDraft();
  EXPECT_FALSE(result.value("valid").toBool());
  EXPECT_EQ(result.value("section").toString(), QStringLiteral("Mesh"));
  EXPECT_EQ(result.value("field").toString(), QStringLiteral("nx"));
}

TEST(CaseEditingTest, ValidationIssuesIsEmptyWhenValid) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));
  ASSERT_TRUE(controller.validateDraft().value("valid").toBool());
  EXPECT_EQ(controller.validationIssues().size(), 0);
}

TEST(CaseEditingTest, ValidationIssuesCarriesOneStructuredEntryWhenInvalid) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));
  QVariantMap mesh = controller.meshConfig();
  mesh["nx"] = 0;
  ASSERT_TRUE(controller.setMeshAndGeometry(mesh, controller.geometryConfig()));
  controller.validateDraft();

  const QVariantList issues = controller.validationIssues();
  ASSERT_EQ(issues.size(), 1);
  const QVariantMap issue = issues.front().toMap();
  EXPECT_EQ(issue.value("severity").toString(), QStringLiteral("Error"));
  EXPECT_EQ(issue.value("section").toString(), QStringLiteral("Mesh"));
  EXPECT_EQ(issue.value("field").toString(), QStringLiteral("nx"));
  EXPECT_FALSE(issue.value("message").toString().isEmpty());
}

TEST(CaseEditingTest, ValidateDraftWithNoCaseLoadedReportsInvalid) {
  SimulationController controller;
  const QVariantMap result = controller.validateDraft();
  EXPECT_FALSE(result.value("valid").toBool());
  EXPECT_FALSE(result.value("message").toString().isEmpty());
}

TEST(CaseEditingTest, ValidateDraftNeverMutatesTheOpenedCaseDirectoryOnDisk) {
  // validateDraft() round-trips through its own scratch temp directory
  // (SimulationControllerEditing.cpp's own ScratchDirectory), never the
  // opened case's own directory -- important both for correctness (a
  // "validate" action must not be a hidden "save") and for this
  // codebase's own parallel-ctest fixture-isolation policy (P7-TEST-001):
  // this must stay true even without CaseFixtureCopy, so this test
  // deliberately opens the canonical shared fixture directly to prove
  // validateDraft() itself never writes into it.
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  QVariantMap mesh = controller.meshConfig();
  mesh["nx"] = 99;
  ASSERT_TRUE(controller.setMeshAndGeometry(mesh, controller.geometryConfig()));

  controller.validateDraft();

  const auto meshJsonPath = std::filesystem::path("tests/data/cases/valid_cavity") / "mesh.json";
  std::ifstream in(meshJsonPath);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("\"nx\": 4"), std::string::npos)
      << "validateDraft() must not have written the in-memory edit to disk";
}

TEST(CaseEditingTest, VocabularyListsMatchWhatBoundaryConfigParserAccepts) {
  SimulationController controller;
  const QStringList velocity = controller.velocityBoundaryTypes();
  EXPECT_TRUE(velocity.contains(QStringLiteral("wall")));
  EXPECT_TRUE(velocity.contains(QStringLiteral("moving_wall")));
  EXPECT_TRUE(velocity.contains(QStringLiteral("inlet")));
  EXPECT_TRUE(velocity.contains(QStringLiteral("outlet")));
  EXPECT_TRUE(velocity.contains(QStringLiteral("symmetry")));

  const QStringList pressure = controller.pressureBoundaryTypes();
  EXPECT_TRUE(pressure.contains(QStringLiteral("fixed_value")));
  EXPECT_TRUE(pressure.contains(QStringLiteral("fixed_gradient")));

  const QStringList temperature = controller.temperatureBoundaryTypes();
  EXPECT_TRUE(temperature.contains(QStringLiteral("fixed_temperature")));
  EXPECT_TRUE(temperature.contains(QStringLiteral("heat_flux")));
  EXPECT_TRUE(temperature.contains(QStringLiteral("adiabatic")));

  const QStringList turbulence = controller.turbulenceModelNames();
  EXPECT_TRUE(turbulence.contains(QStringLiteral("laminar")));
  EXPECT_TRUE(turbulence.contains(QStringLiteral("k_epsilon")));
  EXPECT_TRUE(turbulence.contains(QStringLiteral("k_omega")));
  EXPECT_TRUE(turbulence.contains(QStringLiteral("sst")));
}

TEST(CaseEditingTest, TypeHasValueMatchesTheDocumentedSubsetOnly) {
  SimulationController controller;
  EXPECT_TRUE(controller.velocityTypeHasValue(QStringLiteral("moving_wall")));
  EXPECT_TRUE(controller.velocityTypeHasValue(QStringLiteral("inlet")));
  EXPECT_FALSE(controller.velocityTypeHasValue(QStringLiteral("wall")));
  EXPECT_FALSE(controller.velocityTypeHasValue(QStringLiteral("outlet")));
  EXPECT_FALSE(controller.velocityTypeHasValue(QStringLiteral("symmetry")));

  EXPECT_TRUE(controller.temperatureTypeHasValue(QStringLiteral("fixed_temperature")));
  EXPECT_TRUE(controller.temperatureTypeHasValue(QStringLiteral("heat_flux")));
  EXPECT_FALSE(controller.temperatureTypeHasValue(QStringLiteral("adiabatic")));
}

TEST(CaseEditingTest, MeshCellInfoComputesCountAndSpacing) {
  SimulationController controller;
  QVariantMap mesh;
  mesh["nx"] = 4;
  mesh["ny"] = 5;
  QVariantMap geometry;
  geometry["length"] = 2.0;
  geometry["height"] = 1.0;

  const QVariantMap info = controller.meshCellInfo(mesh, geometry);
  EXPECT_DOUBLE_EQ(info.value("cellCount").toDouble(), 20.0);
  EXPECT_DOUBLE_EQ(info.value("dx").toDouble(), 0.5);
  EXPECT_DOUBLE_EQ(info.value("dy").toDouble(), 0.2);
  EXPECT_FALSE(info.value("isLarge").toBool());
}

TEST(CaseEditingTest, MeshCellInfoNeverDividesByZero) {
  SimulationController controller;
  QVariantMap mesh;
  mesh["nx"] = 0;
  mesh["ny"] = 0;
  QVariantMap geometry;
  geometry["length"] = 1.0;
  geometry["height"] = 1.0;

  const QVariantMap info = controller.meshCellInfo(mesh, geometry);
  EXPECT_DOUBLE_EQ(info.value("cellCount").toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(info.value("dx").toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(info.value("dy").toDouble(), 0.0);
}

TEST(CaseEditingTest, MeshCellInfoFlagsALargeMesh) {
  SimulationController controller;
  QVariantMap mesh;
  mesh["nx"] = 1000;
  mesh["ny"] = 1000;
  QVariantMap geometry;
  geometry["length"] = 1.0;
  geometry["height"] = 1.0;

  EXPECT_TRUE(controller.meshCellInfo(mesh, geometry).value("isLarge").toBool());
}

TEST(CaseEditingTest, ValidatedEditSavesAndRunsThroughTheSamePipelineAsCli) {
  // The end-to-end contract Phase B's "Full case creation from GUI" gate
  // needs: edit in memory -> validateDraft() -> saveAs() -> run() -- all
  // through CaseSession/ProjectRunner, never a second GUI-only path.
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(fixture)));

  QVariantMap solver = controller.solverConfig();
  solver["maxIterations"] = 8000;
  ASSERT_TRUE(controller.setSolverConfig(solver));

  ASSERT_TRUE(controller.validateDraft().value("valid").toBool());
  ASSERT_TRUE(controller.saveAs(fixtureQString(fixture)));

  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  EXPECT_TRUE(controller.hasResults());
}

TEST(CaseEditingTest, PhysicsEditRoundTripsThroughSaveReopenAndRuns) {
  // Physics editor acceptance gate: enabling thermal, saving, reopening,
  // and running must all agree -- the same CaseWriter/CaseReader/
  // ProjectRunner pipeline as every other editor page.
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));

  QVariantMap physics = editor.physicsConfig();
  QVariantMap thermal;
  thermal["conductivity"] = 0.6;
  thermal["specificHeat"] = 4180.0;
  thermal["initialTemperature"] = 300.0;
  physics["thermal"] = thermal;
  ASSERT_TRUE(editor.setPhysicsConfig(physics));

  // Boundaries.json requires a "temperature" entry on every patch once
  // thermal is enabled (BoundaryConfigParser.cpp's own per-patch
  // validation) -- set an adiabatic condition on all four so this is a
  // genuinely valid, runnable case, not a deliberately-invalid probe.
  QVariantMap boundaries = editor.boundaryConfig();
  for (const char* name : {"left", "right", "top", "bottom"}) {
    QVariantMap patch = boundaries.value(name).toMap();
    patch["temperature"] = QVariantMap{{"type", QStringLiteral("adiabatic")}, {"value", 0.0}};
    boundaries[name] = patch;
  }
  ASSERT_TRUE(editor.setBoundaryConfig(boundaries));

  ASSERT_TRUE(editor.validateDraft().value("valid").toBool())
      << editor.validationStatus().value("message").toString().toStdString();
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));

  SimulationController reopened;
  ASSERT_TRUE(reopened.openCase(fixtureQString(fixture)));
  EXPECT_TRUE(reopened.physicsConfig().contains("thermal"));
  EXPECT_DOUBLE_EQ(
      reopened.physicsConfig().value("thermal").toMap().value("conductivity").toDouble(), 0.6);

  QSignalSpy completedSpy(&reopened, &SimulationController::completed);
  reopened.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  EXPECT_TRUE(reopened.hasResults());
}

TEST(CaseEditingTest, BoundaryEditRoundTripsThroughSaveReopenAndRuns) {
  // Boundary-condition editor acceptance gate: "existing case -> GUI ->
  // change one BC -> save -> reopen -> exact BC retained", then a
  // successful run.
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));

  QVariantMap boundaries = editor.boundaryConfig();
  QVariantMap top = boundaries.value("top").toMap();
  QVariantMap topVelocity = top.value("velocity").toMap();
  topVelocity["valueX"] = 2.5;
  top["velocity"] = topVelocity;
  boundaries["top"] = top;
  ASSERT_TRUE(editor.setBoundaryConfig(boundaries));

  ASSERT_TRUE(editor.validateDraft().value("valid").toBool());
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));

  SimulationController reopened;
  ASSERT_TRUE(reopened.openCase(fixtureQString(fixture)));
  EXPECT_DOUBLE_EQ(reopened.boundaryConfig()
                       .value("top")
                       .toMap()
                       .value("velocity")
                       .toMap()
                       .value("valueX")
                       .toDouble(),
                   2.5);

  QSignalSpy completedSpy(&reopened, &SimulationController::completed);
  reopened.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  EXPECT_TRUE(reopened.hasResults());
}

TEST(CaseEditingTest, FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli) {
  // GUI-009/GUI-010 -- the "Full case creation from GUI" gate itself:
  // New Case -> Mesh -> Physics -> Boundaries -> Solver -> Validate ->
  // Save -> Run, built entirely from typed defaults (newCase()), never
  // touching an existing fixture's own JSON files, then independently
  // re-run via ProjectRunner (the exact same entry point
  // apps/cli/main.cpp calls) against the saved directory to prove the
  // saved case is genuinely CLI-runnable, not just GUI-runnable.
  class TempDir {
   public:
    TempDir() {
      path_ = std::filesystem::temp_directory_path() /
              ("cfdapp_full_case_creation_test_" +
               std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
      std::filesystem::create_directories(path_);
    }
    ~TempDir() {
      std::error_code ec;
      std::filesystem::remove_all(path_, ec);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

   private:
    std::filesystem::path path_;
  } target;

  SimulationController controller;
  controller.newCase();

  QVariantMap metadata = controller.caseMetadata();
  metadata["name"] = QStringLiteral("GUI-Authored Cavity");
  ASSERT_TRUE(controller.setCaseMetadata(metadata));

  ASSERT_TRUE(controller.setMeshAndGeometry(
      QVariantMap{{"type", QStringLiteral("structured_cartesian")}, {"nx", 4}, {"ny", 4}},
      QVariantMap{{"type", QStringLiteral("rectangle")}, {"length", 1.0}, {"height", 1.0}}));

  ASSERT_TRUE(
      controller.setPhysicsConfig(QVariantMap{{"model", QStringLiteral("incompressible_laminar")},
                                              {"density", 1.0},
                                              {"dynamicViscosity", 0.01}}));

  QVariantMap boundaries;
  for (const char* name : {"left", "right", "bottom"}) {
    boundaries[name] = QVariantMap{
        {"velocity",
         QVariantMap{{"type", QStringLiteral("wall")}, {"valueX", 0.0}, {"valueY", 0.0}}},
        {"pressure", QVariantMap{{"type", QStringLiteral("fixed_gradient")}, {"value", 0.0}}}};
  }
  boundaries["top"] = QVariantMap{
      {"velocity",
       QVariantMap{{"type", QStringLiteral("moving_wall")}, {"valueX", 1.0}, {"valueY", 0.0}}},
      {"pressure", QVariantMap{{"type", QStringLiteral("fixed_gradient")}, {"value", 0.0}}}};
  ASSERT_TRUE(controller.setBoundaryConfig(boundaries));

  QVariantMap solver = controller.solverConfig();
  solver["type"] = QStringLiteral("SIMPLE");
  solver["maxIterations"] = 6000;
  solver["velocityRelaxation"] = 0.7;
  solver["pressureRelaxation"] = 0.3;
  solver["velocityTolerance"] = 1e-6;
  solver["pressureTolerance"] = 1e-6;
  solver["continuityTolerance"] = 1e-6;
  solver["momentumSolver"] = QVariantMap{{"type", QStringLiteral("BiCGSTAB")},
                                         {"absoluteTolerance", 1e-10},
                                         {"relativeTolerance", 1e-8},
                                         {"maxIterations", 500}};
  solver["pressureSolver"] = QVariantMap{{"type", QStringLiteral("BiCGSTAB")},
                                         {"absoluteTolerance", 1e-10},
                                         {"relativeTolerance", 1e-8},
                                         {"maxIterations", 2000}};
  ASSERT_TRUE(controller.setSolverConfig(solver));

  const QVariantMap validation = controller.validateDraft();
  ASSERT_TRUE(validation.value("valid").toBool())
      << validation.value("section").toString().toStdString() << " / "
      << validation.value("field").toString().toStdString() << ": "
      << validation.value("message").toString().toStdString();

  ASSERT_TRUE(controller.saveAs(QString::fromStdString(target.path().string())));

  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  ASSERT_TRUE(controller.hasResults());

  // "CLI/GUI Round-Trip Gate": the exact production entry point
  // apps/cli/main.cpp's own runCase() delegates to, run independently
  // against the GUI-saved directory.
  const cfd::app::ProjectRunResult cliEquivalentRun = cfd::app::ProjectRunner::run(target.path());
  EXPECT_EQ(cliEquivalentRun.status, cfd::app::ProjectRunStatus::Converged);
}

// P12-MESH-001: the GUI has no vertex editor, so a structured_quad case's
// vertex grid must survive an edit/save/reopen round trip unchanged (the
// mesh editor's QVariantMap carries type/nx/ny only), and the mesh map
// reports the vertex count read-only.
TEST(CaseEditingTest, StructuredQuadVerticesSurviveEditSaveReopen) {
  const CaseFixtureCopy fixture("cases/poiseuille_distorted");
  const auto original = cfd::io::CaseReader{}.read(fixture.path());
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));
  QVariantMap mesh = editor.meshConfig();
  EXPECT_EQ(mesh.value("type").toString(), QStringLiteral("structured_quad"));
  EXPECT_EQ(mesh.value("vertexCount").toInt(), 585);

  // A mesh-page commit (same nx/ny) plus a metadata edit, then save.
  ASSERT_TRUE(editor.setMeshAndGeometry(mesh, editor.geometryConfig()));
  QVariantMap metadata = editor.caseMetadata();
  metadata["name"] = QStringLiteral("edited distorted channel");
  ASSERT_TRUE(editor.setCaseMetadata(metadata));
  ASSERT_TRUE(editor.validateDraft().value("valid").toBool());
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));

  const auto reread = cfd::io::CaseReader{}.read(fixture.path());
  EXPECT_EQ(reread.caseConfig.name, "edited distorted channel");
  EXPECT_EQ(reread.mesh.type, "structured_quad");
  ASSERT_EQ(reread.mesh.vertices.size(), original.mesh.vertices.size());
  for (std::size_t k = 0; k < original.mesh.vertices.size(); ++k) {
    EXPECT_EQ(reread.mesh.vertices[k].x, original.mesh.vertices[k].x) << k;
    EXPECT_EQ(reread.mesh.vertices[k].y, original.mesh.vertices[k].y) << k;
  }
}

// Changing nx/ny of a structured_quad case in the GUI without a matching
// vertex grid is a validation error on the Mesh section, not a silent
// re-mesh.
TEST(CaseEditingTest, StructuredQuadResolutionChangeWithoutVerticesIsInvalid) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(CaseFixtureCopy("cases/poiseuille_distorted"))));
  QVariantMap mesh = controller.meshConfig();
  mesh["nx"] = 32;
  ASSERT_TRUE(controller.setMeshAndGeometry(mesh, controller.geometryConfig()));
  const QVariantMap result = controller.validateDraft();
  EXPECT_FALSE(result.value("valid").toBool());
  EXPECT_EQ(result.value("section").toString(), QStringLiteral("Mesh"));
  EXPECT_EQ(result.value("field").toString(), QStringLiteral("vertices"));
}

// P12-MESH-002: grading through the mesh editor's flattened keys --
// loaded from mesh.json, edited, validated by the real CaseReader, saved,
// and reopened unchanged.
TEST(CaseEditingTest, GradingRoundTripsThroughEditSaveReopen) {
  const CaseFixtureCopy fixture("cases/channel_transpiration_graded");
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));
  QVariantMap mesh = editor.meshConfig();
  EXPECT_TRUE(mesh.value("hasGrading").toBool());
  EXPECT_EQ(mesh.value("yGradingType").toString(), QStringLiteral("geometric"));
  EXPECT_DOUBLE_EQ(mesh.value("yGradingRatio").toDouble(), 1.2);
  EXPECT_EQ(mesh.value("yGradingCluster").toString(), QStringLiteral("both"));
  EXPECT_EQ(mesh.value("xGradingType").toString(), QStringLiteral("uniform"));

  mesh["xGradingType"] = QStringLiteral("geometric");
  mesh["xGradingRatio"] = 1.05;
  mesh["xGradingCluster"] = QStringLiteral("left");
  ASSERT_TRUE(editor.setMeshAndGeometry(mesh, editor.geometryConfig()));
  ASSERT_TRUE(editor.validateDraft().value("valid").toBool());
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));

  const auto reread = cfd::io::CaseReader{}.read(fixture.path());
  ASSERT_TRUE(reread.mesh.grading.has_value());
  EXPECT_EQ(reread.mesh.grading->x.type, cfd::mesh::GradingType::Geometric);
  EXPECT_DOUBLE_EQ(reread.mesh.grading->x.ratio, 1.05);
  EXPECT_EQ(reread.mesh.grading->x.cluster, cfd::mesh::GradingCluster::Start);
  EXPECT_DOUBLE_EQ(reread.mesh.grading->y.ratio, 1.2);
  EXPECT_EQ(reread.mesh.grading->y.cluster, cfd::mesh::GradingCluster::Both);
}

// An invalid ratio entered in the editor is a Mesh-section validation error
// on the offending field (the real CaseReader message), and meshCellInfo
// reports the graded sizes for the preview.
TEST(CaseEditingTest, GradingValidationAndCellInfo) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));
  QVariantMap mesh = controller.meshConfig();
  EXPECT_FALSE(mesh.value("hasGrading").toBool());
  mesh["yGradingType"] = QStringLiteral("geometric");
  mesh["yGradingRatio"] = 0.5;
  mesh["yGradingCluster"] = QStringLiteral("top");
  ASSERT_TRUE(controller.setMeshAndGeometry(mesh, controller.geometryConfig()));
  QVariantMap result = controller.validateDraft();
  EXPECT_FALSE(result.value("valid").toBool());
  EXPECT_EQ(result.value("section").toString(), QStringLiteral("Mesh"));
  EXPECT_EQ(result.value("field").toString(), QStringLiteral("grading.y.ratio"));

  mesh["yGradingRatio"] = 1.1;
  ASSERT_TRUE(controller.setMeshAndGeometry(mesh, controller.geometryConfig()));
  EXPECT_TRUE(controller.validateDraft().value("valid").toBool());

  const QVariantMap info = controller.meshCellInfo(mesh, controller.geometryConfig());
  const int ny = mesh.value("ny").toInt();
  const QVariantList yNodes = info.value("yNodes").toList();
  ASSERT_EQ(yNodes.size(), ny + 1);
  EXPECT_DOUBLE_EQ(yNodes.front().toDouble(), 0.0);
  EXPECT_DOUBLE_EQ(yNodes.back().toDouble(), 1.0);
  EXPECT_LT(info.value("minYWidth").toDouble(), info.value("maxYWidth").toDouble());
  EXPECT_DOUBLE_EQ(info.value("minXWidth").toDouble(), info.value("maxXWidth").toDouble());
  EXPECT_FALSE(info.contains("gradingError"));

  // 1000 over 4 cells: smallest cell ~1e-9 of the height, below the 1e-8
  // floor (kMinimumRelativeCellWidth).
  mesh["yGradingRatio"] = 1000.0;
  EXPECT_TRUE(controller.meshCellInfo(mesh, controller.geometryConfig()).contains("gradingError"));
}

// Editing an ungraded case's mesh page (grading keys present, both axes
// uniform) keeps it ungraded: mesh.json is saved as {type, nx, ny}.
TEST(CaseEditingTest, UngradedCaseStaysUngradedThroughTheMeshEditor) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));
  QVariantMap mesh = editor.meshConfig();
  mesh["nx"] = 10;
  ASSERT_TRUE(editor.setMeshAndGeometry(mesh, editor.geometryConfig()));
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));
  EXPECT_FALSE(cfd::io::CaseReader{}.read(fixture.path()).mesh.grading.has_value());
}

// P12-MESH-003: a multiblock (general 2D geometry) case in the GUI -- load,
// display its mesh metadata read-only, edit a named patch's boundary
// condition, validate, save (blocks, interfaces and patches bitwise
// intact), run, and the result views degrade safely (no nx x ny grid).
// A coarse variant of cases/step_channel_multiblock (104 cells) keeps the
// run short.
TEST(CaseEditingTest, MultiBlockCaseLoadsValidatesSavesAndRunsFromTheGui) {
  const CaseFixtureCopy fixture("cases/step_channel_multiblock");
  cfd::io::CaseDefinition coarse = cfd::io::CaseReader{}.read(fixture.path());
  const auto line = [](double a, double b, cfd::Index n, cfd::Index k) {
    return a + ((b - a) * static_cast<double>(k) / static_cast<double>(n));
  };
  const auto rect = [&](const char* name, double x0, double x1, double y0, double y1, cfd::Index nx,
                        cfd::Index ny) {
    cfd::io::MeshBlockConfig block{name, nx, ny, {}};
    for (cfd::Index j = 0; j <= ny; ++j) {
      for (cfd::Index i = 0; i <= nx; ++i) {
        block.vertices.push_back(cfd::Vector2{line(x0, x1, nx, i), line(y0, y1, ny, j)});
      }
    }
    return block;
  };
  coarse.mesh.blocks = {rect("upstream", 0.0, 2.0, 0.5, 1.0, 4, 2),
                        rect("upper", 2.0, 14.0, 0.5, 1.0, 24, 2),
                        rect("lower", 2.0, 14.0, 0.0, 0.5, 24, 2)};
  cfd::io::CaseWriter::write(fixture.path(), coarse);
  const auto original = cfd::io::CaseReader{}.read(fixture.path());

  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));
  const QVariantMap mesh = editor.meshConfig();
  EXPECT_EQ(mesh.value("type").toString(), QStringLiteral("multiblock"));
  EXPECT_EQ(mesh.value("cellCount").toInt(), 104);
  EXPECT_EQ(mesh.value("interfaceCount").toInt(), 2);
  const QVariantList blocks = mesh.value("blocks").toList();
  ASSERT_EQ(blocks.size(), 3);
  EXPECT_EQ(blocks[1].toMap().value("name").toString(), QStringLiteral("upper"));
  EXPECT_EQ(blocks[1].toMap().value("nx").toInt(), 24);
  EXPECT_EQ(editor.geometryConfig().value("type").toString(), QStringLiteral("mesh_defined"));
  QVariantMap boundaries = editor.boundaryConfig();
  EXPECT_EQ(boundaries.keys(), (QStringList{"bottom_wall", "inlet", "outlet", "step", "top_wall"}));

  // A mesh-page commit (nothing editable), a boundary edit on a named
  // patch, then validate and save.
  ASSERT_TRUE(editor.setMeshAndGeometry(mesh, editor.geometryConfig()));
  QVariantMap inlet = boundaries.value("inlet").toMap();
  QVariantMap velocity = inlet.value("velocity").toMap();
  velocity["valueX"] = 0.5;
  inlet["velocity"] = velocity;
  boundaries["inlet"] = inlet;
  ASSERT_TRUE(editor.setBoundaryConfig(boundaries));
  const QVariantMap validation = editor.validateDraft();
  ASSERT_TRUE(validation.value("valid").toBool())
      << validation.value("section").toString().toStdString() << ": "
      << validation.value("message").toString().toStdString();
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));

  const auto reread = cfd::io::CaseReader{}.read(fixture.path());
  EXPECT_EQ(reread.geometry.type, "mesh_defined");
  ASSERT_EQ(reread.mesh.blocks.size(), 3u);
  for (std::size_t b = 0; b < 3; ++b) {
    EXPECT_EQ(reread.mesh.blocks[b].name, original.mesh.blocks[b].name);
    EXPECT_EQ(reread.mesh.blocks[b].vertices, original.mesh.blocks[b].vertices) << b;
  }
  ASSERT_EQ(reread.mesh.interfaces.size(), 2u);
  EXPECT_EQ(reread.mesh.interfaces[1].first, original.mesh.interfaces[1].first);
  ASSERT_EQ(reread.mesh.patches.size(), 5u);
  EXPECT_EQ(reread.mesh.patches[1].sides, original.mesh.patches[1].sides);
  EXPECT_EQ(reread.boundaries.patches.at("inlet").velocity.value.x, 0.5);
  EXPECT_EQ(reread.boundaries.patches.at("step").velocity.type, "wall");

  // Run from the GUI; the structured field map / contours report nothing
  // (no nx x ny grid) instead of drawing a wrong one.
  QSignalSpy completedSpy(&editor, &SimulationController::completed);
  editor.run();
  ASSERT_TRUE(completedSpy.wait(60000));
  ASSERT_TRUE(editor.hasResults());
  const QVariantMap grid = editor.scalarFieldGrid(QStringLiteral("pressure"));
  EXPECT_EQ(grid.value("nx").toInt(), 0);
  EXPECT_TRUE(editor.contourSegments(QStringLiteral("pressure"), 8).isEmpty());
  EXPECT_EQ(cfd::app::ProjectRunner::run(fixture.path()).status,
            cfd::app::ProjectRunStatus::Converged);
}

// P12-MESH-004: the production mesh-quality report on the Mesh page and in
// the validation panel.
TEST(CaseEditingTest, MeshQualityIsReportedWhenACaseOpens) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(CaseFixtureCopy("cases/poiseuille_distorted"))));
  const QVariantMap quality = controller.meshQuality();
  EXPECT_EQ(quality.value("status").toString(), QStringLiteral("valid"));
  EXPECT_EQ(quality.value("cells").toInt(), 512);
  EXPECT_NEAR(quality.value("maximumNonOrthogonality").toDouble(), 44.76, 0.01);
  EXPECT_TRUE(quality.value("summary").toString().startsWith(QStringLiteral("valid: 512 cells")));
  bool advice = false;
  for (const QVariant& entry : quality.value("issues").toList()) {
    const QVariantMap issue = entry.toMap();
    advice =
        advice ||
        (issue.value("severity").toString() == QStringLiteral("info") &&
         issue.value("text").toString().contains(QStringLiteral("non_orthogonal_corrections")));
  }
  EXPECT_TRUE(advice);
  // Information is not a validation-panel item.
  ASSERT_TRUE(controller.validateDraft().value("valid").toBool());
  EXPECT_EQ(controller.validationIssues().size(), 0);
}

TEST(CaseEditingTest, MeshQualityWarningsAppearInTheValidationPanel) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(
      fixtureQString(CaseFixtureCopy("tests/data/cases/mesh_quality_warning_cli"))));
  ASSERT_TRUE(controller.validateDraft().value("valid").toBool());  // warnings never block
  EXPECT_EQ(controller.meshQuality().value("status").toString(),
            QStringLiteral("valid_with_warnings"));
  const QVariantList issues = controller.validationIssues();
  ASSERT_EQ(issues.size(), 1);
  const QVariantMap issue = issues.front().toMap();
  EXPECT_EQ(issue.value("severity").toString(), QStringLiteral("Warning"));
  EXPECT_EQ(issue.value("section").toString(), QStringLiteral("Mesh"));
  EXPECT_EQ(issue.value("field").toString(), QStringLiteral("expansion_ratio"));
  EXPECT_TRUE(issue.value("message").toString().contains(QStringLiteral("2.5 > 2")));
}

TEST(CaseEditingTest, InvalidMeshShowsAnInvalidMeshQualityWithTheReason) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(
      fixtureQString(CaseFixtureCopy("tests/data/cases/mesh_quality_disconnected_cli"))));
  EXPECT_EQ(controller.meshQuality().value("status").toString(), QStringLiteral("invalid"));
  EXPECT_TRUE(controller.meshQuality().value("summary").toString().contains(
      QStringLiteral("2 disconnected cell regions")));
  const QVariantMap result = controller.validateDraft();
  EXPECT_FALSE(result.value("valid").toBool());
  EXPECT_EQ(result.value("section").toString(), QStringLiteral("Mesh"));
  EXPECT_EQ(controller.meshQuality().value("status").toString(), QStringLiteral("invalid"));
  const QVariantList issues = controller.validationIssues();
  ASSERT_EQ(issues.size(), 1);
  EXPECT_EQ(issues.front().toMap().value("severity").toString(), QStringLiteral("Error"));
}

TEST(CaseEditingTest, NewCaseClearsTheMeshQuality) {
  SimulationController controller;
  ASSERT_TRUE(
      controller.openCase(fixtureQString(CaseFixtureCopy("tests/data/cases/valid_cavity"))));
  EXPECT_FALSE(controller.meshQuality().isEmpty());
  controller.newCase();
  EXPECT_TRUE(controller.meshQuality().isEmpty());
}

// P12-MESH-006 gate G9.4 -- a 3D (box, hexahedral) case in the GUI controller: it opens, the
// production validation builds a 3D mesh, nz / depth / w survive the editor commits (including the
// 2D-shaped maps a page without z fields sends) and a save round trip, a run completes through
// ProjectRunner with a "w" residual series, the 2D-only views return nothing (no exception), and
// the written 3D results reload from disk (fields.csv / residuals.csv columns located by name).
TEST(CaseEditingTest, ThreeDimensionalCaseLoadsValidatesPreservesZAndRunsFromTheGui) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cube3d_cli_smoke");
  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));

  QVariantMap mesh = editor.meshConfig();
  QVariantMap geometry = editor.geometryConfig();
  EXPECT_EQ(geometry.value("type").toString(), QStringLiteral("box"));
  EXPECT_DOUBLE_EQ(geometry.value("depth").toDouble(), 1.0);
  EXPECT_EQ(mesh.value("nz").toInt(), 6);
  EXPECT_EQ(mesh.value("cellCount").toInt(), 216);
  QVariantMap boundaries = editor.boundaryConfig();
  EXPECT_EQ(boundaries.keys(), (QStringList{"xmax", "xmin", "ymax", "ymin", "zmax", "zmin"}));
  EXPECT_DOUBLE_EQ(
      boundaries.value("ymax").toMap().value("velocity").toMap().value("valueX").toDouble(), 1.0);
  EXPECT_DOUBLE_EQ(
      boundaries.value("ymax").toMap().value("velocity").toMap().value("valueZ").toDouble(), 0.0);
  const QVariantMap cells = editor.meshCellInfo(mesh, geometry);
  EXPECT_EQ(cells.value("cellCount").toInt(), 216);
  EXPECT_DOUBLE_EQ(cells.value("dz").toDouble(), 1.0 / 6.0);

  // A commit from a page that shows no z fields (the 2D-shaped maps) keeps nz and depth ...
  ASSERT_TRUE(
      editor.setMeshAndGeometry(QVariantMap{{"type", "structured_cartesian"}, {"nx", 6}, {"ny", 6}},
                                QVariantMap{{"type", "box"}, {"length", 1.0}, {"height", 1.0}}));
  EXPECT_EQ(editor.meshConfig().value("nz").toInt(), 6);
  EXPECT_DOUBLE_EQ(editor.geometryConfig().value("depth").toDouble(), 1.0);
  // ... and the 3D mesh page's own maps set them.
  mesh = editor.meshConfig();
  geometry = editor.geometryConfig();
  mesh["nz"] = 6;
  geometry["depth"] = 1.0;
  ASSERT_TRUE(editor.setMeshAndGeometry(mesh, geometry));
  // The w component of a boundary velocity and of the initial state.
  QVariantMap lid = boundaries.value("ymax").toMap();
  QVariantMap lidVelocity = lid.value("velocity").toMap();
  lidVelocity["valueZ"] = 0.25;
  lid["velocity"] = lidVelocity;
  boundaries["ymax"] = lid;
  ASSERT_TRUE(editor.setBoundaryConfig(boundaries));
  QVariantMap initial = editor.initialConditions();
  initial["velocityZ"] = 0.1;
  ASSERT_TRUE(editor.setInitialConditions(initial));
  ASSERT_TRUE(editor.setInitialConditions(
      QVariantMap{{"velocityX", 0.0}, {"velocityY", 0.0}, {"pressure", 0.0}}));  // no velocityZ key
  EXPECT_DOUBLE_EQ(editor.initialConditions().value("velocityZ").toDouble(), 0.1);

  // Validation runs the production parse + build: a valid 3D case.
  const QVariantMap validation = editor.validateDraft();
  ASSERT_TRUE(validation.value("valid").toBool())
      << validation.value("section").toString().toStdString() << ": "
      << validation.value("message").toString().toStdString();
  EXPECT_EQ(editor.meshQuality().value("dimension").toInt(), 3);

  // Save round trip: every z quantity reaches the case files.
  ASSERT_TRUE(editor.saveAs(fixtureQString(fixture)));
  const auto reread = cfd::io::CaseReader{}.read(fixture.path());
  EXPECT_EQ(reread.geometry.type, "box");
  EXPECT_EQ(reread.geometry.depth, 1.0);
  EXPECT_EQ(reread.mesh.nz, 6u);
  EXPECT_EQ(reread.boundaries.patches.at("ymax").velocity.value, (cfd::Vector3{1.0, 0.0, 0.25}));
  EXPECT_EQ(reread.initialConditions.velocityComponents, 3);
  EXPECT_EQ(reread.initialConditions.velocity.z, 0.1);

  // Run from the GUI.
  QSignalSpy completedSpy(&editor, &SimulationController::completed);
  editor.run();
  ASSERT_TRUE(completedSpy.wait(120000));
  EXPECT_TRUE(completedSpy.front().front().toBool());  // converged
  ASSERT_TRUE(editor.hasResults());
  EXPECT_TRUE(editor.resultsThreeDimensional());
  EXPECT_EQ(editor.availableResidualSeriesNames(),
            (QStringList{"u", "v", "w", "pressure", "continuity"}));
  const auto iterations = editor.residualSeries(QStringLiteral("u")).size();
  EXPECT_GT(iterations, 0);
  EXPECT_EQ(editor.residualSeries(QStringLiteral("w")).size(), iterations);
  // The 2D-only views return nothing for a 3D result (no exception).
  EXPECT_TRUE(editor.scalarFieldGrid(QStringLiteral("pressure")).isEmpty());
  EXPECT_TRUE(editor.contourSegments(QStringLiteral("pressure"), 8).isEmpty());
  EXPECT_TRUE(editor.vectorSamples(1).isEmpty());
  EXPECT_TRUE(editor.probeAt(0.5, 0.5).isEmpty());
  EXPECT_TRUE(editor.sampleLine(QStringLiteral("pressure"), 0.0, 0.5, 1.0, 0.5, 10).isEmpty());
  EXPECT_FALSE(editor.availableFields().isEmpty());

  // The written 3D results reload from disk with the same series.
  SimulationController reopened;
  ASSERT_TRUE(reopened.openCase(fixtureQString(fixture)));
  ASSERT_TRUE(reopened.hasResults());
  EXPECT_TRUE(reopened.resultsThreeDimensional());
  EXPECT_EQ(reopened.residualSeries(QStringLiteral("w")).size(), iterations);
  EXPECT_EQ(reopened.residualSeries(QStringLiteral("w")),
            editor.residualSeries(QStringLiteral("w")));
  EXPECT_TRUE(reopened.contourSegments(QStringLiteral("pressure"), 8).isEmpty());
}

// P12-MESH-006: a 2D result is unchanged -- no "w" series, 2D views available.
TEST(CaseEditingTest, TwoDimensionalResultsHaveNoWSeries) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(fixture)));
  const QVariantMap validation = controller.validateDraft();
  ASSERT_TRUE(validation.value("valid").toBool());
  EXPECT_EQ(controller.meshQuality().value("dimension").toInt(), 2);
  EXPECT_EQ(controller.meshConfig().value("nz").toInt(), 0);
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(120000));
  ASSERT_TRUE(controller.hasResults());
  EXPECT_FALSE(controller.resultsThreeDimensional());
  EXPECT_EQ(controller.availableResidualSeriesNames(),
            (QStringList{"u", "v", "pressure", "continuity"}));
  EXPECT_TRUE(controller.residualSeries(QStringLiteral("w")).isEmpty());
  EXPECT_FALSE(controller.scalarFieldGrid(QStringLiteral("pressure")).isEmpty());
}
