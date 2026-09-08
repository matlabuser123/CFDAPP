#pragma once

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::pressure_velocity {

// Patankar-style implicit under-relaxation, applied to an *already
// assembled* (but not yet finalized) equation: for row P with unrelaxed
// diagonal aP,
//   aP_relaxed = aP / alpha
//   b_relaxed  = b + (1 - alpha) / alpha * aP * phiOld
// (TODO.md P0 -- SIMPLE section 10). This is a SIMPLE-layer concern
// applied AFTER physics assembly, never inside MomentumEquation.cpp
// (TODO.md P0 -- Incompressible Physics section 40).
//
// SparseMatrix is immutable once built, so `aP_relaxed` cannot replace
// the existing diagonal entry in place -- instead this ADDS
// aP * (1/alpha - 1) via `builder.add`, which SparseMatrixBuilder sums
// with the existing entry at build() time, reaching the same aP/alpha
// result. `builder` must already hold every unrelaxed contribution
// (diffusion + convection + pressure source) with a stored diagonal at
// every row; this function performs one build() of its own (cheap: the
// builder is const-inspectable and not consumed) purely to read those
// unrelaxed diagonals, then adds the relaxation terms in place. Call
// builder.build() again afterward for the final, relaxed matrix.
//
// alpha == 1 is a no-op (equivalent to no relaxation) and returns
// immediately without touching builder/rhs.
//
// Throws InvalidArgumentError if alpha is not finite and in (0, 1], or
// if previousValue.size()/rhs.size() does not match the builder's row
// count.
void applyImplicitUnderRelaxation(cfd::algebra::SparseMatrixBuilder& builder,
                                  cfd::algebra::Vector& rhs,
                                  const cfd::algebra::Vector& previousValue, Real alpha);

}  // namespace cfd::pressure_velocity
