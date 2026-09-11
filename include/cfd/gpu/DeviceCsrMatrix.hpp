#pragma once

// P6-PERF-001 -- Performance: CUDA-only. See DeviceBuffer.hpp's own
// header comment for the "never include from a CPU-only-compiled file"
// rule this file inherits.
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceVector.hpp"

namespace cfd::gpu {

// A device-resident CSR matrix whose sparsity structure (row offsets +
// column indices) is uploaded once and then reused across repeated
// solves -- only the coefficient values are re-uploaded when they
// change. This is the direct fix for the anti-pattern
// cfd::gpu::csrSpmvCuda() had before this task: a fresh cudaMalloc + full
// structure-and-values upload on every single call, even across
// iterations that only change coefficients (e.g. a re-linearized
// momentum matrix with unchanged sparsity).
class DeviceCsrMatrix {
 public:
  DeviceCsrMatrix() = default;

  // First-time (or structure-changed) upload: uploads row offsets,
  // column indices, and values.
  void uploadStructureAndValues(const cfd::algebra::SparseMatrix& matrix) {
    rows_ = matrix.rows();
    columns_ = matrix.columns();
    nonZeros_ = matrix.nonZeros();
    rowOffsets_.uploadFrom(matrix.rowOffsetsData(), rows_ + 1);
    columnIndices_.uploadFrom(matrix.columnIndicesData(), nonZeros_);
    values_.uploadFrom(matrix.valuesData(), nonZeros_);
    hasStructure_ = true;
  }

  // Re-uploads only the coefficient values, reusing the already-resident
  // structure. Throws InvalidArgumentError if `matrix`'s dimensions/
  // nonzero count differ from the currently-resident structure -- that
  // means the sparsity actually changed and the caller must call
  // uploadStructureAndValues() instead, never silently reinterpret
  // mismatched values against a stale structure.
  void updateValues(const cfd::algebra::SparseMatrix& matrix) {
    if (!hasStructure_) {
      throw cfd::InvalidArgumentError(
          "DeviceCsrMatrix::updateValues: no structure uploaded yet -- call "
          "uploadStructureAndValues first");
    }
    if (matrix.rows() != rows_ || matrix.columns() != columns_ || matrix.nonZeros() != nonZeros_) {
      throw cfd::InvalidArgumentError(
          "DeviceCsrMatrix::updateValues: matrix structure differs from the resident structure "
          "-- call uploadStructureAndValues instead");
    }
    values_.uploadFrom(matrix.valuesData(), nonZeros_);
  }

  [[nodiscard]] bool hasStructure() const noexcept { return hasStructure_; }
  [[nodiscard]] cfd::Index rows() const noexcept { return rows_; }
  [[nodiscard]] cfd::Index columns() const noexcept { return columns_; }
  [[nodiscard]] cfd::Index nonZeros() const noexcept { return nonZeros_; }

  [[nodiscard]] const cfd::Real* valuesDevice() const noexcept { return values_.data(); }
  [[nodiscard]] const cfd::Index* columnIndicesDevice() const noexcept {
    return columnIndices_.data();
  }
  [[nodiscard]] const cfd::Index* rowOffsetsDevice() const noexcept { return rowOffsets_.data(); }

 private:
  cfd::Index rows_{0};
  cfd::Index columns_{0};
  cfd::Index nonZeros_{0};
  bool hasStructure_{false};

  DeviceBuffer<cfd::Real> values_;
  DeviceBuffer<cfd::Index> columnIndices_;
  DeviceBuffer<cfd::Index> rowOffsets_;
};

// Persistent-residency SpMV: launches the same CSR SpMV kernel
// csrSpmvCuda() uses, but directly against already-resident device
// buffers -- no allocation, no host<->device transfer at all. Defined in
// cuda/kernels/CsrSpmvKernel.cu (the only translation unit that also
// defines the kernel itself). Throws InvalidArgumentError if x/y sizes
// don't match the matrix's columns/rows.
void spmv(const DeviceCsrMatrix& matrix, const DeviceVector& x, DeviceVector& y);

}  // namespace cfd::gpu
