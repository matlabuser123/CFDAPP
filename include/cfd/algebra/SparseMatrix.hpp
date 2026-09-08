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

  void add(Index row, Index column, Real value);

  [[nodiscard]] SparseMatrix build() const;

 private:
  Index rows_{};
  Index columns_{};
  std::vector<Triplet> triplets_;
};

}  // namespace cfd::algebra
