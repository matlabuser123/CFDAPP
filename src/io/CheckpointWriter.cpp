#include "cfd/io/CheckpointWriter.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "cfd/core/Exception.hpp"

namespace cfd::io {

using cfd::pressure_velocity::SIMPLECheckpoint;

void CheckpointWriter::write(const std::filesystem::path& path,
                             const SIMPLECheckpoint& checkpoint) {
  // Never put an invalid checkpoint on disk for a reader to discover later.
  cfd::pressure_velocity::validateSIMPLECheckpoint(checkpoint);

  nlohmann::json doc;
  doc["format_version"] = checkpoint.formatVersion;
  doc["application"]["version"] = checkpoint.applicationVersion;
  doc["application"]["backend"] = checkpoint.backend;

  doc["identity"]["case_fingerprint"] = checkpoint.caseFingerprint;
  doc["identity"]["mesh_fingerprint"] = checkpoint.meshFingerprint;
  doc["identity"]["cell_count"] = checkpoint.cellCount;
  doc["identity"]["face_count"] = checkpoint.faceCount;
  doc["identity"]["dimension"] = checkpoint.dimension;

  doc["state"]["completed_iterations"] = checkpoint.completedIterations;
  doc["state"]["status"] = checkpoint.status;

  nlohmann::json pressure = nlohmann::json::array();
  nlohmann::json velocityX = nlohmann::json::array();
  nlohmann::json velocityY = nlohmann::json::array();
  nlohmann::json velocityZ = nlohmann::json::array();
  for (Index i = 0; i < checkpoint.pressure.size(); ++i) {
    pressure.push_back(checkpoint.pressure[i]);
  }
  for (Index i = 0; i < checkpoint.velocity.size(); ++i) {
    velocityX.push_back(checkpoint.velocity[i].x);
    velocityY.push_back(checkpoint.velocity[i].y);
    velocityZ.push_back(checkpoint.velocity[i].z);
  }
  nlohmann::json massFlux = nlohmann::json::array();
  for (Index i = 0; i < checkpoint.massFlux.size(); ++i) {
    massFlux.push_back(checkpoint.massFlux[i]);
  }
  doc["fields"]["pressure"] = std::move(pressure);
  doc["fields"]["velocity_x"] = std::move(velocityX);
  doc["fields"]["velocity_y"] = std::move(velocityY);
  doc["fields"]["velocity_z"] = std::move(velocityZ);
  doc["fields"]["mass_flux"] = std::move(massFlux);

  // The outer residual sample sequence. One array per component rather than
  // an array of objects: it is a fifth of the bytes on a long run, and the
  // reader cross-checks every length against completed_iterations anyway.
  nlohmann::json u = nlohmann::json::array();
  nlohmann::json v = nlohmann::json::array();
  nlohmann::json pressureHistory = nlohmann::json::array();
  nlohmann::json continuity = nlohmann::json::array();
  nlohmann::json imbalance = nlohmann::json::array();
  nlohmann::json w = nlohmann::json::array();
  nlohmann::json turbulence = nlohmann::json::array();
  const bool hasW = !checkpoint.history.empty() && checkpoint.history.front().w.has_value();
  const bool hasTurbulence =
      !checkpoint.history.empty() && checkpoint.history.front().turbulence.has_value();
  for (const auto& record : checkpoint.history) {
    u.push_back(record.u);
    v.push_back(record.v);
    pressureHistory.push_back(record.pressure);
    continuity.push_back(record.continuity);
    imbalance.push_back(record.globalImbalance);
    if (hasW) w.push_back(*record.w);
    if (hasTurbulence) turbulence.push_back(*record.turbulence);
  }
  doc["history"]["u"] = std::move(u);
  doc["history"]["v"] = std::move(v);
  doc["history"]["pressure"] = std::move(pressureHistory);
  doc["history"]["continuity"] = std::move(continuity);
  doc["history"]["global_imbalance"] = std::move(imbalance);
  if (hasW) doc["history"]["w"] = std::move(w);
  if (hasTurbulence) doc["history"]["turbulence"] = std::move(turbulence);

  std::ofstream out(path);
  if (!out) {
    throw IOError("CheckpointWriter::write: could not open output file: " + path.string());
  }
  out << doc.dump(2) << '\n';
  // A full disk or a broken pipe makes the stream fail without throwing.
  // Reporting success for a half-written checkpoint is exactly the failure
  // mode a checkpoint must not have.
  out.flush();
  if (!out) {
    throw IOError("CheckpointWriter::write: failed while writing: " + path.string());
  }
}

}  // namespace cfd::io
