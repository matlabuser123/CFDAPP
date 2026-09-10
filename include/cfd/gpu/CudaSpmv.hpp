#pragma once

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

namespace cfd::gpu {

// P4 -- Performance, sections 32-33: the first (and, for this task, only
// -- section 32's own "do not begin by moving the full SIMPLE/PISO
// algorithm to CUDA") GPU primitive: CSR sparse matrix-vector
// multiplication, the exact same operation
// cfd::algebra::SparseMatrix::multiply already computes on the CPU (this
// is deliberately the GPU analogue of that one function, not a new
// algorithm -- correctness is judged by direct comparison against it,
// see test_cuda_spmv.cpp).
//
// Declared here unconditionally so the header is always includable, but
// only ever *defined* (in cuda/kernels/CsrSpmvKernel.cu, part of the
// cfdcuda target) when CFDAPP_ENABLE_CUDA=ON -- a CPU-only build must
// never call this (link error if it tries to; every call site in this
// codebase's own tests/benchmarks is itself guarded by
// `#ifdef CFDAPP_ENABLE_CUDA`, section 29's "CUDA must remain optional").
// Callers should check cfd::gpu::cudaAvailable() first; behavior if the
// CUDA runtime has no usable device is a thrown cfd::NumericalError
// (mirrors this codebase's own "invalid states should expose numerical
// defects rather than being hidden", section 56), not a silent CPU
// fallback (that would defeat the point of measuring GPU performance).
//
// Throws InvalidArgumentError if x.size() != matrix.columns(). Throws
// NumericalError if no usable CUDA device is available, or a CUDA API
// call fails.
[[nodiscard]] cfd::algebra::Vector csrSpmvCuda(const cfd::algebra::SparseMatrix& matrix,
                                               const cfd::algebra::Vector& x);

}  // namespace cfd::gpu
