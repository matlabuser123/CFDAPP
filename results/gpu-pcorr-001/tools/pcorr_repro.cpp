// GPU-PCORR-001 step 1: the canonical reproducer for the production GPU pressure-correction
// failure, plus the per-solve detail the investigation needs.
//
// Case, settings and per-grid budgets are copied from benchmarks/gpu/benchmark_cuda_end_to_end.cpp
// so this reproduces exactly the run that failed: a lid-driven cavity, CPU and GPU differing only
// in LinearSolverSettings::backend.
//
// usage: pcorr_repro <edge> [outer-iterations]      e.g. pcorr_repro 320 2
#include <cstdio>
#include <cstdlib>
#include <string>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

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
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

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

const char* statusName(SIMPLEStatus status) {
  switch (status) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    default: return "other";
  }
}

// benchmarks/gpu/benchmark_cuda_end_to_end.cpp's makeCavitySettings, verbatim in intent.
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

void run(Index edge, Index outerIterations, LinearSolverBackend backend, const char* label) {
  cfd::gpu::resetGpuExecutionStats();
  const Mesh mesh = MeshGeometry::createCartesian2D(edge, edge, 1.0, 1.0);
  const auto velocityBoundaries = cavityVelocity(mesh, 1.0);
  const auto pressureBoundaries = cavityPressure(mesh);
  const FluidProperties fluid(1.0, 0.01);
  cfd::fields::VectorField velocity(mesh.numberOfCells());
  cfd::fields::ScalarField pressure(mesh.numberOfCells());

  const SIMPLE simple(cavitySettings(outerIterations, backend));
  const auto result =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
  const auto& stats = cfd::gpu::gpuExecutionStats();

  std::printf("  %-4s | cells %7lld | status %-24s | outer it %3lld | p-res %.3e | cont %.3e\n",
              label, static_cast<long long>(mesh.numberOfCells()),
              statusName(result.status),
              static_cast<long long>(result.iterations), result.finalPressureResidual,
              result.finalContinuityResidual);
  std::printf("       | mass imbalance %.3e | p linear iterations %lld\n",
              result.globalMassImbalance,
              static_cast<long long>(result.pressureLinearIterations));
  if (!result.robustness.statusDetail.empty()) {
    std::printf("       | statusDetail: %s\n", result.robustness.statusDetail.c_str());
  }
  std::printf("       | GPU kernelLaunches %llu, fallbacks %llu, H2D %llu, D2H %llu\n",
              static_cast<unsigned long long>(stats.kernelLaunches),
              static_cast<unsigned long long>(stats.gpuBackendFallbacks),
              static_cast<unsigned long long>(stats.hostToDeviceCalls),
              static_cast<unsigned long long>(stats.deviceToHostCalls));
}

}  // namespace

int main(int argc, char** argv) {
  const Index edge = argc > 1 ? static_cast<Index>(std::atoi(argv[1])) : 320;
  const Index outer = argc > 2 ? static_cast<Index>(std::atoi(argv[2])) : 2;
  std::printf("## grid %lldx%lld, %lld outer iterations, cudaAvailable %s\n",
              static_cast<long long>(edge), static_cast<long long>(edge),
              static_cast<long long>(outer), cfd::gpu::cudaAvailable() ? "true" : "false");
  run(edge, outer, LinearSolverBackend::CPU, "CPU");
  run(edge, outer, LinearSolverBackend::GPU, "GPU");
  return 0;
}
