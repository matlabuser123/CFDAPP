#include "cfd/io/RestartWriter.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "cfd/core/Exception.hpp"

namespace cfd::io {

using cfd::solver::RestartSnapshot;

void RestartWriter::write(const std::filesystem::path& path, const RestartSnapshot& snapshot) {
  nlohmann::json doc;
  doc["format_version"] = snapshot.formatVersion;

  doc["state"]["time"] = snapshot.time;
  doc["state"]["step"] = snapshot.step;
  doc["state"]["delta_t"] = snapshot.deltaT;

  doc["mesh"]["cell_count"] = snapshot.cellCount;
  doc["mesh"]["face_count"] = snapshot.faceCount;
  doc["mesh"]["fingerprint"] = snapshot.meshFingerprint;

  nlohmann::json pressure = nlohmann::json::array();
  nlohmann::json velocityX = nlohmann::json::array();
  nlohmann::json velocityY = nlohmann::json::array();
  for (Index i = 0; i < snapshot.pressure.size(); ++i) {
    pressure.push_back(snapshot.pressure[i]);
  }
  for (Index i = 0; i < snapshot.velocity.size(); ++i) {
    velocityX.push_back(snapshot.velocity[i].x);
    velocityY.push_back(snapshot.velocity[i].y);
  }
  nlohmann::json massFlux = nlohmann::json::array();
  for (Index i = 0; i < snapshot.massFlux.size(); ++i) {
    massFlux.push_back(snapshot.massFlux[i]);
  }
  doc["fields"]["pressure"] = std::move(pressure);
  doc["fields"]["velocity_x"] = std::move(velocityX);
  doc["fields"]["velocity_y"] = std::move(velocityY);
  doc["fields"]["mass_flux"] = std::move(massFlux);

  std::ofstream out(path);
  if (!out) {
    throw IOError("RestartWriter::write: could not open output file: " + path.string());
  }
  out << doc.dump(2) << '\n';
}

}  // namespace cfd::io
