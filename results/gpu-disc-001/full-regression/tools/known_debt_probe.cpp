// GPU-DISC-001R -- the known GPU BiCGSTAB restart asymmetry, on the final build.
//
// TODO.md's recorded reproducer, run on three arms so "has it broadened?" is
// answered by comparison rather than by re-observing the known failure:
//
//   cpu        CPU solver, CPU discretization   -- the reference behaviour
//   gpu-pipe   GPU solver, CPU discretization   -- where the debt was recorded
//   gpu-disc   GPU solver, GPU discretization   -- the path GPU-DISC-001 added
//
// BROADENED would mean: gpu-disc fails where gpu-pipe does not, or either GPU
// arm fails on a case that was previously healthy. UNCHANGED means gpu-disc
// behaves exactly as gpu-pipe does.
//
// Nothing here fixes the debt. It is not authorized and is not in scope.
#include <cstdio>
#include <memory>
#include <string>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

const char* statusName(SIMPLEStatus s) {
  switch (s) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    case SIMPLEStatus::Diverging: return "Diverging";
    case SIMPLEStatus::Stagnated: return "Stagnated";
    default: return "other";
  }
}

}  // namespace

int main() {
  std::printf("=== GPU-DISC-001R: known GPU BiCGSTAB restart asymmetry ===\n");
  std::printf("TODO.md reproducer: 2D cavity 40x40, outer tolerance 1e-6, budget 3000\n");
  std::printf("recorded at baseline 548401a -- NOT introduced here, NOT fixed here.\n\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }

  const Mesh mesh = MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0);
  BoundaryConditionSet vb, pb;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    const bool lid = i + 1 == mesh.boundaryPatches().size();
    vb.set(mesh, patch.name(),
           lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                     std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
               : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                     std::make_unique<cfd::boundary::Wall>()));
    pb.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  const FluidProperties fluid{1.0, 0.01};
  const VectorField v0(mesh.numberOfCells());
  const ScalarField p0(mesh.numberOfCells());

  const auto run = [&](LinearSolverBackend backend, bool gpuDisc) {
    SIMPLESettings s;
    s.maxIterations = 3000;
    s.velocityRelaxation = 0.7;
    s.pressureRelaxation = 0.3;
    s.velocityTolerance = 1e-6;
    s.pressureTolerance = 1e-6;
    s.continuityTolerance = 1e-6;
    s.momentumSolver.type = LinearSolverType::BiCGSTAB;
    s.momentumSolver.backend = backend;
    // The REPRODUCER's tolerances, from tests/solver/simple/test_simple_gpu_solver.cpp
    // (lines 73-79) -- not the benchmark's. A first version of this probe used
    // the benchmark settings (1e-8/1e-6, 1e-7/1e-5) and all three arms reached
    // MaxIterations: the documented failure never triggered, which made the
    // verdict VACUOUS. A probe that cannot reproduce the condition it is
    // checking cannot report that the condition is unchanged.
    s.momentumSolver.maxIterations = 500;
    s.momentumSolver.absoluteTolerance = 1e-10;
    s.momentumSolver.relativeTolerance = 1e-8;
    s.momentumSolver.preconditioner = PreconditionerType::None;
    s.pressureSolver.type = LinearSolverType::BiCGSTAB;
    s.pressureSolver.backend = backend;
    s.pressureSolver.maxIterations = 2000;
    s.pressureSolver.absoluteTolerance = 1e-10;
    s.pressureSolver.relativeTolerance = 1e-8;
    s.pressureSolver.preconditioner = PreconditionerType::None;
    s.enableGpuDiscretization = gpuDisc;
    const SIMPLE simple(s, /*referenceCell=*/0);
    return simple.solve(mesh, fluid, vb, pb, v0, p0);
  };

  const SIMPLEResult cpu = run(LinearSolverBackend::CPU, false);
  std::printf("  %-28s status=%-26s iterations=%lld\n", "cpu (CPU solver, CPU disc)",
              statusName(cpu.status), static_cast<long long>(cpu.iterations));
  const SIMPLEResult pipe = run(LinearSolverBackend::GPU, false);
  std::printf("  %-28s status=%-26s iterations=%lld\n", "gpu-pipe (GPU solver, CPU disc)",
              statusName(pipe.status), static_cast<long long>(pipe.iterations));
  const SIMPLEResult disc = run(LinearSolverBackend::GPU, true);
  std::printf("  %-28s status=%-26s iterations=%lld\n", "gpu-disc (GPU solver, GPU disc)",
              statusName(disc.status), static_cast<long long>(disc.iterations));

  std::printf("\n=== has the debt BROADENED? ===\n");
  const bool sameStatus = pipe.status == disc.status;
  const bool sameIters = pipe.iterations == disc.iterations;
  std::printf("  gpu-disc status matches gpu-pipe:     %s\n", sameStatus ? "yes" : "NO");
  std::printf("  gpu-disc iterations match gpu-pipe:   %s (%lld vs %lld)\n",
              sameIters ? "yes" : "NO", static_cast<long long>(disc.iterations),
              static_cast<long long>(pipe.iterations));

  // Broadened means the NEW path fails where the OLD one did not. Both arms
  // showing the SAME recorded behaviour is the pass condition; the debt itself
  // staying present is expected and is not a failure of this gate.
  // NON-VACUITY: if the GPU arm does not reproduce the recorded failure at all,
  // this probe has not tested anything and must say so rather than reporting
  // "unchanged". The recorded behaviour is PressureCorrectionFailure on the GPU
  // solver arm against MaxIterations on the CPU arm.
  const bool reproduced = pipe.status == SIMPLEStatus::PressureCorrectionFailure;
  std::printf("  reproducer actually triggered on gpu-pipe:  %s\n",
              reproduced ? "yes" : "NO -- this probe would be VACUOUS");

  if (!reproduced) {
    std::printf("\nKNOWN DEBT: NOT REPRODUCED -- the recorded GPU failure did not occur on this\n"
                "            build/settings combination, so this run proves nothing about whether\n"
                "            the debt broadened. Reported as such rather than as 'unchanged'.\n");
    return 2;
  }

  const bool broadened = !sameStatus;
  std::printf("\n%s\n", broadened
      ? "KNOWN DEBT: BROADENED -- the GPU-DISC path fails where the old GPU path did not"
      : "KNOWN DEBT: UNCHANGED -- GPU-DISC behaves exactly as the pre-existing GPU path");
  return broadened ? 1 : 0;
}
