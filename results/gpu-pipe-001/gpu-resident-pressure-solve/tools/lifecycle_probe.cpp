// GPU-PIPE-001 GPU-resident pressure solve -- lifecycle and re-entry.
//
// The resident solve adds persistent state that outlives a single solve: an
// adopted matrix view, a Krylov workspace and two device counters. Persistent
// state is where "works once" and "works every time" come apart, so each
// property is asserted rather than assumed:
//
//   * repeated solves on one facade allocate nothing after the first
//   * a second solve does not inherit the first one's solution as its guess
//   * re-preparing for a LARGER mesh resizes everything and carries no stale
//     state from the smaller one
//   * re-preparing for a SMALLER mesh solves the smaller problem, not a
//     truncated view of the larger buffers it is reusing
//   * a restart (uploadInitialState again) returns a converging solve
//
// The smaller-mesh case matters specifically because DeviceBuffer never shrinks
// -- capacity from the larger mesh is still there, and a length bug would read
// it without any allocation or transfer to give it away.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::GpuSimpleDiscretization;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("  %-4s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

BoundaryConditionSet velocityBc(const Mesh& m) {
  BoundaryConditionSet bc;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    bc.set(m, patch.name(),
           lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                     std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
               : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                     std::make_unique<cfd::boundary::Wall>()));
    ++i;
  }
  return bc;
}

BoundaryConditionSet pressureBc(const Mesh& m) {
  BoundaryConditionSet bc;
  for (const auto& patch : m.boundaryPatches())
    bc.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  return bc;
}

LinearSolverSettings solverSettings() {
  LinearSolverSettings ls;
  ls.type = LinearSolverType::BiCGSTAB;
  ls.preconditioner = PreconditionerType::Jacobi;
  ls.backend = LinearSolverBackend::GPU;
  return ls;
}

// Drives one production iteration up to the pressure stage and then runs the
// resident pressure solve, returning its result.
struct Driver {
  GpuSimpleDiscretization gpu;
  // Mesh has no default constructor, and the point of this probe is to
  // re-prepare one facade for several meshes -- so the mesh is optional and
  // replaced in place, while `gpu` deliberately persists across prepares.
  std::optional<Mesh> meshStorage;
  Index nc{0};
  FluidProperties fluid{1.0, 0.01};
  BoundaryConditionSet vbc, pbc;
  std::unique_ptr<cfd::algebra::LinearSolver> solver;

  [[nodiscard]] const Mesh& mesh() const { return *meshStorage; }

  bool prepare(Index n) {
    meshStorage.emplace(MeshGeometry::createCartesian2D(n, n, 1.0, 1.0));
    nc = mesh().numberOfCells();
    vbc = velocityBc(mesh());
    pbc = pressureBc(mesh());
    std::string reason;
    if (!gpu.prepare(mesh(), vbc, pbc, reason)) {
      std::printf("  prepare refused: %s\n", reason.c_str());
      ++failures;
      return false;
    }
    solver = cfd::algebra::makeLinearSolver(solverSettings());
    restart();
    return true;
  }

  void restart() {
    VectorField velocity(nc);
    ScalarField pressure(nc);
    ScalarField viscosity(nc, fluid.dynamicViscosity());
    SurfaceField massFlux(mesh().numberOfFaces());
    gpu.uploadInitialState(velocity, pressure, massFlux, viscosity);
  }

  cfd::algebra::SolverResult pressureSolve() {
    const Real alpha = 0.7;
    const ScalarField zeroComponent(nc);
    for (Index component = 0; component < 2; ++component) {
      const auto system = gpu.assembleMomentum(component, zeroComponent, alpha, 0, false);
      gpu.setMomentumSolution(component, solver->solve(system).solution);
    }
    gpu.computeResponseCoefficients(false);
    gpu.computePredictedFaceFlux(true, fluid.density(), alpha, 0, false);
    gpu.assemblePressureCorrectionResident(false, 0, fluid.density(), false);
    return gpu.solvePressureCorrectionResident(solverSettings());
  }
};

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001 resident pressure solve: lifecycle ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no CUDA device\n");
    return 2;
  }
  const auto& stats = cfd::gpu::gpuExecutionStats();

  std::printf("\n-- repeated solves on one facade --\n");
  {
    Driver d;
    if (!d.prepare(40)) return 1;
    const auto first = d.pressureSolve();
    const std::uint64_t alloc0 = stats.allocations;
    const auto second = d.pressureSolve();
    const std::uint64_t allocAfter = stats.allocations - alloc0;
    const auto third = d.pressureSolve();
    const std::uint64_t allocAfter2 = stats.allocations - alloc0 - allocAfter;

    check(first.converged() && second.converged() && third.converged(),
          "three consecutive resident solves all converge");
    check(allocAfter == 0 && allocAfter2 == 0,
          "solves after the first allocate nothing");
    // This driver deliberately does NOT apply the pressure update or the
    // corrections between solves, so each solve is handed the same predictor
    // and therefore the same system. That makes repetition a sharp test of
    // state leakage: the solve must be reproducible bit for bit. If the
    // workspace's x were not re-zeroed, the second solve would warm-start from
    // the first solution and diverge from it immediately.
    check(second.initialResidual == first.initialResidual &&
              second.iterations == first.iterations && second.finalResidual == first.finalResidual,
          "repeating an identical solve reproduces it exactly (no state leaks between solves)");
    check(second.solution.empty(), "no host solution is produced");
    std::printf("       allocations after solve 1: %llu, after solve 2: %llu\n",
                static_cast<unsigned long long>(allocAfter),
                static_cast<unsigned long long>(allocAfter2));
  }

  std::printf("\n-- re-prepare for a LARGER mesh --\n");
  {
    Driver d;
    if (!d.prepare(40)) return 1;
    (void)d.pressureSolve();
    if (!d.prepare(80)) return 1;
    const auto grown = d.pressureSolve();
    check(grown.converged(), "the larger mesh converges after re-prepare");
    check(d.gpu.residentSolveBytes() >= static_cast<std::size_t>(d.nc) * sizeof(Real) * 10,
          "the workspace grew to the larger mesh");
    std::printf("       workspace %zu bytes for %llu cells\n", d.gpu.residentSolveBytes(),
                static_cast<unsigned long long>(d.nc));
  }

  std::printf("\n-- re-prepare for a SMALLER mesh (buffers do not shrink) --\n");
  {
    Driver d;
    if (!d.prepare(80)) return 1;
    const auto big = d.pressureSolve();
    if (!d.prepare(40)) return 1;
    const auto small = d.pressureSolve();

    // The authority here is a FRESH facade that only ever saw the small mesh.
    // If the reused one is reading stale capacity, the two disagree.
    Driver fresh;
    if (!fresh.prepare(40)) return 1;
    const auto reference = fresh.pressureSolve();

    check(big.converged() && small.converged(), "both meshes converge");
    check(small.iterations == reference.iterations,
          "the reused facade solves the SAME problem as a fresh one (iterations)");
    check(small.initialResidual == reference.initialResidual,
          "... and from the same initial residual (bitwise)");
    check(small.finalResidual == reference.finalResidual,
          "... reaching the same final residual (bitwise)");
    std::printf("       reused %llu iters / fresh %llu iters\n",
                static_cast<unsigned long long>(small.iterations),
                static_cast<unsigned long long>(reference.iterations));
  }

  std::printf("\n-- restart on the same facade --\n");
  {
    Driver d;
    if (!d.prepare(40)) return 1;
    const auto before = d.pressureSolve();
    (void)d.pressureSolve();
    d.restart();
    const auto after = d.pressureSolve();
    check(after.converged(), "a restarted solve converges");
    check(after.initialResidual == before.initialResidual,
          "a restart reproduces the FIRST solve exactly (bitwise initial residual)");
    check(after.iterations == before.iterations, "... and the same iteration count");
    std::printf("       before %.17g / after %.17g\n", before.initialResidual,
                after.initialResidual);
  }

  std::printf("\n%s\n", failures == 0 ? "LIFECYCLE PROBE: PASS" : "LIFECYCLE PROBE: FAIL");
  return failures == 0 ? 0 : 1;
}
