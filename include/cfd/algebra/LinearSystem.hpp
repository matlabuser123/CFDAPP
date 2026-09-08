#pragma once

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::algebra {

// Bundles A and b for A x = b, validating dimensional consistency once at
// construction (square matrix, rows == rhs size) so no solver needs to
// re-check it.
class LinearSystem {
 public:
  LinearSystem(SparseMatrix matrix, Vector rhs);

  [[nodiscard]] const SparseMatrix& matrix() const noexcept;
  [[nodiscard]] const Vector& rhs() const noexcept;

  [[nodiscard]] Index size() const noexcept;

 private:
  SparseMatrix matrix_;
  Vector rhs_;
};

}  // namespace cfd::algebra
