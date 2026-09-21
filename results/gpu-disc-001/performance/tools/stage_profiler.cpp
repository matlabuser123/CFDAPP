// GPU-DISC-001Q -- DEVICE time per GPU-DISC stage.
//
// Why this exists instead of an Nsight Systems report: Nsight's CUDA kernel
// tracing needs CUPTI, and the CUDA 12.9 install in this WSL2 environment is
// the minimal one -- `extras/CUPTI` is absent. The capture runs, the injection
// library loads, and the report comes back with no kernel rows. That is an
// environment limitation, recorded in profiling/ with the evidence rather than
// worked around by reporting host-side numbers as if they were device time.
//
// The brief permits "existing CUDA event instrumentation" as an alternative,
// and this is the sync-based equivalent: each production stage is driven
// directly through the SAME facade SIMPLE uses, with cudaDeviceSynchronize()
// around it, so the measured interval contains the stage's device work.
//
// THE SYNCHRONIZATION IS IN THIS HARNESS, NOT IN PRODUCTION. SIMPLE::solve()
// still adds none; these numbers are a profiling instrument, deliberately
// separate, exactly so the production path keeps the async behaviour
// GPU-PIPE-001 Phase 3 built.
//
// What the numbers mean: serialising the stages removes the overlap production
// gets for free, so each stage's figure is its ISOLATED device cost and the sum
// EXCEEDS the real iteration time. That is the right instrument for "which
// stage dominates", and the wrong one for "how long does an iteration take" --
// the benchmark answers that.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::GpuSimpleDiscretization;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

struct Stage {
  const char* name;
  double seconds;
  std::uint64_t kernels;
};

void deviceSync() { cudaDeviceSynchronize(); }

}  // namespace

int main(int argc, char** argv) {
  const Index n = argc > 1 ? std::atoi(argv[1]) : 320;
  const int repeats = argc > 2 ? std::atoi(argv[2]) : 20;

  std::printf("=== GPU-DISC-001Q: device time per production discretization stage ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no usable CUDA device -- exiting\n");
    return 2;
  }
  std::printf("case: %lldx%lld cavity, %d timed repeats of each stage\n",
              static_cast<long long>(n), static_cast<long long>(n), repeats);
  std::printf("NOTE stages are synchronized and therefore SERIALISED here; production\n"
              "     overlaps them, so these are isolated per-stage device costs and their\n"
              "     sum exceeds a real iteration.\n");

  const Mesh mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries, pressureBoundaries;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    const bool lid = i + 1 == mesh.boundaryPatches().size();
    velocityBoundaries.set(
        mesh, patch.name(),
        lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
            : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::Wall>()));
    pressureBoundaries.set(mesh, patch.name(),
                           std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }

  const FluidProperties fluid{1.0, 0.01};
  const Index nc = mesh.numberOfCells();
  VectorField velocity(nc);
  ScalarField pressure(nc);
  ScalarField viscosity(nc);
  for (Index c = 0; c < nc; ++c) viscosity[c] = fluid.dynamicViscosity();
  SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  ScalarField previous(nc);

  GpuSimpleDiscretization gpu;
  std::string reason;
  if (!gpu.prepare(mesh, velocityBoundaries, pressureBoundaries, reason)) {
    std::printf("prepare failed: %s\n", reason.c_str());
    return 2;
  }

  // Warm-up: one full iteration, untimed, so plan construction and first-kernel
  // load do not land in a measured stage.
  gpu.beginIteration(velocity, pressure, massFlux, viscosity);
  (void)gpu.assembleMomentum(0, previous, 0.7, 0, false);
  (void)gpu.assembleMomentum(1, previous, 0.7, 0, false);
  gpu.setMomentumSolution(0, cfd::algebra::Vector(nc, 0.0));
  gpu.setMomentumSolution(1, cfd::algebra::Vector(nc, 0.0));
  gpu.computeResponseCoefficients(false);
  gpu.computePredictedFaceFlux(false, fluid.density(), 0.7, 0, false);
  (void)gpu.assemblePressureCorrection(false, 0, fluid.density(), nullptr, false);
  gpu.setPressureCorrection(cfd::algebra::Vector(nc, 0.0));
  VectorField correctedV;
  SurfaceField correctedF;
  gpu.correctVelocity(0, false, correctedV);
  gpu.correctFaceMassFlux(false, correctedF);
  deviceSync();

  std::vector<Stage> stages;
  const auto time = [&](const char* name, auto&& fn) {
    deviceSync();
    const std::uint64_t k0 = cfd::gpu::gpuExecutionStats().kernelLaunches;
    cfd::Timer t;
    for (int r = 0; r < repeats; ++r) fn();
    deviceSync();
    const std::uint64_t k1 = cfd::gpu::gpuExecutionStats().kernelLaunches;
    stages.push_back(Stage{name, t.elapsedSeconds() / repeats, (k1 - k0) / repeats});
  };

  time("beginIteration (upload)",
       [&] { gpu.beginIteration(velocity, pressure, massFlux, viscosity); });
  time("momentum assembly U+V", [&] {
    (void)gpu.assembleMomentum(0, previous, 0.7, 0, false);
    (void)gpu.assembleMomentum(1, previous, 0.7, 0, false);
  });
  time("setMomentumSolution x2", [&] {
    gpu.setMomentumSolution(0, cfd::algebra::Vector(nc, 0.0));
    gpu.setMomentumSolution(1, cfd::algebra::Vector(nc, 0.0));
  });
  time("response coefficients", [&] { gpu.computeResponseCoefficients(false); });
  time("predicted face flux",
       [&] { gpu.computePredictedFaceFlux(false, fluid.density(), 0.7, 0, false); });
  time("pressure-correction assembly",
       [&] { (void)gpu.assemblePressureCorrection(false, 0, fluid.density(), nullptr, false); });
  time("setPressureCorrection", [&] { gpu.setPressureCorrection(cfd::algebra::Vector(nc, 0.0)); });
  time("velocity correction", [&] { gpu.correctVelocity(0, false, correctedV); });
  time("face-flux correction", [&] { gpu.correctFaceMassFlux(false, correctedF); });

  double total = 0.0;
  for (const Stage& s : stages) total += s.seconds;

  std::printf("\n%-32s %14s %9s %10s\n", "stage", "ms/iteration", "% of sum", "kernels");
  std::vector<Stage> sorted = stages;
  std::sort(sorted.begin(), sorted.end(),
            [](const Stage& a, const Stage& b) { return a.seconds > b.seconds; });
  for (const Stage& s : sorted) {
    std::printf("%-32s %14.4f %8.1f%% %10llu\n", s.name, s.seconds * 1e3,
                100.0 * s.seconds / total, static_cast<unsigned long long>(s.kernels));
  }
  std::printf("%-32s %14.4f %8.1f%%\n", "SUM (serialised)", total * 1e3, 100.0);

  std::printf("\ntop production GPU time consumers, in order:\n");
  for (std::size_t k = 0; k < sorted.size() && k < 3; ++k) {
    std::printf("  %zu. %-30s %.4f ms/iteration (%.1f%%)\n", k + 1, sorted[k].name,
                sorted[k].seconds * 1e3, 100.0 * sorted[k].seconds / total);
  }
  return 0;
}
