// GPU-PCORR-001 step 5: is the GPU BiCGSTAB breakdown test scale-dependent?
//
// The same linear system is solved with the RHS scaled by successively smaller factors. A
// scale-relative breakdown criterion is invariant under (A, b) -> (A, beta b): the iteration
// count and the outcome must not change. An absolute criterion (|rho| < 1e-30) must eventually
// declare a healthy iteration a breakdown, because rho scales with the residual.
//
// usage: scale_breakdown [edge]
#include <cstdio>
#include <cstdlib>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::PreconditionerType;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

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
      builder.add(p, p, diagonal + 1e-3);
    }
  }
  return builder.build();
}

const char* statusName(SolverStatus s) {
  switch (s) {
    case SolverStatus::Converged: return "Converged";
    case SolverStatus::MaxIterations: return "MaxIterations";
    case SolverStatus::Breakdown: return "Breakdown";
    case SolverStatus::NonFiniteResidual: return "NonFiniteResidual";
    case SolverStatus::NonFiniteInput: return "NonFiniteInput";
    default: return "other";
  }
}

}  // namespace

int main(int argc, char** argv) {
  const Index edge = argc > 1 ? static_cast<Index>(std::atoi(argv[1])) : 320;
  const SparseMatrix a = gridMatrix(edge, edge);
  const Index n = a.rows();
  std::printf("# same matrix (%lldx%lld, n=%lld), RHS scaled; BiCGSTAB, no preconditioner,\n",
              static_cast<long long>(edge), static_cast<long long>(edge),
              static_cast<long long>(n));
  std::printf("# relative tolerance only, so the scale factor cannot change the work required.\n");
  std::printf("%-10s | %-28s | %-28s\n", "b scale", "CPU BiCGSTAB", "GPU BiCGSTAB");

  for (const Real scale : {1.0, 1e-6, 1e-12, 1e-16, 1e-20}) {
    Vector b(n);
    for (Index i = 0; i < n; ++i) b[i] = scale * ((i % 7) - 3.0);
    const LinearSystem system(a, b);
    LinearSolverSettings settings;
    settings.absoluteTolerance = 0.0;  // relative only: identical work at every scale
    settings.relativeTolerance = 1e-8;
    settings.maxIterations = 5000;
    settings.preconditioner = PreconditionerType::None;

    const BiCGSTAB cpu(settings);
    const auto cpuResult = cpu.solve(system);
    char cpuText[64];
    std::snprintf(cpuText, sizeof cpuText, "%-13s it %4lld", statusName(cpuResult.status),
                  static_cast<long long>(cpuResult.iterations));

    char gpuText[64] = "no GPU";
    const auto gpu = cfd::gpu::makeGpuBiCGSTAB(settings, nullptr);
    if (gpu) {
      const auto gpuResult = gpu->solve(system);
      std::snprintf(gpuText, sizeof gpuText, "%-13s it %4lld", statusName(gpuResult.status),
                    static_cast<long long>(gpuResult.iterations));
    }
    std::printf("%-10.0e | %-28s | %-28s\n", scale, cpuText, gpuText);
  }
  return 0;
}
