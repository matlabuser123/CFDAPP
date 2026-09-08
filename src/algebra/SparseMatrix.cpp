#include "cfd/algebra/SparseMatrix.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

namespace {

void validateCsr(Index rows, Index columns, const std::vector<Real>& values,
                 const std::vector<Index>& columnIndices, const std::vector<Index>& rowOffsets) {
  if (rowOffsets.size() != rows + 1) {
    throw InvalidArgumentError("SparseMatrix: rowOffsets.size() must equal rows + 1");
  }
  if (values.size() != columnIndices.size()) {
    throw InvalidArgumentError("SparseMatrix: values and columnIndices must be the same size");
  }
  if (rowOffsets.front() != 0) {
    throw InvalidArgumentError("SparseMatrix: rowOffsets[0] must be 0");
  }
  if (rowOffsets.back() != values.size()) {
    throw InvalidArgumentError("SparseMatrix: rowOffsets.back() must equal values.size()");
  }

  for (Index row = 0; row < rows; ++row) {
    if (rowOffsets[row] > rowOffsets[row + 1]) {
      throw InvalidArgumentError("SparseMatrix: rowOffsets must be non-decreasing");
    }
    bool first = true;
    Index previousColumn = 0;
    for (Index k = rowOffsets[row]; k < rowOffsets[row + 1]; ++k) {
      const Index column = columnIndices[k];
      if (column >= columns) {
        throw InvalidArgumentError("SparseMatrix: column index out of range");
      }
      if (!first && column <= previousColumn) {
        throw InvalidArgumentError(
            "SparseMatrix: column indices within a row must be strictly ascending");
      }
      previousColumn = column;
      first = false;
    }
  }

  for (const Real value : values) {
    if (!std::isfinite(value)) {
      throw InvalidArgumentError("SparseMatrix: matrix values must be finite");
    }
  }
}

}  // namespace

SparseMatrix::SparseMatrix(Index rows, Index columns, std::vector<Real> values,
                           std::vector<Index> columnIndices, std::vector<Index> rowOffsets)
    : rows_(rows),
      columns_(columns),
      values_(std::move(values)),
      columnIndices_(std::move(columnIndices)),
      rowOffsets_(std::move(rowOffsets)) {
  validateCsr(rows_, columns_, values_, columnIndices_, rowOffsets_);
}

Index SparseMatrix::rows() const noexcept { return rows_; }
Index SparseMatrix::columns() const noexcept { return columns_; }
Index SparseMatrix::nonZeros() const noexcept { return values_.size(); }

Vector SparseMatrix::multiply(const Vector& x) const {
  if (x.size() != columns_) {
    throw InvalidArgumentError("SparseMatrix::multiply: vector size does not match columns");
  }
  Vector y(rows_, 0.0);
  for (Index row = 0; row < rows_; ++row) {
    Real sum = 0.0;
    for (Index k = rowOffsets_[row]; k < rowOffsets_[row + 1]; ++k) {
      sum += values_[k] * x[columnIndices_[k]];
    }
    y[row] = sum;
  }
  return y;
}

Real SparseMatrix::diagonal(Index row) const {
  if (row >= rows_) {
    throw InvalidArgumentError("SparseMatrix::diagonal: row index out of range");
  }
  for (Index k = rowOffsets_[row]; k < rowOffsets_[row + 1]; ++k) {
    if (columnIndices_[k] == row) {
      return values_[k];
    }
  }
  throw InvalidArgumentError("SparseMatrix::diagonal: no diagonal entry stored at row " +
                             std::to_string(row));
}

bool SparseMatrix::allFinite() const noexcept {
  for (const Real value : values_) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

SparseMatrixBuilder::SparseMatrixBuilder(Index rows, Index columns)
    : rows_(rows), columns_(columns) {}

void SparseMatrixBuilder::add(Index row, Index column, Real value) {
  if (row >= rows_ || column >= columns_) {
    throw InvalidArgumentError("SparseMatrixBuilder::add: index out of range");
  }
  triplets_.push_back(Triplet{row, column, value});
}

SparseMatrix SparseMatrixBuilder::build() const {
  std::vector<Triplet> sorted = triplets_;
  std::stable_sort(sorted.begin(), sorted.end(), [](const Triplet& a, const Triplet& b) {
    if (a.row != b.row) {
      return a.row < b.row;
    }
    return a.column < b.column;
  });

  std::vector<Real> values;
  std::vector<Index> columnIndices;
  std::vector<Index> rowOffsets(rows_ + 1, 0);

  std::size_t next = 0;
  for (Index row = 0; row < rows_; ++row) {
    while (next < sorted.size() && sorted[next].row == row) {
      const Index column = sorted[next].column;
      Real sum = sorted[next].value;
      ++next;
      while (next < sorted.size() && sorted[next].row == row && sorted[next].column == column) {
        sum += sorted[next].value;
        ++next;
      }
      if (sum != 0.0) {
        values.push_back(sum);
        columnIndices.push_back(column);
      }
    }
    rowOffsets[row + 1] = values.size();
  }

  return SparseMatrix(rows_, columns_, std::move(values), std::move(columnIndices),
                      std::move(rowOffsets));
}

}  // namespace cfd::algebra
