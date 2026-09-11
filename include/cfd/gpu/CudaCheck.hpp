#pragma once

// P6-PERF-001 -- Performance: the one shared "throw on CUDA API failure"
// helper, lifted out of what used to be a private anonymous-namespace
// copy in cuda/kernels/CsrSpmvKernel.cu so DeviceBuffer/DeviceVector/
// DeviceCsrMatrix/DeviceField and the kernel file all check errors the
// same way instead of each keeping their own copy. Only ever included
// from files compiled with a CUDA-aware compiler (nvcc, or a host
// compiler translation unit that is part of the cfdcuda target/a
// CFDAPP_ENABLE_CUDA-gated test or benchmark target) -- it pulls in
// <cuda_runtime.h>, so it must never be included from a file that also
// builds in a CPU-only configuration.
#include <cuda_runtime.h>

#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::gpu {

// Throws cfd::NumericalError (mirrors this codebase's "invalid states
// should expose numerical defects rather than being hidden") if `status`
// is not cudaSuccess. `what` names the failing operation for the message.
inline void checkCuda(cudaError_t status, const char* what) {
  if (status != cudaSuccess) {
    throw cfd::NumericalError(std::string(what) + " failed: " + cudaGetErrorString(status));
  }
}

}  // namespace cfd::gpu
