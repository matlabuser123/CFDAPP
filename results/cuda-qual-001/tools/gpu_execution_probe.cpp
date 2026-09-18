// CUDA-QUAL-001 steps 6-8: does CFDApp's own production GPU path really execute on the GPU,
// does it agree with the CPU reference, and is it deterministic?
//
// Uses only public CFDApp API plus cudaGetDeviceProperties for device identity. Every phase
// prints the project's own GPUExecutionStats, so a silent CPU fallback is visible as
// kernelLaunches == 0 / gpuBackendFallbacks > 0 rather than being hidden by a passing number.
//
// Run twice:
//   * normally                      -> GPU must be used (kernel launches > 0, 0 fallbacks)
//   * with CUDA_VISIBLE_DEVICES=""  -> the negative control: no device, so the same binary must
//                                      report cudaAvailable() false and launch no kernels
#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/gpu/CudaSpmv.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::CG;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::gpu::gpuExecutionStats;
using cfd::gpu::resetGpuExecutionStats;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("  %-58s %s\n", what.c_str(), ok ? "PASS" : "FAIL");
  if (!ok) ++failures;
}

// A 2D 5-point Laplacian: the sparsity the pressure/momentum assembly produces.
SparseMatrix gridMatrix(Index nx, Index ny) {
  const Index n = nx * ny;
  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  const auto id = [&](Index i, Index j) { return j * nx + i; };
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p = id(i, j);
      Real diagonal = 0.0;
      if (i > 0) { builder.add(p, id(i - 1, j), -1.0); diagonal += 1.0; }
      if (i + 1 < nx) { builder.add(p, id(i + 1, j), -1.0); diagonal += 1.0; }
      if (j > 0) { builder.add(p, id(i, j - 1), -1.0); diagonal += 1.0; }
      if (j + 1 < ny) { builder.add(p, id(i, j + 1), -1.0); diagonal += 1.0; }
      builder.add(p, p, diagonal + 0.1);
    }
  }
  return builder.build();
}

Vector ramp(Index n, Real scale) {
  Vector v(n);
  for (Index i = 0; i < n; ++i) v[i] = scale * std::sin(0.37 * static_cast<Real>(i) + 0.11);
  return v;
}

struct Diff {
  Real absolute{0.0};
  Real relative{0.0};
  Index nans{0};
  Index infs{0};
  Index bitwiseDifferences{0};
};

Diff compare(const Vector& a, const Vector& b) {
  Diff d;
  Real scale = 0.0;
  for (Index i = 0; i < a.size(); ++i) {
    if (std::isnan(b[i])) ++d.nans;
    if (std::isinf(b[i])) ++d.infs;
    const Real e = std::abs(a[i] - b[i]);
    if (a[i] != b[i]) ++d.bitwiseDifferences;
    if (e > d.absolute) d.absolute = e;
    scale = std::max(scale, std::abs(a[i]));
  }
  d.relative = scale > 0.0 ? d.absolute / scale : d.absolute;
  return d;
}

void printStats(const char* phase) {
  const auto& s = gpuExecutionStats();
  std::printf("    stats[%s] kernelLaunches %llu, fallbacks %llu, H2D %llu calls/%llu B, "
              "D2H %llu calls/%llu B, allocations %llu, syncs %llu, kernelSeconds %.6f\n",
              phase, static_cast<unsigned long long>(s.kernelLaunches),
              static_cast<unsigned long long>(s.gpuBackendFallbacks),
              static_cast<unsigned long long>(s.hostToDeviceCalls),
              static_cast<unsigned long long>(s.hostToDeviceBytes),
              static_cast<unsigned long long>(s.deviceToHostCalls),
              static_cast<unsigned long long>(s.deviceToHostBytes),
              static_cast<unsigned long long>(s.allocations),
              static_cast<unsigned long long>(s.synchronizations), s.kernelSeconds);
}

}  // namespace

int main(int argc, char** argv) {
  const bool negativeControl = argc > 1 && std::string(argv[1]) == "--negative-control";
  std::printf("# CUDA-QUAL-001 GPU execution probe%s\n",
              negativeControl ? " (NEGATIVE CONTROL: no visible device expected)" : "");

  std::printf("## device as the CUDA runtime sees it\n");
  int count = 0;
  const cudaError_t countStatus = cudaGetDeviceCount(&count);
  std::printf("  cudaGetDeviceCount -> %s, count %d\n", cudaGetErrorString(countStatus), count);
  if (count > 0) {
    cudaDeviceProp p{};
    if (cudaGetDeviceProperties(&p, 0) == cudaSuccess) {
      std::printf("  device 0: %s, compute capability %d.%d, %.0f MiB, uuid ", p.name, p.major,
                  p.minor, static_cast<double>(p.totalGlobalMem) / 1048576.0);
      for (int i = 0; i < 16; ++i) std::printf("%02x", static_cast<unsigned char>(p.uuid.bytes[i]));
      std::printf("\n");
    }
  }

  const bool available = cfd::gpu::cudaAvailable();
  std::printf("## cfd::gpu::cudaAvailable() -> %s\n", available ? "true" : "false");

  if (negativeControl) {
    // The control's whole point: with no visible device the same binary must refuse the GPU
    // path and launch nothing, so the positive run's kernel launches cannot be a CPU fallback
    // wearing a GPU label.
    resetGpuExecutionStats();
    check(!available, "cudaAvailable() is false with no visible device");
    const auto solver = cfd::gpu::makeGpuBiCGSTAB(LinearSolverSettings{}, nullptr);
    check(solver == nullptr, "makeGpuBiCGSTAB() returns nullptr with no visible device");
    printStats("negative-control");
    check(gpuExecutionStats().kernelLaunches == 0, "no kernels were launched");
    std::printf("PROBE %s (failures %d)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
  }

  check(available, "cudaAvailable() is true");
  if (!available) {
    std::printf("PROBE FAIL (no GPU; nothing further can be qualified)\n");
    return 1;
  }

  // ---- step 6/7: SpMV, CFDApp's GPU kernel against its own CPU reference -------------------
  std::printf("## SpMV: cfd::gpu::csrSpmvCuda vs SparseMatrix::multiply\n");
  for (const auto [nx, ny] : {std::pair<Index, Index>{4, 4}, std::pair<Index, Index>{64, 64},
                              std::pair<Index, Index>{256, 256}}) {
    const SparseMatrix a = gridMatrix(nx, ny);
    const Vector x = ramp(a.rows(), 1.0);
    resetGpuExecutionStats();
    const Vector gpu = cfd::gpu::csrSpmvCuda(a, x);
    const auto launches = gpuExecutionStats().kernelLaunches;
    const Vector cpu = a.multiply(x);
    const Diff d = compare(cpu, gpu);
    std::printf("  %4lldx%-4lld n %7lld | abs %.3e rel %.3e | NaN %lld Inf %lld | launches %llu\n",
                static_cast<long long>(nx), static_cast<long long>(ny),
                static_cast<long long>(a.rows()), d.absolute, d.relative,
                static_cast<long long>(d.nans), static_cast<long long>(d.infs),
                static_cast<unsigned long long>(launches));
    check(d.nans == 0 && d.infs == 0, "SpMV result has no NaN or Inf");
    check(d.relative <= 1e-12, "SpMV matches the CPU reference within 1e-12 relative");
    check(launches > 0, "SpMV launched at least one CUDA kernel");
  }

  // ---- step 8: determinism of the GPU SpMV --------------------------------------------------
  std::printf("## determinism: 5 repeats of the same GPU SpMV\n");
  {
    const SparseMatrix a = gridMatrix(128, 128);
    const Vector x = ramp(a.rows(), 1.0);
    const Vector first = cfd::gpu::csrSpmvCuda(a, x);
    Index differing = 0;
    for (int r = 0; r < 4; ++r) {
      const Vector again = cfd::gpu::csrSpmvCuda(a, x);
      differing += compare(first, again).bitwiseDifferences;
    }
    std::printf("  bitwise differing entries across repeats: %lld of %lld\n",
                static_cast<long long>(differing), static_cast<long long>(4 * first.size()));
    check(differing == 0, "repeated GPU SpMV is bitwise identical");
  }

  // ---- step 6/7/8: the GPU Krylov solvers against the CPU solvers ---------------------------
  // Optional argument: the grid edge length, so the same comparison can be run at the size where
  // the end-to-end benchmark reports PressureCorrectionFailure (320) as well as the default.
  const Index edge = argc > 1 ? static_cast<Index>(std::atoi(argv[1])) : 96;
  std::printf("## linear solves: GPU vs CPU, same system and settings (grid %lldx%lld)\n",
              static_cast<long long>(edge), static_cast<long long>(edge));
  {
    const SparseMatrix a = gridMatrix(edge, edge);
    const Vector b = ramp(a.rows(), 1.0);
    const LinearSystem system(a, b);
    LinearSolverSettings settings;
    settings.absoluteTolerance = 1e-12;
    settings.relativeTolerance = 1e-10;
    settings.maxIterations = 5000;

    const CG cpuCg(settings);
    const auto cpuCgResult = cpuCg.solve(system);
    resetGpuExecutionStats();
    const auto gpuCg = cfd::gpu::makeGpuCG(settings, nullptr);
    check(gpuCg != nullptr, "makeGpuCG() returns a solver when a GPU is present");
    if (gpuCg) {
      const auto gpuCgResult = gpuCg->solve(system);
      const Diff d = compare(cpuCgResult.solution, gpuCgResult.solution);
      printStats("gpu-cg");
      std::printf("  CG  | CPU it %lld res %.3e | GPU it %lld res %.3e | abs %.3e rel %.3e\n",
                  static_cast<long long>(cpuCgResult.iterations), cpuCgResult.finalResidual,
                  static_cast<long long>(gpuCgResult.iterations), gpuCgResult.finalResidual,
                  d.absolute, d.relative);
      check(gpuCgResult.converged(), "GPU CG converged");
      check(d.nans == 0 && d.infs == 0, "GPU CG solution has no NaN or Inf");
      check(d.relative <= 1e-8, "GPU CG solution matches CPU CG within 1e-8 relative");
      check(gpuExecutionStats().kernelLaunches > 0, "GPU CG launched CUDA kernels");
      check(gpuExecutionStats().gpuBackendFallbacks == 0, "GPU CG did not fall back to the CPU");
    }

    const BiCGSTAB cpuBi(settings);
    const auto cpuBiResult = cpuBi.solve(system);
    resetGpuExecutionStats();
    const auto gpuBi = cfd::gpu::makeGpuBiCGSTAB(settings, nullptr);
    check(gpuBi != nullptr, "makeGpuBiCGSTAB() returns a solver when a GPU is present");
    if (gpuBi) {
      const auto gpuBiResult = gpuBi->solve(system);
      const Diff d = compare(cpuBiResult.solution, gpuBiResult.solution);
      printStats("gpu-bicgstab");
      std::printf("  BiC | CPU it %lld res %.3e | GPU it %lld res %.3e | abs %.3e rel %.3e\n",
                  static_cast<long long>(cpuBiResult.iterations), cpuBiResult.finalResidual,
                  static_cast<long long>(gpuBiResult.iterations), gpuBiResult.finalResidual,
                  d.absolute, d.relative);
      check(gpuBiResult.converged(), "GPU BiCGSTAB converged");
      check(d.nans == 0 && d.infs == 0, "GPU BiCGSTAB solution has no NaN or Inf");
      check(d.relative <= 1e-8, "GPU BiCGSTAB solution matches CPU BiCGSTAB within 1e-8 relative");
      check(gpuExecutionStats().kernelLaunches > 0, "GPU BiCGSTAB launched CUDA kernels");
      check(gpuExecutionStats().gpuBackendFallbacks == 0, "GPU BiCGSTAB did not fall back");

      // determinism of the whole solve
      const auto again = gpuBi->solve(system);
      const Diff repeat = compare(gpuBiResult.solution, again.solution);
      std::printf("  BiC repeat: bitwise differing entries %lld of %lld, abs %.3e\n",
                  static_cast<long long>(repeat.bitwiseDifferences),
                  static_cast<long long>(again.solution.size()), repeat.absolute);
      check(repeat.bitwiseDifferences == 0, "repeated GPU BiCGSTAB solve is bitwise identical");
    }
  }

  std::printf("PROBE %s (failures %d)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
