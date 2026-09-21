// GPU-DISC-001B -- gradient operator benchmark.
//
// This measures ONE OPERATOR, not a solve. It is not evidence about end-to-end
// SIMPLE performance and is not presented as such: the pressure/momentum path
// still runs on the CPU, so nothing here says the application got faster.
//
// What is timed:
//   CPU   cfd::discretization::greenGaussGradient, the production call.
//   GPU   greenGaussGradientDevice with the field ALREADY device-resident and
//         the result LEFT device-resident, followed by cudaDeviceSynchronize
//         so the number is device execution time rather than launch time.
//
// The plan build and the field upload are reported separately rather than
// folded in. They are one-time per mesh and per field respectively, and hiding
// a per-solve cost inside a per-iteration number is how a GPU path comes to
// look faster than it is.
//
// No minimum speedup is required by this phase. The number is reported
// whatever it turns out to be.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::gpu::DeviceGradientPlan;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr int kRepeats = 20;

BoundaryConditionSet mixedSet(const Mesh& mesh) {
  BoundaryConditionSet set;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (i % 2 == 0) {
      set.set(mesh, patch.name(), std::make_unique<FixedValue>(1.0 + (0.5 * static_cast<Real>(i))));
    } else {
      set.set(mesh, patch.name(),
              std::make_unique<FixedGradient>(0.25 - (0.1 * static_cast<Real>(i))));
    }
    ++i;
  }
  return set;
}

void benchmark(Index n) {
  const Mesh mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  const BoundaryConditionSet boundaries = mixedSet(mesh);
  ScalarField phi(mesh.numberOfCells());
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    phi[c] = (x.x * x.x) + (2.0 * x.y * x.y);
  }

  cfd::Timer planTimer;
  DeviceGradientPlan plan;
  const bool built = plan.build(mesh, boundaries);
  const double planSeconds = planTimer.elapsedSeconds();
  if (!built) {
    std::printf("%5zu^2  plan unusable: %s\n", static_cast<std::size_t>(n),
                plan.unsupportedReason().c_str());
    return;
  }

  cfd::gpu::DeviceBuffer<Real> devicePhi;
  cfd::Timer uploadTimer;
  devicePhi.uploadFrom(phi.data(), static_cast<Index>(phi.size()));
  cudaDeviceSynchronize();
  const double uploadSeconds = uploadTimer.elapsedSeconds();

  cfd::gpu::DeviceBuffer<Real> gx, gy, gz;
  // Warm-up: first launch pays module load and allocation.
  cfd::gpu::greenGaussGradientDevice(plan, devicePhi,
                                     cfd::discretization::kGreenGaussSkewCorrectionSweeps, gx, gy,
                                     gz);
  cudaDeviceSynchronize();
  const VectorField reference = cfd::discretization::greenGaussGradient(
      mesh, phi, boundaries, cfd::discretization::kGreenGaussSkewCorrectionSweeps);

  // A fast wrong answer is worth nothing. The differential gate runs at small
  // sizes; this confirms the same bitwise agreement holds at benchmark sizes,
  // on the very arrays being timed.
  std::vector<Real> hx(mesh.numberOfCells()), hy(mesh.numberOfCells()), hz(mesh.numberOfCells());
  gx.downloadTo(hx.data(), gx.size());
  gy.downloadTo(hy.data(), gy.size());
  gz.downloadTo(hz.data(), gz.size());
  std::size_t differing = 0;
  for (std::size_t c = 0; c < reference.size(); ++c) {
    const Real want[3] = {reference[c].x, reference[c].y, reference[c].z};
    const Real got[3] = {hx[c], hy[c], hz[c]};
    for (int k = 0; k < 3; ++k) {
      if (std::memcmp(&want[k], &got[k], sizeof(Real)) != 0) ++differing;
    }
  }

  cfd::Timer gpuTimer;
  for (int r = 0; r < kRepeats; ++r) {
    cfd::gpu::greenGaussGradientDevice(plan, devicePhi,
                                       cfd::discretization::kGreenGaussSkewCorrectionSweeps, gx, gy,
                                       gz);
  }
  cudaDeviceSynchronize();
  const double gpuSeconds = gpuTimer.elapsedSeconds() / kRepeats;

  cfd::Timer cpuTimer;
  for (int r = 0; r < kRepeats; ++r) {
    const VectorField g = cfd::discretization::greenGaussGradient(
        mesh, phi, boundaries, cfd::discretization::kGreenGaussSkewCorrectionSweeps);
    if (g.size() == 0) std::printf("unreachable\n");
  }
  const double cpuSeconds = cpuTimer.elapsedSeconds() / kRepeats;

  std::printf("%5zu^2  cells=%-8zu cpu=%9.3f ms  gpu=%9.3f ms  speedup=%6.2fx   "
              "plan=%8.3f ms (once)  upload=%7.3f ms (once)  resident=%8.3f MB  bitwise=%s\n",
              static_cast<std::size_t>(n), mesh.numberOfCells(), cpuSeconds * 1e3,
              gpuSeconds * 1e3, cpuSeconds / gpuSeconds, planSeconds * 1e3, uploadSeconds * 1e3,
              static_cast<double>(plan.residentBytes()) / (1024.0 * 1024.0),
              differing == 0 ? "yes" : "NO");
}

}  // namespace

int main() {
  // Create the CUDA context before anything is timed, so the first mesh's plan
  // build is not charged ~2 s of one-time driver initialization.
  cudaFree(nullptr);
  std::printf("=== GPU-DISC-001B gradient operator benchmark ===\n");
  std::printf("Green-Gauss gradient of one scalar field, %d repeats, field device-resident,\n",
              kRepeats);
  std::printf("result left device-resident. Operator only -- NOT an end-to-end solve number.\n\n");
  for (const Index n : {160u, 320u, 640u}) benchmark(static_cast<Index>(n));
  return 0;
}
