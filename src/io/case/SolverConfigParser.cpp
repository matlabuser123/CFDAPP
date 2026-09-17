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
  rejectUnknownKeys(
      block, path, fieldName,
      {"type", "backend", "absolute_tolerance", "relative_tolerance", "max_iterations"});

  LinearSolverSpec spec;
  spec.type = getRequiredString(block, path, "type", fieldName + ".type");
  // P6-GPU-002: CG is now constructible from case config too (previously
  // only BiCGSTAB); see this struct's own header comment on why BiCGSTAB
  // remains the only well-posed choice for SIMPLE's own systems in
  // general.
  // P12-NUM-004: GMRES (restarted, CPU only) added.
  if (spec.type != "BiCGSTAB" && spec.type != "CG" && spec.type != "GMRES") {
    throwConfigError(path, fieldName + ".type", "be one of: BiCGSTAB, CG, GMRES", spec.type);
  }

  // P6-GPU-002: optional, defaults to "CPU" -- absent in every
  // pre-P6-GPU-002 case file, which keeps every existing case parsing
  // (and its CPU-only behavior) unchanged.
  spec.backend = getOptionalString(block, path, "backend", "CPU", fieldName + ".backend");
  if (spec.backend != "CPU" && spec.backend != "GPU") {
    throwConfigError(path, fieldName + ".backend", "be one of: CPU, GPU", spec.backend);
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

// P12-NUM-004: the optional "robustness" block. Every key is optional; an
// absent key keeps cfd::solver::SolverRobustnessSettings's default (every
// feature disabled). Each constraint is checked here with the offending
// field named -- the same constraints validateSolverRobustnessSettings
// enforces for a programmatic caller.
cfd::solver::SolverRobustnessSettings parseRobustness(const nlohmann::json& json,
                                                      const std::filesystem::path& path,
                                                      Real velocityRelaxation,
                                                      Real pressureRelaxation) {
  const std::string prefix = "robustness";
  const auto& block = json.at("robustness");
  requireObject(block, path, prefix);
  rejectUnknownKeys(block, path, prefix,
                    {"convergence_criterion", "normalization", "stagnation_detection",
                     "divergence_detection", "adaptive_relaxation", "linear_solver_fallback"});

  cfd::solver::SolverRobustnessSettings settings;

  const std::string criterion = getOptionalString(block, path, "convergence_criterion", "absolute",
                                                  prefix + ".convergence_criterion");
  if (criterion == "absolute") {
    settings.convergenceCriterion = cfd::solver::ConvergenceCriterion::Absolute;
  } else if (criterion == "normalized") {
    settings.convergenceCriterion = cfd::solver::ConvergenceCriterion::Normalized;
  } else {
    throwConfigError(path, prefix + ".convergence_criterion", "be one of: absolute, normalized",
                     criterion);
  }

  const auto subBlock = [&](const char* name,
                            const std::vector<std::string_view>& keys) -> const nlohmann::json* {
    if (!block.contains(name)) return nullptr;
    const auto& node = block.at(name);
    requireObject(node, path, prefix + "." + name);
    rejectUnknownKeys(node, path, prefix + "." + name, keys);
    return &node;
  };
  const auto integerAtLeast = [&](const nlohmann::json& node, const std::string& label,
                                  const char* key, int fallback, int minimum) {
    const int value = getOptionalInt(node, path, key, fallback, label + "." + key);
    if (value < minimum) {
      throwConfigError(path, label + "." + key, "be >= " + std::to_string(minimum),
                       std::to_string(value));
    }
    return value;
  };
  const auto openUnit = [&](const nlohmann::json& node, const std::string& label, const char* key,
                            Real fallback) {
    const Real value = getOptionalReal(node, path, key, fallback, label + "." + key);
    if (!(value > 0.0 && value < 1.0)) {
      throwConfigError(path, label + "." + key, "satisfy 0 < value < 1", std::to_string(value));
    }
    return value;
  };
  const auto relaxationBound = [&](const nlohmann::json& node, const std::string& label,
                                   const char* key, Real fallback) {
    const Real value = getOptionalReal(node, path, key, fallback, label + "." + key);
    if (!(value > 0.0 && value <= 1.0)) {
      throwConfigError(path, label + "." + key, "satisfy 0 < value <= 1", std::to_string(value));
    }
    return value;
  };
  const int maxWindow = static_cast<int>(cfd::solver::kMaxDetectionWindow);
  const auto window = [&](const nlohmann::json& node, const std::string& label, Index fallback) {
    const int value = integerAtLeast(node, label, "window", static_cast<int>(fallback), 2);
    if (value > maxWindow) {
      throwConfigError(path, label + ".window", "be <= " + std::to_string(maxWindow),
                       std::to_string(value));
    }
    return static_cast<Index>(value);
  };

  if (const auto* node = subBlock(
          "normalization", {"reference_iterations", "velocity_tolerance", "pressure_tolerance"})) {
    const std::string label = prefix + ".normalization";
    auto& n = settings.normalization;
    n.referenceIterations = static_cast<Index>(integerAtLeast(
        *node, label, "reference_iterations", static_cast<int>(n.referenceIterations), 1));
    n.velocityTolerance = openUnit(*node, label, "velocity_tolerance", n.velocityTolerance);
    n.pressureTolerance = openUnit(*node, label, "pressure_tolerance", n.pressureTolerance);
  }

  if (const auto* node =
          subBlock("stagnation_detection",
                   {"enabled", "window", "min_relative_improvement", "start_iteration"})) {
    const std::string label = prefix + ".stagnation_detection";
    auto& s = settings.stagnation;
    s.enabled = getOptionalBool(*node, path, "enabled", false, label + ".enabled");
    s.window = window(*node, label, s.window);
    s.minRelativeImprovement =
        openUnit(*node, label, "min_relative_improvement", s.minRelativeImprovement);
    s.startIteration = static_cast<Index>(
        integerAtLeast(*node, label, "start_iteration", static_cast<int>(s.startIteration), 0));
  }

  if (const auto* node = subBlock("divergence_detection",
                                  {"enabled", "window", "growth_factor", "start_iteration"})) {
    const std::string label = prefix + ".divergence_detection";
    auto& d = settings.divergence;
    d.enabled = getOptionalBool(*node, path, "enabled", false, label + ".enabled");
    d.window = window(*node, label, d.window);
    d.growthFactor =
        getOptionalReal(*node, path, "growth_factor", d.growthFactor, label + ".growth_factor");
    if (!(d.growthFactor > 1.0)) {
      throwConfigError(path, label + ".growth_factor", "be > 1", std::to_string(d.growthFactor));
    }
    d.startIteration = static_cast<Index>(
        integerAtLeast(*node, label, "start_iteration", static_cast<int>(d.startIteration), 0));
  }

  if (const auto* node = subBlock("adaptive_relaxation", {"enabled", "min_velocity", "max_velocity",
                                                          "min_pressure", "max_pressure"})) {
    const std::string label = prefix + ".adaptive_relaxation";
    auto& a = settings.adaptiveRelaxation;
    a.enabled = getOptionalBool(*node, path, "enabled", false, label + ".enabled");
    a.minVelocity = relaxationBound(*node, label, "min_velocity", a.minVelocity);
    a.maxVelocity = relaxationBound(*node, label, "max_velocity", a.maxVelocity);
    a.minPressure = relaxationBound(*node, label, "min_pressure", a.minPressure);
    a.maxPressure = relaxationBound(*node, label, "max_pressure", a.maxPressure);
    if (a.minVelocity > a.maxVelocity) {
      throwConfigError(path, label + ".min_velocity", "be <= max_velocity",
                       std::to_string(a.minVelocity));
    }
    if (a.minPressure > a.maxPressure) {
      throwConfigError(path, label + ".min_pressure", "be <= max_pressure",
                       std::to_string(a.minPressure));
    }
    if (a.enabled &&
        !(velocityRelaxation >= a.minVelocity && velocityRelaxation <= a.maxVelocity)) {
      throwConfigError(path, "velocity_relaxation",
                       "lie within [adaptive_relaxation.min_velocity, max_velocity] when adaptive "
                       "relaxation is enabled",
                       std::to_string(velocityRelaxation));
    }
    if (a.enabled &&
        !(pressureRelaxation >= a.minPressure && pressureRelaxation <= a.maxPressure)) {
      throwConfigError(path, "pressure_relaxation",
                       "lie within [adaptive_relaxation.min_pressure, max_pressure] when adaptive "
                       "relaxation is enabled",
                       std::to_string(pressureRelaxation));
    }
  }

  if (const auto* node = subBlock("linear_solver_fallback", {"enabled", "max_attempts"})) {
    const std::string label = prefix + ".linear_solver_fallback";
    auto& f = settings.linearSolverFallback;
    f.enabled = getOptionalBool(*node, path, "enabled", false, label + ".enabled");
    const int attempts =
        integerAtLeast(*node, label, "max_attempts", static_cast<int>(f.maxAttempts), 0);
    const int maxAttempts = static_cast<int>(cfd::algebra::kMaxLinearSolverFallbackAttempts);
    if (attempts > maxAttempts) {
      throwConfigError(path, label + ".max_attempts", "be <= " + std::to_string(maxAttempts),
                       std::to_string(attempts));
    }
    f.maxAttempts = static_cast<Index>(attempts);
  }
  return settings;
}

}  // namespace

SolverConfig parseSolverConfig(const nlohmann::json& json, const std::filesystem::path& path) {
  requireObject(json, path);
  rejectUnknownKeys(json, path, "solver.json",
                    {"type", "max_iterations", "velocity_relaxation", "pressure_relaxation",
                     "velocity_tolerance", "pressure_tolerance", "continuity_tolerance",
                     "momentum_linear_solver", "pressure_linear_solver", "convection_scheme",
                     "gradient_scheme", "non_orthogonal_corrections", "robustness", "face_flux"});

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

  // P12-NUM-001: optional, defaults to "upwind" -- absent in every
  // pre-P12-NUM-001 case file, which keeps every existing case parsing
  // (and its first-order-upwind behavior) unchanged.
  config.convectionScheme =
      getOptionalString(json, path, "convection_scheme", "upwind", "convection_scheme");
  if (config.convectionScheme != "upwind" && config.convectionScheme != "central" &&
      config.convectionScheme != "linear_upwind" && config.convectionScheme != "quick") {
    throwConfigError(path, "convection_scheme", "be one of: upwind, central, linear_upwind, quick",
                     config.convectionScheme);
  }

  // P12-NUM-002: optional, defaults to "green_gauss" -- absent in every
  // pre-P12-NUM-002 case file, which keeps every existing case parsing
  // (and its Green-Gauss-gradient behavior) unchanged.
  config.gradientScheme =
      getOptionalString(json, path, "gradient_scheme", "green_gauss", "gradient_scheme");
  if (config.gradientScheme != "green_gauss" && config.gradientScheme != "least_squares") {
    throwConfigError(path, "gradient_scheme", "be one of: green_gauss, least_squares",
                     config.gradientScheme);
  }

  // P12-NUM-003: optional, defaults to 0 -- absent in every
  // pre-P12-NUM-003 case file, which keeps every existing case parsing
  // (and its uncorrected-diffusion behavior) unchanged.
  const int nonOrthogonalCorrections = getOptionalInt(json, path, "non_orthogonal_corrections", 0);
  if (nonOrthogonalCorrections < 0) {
    throwConfigError(path, "non_orthogonal_corrections", "be >= 0",
                     std::to_string(nonOrthogonalCorrections));
  }
  config.nonOrthogonalCorrections = static_cast<Index>(nonOrthogonalCorrections);

  // P12-MESH-006: optional, defaults to "automatic" (linear flux in 2D -- every
  // existing case, unchanged; Rhie-Chow in 3D). See SIMPLESettings.hpp.
  config.faceFlux = getOptionalString(json, path, "face_flux", "automatic", "face_flux");
  if (config.faceFlux != "automatic" && config.faceFlux != "linear" &&
      config.faceFlux != "rhie_chow") {
    throwConfigError(path, "face_flux", "be one of: automatic, linear, rhie_chow", config.faceFlux);
  }

  // P12-NUM-004: optional -- absent in every pre-P12-NUM-004 case file,
  // which keeps every existing case parsing into exactly today's solver.
  if (json.contains("robustness")) {
    config.robustness =
        parseRobustness(json, path, config.velocityRelaxation, config.pressureRelaxation);
  }
  return config;
}

}  // namespace cfd::io::detail
