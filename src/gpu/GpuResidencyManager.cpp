#include "cfd/gpu/GpuResidencyManager.hpp"

// Compiled into cfdcore only when CFDAPP_ENABLE_CUDA is OFF (see
// src/CMakeLists.txt) -- the real implementation lives in
// cuda/kernels/GpuResidencyManagerCuda.cu instead, compiled into the
// cfdcuda target and linked into cfdcore in that configuration. Exactly
// one of the two is ever compiled, so there is no ODR conflict (same
// split as src/gpu/GPUBackend.cpp's own header comment).
namespace cfd::gpu {

struct GpuResidencyManager::Impl {};

GpuResidencyManager::GpuResidencyManager() = default;
GpuResidencyManager::~GpuResidencyManager() = default;
GpuResidencyManager::GpuResidencyManager(GpuResidencyManager&&) noexcept = default;
GpuResidencyManager& GpuResidencyManager::operator=(GpuResidencyManager&&) noexcept = default;

bool GpuResidencyManager::active() const noexcept { return false; }

void GpuResidencyManager::syncMatrix(const std::string&, const cfd::algebra::SparseMatrix&) {}

void GpuResidencyManager::syncField(const std::string&, const cfd::fields::Field<cfd::Real>&) {}

}  // namespace cfd::gpu
