// P6-GPU-001 -- Performance: the real, CUDA-backed implementation of
// cfd::gpu::GpuResidencyManager (see include/cfd/gpu/
// GpuResidencyManager.hpp's own header comment for the ODR split with
// src/gpu/GpuResidencyManager.cpp's CPU stub). Pure host-side CUDA
// Runtime API usage -- no device kernel of its own -- deliberately a
// plain .cpp (compiled by the host compiler, not nvcc) rather than a
// .cu file like CsrSpmvKernel.cu: <cuda_runtime.h> is designed to be
// includable from ordinary host translation units (no __global__/<<<>>>
// syntax appears anywhere in this file or in the DeviceBuffer/
// DeviceCsrMatrix/DeviceField headers it pulls in), and this project's
// own nvcc 11.5 cannot parse this GCC 11 toolchain's <functional> (a
// documented nvcc/libstdc++ incompatibility -- nvcc 11.5 only officially
// supports up to GCC 10) -- pulled in transitively the moment this file
// needs <unordered_map> for its per-key matrix/field maps, something
// CsrSpmvKernel.cu's narrower include set never triggers. Still part of
// the cfdcuda target (cuda/CMakeLists.txt) so it shares that target's
// include paths and CUDA::cudart link, but CMake compiles a .cpp source
// inside a CUDA-language target with the host compiler by default -- no
// separate target or language override needed.
#include <unordered_map>

#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuResidencyManager.hpp"

namespace cfd::gpu {

struct GpuResidencyManager::Impl {
  // Captured once at construction, not re-queried per call -- matches
  // cfd::gpu::cudaAvailable()'s own "single truthful capability check"
  // contract (GPUBackend.hpp's own header comment) and means a device
  // that disappears mid-solve fails loudly (the next real CUDA call
  // throws cfd::NumericalError via checkCuda) rather than this class
  // silently flipping to a different behavior mid-lifetime.
  bool active{cudaAvailable()};
  std::unordered_map<std::string, DeviceCsrMatrix> matrices;
  std::unordered_map<std::string, DeviceField> fields;
};

GpuResidencyManager::GpuResidencyManager() : impl_(std::make_unique<Impl>()) {}
GpuResidencyManager::~GpuResidencyManager() = default;
GpuResidencyManager::GpuResidencyManager(GpuResidencyManager&&) noexcept = default;
GpuResidencyManager& GpuResidencyManager::operator=(GpuResidencyManager&&) noexcept = default;

bool GpuResidencyManager::active() const noexcept { return impl_->active; }

void GpuResidencyManager::syncMatrix(const std::string& key,
                                     const cfd::algebra::SparseMatrix& matrix) {
  if (!impl_->active) return;
  auto [it, inserted] = impl_->matrices.try_emplace(key);
  DeviceCsrMatrix& device = it->second;
  // A structure-changed matrix (dimensions or nonzero count differ from
  // what is resident) must go through a full re-upload -- DeviceCsrMatrix
  // ::updateValues() itself throws rather than reinterpret mismatched
  // values against a stale structure (see its own header comment), so
  // this check exists precisely to route that case to
  // uploadStructureAndValues() instead of letting the exception escape a
  // production solve.
  if (inserted || !device.hasStructure() || device.rows() != matrix.rows() ||
      device.columns() != matrix.columns() || device.nonZeros() != matrix.nonZeros()) {
    device.uploadStructureAndValues(matrix);
  } else {
    device.updateValues(matrix);
  }
}

void GpuResidencyManager::syncField(const std::string& key,
                                    const cfd::fields::Field<cfd::Real>& field) {
  if (!impl_->active) return;
  DeviceField& device = impl_->fields[key];
  device.syncToDevice(field);
}

}  // namespace cfd::gpu
