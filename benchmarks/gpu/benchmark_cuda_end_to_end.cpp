// P7-PERF-001 -- Performance: real, end-to-end CPU-vs-GPU production
// solve timing -- the whole point of this file is that "GPU acceleration"
// is judged by a complete SIMPLE::solve() (mesh setup through final
// converged/max-iteration result), never by an isolated SpMV or CUDA
// kernel timing (benchmarks/gpu/benchmark_gpu_spmv.cpp and
// benchmarks/gpu/benchmark_gpu_preconditioner.cpp's own linear-system-
// level section remain useful *microbenchmarks*, but neither one alone
// satisfies this task). Reuses the exact production assembly/SIMPLE code
// benchmarks/cpu/benchmark_runner.cpp already established the convention
// of calling directly, unmodified, for the same case (a lid-driven
// cavity) -- CPU and GPU configurations differ *only* in
// LinearSolverSettings::backend, nothing else (same solver type,
// tolerances, relaxation, preconditioner (none, for both), initial
// conditions).
//
// Per-grid outer-iteration budgets differ across grid sizes (documented
// in gridConfigs, main()'s own local table below) purely to keep total
// wall-clock time for this tool bounded -- CPU and GPU always use the *same* budget at a given
// grid size, so the CPU-vs-GPU comparison at that grid size is still a
// true apples-to-apples measurement; only cross-grid-size "total time"
// comparisons need the per-iteration metric instead (also reported).
//
// Usage: run with no arguments; writes results/performance/
// cuda_end_to_end/{runs.csv,summary.csv,metadata.json} and prints
// summary tables to stdout. Long-running (minutes) by design -- not part
// of the routine CTest suite, same convention as every other file under
// benchmarks/.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/core/Version.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::makeLinearSolver;
using cfd::algebra::PreconditionerType;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::assembleRelaxedMomentumComponent;
using cfd::pressure_velocity::computeMomentumResponseCoefficient;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// ---------------------------------------------------------------------
// Case setup (mirrors benchmarks/cpu/benchmark_runner.cpp's own cavity)
// ---------------------------------------------------------------------

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh, Real lidSpeed) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{lidSpeed, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeCavityPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return boundaries;
}

// Same momentum/pressure linear-solver settings for CPU and GPU --
// `backend` is the *only* field that differs between the two
// configurations this file ever constructs (section 3's own "keep
// equivalent" requirement). No preconditioner on either side: isolating
// backend as the single variable under test is the point of this
// specific benchmark (P6-GPU-003's Jacobi preconditioner has its own
// dedicated before/after comparison in benchmark_gpu_preconditioner.cpp).
SIMPLESettings makeCavitySettings(Index outerIterations, LinearSolverBackend backend) {
  SIMPLESettings settings;
  settings.maxIterations = outerIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-10;   // effectively unreachable -- always runs the full budget,
  settings.pressureTolerance = 1e-10;   // matching benchmark_runner.cpp's own "fixed iteration
  settings.continuityTolerance = 1e-10; // budget" rationale (comparable runtime/iteration).

  settings.momentumSolver.type = LinearSolverType::BiCGSTAB;
  settings.momentumSolver.backend = backend;
  settings.momentumSolver.maxIterations = 1000;
  settings.momentumSolver.absoluteTolerance = 1e-8;
  settings.momentumSolver.relativeTolerance = 1e-6;
  settings.momentumSolver.preconditioner = PreconditionerType::None;

  settings.pressureSolver.type = LinearSolverType::BiCGSTAB;
  settings.pressureSolver.backend = backend;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-7;
  settings.pressureSolver.relativeTolerance = 1e-5;
  settings.pressureSolver.preconditioner = PreconditionerType::None;

  return settings;
}

// ---------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------

struct Stats {
  double min{};
  double median{};
  double mean{};
  double stddev{};
};

Stats computeStats(std::vector<double> values) {
  Stats s;
  if (values.empty()) return s;
  std::sort(values.begin(), values.end());
  s.min = values.front();
  const std::size_t mid = values.size() / 2;
  s.median = (values.size() % 2 == 0) ? 0.5 * (values[mid - 1] + values[mid]) : values[mid];
  s.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
  double variance = 0.0;
  for (double v : values) variance += (v - s.mean) * (v - s.mean);
  variance /= static_cast<double>(values.size());
  s.stddev = std::sqrt(variance);
  return s;
}

// ---------------------------------------------------------------------
// One end-to-end production solve
// ---------------------------------------------------------------------

struct RunResult {
  double meshSetupSeconds{};
  double simpleSolveSeconds{};
  double totalSeconds{};  // mesh setup + SIMPLE::solve() -- the true end-to-end number.
  Index outerIterations{};
  SIMPLEStatus status{};
  bool ranCleanly{};
  Real finalUResidual{};
  Real finalVResidual{};
  Real finalPressureResidual{};
  Real finalContinuityResidual{};
  Real globalMassImbalance{};
  VectorField velocity;
  ScalarField pressure;

  // Populated only for backend == GPU (all zero on CPU runs, matching
  // GPUExecutionStats's own "stays zero on a path that never touches a
  // device" contract).
  std::uint64_t hostToDeviceCalls{};
  std::uint64_t hostToDeviceBytes{};
  std::uint64_t deviceToHostCalls{};
  std::uint64_t deviceToHostBytes{};
  double uploadSeconds{};
  double downloadSeconds{};
  double kernelSeconds{};
  double gpuSolveSeconds{};
  std::uint64_t gpuLinearSolves{};
  std::uint64_t gpuLinearSolverIterations{};
};

RunResult runOnceEndToEnd(Index nx, Index ny, Index outerIterations, LinearSolverBackend backend) {
  if (backend == LinearSolverBackend::GPU) cfd::gpu::resetGpuExecutionStats();

  cfd::Timer totalTimer;

  cfd::Timer meshTimer;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  const double meshSetupSeconds = meshTimer.elapsedSeconds();

  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, 1.0);
  const auto pressureBoundaries = makeCavityPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Index n = mesh.numberOfCells();

  VectorField velocity(n, Vector2{0.0, 0.0});
  ScalarField pressure(n, 0.0);

  cfd::Timer solveTimer;
  const SIMPLE simple(makeCavitySettings(outerIterations, backend));
  const SIMPLEResult result =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
  const double simpleSolveSeconds = solveTimer.elapsedSeconds();
  const double totalSeconds = totalTimer.elapsedSeconds();

  RunResult run;
  run.meshSetupSeconds = meshSetupSeconds;
  run.simpleSolveSeconds = simpleSolveSeconds;
  run.totalSeconds = totalSeconds;
  run.outerIterations = result.iterations;
  run.status = result.status;
  run.ranCleanly =
      (result.status == SIMPLEStatus::Converged || result.status == SIMPLEStatus::MaxIterations);
  run.finalUResidual = result.finalUResidual;
  run.finalVResidual = result.finalVResidual;
  run.finalPressureResidual = result.finalPressureResidual;
  run.finalContinuityResidual = result.finalContinuityResidual;
  run.globalMassImbalance = result.globalMassImbalance;
  run.velocity = result.velocity;
  run.pressure = result.pressure;

  if (backend == LinearSolverBackend::GPU) {
    const auto& stats = cfd::gpu::gpuExecutionStats();
    run.hostToDeviceCalls = stats.hostToDeviceCalls;
    run.hostToDeviceBytes = stats.hostToDeviceBytes;
    run.deviceToHostCalls = stats.deviceToHostCalls;
    run.deviceToHostBytes = stats.deviceToHostBytes;
    run.uploadSeconds = stats.uploadSeconds;
    run.downloadSeconds = stats.downloadSeconds;
    run.kernelSeconds = stats.kernelSeconds;
    run.gpuSolveSeconds = stats.gpuSolveSeconds;
    run.gpuLinearSolves = stats.gpuLinearSolves;
    run.gpuLinearSolverIterations = stats.gpuLinearSolverIterations;
  }

  return run;
}

// ---------------------------------------------------------------------
// One-iteration assembly/solver breakdown (mirrors benchmark_runner.cpp)
// ---------------------------------------------------------------------

struct BreakdownResult {
  double momentumAssemblySeconds{};
  double pressureAssemblySeconds{};
  double momentumSolveSeconds{};
  double pressureSolveSeconds{};
};

BreakdownResult runBreakdown(const Mesh& mesh, const FluidProperties& fluid,
                             const BoundaryConditionSet& velocityBoundaries,
                             const BoundaryConditionSet& pressureBoundaries,
                             LinearSolverBackend backend) {
  const Index n = mesh.numberOfCells();
  VectorField velocity(n, Vector2{0.0, 0.0});
  ScalarField pressure(n, 0.0);
  const ScalarField effectiveViscosity(n, fluid.dynamicViscosity());
  const auto massFlux0 = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  ScalarField previousU(n, 0.0);

  BreakdownResult breakdown;

  cfd::Timer momentumAssemblyTimer;
  const auto momentumU = assembleRelaxedMomentumComponent(
      mesh, velocity, pressure, massFlux0, effectiveViscosity, velocityBoundaries,
      pressureBoundaries, VelocityComponent::U, previousU, 0.7);
  breakdown.momentumAssemblySeconds = momentumAssemblyTimer.elapsedSeconds();

  LinearSolverSettings momentumSettings;
  momentumSettings.absoluteTolerance = 1e-8;
  momentumSettings.relativeTolerance = 1e-6;
  momentumSettings.maxIterations = 1000;
  momentumSettings.type = LinearSolverType::BiCGSTAB;
  momentumSettings.backend = backend;

  cfd::Timer momentumSolveTimer;
  const auto momentumSolver = makeLinearSolver(momentumSettings);
  const auto momentumSolve = momentumSolver->solve(momentumU.system);
  breakdown.momentumSolveSeconds = momentumSolveTimer.elapsedSeconds();
  (void)momentumSolve;

  const auto uResponse = computeMomentumResponseCoefficient(mesh, momentumU.diagonal);
  const auto vResponse = uResponse;

  cfd::Timer pressureAssemblyTimer;
  const auto pressureAssembly = assemblePressureCorrection(mesh, massFlux0, uResponse, vResponse,
                                                            fluid.density(), 0, pressureBoundaries);
  breakdown.pressureAssemblySeconds = pressureAssemblyTimer.elapsedSeconds();

  LinearSolverSettings pressureSettings;
  pressureSettings.absoluteTolerance = 1e-7;
  pressureSettings.relativeTolerance = 1e-5;
  pressureSettings.maxIterations = 5000;
  pressureSettings.type = LinearSolverType::BiCGSTAB;
  pressureSettings.backend = backend;

  cfd::Timer pressureSolveTimer;
  const auto pressureSolver = makeLinearSolver(pressureSettings);
  const auto pressureSolve = pressureSolver->solve(pressureAssembly.system);
  breakdown.pressureSolveSeconds = pressureSolveTimer.elapsedSeconds();
  (void)pressureSolve;

  return breakdown;
}

// ---------------------------------------------------------------------
// Numerical equivalence
// ---------------------------------------------------------------------

struct Equivalence {
  Real maxAbsVelocityError{};
  Real maxRelVelocityError{};
  Real maxAbsPressureError{};
  Real maxRelPressureError{};
};

Equivalence compareResults(const RunResult& cpu, const RunResult& gpu) {
  Equivalence eq;
  for (Index i = 0; i < cpu.velocity.size(); ++i) {
    const Real dx = std::abs(cpu.velocity[i].x - gpu.velocity[i].x);
    const Real dy = std::abs(cpu.velocity[i].y - gpu.velocity[i].y);
    eq.maxAbsVelocityError = std::max({eq.maxAbsVelocityError, dx, dy});
    if (std::abs(cpu.velocity[i].x) > 1e-8) {
      eq.maxRelVelocityError = std::max(eq.maxRelVelocityError, dx / std::abs(cpu.velocity[i].x));
    }
    if (std::abs(cpu.velocity[i].y) > 1e-8) {
      eq.maxRelVelocityError = std::max(eq.maxRelVelocityError, dy / std::abs(cpu.velocity[i].y));
    }
  }
  for (Index i = 0; i < cpu.pressure.size(); ++i) {
    const Real dp = std::abs(cpu.pressure[i] - gpu.pressure[i]);
    eq.maxAbsPressureError = std::max(eq.maxAbsPressureError, dp);
    if (std::abs(cpu.pressure[i]) > 1e-8) {
      eq.maxRelPressureError = std::max(eq.maxRelPressureError, dp / std::abs(cpu.pressure[i]));
    }
  }
  return eq;
}

// ---------------------------------------------------------------------
// Grid matrix
// ---------------------------------------------------------------------

struct GridConfig {
  Index nx, ny;
  Index outerIterations;
  int repeats;
};

const char* statusName(SIMPLEStatus status) {
  switch (status) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration: return "InvalidConfiguration";
    default: return "Cancelled";
  }
}

}  // namespace

int main() {
  const std::string outDir = "results/performance/cuda_end_to_end";
  std::filesystem::create_directories(outDir);

  std::cout << "cfd_benchmark_cuda_end_to_end -- CFDApp version " << cfd::core::versionString()
            << "\n";
  const bool gpuAvailable = cfd::gpu::cudaAvailable();
  std::cout << "cuda_available_at_runtime: " << (gpuAvailable ? "yes" : "no") << "\n";
  if (!gpuAvailable) {
    std::cout << "no usable CUDA device at runtime -- cannot measure a real CPU-vs-GPU "
                "end-to-end comparison, exiting.\n";
    return 0;
  }

  // Section 4's mandatory grid set, plus 640x640 attempted per section 4/
  // 21. Outer-iteration budgets shrink for the larger grids purely to
  // keep this tool's own wall-clock time bounded -- see this file's own
  // header comment; CPU and GPU always share the same budget at a given
  // grid.
  const std::vector<GridConfig> gridConfigs = {
      {20, 20, 200, 3}, {40, 40, 200, 3}, {80, 80, 200, 3},
      {160, 160, 60, 3}, {320, 320, 20, 3}, {640, 640, 8, 1},
  };

  // Section 7: one untimed cold run isolates CUDA context/driver
  // initialization + first-kernel-load cost, reported separately, so it
  // does not distort every grid's own "warm" GPU numbers.
  std::cout << "\nCold GPU warm-up (CUDA context init, first kernel load) -- untimed for the "
              "per-grid results below, reported separately.\n";
  cfd::Timer coldTimer;
  (void)runOnceEndToEnd(20, 20, 5, LinearSolverBackend::GPU);
  const double coldGpuSeconds = coldTimer.elapsedSeconds();
  std::cout << "cold_gpu_first_use_seconds: " << coldGpuSeconds << "\n";

  std::ofstream runsCsv(outDir + "/runs.csv");
  runsCsv << "grid,cells,backend,repeat,outer_iterations,total_seconds,mesh_setup_seconds,"
             "simple_solve_seconds,status,ran_cleanly,mass_imbalance,h2d_calls,h2d_bytes,"
             "d2h_calls,d2h_bytes,upload_seconds,download_seconds,kernel_seconds,"
             "gpu_solve_seconds,gpu_linear_solves,gpu_linear_solver_iterations\n";

  nlohmann::json summaryJson = nlohmann::json::array();
  std::vector<std::string> gridSummaryLines;

  std::cout << "\ngrid | cells | backend | outer_iters | total_min_s | total_median_s | "
              "total_mean_s | total_stddev_s | per_iter_ms | transfer_pct | converged\n";

  for (const auto& cfg : gridConfigs) {
    const std::string gridLabel = std::to_string(cfg.nx) + "x" + std::to_string(cfg.ny);
    const Index cells = cfg.nx * cfg.ny;

    // One-iteration assembly/solver breakdown, CPU and GPU, before the
    // repeated full-SIMPLE timing below.
    const Mesh breakdownMesh = MeshGeometry::createCartesian2D(cfg.nx, cfg.ny, 1.0, 1.0);
    const auto breakdownVelocityBoundaries = makeCavityVelocityBoundaries(breakdownMesh, 1.0);
    const auto breakdownPressureBoundaries = makeCavityPressureBoundaries(breakdownMesh);
    const FluidProperties fluid(1.0, 0.01);
    const auto cpuBreakdown = runBreakdown(breakdownMesh, fluid, breakdownVelocityBoundaries,
                                           breakdownPressureBoundaries, LinearSolverBackend::CPU);
    const auto gpuBreakdown = runBreakdown(breakdownMesh, fluid, breakdownVelocityBoundaries,
                                           breakdownPressureBoundaries, LinearSolverBackend::GPU);

    std::vector<RunResult> cpuRuns;
    std::vector<RunResult> gpuRuns;

    for (int r = 0; r < cfg.repeats; ++r) {
      cpuRuns.push_back(runOnceEndToEnd(cfg.nx, cfg.ny, cfg.outerIterations, LinearSolverBackend::CPU));
    }
    for (int r = 0; r < cfg.repeats; ++r) {
      gpuRuns.push_back(runOnceEndToEnd(cfg.nx, cfg.ny, cfg.outerIterations, LinearSolverBackend::GPU));
    }

    const auto writeRuns = [&](const std::vector<RunResult>& runs, const char* backendName) {
      int repeatIndex = 0;
      for (const auto& run : runs) {
        runsCsv << gridLabel << "," << cells << "," << backendName << "," << repeatIndex << ","
                << run.outerIterations << "," << run.totalSeconds << "," << run.meshSetupSeconds
                << "," << run.simpleSolveSeconds << "," << statusName(run.status) << ","
                << (run.ranCleanly ? "yes" : "no") << "," << std::abs(run.globalMassImbalance)
                << "," << run.hostToDeviceCalls << "," << run.hostToDeviceBytes << ","
                << run.deviceToHostCalls << "," << run.deviceToHostBytes << ","
                << run.uploadSeconds << "," << run.downloadSeconds << "," << run.kernelSeconds
                << "," << run.gpuSolveSeconds << "," << run.gpuLinearSolves << ","
                << run.gpuLinearSolverIterations << "\n";
        ++repeatIndex;
      }
    };
    writeRuns(cpuRuns, "CPU");
    writeRuns(gpuRuns, "GPU");

    std::vector<double> cpuTotals, gpuTotals;
    for (const auto& r : cpuRuns) cpuTotals.push_back(r.totalSeconds);
    for (const auto& r : gpuRuns) gpuTotals.push_back(r.totalSeconds);
    const Stats cpuStats = computeStats(cpuTotals);
    const Stats gpuStats = computeStats(gpuTotals);

    const bool allCpuClean = std::all_of(cpuRuns.begin(), cpuRuns.end(),
                                         [](const RunResult& r) { return r.ranCleanly; });
    const bool allGpuClean = std::all_of(gpuRuns.begin(), gpuRuns.end(),
                                         [](const RunResult& r) { return r.ranCleanly; });

    const double cpuPerIterMs = cpuRuns.back().outerIterations > 0
                                    ? 1000.0 * cpuStats.median /
                                          static_cast<double>(cpuRuns.back().outerIterations)
                                    : 0.0;
    const double gpuPerIterMs = gpuRuns.back().outerIterations > 0
                                    ? 1000.0 * gpuStats.median /
                                          static_cast<double>(gpuRuns.back().outerIterations)
                                    : 0.0;

    const double gpuTransferSeconds = gpuRuns.back().uploadSeconds + gpuRuns.back().downloadSeconds;
    const double gpuTransferPct =
        gpuStats.median > 0.0 ? 100.0 * gpuTransferSeconds / gpuRuns.back().simpleSolveSeconds : 0.0;

    std::cout << gridLabel << " | " << cells << " | CPU | " << cpuRuns.back().outerIterations
              << " | " << cpuStats.min << " | " << cpuStats.median << " | " << cpuStats.mean
              << " | " << cpuStats.stddev << " | " << cpuPerIterMs << " | n/a | "
              << (allCpuClean ? "yes" : "NO") << "\n";
    std::cout << gridLabel << " | " << cells << " | GPU | " << gpuRuns.back().outerIterations
              << " | " << gpuStats.min << " | " << gpuStats.median << " | " << gpuStats.mean
              << " | " << gpuStats.stddev << " | " << gpuPerIterMs << " | " << gpuTransferPct
              << " | " << (allGpuClean ? "yes" : "NO") << "\n";

    const double speedup = gpuStats.median > 0.0 ? cpuStats.median / gpuStats.median : 0.0;
    const Equivalence eq = compareResults(cpuRuns.front(), gpuRuns.front());

    std::ostringstream lineStream;
    lineStream << gridLabel << " | " << cells << " | " << cpuStats.median << "s | "
              << gpuStats.median << "s | " << speedup << "x | " << gpuTransferPct << "% | "
              << ((allCpuClean && allGpuClean) ? "yes" : "NO");
    gridSummaryLines.push_back(lineStream.str());

    nlohmann::json j;
    j["grid"] = gridLabel;
    j["nx"] = static_cast<int>(cfg.nx);
    j["ny"] = static_cast<int>(cfg.ny);
    j["cells"] = static_cast<int>(cells);
    j["outer_iterations"] = static_cast<int>(cpuRuns.back().outerIterations);
    j["repeats"] = cfg.repeats;
    j["cpu"] = {{"min_seconds", cpuStats.min},        {"median_seconds", cpuStats.median},
               {"mean_seconds", cpuStats.mean},       {"stddev_seconds", cpuStats.stddev},
               {"per_iteration_ms", cpuPerIterMs},    {"ran_cleanly", allCpuClean},
               {"momentum_assembly_seconds", cpuBreakdown.momentumAssemblySeconds},
               {"pressure_assembly_seconds", cpuBreakdown.pressureAssemblySeconds},
               {"momentum_solve_seconds", cpuBreakdown.momentumSolveSeconds},
               {"pressure_solve_seconds", cpuBreakdown.pressureSolveSeconds}};
    j["gpu"] = {{"min_seconds", gpuStats.min},
               {"median_seconds", gpuStats.median},
               {"mean_seconds", gpuStats.mean},
               {"stddev_seconds", gpuStats.stddev},
               {"per_iteration_ms", gpuPerIterMs},
               {"ran_cleanly", allGpuClean},
               {"momentum_assembly_seconds", gpuBreakdown.momentumAssemblySeconds},
               {"pressure_assembly_seconds", gpuBreakdown.pressureAssemblySeconds},
               {"momentum_solve_seconds", gpuBreakdown.momentumSolveSeconds},
               {"pressure_solve_seconds", gpuBreakdown.pressureSolveSeconds},
               {"transfer_seconds", gpuTransferSeconds},
               {"transfer_pct_of_simple_solve", gpuTransferPct},
               {"host_to_device_bytes", gpuRuns.back().hostToDeviceBytes},
               {"device_to_host_bytes", gpuRuns.back().deviceToHostBytes},
               {"host_to_device_calls", gpuRuns.back().hostToDeviceCalls},
               {"device_to_host_calls", gpuRuns.back().deviceToHostCalls},
               {"kernel_seconds", gpuRuns.back().kernelSeconds},
               {"gpu_solve_seconds", gpuRuns.back().gpuSolveSeconds},
               {"gpu_linear_solves", gpuRuns.back().gpuLinearSolves},
               {"gpu_linear_solver_iterations", gpuRuns.back().gpuLinearSolverIterations}};
    j["speedup_cpu_over_gpu"] = speedup;
    j["equivalence"] = {{"max_abs_velocity_error", eq.maxAbsVelocityError},
                        {"max_rel_velocity_error", eq.maxRelVelocityError},
                        {"max_abs_pressure_error", eq.maxAbsPressureError},
                        {"max_rel_pressure_error", eq.maxRelPressureError}};
    j["cpu_mass_imbalance"] = std::abs(cpuRuns.front().globalMassImbalance);
    j["gpu_mass_imbalance"] = std::abs(gpuRuns.front().globalMassImbalance);
    summaryJson.push_back(j);
  }

  std::ofstream summaryCsv(outDir + "/summary.csv");
  summaryCsv << "grid,cells,outer_iterations,cpu_min_s,cpu_median_s,cpu_mean_s,cpu_stddev_s,"
                "gpu_min_s,gpu_median_s,gpu_mean_s,gpu_stddev_s,speedup,transfer_pct,"
                "max_abs_velocity_error,max_abs_pressure_error,converged\n";
  for (const auto& j : summaryJson) {
    summaryCsv << j["grid"].get<std::string>() << "," << j["cells"].get<int>() << ","
              << j["outer_iterations"].get<int>() << "," << j["cpu"]["min_seconds"].get<double>()
              << "," << j["cpu"]["median_seconds"].get<double>() << ","
              << j["cpu"]["mean_seconds"].get<double>() << ","
              << j["cpu"]["stddev_seconds"].get<double>() << ","
              << j["gpu"]["min_seconds"].get<double>() << ","
              << j["gpu"]["median_seconds"].get<double>() << ","
              << j["gpu"]["mean_seconds"].get<double>() << ","
              << j["gpu"]["stddev_seconds"].get<double>() << ","
              << j["speedup_cpu_over_gpu"].get<double>() << ","
              << j["gpu"]["transfer_pct_of_simple_solve"].get<double>() << ","
              << j["equivalence"]["max_abs_velocity_error"].get<double>() << ","
              << j["equivalence"]["max_abs_pressure_error"].get<double>() << ","
              << ((j["cpu"]["ran_cleanly"].get<bool>() && j["gpu"]["ran_cleanly"].get<bool>())
                      ? "yes"
                      : "no")
              << "\n";
  }

  // Break-even: smallest tested grid where GPU median beats CPU median.
  std::string breakEven = "No GPU break-even observed up to the largest tested grid.";
  double maxSpeedup = 0.0;
  std::string maxSpeedupGrid;
  for (const auto& j : summaryJson) {
    const double speedup = j["speedup_cpu_over_gpu"].get<double>();
    if (speedup > maxSpeedup) {
      maxSpeedup = speedup;
      maxSpeedupGrid = j["grid"].get<std::string>();
    }
    if (speedup > 1.0 && breakEven.rfind("No GPU", 0) == 0) {
      breakEven = j["grid"].get<std::string>();
    }
  }

  nlohmann::json metadata;
  metadata["cfdapp_version"] = cfd::core::versionString();
#if defined(__VERSION__)
  metadata["compiler"] = __VERSION__;
#endif
#ifdef NDEBUG
  metadata["build_type"] = "Release (NDEBUG)";
#else
  metadata["build_type"] = "Debug/other (NDEBUG not defined)";
#endif
  // CFDAPP_ENABLE_CUDA is a CMake cache variable, never propagated as a
  // preprocessor define to any target (checked: no target_compile_
  // definitions anywhere in this project's CMake sets it) -- an #ifdef on
  // it here would always take the #else branch regardless of the actual
  // build configuration, which is exactly the kind of silently-wrong
  // metadata this task's own section 6 exists to prevent. The only
  // reliable *runtime* signal for "can this binary use a GPU at all" is
  // cfd::gpu::cudaAvailable() (already captured as `gpuAvailable` above,
  // and this function already returned early if it were false) --
  // reported here under its own name, not a mislabeled "at build" field.
  metadata["cuda_available_at_runtime"] = gpuAvailable;
  metadata["cold_gpu_first_use_seconds"] = coldGpuSeconds;
  metadata["gpu_break_even_grid"] = breakEven;
  metadata["max_measured_speedup"] = maxSpeedup;
  metadata["max_measured_speedup_grid"] = maxSpeedupGrid;
  metadata["hardware_note"] =
      "See results/performance/cuda_end_to_end/environment_hardware.txt (nproc/free/nvidia-smi "
      "captured alongside this run) for CPU/RAM/GPU details.";
  std::ofstream metaOut(outDir + "/metadata.json");
  metaOut << metadata.dump(2);

  std::cout << "\n=== Summary ===\n";
  std::cout << "Grid | Cells | CPU Median | GPU Median | Speedup | Transfer % | Converged\n";
  for (const auto& line : gridSummaryLines) std::cout << line << "\n";
  std::cout << "\ncold_gpu_first_use_seconds: " << coldGpuSeconds << "\n";
  std::cout << "gpu_break_even_grid: " << breakEven << "\n";
  std::cout << "max_measured_speedup: " << maxSpeedup << "x at " << maxSpeedupGrid << "\n";

  return 0;
}
