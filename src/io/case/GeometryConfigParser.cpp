#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::GeometryConfig;

GeometryConfig parseGeometryConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "geometry.json", {"type", "length", "height", "depth"});

  GeometryConfig config;
  config.type = getRequiredString(json, path, "type");
  // TODO.md P1 section 7: reject unsupported geometry types clearly
  // rather than silently accepting a shape the numerical core cannot
  // build (circle, unstructured -- neither exists).
  // P12-MESH-006: "box" is the 3D Cartesian domain.
  if (config.type != "rectangle" && config.type != "mesh_defined" && config.type != "box") {
    throwConfigError(path, "type", "be one of: rectangle, mesh_defined, box", config.type);
  }
  if (config.type != "box" && json.contains("depth")) {  // P12-MESH-006
    throwConfigError(path, "depth", "be absent unless type is box (the 3D domain)",
                     describeJsonValue(json.at("depth")));
  }
  if (config.type == "mesh_defined") {  // P12-MESH-003
    for (const char* key : {"length", "height"}) {
      if (json.contains(key)) {
        throwConfigError(path, key,
                         "be absent for type mesh_defined (the multiblock mesh defines the "
                         "domain)",
                         describeJsonValue(json.at(key)));
      }
    }
    return config;
  }

  config.length = getRequiredReal(json, path, "length");
  if (!(config.length > 0.0)) {
    throwConfigError(path, "length", "be > 0", std::to_string(config.length));
  }
  config.height = getRequiredReal(json, path, "height");
  if (!(config.height > 0.0)) {
    throwConfigError(path, "height", "be > 0", std::to_string(config.height));
  }
  if (config.type == "box") {  // P12-MESH-006: the z extent.
    config.depth = getRequiredReal(json, path, "depth");
    if (!(config.depth > 0.0)) {
      throwConfigError(path, "depth", "be > 0", std::to_string(config.depth));
    }
  }
  return config;
}

}  // namespace cfd::io::detail
