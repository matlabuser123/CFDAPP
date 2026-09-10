#pragma once

// P5-B -- GUI Solver Workflow: the one Qt-facing controller QML drives
// (TODO.md P5 section 11) -- wraps cfd::app::CaseSession, the exact same
// production case-manager/solver-backend the CLI uses (section 0's "ONE
// SOLVER BACKEND"). Runs the (blocking) CaseSession::run() call on a
// worker std::thread so the Qt UI thread never blocks on a solve
// (section 12) -- SIMPLE's own progress/cancellation callbacks (called
// FROM that worker thread) only ever `emit` Qt signals, never touch a
// QML-visible property directly from off the UI thread; Qt's signal/
// slot machinery auto-queues an emission across threads onto the
// receiver's own thread (here, the main/UI thread SimulationController
// itself lives on) as long as the connection is left at its default
// Qt::AutoConnection, which every connect() call in SimulationController.cpp
// and every QML "onXyzChanged" handler uses -- no manual mutex/queue of
// its own is needed for that hand-off (section 63: "choose the simplest
// safe model compatible with performance").

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <mutex>
#include <thread>

#include "cfd/app/CaseSession.hpp"
#include "cfd/app/VisualizationSnapshot.hpp"
#include "cfd/core/Version.hpp"

class SimulationController : public QObject {
  Q_OBJECT
  // P5 Final Release Gate, section 15: the same one version string
  // cfd::core::versionString() already gives `cfdapp --version` and
  // CPack's own CPACK_PACKAGE_VERSION (cmake/Packaging.cmake) --
  // exposed as a constant property (no NOTIFY needed, it never changes
  // for the life of the process) so an "About" view can show it without
  // maintaining a second, separately-typed version string.
  Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
  Q_PROPERTY(QString applicationName READ applicationName CONSTANT)
  Q_PROPERTY(QString caseDirectory READ caseDirectory NOTIFY caseChanged)
  Q_PROPERTY(QString caseName READ caseName NOTIFY caseChanged)
  Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
  Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)
  Q_PROPERTY(bool canSave READ canSave NOTIFY stateChanged)
  Q_PROPERTY(bool canStop READ canStop NOTIFY stateChanged)
  Q_PROPERTY(bool isModified READ isModified NOTIFY stateChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
  Q_PROPERTY(int iteration READ iteration NOTIFY progressChanged)
  Q_PROPERTY(int maxIterations READ maxIterations NOTIFY progressChanged)
  Q_PROPERTY(double continuityResidual READ continuityResidual NOTIFY progressChanged)
  // P5 GUI visualization integration -- see VisualizationSnapshot.hpp.
  Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged)
  Q_PROPERTY(QStringList availableFields READ availableFields NOTIFY resultsChanged)
  Q_PROPERTY(QStringList availableResidualSeriesNames READ availableResidualSeriesNames NOTIFY
                 resultsChanged)
  Q_PROPERTY(QVariantMap meshBounds READ meshBounds NOTIFY resultsChanged)

 public:
  explicit SimulationController(QObject* parent = nullptr);
  ~SimulationController() override;

  [[nodiscard]] QString applicationVersion() const;
  [[nodiscard]] QString applicationName() const;
  [[nodiscard]] QString caseDirectory() const;
  [[nodiscard]] QString caseName() const;
  [[nodiscard]] QString stateName() const;
  [[nodiscard]] bool canRun() const;
  [[nodiscard]] bool canSave() const;
  [[nodiscard]] bool canStop() const;
  [[nodiscard]] bool isModified() const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] int iteration() const noexcept { return lastIteration_; }
  [[nodiscard]] int maxIterations() const noexcept { return lastMaxIterations_; }
  [[nodiscard]] double continuityResidual() const noexcept { return lastContinuityResidual_; }

  // section 11's own worked example signature -- Q_INVOKABLE so QML can
  // call these directly.
  Q_INVOKABLE bool openCase(const QString& caseDirectory);
  Q_INVOKABLE void newCase();
  Q_INVOKABLE bool save();
  Q_INVOKABLE bool saveAs(const QString& caseDirectory);
  Q_INVOKABLE bool validateCase();
  Q_INVOKABLE void run();
  Q_INVOKABLE void stop();

  // --- P5 GUI visualization integration -----------------------------------
  // All of these read a single, immutable VisualizationSnapshot (built
  // once, either right after a run completes or by loadCompletedResults()
  // below) under snapshot_'s own mutex -- never the live SIMPLEResult a
  // solve in progress might still be mutating (section 10). Every one of
  // these delegates to the already-tested include/cfd/viz/ algorithms
  // (section: "do not rewrite the existing visualization algorithms");
  // this class only adapts their C++ types to Qt/QML-friendly ones.
  [[nodiscard]] bool hasResults() const;
  [[nodiscard]] QStringList availableFields() const;
  [[nodiscard]] QStringList availableResidualSeriesNames() const;
  [[nodiscard]] QVariantMap meshBounds() const;

  // Loads a completed case's own results/ directory without solving
  // (section 9) -- called automatically after a successful openCase()
  // when that directory already has one, and callable directly to force
  // a reload. Returns false (hasResults stays whatever it was) if there
  // is nothing valid to load.
  Q_INVOKABLE bool loadCompletedResults();

  // {nx, ny, minValue, maxValue, values: [...]} in row-major cell-id
  // order (section 3) -- QML draws this on one Canvas, not one item per
  // cell.
  Q_INVOKABLE QVariantMap scalarFieldGrid(const QString& field) const;
  // [{x1,y1,x2,y2}, ...] in physical (domain) coordinates -- reuses
  // cfd::viz::extractContourSegments/automaticContourLevels directly.
  Q_INVOKABLE QVariantList contourSegments(const QString& field, int numberOfLevels) const;
  // [{x,y,vx,vy}, ...] -- vx/vy are the real physical velocity; QML
  // computes its own display arrow length from a UI-side scale slider,
  // never a rescaled copy stored here (section 25).
  Q_INVOKABLE QVariantList vectorSamples(int stride) const;
  // {field: value, ...} for every available field at the cell nearest
  // (x, y) -- read-only (section 31).
  Q_INVOKABLE QVariantMap probeAt(double x, double y) const;
  // [{x,y,value}, ...] evenly spaced from (x0,y0) to (x1,y1).
  Q_INVOKABLE QVariantList sampleLine(const QString& field, double x0, double y0, double x1,
                                      double y1, int numberOfSamples) const;
  // Writes exactly what sampleLine() above would return as
  // "x,y,value\n"-format CSV (section 7's own "allow CSV export").
  // Returns false on any I/O failure.
  Q_INVOKABLE bool exportLineSampleCsv(const QString& path, const QString& field, double x0,
                                       double y0, double x1, double y1, int numberOfSamples) const;
  // The solver's own canonical per-iteration history (section 8/27) --
  // already log-safe (each value clamped to a small positive epsilon
  // before being returned) so QML's own Math.log10() for a log-scale
  // plot never receives a non-positive input (section 8's "handle zero/
  // nonpositive residual values safely").
  Q_INVOKABLE QVariantList residualSeries(const QString& name) const;

 signals:
  void caseChanged();
  void stateChanged();
  void errorChanged();
  void started();
  // Structured progress data (section 14), not a parsed console string --
  // iteration/maxIterations/residuals straight from
  // cfd::pressure_velocity::SIMPLEIterationProgress.
  void progressChanged();
  void completed(bool converged);
  void failed(const QString& message);
  void resultsChanged();

 private:
  void joinWorkerIfAny();
  void setSnapshot(cfd::app::VisualizationSnapshot snapshot);
  [[nodiscard]] cfd::app::VisualizationSnapshot currentSnapshot() const;

  cfd::app::CaseSession session_;
  std::thread worker_;
  std::atomic<bool> running_{false};

  // Written once per completed run (or by loadCompletedResults()), read
  // by every Q_INVOKABLE above -- see this class's own header comment on
  // why a mutex (not the lock-free running_/lastIteration_ style) is the
  // right tool here: a single infrequent whole-object replace/copy, not
  // a per-iteration counter.
  mutable std::mutex snapshotMutex_;
  cfd::app::VisualizationSnapshot snapshot_;

  // Written only from the worker thread's progress callback, read only
  // from the UI thread's property getters above -- both sides go
  // through this same relaxed-is-enough-for-a-display-only-counter
  // atomic set, not a lock (section 63: a stale-by-one-iteration display
  // value is harmless; a torn read is not acceptable, hence still
  // atomic, just not mutex-guarded).
  std::atomic<int> lastIteration_{0};
  std::atomic<int> lastMaxIterations_{0};
  std::atomic<double> lastContinuityResidual_{0.0};
};
