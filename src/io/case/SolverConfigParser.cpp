#include "JsonUtil.hpp"
#include "Parsers.hpp"

namespace cfd::io::detail {

using cfd::io::LinearSolverSpec;
using cfd::io::SolverConfig;

namespace {

LinearSolverSpec parseLinearSolverSpec(const nlohmann::json& json,
                                       const std::filesystem::path& path,
                                       const std::string& fieldName) {
  requireField(json, path, fieldName);
  const auto& block = json.at(fieldName);
  requireObject(block, path, fieldName);
  rejectUnknownKeys(block, path, fieldName,
                    {"type", "absolute_tolerance", "relative_tolerance", "max_iterations"});

  LinearSolverSpec spec;
  spec.type = getRequiredString(block, path, "type", fieldName + ".type");
  // The only Krylov solver SIMPLE actually constructs (SIMPLE.cpp always
  // uses BiCGSTAB for both the momentum and pressure-correction solves).
  if (spec.type != "BiCGSTAB") {
    throwConfigError(path, fieldName + ".type", "be one of: BiCGSTAB", spec.type);
  }

  spec.absoluteTolerance =
      getRequiredReal(block, path, "absolute_tolerance", fieldName + ".absolute_tolerance");
  if (!(spec.absoluteTolerance > 0.0)) {
    throwConfigError(path, fieldName + ".absolute_tolerance", "be > 0",
                     std::to_string(spec.absoluteTolerance));
  }
  spec.relativeTolerance =
      getRequiredReal(block, path, "relative_tolerance", fieldName + ".relative_tolerance");
  if (!(spec.relativeTolerance > 0.0)) {
    throwConfigError(path, fieldName + ".relative_tolerance", "be > 0",
                     std::to_string(spec.relativeTolerance));
  }
  spec.maxIterations =
      getRequiredIndex(block, path, "max_iterations", fieldName + ".max_iterations");
  if (spec.maxIterations == 0) {
    throwConfigError(path, fieldName + ".max_iterations", "be > 0", "0");
  }
  return spec;
}

}  // namespace

SolverConfig parseSolverConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "solver.json",
                    {"type", "max_iterations", "velocity_relaxation", "pressure_relaxation",
                     "velocity_tolerance", "pressure_tolerance", "continuity_tolerance",
                     "momentum_linear_solver", "pressure_linear_solver"});

  SolverConfig config;
  config.type = getRequiredString(json, path, "type");
  // The only pressure-velocity coupling algorithm implemented (TODO.md P0
  // -- SIMPLE); PISO/PIMPLE are explicitly out of scope for this phase
  // (TODO.md P1 section 53).
  if (config.type != "SIMPLE") {
    throwConfigError(path, "type", "be one of: SIMPLE", config.type);
  }

  config.maxIterations = getRequiredIndex(json, path, "max_iterations");
  if (config.maxIterations == 0) {
    throwConfigError(path, "max_iterations", "be > 0", "0");
  }

  auto requireRelaxation = [&](std::string_view field) {
    const Real value = getRequiredReal(json, path, field);
    if (!(value > 0.0 && value <= 1.0)) {
      throwConfigError(path, field, "satisfy 0 < value <= 1", std::to_string(value));
    }
    return value;
  };
  config.velocityRelaxation = requireRelaxation("velocity_relaxation");
  config.pressureRelaxation = requireRelaxation("pressure_relaxation");

  auto requirePositiveTolerance = [&](std::string_view field) {
    const Real value = getRequiredReal(json, path, field);
    if (!(value > 0.0)) {
      throwConfigError(path, field, "be > 0", std::to_string(value));
    }
    return value;
  };
  config.velocityTolerance = requirePositiveTolerance("velocity_tolerance");
  config.pressureTolerance = requirePositiveTolerance("pressure_tolerance");
  config.continuityTolerance = requirePositiveTolerance("continuity_tolerance");

  config.momentumSolver = parseLinearSolverSpec(json, path, "momentum_linear_solver");
  config.pressureSolver = parseLinearSolverSpec(json, path, "pressure_linear_solver");
  return config;
}

}  // namespace cfd::io::detail
