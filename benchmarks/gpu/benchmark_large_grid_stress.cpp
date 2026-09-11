// P7-PERF-003 -- Performance: large-grid stress testing. Progressively
// larger production lid-driven-cavity SIMPLE solves, CPU and GPU, looking
// for where memory, runtime, or solver stability actually break down --
// not chasing a headline number. Same case/solver-settings convention as
// P7-PERF-001/P7-PERF-002 (BiCGSTAB, no preconditioner, same tolerances)
// so results stay comparable with those two tasks' own baselines.
//
// Outer-iteration budget tapers as grid size grows (60 down to 3) purely
// to keep this tool's own wall-clock time bounded -- the same convention
// P7-PERF-001's cfd_benchmark_cuda_end_to_end already established for
// exactly this reason. This is a deliberate methodology choice, not an
// attempt to hide instability: convergence *trends* (residual history,
// iteration counts, NaN/Inf, mass imbalance) are recorded at every grid
// regardless of whether the fixed budget was enough to fully converge,
// and a solver failure (e.g. a linear solve that does not converge
// within its own iteration cap) is recorded as exactly that, not masked
// by a smaller outer-iteration count.
//
// Usage: run with no arguments; writes results/performance/
// large_grid_stress/{runs.csv,summary.csv,metadata.json} plus one
// convergence_<backend>_<grid>.csv per attempted grid/backend. Long-
// running by design (multiple large grids x 2 backends) -- not part of
// the routine CTest suite.
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
using cfd::physics::FluidProperties;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// ---------------------------------------------------------------------
// Memory instrumentation -- host RSS/high-water-mark from /proc/self/
// status (Linux/WSL2; this tool is only ever built+run in this project's
// own CI/dev environment, which is always Linux), GPU free/total via
// cudaMemGetInfo. Neither exists anywhere else in this codebase -- added
// here only, not in production code (this task's own "do not create
// redundant profiling systems" applies to *reusing* existing
// instrumentation like GPUExecutionStats, not to adding the one new
// measurement -- host process memory -- nothing here already provides).
struct HostMemoryStats {
  long vmRssKb{};
  long vmHwmKb{};  // "high water mark" -- peak RSS ever reached by this process.
};

HostMemoryStats readHostMemory() {
  HostMemoryStats stats;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      std::istringstream iss(line.substr(6));
      iss >> stats.vmRssKb;
    } else if (line.rfind("VmHWM:", 0) == 0) {
      std::istringstream iss(line.substr(6));
      iss >> stats.vmHwmKb;
    }
  }
  return stats;
}

struct GpuMemoryStats {
  std::size_t freeBytes{};
  std::size_t totalBytes{};
};

GpuMemoryStats readGpuMemory() {
  GpuMemoryStats stats;
  cudaMemGetInfo(&stats.freeBytes, &stats.totalBytes);
  return stats;
}

// ---------------------------------------------------------------------
// Case setup (same cavity as every other P7 benchmark this session)
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

SIMPLESettings makeCavitySettings(Index outerIterations, LinearSolverBackend backend) {
  SIMPLESettings settings;
  settings.maxIterations = outerIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-10;
  settings.pressureTolerance = 1e-10;
  settings.continuityTolerance = 1e-10;

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

struct GridConfig {
  Index nx, ny;
  Index outerIterations;
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

struct StressRunResult {
  double totalSeconds{};
  Index outerIterations{};
  SIMPLEStatus status{};
  bool ranCleanly{};
  Real finalUResidual{}, finalVResidual{}, finalPressureResidual{}, finalContinuityResidual{};
  Real globalMassImbalance{};
  bool velocityFinite{true};
  bool pressureFinite{true};
  std::uint64_t nanCount{};
  std::uint64_t infCount{};
  long hostRssBeforeKb{}, hostRssAfterKb{}, hostHwmAfterKb{};
  std::size_t gpuFreeBeforeBytes{}, gpuFreeAfterBytes{}, gpuTotalBytes{};
  std::uint64_t gpuHostToDeviceBytes{}, gpuDeviceToHostBytes{};
  std::vector<Real> uHistory, vHistory, pHistory, continuityHistory;
};

StressRunResult runStress(Index nx, Index ny, Index outerIterations, LinearSolverBackend backend) {
  StressRunResult run;

  const auto hostBefore = readHostMemory();
  run.hostRssBeforeKb = hostBefore.vmRssKb;
  GpuMemoryStats gpuBefore;
  if (backend == LinearSolverBackend::GPU) {
    gpuBefore = readGpuMemory();
    run.gpuFreeBeforeBytes = gpuBefore.freeBytes;
    run.gpuTotalBytes = gpuBefore.totalBytes;
    cfd::gpu::resetGpuExecutionStats();
  }

  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, 1.0);
  const auto pressureBoundaries = makeCavityPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Index n = mesh.numberOfCells();

  VectorField velocity(n, Vector2{0.0, 0.0});
  ScalarField pressure(n, 0.0);

  cfd::Timer timer;
  const SIMPLE simple(makeCavitySettings(outerIterations, backend));
  const SIMPLEResult result =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
  run.totalSeconds = timer.elapsedSeconds();

  run.outerIterations = result.iterations;
  run.status = result.status;
  run.ranCleanly =
      (result.status == SIMPLEStatus::Converged || result.status == SIMPLEStatus::MaxIterations);
  run.finalUResidual = result.finalUResidual;
  run.finalVResidual = result.finalVResidual;
  run.finalPressureResidual = result.finalPressureResidual;
  run.finalContinuityResidual = result.finalContinuityResidual;
  run.globalMassImbalance = result.globalMassImbalance;
  run.uHistory = result.uResidualHistory;
  run.vHistory = result.vResidualHistory;
  run.pHistory = result.pressureResidualHistory;
  run.continuityHistory = result.continuityHistory;

  for (Index i = 0; i < result.velocity.size(); ++i) {
    if (!std::isfinite(result.velocity[i].x)) {
      run.velocityFinite = false;
      if (std::isnan(result.velocity[i].x)) ++run.nanCount; else ++run.infCount;
    }
    if (!std::isfinite(result.velocity[i].y)) {
      run.velocityFinite = false;
      if (std::isnan(result.velocity[i].y)) ++run.nanCount; else ++run.infCount;
    }
  }
  for (Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) {
      run.pressureFinite = false;
      if (std::isnan(result.pressure[i])) ++run.nanCount; else ++run.infCount;
    }
  }

  const auto hostAfter = readHostMemory();
  run.hostRssAfterKb = hostAfter.vmRssKb;
  run.hostHwmAfterKb = hostAfter.vmHwmKb;
  if (backend == LinearSolverBackend::GPU) {
    const auto gpuAfter = readGpuMemory();
    run.gpuFreeAfterBytes = gpuAfter.freeBytes;
    const auto& stats = cfd::gpu::gpuExecutionStats();
    run.gpuHostToDeviceBytes = stats.hostToDeviceBytes;
    run.gpuDeviceToHostBytes = stats.deviceToHostBytes;
  }

  return run;
}

void writeConvergenceHistory(const std::string& path, const StressRunResult& run) {
  std::ofstream out(path);
  out << "iteration,u_residual,v_residual,pressure_residual,continuity\n";
  const std::size_t n = run.uHistory.size();
  for (std::size_t i = 0; i < n; ++i) {
    out << i << "," << run.uHistory[i] << "," << run.vHistory[i] << ","
        << (i < run.pHistory.size() ? run.pHistory[i] : 0.0) << ","
        << (i < run.continuityHistory.size() ? run.continuityHistory[i] : 0.0) << "\n";
  }
}

}  // namespace

int main() {
  const std::string outDir = "results/performance/large_grid_stress";
  std::filesystem::create_directories(outDir);

  const bool gpuAvailable = cfd::gpu::cudaAvailable();
  std::cout << "cfd_benchmark_large_grid_stress -- CFDApp version " << cfd::core::versionString()
            << "\n";
  std::cout << "cuda_available_at_runtime: " << (gpuAvailable ? "yes" : "no") << "\n\n";

  // Section 2: progressively larger grids. Outer-iteration budget tapers
  // (same convention as P7-PERF-001's own cfd_benchmark_cuda_end_to_end)
  // purely to keep this tool's own wall-clock time bounded -- see this
  // file's own header comment.
  const std::vector<GridConfig> gridConfigs = {
      {160, 160, 60}, {320, 320, 20}, {480, 480, 10},
      {640, 640, 8},  {800, 800, 5},  {1024, 1024, 3},
  };

  std::ofstream runsCsv(outDir + "/runs.csv");
  runsCsv << "grid,cells,backend,total_seconds,outer_iterations,status,ran_cleanly,"
             "final_u_residual,final_v_residual,final_pressure_residual,"
             "final_continuity_residual,mass_imbalance,nan_count,inf_count,"
             "host_rss_before_kb,host_rss_after_kb,host_hwm_after_kb,"
             "gpu_free_before_bytes,gpu_free_after_bytes,gpu_total_bytes,"
             "gpu_h2d_bytes,gpu_d2h_bytes\n";

  nlohmann::json summaryJson = nlohmann::json::array();

  std::cout << "grid | cells | backend | runtime_s | outer_iters | status | host_hwm_mb | "
              "gpu_used_mb | mass_imbalance | nan/inf | result\n";

  std::string largestStableCpuGrid = "none";
  std::string largestStableGpuGrid = "none";
  std::string cpuLimitingFactor = "not reached within tested range";
  std::string gpuLimitingFactor = "not reached within tested range";
  bool cpuStillStable = true;
  bool gpuStillStable = true;

  for (const auto& cfg : gridConfigs) {
    const std::string gridLabel = std::to_string(cfg.nx) + "x" + std::to_string(cfg.ny);
    const Index cells = cfg.nx * cfg.ny;

    std::vector<std::pair<LinearSolverBackend, const char*>> backends = {
        {LinearSolverBackend::CPU, "CPU"}};
    if (gpuAvailable) backends.push_back({LinearSolverBackend::GPU, "GPU"});

    for (const auto& [backend, backendName] : backends) {
      const bool isCpu = (backend == LinearSolverBackend::CPU);
      if (isCpu && !cpuStillStable) {
        std::cout << gridLabel << " | " << cells << " | CPU | -- | -- | SKIPPED | -- | -- | -- | "
                    "-- | skipped (prior grid already unstable/impractical)\n";
        continue;
      }
      if (!isCpu && !gpuStillStable) {
        std::cout << gridLabel << " | " << cells << " | GPU | -- | -- | SKIPPED | -- | -- | -- | "
                    "-- | skipped (prior grid already unstable/impractical)\n";
        continue;
      }

      const auto run = runStress(cfg.nx, cfg.ny, cfg.outerIterations, backend);

      runsCsv << gridLabel << "," << cells << "," << backendName << "," << run.totalSeconds << ","
              << run.outerIterations << "," << statusName(run.status) << ","
              << (run.ranCleanly ? "yes" : "no") << "," << run.finalUResidual << ","
              << run.finalVResidual << "," << run.finalPressureResidual << ","
              << run.finalContinuityResidual << "," << std::abs(run.globalMassImbalance) << ","
              << run.nanCount << "," << run.infCount << "," << run.hostRssBeforeKb << ","
              << run.hostRssAfterKb << "," << run.hostHwmAfterKb << "," << run.gpuFreeBeforeBytes
              << "," << run.gpuFreeAfterBytes << "," << run.gpuTotalBytes << ","
              << run.gpuHostToDeviceBytes << "," << run.gpuDeviceToHostBytes << "\n";

      writeConvergenceHistory(outDir + "/convergence_" + std::string(backendName) + "_" +
                                  gridLabel + ".csv",
                              run);

      const bool numericallyValid = run.velocityFinite && run.pressureFinite;
      const bool overallOk = run.ranCleanly && numericallyValid;
      const double gpuUsedMb = isCpu ? 0.0
                                     : static_cast<double>(run.gpuTotalBytes - run.gpuFreeAfterBytes) /
                                           (1024.0 * 1024.0);

      std::cout << gridLabel << " | " << cells << " | " << backendName << " | "
                << run.totalSeconds << " | " << run.outerIterations << " | "
                << statusName(run.status) << " | "
                << (static_cast<double>(run.hostHwmAfterKb) / 1024.0) << " | "
                << (isCpu ? 0.0 : gpuUsedMb) << " | " << std::abs(run.globalMassImbalance) << " | "
                << run.nanCount << "/" << run.infCount << " | " << (overallOk ? "OK" : "FAILED")
                << "\n";

      if (overallOk) {
        if (isCpu) largestStableCpuGrid = gridLabel; else largestStableGpuGrid = gridLabel;
      } else {
        if (isCpu) {
          cpuStillStable = false;
          cpuLimitingFactor = "solver instability at " + gridLabel + " (" +
                              statusName(run.status) + (numericallyValid ? "" : ", NaN/Inf") + ")";
        } else {
          gpuStillStable = false;
          gpuLimitingFactor = "solver instability at " + gridLabel + " (" +
                              statusName(run.status) + (numericallyValid ? "" : ", NaN/Inf") + ")";
        }
      }

      nlohmann::json j;
      j["grid"] = gridLabel;
      j["cells"] = static_cast<int>(cells);
      j["backend"] = backendName;
      j["total_seconds"] = run.totalSeconds;
      j["outer_iterations"] = static_cast<int>(run.outerIterations);
      j["status"] = statusName(run.status);
      j["ran_cleanly"] = run.ranCleanly;
      j["numerically_valid"] = numericallyValid;
      j["overall_ok"] = overallOk;
      j["final_u_residual"] = run.finalUResidual;
      j["final_v_residual"] = run.finalVResidual;
      j["final_pressure_residual"] = run.finalPressureResidual;
      j["final_continuity_residual"] = run.finalContinuityResidual;
      j["mass_imbalance"] = std::abs(run.globalMassImbalance);
      j["nan_count"] = run.nanCount;
      j["inf_count"] = run.infCount;
      j["host_rss_before_kb"] = run.hostRssBeforeKb;
      j["host_rss_after_kb"] = run.hostRssAfterKb;
      j["host_hwm_after_kb"] = run.hostHwmAfterKb;
      if (!isCpu) {
        j["gpu_free_before_bytes"] = run.gpuFreeBeforeBytes;
        j["gpu_free_after_bytes"] = run.gpuFreeAfterBytes;
        j["gpu_total_bytes"] = run.gpuTotalBytes;
        j["gpu_used_after_mb"] = gpuUsedMb;
        j["gpu_h2d_bytes"] = run.gpuHostToDeviceBytes;
        j["gpu_d2h_bytes"] = run.gpuDeviceToHostBytes;
      }
      summaryJson.push_back(j);
    }
  }

  std::ofstream summaryCsv(outDir + "/summary.csv");
  summaryCsv << "grid,cells,backend,total_seconds,outer_iterations,status,overall_ok,"
                "host_hwm_after_mb,gpu_used_after_mb,mass_imbalance,nan_count,inf_count\n";
  for (const auto& j : summaryJson) {
    summaryCsv << j["grid"].get<std::string>() << "," << j["cells"].get<int>() << ","
              << j["backend"].get<std::string>() << "," << j["total_seconds"].get<double>() << ","
              << j["outer_iterations"].get<int>() << "," << j["status"].get<std::string>() << ","
              << (j["overall_ok"].get<bool>() ? "yes" : "no") << ","
              << (static_cast<double>(j["host_hwm_after_kb"].get<long>()) / 1024.0) << ","
              << (j.contains("gpu_used_after_mb") ? j["gpu_used_after_mb"].get<double>() : 0.0)
              << "," << j["mass_imbalance"].get<double>() << "," << j["nan_count"].get<int>()
              << "," << j["inf_count"].get<int>() << "\n";
  }

  nlohmann::json metadata;
  metadata["cfdapp_version"] = cfd::core::versionString();
#ifdef NDEBUG
  metadata["build_type"] = "Release (NDEBUG)";
#else
  metadata["build_type"] = "Debug/other (NDEBUG not defined)";
#endif
  metadata["cuda_available_at_runtime"] = gpuAvailable;
  metadata["largest_stable_cpu_grid"] = largestStableCpuGrid;
  metadata["largest_stable_gpu_grid"] = largestStableGpuGrid;
  metadata["cpu_limiting_factor"] = cpuLimitingFactor;
  metadata["gpu_limiting_factor"] = gpuLimitingFactor;
  metadata["hardware_note"] =
      "See results/performance/cuda_end_to_end/environment_hardware.txt (same machine) for "
      "CPU/RAM/GPU details.";
  std::ofstream metaOut(outDir + "/metadata.json");
  metaOut << metadata.dump(2);

  std::cout << "\nlargest_stable_cpu_grid: " << largestStableCpuGrid << " (" << cpuLimitingFactor
            << ")\n";
  std::cout << "largest_stable_gpu_grid: " << largestStableGpuGrid << " (" << gpuLimitingFactor
            << ")\n";

  return 0;
}
