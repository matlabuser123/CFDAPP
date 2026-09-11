#include "cfd/algebra/Preconditioner.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

Vector computeInverseDiagonal(const SparseMatrix& matrix) {
  const Index n = matrix.rows();
  Vector inverseDiagonal(n);
  for (Index row = 0; row < n; ++row) {
    const Real diag = matrix.diagonal(row);  // throws if no diagonal is stored at all
    if (!std::isfinite(diag)) {
      throw InvalidArgumentError("computeInverseDiagonal: non-finite diagonal at row " +
                                 std::to_string(row));
    }
    if (diag == 0.0) {
      throw InvalidArgumentError("computeInverseDiagonal: zero diagonal at row " +
                                 std::to_string(row));
    }
    if (std::abs(diag) < constants::small) {
      throw InvalidArgumentError("computeInverseDiagonal: near-zero diagonal at row " +
                                 std::to_string(row) + " (|A_ii| < " +
                                 std::to_string(constants::small) + ")");
    }
    inverseDiagonal[row] = 1.0 / diag;
  }
  return inverseDiagonal;
}

void JacobiPreconditioner::build(const SparseMatrix& matrix) {
  inverseDiagonal_ = computeInverseDiagonal(matrix);
}

void JacobiPreconditioner::apply(const Vector& input, Vector& output) const {
  if (input.size() != inverseDiagonal_.size()) {
    throw InvalidArgumentError(
        "JacobiPreconditioner::apply: input size does not match the built matrix");
  }
  if (output.size() != input.size()) {
    output = Vector(input.size());
  }
  for (Index i = 0; i < input.size(); ++i) {
    output[i] = inverseDiagonal_[i] * input[i];
  }
}

}  // namespace cfd::algebra
