#include "cfd/io/CaseWriter.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "cfd/core/Exception.hpp"

namespace cfd::io {

namespace {

using json = nlohmann::json;

// Only "moving_wall"/"inlet" carry a "value" array -- see
// BoundaryConfigParser.cpp's own kVelocityTypesWithValue. Writing a
// "value" key for any other type would make CaseReader reject the
// written file outright (rejectUnknownKeys), so this mirrors the
// parser's own per-type set rather than a blanket "always write value".
bool velocityTypeHasValue(const std::string& type) {
  return type == "moving_wall" || type == "inlet";
}

// Only "fixed_temperature"/"heat_flux" carry a "value" -- see
// BoundaryConfigParser.cpp's own kTemperatureTypesWithValue.
bool temperatureTypeHasValue(const std::string& type) {
  return type == "fixed_temperature" || type == "heat_flux";
}

void writeJsonFile(const std::filesystem::path& path, const json& document) {
  std::ofstream out(path);
  if (!out) {
    throw IOError("CaseWriter: could not open file for writing: " + path.string());
  }
  // 2-space indent: matches this project's own hand-authored case files
  // under cases/* (readability for a human editing a GUI-saved case by
  // hand later -- section 10's "advanced/manual editing can remain
  // available").
  out << document.dump(2) << '\n';
  if (!out) {
    throw IOError("CaseWriter: failed writing file: " + path.string());
  }
}

}  // namespace

void CaseWriter::write(const std::filesystem::path& caseDirectory, const CaseDefinition& d) {
  std::error_code createError;
  std::filesystem::create_directories(caseDirectory, createError);
  if (createError) {
    throw IOError("CaseWriter: could not create case directory: " + caseDirectory.string());
  }

  // P12-MESH-006: a 3D (box) case writes 3-component velocities, depth and nz;
  // a 2D case writes exactly what it wrote before.
  const bool threeDimensional = geometryDimension(d.geometry) == 3;
  const auto vectorJson = [threeDimensional](const Vector2& v) {
    return threeDimensional ? json::array({v.x, v.y, v.z}) : json::array({v.x, v.y});
  };

  // --- case.json ----------------------------------------------------------
  json caseJson{
      {"name", d.caseConfig.name},
      {"description", d.caseConfig.description},
      {"format_version", d.caseConfig.formatVersion},
      {"geometry", "geometry.json"},
      {"mesh", "mesh.json"},
      {"physics", "physics.json"},
      {"boundaries", "boundaries.json"},
      {"solver", "solver.json"},
      {"initial_conditions", json{{"velocity", vectorJson(d.initialConditions.velocity)},
                                  {"pressure", d.initialConditions.pressure}}},
  };
  writeJsonFile(caseDirectory / "case.json", caseJson);

  // --- geometry.json --------------------------------------------------------
  if (d.geometry.type == "mesh_defined") {  // P12-MESH-003
    writeJsonFile(caseDirectory / "geometry.json", json{{"type", d.geometry.type}});
  } else if (threeDimensional) {  // P12-MESH-006
    writeJsonFile(caseDirectory / "geometry.json", json{{"type", d.geometry.type},
                                                        {"length", d.geometry.length},
                                                        {"height", d.geometry.height},
                                                        {"depth", d.geometry.depth}});
  } else {
    writeJsonFile(caseDirectory / "geometry.json", json{{"type", d.geometry.type},
                                                        {"length", d.geometry.length},
                                                        {"height", d.geometry.height}});
  }

  // --- mesh.json ------------------------------------------------------------
  json meshJson{{"type", d.mesh.type}, {"nx", d.mesh.nx}, {"ny", d.mesh.ny}};
  if (d.mesh.nz > 0) meshJson["nz"] = d.mesh.nz;  // P12-MESH-006: written iff 3D
  if (d.mesh.type == "multiblock") {              // P12-MESH-003
    const auto sideJson = [](const MeshSideRefConfig& ref) {
      return json{{"block", ref.block}, {"side", ref.side}};
    };
    json blocks = json::array();
    for (const auto& block : d.mesh.blocks) {
      json vertices = json::array();
      for (const auto& v : block.vertices) vertices.push_back(json::array({v.x, v.y}));
      blocks.push_back(json{{"name", block.name},
                            {"nx", block.nx},
                            {"ny", block.ny},
                            {"vertices", std::move(vertices)}});
    }
    json interfaces = json::array();
    for (const auto& iface : d.mesh.interfaces) {
      interfaces.push_back(json{{"first", sideJson(iface.first)},
                                {"second", sideJson(iface.second)},
                                {"orientation", iface.reversed ? "reversed" : "aligned"}});
    }
    json patches = json::array();
    for (const auto& patch : d.mesh.patches) {
      json sides = json::array();
      for (const auto& ref : patch.sides) sides.push_back(sideJson(ref));
      patches.push_back(json{{"name", patch.name}, {"sides", std::move(sides)}});
    }
    meshJson = json{{"type", d.mesh.type},
                    {"blocks", std::move(blocks)},
                    {"interfaces", std::move(interfaces)},
                    {"patches", std::move(patches)}};
  }
  if (d.mesh.type == "structured_quad") {  // P12-MESH-001
    json vertices = json::array();
    for (const auto& v : d.mesh.vertices) vertices.push_back(json::array({v.x, v.y}));
    meshJson["vertices"] = std::move(vertices);
  }
  if (d.mesh.grading.has_value()) {  // P12-MESH-002: written iff present
    const auto axisJson = [](const cfd::mesh::AxisGrading& g, bool xAxis) {
      json axis{{"type", gradingTypeName(g.type)}};
      if (g.type == cfd::mesh::GradingType::Geometric) {
        axis["ratio"] = g.ratio;
        axis["cluster"] = gradingClusterName(g.cluster, xAxis);
      }
      return axis;
    };
    meshJson["grading"] =
        json{{"x", axisJson(d.mesh.grading->x, true)}, {"y", axisJson(d.mesh.grading->y, false)}};
  }
  writeJsonFile(caseDirectory / "mesh.json", meshJson);

  // --- physics.json -----------------------------------------------------
  json physicsJson{
      {"model", d.physics.model},
      {"density", d.physics.density},
      {"dynamic_viscosity", d.physics.dynamicViscosity},
  };
  if (d.physics.reynoldsNumber.has_value()) {
    physicsJson["reynolds_number"] = *d.physics.reynoldsNumber;
  }
  if (d.physics.thermal.has_value()) {
    physicsJson["thermal"] = json{{"conductivity", d.physics.thermal->conductivity},
                                  {"specific_heat", d.physics.thermal->specificHeat},
                                  {"initial_temperature", d.physics.thermal->initialTemperature}};
  }
  if (d.physics.turbulence.has_value()) {
    const auto& t = *d.physics.turbulence;
    json turbulenceJson{{"model", t.model}, {"initial_k", t.initialK}};
    if (t.initialEpsilon.has_value()) turbulenceJson["initial_epsilon"] = *t.initialEpsilon;
    if (t.initialOmega.has_value()) turbulenceJson["initial_omega"] = *t.initialOmega;
    if (t.kRelaxation.has_value()) turbulenceJson["k_relaxation"] = *t.kRelaxation;
    if (t.epsilonRelaxation.has_value())
      turbulenceJson["epsilon_relaxation"] = *t.epsilonRelaxation;
    if (t.omegaRelaxation.has_value()) turbulenceJson["omega_relaxation"] = *t.omegaRelaxation;
    physicsJson["turbulence"] = std::move(turbulenceJson);
  }
  if (d.physics.buoyancy.has_value()) {
    const auto& b = *d.physics.buoyancy;
    physicsJson["buoyancy"] = json{
        {"model", "boussinesq"},
        {"beta", b.beta},
        {"reference_temperature", b.referenceTemperature},
        {"gravity", json::array({b.gravity.x, b.gravity.y})},
    };
  }
  // P6-PHYS-001: written whenever non-empty -- an empty vector (no
  // species configured) writes no "species" key at all, so every
  // existing nonspecies case round-trips byte-for-byte identically (same
  // "absent key" as before this field existed).
  if (!d.physics.species.empty()) {
    json speciesJson = json::array();
    for (const auto& s : d.physics.species) {
      speciesJson.push_back(json{{"name", s.name},
                                 {"diffusivity", s.diffusivity},
                                 {"initial_concentration", s.initialConcentration}});
    }
    physicsJson["species"] = std::move(speciesJson);
  }
  // P6-PHYS-002: written iff present.
  if (d.physics.multiphase.has_value()) {
    const auto& m = *d.physics.multiphase;
    auto phaseJson = [](const PhasePhysicsConfig& p) {
      return json{{"name", p.name}, {"density", p.density}, {"viscosity", p.viscosity}};
    };
    physicsJson["multiphase"] = json{{"phase1", phaseJson(m.phase1)},
                                     {"phase2", phaseJson(m.phase2)},
                                     {"initial_alpha", m.initialAlpha},
                                     {"transport_time_step", m.transportTimeStep}};
  }
  // P6-PHYS-003: written iff present. Exactly one of temperature/
  // thermal_coupled -- see CompressiblePhysicsConfig's own header
  // comment.
  if (d.physics.compressible.has_value()) {
    const auto& c = *d.physics.compressible;
    json compressibleJson{{"gas_constant", c.gasConstant},
                          {"specific_heat_pressure", c.specificHeatPressure},
                          {"reference_pressure", c.referencePressure}};
    if (c.thermalCoupled) {
      compressibleJson["thermal_coupled"] = true;
    } else {
      compressibleJson["temperature"] = *c.temperature;
    }
    physicsJson["compressible"] = std::move(compressibleJson);
  }
  writeJsonFile(caseDirectory / "physics.json", physicsJson);

  // --- boundaries.json --------------------------------------------------
  json patchesJson = json::object();
  for (const auto& [patchName, patch] : d.boundaries.patches) {
    json velocityJson{{"type", patch.velocity.type}};
    if (velocityTypeHasValue(patch.velocity.type)) {
      velocityJson["value"] = vectorJson(patch.velocity.value);
    }
    json pressureJson{{"type", patch.pressure.type}, {"value", patch.pressure.value}};
    json patchJson{{"velocity", std::move(velocityJson)}, {"pressure", std::move(pressureJson)}};
    if (patch.temperature.has_value()) {
      json temperatureJson{{"type", patch.temperature->type}};
      if (temperatureTypeHasValue(patch.temperature->type)) {
        temperatureJson["value"] = patch.temperature->value;
      }
      patchJson["temperature"] = std::move(temperatureJson);
    }
    // P6-PHYS-001: same "written only when non-empty" reasoning as
    // physics.json's own "species" key above -- both types
    // (fixed_value/fixed_gradient) always carry "value" (see
    // ConcentrationBoundarySpec's own header comment), so no per-type
    // has-value check is needed the way temperature's three-type set
    // above needs one.
    if (!patch.concentration.empty()) {
      json speciesJson = json::object();
      for (const auto& [speciesName, concentrationSpec] : patch.concentration) {
        speciesJson[speciesName] =
            json{{"type", concentrationSpec.type}, {"value", concentrationSpec.value}};
      }
      patchJson["species"] = std::move(speciesJson);
    }
    // P6-PHYS-002: same "written iff present" reasoning as temperature
    // above.
    if (patch.alpha.has_value()) {
      patchJson["alpha"] = json{{"type", patch.alpha->type}, {"value", patch.alpha->value}};
    }
    patchesJson[patchName] = std::move(patchJson);
  }
  writeJsonFile(caseDirectory / "boundaries.json", json{{"patches", std::move(patchesJson)}});

  // --- solver.json --------------------------------------------------------
  auto linearSolverJson = [](const LinearSolverSpec& s) {
    return json{{"type", s.type},
                {"backend", s.backend},
                {"absolute_tolerance", s.absoluteTolerance},
                {"relative_tolerance", s.relativeTolerance},
                {"max_iterations", s.maxIterations}};
  };
  json solverJson{
      {"type", d.solver.type},
      {"max_iterations", d.solver.maxIterations},
      {"velocity_relaxation", d.solver.velocityRelaxation},
      {"pressure_relaxation", d.solver.pressureRelaxation},
      {"velocity_tolerance", d.solver.velocityTolerance},
      {"pressure_tolerance", d.solver.pressureTolerance},
      {"continuity_tolerance", d.solver.continuityTolerance},
      {"momentum_linear_solver", linearSolverJson(d.solver.momentumSolver)},
      {"pressure_linear_solver", linearSolverJson(d.solver.pressureSolver)},
      {"convection_scheme", d.solver.convectionScheme},
      {"gradient_scheme", d.solver.gradientScheme},
      {"non_orthogonal_corrections", d.solver.nonOrthogonalCorrections},
  };
  // P12-MESH-006: written only when not the default "automatic", so every
  // existing case's solver.json is unchanged.
  if (d.solver.faceFlux != "automatic") solverJson["face_flux"] = d.solver.faceFlux;
  // P12-NUM-004: the "robustness" block is written only when it differs
  // from the defaults, so a default case's solver.json is unchanged; when
  // written, every field is explicit.
  const cfd::solver::SolverRobustnessSettings& r = d.solver.robustness;
  if (!(r == cfd::solver::SolverRobustnessSettings{})) {
    solverJson["robustness"] = json{
        {"convergence_criterion",
         std::string(cfd::solver::convergenceCriterionName(r.convergenceCriterion))},
        {"normalization", json{{"reference_iterations", r.normalization.referenceIterations},
                               {"velocity_tolerance", r.normalization.velocityTolerance},
                               {"pressure_tolerance", r.normalization.pressureTolerance}}},
        {"stagnation_detection",
         json{{"enabled", r.stagnation.enabled},
              {"window", r.stagnation.window},
              {"min_relative_improvement", r.stagnation.minRelativeImprovement},
              {"start_iteration", r.stagnation.startIteration}}},
        {"divergence_detection", json{{"enabled", r.divergence.enabled},
                                      {"window", r.divergence.window},
                                      {"growth_factor", r.divergence.growthFactor},
                                      {"start_iteration", r.divergence.startIteration}}},
        {"adaptive_relaxation", json{{"enabled", r.adaptiveRelaxation.enabled},
                                     {"min_velocity", r.adaptiveRelaxation.minVelocity},
                                     {"max_velocity", r.adaptiveRelaxation.maxVelocity},
                                     {"min_pressure", r.adaptiveRelaxation.minPressure},
                                     {"max_pressure", r.adaptiveRelaxation.maxPressure}}},
        {"linear_solver_fallback", json{{"enabled", r.linearSolverFallback.enabled},
                                        {"max_attempts", r.linearSolverFallback.maxAttempts}}},
    };
  }
  writeJsonFile(caseDirectory / "solver.json", solverJson);
}

}  // namespace cfd::io
