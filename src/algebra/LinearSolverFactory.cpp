#include "cfd/algebra/LinearSolverFactory.hpp"

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/core/Logger.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"

namespace cfd::algebra {

std::unique_ptr<LinearSolver> makeLinearSolver(LinearSolverSettings settings,
                                               std::shared_ptr<Preconditioner> preconditioner) {
  if (settings.backend == LinearSolverBackend::GPU) {
    std::unique_ptr<LinearSolver> gpuSolver =
        (settings.type == LinearSolverType::CG)
            ? cfd::gpu::makeGpuCG(settings, preconditioner)
            : cfd::gpu::makeGpuBiCGSTAB(settings, preconditioner);
    if (gpuSolver != nullptr) {
      return gpuSolver;
    }
    // Deterministic, logged, counted fallback -- see this function's own
    // header comment. Never silent: a caller relying on GPU execution
    // can check gpuExecutionStats().gpuBackendFallbacks or the log.
    ++cfd::gpu::gpuExecutionStats().gpuBackendFallbacks;
    cfd::Logger::instance().warning(
        "makeLinearSolver: GPU backend requested but unavailable (no CUDA support in this build, "
        "or no usable device at runtime) -- falling back to the CPU-equivalent solver");
  }

  // P6-GPU-003 -- Performance: only reached for an actual CPU-backend
  // solve (backend == CPU directly, or a GPU request that just fell back
  // above) -- a real GPU solve never reaches here at all (it already
  // returned above), so this never constructs a CPU-side
  // JacobiPreconditioner that GPU execution would then have to pay a
  // host<->device round trip to use. GpuCG/GpuBiCGSTAB
  // (cuda/kernels/GpuLinearSolverCuda.cpp) instead read
  // `settings.preconditioner` themselves and build their own
  // GPU-resident diagonal when it requests Jacobi -- see that file's own
  // header comment. An explicit `preconditioner` argument from the
  // caller always wins over this settings-driven default, matching this
  // function's own pre-existing "explicit argument is an escape hatch"
  // contract.
  if (preconditioner == nullptr && settings.preconditioner == PreconditionerType::Jacobi) {
    preconditioner = std::make_shared<JacobiPreconditioner>();
  }

  if (settings.type == LinearSolverType::CG) {
    return std::make_unique<CG>(settings, std::move(preconditioner));
  }
  return std::make_unique<BiCGSTAB>(settings, std::move(preconditioner));
}

}  // namespace cfd::algebra
