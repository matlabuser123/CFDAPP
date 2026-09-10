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
      {"initial_conditions", json{{"velocity", json::array({d.initialConditions.velocity.x,
                                                            d.initialConditions.velocity.y})},
                                  {"pressure", d.initialConditions.pressure}}},
  };
  writeJsonFile(caseDirectory / "case.json", caseJson);

  // --- geometry.json --------------------------------------------------------
  writeJsonFile(caseDirectory / "geometry.json", json{{"type", d.geometry.type},
                                                      {"length", d.geometry.length},
                                                      {"height", d.geometry.height}});

  // --- mesh.json ------------------------------------------------------------
  writeJsonFile(caseDirectory / "mesh.json",
                json{{"type", d.mesh.type}, {"nx", d.mesh.nx}, {"ny", d.mesh.ny}});

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
  writeJsonFile(caseDirectory / "physics.json", physicsJson);

  // --- boundaries.json --------------------------------------------------
  json patchesJson = json::object();
  for (const auto& [patchName, patch] : d.boundaries.patches) {
    json velocityJson{{"type", patch.velocity.type}};
    if (velocityTypeHasValue(patch.velocity.type)) {
      velocityJson["value"] = json::array({patch.velocity.value.x, patch.velocity.value.y});
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
    patchesJson[patchName] = std::move(patchJson);
  }
  writeJsonFile(caseDirectory / "boundaries.json", json{{"patches", std::move(patchesJson)}});

  // --- solver.json --------------------------------------------------------
  auto linearSolverJson = [](const LinearSolverSpec& s) {
    return json{{"type", s.type},
                {"absolute_tolerance", s.absoluteTolerance},
                {"relative_tolerance", s.relativeTolerance},
                {"max_iterations", s.maxIterations}};
  };
  writeJsonFile(caseDirectory / "solver.json",
                json{
                    {"type", d.solver.type},
                    {"max_iterations", d.solver.maxIterations},
                    {"velocity_relaxation", d.solver.velocityRelaxation},
                    {"pressure_relaxation", d.solver.pressureRelaxation},
                    {"velocity_tolerance", d.solver.velocityTolerance},
                    {"pressure_tolerance", d.solver.pressureTolerance},
                    {"continuity_tolerance", d.solver.continuityTolerance},
                    {"momentum_linear_solver", linearSolverJson(d.solver.momentumSolver)},
                    {"pressure_linear_solver", linearSolverJson(d.solver.pressureSolver)},
                });
}

}  // namespace cfd::io
