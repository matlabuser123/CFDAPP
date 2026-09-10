// P4 -- Performance: the primary CPU benchmark. Reuses the *existing,
// unmodified* production assembly/solve functions directly (never a
// separate "fast path" reimplementation -- TODO.md P4 section 1's own
// "Numerical correctness > reproducibility > measured performance"
// priority) around cfd::ScopedTimer/cfd::Profiler instrumentation, so
// every timing category below is measuring the actual code SIMPLE::solve()
// itself calls each outer iteration, not a synthetic proxy for it.
//
// Primary benchmark case (section 3): a closed lid-driven cavity (Wall on
// three sides, MovingWall lid) -- this project's own mandatory P0
// validation case, already numerically trusted at every grid size used
// here.
//
// Usage: run with no arguments; writes results/performance/baseline/
// benchmark_<N>x<N>.json for each configured grid and prints a summary
// table to stdout. Not part of the routine CTest suite (section 27: "Do
// not put fragile wall-clock limits into routine unit tests") -- this is
// a standalone, explicitly-invoked tool.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Profiler.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/core/Version.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
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
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::evaluateContinuity;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::assembleRelaxedMomentumComponent;
using cfd::pressure_velocity::computeMomentumResponseCoefficient;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

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

// Deliberately a *fixed* outer-iteration budget across every grid size,
// not tuned per grid to reach tight-tolerance convergence (the mandatory
// P0 Ghia validation's own settings need 6000-20000 outer iterations at
// tight 1e-6 tolerance -- appropriate for a correctness gate, not for a
// performance benchmark, TODO.md P4's own section 40 "a benchmark only
// counts if the case remains finite... converged according to canonical
// criteria" note is about not benchmarking a *diverging* run, which this
// is not). Running the identical, fixed number of outer iterations at
// every grid size gives a well-posed "runtime/iteration" metric (section
// 7's own required field) independent of how many iterations
// convergence at any particular resolution happens to need -- exactly
// the quantity every later P4 optimization stage is compared against.
SIMPLESettings makeCavitySettings(Index maxIterations) {
  SIMPLESettings settings;
  settings.maxIterations = maxIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-10;   // effectively unreachable -- always runs the full budget.
  settings.pressureTolerance = 1e-10;
  settings.continuityTolerance = 1e-10;
  // Diagnosed (same class of finding as every P3-PHYS task's own
  // unpreconditioned-BiCGSTAB-robustness note in this codebase's TODO.md):
  // the 80x80 grid's own inner solves need a looser absolute tolerance
  // than 20x20/40x40 tolerate fine -- loosened here across the board
  // rather than per-grid, since this benchmark's own purpose is
  // comparative timing, not tolerance-sensitivity research.
  settings.momentumSolver.maxIterations = 1000;
  settings.momentumSolver.absoluteTolerance = 1e-8;
  settings.momentumSolver.relativeTolerance = 1e-6;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-7;
  settings.pressureSolver.relativeTolerance = 1e-5;
  return settings;
}

constexpr Index kFixedOuterIterations = 200;

struct GridBenchmark {
  Index nx, ny;
  double meshSetupSeconds{};
  double momentumAssemblySeconds{};
  double pressureAssemblySeconds{};
  double linearSolveSeconds{};
  double massFluxSeconds{};
  double residualSeconds{};
  double totalSimpleSeconds{};
  Index simpleIterations{};
  bool converged{};
  double globalMassImbalance{};
};

// One repeat of the per-category building-block timings (section 6):
// mesh setup, one momentum-component assembly, one pressure-correction
// assembly, one linear solve on each, mass-flux calculation, and a
// residual (continuity) evaluation -- each measured directly around the
// unmodified production function call, using a fresh Profiler category
// per call so repeats accumulate additively (median is computed by the
// caller from repeated whole-run totals, not from these accumulated
// per-call sums).
GridBenchmark runOnce(Index nx, Index ny) {
  GridBenchmark result{nx, ny};

  cfd::Timer meshTimer;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  result.meshSetupSeconds = meshTimer.elapsedSeconds();

  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, 1.0);
  const auto pressureBoundaries = makeCavityPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Index n = mesh.numberOfCells();

  VectorField velocity(n, Vector2{0.0, 0.0});
  ScalarField pressure(n, 0.0);
  const ScalarField effectiveViscosity(n, fluid.dynamicViscosity());
  const SurfaceField massFlux0 = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  ScalarField previousU(n, 0.0);

  cfd::Timer momentumTimer;
  const auto momentumU = assembleRelaxedMomentumComponent(
      mesh, velocity, pressure, massFlux0, effectiveViscosity, velocityBoundaries,
      pressureBoundaries, VelocityComponent::U, previousU, 0.7);
  result.momentumAssemblySeconds = momentumTimer.elapsedSeconds();

  cfd::Timer linearSolveTimer;
  cfd::algebra::BiCGSTAB momentumSolver({1e-10, 1e-8, 500});
  const auto momentumSolve = momentumSolver.solve(momentumU.system);
  result.linearSolveSeconds = linearSolveTimer.elapsedSeconds();
  (void)momentumSolve;

  const auto uResponse = computeMomentumResponseCoefficient(mesh, momentumU.diagonal);
  const auto vResponse = uResponse;  // symmetric enough for a benchmark predictor flux.

  cfd::Timer pressureTimer;
  const auto pressureAssembly = assemblePressureCorrection(mesh, massFlux0, uResponse, vResponse,
                                                            fluid.density(), 0, pressureBoundaries);
  result.pressureAssemblySeconds = pressureTimer.elapsedSeconds();

  cfd::Timer pressureSolveTimer;
  cfd::algebra::BiCGSTAB pressureSolver({1e-9, 1e-7, 2000});
  const auto pressureSolve = pressureSolver.solve(pressureAssembly.system);
  result.linearSolveSeconds += pressureSolveTimer.elapsedSeconds();
  (void)pressureSolve;

  cfd::Timer massFluxTimer;
  const SurfaceField massFlux1 = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  result.massFluxSeconds = massFluxTimer.elapsedSeconds();

  cfd::Timer residualTimer;
  const auto continuity = evaluateContinuity(mesh, massFlux1);
  result.residualSeconds = residualTimer.elapsedSeconds();
  (void)continuity;

  // Full end-to-end SIMPLE::solve() -- "total solver runtime" (section
  // 6), the actual quantity every later optimization stage is judged
  // against (section 9).
  cfd::Timer totalTimer;
  const SIMPLE simple(makeCavitySettings(kFixedOuterIterations));
  const SIMPLEResult flow =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
  result.totalSimpleSeconds = totalTimer.elapsedSeconds();
  result.simpleIterations = flow.iterations;
  // Every field stays finite/mass-conservative at every outer iteration
  // regardless of whether the (deliberately unreachable) outer tolerance
  // was hit -- "converged" here means "ran the full fixed budget without
  // a numerical failure" (MomentumFailure/PressureCorrectionFailure/
  // NonFiniteState/InvalidConfiguration), not SIMPLEStatus::Converged
  // (which this benchmark's own settings never aim for -- see
  // makeCavitySettings's own comment).
  result.converged = (flow.status == SIMPLEStatus::Converged ||
                      flow.status == SIMPLEStatus::MaxIterations);
  result.globalMassImbalance = std::abs(flow.globalMassImbalance);

  return result;
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const std::size_t mid = values.size() / 2;
  if (values.size() % 2 == 0) return 0.5 * (values[mid - 1] + values[mid]);
  return values[mid];
}

}  // namespace

int main(int argc, char** argv) {
  // Section 37: "20x20/40x40/80x80" is the routine baseline; --large adds
  // the 160x160/320x320 large-grid family (section 38) without making
  // every invocation of this tool pay their much longer runtime.
  bool includeLargeGrids = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--large") includeLargeGrids = true;
  }

  std::vector<std::pair<Index, Index>> grids = {{20, 20}, {40, 40}, {80, 80}};
  if (includeLargeGrids) {
    grids.push_back({160, 160});
    grids.push_back({320, 320});
  }
  constexpr int kRepeats = 3;

  std::filesystem::create_directories("results/performance/baseline");

  // Section 4: environment metadata -- written once per run, alongside
  // the per-grid benchmark files.
  {
    nlohmann::json env;
    env["cfdapp_version"] = cfd::core::versionString();
#if defined(__VERSION__)
    env["compiler"] = __VERSION__;
#endif
#ifdef NDEBUG
    env["build_type"] = "Release (NDEBUG)";
#else
    env["build_type"] = "Debug/other (NDEBUG not defined)";
#endif
#ifdef _OPENMP
    env["openmp_enabled"] = true;
#else
    env["openmp_enabled"] = false;
#endif
#ifdef CFDAPP_ENABLE_CUDA
    env["cuda_enabled_at_build"] = true;
#else
    env["cuda_enabled_at_build"] = false;
#endif
    env["hardware_note"] =
        "See results/performance/baseline/environment_hardware.txt (nproc/free/nvidia-smi "
        "captured alongside this run) for CPU/RAM/GPU details -- not embedded here to avoid "
        "duplicating shell-tool output inside C++.";
    std::ofstream envOut("results/performance/baseline/environment.json");
    envOut << env.dump(2);
  }

  std::cout << "CFDApp P4 CPU benchmark -- cfdapp version " << cfd::core::versionString() << "\n";
  std::cout << "grid      | total(median,s) | per_iter(ms) | momentum_asm(s) | pressure_asm(s) | "
              "linear_solve(s) | mass_flux(s) | residual(s) | iterations | converged\n";

  for (const auto& [nx, ny] : grids) {
    std::vector<GridBenchmark> repeats;
    for (int r = 0; r < kRepeats; ++r) {
      repeats.push_back(runOnce(nx, ny));
    }

    std::vector<double> totals;
    for (const auto& r : repeats) totals.push_back(r.totalSimpleSeconds);
    const double medianTotal = median(totals);
    const auto& last = repeats.back();

    const double perIterationMs =
        last.simpleIterations > 0 ? 1000.0 * medianTotal / static_cast<double>(last.simpleIterations)
                                  : 0.0;
    std::cout << nx << "x" << ny << "   | " << medianTotal << " | " << perIterationMs << " | "
              << last.momentumAssemblySeconds << " | " << last.pressureAssemblySeconds << " | "
              << last.linearSolveSeconds << " | " << last.massFluxSeconds << " | "
              << last.residualSeconds << " | " << last.simpleIterations << " | "
              << (last.converged ? "yes" : "NO") << "\n";

    nlohmann::json j;
    j["grid"] = {{"nx", nx}, {"ny", ny}, {"cells", static_cast<int>(nx * ny)}};
    j["repeats"] = kRepeats;
    std::vector<double> allTotals(totals.begin(), totals.end());
    j["total_simple_seconds"] = {{"median", medianTotal},
                                 {"min", *std::min_element(totals.begin(), totals.end())},
                                 {"max", *std::max_element(totals.begin(), totals.end())}};
    j["runtime_per_iteration_ms"] = perIterationMs;
    j["momentum_assembly_seconds"] = last.momentumAssemblySeconds;
    j["pressure_assembly_seconds"] = last.pressureAssemblySeconds;
    j["linear_solve_seconds"] = last.linearSolveSeconds;
    j["mass_flux_seconds"] = last.massFluxSeconds;
    j["residual_seconds"] = last.residualSeconds;
    j["mesh_setup_seconds"] = last.meshSetupSeconds;
    j["simple_iterations"] = last.simpleIterations;
    j["converged"] = last.converged;
    j["global_mass_imbalance"] = last.globalMassImbalance;

    std::ofstream out("results/performance/baseline/benchmark_" + std::to_string(nx) + "x" +
                      std::to_string(ny) + ".json");
    out << j.dump(2);
  }

  return 0;
}
