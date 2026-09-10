#pragma once

namespace cfd::gpu {

// P4 -- Performance, section 30: a single, truthful capability check --
// true iff this binary was built with CUDA support (CFDAPP_ENABLE_CUDA)
// AND the CUDA runtime reports at least one usable device right now.
// Never "claims GPU execution if the CPU path actually ran" (section 30's
// own explicit requirement): a CPU-only build always returns false here
// (src/gpu/GPUBackend.cpp's stub implementation, compiled in exactly
// when the real one below is not); a CUDA-enabled build queries the
// actual runtime (cuda/kernels/CsrSpmvKernel.cu's implementation) rather
// than assuming compiled-in means available (a machine with no GPU, or a
// driver mismatch, still reports false).
[[nodiscard]] bool cudaAvailable();

}  // namespace cfd::gpu
