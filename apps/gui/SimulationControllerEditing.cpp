// P7-GUI -- GUI Case Editing: SimulationController's case-editing surface
// (mesh/physics/boundaries/solver/initial-conditions/case-metadata
// get+set, plus validateDraft()) -- split into its own translation unit
// from SimulationController.cpp purely for file-size hygiene (both are
// still the one SimulationController class; see its own header comment
// on why editing lives on the same controller/CaseSession as run/
// visualize rather than a second controller with a second CaseSession).
#include <cstdlib>
#include <filesystem>
#include <random>
#include <sstream>

#include "CaseModelAdapter.hpp"
#include "SimulationController.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/io/case/BoundaryVocabulary.hpp"
#include "cfd/io/case/PhysicsVocabulary.hpp"

using cfd::io::CaseDefinition;

namespace {

// RAII scratch directory for validateDraft()'s own CaseWriter/CaseReader
// round-trip -- always removed on scope exit, success or failure, so a
// user validating a draft repeatedly never accumulates junk under the
// system temp directory.
class ScratchDirectory {
 public:
  ScratchDirectory() {
    std::random_device rd;
    std::ostringstream name;
    name << "cfdapp_gui_validate_" << rd();
    path_ = std::filesystem::temp_directory_path() / name.str();
  }
  ~ScratchDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);  // best-effort; a leftover temp dir is harmless.
  }
  ScratchDirectory(const ScratchDirectory&) = delete;
  ScratchDirectory& operator=(const ScratchDirectory&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

// Maps a CaseConfigurationError/IOError's own message (which always
// starts with the failing file's path, see JsonUtil.cpp's own
// throwConfigError) to the editor page a QML validation panel should
// navigate to -- "Case" is the fallback for a message naming none of the
// five (e.g. a CaseBuilder-level construction error, which has no file
// path at all).
QString sectionForMessage(const QString& message) {
  struct Entry {
    const char* fileName;
    const char* section;
  };
  static constexpr Entry kEntries[] = {
      {"geometry.json", "Geometry"},     {"mesh.json", "Mesh"},     {"physics.json", "Physics"},
      {"boundaries.json", "Boundaries"}, {"solver.json", "Solver"}, {"case.json", "Case"},
  };
  for (const auto& entry : kEntries) {
    if (message.contains(QString::fromUtf8(entry.fileName))) {
      return QString::fromUtf8(entry.section);
    }
  }
  return QStringLiteral("Case");
}

// Pulls the offending field name out of a throwConfigError()-produced
// message (JsonUtil.cpp: "<path>: field \"<field>\" must satisfy ...") --
// purely parsing the one canonical message format every case-file
// validation error already uses, never a second field-detection pass of
// its own. Empty when the message names no field (e.g. a
// CaseBuilder-level construction error, which throwConfigError never
// produced in the first place).
QString fieldForMessage(const QString& message) {
  static const QString kMarker = QStringLiteral("field \"");
  const int start = message.indexOf(kMarker);
  if (start < 0) return QString();
  const int nameStart = start + kMarker.size();
  const int nameEnd = message.indexOf(QLatin1Char('"'), nameStart);
  if (nameEnd < 0) return QString();
  return message.mid(nameStart, nameEnd - nameStart);
}

QVariantMap validResult() {
  return QVariantMap{
      {"valid", true}, {"section", QString()}, {"field", QString()}, {"message", QString()}};
}
QVariantMap invalidResult(const QString& section, const QString& message) {
  return QVariantMap{{"valid", false},
                     {"section", section},
                     {"field", fieldForMessage(message)},
                     {"message", message}};
}

QStringList toStringList(const auto& views) {
  QStringList list;
  for (std::string_view v : views) list.push_back(QString::fromUtf8(v.data(), int(v.size())));
  return list;
}

}  // namespace

QVariantMap SimulationController::caseMetadata() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->caseConfig) : QVariantMap{};
}
QVariantMap SimulationController::meshConfig() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->mesh) : QVariantMap{};
}
QVariantMap SimulationController::geometryConfig() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->geometry) : QVariantMap{};
}
QVariantMap SimulationController::physicsConfig() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->physics) : QVariantMap{};
}
QVariantMap SimulationController::boundaryConfig() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->boundaries) : QVariantMap{};
}
QVariantMap SimulationController::solverConfig() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->solver) : QVariantMap{};
}
QVariantMap SimulationController::initialConditions() const {
  const auto def = session_.caseDefinition();
  return def.has_value() ? cfd::gui::toVariant(def->initialConditions) : QVariantMap{};
}

bool SimulationController::setCaseMetadata(const QVariantMap& metadata) {
  auto def = session_.caseDefinition();
  if (!def.has_value()) return false;
  def->caseConfig = cfd::gui::caseConfigFromVariant(metadata, def->caseConfig);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

bool SimulationController::setMeshAndGeometry(const QVariantMap& mesh,
                                              const QVariantMap& geometry) {
  auto def = session_.caseDefinition();
  if (!def.has_value()) return false;
  def->mesh = cfd::gui::meshConfigFromVariant(mesh);
  def->geometry = cfd::gui::geometryConfigFromVariant(geometry);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

bool SimulationController::setPhysicsConfig(const QVariantMap& physics) {
  auto def = session_.caseDefinition();
  if (!def.has_value()) return false;
  def->physics = cfd::gui::physicsConfigFromVariant(physics);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

bool SimulationController::setBoundaryConfig(const QVariantMap& boundaries) {
  auto def = session_.caseDefinition();
  if (!def.has_value()) return false;
  def->boundaries = cfd::gui::boundaryConfigFromVariant(boundaries);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

bool SimulationController::setSolverConfig(const QVariantMap& solver) {
  auto def = session_.caseDefinition();
  if (!def.has_value()) return false;
  def->solver = cfd::gui::solverConfigFromVariant(solver);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

bool SimulationController::setInitialConditions(const QVariantMap& initialConditions) {
  auto def = session_.caseDefinition();
  if (!def.has_value()) return false;
  def->initialConditions = cfd::gui::initialConditionsFromVariant(initialConditions);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

QVariantMap SimulationController::validateDraft() {
  const auto def = session_.caseDefinition();
  if (!def.has_value()) {
    validationStatus_ = invalidResult(QStringLiteral("Case"), QStringLiteral("no case is loaded"));
    emit validationChanged();
    return validationStatus_;
  }

  try {
    ScratchDirectory scratch;
    cfd::io::CaseWriter::write(scratch.path(), *def);
    const CaseDefinition reread = cfd::io::CaseReader{}.read(scratch.path());
    (void)cfd::io::CaseBuilder{}.build(reread);
  } catch (const cfd::Error& e) {
    const QString message = QString::fromStdString(e.what());
    validationStatus_ = invalidResult(sectionForMessage(message), message);
    emit validationChanged();
    return validationStatus_;
  }

  validationStatus_ = validResult();
  emit validationChanged();
  return validationStatus_;
}

QVariantList SimulationController::validationIssues() const {
  if (validationStatus_.value(QStringLiteral("valid")).toBool()) return {};
  QVariantMap issue;
  issue["severity"] = QStringLiteral("Error");
  issue["section"] = validationStatus_.value(QStringLiteral("section"));
  issue["field"] = validationStatus_.value(QStringLiteral("field"));
  issue["message"] = validationStatus_.value(QStringLiteral("message"));
  return {issue};
}

QStringList SimulationController::velocityBoundaryTypes() const {
  return toStringList(cfd::io::kVelocityTypes);
}
QStringList SimulationController::pressureBoundaryTypes() const {
  return toStringList(cfd::io::kPressureTypes);
}
QStringList SimulationController::temperatureBoundaryTypes() const {
  return toStringList(cfd::io::kTemperatureTypes);
}
QStringList SimulationController::turbulenceModelNames() const {
  return toStringList(cfd::io::kTurbulenceModels);
}

bool SimulationController::velocityTypeHasValue(const QString& type) const {
  const std::string t = type.toStdString();
  return t == "moving_wall" || t == "inlet";
}
bool SimulationController::temperatureTypeHasValue(const QString& type) const {
  const std::string t = type.toStdString();
  return t == "fixed_temperature" || t == "heat_flux";
}

QVariantMap SimulationController::meshCellInfo(const QVariantMap& mesh,
                                               const QVariantMap& geometry) const {
  const int nx = mesh.value(QStringLiteral("nx")).toInt();
  const int ny = mesh.value(QStringLiteral("ny")).toInt();
  const double length = geometry.value(QStringLiteral("length")).toDouble();
  const double height = geometry.value(QStringLiteral("height")).toDouble();

  QVariantMap info;
  const qint64 cellCount = static_cast<qint64>(nx) * static_cast<qint64>(ny);
  info["cellCount"] = static_cast<double>(cellCount);
  info["dx"] = nx > 0 ? length / nx : 0.0;
  info["dy"] = ny > 0 ? height / ny : 0.0;
  // A UX nicety only -- see this method's own header comment. 250,000
  // cells is well past every case shipped under cases/ (the largest,
  // compressible_validation, is 24x6=144) but nowhere near a hard
  // production limit.
  info["isLarge"] = cellCount > 250000;
  return info;
}
