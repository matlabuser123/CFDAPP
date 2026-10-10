#include "cfd/io/CheckpointReader.hpp"

#include <string>
#include <vector>

#include "case/JsonUtil.hpp"
#include "cfd/core/Vector3.hpp"

namespace cfd::io {

using cfd::pressure_velocity::SIMPLECheckpoint;
using cfd::pressure_velocity::SIMPLEIterationRecord;
using cfd::pressure_velocity::validateSIMPLECheckpoint;

namespace {

std::vector<Real> readRealArray(const nlohmann::json& node, const std::filesystem::path& path,
                                std::string_view field, std::string_view label) {
  detail::requireField(node, path, field, label);
  const auto& value = node.at(std::string(field));
  if (!value.is_array()) {
    detail::throwConfigError(path, label, "be an array of numbers",
                             detail::describeJsonValue(value));
  }
  std::vector<Real> result;
  result.reserve(value.size());
  for (const auto& element : value) {
    if (!element.is_number()) {
      detail::throwConfigError(path, label, "contain only numbers",
                               detail::describeJsonValue(element));
    }
    // An out-of-finite-range literal never reaches here: readJsonFile's own
    // parse fails first. validateSIMPLECheckpoint stays the single place that
    // decides what counts as a valid value.
    result.push_back(element.get<Real>());
  }
  return result;
}

void requireLength(const std::vector<Real>& values, Index expected,
                   const std::filesystem::path& path, std::string_view label) {
  if (values.size() != expected) {
    detail::throwConfigError(path, label, "have " + std::to_string(expected) + " entries",
                             "length " + std::to_string(values.size()));
  }
}

}  // namespace

SIMPLECheckpoint CheckpointReader::read(const std::filesystem::path& path,
                                        const cfd::mesh::Mesh& mesh) {
  const nlohmann::json doc = detail::readJsonFile(path);
  detail::requireObject(doc, path);

  SIMPLECheckpoint checkpoint;
  checkpoint.formatVersion =
      static_cast<std::uint32_t>(detail::getRequiredIndex(doc, path, "format_version"));

  detail::requireField(doc, path, "application");
  const auto& application = doc.at("application");
  detail::requireObject(application, path, "application");
  checkpoint.applicationVersion =
      detail::getRequiredString(application, path, "version", "application.version");
  checkpoint.backend =
      detail::getRequiredString(application, path, "backend", "application.backend");

  detail::requireField(doc, path, "identity");
  const auto& identity = doc.at("identity");
  detail::requireObject(identity, path, "identity");
  checkpoint.caseFingerprint =
      detail::getRequiredString(identity, path, "case_fingerprint", "identity.case_fingerprint");
  checkpoint.meshFingerprint =
      detail::getRequiredString(identity, path, "mesh_fingerprint", "identity.mesh_fingerprint");
  checkpoint.cellCount =
      detail::getRequiredIndex(identity, path, "cell_count", "identity.cell_count");
  checkpoint.faceCount =
      detail::getRequiredIndex(identity, path, "face_count", "identity.face_count");
  checkpoint.dimension =
      static_cast<int>(detail::getRequiredIndex(identity, path, "dimension", "identity.dimension"));

  detail::requireField(doc, path, "state");
  const auto& state = doc.at("state");
  detail::requireObject(state, path, "state");
  checkpoint.completedIterations =
      detail::getRequiredIndex(state, path, "completed_iterations", "state.completed_iterations");
  checkpoint.status = detail::getRequiredString(state, path, "status", "state.status");

  detail::requireField(doc, path, "fields");
  const auto& fields = doc.at("fields");
  detail::requireObject(fields, path, "fields");
  const std::vector<Real> pressure = readRealArray(fields, path, "pressure", "fields.pressure");
  const std::vector<Real> velocityX =
      readRealArray(fields, path, "velocity_x", "fields.velocity_x");
  const std::vector<Real> velocityY =
      readRealArray(fields, path, "velocity_y", "fields.velocity_y");
  const std::vector<Real> velocityZ =
      readRealArray(fields, path, "velocity_z", "fields.velocity_z");
  const std::vector<Real> massFlux = readRealArray(fields, path, "mass_flux", "fields.mass_flux");

  // Every array is checked against the counts the document itself declares,
  // so a truncated field array is a schema error here rather than a size
  // mismatch discovered later against the mesh.
  requireLength(pressure, checkpoint.cellCount, path, "fields.pressure");
  requireLength(velocityX, checkpoint.cellCount, path, "fields.velocity_x");
  requireLength(velocityY, checkpoint.cellCount, path, "fields.velocity_y");
  requireLength(velocityZ, checkpoint.cellCount, path, "fields.velocity_z");
  requireLength(massFlux, checkpoint.faceCount, path, "fields.mass_flux");

  checkpoint.pressure = cfd::fields::ScalarField(pressure.size());
  for (Index i = 0; i < pressure.size(); ++i) checkpoint.pressure[i] = pressure[i];
  checkpoint.velocity = cfd::fields::VectorField(velocityX.size());
  for (Index i = 0; i < velocityX.size(); ++i) {
    checkpoint.velocity[i] = Vector3{velocityX[i], velocityY[i], velocityZ[i]};
  }
  checkpoint.massFlux = cfd::fields::SurfaceField(massFlux.size());
  for (Index i = 0; i < massFlux.size(); ++i) checkpoint.massFlux[i] = massFlux[i];

  detail::requireField(doc, path, "history");
  const auto& history = doc.at("history");
  detail::requireObject(history, path, "history");
  const std::vector<Real> u = readRealArray(history, path, "u", "history.u");
  const std::vector<Real> v = readRealArray(history, path, "v", "history.v");
  const std::vector<Real> pressureHistory =
      readRealArray(history, path, "pressure", "history.pressure");
  const std::vector<Real> continuity =
      readRealArray(history, path, "continuity", "history.continuity");
  const std::vector<Real> imbalance =
      readRealArray(history, path, "global_imbalance", "history.global_imbalance");
  const Index n = checkpoint.completedIterations;
  requireLength(u, n, path, "history.u");
  requireLength(v, n, path, "history.v");
  requireLength(pressureHistory, n, path, "history.pressure");
  requireLength(continuity, n, path, "history.continuity");
  requireLength(imbalance, n, path, "history.global_imbalance");

  // Optional by schema, but present-or-absent is itself checked: `w` must be
  // there for a 3D checkpoint and absent for a 2D one, which
  // validateSIMPLECheckpoint then cross-checks against `dimension`.
  std::vector<Real> w;
  const bool hasW = history.contains("w");
  if (hasW) {
    w = readRealArray(history, path, "w", "history.w");
    requireLength(w, n, path, "history.w");
  }
  std::vector<Real> turbulence;
  const bool hasTurbulence = history.contains("turbulence");
  if (hasTurbulence) {
    turbulence = readRealArray(history, path, "turbulence", "history.turbulence");
    requireLength(turbulence, n, path, "history.turbulence");
  }

  checkpoint.history.reserve(n);
  for (Index i = 0; i < n; ++i) {
    SIMPLEIterationRecord record;
    record.u = u[i];
    record.v = v[i];
    record.pressure = pressureHistory[i];
    record.continuity = continuity[i];
    record.globalImbalance = imbalance[i];
    if (hasW) record.w = w[i];
    if (hasTurbulence) record.turbulence = turbulence[i];
    checkpoint.history.push_back(record);
  }

  // The single validation entry point -- unsupported version, non-finite
  // value, size mismatch, dimension/history disagreement, and mesh identity.
  // No partial acceptance past this point.
  validateSIMPLECheckpoint(checkpoint, mesh);
  return checkpoint;
}

void CheckpointReader::requireCaseFingerprint(const SIMPLECheckpoint& checkpoint,
                                              const std::string& caseFingerprint,
                                              const std::filesystem::path& path) {
  if (checkpoint.caseFingerprint != caseFingerprint) {
    throw InvalidArgumentError(
        "CheckpointReader: case identity mismatch in " + path.string() +
        " -- the checkpoint was "
        "written for case fingerprint " +
        checkpoint.caseFingerprint + " but this case is " + caseFingerprint +
        " (the case configuration changed; only solver.max_iterations may differ)");
  }
}

}  // namespace cfd::io
