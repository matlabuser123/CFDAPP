#include "cfd/pressure_velocity/UnderRelaxation.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

void applyImplicitUnderRelaxation(SparseMatrixBuilder& builder, Vector& rhs,
                                  const Vector& previousValue, Real alpha) {
  if (!std::isfinite(alpha) || !(alpha > 0.0) || alpha > 1.0) {
    throw InvalidArgumentError("applyImplicitUnderRelaxation: alpha must be finite and in (0, 1]");
  }
  if (alpha == 1.0) {
    return;  // No relaxation: aP/1 == aP, (1-1)/1 == 0 -- exact no-op.
  }

  const SparseMatrix unrelaxed = builder.build();
  const Index n = unrelaxed.rows();
  if (previousValue.size() != n || rhs.size() != n) {
    throw InvalidArgumentError(
        "applyImplicitUnderRelaxation: previousValue/rhs size does not match the builder's row "
        "count");
  }

  const Real factor = (1.0 / alpha) - 1.0;
  for (Index i = 0; i < n; ++i) {
    const Real extra = unrelaxed.diagonal(i) * factor;
    builder.add(i, i, extra);
    rhs[i] += extra * previousValue[i];
  }
}

}  // namespace cfd::pressure_velocity
