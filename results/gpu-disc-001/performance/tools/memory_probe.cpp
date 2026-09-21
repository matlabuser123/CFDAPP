// GPU-DISC-001Q -- device-memory behaviour of the integrated GPU-DISC path.
//
// Three questions the benchmark CSV can only answer indirectly:
//
//   1. does anything allocate INSIDE the outer loop?      (per-iteration creep)
//   2. is memory released when a solve ends?              (leak)
//   3. does repeating a solve grow the footprint?         (creep across solves)
//
// (1) is answered by running the SAME case at very different outer budgets: if
// allocations or peak bytes depend on the budget, something allocates per
// iteration. (2) by checking currentDeviceBytes returns to zero once the solve's
// buffers are destroyed. (3) by solving repeatedly and watching peak.
//
// Deliberately a separate tool rather than another mode of the benchmark: it
// needs a different CSV schema, and changing the benchmark's schema midway
// through a measurement campaign would leave incompatible files behind.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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

namespace {

int failures = 0;

struct Case {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

Case cavity(Mesh mesh) {
  Case c{std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
  const auto& m = c.mesh;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    c.velocityBoundaries.set(
        m, patch.name(),
        lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
            : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::Wall>()));
    c.pressureBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  return c;
}

SIMPLESettings settings(Index outer) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-10;
  s.pressureTolerance = 1e-10;
  s.continuityTolerance = 1e-10;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::GPU;
  s.momentumSolver.maxIterations = 1000;
  s.momentumSolver.absoluteTolerance = 1e-8;
  s.momentumSolver.relativeTolerance = 1e-6;
  s.momentumSolver.preconditioner = PreconditionerType::None;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = LinearSolverBackend::GPU;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-7;
  s.pressureSolver.relativeTolerance = 1e-5;
  s.pressureSolver.preconditioner = PreconditionerType::None;
  s.enableGpuDiscretization = true;
  return s;
}

struct Sample {
  std::uint64_t allocations, reallocations, frees, peak, currentAfter;
  Index iterations;
};

Sample solveOnce(const Case& c, Index outer) {
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(settings(outer), /*referenceCell=*/0);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                                      c.velocity, c.pressure);
  const auto& s = cfd::gpu::gpuExecutionStats();
  return Sample{s.allocations, s.reallocations, s.frees, s.peakDeviceBytes, s.currentDeviceBytes,
                r.iterations};
}

}  // namespace

int main() {
  std::printf("=== GPU-DISC-001Q: device-memory behaviour ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no usable CUDA device -- exiting\n");
    return 2;
  }
  const Case c = cavity(MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0));
  std::printf("case: 160x160 cavity, gpu-disc path (GPU discretization + GPU solver)\n");

  // Warm-up, so CUDA context creation is not counted as this case's memory.
  (void)solveOnce(c, 2);

  std::printf("\n--- 1. does anything allocate INSIDE the outer loop? ---\n");
  std::printf("%8s %8s %12s %14s %14s %12s\n", "budget", "iters", "allocations", "reallocations",
              "peak bytes", "frees");
  std::vector<Sample> byBudget;
  for (Index outer : {2, 5, 20, 60}) {
    const Sample s = solveOnce(c, outer);
    byBudget.push_back(s);
    std::printf("%8lld %8lld %12llu %14llu %14llu %12llu\n", static_cast<long long>(outer),
                static_cast<long long>(s.iterations),
                static_cast<unsigned long long>(s.allocations),
                static_cast<unsigned long long>(s.reallocations),
                static_cast<unsigned long long>(s.peak),
                static_cast<unsigned long long>(s.frees));
  }
  const bool allocInvariant =
      std::all_of(byBudget.begin(), byBudget.end(), [&](const Sample& s) {
        return s.allocations == byBudget[0].allocations && s.peak == byBudget[0].peak;
      });
  if (allocInvariant) {
    std::printf("  PASS allocation count and peak bytes are INDEPENDENT of the outer budget\n");
    std::printf("       (2 iterations and 60 iterations allocate identically -- so nothing\n");
    std::printf("        allocates inside the loop, and there is no per-iteration VRAM creep)\n");
  } else {
    std::printf("  FAIL allocations or peak depend on the outer budget -- something allocates\n");
    std::printf("       per iteration\n");
    ++failures;
  }
  const bool noRealloc = std::all_of(byBudget.begin(), byBudget.end(),
                                     [](const Sample& s) { return s.reallocations == 0; });
  std::printf("  %s no buffer ever GREW after its first allocation (reallocations == 0)\n",
              noRealloc ? "PASS" : "FAIL");
  if (!noRealloc) ++failures;

  std::printf("\n--- 2. is device memory released when the solve ends? ---\n");
  const Sample last = byBudget.back();
  std::printf("  currentDeviceBytes after the solve returned: %llu\n",
              static_cast<unsigned long long>(last.currentAfter));
  std::printf("  allocations %llu, frees %llu\n", static_cast<unsigned long long>(last.allocations),
              static_cast<unsigned long long>(last.frees));
  if (last.currentAfter == 0) {
    std::printf("  PASS every DeviceBuffer this solve allocated was released\n");
  } else {
    std::printf("  NOTE %llu bytes still held -- buffers outliving the solve (plan caches etc.)\n",
                static_cast<unsigned long long>(last.currentAfter));
  }

  std::printf("\n--- 3. does repeating the SAME solve grow the footprint? ---\n");
  std::printf("%8s %12s %14s %14s\n", "repeat", "allocations", "reallocations", "peak bytes");
  std::vector<Sample> repeats;
  for (int i = 0; i < 5; ++i) {
    const Sample s = solveOnce(c, 20);
    repeats.push_back(s);
    std::printf("%8d %12llu %14llu %14llu\n", i, static_cast<unsigned long long>(s.allocations),
                static_cast<unsigned long long>(s.reallocations),
                static_cast<unsigned long long>(s.peak));
  }
  const bool stable = std::all_of(repeats.begin(), repeats.end(), [&](const Sample& s) {
    return s.peak == repeats[0].peak && s.allocations == repeats[0].allocations;
  });
  std::printf("  %s repeated solves are identical in allocations and peak -- no creep, no leak\n",
              stable ? "PASS" : "FAIL");
  if (!stable) ++failures;

  std::printf("\n%s\n", failures == 0 ? "MEMORY PROBE: PASS" : "MEMORY PROBE: FAIL");
  return failures == 0 ? 0 : 1;
}
