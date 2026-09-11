#pragma once

// P6-GPU-003 -- Performance: GPU-resident Jacobi preconditioning for
// GpuCG/GpuBiCGSTAB (cuda/kernels/GpuLinearSolverCuda.cpp). See
// DeviceVectorOps.hpp's own header comment for the "never include from a
// CPU-only-compiled file" rule this file inherits.
//
// This is deliberately NOT an implementation of cfd::algebra::
// Preconditioner (Preconditioner.hpp's CPU-Vector-based interface) --
// that interface's apply(const Vector&, Vector&) contract is host
// memory by construction, so any implementation of it (including the one
// GpuCG/GpuBiCGSTAB already accept as an explicit constructor argument
// for arbitrary caller-supplied preconditioners) necessarily pays a
// device<->host round trip every application. These two free functions
// are the GPU-resident fast path used instead when
// LinearSolverSettings::preconditioner == PreconditionerType::Jacobi and
// no explicit CPU-side Preconditioner was supplied: diag(A)^-1 lives in
// device memory for the lifetime of the solver instance, and applying it
// (M^-1 r) never leaves the device.
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceVector.hpp"

namespace cfd::gpu {

// Extracts diag(A)^-1 on the host -- reusing
// cfd::algebra::computeInverseDiagonal's own validation, so this throws
// InvalidArgumentError on exactly the same missing/zero/near-zero/
// non-finite diagonal cases JacobiPreconditioner::build does, not a
// second independently-maintained policy -- then uploads it once into
// `inverseDiagonal` (resizing/reallocating only if its size actually
// changed; DeviceBuffer::resize()'s own contract makes repeated calls at
// the same size free). Meant to be called once per solve() (matrix
// coefficients can legitimately change every outer SIMPLE iteration --
// see GpuLinearSolverCuda.cpp's own call site), never per Krylov
// iteration. Recorded in GPUExecutionStats::preconditionerSetupSeconds.
void buildJacobiDiagonal(const cfd::algebra::SparseMatrix& matrix, DeviceVector& inverseDiagonal);

// output[i] = inverseDiagonal[i] * input[i] -- M^-1 r for a Jacobi
// preconditioner, entirely on the GPU (no host round trip, unlike
// applying a CPU-side cfd::algebra::Preconditioner). `output` may alias
// `input`. Throws InvalidArgumentError if input/inverseDiagonal sizes
// differ. Recorded in GPUExecutionStats::preconditionerApplySeconds.
void applyJacobiDiagonal(const DeviceVector& inverseDiagonal, const DeviceVector& input,
                         DeviceVector& output);

}  // namespace cfd::gpu
