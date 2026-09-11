// P7-PERF-002 -- Performance: real, end-to-end OpenMP thread-count
// scaling for the production CPU SIMPLE solve. benchmark_spmv_scaling.cpp
// (P4) already covers the one parallelized kernel in isolation
// (SparseMatrix::multiply, the only #pragma omp in this codebase --
// confirmed by grepping src/, include/, cuda/ for "pragma omp": exactly
// one hit) -- that microbenchmark "may supplement the analysis but
// cannot replace end-to-end results" (this task's own section 6), so
// this file measures the whole thing: a complete SIMPLE::solve() call
// (mesh setup through max-iteration result), timed at each thread count,
// reusing the same production assembly/SIMPLE code unmodified (same
// convention as benchmark_runner.cpp/benchmark_cuda_end_to_end.cpp).
//
// Since SpMV is the *only* parallel region, and every other phase
// (assembly, dot/l2Norm/vector arithmetic inside CG/BiCGSTAB, residual
// computation, field updates) is single-threaded regardless of
// OMP_NUM_THREADS, this benchmark's own expected finding -- confirmed or
// refuted by its own measurements, not assumed -- is that end-to-end
// scaling is far more modest than the pure-SpMV numbers above suggest,
// and the "component breakdown" section makes the reason directly
// visible (assembly time is flat across thread counts; only the linear-
// solve time can move at all).
//
// Usage: run with no arguments; writes results/performance/
// openmp_scaling/{runs.csv,summary.csv,metadata.json} and prints summary
// tables to stdout. Prints a clear message and exits 0 (not a failure)
// if built without OpenMP, matching benchmark_spmv_scaling.cpp's own
// convention.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
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
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

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

#ifndef _OPENMP

int main() {
  std::cout << "cfd_benchmark_openmp_scaling: built without OpenMP "
              "(-DCFDAPP_ENABLE_OPENMP=OFF) -- nothing to scale, exiting.\n";
  return 0;
}

#else

namespace {

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

SIMPLESettings makeCavitySettings(Index outerIterations) {
  SIMPLESettings settings;
  settings.maxIterations = outerIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-10;
  settings.pressureTolerance = 1e-10;
  settings.continuityTolerance = 1e-10;

  settings.momentumSolver.type = LinearSolverType::BiCGSTAB;
  settings.momentumSolver.backend = LinearSolverBackend::CPU;
  settings.momentumSolver.maxIterations = 1000;
  settings.momentumSolver.absoluteTolerance = 1e-8;
  settings.momentumSolver.relativeTolerance = 1e-6;
  settings.momentumSolver.preconditioner = PreconditionerType::None;

  settings.pressureSolver.type = LinearSolverType::BiCGSTAB;
  settings.pressureSolver.backend = LinearSolverBackend::CPU;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-7;
  settings.pressureSolver.relativeTolerance = 1e-5;
  settings.pressureSolver.preconditioner = PreconditionerType::None;

  return settings;
}

struct Stats {
  double min{}, median{}, mean{}, stddev{};
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

struct RunResult {
  double totalSeconds{};
  Index outerIterations{};
  SIMPLEStatus status{};
  bool ranCleanly{};
  Real globalMassImbalance{};
  VectorField velocity;
  ScalarField pressure;
};

RunResult runOnce(const Mesh& mesh, const FluidProperties& fluid,
                  const BoundaryConditionSet& velocityBoundaries,
                  const BoundaryConditionSet& pressureBoundaries, Index outerIterations) {
  const Index n = mesh.numberOfCells();
  VectorField velocity(n, Vector2{0.0, 0.0});
  ScalarField pressure(n, 0.0);

  cfd::Timer timer;
  const SIMPLE simple(makeCavitySettings(outerIterations));
  const SIMPLEResult result =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
  const double totalSeconds = timer.elapsedSeconds();

  RunResult run;
  run.totalSeconds = totalSeconds;
  run.outerIterations = result.iterations;
  run.status = result.status;
  run.ranCleanly =
      (result.status == SIMPLEStatus::Converged || result.status == SIMPLEStatus::MaxIterations);
  run.globalMassImbalance = result.globalMassImbalance;
  run.velocity = result.velocity;
  run.pressure = result.pressure;
  return run;
}

struct BreakdownResult {
  double momentumAssemblySeconds{};
  double pressureAssemblySeconds{};
  double momentumSolveSeconds{};
  double pressureSolveSeconds{};
};

BreakdownResult runBreakdown(const Mesh& mesh, const FluidProperties& fluid,
                             const BoundaryConditionSet& velocityBoundaries,
                             const BoundaryConditionSet& pressureBoundaries) {
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
  momentumSettings.backend = LinearSolverBackend::CPU;

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
  pressureSettings.backend = LinearSolverBackend::CPU;

  cfd::Timer pressureSolveTimer;
  const auto pressureSolver = makeLinearSolver(pressureSettings);
  const auto pressureSolve = pressureSolver->solve(pressureAssembly.system);
  breakdown.pressureSolveSeconds = pressureSolveTimer.elapsedSeconds();
  (void)pressureSolve;

  return breakdown;
}

}  // namespace

int main() {
  omp_set_dynamic(0);  // section 5: never let OpenMP silently choose a different thread count.

  const std::string outDir = "results/performance/openmp_scaling";
  std::filesystem::create_directories(outDir);

  const int logicalProcessors = omp_get_num_procs();
  std::cout << "cfd_benchmark_openmp_scaling -- CFDApp version " << cfd::core::versionString()
            << "\n";
  std::cout << "omp_get_num_procs(): " << logicalProcessors << "\n";

  // Section 4/22: 1/2/4/8 mandatory, plus this machine's own physical-
  // core (16, per lscpu) and logical-thread (32) maxima so hyperthreading
  // effectiveness can be evaluated -- never a fabricated count beyond
  // what omp_get_num_procs() actually reports.
  std::vector<int> threadCounts = {1, 2, 4, 8};
  if (logicalProcessors >= 16) threadCounts.push_back(16);
  if (logicalProcessors > 16) threadCounts.push_back(logicalProcessors);
  // Deduplicate while preserving order (logicalProcessors may already equal 16 or a smaller value).
  std::vector<int> dedup;
  for (int t : threadCounts) {
    if (std::find(dedup.begin(), dedup.end(), t) == dedup.end()) dedup.push_back(t);
  }
  threadCounts = dedup;

  // Section 2: a substantial grid so per-iteration work is large enough
  // that thread-launch overhead does not trivially dominate -- 160x160,
  // the same grid/outer-iteration budget P7-PERF-001 already used for
  // its own CPU baseline (directly comparable).
  const Index nx = 160, ny = 160;
  constexpr Index kOuterIterations = 60;
  constexpr int kRepeats = 3;

  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, 1.0);
  const auto pressureBoundaries = makeCavityPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Index n = mesh.numberOfCells();

  std::cout << "grid: " << nx << "x" << ny << " (" << n << " cells), outer_iterations="
            << kOuterIterations << ", repeats=" << kRepeats << "\n\n";

  std::ofstream runsCsv(outDir + "/runs.csv");
  runsCsv << "threads,repeat,total_seconds,outer_iterations,status,ran_cleanly,mass_imbalance\n";

  nlohmann::json summaryJson = nlohmann::json::array();
  RunResult firstThreadBaseline;
  bool haveBaseline = false;

  std::cout << "threads | median_s | min_s | mean_s | stddev_s | speedup | efficiency | "
              "assembly_s | solve_s | max_abs_velocity_diff_from_1_thread\n";

  double baselineMedian = 0.0;
  for (const int threads : threadCounts) {
    omp_set_num_threads(threads);

    const auto breakdown = runBreakdown(mesh, fluid, velocityBoundaries, pressureBoundaries);

    std::vector<RunResult> runs;
    for (int r = 0; r < kRepeats; ++r) {
      runs.push_back(runOnce(mesh, fluid, velocityBoundaries, pressureBoundaries, kOuterIterations));
    }

    for (std::size_t r = 0; r < runs.size(); ++r) {
      const auto& run = runs[r];
      runsCsv << threads << "," << r << "," << run.totalSeconds << "," << run.outerIterations << ","
              << (run.status == SIMPLEStatus::MaxIterations ? "MaxIterations" : "Other") << ","
              << (run.ranCleanly ? "yes" : "no") << "," << std::abs(run.globalMassImbalance) << "\n";
    }

    std::vector<double> totals;
    for (const auto& r : runs) totals.push_back(r.totalSeconds);
    const Stats stats = computeStats(totals);
    if (threads == 1) {
      baselineMedian = stats.median;
      firstThreadBaseline = runs.front();
      haveBaseline = true;
    }
    const double speedup = stats.median > 0.0 ? baselineMedian / stats.median : 0.0;
    const double efficiency = speedup / static_cast<double>(threads);

    // Section 16/17: numerical equivalence against the 1-thread baseline
    // -- SparseMatrix::multiply is documented+tested bit-identical at any
    // thread count (its own header comment), and it is the only
    // parallelized region, so the full SIMPLE result is expected to be
    // exactly bit-identical too; measured directly here rather than
    // assumed.
    Real maxAbsVelocityDiff = 0.0;
    if (haveBaseline) {
      for (Index i = 0; i < runs.front().velocity.size(); ++i) {
        maxAbsVelocityDiff = std::max(
            maxAbsVelocityDiff,
            std::abs(runs.front().velocity[i].x - firstThreadBaseline.velocity[i].x));
        maxAbsVelocityDiff = std::max(
            maxAbsVelocityDiff,
            std::abs(runs.front().velocity[i].y - firstThreadBaseline.velocity[i].y));
      }
    }

    const bool allClean =
        std::all_of(runs.begin(), runs.end(), [](const RunResult& r) { return r.ranCleanly; });

    std::cout << threads << " | " << stats.median << " | " << stats.min << " | " << stats.mean
              << " | " << stats.stddev << " | " << speedup << " | " << efficiency << " | "
              << breakdown.momentumAssemblySeconds + breakdown.pressureAssemblySeconds << " | "
              << breakdown.momentumSolveSeconds + breakdown.pressureSolveSeconds << " | "
              << maxAbsVelocityDiff << (allClean ? "" : "  [FAILED RUN]") << "\n";

    nlohmann::json j;
    j["threads"] = threads;
    j["grid"] = std::to_string(nx) + "x" + std::to_string(ny);
    j["cells"] = static_cast<int>(n);
    j["outer_iterations"] = static_cast<int>(runs.front().outerIterations);
    j["repeats"] = kRepeats;
    j["min_seconds"] = stats.min;
    j["median_seconds"] = stats.median;
    j["mean_seconds"] = stats.mean;
    j["stddev_seconds"] = stats.stddev;
    j["speedup"] = speedup;
    j["efficiency"] = efficiency;
    j["momentum_assembly_seconds"] = breakdown.momentumAssemblySeconds;
    j["pressure_assembly_seconds"] = breakdown.pressureAssemblySeconds;
    j["momentum_solve_seconds"] = breakdown.momentumSolveSeconds;
    j["pressure_solve_seconds"] = breakdown.pressureSolveSeconds;
    j["max_abs_velocity_diff_from_1_thread"] = maxAbsVelocityDiff;
    j["ran_cleanly"] = allClean;
    j["mass_imbalance"] = std::abs(runs.front().globalMassImbalance);
    summaryJson.push_back(j);
  }

  std::ofstream summaryCsv(outDir + "/summary.csv");
  summaryCsv << "threads,grid,cells,outer_iterations,min_s,median_s,mean_s,stddev_s,speedup,"
                "efficiency,assembly_s,solve_s,max_abs_velocity_diff_from_1_thread,converged\n";
  for (const auto& j : summaryJson) {
    summaryCsv << j["threads"].get<int>() << "," << j["grid"].get<std::string>() << ","
              << j["cells"].get<int>() << "," << j["outer_iterations"].get<int>() << ","
              << j["min_seconds"].get<double>() << "," << j["median_seconds"].get<double>() << ","
              << j["mean_seconds"].get<double>() << "," << j["stddev_seconds"].get<double>() << ","
              << j["speedup"].get<double>() << "," << j["efficiency"].get<double>() << ","
              << (j["momentum_assembly_seconds"].get<double>() +
                  j["pressure_assembly_seconds"].get<double>())
              << "," << (j["momentum_solve_seconds"].get<double>() +
                        j["pressure_solve_seconds"].get<double>())
              << "," << j["max_abs_velocity_diff_from_1_thread"].get<double>() << ","
              << (j["ran_cleanly"].get<bool>() ? "yes" : "no") << "\n";
  }

  // Section 24: best-performing thread count by measured median runtime,
  // not theoretical core count.
  int bestThreads = threadCounts.front();
  double bestMedian = summaryJson.front()["median_seconds"].get<double>();
  for (const auto& j : summaryJson) {
    const double median = j["median_seconds"].get<double>();
    if (median < bestMedian) {
      bestMedian = median;
      bestThreads = j["threads"].get<int>();
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
  metadata["omp_get_num_procs"] = logicalProcessors;
  metadata["grid"] = std::to_string(nx) + "x" + std::to_string(ny);
  metadata["outer_iterations"] = static_cast<int>(kOuterIterations);
  metadata["best_thread_count"] = bestThreads;
  metadata["best_median_seconds"] = bestMedian;
  metadata["hardware_note"] =
      "See results/performance/cuda_end_to_end/environment_hardware.txt (same machine, captured "
      "during P7-PERF-001) for CPU/RAM details; lscpu reports 16 cores/socket, 2 threads/core, "
      "32 logical CPUs total (WSL2's own flattened view of this host's hybrid P/E-core topology "
      "-- see this run's own written summary for the caveat).";
  std::ofstream metaOut(outDir + "/metadata.json");
  metaOut << metadata.dump(2);

  std::cout << "\nbest_thread_count: " << bestThreads << " (median " << bestMedian << "s)\n";

  return 0;
}

#endif
