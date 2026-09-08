#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::GeometryConfig;

GeometryConfig parseGeometryConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "geometry.json", {"type", "length", "height"});

  GeometryConfig config;
  config.type = getRequiredString(json, path, "type");
  // TODO.md P1 section 7: reject unsupported geometry types clearly
  // rather than silently accepting a shape the numerical core cannot
  // build (circle, 3D, unstructured -- none of which exist yet).
  if (config.type != "rectangle") {
    throwConfigError(path, "type", "be one of: rectangle", config.type);
  }

  config.length = getRequiredReal(json, path, "length");
  if (!(config.length > 0.0)) {
    throwConfigError(path, "length", "be > 0", std::to_string(config.length));
  }
  config.height = getRequiredReal(json, path, "height");
  if (!(config.height > 0.0)) {
    throwConfigError(path, "height", "be > 0", std::to_string(config.height));
  }
  return config;
}

}  // namespace cfd::io::detail
