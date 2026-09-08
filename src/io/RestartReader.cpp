#include "cfd/io/RestartReader.hpp"

#include <string>
#include <vector>

#include "case/JsonUtil.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"

namespace cfd::io {

using cfd::solver::RestartSnapshot;
using cfd::solver::validateRestartSnapshot;

namespace {

std::vector<Real> readRealArray(const nlohmann::json& node, const std::filesystem::path& path,
                                std::string_view field) {
  detail::requireField(node, path, field);
  const auto& value = node.at(std::string(field));
  if (!value.is_array()) {
    detail::throwConfigError(path, field, "be an array of numbers",
                             detail::describeJsonValue(value));
  }
  std::vector<Real> result;
  result.reserve(value.size());
  for (const auto& element : value) {
    if (!element.is_number()) {
      detail::throwConfigError(path, field, "contain only numbers",
                               detail::describeJsonValue(element));
    }
    // Not this parsing step's job to reject an out-of-finite-range
    // number -- readJsonFile's own parse already fails first for that
    // (nlohmann::json throws json::out_of_range while parsing, before
    // this function is even reached); a value that legitimately reaches
    // here is exactly what a JSON number can represent, and
    // validateRestartSnapshot is still the single place that decides
    // what counts as a *valid* one.
    result.push_back(element.get<Real>());
  }
  return result;
}

}  // namespace

RestartSnapshot RestartReader::read(const std::filesystem::path& path,
                                    const cfd::mesh::Mesh& mesh) {
  const nlohmann::json doc = detail::readJsonFile(path);
  detail::requireObject(doc, path);

  RestartSnapshot snapshot;
  snapshot.formatVersion =
      static_cast<std::uint32_t>(detail::getRequiredIndex(doc, path, "format_version"));

  detail::requireField(doc, path, "state");
  const auto& stateNode = doc.at("state");
  detail::requireObject(stateNode, path, "state");
  snapshot.time = detail::getRequiredReal(stateNode, path, "time", "state.time");
  snapshot.step = detail::getRequiredIndex(stateNode, path, "step", "state.step");
  snapshot.deltaT = detail::getRequiredReal(stateNode, path, "delta_t", "state.delta_t");

  detail::requireField(doc, path, "mesh");
  const auto& meshNode = doc.at("mesh");
  detail::requireObject(meshNode, path, "mesh");
  snapshot.cellCount = detail::getRequiredIndex(meshNode, path, "cell_count", "mesh.cell_count");
  snapshot.faceCount = detail::getRequiredIndex(meshNode, path, "face_count", "mesh.face_count");
  snapshot.meshFingerprint =
      detail::getRequiredString(meshNode, path, "fingerprint", "mesh.fingerprint");

  detail::requireField(doc, path, "fields");
  const auto& fieldsNode = doc.at("fields");
  detail::requireObject(fieldsNode, path, "fields");
  const std::vector<Real> pressure = readRealArray(fieldsNode, path, "pressure");
  const std::vector<Real> velocityX = readRealArray(fieldsNode, path, "velocity_x");
  const std::vector<Real> velocityY = readRealArray(fieldsNode, path, "velocity_y");
  const std::vector<Real> massFlux = readRealArray(fieldsNode, path, "mass_flux");

  if (velocityX.size() != velocityY.size()) {
    detail::throwConfigError(path, "fields.velocity_y", "have the same length as velocity_x",
                             "length " + std::to_string(velocityY.size()) +
                                 " vs velocity_x length " + std::to_string(velocityX.size()));
  }

  snapshot.pressure = cfd::fields::ScalarField(pressure.size());
  for (Index i = 0; i < pressure.size(); ++i) snapshot.pressure[i] = pressure[i];

  snapshot.velocity = cfd::fields::VectorField(velocityX.size());
  for (Index i = 0; i < velocityX.size(); ++i) {
    snapshot.velocity[i] = Vector2{velocityX[i], velocityY[i]};
  }

  snapshot.massFlux = cfd::fields::SurfaceField(massFlux.size());
  for (Index i = 0; i < massFlux.size(); ++i) snapshot.massFlux[i] = massFlux[i];

  // The single validation entry point (RestartSnapshot.hpp) -- rejects
  // an unsupported format version, non-finite values, size mismatches
  // against cellCount/faceCount, and mesh-identity mismatch against
  // `mesh`. No partial acceptance of an invalid snapshot past this
  // point.
  validateRestartSnapshot(snapshot, mesh);
  return snapshot;
}

}  // namespace cfd::io
