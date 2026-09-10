#include "cfd/gpu/GPUBackend.hpp"

// Compiled into cfdcore only when CFDAPP_ENABLE_CUDA is OFF (see
// src/CMakeLists.txt) -- the real implementation lives in
// cuda/kernels/CsrSpmvKernel.cu instead, compiled into the cfdcuda
// target and linked into cfdcore in that configuration. Exactly one of
// the two is ever compiled, so there is no ODR conflict.
namespace cfd::gpu {

bool cudaAvailable() { return false; }

}  // namespace cfd::gpu
