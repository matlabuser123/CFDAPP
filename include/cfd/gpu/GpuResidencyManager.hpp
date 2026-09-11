#pragma once

#include <memory>
#include <string>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/Field.hpp"

namespace cfd::gpu {

// P6-GPU-001 -- Performance: the one production-facing entry point that
// connects the persistent-residency primitives (DeviceCsrMatrix/
// DeviceField, both CUDA-only, see their own header comments) to the
// real solver loop -- cfd::pressure_velocity::SIMPLE::solve(), gated by
// SIMPLESettings::enableGpuResidency -- without pulling <cuda_runtime.h>
// into SIMPLE.cpp, a translation unit that must also build in a
// CPU-only configuration. Same ODR split as GPUBackend.hpp/
// CudaSpmv.hpp: this header and src/gpu/GpuResidencyManager.cpp (the
// CPU stub -- active() always false, every other method a no-op) are
// compiled together only when CFDAPP_ENABLE_CUDA is OFF;
// cuda/kernels/GpuResidencyManagerCuda.cu (the real pImpl, backed by a
// DeviceCsrMatrix/DeviceField per key) is compiled instead, into the
// cfdcuda target, only when CFDAPP_ENABLE_CUDA=ON. Exactly one of the
// two ever exists in a given binary.
//
// Deliberately runs no GPU *compute* -- only mirrors host data (matrix
// structure/values, field values) into persistent device buffers,
// tracked by GPUExecutionStats. P6-GPU-002 is what will actually read
// this resident data back for a GPU linear solve; until then this class
// only proves the residency/reuse mechanics hold against genuine
// production matrices/fields across genuine SIMPLE outer iterations
// (see SIMPLE.cpp's own call sites) -- it never changes SIMPLE's
// computed result, which stays exclusively CPU-derived (this task does
// not implement GPU CG/BiCGSTAB; see TODO.md P6-GPU-002).
class GpuResidencyManager {
 public:
  GpuResidencyManager();
  ~GpuResidencyManager();

  GpuResidencyManager(const GpuResidencyManager&) = delete;
  GpuResidencyManager& operator=(const GpuResidencyManager&) = delete;
  GpuResidencyManager(GpuResidencyManager&&) noexcept;
  GpuResidencyManager& operator=(GpuResidencyManager&&) noexcept;

  // True iff this binary was built with CUDA support AND a usable
  // device was available when this manager was constructed (mirrors
  // cfd::gpu::cudaAvailable(), see GPUBackend.hpp). Every method below
  // is a safe, side-effect-free no-op when this is false.
  [[nodiscard]] bool active() const noexcept;

  // Uploads `matrix`'s structure once per distinct `key`, then reuses it
  // on every later call with the same key -- value-only re-upload when
  // rows/columns/nonZeros are unchanged from the resident structure for
  // that key, a full structure-and-values re-upload when they differ
  // (the caller never needs to know which happened; this is exactly
  // DeviceCsrMatrix's own uploadStructureAndValues/updateValues split,
  // applied automatically). No-op if !active().
  void syncMatrix(const std::string& key, const cfd::algebra::SparseMatrix& matrix);

  // Uploads `field`'s values to a persistent, per-`key` device buffer,
  // reusing its allocation across same-size calls (DeviceField's own
  // contract). No-op if !active().
  void syncField(const std::string& key, const cfd::fields::Field<cfd::Real>& field);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cfd::gpu
