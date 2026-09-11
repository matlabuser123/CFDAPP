#include "SimulationController.hpp"

#include <QFile>
#include <QIODevice>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/viz/FieldProbe.hpp"
#include "cfd/viz/MarchingSquares.hpp"
#include "cfd/viz/VectorSampling.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::app::CaseState;
using cfd::app::ProjectRunOptions;
using cfd::app::ProjectRunStatus;
using cfd::app::VisualizationSnapshot;
using cfd::pressure_velocity::SIMPLEIterationProgress;

namespace {

QString stateLabel(CaseState state) {
  switch (state) {
    case CaseState::Empty:
      return QStringLiteral("Empty");
    case CaseState::Loaded:
      return QStringLiteral("Loaded");
    case CaseState::Modified:
      return QStringLiteral("Modified");
    case CaseState::Validated:
      return QStringLiteral("Validated");
    case CaseState::Running:
      return QStringLiteral("Running");
    case CaseState::Completed:
      return QStringLiteral("Completed");
    case CaseState::Failed:
      return QStringLiteral("Failed");
    case CaseState::Cancelled:
      return QStringLiteral("Cancelled");
  }
  return QStringLiteral("Unknown");
}

// section 8's own "handle zero/nonpositive residual values safely" --
// clamped once here (C++, testable) rather than left to QML's own
// Math.log10() to silently produce -Infinity/NaN.
constexpr double kLogSafeEpsilon = 1e-300;
double logSafe(double value) { return std::max(value, kLogSafeEpsilon); }

std::vector<cfd::viz::GridPoint> toGridPoints(const std::vector<Vector2>& points) {
  std::vector<cfd::viz::GridPoint> gridPoints;
  gridPoints.reserve(points.size());
  for (const auto& p : points) gridPoints.push_back(cfd::viz::GridPoint{p.x, p.y});
  return gridPoints;
}

// Explorer's "Copy as path" wraps the result in double quotes, and a
// pasted/typed path can pick up stray leading/trailing whitespace -- a
// case directory is trimmed and unquoted the same way a shell would
// before it ever reaches CaseReader, so a straight paste from Explorer
// just works instead of failing as "case directory not found" against a
// literal quote-including path.
QString sanitizeCaseDirectory(const QString& raw) {
  QString trimmed = raw.trimmed();
  if (trimmed.size() >= 2 && trimmed.front() == QLatin1Char('"') &&
      trimmed.back() == QLatin1Char('"')) {
    trimmed = trimmed.mid(1, trimmed.size() - 2).trimmed();
  }
  return trimmed;
}

}  // namespace

SimulationController::SimulationController(QObject* parent) : QObject(parent) {}

SimulationController::~SimulationController() {
  // section 13/64: never abandon a running worker thread on teardown --
  // ask it to stop at its own next safe point, then wait for it, so the
  // case's result files are never left mid-write.
  session_.requestCancel();
  joinWorkerIfAny();
}

void SimulationController::joinWorkerIfAny() {
  if (worker_.joinable()) worker_.join();
}

void SimulationController::setSnapshot(VisualizationSnapshot snapshot) {
  {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = std::move(snapshot);
  }
  emit resultsChanged();
}

VisualizationSnapshot SimulationController::currentSnapshot() const {
  std::lock_guard<std::mutex> lock(snapshotMutex_);
  return snapshot_;  // a whole-object copy -- see this class's own header comment on why.
}

QString SimulationController::applicationVersion() const {
  return QString::fromStdString(cfd::core::versionString());
}

QString SimulationController::applicationName() const {
  return QString::fromUtf8(cfd::core::projectName().data(),
                           static_cast<int>(cfd::core::projectName().size()));
}

QString SimulationController::caseDirectory() const {
  const auto dir = session_.directory();
  return dir.has_value() ? QString::fromStdString(dir->string()) : QString();
}

QString SimulationController::caseName() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? QString::fromStdString(def->caseConfig.name) : QString();
}

QString SimulationController::stateName() const { return stateLabel(session_.state()); }
bool SimulationController::canRun() const { return session_.canRun(); }
bool SimulationController::canSave() const { return session_.canSave(); }
// Deliberately the controller's own running_ flag, not
// session_.canStop() -- running_ is set synchronously in run() before
// the worker thread starts, while session_'s own Running state is only
// set once that thread actually reaches CaseSession::run() (section 12:
// a tiny, otherwise-real window where the UI's Stop button would
// wrongly read as disabled immediately after Run was clicked).
bool SimulationController::canStop() const { return running_.load(); }
bool SimulationController::isModified() const { return session_.isModified(); }

QString SimulationController::lastError() const {
  return QString::fromStdString(session_.lastError());
}

bool SimulationController::openCase(const QString& caseDirectory) {
  const bool ok = session_.open(sanitizeCaseDirectory(caseDirectory).toStdString());
  // P7-GUI: a freshly opened/created case has no validation history of
  // its own yet -- a stale error from whatever case was open before
  // would be actively misleading here.
  validationStatus_ = QVariantMap{
      {"valid", true}, {"section", QString()}, {"field", QString()}, {"message", QString()}};
  emit validationChanged();
  emit caseChanged();
  emit stateChanged();
  if (!ok) {
    emit errorChanged();
    setSnapshot(VisualizationSnapshot{});
    return false;
  }
  // section 9: a case opened with an existing results/ directory (from
  // a prior CLI or GUI run) is immediately post-processable without
  // solving -- silently a no-op (setSnapshot(invalid)) if there is none
  // yet, never an error.
  if (!loadCompletedResults()) setSnapshot(VisualizationSnapshot{});
  return true;
}

void SimulationController::newCase() {
  session_.newCase();
  validationStatus_ = QVariantMap{
      {"valid", true}, {"section", QString()}, {"field", QString()}, {"message", QString()}};
  emit validationChanged();
  emit caseChanged();
  emit stateChanged();
  setSnapshot(VisualizationSnapshot{});
}

bool SimulationController::save() {
  const bool ok = session_.save();
  emit stateChanged();
  if (!ok) emit errorChanged();
  return ok;
}

bool SimulationController::saveAs(const QString& caseDirectory) {
  const bool ok = session_.saveAs(sanitizeCaseDirectory(caseDirectory).toStdString());
  emit caseChanged();
  emit stateChanged();
  if (!ok) emit errorChanged();
  return ok;
}

bool SimulationController::validateCase() {
  const bool ok = session_.validate();
  emit stateChanged();
  // P7-GUI: keeps validationStatus() in sync with this pre-existing
  // entry point too, not only the newer validateDraft() (see its own
  // header comment) -- session_.validate() alone only ever reaches
  // CaseBuilder::build(), so a failure here never names a *.json file
  // (sectionForMessage()'s own fallback, "Case", is exactly right for
  // it) -- SimulationControllerEditing.cpp.
  validationStatus_ = ok ? QVariantMap{{"valid", true},
                                       {"section", QString()},
                                       {"field", QString()},
                                       {"message", QString()}}
                         : QVariantMap{{"valid", false},
                                       {"section", QStringLiteral("Case")},
                                       {"field", QString()},
                                       {"message", lastError()}};
  emit validationChanged();
  if (!ok) emit errorChanged();
  return ok;
}

void SimulationController::run() {
  if (running_.load() || !session_.canRun()) return;
  joinWorkerIfAny();  // a prior run's worker (already finished) is joined before starting a new
                      // one.

  running_.store(true);
  emit started();
  emit stateChanged();

  worker_ = std::thread([this]() {
    ProjectRunOptions options;
    // Called from this worker thread -- only ever stores plain atomics
    // and `emit`s (Qt auto-queues the cross-thread delivery onto this
    // controller's own UI thread, see this class's own header comment).
    options.progressCallback = [this](const SIMPLEIterationProgress& progress) {
      lastIteration_.store(static_cast<int>(progress.iteration));
      lastMaxIterations_.store(static_cast<int>(progress.maxIterations));
      lastContinuityResidual_.store(progress.continuityResidual);
      emit progressChanged();
    };

    const auto result = session_.run(options);
    running_.store(false);
    emit stateChanged();
    emit errorChanged();
    // P5 GUI visualization integration, section 10: build the snapshot
    // from this worker thread's own already-finished `result` (a plain
    // value, not the live solver state) -- setSnapshot() only ever
    // publishes a complete, immutable snapshot under its own mutex,
    // never a field the solver might still be mutating.
    setSnapshot(cfd::app::buildSnapshot(result));
    // Cancelled is not a failure from the user's own point of view --
    // they asked for it (section 13) -- so it gets neither completed()
    // nor failed(), only the stateChanged() above (stateName() ==
    // "Cancelled"); a QML view distinguishes it from a real failure by
    // reading that property, not by an error banner.
    if (result.status == ProjectRunStatus::Converged ||
        result.status == ProjectRunStatus::DidNotConverge) {
      emit completed(result.status == ProjectRunStatus::Converged);
    } else if (result.status != ProjectRunStatus::Cancelled) {
      const QString message = result.errorMessage.empty()
                                  ? QStringLiteral("Solve did not complete successfully")
                                  : QString::fromStdString(result.errorMessage);
      emit failed(message);
    }
  });
}

void SimulationController::stop() { session_.requestCancel(); }

// --- P5 GUI visualization integration ---------------------------------

bool SimulationController::hasResults() const {
  std::lock_guard<std::mutex> lock(snapshotMutex_);
  return snapshot_.valid;
}

QStringList SimulationController::availableFields() const {
  QStringList fields;
  for (const auto& name : currentSnapshot().availableScalarFields()) {
    fields.push_back(QString::fromStdString(name));
  }
  return fields;
}

QStringList SimulationController::availableResidualSeriesNames() const {
  QStringList names;
  for (const auto& name : currentSnapshot().availableResidualSeries()) {
    names.push_back(QString::fromStdString(name));
  }
  return names;
}

QVariantMap SimulationController::meshBounds() const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantMap bounds;
  if (!snapshot.valid || snapshot.points.empty()) {
    bounds["minX"] = 0.0;
    bounds["minY"] = 0.0;
    bounds["maxX"] = 1.0;
    bounds["maxY"] = 1.0;
    return bounds;
  }
  double minX = std::numeric_limits<double>::infinity();
  double minY = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double maxY = -std::numeric_limits<double>::infinity();
  for (const auto& p : snapshot.points) {
    minX = std::min(minX, p.x);
    minY = std::min(minY, p.y);
    maxX = std::max(maxX, p.x);
    maxY = std::max(maxY, p.y);
  }
  bounds["minX"] = minX;
  bounds["minY"] = minY;
  bounds["maxX"] = maxX;
  bounds["maxY"] = maxY;
  return bounds;
}

bool SimulationController::loadCompletedResults() {
  const auto dir = session_.directory();
  if (!dir.has_value()) return false;
  VisualizationSnapshot snapshot = cfd::app::loadSnapshotFromResults(*dir / "results");
  if (!snapshot.valid) return false;
  setSnapshot(std::move(snapshot));
  return true;
}

QVariantMap SimulationController::scalarFieldGrid(const QString& field) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantMap grid;
  const std::vector<Real>* values =
      snapshot.valid ? snapshot.scalarField(field.toStdString()) : nullptr;
  if (values == nullptr) return grid;

  // Every value here already passed buildSnapshot()'s/
  // loadSnapshotFromResults()'s own all-finite gate (a snapshot is never
  // `valid` otherwise), so a plain min/max is enough -- no NaN-filtering
  // needed on top of cfd::viz::FieldStatistics's own (that helper takes
  // a cfd::fields::ScalarField, not the plain std::vector this class
  // works with throughout; not worth a conversion just for this).
  const auto [minIt, maxIt] = std::minmax_element(values->begin(), values->end());
  grid["nx"] = static_cast<int>(snapshot.nx);
  grid["ny"] = static_cast<int>(snapshot.ny);
  grid["minValue"] = values->empty() ? 0.0 : *minIt;
  grid["maxValue"] = values->empty() ? 0.0 : *maxIt;
  QVariantList values_;
  values_.reserve(static_cast<int>(values->size()));
  for (Real v : *values) values_.push_back(v);
  grid["values"] = values_;
  return grid;
}

QVariantList SimulationController::contourSegments(const QString& field, int numberOfLevels) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantList segments;
  if (!snapshot.valid || numberOfLevels <= 0) return segments;
  const std::vector<Real>* values = snapshot.scalarField(field.toStdString());
  if (values == nullptr || snapshot.nx < 2 || snapshot.ny < 2) return segments;

  const auto coordinates = toGridPoints(snapshot.points);
  const auto levels = cfd::viz::automaticContourLevels(*values, numberOfLevels);
  for (Real level : levels) {
    const auto levelSegments =
        cfd::viz::extractContourSegments(snapshot.nx, snapshot.ny, *values, coordinates, level);
    for (const auto& s : levelSegments) {
      QVariantMap seg;
      seg["x1"] = s.start.x;
      seg["y1"] = s.start.y;
      seg["x2"] = s.end.x;
      seg["y2"] = s.end.y;
      segments.push_back(seg);
    }
  }
  return segments;
}

QVariantList SimulationController::vectorSamples(int stride) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantList samples;
  if (!snapshot.valid || stride < 1) return samples;
  const auto raw = cfd::viz::sampleVectorFieldRaw(snapshot.points, snapshot.velocityX,
                                                  snapshot.velocityY, static_cast<Index>(stride));
  for (const auto& s : raw) {
    QVariantMap sample;
    sample["x"] = s.position.x;
    sample["y"] = s.position.y;
    sample["vx"] = s.vector.x;
    sample["vy"] = s.vector.y;
    samples.push_back(sample);
  }
  return samples;
}

QVariantMap SimulationController::probeAt(double x, double y) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantMap result;
  if (!snapshot.valid || snapshot.points.empty()) return result;

  const auto index = cfd::viz::nearestIndex(snapshot.points, Vector2{x, y});
  if (!index.has_value()) return result;
  const auto i = static_cast<std::size_t>(*index);

  result["x"] = snapshot.points[i].x;
  result["y"] = snapshot.points[i].y;
  result["u"] = snapshot.velocityX[i];
  result["v"] = snapshot.velocityY[i];
  result["speed"] = snapshot.velocityMagnitude[i];
  result["pressure"] = snapshot.pressure[i];
  if (snapshot.temperature.has_value()) result["temperature"] = (*snapshot.temperature)[i];
  return result;
}

QVariantList SimulationController::sampleLine(const QString& field, double x0, double y0, double x1,
                                              double y1, int numberOfSamples) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantList samples;
  if (!snapshot.valid || numberOfSamples < 2) return samples;
  const std::vector<Real>* values = snapshot.scalarField(field.toStdString());
  if (values == nullptr) return samples;

  const auto raw = cfd::viz::sampleLineRaw(snapshot.points, *values, Vector2{x0, y0},
                                           Vector2{x1, y1}, static_cast<Index>(numberOfSamples));
  for (const auto& s : raw) {
    QVariantMap sample;
    sample["x"] = s.queryPoint.x;
    sample["y"] = s.queryPoint.y;
    sample["value"] = s.value;
    samples.push_back(sample);
  }
  return samples;
}

bool SimulationController::exportLineSampleCsv(const QString& path, const QString& field, double x0,
                                               double y0, double x1, double y1,
                                               int numberOfSamples) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  if (!snapshot.valid || numberOfSamples < 2) return false;
  const std::vector<Real>* values = snapshot.scalarField(field.toStdString());
  if (values == nullptr) return false;

  std::vector<cfd::viz::LineSample> samples;
  try {
    samples = cfd::viz::sampleLineRaw(snapshot.points, *values, Vector2{x0, y0}, Vector2{x1, y1},
                                      static_cast<Index>(numberOfSamples));
  } catch (const cfd::Error&) {
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
  QTextStream out(&file);
  out << "x,y,value\n";
  for (const auto& s : samples) {
    out << s.queryPoint.x << ',' << s.queryPoint.y << ',' << s.value << '\n';
  }
  return true;
}

QVariantList SimulationController::residualSeries(const QString& name) const {
  const VisualizationSnapshot snapshot = currentSnapshot();
  QVariantList series;
  if (!snapshot.valid) return series;
  const std::vector<Real>* history = snapshot.residualSeries(name.toStdString());
  if (history == nullptr) return series;
  series.reserve(static_cast<int>(history->size()));
  for (Real v : *history) series.push_back(logSafe(v));
  return series;
}
