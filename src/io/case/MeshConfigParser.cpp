#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::MeshConfig;

MeshConfig parseMeshConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "mesh.json", {"type", "nx", "ny"});

  MeshConfig config;
  config.type = getRequiredString(json, path, "type");
  // The only mesh generator the numerical core has (TODO.md P1 section
  // 8) -- do not duplicate mesh-generation logic here, only recognize
  // this one type and defer to MeshGeometry::createCartesian2D later.
  if (config.type != "structured_cartesian") {
    throwConfigError(path, "type", "be one of: structured_cartesian", config.type);
  }

  config.nx = getRequiredIndex(json, path, "nx");
  if (config.nx == 0) {
    throwConfigError(path, "nx", "be > 0", "0");
  }
  config.ny = getRequiredIndex(json, path, "ny");
  if (config.ny == 0) {
    throwConfigError(path, "ny", "be > 0", "0");
  }
  return config;
}

}  // namespace cfd::io::detail
