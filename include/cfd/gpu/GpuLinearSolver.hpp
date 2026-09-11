#pragma once

// P6-GPU-002 -- Performance: CUDA-independent factory declarations for
// the GPU-backed Krylov solvers -- deliberately does not declare (or even
// name) the concrete GpuCG/GpuBiCGSTAB classes, only functions that
// return the existing CUDA-independent cfd::algebra::LinearSolver base
// pointer. This lets cfd::algebra::LinearSolverFactory.cpp (always
// compiled, no CUDA dependency) call these unconditionally, the same way
// SIMPLE.cpp already calls cfd::gpu::cudaAvailable() -- see
// GPUBackend.hpp's own header comment for why this codebase gates GPU
// code at the target level rather than #ifdef, and
// GpuResidencyManager.hpp for the identical CPU-stub/CUDA-impl ODR split
// this file follows: this header and src/gpu/GpuLinearSolver.cpp (the
// CPU stub -- both factories always return nullptr) are compiled
// together only when CFDAPP_ENABLE_CUDA is OFF;
// cuda/kernels/GpuLinearSolverCuda.cpp (the real implementation, defining
// the concrete classes privately in its own translation unit) is
// compiled instead, into the cfdcuda target, only when
// CFDAPP_ENABLE_CUDA=ON.
#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/Preconditioner.hpp"

namespace cfd::gpu {

// Returns nullptr iff GPU execution is not currently usable (CPU-only
// build, or a CUDA build with no usable device at runtime --
// cfd::gpu::cudaAvailable() decides) -- never throws, and never returns a
// solver that would then fail every call. A non-null return is a fully
// working, persistent-GPU-resident cfd::algebra::LinearSolver: its own
// solve() still honors `settings`'s tolerances/maxIterations exactly like
// the CPU CG/BiCGSTAB classes do, and repeated solve() calls on the same
// instance reuse the device-resident matrix structure/vectors from the
// previous call (P6-GPU-001's persistent pipeline) rather than
// reallocating/re-uploading from scratch. `preconditioner` may be
// nullptr (no preconditioning, still SIMPLE's own default -- see
// SIMPLE.cpp); if non-null, it is applied via a per-iteration
// device<->host round trip of just that one vector (not the whole
// iterative state) -- a general-purpose escape hatch for a caller-
// supplied preconditioner this codebase has no GPU-resident
// implementation of. `settings.preconditioner ==
// PreconditionerType::Jacobi` (with `preconditioner` left null) instead
// takes P6-GPU-003's GPU-resident fast path -- diag(A)^-1 built and
// applied entirely on the device, no per-iteration round trip at all --
// see cuda/kernels/GpuPreconditionerKernel.cu and this file's own
// GpuLinearSolverCuda.cpp implementation for the details.
[[nodiscard]] std::unique_ptr<cfd::algebra::LinearSolver> makeGpuCG(
    cfd::algebra::LinearSolverSettings settings,
    std::shared_ptr<cfd::algebra::Preconditioner> preconditioner = nullptr);

[[nodiscard]] std::unique_ptr<cfd::algebra::LinearSolver> makeGpuBiCGSTAB(
    cfd::algebra::LinearSolverSettings settings,
    std::shared_ptr<cfd::algebra::Preconditioner> preconditioner = nullptr);

}  // namespace cfd::gpu
