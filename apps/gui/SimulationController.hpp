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

  // --- P7-GUI -- Case Editing ---------------------------------------------
  // One property per cfd::io::CaseDefinition section (see
  // CaseModelAdapter.hpp's own header comment for the exact QVariantMap
  // shape each carries) -- QML's editor pages bind to these read-only
  // properties and commit an edit through the matching setXyz()
  // Q_INVOKABLE below, never by mutating the map returned here in place
  // (QVariantMap is a value type; mutating a QML-side copy does nothing
  // to caseDefinition() until setXyz() is called). All seven re-read
  // caseChanged (the same signal openCase()/newCase()/setXyz() below
  // already emit) -- one authoritative case model, not one copy per page
  // (the task's own "GUI State Model" requirement).
  Q_PROPERTY(QVariantMap caseMetadata READ caseMetadata NOTIFY caseChanged)
  Q_PROPERTY(QVariantMap meshConfig READ meshConfig NOTIFY caseChanged)
  Q_PROPERTY(QVariantMap geometryConfig READ geometryConfig NOTIFY caseChanged)
  Q_PROPERTY(QVariantMap physicsConfig READ physicsConfig NOTIFY caseChanged)
  Q_PROPERTY(QVariantMap boundaryConfig READ boundaryConfig NOTIFY caseChanged)
  Q_PROPERTY(QVariantMap solverConfig READ solverConfig NOTIFY caseChanged)
  Q_PROPERTY(QVariantMap initialConditions READ initialConditions NOTIFY caseChanged)
  // The most recent validateCase()/validateDraft() outcome -- {valid:
  // bool, section: string, field: string, message: string} -- {valid:
  // true} (empty section/field/message) before either has ever been
  // called. Not recomputed on every keystroke (each call is a real
  // parse+build round-trip, see validateDraft()'s own header comment); a
  // QML editor calls validateDraft() explicitly (typically on blur/
  // tab-away or a "Validate" action) for the "live validation" the task
  // asks for.
  Q_PROPERTY(QVariantMap validationStatus READ validationStatus NOTIFY validationChanged)
  // validationStatus() above, wrapped as a 0-or-1-element list of
  // {severity, section, field, message} -- the shape a central
  // validation panel binds a Repeater to. Always at most one element:
  // CaseReader/CaseBuilder both fail fast (throw on the first problem
  // found), so there is no second, independent GUI-side check that could
  // find additional issues on its own without becoming exactly the
  // "GUI-only validation path that could disagree with production
  // validation" this task forbids (see validateDraft()'s own header
  // comment) -- this property exists purely to give QML one consistent
  // "list of issues" shape to render (empty when valid, one entry when
  // not), not to promise multi-error aggregation the underlying
  // validation pipeline does not do.
  Q_PROPERTY(QVariantList validationIssues READ validationIssues NOTIFY validationChanged)

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

  // --- P7-GUI -- Case Editing ----------------------------------------------
  [[nodiscard]] QVariantMap caseMetadata() const;
  [[nodiscard]] QVariantMap meshConfig() const;
  [[nodiscard]] QVariantMap geometryConfig() const;
  [[nodiscard]] QVariantMap physicsConfig() const;
  [[nodiscard]] QVariantMap boundaryConfig() const;
  [[nodiscard]] QVariantMap solverConfig() const;
  [[nodiscard]] QVariantMap initialConditions() const;
  [[nodiscard]] QVariantMap validationStatus() const { return validationStatus_; }
  [[nodiscard]] QVariantList validationIssues() const;

  // Each commits a structural QVariantMap -> typed cfd::io::* conversion
  // (CaseModelAdapter.hpp) into the session's in-memory CaseDefinition
  // via CaseSession::setCaseDefinition() (-> Modified, emits
  // caseChanged/stateChanged) -- never physics validation, see
  // CaseModelAdapter.hpp's own header comment; returns false (a no-op)
  // only when no case is loaded yet (mirrors CaseSession::
  // setCaseDefinition()'s own documented "caller bug" no-op). Mesh and
  // geometry commit together (one Q_INVOKABLE, not two) because the mesh
  // editor's own live cell-spacing display needs both at once and a
  // single caseChanged emission per user edit is simpler for QML to
  // react to than two in immediate succession.
  Q_INVOKABLE bool setCaseMetadata(const QVariantMap& metadata);
  Q_INVOKABLE bool setMeshAndGeometry(const QVariantMap& mesh, const QVariantMap& geometry);
  Q_INVOKABLE bool setPhysicsConfig(const QVariantMap& physics);
  Q_INVOKABLE bool setBoundaryConfig(const QVariantMap& boundaries);
  Q_INVOKABLE bool setSolverConfig(const QVariantMap& solver);
  Q_INVOKABLE bool setInitialConditions(const QVariantMap& initialConditions);

  // The real production validation, exercised end to end: writes the
  // current in-memory CaseDefinition to a scratch temp directory via
  // CaseWriter, reads it back via CaseReader (the exact parser CLI/
  // production use, including every cross-file check -- "exactly four
  // patches", "alpha required iff multiphase enabled", etc.), then runs
  // CaseBuilder::build() on the result (the same runtime-construction
  // checks session_.validate() alone already exercises) -- strictly more
  // coverage than validateCase()/session_.validate() alone, since a
  // filesystem round-trip also re-exercises CaseReader's own JSON-level
  // rules on a definition that only ever existed in memory before now.
  // On success: validationStatus() reads {valid: true}. On the first
  // error found: {valid: false, section, message} -- CaseReader/
  // CaseBuilder both fail fast (throw on the first problem), so this is
  // never a multi-error list, the same one-at-a-time limitation the CLI
  // itself has (section: "the GUI must not have a separate weaker
  // validation path" -- this has exactly the same power, not less, not
  // invented to be more). `section` is one of "Mesh", "Geometry",
  // "Physics", "Boundaries", "Solver", or "Case", derived from which
  // *.json file the failing check's own path names. Also returned
  // directly (not only through the property) so a caller can act on the
  // result without a second read. No-op (returns {valid: false, message:
  // "no case is loaded"}) if nothing is loaded yet.
  Q_INVOKABLE QVariantMap validateDraft();

  // --- P7-GUI -- BC/turbulence vocabulary (QML dropdown population) -------
  // The exact type vocabularies BoundaryConfigParser.cpp/
  // PhysicsConfigParser.cpp themselves enforce (cfd/io/case/
  // BoundaryVocabulary.hpp, PhysicsVocabulary.hpp) -- never a second,
  // hand-typed list that could drift from what the production parser
  // actually accepts.
  Q_INVOKABLE QStringList velocityBoundaryTypes() const;
  Q_INVOKABLE QStringList pressureBoundaryTypes() const;
  Q_INVOKABLE QStringList temperatureBoundaryTypes() const;
  Q_INVOKABLE QStringList turbulenceModelNames() const;
  // Whether `type` (one of velocityBoundaryTypes()/
  // temperatureBoundaryTypes()) carries a "value" field -- so
  // BoundaryEditor.qml knows whether to show the value control (e.g.
  // "wall" and "adiabatic" have none).
  Q_INVOKABLE bool velocityTypeHasValue(const QString& type) const;
  Q_INVOKABLE bool temperatureTypeHasValue(const QString& type) const;

  // Pure arithmetic display helper for the mesh editor -- cellCount =
  // nx*ny, dx = length/nx, dy = height/ny (0 when nx/ny is 0, never a
  // divide-by-zero crash), plus a GUI-only, non-blocking "this is a
  // large mesh" warning (cellCount > 250,000 -- a UX nicety, not a
  // production limit: MeshConfigParser.cpp itself enforces no upper
  // bound on nx/ny, only nx > 0 and ny > 0, so this warning never
  // becomes a validateDraft() error). Never itself a validity check --
  // nx <= 0/ny <= 0/length <= 0/height <= 0 are reported through
  // validateDraft() like every other field, not invented here.
  Q_INVOKABLE QVariantMap meshCellInfo(const QVariantMap& mesh, const QVariantMap& geometry) const;

 public:
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
  // P7-GUI: emitted whenever validationStatus() changes (validateCase()/
  // validateDraft() both emit this in addition to stateChanged()/
  // errorChanged() where applicable).
  void validationChanged();

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

  // P7-GUI: {valid: true} until validateCase()/validateDraft() first
  // runs -- read only from the UI thread (neither editing nor validation
  // ever happens from the worker thread run() uses), so no mutex is
  // needed the way snapshot_ above has one.
  QVariantMap validationStatus_{
      {"valid", true}, {"section", QString()}, {"field", QString()}, {"message", QString()}};
};
