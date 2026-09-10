#include "CaseModelAdapter.hpp"

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
  return m;
}

GeometryConfig geometryConfigFromVariant(const QVariantMap& variant) {
  GeometryConfig config;
  config.type = str(variant, "type", "rectangle");
  config.length = num(variant, "length");
  config.height = num(variant, "height");
  return config;
}

QVariantMap toVariant(const MeshConfig& config) {
  QVariantMap m;
  m["type"] = QString::fromStdString(config.type);
  m["nx"] = static_cast<int>(config.nx);
  m["ny"] = static_cast<int>(config.ny);
  return m;
}

MeshConfig meshConfigFromVariant(const QVariantMap& variant) {
  MeshConfig config;
  config.type = str(variant, "type", "structured_cartesian");
  config.nx = static_cast<cfd::Index>(integer(variant, "nx"));
  config.ny = static_cast<cfd::Index>(integer(variant, "ny"));
  return config;
}

QVariantMap toVariant(const InitialConditions& config) {
  QVariantMap m;
  m["velocityX"] = config.velocity.x;
  m["velocityY"] = config.velocity.y;
  m["pressure"] = config.pressure;
  return m;
}

InitialConditions initialConditionsFromVariant(const QVariantMap& variant) {
  InitialConditions config;
  config.velocity = Vector2{num(variant, "velocityX"), num(variant, "velocityY")};
  config.pressure = num(variant, "pressure");
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
  return m;
}
VelocityBoundarySpec velocityFromVariant(const QVariantMap& m) {
  VelocityBoundarySpec spec;
  spec.type = str(m, "type", "wall");
  spec.value = Vector2{num(m, "valueX"), num(m, "valueY")};
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

BoundaryConfig boundaryConfigFromVariant(const QVariantMap& variant) {
  BoundaryConfig config;
  // Only the four patches the one supported geometry/mesh combination
  // produces are ever GUI-editable (BoundaryEditor.qml's own fixed
  // left/right/bottom/top tab set) -- reading any other key here would
  // just be silently ignored data, so this loop deliberately walks the
  // canonical four rather than `variant`'s own keys.
  for (const char* patchName : {"left", "right", "bottom", "top"}) {
    const QString key = QString::fromUtf8(patchName);
    if (!variant.contains(key)) continue;
    const QVariantMap patchMap = variant.value(key).toMap();
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
