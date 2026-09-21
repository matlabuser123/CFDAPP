// GPU-PIPE-001 Phase 2 -- paired before/after probe.
//
// Phase 1 measured ~10% cross-session variation, so a Phase-2 speed-up claim
// must come from BEFORE and AFTER binaries run in the SAME session, alternating.
// This is the single source both are built from.
//
// It must therefore compile against the pre-Phase-2 GPUExecutionStats, which has
// no reductionGroups/reductionQuantities. Rather than forking the file or
// #ifdef-ing on a version, it detects those members at compile time and prints
// -1 where they do not exist -- so BEFORE and AFTER really are the same probe.
//
// Case, settings and budgets follow results/gpu-pcorr-001/tools/pcorr_repro.cpp,
// which in turn copies benchmarks/gpu/benchmark_cuda_end_to_end.cpp, so the
// numbers are comparable with the Phase-1 baseline.
//
// usage: paired_probe <edge> <outer-iterations> <cpu|gpu>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

// --- compile-time detection of the Phase-2C counters ----------------------
template <typename T, typename = void>
struct HasReductionCounters : std::false_type {};
template <typename T>
struct HasReductionCounters<T, std::void_t<decltype(T::reductionGroups)>> : std::true_type {};

template <typename S>
long long reductionGroupsOf(const S& s) {
  if constexpr (HasReductionCounters<S>::value) {
    return static_cast<long long>(s.reductionGroups);
  } else {
    return -1;  // pre-Phase-2 build: the counter does not exist
  }
}
template <typename S>
long long reductionQuantitiesOf(const S& s) {
  if constexpr (HasReductionCounters<S>::value) {
    return static_cast<long long>(s.reductionQuantities);
  } else {
    return -1;
  }
}

BoundaryConditionSet cavityVelocity(const Mesh& mesh, Real lidSpeed) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(cfd::Vector2{lidSpeed, 0.0}));
  return boundaries;
}

BoundaryConditionSet cavityPressure(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return boundaries;
}

SIMPLESettings cavitySettings(Index outerIterations, LinearSolverBackend backend) {
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

}  // namespace

int main(int argc, char** argv) {
  const Index edge = argc > 1 ? static_cast<Index>(std::atoi(argv[1])) : 160;
  const Index outer = argc > 2 ? static_cast<Index>(std::atoi(argv[2])) : 8;
  const std::string backendArg = argc > 3 ? argv[3] : "gpu";
  const auto backend =
      (backendArg == "cpu") ? LinearSolverBackend::CPU : LinearSolverBackend::GPU;

  cfd::gpu::resetGpuExecutionStats();
  const Mesh mesh = MeshGeometry::createCartesian2D(edge, edge, 1.0, 1.0);
  const auto vbc = cavityVelocity(mesh, 1.0);
  const auto pbc = cavityPressure(mesh);
  const FluidProperties fluid(1.0, 0.01);
  cfd::fields::VectorField velocity(mesh.numberOfCells());
  cfd::fields::ScalarField pressure(mesh.numberOfCells());

  const SIMPLE simple(cavitySettings(outer, backend));
  cfd::Timer timer;
  const auto result =
      simple.solve(mesh, fluid, vbc, pbc, velocity, pressure);
  const double solveSeconds = timer.elapsedSeconds();
  const auto& st = cfd::gpu::gpuExecutionStats();

  // One machine-parseable line per run.
  std::printf(
      "RESULT edge=%lld backend=%s cells=%lld outer=%lld solve_s=%.6f "
      "p_res=%.17g cont=%.17g mass=%.17g p_lin_it=%lld "
      "h2d_calls=%llu h2d_bytes=%llu d2h_calls=%llu d2h_bytes=%llu "
      "syncs=%llu launches=%llu krylov_it=%llu solves=%llu "
      "red_groups=%lld red_quantities=%lld gpu_solve_s=%.6f status=%d fallbacks=%llu "
      "allocs=%llu reallocs=%llu frees=%llu\n",
      static_cast<long long>(edge), backendArg.c_str(),
      static_cast<long long>(mesh.numberOfCells()), static_cast<long long>(result.iterations),
      solveSeconds, result.finalPressureResidual, result.finalContinuityResidual,
      result.globalMassImbalance, static_cast<long long>(result.pressureLinearIterations),
      static_cast<unsigned long long>(st.hostToDeviceCalls),
      static_cast<unsigned long long>(st.hostToDeviceBytes),
      static_cast<unsigned long long>(st.deviceToHostCalls),
      static_cast<unsigned long long>(st.deviceToHostBytes),
      static_cast<unsigned long long>(st.synchronizations),
      static_cast<unsigned long long>(st.kernelLaunches),
      static_cast<unsigned long long>(st.gpuLinearSolverIterations),
      static_cast<unsigned long long>(st.gpuLinearSolves), reductionGroupsOf(st),
      reductionQuantitiesOf(st), st.gpuSolveSeconds, static_cast<int>(result.status),
      static_cast<unsigned long long>(st.gpuBackendFallbacks),
      static_cast<unsigned long long>(st.allocations),
      static_cast<unsigned long long>(st.reallocations),
      static_cast<unsigned long long>(st.frees));
  return 0;
}
