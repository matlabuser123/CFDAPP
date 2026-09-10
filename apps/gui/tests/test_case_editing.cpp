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
