// P6-GPU-003 -- Performance: the baseline + before/after benchmark tool
// for GPU-resident Jacobi preconditioning (cuda/kernels/
// GpuPreconditionerKernel.cu, cuda/kernels/GpuLinearSolverCuda.cpp).
// Two parts:
//
//   1. Linear-system-level: CG/BiCGSTAB x CPU/GPU x None/Jacobi on a
//      handful of representative systems (a small controlled SPD system,
//      a well-conditioned pressure-correction-like grid system, a
//      deliberately poorly-conditioned one, and momentum-like
//      nonsymmetric systems), reporting iterations, wall-clock linear-
//      solve time, and residuals -- raw numbers, not just a speedup
//      ratio (this task's own section 12 requirement).
//   2. Production-level: a full lid-driven-cavity SIMPLE::solve() at two
//      grid sizes, GPU backend, None vs Jacobi for both the momentum and
//      pressure solvers, reusing the exact production assembly/solve
//      path (never a synthetic proxy -- same principle
//      benchmarks/cpu/benchmark_runner.cpp's own header comment states).
//
// Deliberately does NOT assume "fewer iterations == faster" -- every
// configuration's own wall-clock linear-solve time (and, for the GPU
// configurations, GPUExecutionStats::preconditionerSetupSeconds/
// preconditionerApplySeconds) is measured and reported alongside the
// iteration count, so a reduction in iterations that costs more per
// iteration is visible, not hidden.
//
// Usage: run with no arguments; writes results/performance/preconditioner/
// linear_systems.json and simple_<N>x<N>.json, prints summary tables to
// stdout. Not part of the routine CTest suite (same "no fragile
// wall-clock limits in unit tests" convention as benchmark_runner.cpp).
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
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
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::LinearSystem;
using cfd::algebra::makeLinearSolver;
using cfd::algebra::PreconditionerType;
using cfd::algebra::SolverResult;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// ---------------------------------------------------------------------
// Representative matrices
// ---------------------------------------------------------------------

SparseMatrix makeSpdGridMatrix(Index nx, Index ny, Real diagonalBoost) {
  const Index n = nx * ny;
  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  const auto index = [&](Index i, Index j) { return j * nx + i; };
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p = index(i, j);
      Real diagonal = 0.0;
      if (i > 0) {
        builder.add(p, index(i - 1, j), -1.0);
        diagonal += 1.0;
      }
      if (i + 1 < nx) {
        builder.add(p, index(i + 1, j), -1.0);
        diagonal += 1.0;
      }
      if (j > 0) {
        builder.add(p, index(i, j - 1), -1.0);
        diagonal += 1.0;
      }
      if (j + 1 < ny) {
        builder.add(p, index(i, j + 1), -1.0);
        diagonal += 1.0;
      }
      builder.add(p, p, diagonal + diagonalBoost);
    }
  }
  return builder.build();
}

// A momentum-equation-like nonsymmetric system: the same 5-point grid
// Laplacian, plus a mild directional (upwind-style) skew on the
// horizontal off-diagonals -- representative of a convection-diffusion
// discretization BiCGSTAB (not CG) must handle.
SparseMatrix makeConvectionDiffusionMatrix(Index nx, Index ny, Real diagonalBoost) {
  const Index n = nx * ny;
  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  const auto index = [&](Index i, Index j) { return j * nx + i; };
  constexpr Real convection = 0.4;
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p = index(i, j);
      Real diagonal = 0.0;
      if (i > 0) {
        builder.add(p, index(i - 1, j), -1.0 - convection);
        diagonal += 1.0;
      }
      if (i + 1 < nx) {
        builder.add(p, index(i + 1, j), -1.0 + convection);
        diagonal += 1.0;
      }
      if (j > 0) {
        builder.add(p, index(i, j - 1), -1.0);
        diagonal += 1.0;
      }
      if (j + 1 < ny) {
        builder.add(p, index(i, j + 1), -1.0);
        diagonal += 1.0;
      }
      builder.add(p, p, diagonal + diagonalBoost);
    }
  }
  return builder.build();
}

SparseMatrix makeSmallSpdMatrix() {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, -1.0);
  builder.add(1, 0, -1.0);
  builder.add(1, 1, 4.0);
  builder.add(1, 2, -1.0);
  builder.add(2, 1, -1.0);
  builder.add(2, 2, 3.0);
  return builder.build();
}

Vector makeVector(Index n, Real seed) {
  Vector v(n);
  for (Index i = 0; i < n; ++i) v[i] = std::sin(seed * static_cast<Real>(i + 1));
  return v;
}

// ---------------------------------------------------------------------
// Linear-system-level benchmark
// ---------------------------------------------------------------------

struct SolveRecord {
  std::string caseName;
  std::string solverType;
  std::string backend;
  std::string preconditionerName;
  Index n{};
  Index nnz{};
  Index iterations{};
  Real initialResidual{};
  Real finalResidual{};
  bool converged{};
  double solveSecondsMedian{};
  double preconditionerSetupSeconds{};
  double preconditionerApplySeconds{};
};

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const std::size_t mid = values.size() / 2;
  if (values.size() % 2 == 0) return 0.5 * (values[mid - 1] + values[mid]);
  return values[mid];
}

SolveRecord runOneConfiguration(const std::string& caseName, const SparseMatrix& matrix,
                                const Vector& rhs, LinearSolverType type, LinearSolverBackend backend,
                                PreconditionerType preconditioner, int repeats) {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-10;
  settings.relativeTolerance = 1e-8;
  settings.maxIterations = 5000;
  settings.type = type;
  settings.backend = backend;
  settings.preconditioner = preconditioner;

  const LinearSystem system(matrix, rhs);

  SolverResult result;
  std::vector<double> seconds;
  double setupSeconds = 0.0;
  double applySeconds = 0.0;
  for (int r = 0; r < repeats; ++r) {
    const auto solver = makeLinearSolver(settings);
    cfd::gpu::resetGpuExecutionStats();
    cfd::Timer timer;
    result = solver->solve(system);
    seconds.push_back(timer.elapsedSeconds());
    const auto& stats = cfd::gpu::gpuExecutionStats();
    setupSeconds = stats.preconditionerSetupSeconds;
    applySeconds = stats.preconditionerApplySeconds;
  }

  SolveRecord record;
  record.caseName = caseName;
  record.solverType = (type == LinearSolverType::CG) ? "CG" : "BiCGSTAB";
  record.backend = (result.backendUsed == LinearSolverBackend::GPU) ? "GPU" : "CPU";
  record.preconditionerName = (preconditioner == PreconditionerType::Jacobi) ? "Jacobi" : "None";
  record.n = matrix.rows();
  record.nnz = matrix.nonZeros();
  record.iterations = result.iterations;
  record.initialResidual = result.initialResidual;
  record.finalResidual = result.finalResidual;
  record.converged = result.converged();
  record.solveSecondsMedian = median(seconds);
  record.preconditionerSetupSeconds = setupSeconds;
  record.preconditionerApplySeconds = applySeconds;
  return record;
}

void printRecord(const SolveRecord& r) {
  std::cout << r.caseName << " | " << r.solverType << " | " << r.backend << " | "
            << r.preconditionerName << " | n=" << r.n << " nnz=" << r.nnz
            << " | iters=" << r.iterations << " | t0=" << r.initialResidual
            << " | tFinal=" << r.finalResidual << " | solve_s=" << r.solveSecondsMedian
            << " | precond_setup_s=" << r.preconditionerSetupSeconds
            << " | precond_apply_s=" << r.preconditionerApplySeconds
            << " | " << (r.converged ? "CONVERGED" : "FAILED") << "\n";
}

nlohmann::json toJson(const SolveRecord& r) {
  nlohmann::json j;
  j["case"] = r.caseName;
  j["solver"] = r.solverType;
  j["backend"] = r.backend;
  j["preconditioner"] = r.preconditionerName;
  j["n"] = static_cast<int>(r.n);
  j["nnz"] = static_cast<int>(r.nnz);
  j["iterations"] = static_cast<int>(r.iterations);
  j["initial_residual"] = r.initialResidual;
  j["final_residual"] = r.finalResidual;
  j["converged"] = r.converged;
  j["solve_seconds_median"] = r.solveSecondsMedian;
  j["preconditioner_setup_seconds"] = r.preconditionerSetupSeconds;
  j["preconditioner_apply_seconds"] = r.preconditionerApplySeconds;
  return j;
}

void runLinearSystemBenchmarks(const std::string& outDir) {
  constexpr int kRepeats = 5;
  std::vector<SolveRecord> records;

  const bool gpuAvailable = cfd::gpu::cudaAvailable();
  std::vector<LinearSolverBackend> backends = {LinearSolverBackend::CPU};
  if (gpuAvailable) backends.push_back(LinearSolverBackend::GPU);

  struct SpdCase {
    std::string name;
    SparseMatrix matrix;
  };
  std::vector<SpdCase> spdCases;
  spdCases.push_back({"small_spd_3x3", makeSmallSpdMatrix()});
  spdCases.push_back(
      {"pressure_like_well_conditioned_40x40", makeSpdGridMatrix(40, 40, /*diagonalBoost=*/0.1)});
  spdCases.push_back({"pressure_like_poorly_conditioned_40x40",
                      makeSpdGridMatrix(40, 40, /*diagonalBoost=*/0.001)});
  spdCases.push_back(
      {"pressure_like_poorly_conditioned_80x80", makeSpdGridMatrix(80, 80, /*diagonalBoost=*/0.001)});

  for (const auto& spdCase : spdCases) {
    const Vector x = makeVector(spdCase.matrix.columns(), 0.37);
    const Vector rhs = spdCase.matrix.multiply(x);
    for (auto backend : backends) {
      for (auto preconditioner : {PreconditionerType::None, PreconditionerType::Jacobi}) {
        const auto record = runOneConfiguration(spdCase.name, spdCase.matrix, rhs,
                                                 LinearSolverType::CG, backend, preconditioner,
                                                 kRepeats);
        printRecord(record);
        records.push_back(record);
      }
    }
  }

  std::vector<SpdCase> nonsymmetricCases;
  nonsymmetricCases.push_back(
      {"momentum_like_40x40", makeConvectionDiffusionMatrix(40, 40, /*diagonalBoost=*/0.1)});
  nonsymmetricCases.push_back({"momentum_like_poorly_conditioned_40x40",
                               makeConvectionDiffusionMatrix(40, 40, /*diagonalBoost=*/0.001)});

  for (const auto& nsCase : nonsymmetricCases) {
    const Vector x = makeVector(nsCase.matrix.columns(), 0.61);
    const Vector rhs = nsCase.matrix.multiply(x);
    for (auto backend : backends) {
      for (auto preconditioner : {PreconditionerType::None, PreconditionerType::Jacobi}) {
        const auto record = runOneConfiguration(nsCase.name, nsCase.matrix, rhs,
                                                 LinearSolverType::BiCGSTAB, backend, preconditioner,
                                                 kRepeats);
        printRecord(record);
        records.push_back(record);
      }
    }
  }

  nlohmann::json j;
  j["cfdapp_version"] = cfd::core::versionString();
  j["gpu_available"] = gpuAvailable;
  j["repeats"] = kRepeats;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& r : records) arr.push_back(toJson(r));
  j["records"] = arr;

  std::filesystem::create_directories(outDir);
  std::ofstream out(outDir + "/linear_systems.json");
  out << j.dump(2);
}

// ---------------------------------------------------------------------
// Production SIMPLE benchmark
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

SIMPLESettings makeCavitySettings(Index maxIterations, PreconditionerType preconditioner) {
  SIMPLESettings settings;
  settings.maxIterations = maxIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-10;
  settings.pressureTolerance = 1e-10;
  settings.continuityTolerance = 1e-10;

  settings.momentumSolver.type = LinearSolverType::BiCGSTAB;
  settings.momentumSolver.backend = LinearSolverBackend::GPU;
  settings.momentumSolver.maxIterations = 1000;
  settings.momentumSolver.absoluteTolerance = 1e-8;
  settings.momentumSolver.relativeTolerance = 1e-6;
  settings.momentumSolver.preconditioner = preconditioner;

  settings.pressureSolver.type = LinearSolverType::BiCGSTAB;
  settings.pressureSolver.backend = LinearSolverBackend::GPU;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-7;
  settings.pressureSolver.relativeTolerance = 1e-5;
  settings.pressureSolver.preconditioner = preconditioner;

  return settings;
}

void runSimpleBenchmark(Index nx, Index ny, const std::string& outDir) {
  constexpr Index kFixedOuterIterations = 200;

  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, 1.0);
  const auto pressureBoundaries = makeCavityPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Index n = mesh.numberOfCells();

  nlohmann::json j;
  j["grid"] = {{"nx", nx}, {"ny", ny}, {"cells", static_cast<int>(n)}};
  nlohmann::json configs = nlohmann::json::array();

  std::cout << "\nSIMPLE lid-driven cavity " << nx << "x" << ny
            << " -- preconditioner | total_s | per_iter_ms | outer_iters | status | "
              "gpu_linear_solves | gpu_linear_iters | gpu_solve_s | precond_setup_s | "
              "precond_apply_s | mass_imbalance\n";

  for (auto preconditioner : {PreconditionerType::None, PreconditionerType::Jacobi}) {
    cfd::fields::VectorField velocity(n, Vector2{0.0, 0.0});
    cfd::fields::ScalarField pressure(n, 0.0);

    cfd::gpu::resetGpuExecutionStats();
    const SIMPLE simple(makeCavitySettings(kFixedOuterIterations, preconditioner));
    cfd::Timer timer;
    const SIMPLEResult result =
        simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
    const double totalSeconds = timer.elapsedSeconds();
    const auto& stats = cfd::gpu::gpuExecutionStats();

    const double perIterationMs =
        result.iterations > 0 ? 1000.0 * totalSeconds / static_cast<double>(result.iterations) : 0.0;

    const std::string preconditionerName =
        (preconditioner == PreconditionerType::Jacobi) ? "Jacobi" : "None";
    const bool ranCleanly = (result.status == SIMPLEStatus::Converged ||
                            result.status == SIMPLEStatus::MaxIterations);

    std::cout << preconditionerName << " | " << totalSeconds << " | " << perIterationMs << " | "
              << result.iterations << " | " << (ranCleanly ? "ok" : "FAILED") << " | "
              << stats.gpuLinearSolves << " | " << stats.gpuLinearSolverIterations << " | "
              << stats.gpuSolveSeconds << " | " << stats.preconditionerSetupSeconds << " | "
              << stats.preconditionerApplySeconds << " | " << std::abs(result.globalMassImbalance)
              << "\n";

    nlohmann::json cfg;
    cfg["preconditioner"] = preconditionerName;
    cfg["total_seconds"] = totalSeconds;
    cfg["runtime_per_iteration_ms"] = perIterationMs;
    cfg["outer_iterations"] = static_cast<int>(result.iterations);
    cfg["ran_cleanly"] = ranCleanly;
    cfg["final_u_residual"] = result.finalUResidual;
    cfg["final_v_residual"] = result.finalVResidual;
    cfg["final_pressure_residual"] = result.finalPressureResidual;
    cfg["final_continuity_residual"] = result.finalContinuityResidual;
    cfg["global_mass_imbalance"] = std::abs(result.globalMassImbalance);
    cfg["gpu_linear_solves"] = stats.gpuLinearSolves;
    cfg["gpu_linear_solver_iterations"] = stats.gpuLinearSolverIterations;
    cfg["gpu_solve_seconds"] = stats.gpuSolveSeconds;
    cfg["preconditioner_setup_seconds"] = stats.preconditionerSetupSeconds;
    cfg["preconditioner_apply_seconds"] = stats.preconditionerApplySeconds;
    cfg["kernel_seconds"] = stats.kernelSeconds;
    configs.push_back(cfg);
  }

  j["configurations"] = configs;
  std::filesystem::create_directories(outDir);
  std::ofstream out(outDir + "/simple_" + std::to_string(nx) + "x" + std::to_string(ny) + ".json");
  out << j.dump(2);
}

}  // namespace

int main() {
  const std::string outDir = "results/performance/preconditioner";

  std::cout << "cfd_benchmark_gpu_preconditioner -- CFDApp version " << cfd::core::versionString()
            << "\n";
  std::cout << "cuda_available_at_runtime: " << (cfd::gpu::cudaAvailable() ? "yes" : "no") << "\n\n";

  std::cout << "=== Linear-system-level: CG/BiCGSTAB x CPU/GPU x None/Jacobi ===\n";
  runLinearSystemBenchmarks(outDir);

  if (!cfd::gpu::cudaAvailable()) {
    std::cout << "\nno usable CUDA device at runtime -- skipping production SIMPLE GPU "
                "benchmark.\n";
    return 0;
  }

  std::cout << "\n=== Production SIMPLE (lid-driven cavity, GPU BiCGSTAB) ===\n";
  runSimpleBenchmark(40, 40, outDir);
  runSimpleBenchmark(80, 80, outDir);

  return 0;
}
