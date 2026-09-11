#include "cfd/gpu/GpuLinearSolver.hpp"

// Compiled into cfdcore only when CFDAPP_ENABLE_CUDA is OFF (see
// src/CMakeLists.txt) -- the real implementation lives in
// cuda/kernels/GpuLinearSolverCuda.cpp instead, compiled into the
// cfdcuda target and linked into cfdcore in that configuration. Exactly
// one of the two is ever compiled, so there is no ODR conflict (same
// split as src/gpu/GPUBackend.cpp's own header comment).
namespace cfd::gpu {

std::unique_ptr<cfd::algebra::LinearSolver> makeGpuCG(
    cfd::algebra::LinearSolverSettings, std::shared_ptr<cfd::algebra::Preconditioner>) {
  return nullptr;
}

std::unique_ptr<cfd::algebra::LinearSolver> makeGpuBiCGSTAB(
    cfd::algebra::LinearSolverSettings, std::shared_ptr<cfd::algebra::Preconditioner>) {
  return nullptr;
}

}  // namespace cfd::gpu
