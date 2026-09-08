#include "cfd/algebra/LinearSystem.hpp"

#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

LinearSystem::LinearSystem(SparseMatrix matrix, Vector rhs)
    : matrix_(std::move(matrix)), rhs_(std::move(rhs)) {
  if (matrix_.rows() != matrix_.columns()) {
    throw InvalidArgumentError("LinearSystem requires a square matrix");
  }
  if (matrix_.rows() != rhs_.size()) {
    throw InvalidArgumentError("LinearSystem: matrix row count must equal rhs size");
  }
}

const SparseMatrix& LinearSystem::matrix() const noexcept { return matrix_; }
const Vector& LinearSystem::rhs() const noexcept { return rhs_; }
Index LinearSystem::size() const noexcept { return matrix_.rows(); }

}  // namespace cfd::algebra
