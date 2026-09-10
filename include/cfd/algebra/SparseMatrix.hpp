#pragma once

#include <vector>

#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::algebra {

// Sparse matrix in Compressed Sparse Row (CSR) format. Immutable once
// constructed -- assemble via SparseMatrixBuilder and finalize into a
// SparseMatrix rather than mutating CSR data incrementally (CSR is a poor
// fit for that).
//
// Within every row, column indices are stored in strictly ascending
// order: this is validated at construction, not just a builder
// convention, because it guarantees deterministic iteration and
// floating-point summation order (see PROJECT_STRUCTURE.md).
class SparseMatrix {
 public:
  SparseMatrix(Index rows, Index columns, std::vector<Real> values,
               std::vector<Index> columnIndices, std::vector<Index> rowOffsets);

  [[nodiscard]] Index rows() const noexcept;
  [[nodiscard]] Index columns() const noexcept;
  [[nodiscard]] Index nonZeros() const noexcept;

  // y = A * x. Throws InvalidArgumentError if x.size() != columns().
  [[nodiscard]] Vector multiply(const Vector& x) const;

  // Linear scan within the row -- fine for P0; cache positions later if
  // profiling shows it matters. Throws InvalidArgumentError if the row
  // has no stored diagonal entry (never silently returns 0).
  [[nodiscard]] Real diagonal(Index row) const;

  [[nodiscard]] bool allFinite() const noexcept;

  // P4 -- Performance/GPU: raw CSR array access, needed to copy this
  // matrix's storage to a device buffer (cuda/kernels/CsrSpmvKernel.cu)
  // without exposing the private std::vectors themselves or letting a
  // caller mutate CSR data in place (this class stays immutable once
  // built -- see its own class-level comment). Any other caller wanting
  // read-only bulk access (e.g. a future non-CUDA SpMV backend) can use
  // these too; nothing here is CUDA-specific.
  [[nodiscard]] const Real* valuesData() const noexcept;
  [[nodiscard]] const Index* columnIndicesData() const noexcept;
  [[nodiscard]] const Index* rowOffsetsData() const noexcept;

 private:
  Index rows_{};
  Index columns_{};

  std::vector<Real> values_;
  std::vector<Index> columnIndices_;
  std::vector<Index> rowOffsets_;
};

struct Triplet {
  Index row{};
  Index column{};
  Real value{};
};

// Assembles a SparseMatrix from (row, column, value) contributions added
// in any order -- duplicate (row, column) pairs are summed, matching how
// finite-volume assembly repeatedly contributes to the same coefficient
// (e.g. A(P,P) += ...). Exact-zero entries (including ones that sum to
// exactly zero) are dropped at finalization; small-but-nonzero entries
// are always kept -- no arbitrary tolerance-based dropping.
class SparseMatrixBuilder {
 public:
  SparseMatrixBuilder(Index rows, Index columns);

  // P4 -- Performance, matrix-assembly optimization: reserves capacity
  // for at least `expectedTripletCount` (row, column, value)
  // contributions up front, avoiding the repeated reallocation/copy a
  // growing std::vector otherwise pays for during assembly (measured
  // hotspot -- see TODO.md's own P4 status note for the benchmark
  // evidence). Purely a capacity hint: never changes which triplets get
  // added, their order, or the resulting matrix -- calling add() more or
  // fewer times than reserved for is always still correct, just without
  // the reservation's benefit. Safe to skip entirely (the default
  // constructor behavior, an empty vector that grows on demand, is
  // unchanged for any caller that does not call this).
  void reserve(Index expectedTripletCount);

  void add(Index row, Index column, Real value);

  [[nodiscard]] SparseMatrix build() const;

 private:
  Index rows_{};
  Index columns_{};
  std::vector<Triplet> triplets_;
};

}  // namespace cfd::algebra
