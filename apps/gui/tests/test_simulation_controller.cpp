// P5-B -- GUI Solver Workflow, section 59: "Add tests at the
// backend/controller level... do not try to test all QML behavior
// solely through pixel screenshots." SimulationController is plain
// QObject logic (no windowing), so these run under a bare
// QCoreApplication -- no display, no offscreen platform plugin needed --
// and exercise the exact worker-thread/queued-signal hand-off the real
// QML UI depends on (QSignalSpy::wait() pumps a real Qt event loop,
// which is what actually delivers a cross-thread queued signal).
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QSignalSpy>

#include "cfd/core/Version.hpp"

#include "../SimulationController.hpp"

namespace {

// A single process-wide QCoreApplication -- Qt requires exactly one,
// constructed before any QObject with signals/slots is used, and it
// must outlive every test.
class QtEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    static int argc = 1;
    static char argv0[] = "CFDAppGuiTests";
    static char* argv[] = {argv0};
    app_ = new QCoreApplication(argc, argv);
  }
  void TearDown() override { delete app_; }

 private:
  QCoreApplication* app_ = nullptr;
};

[[maybe_unused]] ::testing::Environment* const kQtEnvironment =
    ::testing::AddGlobalTestEnvironment(new QtEnvironment());

}  // namespace

TEST(SimulationControllerTest, StartsWithNoCaseLoaded) {
  SimulationController controller;
  EXPECT_EQ(controller.stateName(), QStringLiteral("Empty"));
  EXPECT_FALSE(controller.canRun());
  EXPECT_TRUE(controller.caseName().isEmpty());
}

// P5 Final Release Gate, section 15: the GUI's own "About" version
// string is the exact same one-authoritative-source cfd::core::
// versionString() the CLI's own --version and CPack's package filename
// already read from -- never a second, separately-typed string.
TEST(SimulationControllerTest, ApplicationVersionMatchesCoreVersionString) {
  SimulationController controller;
  EXPECT_EQ(controller.applicationVersion(), QString::fromStdString(cfd::core::versionString()));
  EXPECT_FALSE(controller.applicationName().isEmpty());
}

TEST(SimulationControllerTest, NewCaseEmitsCaseChangedAndBecomesLoaded) {
  SimulationController controller;
  QSignalSpy caseChangedSpy(&controller, &SimulationController::caseChanged);
  controller.newCase();
  EXPECT_EQ(caseChangedSpy.count(), 1);
  EXPECT_EQ(controller.stateName(), QStringLiteral("Loaded"));
  EXPECT_TRUE(controller.canRun());
}

TEST(SimulationControllerTest, OpenValidCaseSucceeds) {
  SimulationController controller;
  EXPECT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  EXPECT_EQ(controller.caseName(), QStringLiteral("Test Cavity 4x4"));
  EXPECT_EQ(controller.stateName(), QStringLiteral("Loaded"));
}

TEST(SimulationControllerTest, OpenMissingCaseFailsAndReportsError) {
  SimulationController controller;
  QSignalSpy errorSpy(&controller, &SimulationController::errorChanged);
  EXPECT_FALSE(controller.openCase(QStringLiteral("this/does/not/exist")));
  EXPECT_GE(errorSpy.count(), 1);
  EXPECT_FALSE(controller.lastError().isEmpty());
}

// The real cross-thread path: run() launches a worker thread; its
// progress/completion signals are delivered to this (main) thread's
// event loop, which QSignalSpy::wait() pumps.
TEST(SimulationControllerTest, RunOnAValidCaseEmitsStartedThenCompleted) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));

  QSignalSpy startedSpy(&controller, &SimulationController::started);
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  QSignalSpy progressSpy(&controller, &SimulationController::progressChanged);

  controller.run();
  ASSERT_EQ(startedSpy.count(), 1);  // emitted synchronously before the worker thread starts.
  EXPECT_TRUE(controller.canStop());

  ASSERT_TRUE(completedSpy.wait(15000)) << "solve did not complete within 15s";
  ASSERT_EQ(completedSpy.count(), 1);
  EXPECT_TRUE(completedSpy.at(0).at(0).toBool());  // converged == true.
  EXPECT_GT(progressSpy.count(), 0);
  EXPECT_EQ(controller.stateName(), QStringLiteral("Completed"));
  EXPECT_FALSE(controller.canStop());
  EXPECT_TRUE(controller.canRun());  // re-run is allowed once Completed.
}

// --- P5 GUI visualization integration -----------------------------------

TEST(SimulationControllerTest, NoResultsBeforeAnyRunOrLoad) {
  SimulationController controller;
  EXPECT_FALSE(controller.hasResults());
  EXPECT_TRUE(controller.availableFields().isEmpty());
}

TEST(SimulationControllerTest, RunPublishesAUsableResultSnapshot) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));

  QSignalSpy resultsSpy(&controller, &SimulationController::resultsChanged);
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  ASSERT_TRUE(resultsSpy.wait(1000) || resultsSpy.count() > 0);

  EXPECT_TRUE(controller.hasResults());
  const QStringList fields = controller.availableFields();
  EXPECT_TRUE(fields.contains(QStringLiteral("pressure")));
  EXPECT_TRUE(fields.contains(QStringLiteral("velocity_magnitude")));
  EXPECT_FALSE(fields.contains(QStringLiteral("temperature")));  // valid_cavity has no thermal block.

  const QVariantMap grid = controller.scalarFieldGrid(QStringLiteral("pressure"));
  ASSERT_FALSE(grid.isEmpty());
  EXPECT_EQ(grid["nx"].toInt(), 4);
  EXPECT_EQ(grid["ny"].toInt(), 4);
  const QVariantList values = grid["values"].toList();
  EXPECT_EQ(values.size(), 16);

  // Missing/unknown field handled safely (empty map/list, never a crash
  // or a thrown exception reaching QML).
  EXPECT_TRUE(controller.scalarFieldGrid(QStringLiteral("temperature")).isEmpty());
  EXPECT_TRUE(controller.scalarFieldGrid(QStringLiteral("nonexistent")).isEmpty());
}

TEST(SimulationControllerTest, ContoursVectorsProbeAndLineSampleWorkAfterARun) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(15000));
  ASSERT_TRUE(controller.hasResults());

  const QVariantList contours = controller.contourSegments(QStringLiteral("pressure"), 5);
  for (const auto& seg : contours) {
    const QVariantMap m = seg.toMap();
    EXPECT_TRUE(m.contains("x1") && m.contains("y1") && m.contains("x2") && m.contains("y2"));
  }

  const QVariantList vectors = controller.vectorSamples(1);
  EXPECT_EQ(vectors.size(), 16);
  for (const auto& v : vectors) {
    const QVariantMap m = v.toMap();
    EXPECT_TRUE(m.contains("vx") && m.contains("vy"));
  }

  const QVariantMap bounds = controller.meshBounds();
  const double midX = (bounds["minX"].toDouble() + bounds["maxX"].toDouble()) / 2.0;
  const double midY = (bounds["minY"].toDouble() + bounds["maxY"].toDouble()) / 2.0;
  const QVariantMap probe = controller.probeAt(midX, midY);
  ASSERT_FALSE(probe.isEmpty());
  EXPECT_TRUE(probe.contains("pressure"));
  EXPECT_TRUE(probe.contains("speed"));
  EXPECT_FALSE(probe.contains("temperature"));  // not a thermal case.

  const QVariantList line = controller.sampleLine(QStringLiteral("pressure"), bounds["minX"].toDouble(),
                                                  midY, bounds["maxX"].toDouble(), midY, 5);
  EXPECT_EQ(line.size(), 5);
}

TEST(SimulationControllerTest, ExportLineSampleCsvWritesAReadableFile) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(15000));

  const QString path = QStringLiteral("tests/data/cases/valid_cavity/results/probe_export_test.csv");
  ASSERT_TRUE(controller.exportLineSampleCsv(path, QStringLiteral("pressure"), 0.0, 0.5, 1.0, 0.5, 5));

  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString content = QString::fromUtf8(file.readAll());
  EXPECT_TRUE(content.startsWith(QStringLiteral("x,y,value\n")));
  EXPECT_EQ(content.count('\n'), 6);  // header + 5 samples.
  file.remove();
}

TEST(SimulationControllerTest, ResidualHistoryIsAvailableAfterARun) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completedSpy.wait(15000));

  const QStringList series = controller.availableResidualSeriesNames();
  EXPECT_TRUE(series.contains(QStringLiteral("continuity")));
  const QVariantList history = controller.residualSeries(QStringLiteral("continuity"));
  EXPECT_FALSE(history.isEmpty());
  for (const auto& v : history) {
    EXPECT_GT(v.toDouble(), 0.0);  // log-safe -- never zero/negative (section 8).
  }
  EXPECT_TRUE(controller.residualSeries(QStringLiteral("nonexistent")).isEmpty());
}

TEST(SimulationControllerTest, OpeningACaseWithExistingResultsAutoLoadsThem) {
  SimulationController producer;
  ASSERT_TRUE(producer.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  QSignalSpy completedSpy(&producer, &SimulationController::completed);
  producer.run();
  ASSERT_TRUE(completedSpy.wait(15000));  // writes tests/data/cases/valid_cavity/results/.

  // A second, independent controller opening the same (now-solved) case
  // directory sees its results immediately, without running anything --
  // section 9's own "without rerunning the solver".
  SimulationController viewer;
  ASSERT_TRUE(viewer.openCase(QStringLiteral("tests/data/cases/valid_cavity")));
  EXPECT_TRUE(viewer.hasResults());
  EXPECT_FALSE(viewer.canStop());  // never entered Running.
  EXPECT_TRUE(viewer.availableFields().contains(QStringLiteral("pressure")));
}

TEST(SimulationControllerTest, StopDuringARunLeadsToCancelledState) {
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QStringLiteral("tests/data/cases/valid_cavity")));

  QSignalSpy progressSpy(&controller, &SimulationController::progressChanged);
  QSignalSpy stateSpy(&controller, &SimulationController::stateChanged);
  QSignalSpy completedSpy(&controller, &SimulationController::completed);
  QSignalSpy failedSpy(&controller, &SimulationController::failed);

  controller.run();
  ASSERT_TRUE(progressSpy.wait(15000)) << "no progress signal arrived before stop()";
  controller.stop();

  // Wait for the terminal stateChanged (Running -> Cancelled); poll via
  // repeated short waits since we don't know exactly which stateChanged
  // index is the terminal one.
  bool reachedTerminal = false;
  for (int attempt = 0; attempt < 50 && !reachedTerminal; ++attempt) {
    stateSpy.wait(500);
    reachedTerminal = (controller.stateName() == QStringLiteral("Cancelled"));
  }
  EXPECT_TRUE(reachedTerminal) << "state was: " << controller.stateName().toStdString();
  EXPECT_EQ(completedSpy.count(), 0);
  EXPECT_EQ(failedSpy.count(), 0);  // cancellation is not reported as a failure -- section 13.
}
