// P13-003 -- CLI / GUI numerical equivalence.
//
// The pre-existing check calling itself the "CLI/GUI Round-Trip Gate" asserted
// one thing:
//
//     EXPECT_EQ(cliEquivalentRun.status, ProjectRunStatus::Converged);
//
// Two runs that converged to *different answers* would pass it. This file is
// the criterion the brief actually asks for: final fields, iteration count,
// convergence state, residual histories, mass balance and the exported files,
// compared between a run driven through the GUI's controller and a run driven
// through the CLI's entry point.
//
// BITWISE, because both arms call the identical ProjectRunner::run on
// identical inputs on one machine. Anything weaker would not distinguish "one
// execution path" from "two paths that happen to agree today", and telling
// those apart is the entire point of the item.
//
// NON-VACUITY. A harness that compared one arm with itself would pass for
// free. Every case asserts the two results came from *different clients*
// before comparing a single value: the GUI arm must have gone through
// SimulationController::run() on its worker thread (proved by the completed()
// signal and hasResults()), and the CLI arm must be a separate, direct
// ProjectRunner::run() call. Control N2 exists to show that assertion can
// fail.
#include <gtest/gtest.h>

#include <QSignalSpy>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseReader.hpp"

#include "../SimulationController.hpp"

namespace {

using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::testutil::CaseFixtureCopy;

QString fixtureQString(const CaseFixtureCopy& fixture) {
  return QString::fromStdString(fixture.path().string());
}

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// Lower the iteration budget so these run in seconds. It changes how far both
// arms get, identically; it cannot make two different answers look equal.
void setMaxIterations(const std::filesystem::path& caseDirectory, int iterations) {
  const auto path = caseDirectory / "solver.json";
  std::string text = readFile(path);
  const std::string key = "\"max_iterations\":";
  const auto at = text.find(key);
  ASSERT_NE(at, std::string::npos);
  const auto valueStart = text.find_first_of("0123456789", at + key.size());
  const auto valueEnd = text.find_first_not_of("0123456789", valueStart);
  text.replace(valueStart, valueEnd - valueStart, std::to_string(iterations));
  std::ofstream(path) << text;
}

void compareHistories(const std::vector<double>& gui, const std::vector<double>& cli,
                      const char* name) {
  ASSERT_EQ(gui.size(), cli.size()) << name << " history length";
  for (std::size_t i = 0; i < gui.size(); ++i) {
    EXPECT_TRUE(sameBits(gui[i], cli[i])) << name << "[" << i << "] " << gui[i] << " vs " << cli[i];
  }
}

// E1..E7 between two SIMPLE results, bitwise. Extracted from
// expectClientsAgree during P13 final qualification so that the G4 gate's
// GUI-checkpoint -> CLI-resume row (finding F1) compares its two arms against
// the SAME frozen contract instead of re-stating it. No criterion changed.
void expectSimpleResultsAgree(const cfd::pressure_velocity::SIMPLEResult& g,
                              const cfd::pressure_velocity::SIMPLEResult& c) {
  // E4 -- iteration count.
  EXPECT_EQ(g.iterations, c.iterations);

  // E7 -- the backend and scheme actually used, not the ones configured.
  EXPECT_EQ(g.gpuDiscretization, c.gpuDiscretization);
  EXPECT_EQ(static_cast<int>(g.faceFlux), static_cast<int>(c.faceFlux));

  // E1, E2, E3 -- the fields.
  ASSERT_EQ(g.velocity.size(), c.velocity.size());
  ASSERT_EQ(g.massFlux.size(), c.massFlux.size());
  for (cfd::Index i = 0; i < g.velocity.size(); ++i) {
    EXPECT_TRUE(sameBits(g.velocity[i].x, c.velocity[i].x)) << "velocity.x " << i;
    EXPECT_TRUE(sameBits(g.velocity[i].y, c.velocity[i].y)) << "velocity.y " << i;
    EXPECT_TRUE(sameBits(g.velocity[i].z, c.velocity[i].z)) << "velocity.z " << i;
    EXPECT_TRUE(sameBits(g.pressure[i], c.pressure[i])) << "pressure " << i;
  }
  for (cfd::Index i = 0; i < g.massFlux.size(); ++i) {
    EXPECT_TRUE(sameBits(g.massFlux[i], c.massFlux[i])) << "massFlux " << i;
  }

  // E5 -- the residual histories.
  compareHistories(g.uResidualHistory, c.uResidualHistory, "u");
  compareHistories(g.vResidualHistory, c.vResidualHistory, "v");
  compareHistories(g.wResidualHistory, c.wResidualHistory, "w");
  compareHistories(g.pressureResidualHistory, c.pressureResidualHistory, "p");
  compareHistories(g.continuityHistory, c.continuityHistory, "continuity");

  // E6 -- mass balance.
  EXPECT_TRUE(sameBits(g.globalMassImbalance, c.globalMassImbalance));
  EXPECT_TRUE(sameBits(g.finalContinuityResidual, c.finalContinuityResidual));
}

// P13 final qualification, G4 finding F2: the frozen gate's `E-thermal`
// coverage row ("a case with a thermal block -- a second solver after SIMPLE")
// was specified in acceptance_gate.md and never implemented. A row that merely
// LOADED a thermal case would be vacuous, so the caller states whether the
// thermal solve is required to have actually run.
enum class ThermalExpectation { NotRequired, MustHaveRun };

// --- P13 final qualification, G4 finding F4: client provenance -----------
//
// This file's header has always claimed:
//
//     "Every case asserts the two results came from *different clients*
//      before comparing a single value. Control N2 exists to show that
//      assertion can fail."
//
// Control N2a, written during G4, showed that no such assertion existed. The
// checks present -- hasResults() and the completed() signal -- prove the GUI
// arm RAN. They say nothing about what the SECOND arm is. Substituting the GUI
// arm for the CLI arm made every E row pass for free, which is exactly the
// failure mode the header warned about. The original UNDETECTED result is
// preserved in results/p13/final-qualification/2026-09-24/g4-cli-gui/.
//
// The repair: a result no longer travels as a bare ProjectRunResult. It travels
// tagged with the client that produced it, and the tag is written ONLY by the
// factory that drove that client. Substituting one arm for the other therefore
// carries the wrong tag with it, and the provenance assertion fires before a
// single number is compared. Provenance is never inferred from equal values,
// filenames, execution order or comments.
// `Unset` is the default deliberately: an ArmResult that no factory ever filled
// must be detectable too, not silently indistinguishable from a real CLI arm.
enum class ClientArm { Unset, Cli, Gui };

const char* armName(ClientArm arm) {
  switch (arm) {
    case ClientArm::Gui:
      return "GUI";
    case ClientArm::Cli:
      return "CLI";
    case ClientArm::Unset:
      break;
  }
  return "UNSET (no client ever produced this result)";
}

struct ArmResult {
  ClientArm arm{ClientArm::Unset};
  ProjectRunResult result;
  // The exported bytes, read while THIS arm's files were the ones on disk.
  // ResultExporter always writes <case>/results, so the second arm to run
  // overwrites the first: each arm must snapshot its own bytes as it goes, or
  // a later comparison reads one file twice.
  std::string metadata;
  std::string residuals;
  std::string fields;
};

// Out-parameter rather than a return value so ASSERT_* can be used inside.
void runGuiArm(const std::filesystem::path& caseDirectory, ArmResult& out) {
  out.arm = ClientArm::Gui;  // written here, by the code that drives the GUI

  SimulationController controller;
  ASSERT_TRUE(controller.openCase(QString::fromStdString(caseDirectory.string())));

  QSignalSpy completed(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completed.wait(120000)) << "the GUI run did not finish";
  // The GUI arm must genuinely have run through the controller.
  ASSERT_TRUE(controller.hasResults()) << "the GUI produced no results, so there is nothing to "
                                          "compare and the comparison would be vacuous";

  const auto guiRunOpt = controller.lastRunResult();
  ASSERT_TRUE(guiRunOpt.has_value()) << "the GUI reported no run result";
  out.result = *guiRunOpt;

  ASSERT_TRUE(out.result.exportSummary.has_value());
  out.metadata = readFile(out.result.exportSummary->metadataPath);
  out.residuals = readFile(out.result.exportSummary->residualsCsvPath);
  out.fields = out.result.exportSummary->fieldsCsvPath.has_value()
                   ? readFile(*out.result.exportSummary->fieldsCsvPath)
                   : std::string();
  ASSERT_FALSE(out.metadata.empty()) << "the GUI wrote no metadata, so E8 would be vacuous";
}

// The CLI's entry point, called directly -- exactly what apps/cli/main.cpp
// delegates to. This REWRITES <case>/results, which is why it runs second and
// snapshots its own bytes immediately.
void runCliArm(const std::filesystem::path& caseDirectory, ArmResult& out) {
  out.arm = ClientArm::Cli;  // written here, by the code that drives the CLI

  out.result = ProjectRunner::run(caseDirectory);

  ASSERT_TRUE(out.result.exportSummary.has_value());
  out.metadata = readFile(out.result.exportSummary->metadataPath);
  out.residuals = readFile(out.result.exportSummary->residualsCsvPath);
  out.fields = out.result.exportSummary->fieldsCsvPath.has_value()
                   ? readFile(*out.result.exportSummary->fieldsCsvPath)
                   : std::string();
  ASSERT_FALSE(out.metadata.empty()) << "the CLI wrote no metadata, so E8 would be vacuous";
}

void expectArmsAgree(const ArmResult& gui, const ArmResult& cli,
                     ThermalExpectation thermal = ThermalExpectation::NotRequired) {
  // F4 -- PROVENANCE FIRST. Fatal, and before any value is looked at, so that a
  // self-comparison fails HERE and is attributable to provenance rather than
  // surfacing later as some unrelated numeric or filesystem difference.
  ASSERT_NE(static_cast<int>(gui.arm), static_cast<int>(cli.arm))
      << "both arms were produced by the " << armName(gui.arm)
      << " client: this is a self-comparison, it passes for free, and it proves nothing";
  ASSERT_EQ(static_cast<int>(gui.arm), static_cast<int>(ClientArm::Gui))
      << "the first arm must be the GUI arm; got " << armName(gui.arm);
  ASSERT_EQ(static_cast<int>(cli.arm), static_cast<int>(ClientArm::Cli))
      << "the second arm must be the CLI arm; got " << armName(cli.arm);

  const ProjectRunResult& guiRun = gui.result;
  const ProjectRunResult& cliRun = cli.result;

  // E4 -- status and iteration count.
  EXPECT_EQ(static_cast<int>(guiRun.status), static_cast<int>(cliRun.status));
  ASSERT_TRUE(guiRun.simpleResult.has_value());
  ASSERT_TRUE(cliRun.simpleResult.has_value());
  expectSimpleResultsAgree(*guiRun.simpleResult, *cliRun.simpleResult);

  // E-thermal (F2) -- the second solver that runs after SIMPLE.
  if (thermal == ThermalExpectation::MustHaveRun) {
    // Non-vacuity, and the reason this row is not just "load a thermal case":
    // ProjectRunner runs the thermal solve only when the SIMPLE result is fully
    // finite, so a case can carry a thermal block and still never solve one.
    ASSERT_TRUE(guiRun.thermalResult.has_value())
        << "the GUI produced no thermal result, so E-thermal would be vacuous";
    ASSERT_TRUE(cliRun.thermalResult.has_value())
        << "the CLI produced no thermal result, so E-thermal would be vacuous";
    EXPECT_GT(guiRun.thermalResult->iterations, 0)
        << "the thermal solve did not iterate, so nothing thermal was exercised";
    // The temperature field must have MOVED off its uniform initial value --
    // otherwise a no-op thermal solve would satisfy every comparison below.
    const auto& t = guiRun.thermalResult->temperature;
    ASSERT_GT(t.size(), 0);
    bool moved = false;
    for (cfd::Index i = 1; i < t.size(); ++i) {
      if (!sameBits(t[i], t[0])) {
        moved = true;
        break;
      }
    }
    EXPECT_TRUE(moved) << "the temperature field is uniform, so the thermal solve did nothing";
  }
  if (guiRun.thermalResult.has_value() && cliRun.thermalResult.has_value()) {
    const auto& gt = *guiRun.thermalResult;
    const auto& ct = *cliRun.thermalResult;
    EXPECT_EQ(static_cast<int>(gt.status), static_cast<int>(ct.status));
    EXPECT_EQ(gt.iterations, ct.iterations);
    EXPECT_EQ(gt.linearIterations, ct.linearIterations);
    EXPECT_TRUE(sameBits(gt.finalResidual, ct.finalResidual));
    EXPECT_TRUE(sameBits(gt.maxTemperatureChange, ct.maxTemperatureChange));
    ASSERT_EQ(gt.temperature.size(), ct.temperature.size());
    for (cfd::Index i = 0; i < gt.temperature.size(); ++i) {
      EXPECT_TRUE(sameBits(gt.temperature[i], ct.temperature[i])) << "temperature " << i;
    }
    compareHistories(gt.residualHistory, ct.residualHistory, "thermal residual");
    compareHistories(gt.outerChangeHistory, ct.outerChangeHistory, "thermal outer change");
  }

  // E8 -- the exported files. Each arm snapshotted its own bytes while they
  // were the ones on disk, so these are two real, independently produced sets.
  EXPECT_EQ(gui.metadata, cli.metadata);
  EXPECT_EQ(gui.residuals, cli.residuals);
  EXPECT_EQ(gui.fields, cli.fields);
}

// Runs `caseDirectory` through the GUI controller and, separately, through the
// CLI's entry point, and compares everything the brief lists. The GUI arm runs
// first: the CLI arm overwrites <case>/results.
void expectClientsAgree(const std::filesystem::path& caseDirectory,
                        ThermalExpectation thermal = ThermalExpectation::NotRequired) {
  ArmResult gui;
  ArmResult cli;
  ASSERT_NO_FATAL_FAILURE(runGuiArm(caseDirectory, gui));
  ASSERT_NO_FATAL_FAILURE(runCliArm(caseDirectory, cli));
  ASSERT_NO_FATAL_FAILURE(expectArmsAgree(gui, cli, thermal));
}

}  // namespace

TEST(CliGuiEquivalenceTest, TwoDimensionalCaseAgreesBitwise) {
  CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  setMaxIterations(fixture.path(), 25);
  expectClientsAgree(fixture.path());
}

TEST(CliGuiEquivalenceTest, ThreeDimensionalCaseAgreesBitwise) {
  CaseFixtureCopy fixture("tests/data/cases/valid_duct3d_cli_smoke");
  setMaxIterations(fixture.path(), 10);
  expectClientsAgree(fixture.path());
}

TEST(CliGuiEquivalenceTest, ACaseEditedInTheGuiStillAgrees) {
  // The row that matters most: the GUI did something the CLI cannot (edit a
  // case in memory and save it), and both must still agree afterwards.
  CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  setMaxIterations(fixture.path(), 25);

  SimulationController editor;
  ASSERT_TRUE(editor.openCase(fixtureQString(fixture)));
  QVariantMap physics = editor.physicsConfig();
  physics["dynamic_viscosity"] = physics["dynamic_viscosity"].toDouble() * 1.5;
  ASSERT_TRUE(editor.setPhysicsConfig(physics));
  ASSERT_TRUE(editor.save());

  expectClientsAgree(fixture.path());
}

TEST(CliGuiEquivalenceTest, AThermalCaseAgreesBitwise) {
  // P13 final qualification, G4 finding F2. acceptance_gate.md's coverage list
  // names five rows; this is the one that was never implemented:
  //
  //     E-thermal     a case with a thermal block (a second solver after SIMPLE)
  //
  // It is the row where the run does not end at SIMPLE: ProjectRunner solves
  // SIMPLE, then hands the converged flow to the thermal solver. If the two
  // clients diverged anywhere in that hand-off, no other row would notice.
  //
  // cases/heated_cavity is used rather than a tests/data fixture because no
  // fixture under tests/data/cases carries a valid thermal block -- the only
  // one that mentions thermal is invalid_3d_thermal, which exists to be
  // rejected. No new case file is invented for this gate.
  CaseFixtureCopy fixture("cases/heated_cavity");
  setMaxIterations(fixture.path(), 20);
  expectClientsAgree(fixture.path(), ThermalExpectation::MustHaveRun);
}

// --- P13-003, group P: capability parity --------------------------------
//
// Checkpoint and resume existed since P13-001 and the plumbing already
// carried them, but nothing in the GUI could ask for them: the CLI could
// checkpoint and the GUI could not. These pin the surface AND the fact that
// it is the same surface, not a second implementation.

TEST(CliGuiCheckpointTest, TheGuiCanCheckpointAndTheFileMatchesTheClis) {
  CaseFixtureCopy guiFixture("tests/data/cases/valid_cavity");
  CaseFixtureCopy cliFixture("tests/data/cases/valid_cavity");
  setMaxIterations(guiFixture.path(), 12);
  setMaxIterations(cliFixture.path(), 12);

  const auto guiCheckpoint = guiFixture.path() / "gui.ckpt";
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(guiFixture)));
  ASSERT_TRUE(controller.setCheckpoint(QString::fromStdString(guiCheckpoint.string()), 0));
  QSignalSpy completed(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completed.wait(120000));
  EXPECT_EQ(controller.checkpointsWritten(), 1);
  EXPECT_EQ(controller.resumedFromIteration(), 0);
  ASSERT_TRUE(std::filesystem::exists(guiCheckpoint));

  // The CLI's own entry point, same case, same budget.
  const auto cliCheckpoint = cliFixture.path() / "cli.ckpt";
  cfd::app::ProjectRunOptions options;
  options.checkpointPath = cliCheckpoint;
  const ProjectRunResult cliRun = ProjectRunner::run(cliFixture.path(), options);
  ASSERT_EQ(cliRun.checkpointsWritten, 1u);

  // P3 -- byte-identical. The two clients wrote the same solver state through
  // the same writer; anything else would mean one of them took a different
  // path to it.
  EXPECT_EQ(readFile(guiCheckpoint), readFile(cliCheckpoint));
}

TEST(CliGuiCheckpointTest, ACheckpointCrossesBetweenTheClients) {
  // P4 -- written by the CLI, resumed in the GUI. A checkpoint that only its
  // own client could read would be two formats wearing one name.
  CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  setMaxIterations(fixture.path(), 8);
  const auto checkpoint = fixture.path() / "cross.ckpt";

  cfd::app::ProjectRunOptions writeOptions;
  writeOptions.checkpointPath = checkpoint;
  ASSERT_EQ(ProjectRunner::run(fixture.path(), writeOptions).checkpointsWritten, 1u);

  setMaxIterations(fixture.path(), 20);
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(fixture)));
  ASSERT_TRUE(controller.setResumeFrom(QString::fromStdString(checkpoint.string())));
  QSignalSpy completed(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completed.wait(120000));

  EXPECT_EQ(controller.resumedFromIteration(), 8);
  const auto run = controller.lastRunResult();
  ASSERT_TRUE(run.has_value());
  ASSERT_TRUE(run->simpleResult.has_value());
  EXPECT_EQ(run->simpleResult->iterations, 20u);
  EXPECT_TRUE(run->simpleResult->resumedFromContinuation);
}

TEST(CliGuiCheckpointTest, ACheckpointWrittenByTheGuiResumesInTheCli) {
  // P13 final qualification, G4 finding F1 -- the OTHER half of frozen
  // criterion P4: "A checkpoint written by the CLI resumes in the GUI, and one
  // written by the GUI resumes in the CLI." Only the first direction was ever
  // measured (ACheckpointCrossesBetweenTheClients). The second was inferred
  // from P3: the two clients' checkpoint files are byte-identical, and the CLI
  // can resume its own, so it must be able to resume the GUI's. Sound, but an
  // inference. TODO.md lists the two directions as separate line items, so this
  // measures the direction directly: ProjectRunner is handed the exact file the
  // GUI controller wrote.
  CaseFixtureCopy guiFixture("tests/data/cases/valid_cavity");
  CaseFixtureCopy cliFixture("tests/data/cases/valid_cavity");
  setMaxIterations(guiFixture.path(), 8);
  setMaxIterations(cliFixture.path(), 8);

  // The GUI writes a checkpoint through its controller.
  const auto guiCheckpoint = guiFixture.path() / "gui_written.ckpt";
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(guiFixture)));
  ASSERT_TRUE(controller.setCheckpoint(QString::fromStdString(guiCheckpoint.string()), 0));
  QSignalSpy completed(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completed.wait(120000)) << "the GUI run did not finish";
  ASSERT_EQ(controller.checkpointsWritten(), 1);
  ASSERT_TRUE(std::filesystem::exists(guiCheckpoint));

  // The CLI writes its own, to resume from as the reference.
  const auto cliCheckpoint = cliFixture.path() / "cli_written.ckpt";
  cfd::app::ProjectRunOptions writeOptions;
  writeOptions.checkpointPath = cliCheckpoint;
  ASSERT_EQ(ProjectRunner::run(cliFixture.path(), writeOptions).checkpointsWritten, 1u);

  // Attributability, not a new criterion: P3 already asserts byte-identity, and
  // checking it here means a failure below reads as "the resume diverged"
  // rather than "the inputs differed".
  EXPECT_EQ(readFile(guiCheckpoint), readFile(cliCheckpoint));

  // Both resumed through the CLI's entry point -- the direction under test.
  setMaxIterations(guiFixture.path(), 20);
  setMaxIterations(cliFixture.path(), 20);
  cfd::app::ProjectRunOptions resumeFromGui;
  resumeFromGui.resumeFrom = guiCheckpoint;
  cfd::app::ProjectRunOptions resumeFromCli;
  resumeFromCli.resumeFrom = cliCheckpoint;
  const ProjectRunResult fromGui = ProjectRunner::run(guiFixture.path(), resumeFromGui);
  const ProjectRunResult fromCli = ProjectRunner::run(cliFixture.path(), resumeFromCli);

  // The CLI genuinely consumed the GUI's file and continued from it.
  ASSERT_TRUE(fromGui.simpleResult.has_value())
      << "the CLI produced no result from the GUI's checkpoint";
  EXPECT_EQ(fromGui.resumedFromIteration, 8);
  EXPECT_TRUE(fromGui.simpleResult->resumedFromContinuation);
  EXPECT_EQ(fromGui.simpleResult->iterations, 20u);

  // Iteration/residual accounting and final numerical state, against the same
  // frozen bitwise contract the E rows use.
  ASSERT_TRUE(fromCli.simpleResult.has_value());
  EXPECT_EQ(static_cast<int>(fromGui.status), static_cast<int>(fromCli.status));
  EXPECT_EQ(fromGui.resumedFromIteration, fromCli.resumedFromIteration);
  expectSimpleResultsAgree(*fromGui.simpleResult, *fromCli.simpleResult);
}

TEST(CliGuiCheckpointTest, AnIntervalWithoutAPathIsRejectedAsTheCliRejectsIt) {
  // The same combination `cfdapp --checkpoint-every N` without --checkpoint
  // refuses. Accepting it silently would leave a user who asked for periodic
  // checkpoints with none, discovered only when the run needed resuming.
  SimulationController controller;
  EXPECT_FALSE(controller.setCheckpoint(QString(), 50));
  EXPECT_FALSE(controller.lastError().isEmpty());
  EXPECT_FALSE(controller.setCheckpoint(QStringLiteral("x.ckpt"), -1));
}

TEST(CliGuiCheckpointTest, ARunWithoutCheckpointOptionsReportsNothing) {
  CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  setMaxIterations(fixture.path(), 6);
  SimulationController controller;
  ASSERT_TRUE(controller.openCase(fixtureQString(fixture)));
  QSignalSpy completed(&controller, &SimulationController::completed);
  controller.run();
  ASSERT_TRUE(completed.wait(120000));
  EXPECT_EQ(controller.checkpointsWritten(), 0);
  EXPECT_EQ(controller.resumedFromIteration(), 0);
}
