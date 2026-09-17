// P7-GUI -- GUI Case Editing: SimulationController's case-editing surface
// (mesh/physics/boundaries/solver/initial-conditions/case-metadata
// get+set, plus validateDraft()) -- split into its own translation unit
// from SimulationController.cpp purely for file-size hygiene (both are
// still the one SimulationController class; see its own header comment
// on why editing lives on the same controller/CaseSession as run/
// visualize rather than a second controller with a second CaseSession).
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <sstream>
#include <vector>

#include "CaseModelAdapter.hpp"
#include "SimulationController.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/io/case/BoundaryVocabulary.hpp"
#include "cfd/io/case/PhysicsVocabulary.hpp"
#include "cfd/mesh/MeshGrading.hpp"

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
  def->mesh = cfd::gui::meshConfigFromVariant(mesh, def->mesh);
  def->geometry = cfd::gui::geometryConfigFromVariant(geometry, def->geometry);
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
  def->initialConditions =
      cfd::gui::initialConditionsFromVariant(initialConditions, def->initialConditions);
  session_.setCaseDefinition(std::move(*def));
  emit caseChanged();
  emit stateChanged();
  return true;
}

void SimulationController::refreshMeshQuality() {
  meshQuality_.clear();
  const auto def = session_.caseDefinition();
  if (!def.has_value()) return;
  try {
    const auto built = cfd::io::CaseBuilder{}.build(*def);
    meshQuality_ = cfd::gui::toVariant(built.meshQuality);
    // P12-MESH-006: the dimension of the mesh the production pipeline actually built (2 or 3).
    meshQuality_["dimension"] = built.mesh.dimension();
  } catch (const cfd::Error& e) {
    meshQuality_ = QVariantMap{{"status", QStringLiteral("invalid")},
                               {"summary", QString::fromStdString(e.what())}};
  }
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
    const auto built = cfd::io::CaseBuilder{}.build(reread);
    meshQuality_ = cfd::gui::toVariant(built.meshQuality);
    meshQuality_["dimension"] = built.mesh.dimension();  // P12-MESH-006: 2 or 3
  } catch (const cfd::Error& e) {
    const QString message = QString::fromStdString(e.what());
    validationStatus_ = invalidResult(sectionForMessage(message), message);
    if (message.contains(QStringLiteral("mesh is invalid"))) {
      meshQuality_ = QVariantMap{{"status", QStringLiteral("invalid")}, {"summary", message}};
    }
    emit validationChanged();
    return validationStatus_;
  }

  validationStatus_ = validResult();
  emit validationChanged();
  return validationStatus_;
}

QVariantList SimulationController::validationIssues() const {
  if (validationStatus_.value(QStringLiteral("valid")).toBool()) {
    // P12-MESH-004: a valid case may still carry mesh-quality warnings --
    // listed (severity "Warning", section "Mesh") so the shared validation
    // panel shows them; informational items stay on the Mesh page.
    QVariantList warnings;
    for (const QVariant& entry : meshQuality_.value(QStringLiteral("issues")).toList()) {
      const QVariantMap item = entry.toMap();
      if (item.value(QStringLiteral("severity")).toString() != QStringLiteral("warning")) continue;
      warnings.push_back(QVariantMap{{"severity", QStringLiteral("Warning")},
                                     {"section", QStringLiteral("Mesh")},
                                     {"field", item.value(QStringLiteral("metric"))},
                                     {"message", item.value(QStringLiteral("text"))}});
    }
    return warnings;
  }
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

  // P12-MESH-006: a box geometry (3D) adds nz cells over the depth.
  const int nz = mesh.value(QStringLiteral("nz")).toInt();
  const double depth = geometry.value(QStringLiteral("depth")).toDouble();

  QVariantMap info;
  const qint64 cellCount =
      static_cast<qint64>(nx) * static_cast<qint64>(ny) * (nz > 0 ? static_cast<qint64>(nz) : 1);
  info["cellCount"] = static_cast<double>(cellCount);
  info["dx"] = nx > 0 ? length / nx : 0.0;
  info["dy"] = ny > 0 ? height / ny : 0.0;
  if (nz > 0) info["dz"] = depth / nz;
  // A UX nicety only -- see this method's own header comment. 250,000
  // cells is well past every case shipped under cases/ (the largest,
  // compressible_validation, is 24x6=144) but nowhere near a hard
  // production limit.
  info["isLarge"] = cellCount > 250000;

  // P12-MESH-002: the graded cell sizes the real mesh will have
  // (cfd::mesh::gradedNodeCoordinates -- the same function CaseReader and
  // MeshGeometry use), min/max width per axis and the node positions as
  // fractions of the axis (for the preview; up to 2000 cells per axis). A
  // grading the library rejects is reported as gradingError, and
  // validateDraft() reports it as the Mesh-section error.
  const cfd::io::MeshConfig config = cfd::gui::meshConfigFromVariant(mesh);
  const cfd::io::MeshGradingConfig grading = config.grading.value_or(cfd::io::MeshGradingConfig{});
  for (const bool xAxis : {true, false}) {
    const int cells = xAxis ? nx : ny;
    const double extent = xAxis ? length : height;
    if (cells <= 0 || !(extent > 0.0)) continue;
    const QString axis = xAxis ? QStringLiteral("x") : QStringLiteral("y");
    try {
      const std::vector<cfd::Real> nodes = cfd::mesh::gradedNodeCoordinates(
          static_cast<cfd::Index>(cells), extent, xAxis ? grading.x : grading.y);
      double smallest = extent;
      double largest = 0.0;
      QVariantList fractions;
      for (std::size_t k = 0; k + 1 < nodes.size(); ++k) {
        smallest = std::min(smallest, nodes[k + 1] - nodes[k]);
        largest = std::max(largest, nodes[k + 1] - nodes[k]);
      }
      if (cells <= 2000) {
        for (const double node : nodes) fractions.push_back(node / extent);
      }
      info["min" + axis.toUpper() + "Width"] = smallest;
      info["max" + axis.toUpper() + "Width"] = largest;
      info[axis + "Nodes"] = fractions;
    } catch (const cfd::InvalidArgumentError& e) {
      info["gradingError"] = QString::fromStdString(e.what());
    }
  }
  return info;
}
