#include "CaseModelAdapter.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::io::AlphaBoundarySpec;
using cfd::io::BoundaryConfig;
using cfd::io::BuoyancyPhysicsConfig;
using cfd::io::CaseConfig;
using cfd::io::CompressiblePhysicsConfig;
using cfd::io::ConcentrationBoundarySpec;
using cfd::io::GeometryConfig;
using cfd::io::InitialConditions;
using cfd::io::LinearSolverSpec;
using cfd::io::MeshConfig;
using cfd::io::MultiphasePhysicsConfig;
using cfd::io::PatchBoundaryConfig;
using cfd::io::PhasePhysicsConfig;
using cfd::io::PhysicsConfig;
using cfd::io::PressureBoundarySpec;
using cfd::io::SolverConfig;
using cfd::io::SpeciesConfig;
using cfd::io::TemperatureBoundarySpec;
using cfd::io::ThermalPhysicsConfig;
using cfd::io::TurbulencePhysicsConfig;
using cfd::io::VelocityBoundarySpec;

namespace cfd::gui {

namespace {

// --- small QVariantMap helpers ------------------------------------------

double num(const QVariantMap& m, const char* key, double fallback = 0.0) {
  const auto it = m.find(QString::fromUtf8(key));
  return it == m.end() ? fallback : it->toDouble();
}
int integer(const QVariantMap& m, const char* key, int fallback = 0) {
  const auto it = m.find(QString::fromUtf8(key));
  return it == m.end() ? fallback : it->toInt();
}
std::string str(const QVariantMap& m, const char* key, const std::string& fallback = "") {
  const auto it = m.find(QString::fromUtf8(key));
  return it == m.end() ? fallback : it->toString().toStdString();
}
bool boolean(const QVariantMap& m, const char* key, bool fallback = false) {
  const auto it = m.find(QString::fromUtf8(key));
  return it == m.end() ? fallback : it->toBool();
}
// Only inserts the key when `value` is set -- the "absent means off" /
// "absent means not given" convention this whole file follows (see
// CaseModelAdapter.hpp's own header comment).
void setIfPresent(QVariantMap& m, const char* key, const std::optional<Real>& value) {
  if (value.has_value()) m[QString::fromUtf8(key)] = *value;
}
std::optional<Real> optionalNum(const QVariantMap& m, const char* key) {
  const auto it = m.find(QString::fromUtf8(key));
  return it == m.end() ? std::nullopt : std::make_optional(it->toDouble());
}

QVariantMap phaseToVariant(const PhasePhysicsConfig& p) {
  QVariantMap m;
  m["name"] = QString::fromStdString(p.name);
  m["density"] = p.density;
  m["viscosity"] = p.viscosity;
  return m;
}
PhasePhysicsConfig phaseFromVariant(const QVariantMap& m) {
  PhasePhysicsConfig phase;
  phase.name = str(m, "name");
  phase.density = num(m, "density");
  phase.viscosity = num(m, "viscosity");
  return phase;
}

QVariantMap linearSolverToVariant(const LinearSolverSpec& s) {
  QVariantMap m;
  m["type"] = QString::fromStdString(s.type);
  m["absoluteTolerance"] = s.absoluteTolerance;
  m["relativeTolerance"] = s.relativeTolerance;
  m["maxIterations"] = static_cast<int>(s.maxIterations);
  return m;
}
LinearSolverSpec linearSolverFromVariant(const QVariantMap& m) {
  LinearSolverSpec spec;
  spec.type = str(m, "type", "BiCGSTAB");
  spec.absoluteTolerance = num(m, "absoluteTolerance");
  spec.relativeTolerance = num(m, "relativeTolerance");
  spec.maxIterations = static_cast<cfd::Index>(integer(m, "maxIterations"));
  return spec;
}

}  // namespace

QVariantMap toVariant(const CaseConfig& config) {
  QVariantMap m;
  m["name"] = QString::fromStdString(config.name);
  m["description"] = QString::fromStdString(config.description);
  m["formatVersion"] = config.formatVersion;
  return m;
}

CaseConfig caseConfigFromVariant(const QVariantMap& variant, const CaseConfig& previous) {
  CaseConfig config = previous;
  config.name = str(variant, "name", previous.name);
  config.description = str(variant, "description", previous.description);
  return config;
}

QVariantMap toVariant(const GeometryConfig& config) {
  QVariantMap m;
  m["type"] = QString::fromStdString(config.type);
  m["length"] = config.length;
  m["height"] = config.height;
  m["depth"] = config.depth;  // P12-MESH-006: a box's z extent (0 for every 2D geometry)
  return m;
}

GeometryConfig geometryConfigFromVariant(const QVariantMap& variant,
                                         const GeometryConfig& previous) {
  GeometryConfig config;
  config.type = str(variant, "type", "rectangle");
  config.length = num(variant, "length");
  config.height = num(variant, "height");
  // P12-MESH-006: an editor map without "depth" keeps the previous depth, so a 3D case's depth
  // survives a commit from a page that does not show it.
  config.depth = variant.contains(QStringLiteral("depth")) ? num(variant, "depth") : previous.depth;
  return config;
}

QVariantMap toVariant(const MeshConfig& config) {
  QVariantMap m;
  m["type"] = QString::fromStdString(config.type);
  m["nx"] = static_cast<int>(config.nx);
  m["ny"] = static_cast<int>(config.ny);
  m["nz"] = static_cast<int>(config.nz);  // P12-MESH-006: cells along z (0 for a 2D mesh)
  // P12-MESH-001: read-only -- the vertex grid of a structured_quad mesh
  // is not editable in the GUI (it comes from mesh.json).
  m["vertexCount"] = static_cast<int>(config.vertices.size());
  // P12-MESH-003: read-only -- a multiblock mesh (blocks, interfaces,
  // patches) comes from mesh.json; the editor shows its size only.
  cfd::Index cells = config.nx * config.ny * (config.nz > 0 ? config.nz : 1);
  if (config.type == "multiblock") {
    cells = 0;
    QVariantList blocks;
    for (const auto& block : config.blocks) {
      cells += block.nx * block.ny;
      QVariantMap b;
      b["name"] = QString::fromStdString(block.name);
      b["nx"] = static_cast<int>(block.nx);
      b["ny"] = static_cast<int>(block.ny);
      blocks.push_back(b);
    }
    m["blocks"] = blocks;
    m["interfaceCount"] = static_cast<int>(config.interfaces.size());
  }
  m["cellCount"] = static_cast<double>(cells);
  // P12-MESH-002: grading, flattened per axis for the mesh editor (an
  // absent "grading" reads as uniform on both axes).
  const cfd::io::MeshGradingConfig grading = config.grading.value_or(cfd::io::MeshGradingConfig{});
  m["hasGrading"] = config.grading.has_value();
  for (const bool xAxis : {true, false}) {
    const cfd::mesh::AxisGrading& g = xAxis ? grading.x : grading.y;
    const QString prefix = xAxis ? QStringLiteral("xGrading") : QStringLiteral("yGrading");
    m[prefix + "Type"] = QString::fromLatin1(cfd::io::gradingTypeName(g.type));
    m[prefix + "Ratio"] = g.ratio;
    m[prefix + "Cluster"] = QString::fromLatin1(cfd::io::gradingClusterName(g.cluster, xAxis));
  }
  return m;
}

namespace {

// One axis of the mesh editor's flattened grading keys. The vocabulary is
// that of mesh.json (MeshConfig.hpp); anything else maps to a value the
// real CaseReader then validates (an unknown type reads as geometric so a
// typo is rejected there instead of silently becoming uniform).
cfd::mesh::AxisGrading axisGradingFromVariant(const QVariantMap& variant, bool xAxis) {
  const char* type = xAxis ? "xGradingType" : "yGradingType";
  const char* ratio = xAxis ? "xGradingRatio" : "yGradingRatio";
  const char* cluster = xAxis ? "xGradingCluster" : "yGradingCluster";
  cfd::mesh::AxisGrading g;
  if (str(variant, type, "uniform") == "uniform") return g;
  g.type = cfd::mesh::GradingType::Geometric;
  g.ratio = num(variant, ratio, 1.0);
  const std::string side = str(variant, cluster, "both");
  if (side == (xAxis ? "left" : "bottom")) {
    g.cluster = cfd::mesh::GradingCluster::Start;
  } else if (side == (xAxis ? "right" : "top")) {
    g.cluster = cfd::mesh::GradingCluster::End;
  } else {
    g.cluster = cfd::mesh::GradingCluster::Both;
  }
  return g;
}

}  // namespace

MeshConfig meshConfigFromVariant(const QVariantMap& variant, const MeshConfig& previous) {
  MeshConfig config;
  config.type = str(variant, "type", "structured_cartesian");
  config.nx = static_cast<cfd::Index>(integer(variant, "nx"));
  config.ny = static_cast<cfd::Index>(integer(variant, "ny"));
  // P12-MESH-006: a map without "nz" keeps the previous nz (a 3D case's z resolution survives a
  // commit from a page that does not show it); validity is CaseReader's check.
  config.nz = variant.contains(QStringLiteral("nz"))
                  ? static_cast<cfd::Index>(std::max(0, integer(variant, "nz")))
                  : previous.nz;
  // P12-MESH-001: the editor never carries vertices; a structured_quad
  // mesh keeps the previous (mesh.json) vertex grid, so saving never drops
  // it. If nx/ny were changed, the kept grid no longer matches and
  // validation reports that (CaseReader: vertex count).
  if (config.type == "structured_quad") config.vertices = previous.vertices;
  // P12-MESH-003: likewise a multiblock mesh keeps its blocks, interfaces
  // and patches (nx/ny stay 0).
  if (config.type == "multiblock") {
    config.nx = 0;
    config.ny = 0;
    config.blocks = previous.blocks;
    config.interfaces = previous.interfaces;
    config.patches = previous.patches;
  }
  // P12-MESH-002: grading (structured_cartesian only). A map without the
  // grading keys keeps the previous grading unchanged; with them, a grading
  // is recorded when either axis is geometric or the case already had one
  // (so an ungraded case still saves exactly {type, nx, ny}).
  if (config.type == "structured_cartesian") {
    if (!variant.contains(QStringLiteral("xGradingType")) &&
        !variant.contains(QStringLiteral("yGradingType"))) {
      config.grading = previous.grading;
    } else {
      cfd::io::MeshGradingConfig grading{axisGradingFromVariant(variant, true),
                                         axisGradingFromVariant(variant, false)};
      if (previous.grading.has_value() || grading.x.type == cfd::mesh::GradingType::Geometric ||
          grading.y.type == cfd::mesh::GradingType::Geometric) {
        config.grading = grading;
      }
    }
  }
  return config;
}

QVariantMap toVariant(const InitialConditions& config) {
  QVariantMap m;
  m["velocityX"] = config.velocity.x;
  m["velocityY"] = config.velocity.y;
  m["velocityZ"] = config.velocity.z;  // P12-MESH-006 (0 for a 2D case)
  m["pressure"] = config.pressure;
  return m;
}

InitialConditions initialConditionsFromVariant(const QVariantMap& variant,
                                               const InitialConditions& previous) {
  InitialConditions config;
  // P12-MESH-006: velocityZ (absent: keep the previous w); CaseWriter writes [u, v, w] for a box
  // geometry and [u, v] otherwise, whatever the component count read from case.json was.
  config.velocity = Vector2{num(variant, "velocityX"), num(variant, "velocityY"),
                            num(variant, "velocityZ", previous.velocity.z)};
  config.pressure = num(variant, "pressure");
  config.velocityComponents = previous.velocityComponents;
  return config;
}

QVariantMap toVariant(const PhysicsConfig& config) {
  QVariantMap m;
  m["model"] = QString::fromStdString(config.model);
  m["density"] = config.density;
  m["dynamicViscosity"] = config.dynamicViscosity;
  setIfPresent(m, "reynoldsNumber", config.reynoldsNumber);

  if (config.thermal.has_value()) {
    QVariantMap t;
    t["conductivity"] = config.thermal->conductivity;
    t["specificHeat"] = config.thermal->specificHeat;
    t["initialTemperature"] = config.thermal->initialTemperature;
    m["thermal"] = t;
  }
  if (config.turbulence.has_value()) {
    const auto& tb = *config.turbulence;
    QVariantMap t;
    t["model"] = QString::fromStdString(tb.model);
    t["initialK"] = tb.initialK;
    setIfPresent(t, "initialEpsilon", tb.initialEpsilon);
    setIfPresent(t, "initialOmega", tb.initialOmega);
    setIfPresent(t, "kRelaxation", tb.kRelaxation);
    setIfPresent(t, "epsilonRelaxation", tb.epsilonRelaxation);
    setIfPresent(t, "omegaRelaxation", tb.omegaRelaxation);
    m["turbulence"] = t;
  }
  if (config.buoyancy.has_value()) {
    QVariantMap b;
    b["beta"] = config.buoyancy->beta;
    b["referenceTemperature"] = config.buoyancy->referenceTemperature;
    b["gravityX"] = config.buoyancy->gravity.x;
    b["gravityY"] = config.buoyancy->gravity.y;
    m["buoyancy"] = b;
  }
  QVariantList speciesList;
  for (const auto& s : config.species) {
    QVariantMap sm;
    sm["name"] = QString::fromStdString(s.name);
    sm["diffusivity"] = s.diffusivity;
    sm["initialConcentration"] = s.initialConcentration;
    speciesList.push_back(sm);
  }
  m["species"] = speciesList;
  if (config.multiphase.has_value()) {
    QVariantMap mp;
    mp["phase1"] = phaseToVariant(config.multiphase->phase1);
    mp["phase2"] = phaseToVariant(config.multiphase->phase2);
    mp["initialAlpha"] = config.multiphase->initialAlpha;
    mp["transportTimeStep"] = config.multiphase->transportTimeStep;
    m["multiphase"] = mp;
  }
  if (config.compressible.has_value()) {
    const auto& c = *config.compressible;
    QVariantMap cm;
    cm["gasConstant"] = c.gasConstant;
    cm["specificHeatPressure"] = c.specificHeatPressure;
    cm["referencePressure"] = c.referencePressure;
    cm["thermalCoupled"] = c.thermalCoupled;
    setIfPresent(cm, "temperature", c.temperature);
    m["compressible"] = cm;
  }
  return m;
}

PhysicsConfig physicsConfigFromVariant(const QVariantMap& variant) {
  PhysicsConfig config;
  config.model = str(variant, "model", "incompressible_laminar");
  config.density = num(variant, "density");
  config.dynamicViscosity = num(variant, "dynamicViscosity");
  config.reynoldsNumber = optionalNum(variant, "reynoldsNumber");

  if (variant.contains(QStringLiteral("thermal"))) {
    const QVariantMap t = variant.value(QStringLiteral("thermal")).toMap();
    ThermalPhysicsConfig thermal;
    thermal.conductivity = num(t, "conductivity");
    thermal.specificHeat = num(t, "specificHeat");
    thermal.initialTemperature = num(t, "initialTemperature");
    config.thermal = thermal;
  }
  if (variant.contains(QStringLiteral("turbulence"))) {
    const QVariantMap t = variant.value(QStringLiteral("turbulence")).toMap();
    TurbulencePhysicsConfig turbulence;
    turbulence.model = str(t, "model", "k_epsilon");
    turbulence.initialK = num(t, "initialK");
    turbulence.initialEpsilon = optionalNum(t, "initialEpsilon");
    turbulence.initialOmega = optionalNum(t, "initialOmega");
    turbulence.kRelaxation = optionalNum(t, "kRelaxation");
    turbulence.epsilonRelaxation = optionalNum(t, "epsilonRelaxation");
    turbulence.omegaRelaxation = optionalNum(t, "omegaRelaxation");
    config.turbulence = turbulence;
  }
  if (variant.contains(QStringLiteral("buoyancy"))) {
    const QVariantMap b = variant.value(QStringLiteral("buoyancy")).toMap();
    BuoyancyPhysicsConfig buoyancy;
    buoyancy.beta = num(b, "beta");
    buoyancy.referenceTemperature = num(b, "referenceTemperature");
    buoyancy.gravity = Vector2{num(b, "gravityX"), num(b, "gravityY")};
    config.buoyancy = buoyancy;
  }
  if (variant.contains(QStringLiteral("species"))) {
    const QVariantList speciesList = variant.value(QStringLiteral("species")).toList();
    for (const auto& entry : speciesList) {
      const QVariantMap sm = entry.toMap();
      SpeciesConfig species;
      species.name = str(sm, "name");
      species.diffusivity = num(sm, "diffusivity");
      species.initialConcentration = num(sm, "initialConcentration");
      config.species.push_back(std::move(species));
    }
  }
  if (variant.contains(QStringLiteral("multiphase"))) {
    const QVariantMap mp = variant.value(QStringLiteral("multiphase")).toMap();
    MultiphasePhysicsConfig multiphase;
    multiphase.phase1 = phaseFromVariant(mp.value(QStringLiteral("phase1")).toMap());
    multiphase.phase2 = phaseFromVariant(mp.value(QStringLiteral("phase2")).toMap());
    multiphase.initialAlpha = num(mp, "initialAlpha");
    multiphase.transportTimeStep = num(mp, "transportTimeStep");
    config.multiphase = multiphase;
  }
  if (variant.contains(QStringLiteral("compressible"))) {
    const QVariantMap cm = variant.value(QStringLiteral("compressible")).toMap();
    CompressiblePhysicsConfig compressible;
    compressible.gasConstant = num(cm, "gasConstant");
    compressible.specificHeatPressure = num(cm, "specificHeatPressure");
    compressible.referencePressure = num(cm, "referencePressure");
    compressible.thermalCoupled = boolean(cm, "thermalCoupled");
    if (!compressible.thermalCoupled) {
      compressible.temperature = optionalNum(cm, "temperature");
    }
    config.compressible = compressible;
  }
  return config;
}

namespace {

QVariantMap velocityToVariant(const VelocityBoundarySpec& spec) {
  QVariantMap m;
  m["type"] = QString::fromStdString(spec.type);
  m["valueX"] = spec.value.x;
  m["valueY"] = spec.value.y;
  m["valueZ"] = spec.value.z;  // P12-MESH-006 (0 for a 2D case; CaseWriter writes it only in 3D)
  return m;
}
VelocityBoundarySpec velocityFromVariant(const QVariantMap& m) {
  VelocityBoundarySpec spec;
  spec.type = str(m, "type", "wall");
  spec.value = Vector2{num(m, "valueX"), num(m, "valueY"), num(m, "valueZ")};
  return spec;
}
QVariantMap pressureToVariant(const PressureBoundarySpec& spec) {
  QVariantMap m;
  m["type"] = QString::fromStdString(spec.type);
  m["value"] = spec.value;
  return m;
}
PressureBoundarySpec pressureFromVariant(const QVariantMap& m) {
  PressureBoundarySpec spec;
  spec.type = str(m, "type", "fixed_gradient");
  spec.value = num(m, "value");
  return spec;
}
QVariantMap temperatureToVariant(const TemperatureBoundarySpec& spec) {
  QVariantMap m;
  m["type"] = QString::fromStdString(spec.type);
  m["value"] = spec.value;
  return m;
}
TemperatureBoundarySpec temperatureFromVariant(const QVariantMap& m) {
  TemperatureBoundarySpec spec;
  spec.type = str(m, "type", "adiabatic");
  spec.value = num(m, "value");
  return spec;
}
QVariantMap concentrationToVariant(const ConcentrationBoundarySpec& spec) {
  QVariantMap m;
  m["type"] = QString::fromStdString(spec.type);
  m["value"] = spec.value;
  return m;
}
ConcentrationBoundarySpec concentrationFromVariant(const QVariantMap& m) {
  ConcentrationBoundarySpec spec;
  spec.type = str(m, "type", "fixed_value");
  spec.value = num(m, "value");
  return spec;
}
QVariantMap alphaToVariant(const AlphaBoundarySpec& spec) {
  QVariantMap m;
  m["type"] = QString::fromStdString(spec.type);
  m["value"] = spec.value;
  return m;
}
AlphaBoundarySpec alphaFromVariant(const QVariantMap& m) {
  AlphaBoundarySpec spec;
  spec.type = str(m, "type", "fixed_value");
  spec.value = num(m, "value");
  return spec;
}

}  // namespace

QVariantMap toVariant(const BoundaryConfig& config) {
  QVariantMap m;
  for (const auto& [patchName, patch] : config.patches) {
    QVariantMap patchMap;
    patchMap["velocity"] = velocityToVariant(patch.velocity);
    patchMap["pressure"] = pressureToVariant(patch.pressure);
    if (patch.temperature.has_value()) {
      patchMap["temperature"] = temperatureToVariant(*patch.temperature);
    }
    if (!patch.concentration.empty()) {
      QVariantMap speciesMap;
      for (const auto& [name, spec] : patch.concentration) {
        speciesMap[QString::fromStdString(name)] = concentrationToVariant(spec);
      }
      patchMap["species"] = speciesMap;
    }
    if (patch.alpha.has_value()) {
      patchMap["alpha"] = alphaToVariant(*patch.alpha);
    }
    m[QString::fromStdString(patchName)] = patchMap;
  }
  return m;
}

QVariantMap toVariant(const cfd::mesh::MeshQualityReport& report) {
  QVariantMap m;
  m["status"] = QString::fromLatin1(cfd::mesh::meshQualityStatusName(report.status));
  m["summary"] = QString::fromStdString(report.summaryLine());
  m["cells"] = static_cast<double>(report.cellCount);
  m["minimumCellArea"] = report.cellArea.minimum;
  m["maximumCellArea"] = report.cellArea.maximum;
  m["maximumAspectRatio"] = report.aspectRatio.maximum;
  m["maximumNonOrthogonality"] = report.nonOrthogonality.maximum;
  m["maximumSkewness"] = report.skewness.maximum;
  m["maximumExpansionRatio"] =
      report.expansionRatio.count > 0 ? report.expansionRatio.maximum : 1.0;
  m["degenerateCells"] = static_cast<double>(report.degenerateCells);
  m["invalidFaces"] = static_cast<double>(report.invalidFaces);
  QVariantList issues;
  for (const auto& issue : report.issues) {
    QVariantMap i;
    i["severity"] = QString::fromLatin1(cfd::mesh::meshQualitySeverityName(issue.severity));
    i["metric"] = QString::fromStdString(issue.metric);
    i["message"] = QString::fromStdString(issue.message);
    i["text"] = QString::fromStdString(cfd::mesh::formatMeshQualityIssue(issue));
    issues.push_back(i);
  }
  m["issues"] = issues;
  return m;
}

BoundaryConfig boundaryConfigFromVariant(const QVariantMap& variant) {
  BoundaryConfig config;
  // Every patch in the map (BoundaryEditor.qml edits a deep copy of the
  // whole boundary configuration and hands all of it back): the four
  // canonical patches of a single-grid mesh, or a P12-MESH-003 multiblock
  // mesh's own named patches -- which patch names are valid is CaseReader's
  // check (they must be exactly the mesh's), not this adapter's.
  for (auto entry = variant.begin(); entry != variant.end(); ++entry) {
    const std::string patchName = entry.key().toStdString();
    const QVariantMap patchMap = entry->toMap();
    PatchBoundaryConfig patch;
    patch.velocity = velocityFromVariant(patchMap.value(QStringLiteral("velocity")).toMap());
    patch.pressure = pressureFromVariant(patchMap.value(QStringLiteral("pressure")).toMap());
    if (patchMap.contains(QStringLiteral("temperature"))) {
      patch.temperature =
          temperatureFromVariant(patchMap.value(QStringLiteral("temperature")).toMap());
    }
    if (patchMap.contains(QStringLiteral("species"))) {
      const QVariantMap speciesMap = patchMap.value(QStringLiteral("species")).toMap();
      for (auto it = speciesMap.begin(); it != speciesMap.end(); ++it) {
        patch.concentration.emplace(it.key().toStdString(), concentrationFromVariant(it->toMap()));
      }
    }
    if (patchMap.contains(QStringLiteral("alpha"))) {
      patch.alpha = alphaFromVariant(patchMap.value(QStringLiteral("alpha")).toMap());
    }
    config.patches.emplace(patchName, std::move(patch));
  }
  return config;
}

QVariantMap toVariant(const SolverConfig& config) {
  QVariantMap m;
  m["type"] = QString::fromStdString(config.type);
  m["maxIterations"] = static_cast<int>(config.maxIterations);
  m["velocityRelaxation"] = config.velocityRelaxation;
  m["pressureRelaxation"] = config.pressureRelaxation;
  m["velocityTolerance"] = config.velocityTolerance;
  m["pressureTolerance"] = config.pressureTolerance;
  m["continuityTolerance"] = config.continuityTolerance;
  m["momentumSolver"] = linearSolverToVariant(config.momentumSolver);
  m["pressureSolver"] = linearSolverToVariant(config.pressureSolver);
  return m;
}

SolverConfig solverConfigFromVariant(const QVariantMap& variant) {
  SolverConfig config;
  config.type = str(variant, "type", "SIMPLE");
  config.maxIterations = static_cast<cfd::Index>(integer(variant, "maxIterations"));
  config.velocityRelaxation = num(variant, "velocityRelaxation");
  config.pressureRelaxation = num(variant, "pressureRelaxation");
  config.velocityTolerance = num(variant, "velocityTolerance");
  config.pressureTolerance = num(variant, "pressureTolerance");
  config.continuityTolerance = num(variant, "continuityTolerance");
  config.momentumSolver =
      linearSolverFromVariant(variant.value(QStringLiteral("momentumSolver")).toMap());
  config.pressureSolver =
      linearSolverFromVariant(variant.value(QStringLiteral("pressureSolver")).toMap());
  return config;
}

}  // namespace cfd::gui
