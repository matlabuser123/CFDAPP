#include "cfd/algebra/Preconditioner.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

void JacobiPreconditioner::build(const SparseMatrix& matrix) {
  const Index n = matrix.rows();
  std::vector<Real> inverseDiagonal(n);
  for (Index row = 0; row < n; ++row) {
    const Real diag = matrix.diagonal(row);  // throws if no diagonal is stored at all
    if (!std::isfinite(diag)) {
      throw InvalidArgumentError("JacobiPreconditioner: non-finite diagonal at row " +
                                 std::to_string(row));
    }
    if (diag == 0.0) {
      throw InvalidArgumentError("JacobiPreconditioner: zero diagonal at row " +
                                 std::to_string(row));
    }
    inverseDiagonal[row] = 1.0 / diag;
  }
  inverseDiagonal_ = std::move(inverseDiagonal);
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
