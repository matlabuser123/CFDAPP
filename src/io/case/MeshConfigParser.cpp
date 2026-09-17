#include <cmath>
#include <string>

#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::MeshConfig;

namespace {

// P12-MESH-002: one axis of mesh.json "grading":
//   {"type": "uniform"}  or
//   {"type": "geometric", "ratio": r >= 1, "cluster": <patch> | "both"}.
cfd::mesh::AxisGrading parseAxisGrading(const nlohmann::json& node,
                                        const std::filesystem::path& path, bool xAxis) {
  const std::string axis = xAxis ? "grading.x" : "grading.y";
  requireObject(node, path, axis);
  rejectUnknownKeys(node, path, "mesh.json " + axis, {"type", "ratio", "cluster"});
  cfd::mesh::AxisGrading grading;
  const std::string type = getRequiredString(node, path, "type", axis + ".type");
  if (type == "uniform") {
    for (const char* key : {"ratio", "cluster"}) {
      if (node.contains(key)) {
        throwConfigError(path, axis + "." + key, "be absent for grading type uniform",
                         describeJsonValue(node.at(key)));
      }
    }
    return grading;
  }
  if (type != "geometric") {
    throwConfigError(path, axis + ".type", "be one of: uniform, geometric", type);
  }
  grading.type = cfd::mesh::GradingType::Geometric;
  grading.ratio = getRequiredReal(node, path, "ratio", axis + ".ratio");
  if (!(grading.ratio >= 1.0)) {
    throwConfigError(path, axis + ".ratio",
                     "be >= 1 (the growth factor of adjacent cell widths away from the clustered "
                     "boundary; \"cluster\" selects that boundary)",
                     describeJsonValue(node.at("ratio")));
  }
  const std::string cluster = getRequiredString(node, path, "cluster", axis + ".cluster");
  const std::string start = xAxis ? "left" : "bottom";
  const std::string end = xAxis ? "right" : "top";
  if (cluster == start) {
    grading.cluster = cfd::mesh::GradingCluster::Start;
  } else if (cluster == end) {
    grading.cluster = cfd::mesh::GradingCluster::End;
  } else if (cluster == "both") {
    grading.cluster = cfd::mesh::GradingCluster::Both;
  } else {
    throwConfigError(path, axis + ".cluster", "be one of: " + start + ", " + end + ", both",
                     cluster);
  }
  return grading;
}

// A [[x, y], ...] array of exactly `expected` finite vertices; labels
// "<prefix>[k]" (structured_quad: prefix "vertices").
std::vector<Vector2> parseVertexArray(const nlohmann::json& vertices,
                                      const std::filesystem::path& path, const std::string& prefix,
                                      std::size_t expected) {
  if (!vertices.is_array() || vertices.size() != expected) {
    throwConfigError(path, prefix,
                     "be an array of (nx + 1) * (ny + 1) = " + std::to_string(expected) +
                         " [x, y] vertices (row-major, i fastest)",
                     vertices.is_array() ? std::to_string(vertices.size()) + " entries"
                                         : describeJsonValue(vertices).substr(0, 60));
  }
  std::vector<Vector2> result;
  result.reserve(expected);
  for (std::size_t k = 0; k < expected; ++k) {
    const auto& v = vertices[k];
    const std::string label = prefix + "[" + std::to_string(k) + "]";
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
      throwConfigError(path, label, "be an array of exactly 2 numbers [x, y]",
                       describeJsonValue(v));
    }
    const Real x = v[0].get<Real>();
    const Real y = v[1].get<Real>();
    if (!std::isfinite(x) || !std::isfinite(y)) {
      throwConfigError(path, label, "have finite components", describeJsonValue(v));
    }
    result.push_back(Vector2{x, y});
  }
  return result;
}

// P12-MESH-003: {"block": <name>, "side": "left"|"right"|"bottom"|"top"},
// the block resolved against the blocks already parsed.
MeshSideRefConfig parseSideRef(const nlohmann::json& node, const std::filesystem::path& path,
                               const std::string& label,
                               const std::vector<MeshBlockConfig>& blocks) {
  requireObject(node, path, label);
  rejectUnknownKeys(node, path, "mesh.json " + label, {"block", "side"});
  MeshSideRefConfig ref;
  ref.block = getRequiredString(node, path, "block", label + ".block");
  ref.side = getRequiredString(node, path, "side", label + ".side");
  bool known = false;
  for (const auto& block : blocks) known = known || block.name == ref.block;
  if (!known) throwConfigError(path, label + ".block", "name a block of this mesh", ref.block);
  if (ref.side != "left" && ref.side != "right" && ref.side != "bottom" && ref.side != "top") {
    throwConfigError(path, label + ".side", "be one of: left, right, bottom, top", ref.side);
  }
  return ref;
}

bool isPatchName(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// P12-MESH-003: type "multiblock" -- blocks, interfaces, patches. Structure,
// names and references are checked here; the geometry (cell validity,
// interface coincidence, overlap) by MeshGeometry::createMultiBlock2D in
// CaseBuilder, before any solver runs.
void parseMultiBlock(const nlohmann::json& json, const std::filesystem::path& path,
                     MeshConfig& config) {
  for (const char* key : {"nx", "ny", "vertices", "grading"}) {
    if (json.contains(key)) {
      throwConfigError(path, key,
                       "be absent for type multiblock (each block gives its own nx, ny and "
                       "vertices)",
                       describeJsonValue(json.at(key)).substr(0, 60));
    }
  }
  requireField(json, path, "blocks");
  const auto& blocks = json.at("blocks");
  if (!blocks.is_array() || blocks.empty()) {
    throwConfigError(path, "blocks", "be a non-empty array of blocks",
                     describeJsonValue(blocks).substr(0, 60));
  }
  for (std::size_t b = 0; b < blocks.size(); ++b) {
    const std::string label = "blocks[" + std::to_string(b) + "]";
    const auto& node = blocks[b];
    requireObject(node, path, label);
    rejectUnknownKeys(node, path, "mesh.json " + label, {"name", "nx", "ny", "vertices"});
    MeshBlockConfig block;
    block.name = getRequiredString(node, path, "name", label + ".name");
    if (!isPatchName(block.name)) {
      throwConfigError(path, label + ".name", "be 1-64 characters of [A-Za-z0-9_-]", block.name);
    }
    for (const auto& other : config.blocks) {
      if (other.name == block.name) {
        throwConfigError(path, label + ".name", "be unique among the blocks", block.name);
      }
    }
    block.nx = getRequiredIndex(node, path, "nx", label + ".nx");
    if (block.nx == 0) throwConfigError(path, label + ".nx", "be > 0", "0");
    block.ny = getRequiredIndex(node, path, "ny", label + ".ny");
    if (block.ny == 0) throwConfigError(path, label + ".ny", "be > 0", "0");
    requireField(node, path, "vertices", label + ".vertices");
    block.vertices = parseVertexArray(node.at("vertices"), path, label + ".vertices",
                                      (block.nx + 1) * (block.ny + 1));
    config.blocks.push_back(std::move(block));
  }

  if (json.contains("interfaces")) {
    const auto& interfaces = json.at("interfaces");
    if (!interfaces.is_array()) {
      throwConfigError(path, "interfaces", "be an array", describeJsonValue(interfaces));
    }
    for (std::size_t n = 0; n < interfaces.size(); ++n) {
      const std::string label = "interfaces[" + std::to_string(n) + "]";
      const auto& node = interfaces[n];
      requireObject(node, path, label);
      rejectUnknownKeys(node, path, "mesh.json " + label, {"first", "second", "orientation"});
      MeshInterfaceConfig iface;
      requireField(node, path, "first", label + ".first");
      requireField(node, path, "second", label + ".second");
      iface.first = parseSideRef(node.at("first"), path, label + ".first", config.blocks);
      iface.second = parseSideRef(node.at("second"), path, label + ".second", config.blocks);
      const std::string orientation =
          getOptionalString(node, path, "orientation", "aligned", label + ".orientation");
      if (orientation != "aligned" && orientation != "reversed") {
        throwConfigError(path, label + ".orientation", "be one of: aligned, reversed", orientation);
      }
      iface.reversed = orientation == "reversed";
      config.interfaces.push_back(std::move(iface));
    }
  }

  requireField(json, path, "patches");
  const auto& patches = json.at("patches");
  if (!patches.is_array() || patches.empty()) {
    throwConfigError(path, "patches", "be a non-empty array of named boundary patches",
                     describeJsonValue(patches).substr(0, 60));
  }
  for (std::size_t p = 0; p < patches.size(); ++p) {
    const std::string label = "patches[" + std::to_string(p) + "]";
    const auto& node = patches[p];
    requireObject(node, path, label);
    rejectUnknownKeys(node, path, "mesh.json " + label, {"name", "sides"});
    MeshPatchConfig patch;
    patch.name = getRequiredString(node, path, "name", label + ".name");
    if (!isPatchName(patch.name)) {
      throwConfigError(path, label + ".name", "be 1-64 characters of [A-Za-z0-9_-]", patch.name);
    }
    for (const auto& other : config.patches) {
      if (other.name == patch.name) {
        throwConfigError(path, label + ".name", "be unique among the patches", patch.name);
      }
    }
    requireField(node, path, "sides", label + ".sides");
    const auto& sides = node.at("sides");
    if (!sides.is_array() || sides.empty()) {
      throwConfigError(path, label + ".sides", "be a non-empty array of block sides",
                       describeJsonValue(sides).substr(0, 60));
    }
    for (std::size_t k = 0; k < sides.size(); ++k) {
      patch.sides.push_back(
          parseSideRef(sides[k], path, label + ".sides[" + std::to_string(k) + "]", config.blocks));
    }
    config.patches.push_back(std::move(patch));
  }

  // Every block side exactly once, in one interface or one patch -- named
  // here so the error points at mesh.json (the builder re-checks it).
  std::vector<std::pair<MeshSideRefConfig, std::string>> uses;
  const auto use = [&](const MeshSideRefConfig& ref, const std::string& where) {
    for (const auto& [seen, seenWhere] : uses) {
      if (seen == ref) {
        throwConfigError(path, where,
                         "not reuse block '" + ref.block + "' side '" + ref.side +
                             "' (already used by " + seenWhere +
                             "; each block side is one interface or belongs to one patch)",
                         ref.block + "." + ref.side);
      }
    }
    uses.emplace_back(ref, where);
  };
  for (std::size_t n = 0; n < config.interfaces.size(); ++n) {
    use(config.interfaces[n].first, "interfaces[" + std::to_string(n) + "].first");
    use(config.interfaces[n].second, "interfaces[" + std::to_string(n) + "].second");
  }
  for (std::size_t p = 0; p < config.patches.size(); ++p) {
    for (std::size_t k = 0; k < config.patches[p].sides.size(); ++k) {
      use(config.patches[p].sides[k],
          "patches[" + std::to_string(p) + "].sides[" + std::to_string(k) + "]");
    }
  }
  for (const auto& block : config.blocks) {
    for (const char* side : {"left", "right", "bottom", "top"}) {
      bool found = false;
      for (const auto& [seen, where] : uses) {
        (void)where;
        found = found || (seen.block == block.name && seen.side == side);
      }
      if (!found) {
        throwConfigError(path, "patches",
                         "cover every block side not in an interface (block '" + block.name +
                             "' side '" + side + "' is in no interface and no patch)",
                         "unassigned");
      }
    }
  }
}

MeshGradingConfig parseGrading(const nlohmann::json& node, const std::filesystem::path& path) {
  requireObject(node, path, "grading");
  rejectUnknownKeys(node, path, "mesh.json grading", {"x", "y"});
  MeshGradingConfig grading;
  if (node.contains("x")) grading.x = parseAxisGrading(node.at("x"), path, /*xAxis=*/true);
  if (node.contains("y")) grading.y = parseAxisGrading(node.at("y"), path, /*xAxis=*/false);
  return grading;
}

}  // namespace

MeshConfig parseMeshConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(
      json, path, "mesh.json",
      {"type", "nx", "ny", "nz", "vertices", "grading", "blocks", "interfaces", "patches"});

  MeshConfig config;
  config.type = getRequiredString(json, path, "type");
  // The structured generators the mesh layer has -- do not duplicate mesh
  // generation here, only recognize the type and defer to MeshGeometry
  // (createCartesian2D / createStructuredQuad2D) in CaseBuilder.
  if (config.type != "structured_cartesian" && config.type != "structured_quad" &&
      config.type != "multiblock") {
    throwConfigError(path, "type", "be one of: structured_cartesian, structured_quad, multiblock",
                     config.type);
  }
  // P12-MESH-006: nz (a 3D mesh) only for structured_cartesian.
  if (config.type != "structured_cartesian" && json.contains("nz")) {
    throwConfigError(path, "nz",
                     "be absent unless type is structured_cartesian (only the Cartesian mesh has "
                     "a 3D form)",
                     describeJsonValue(json.at("nz")).substr(0, 60));
  }
  if (config.type == "multiblock") {
    parseMultiBlock(json, path, config);
    return config;
  }
  for (const char* key : {"blocks", "interfaces", "patches"}) {
    if (json.contains(key)) {
      throwConfigError(path, key, "be absent unless type is multiblock",
                       describeJsonValue(json.at(key)).substr(0, 60));
    }
  }

  config.nx = getRequiredIndex(json, path, "nx");
  if (config.nx == 0) {
    throwConfigError(path, "nx", "be > 0", "0");
  }
  config.ny = getRequiredIndex(json, path, "ny");
  if (config.ny == 0) {
    throwConfigError(path, "ny", "be > 0", "0");
  }

  if (config.type == "structured_cartesian") {
    if (json.contains("vertices")) {
      throwConfigError(path, "vertices",
                       "be absent for type structured_cartesian (only structured_quad takes "
                       "explicit vertices)",
                       describeJsonValue(json.at("vertices")).substr(0, 60));
    }
    if (json.contains("nz")) {  // P12-MESH-006: a 3D (uniform) Cartesian mesh.
      config.nz = getRequiredIndex(json, path, "nz");
      if (config.nz == 0) {
        throwConfigError(path, "nz", "be > 0 (omit nz for a 2D mesh)", "0");
      }
      if (json.contains("grading")) {
        throwConfigError(path, "grading",
                         "be absent for a 3D mesh (nz given): 3D meshes are uniform Cartesian",
                         describeJsonValue(json.at("grading")).substr(0, 60));
      }
      return config;
    }
    if (json.contains("grading")) config.grading = parseGrading(json.at("grading"), path);
    return config;
  }
  if (json.contains("grading")) {
    throwConfigError(path, "grading",
                     "be absent for type structured_quad (its vertices are given explicitly -- "
                     "grade them there; grading applies to structured_cartesian)",
                     describeJsonValue(json.at("grading")).substr(0, 60));
  }

  // P12-MESH-001: structured_quad -- the (nx + 1) x (ny + 1) row-major
  // vertex grid, each vertex a 2-component finite [x, y] array.
  requireField(json, path, "vertices");
  config.vertices =
      parseVertexArray(json.at("vertices"), path, "vertices", (config.nx + 1) * (config.ny + 1));
  return config;
}

}  // namespace cfd::io::detail

namespace cfd::io {

std::vector<std::string> meshPatchNames(const MeshConfig& config) {
  if (config.nz > 0) return {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};  // P12-MESH-006
  if (config.type != "multiblock") return {"left", "right", "bottom", "top"};
  std::vector<std::string> names;
  names.reserve(config.patches.size());
  for (const auto& patch : config.patches) names.push_back(patch.name);
  return names;
}

const char* gradingTypeName(cfd::mesh::GradingType type) noexcept {
  return type == cfd::mesh::GradingType::Geometric ? "geometric" : "uniform";
}

const char* gradingClusterName(cfd::mesh::GradingCluster cluster, bool xAxis) noexcept {
  switch (cluster) {
    case cfd::mesh::GradingCluster::Start:
      return xAxis ? "left" : "bottom";
    case cfd::mesh::GradingCluster::End:
      return xAxis ? "right" : "top";
    case cfd::mesh::GradingCluster::Both:
      return "both";
  }
  return "both";
}

}  // namespace cfd::io
