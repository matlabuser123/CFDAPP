#pragma once

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

namespace cfd::algebra {

// A preconditioner approximates A^-1 to accelerate Krylov iteration.
// build() is called once against the system matrix; apply() is then
// called every iteration to compute output = M^-1 * input.
class Preconditioner {
 public:
  virtual ~Preconditioner() = default;

  virtual void build(const SparseMatrix& matrix) = 0;
  virtual void apply(const Vector& input, Vector& output) const = 0;
};

// No-op preconditioner (M = I). Useful for tests comparing CG/BiCGSTAB
// with and without preconditioning.
class IdentityPreconditioner : public Preconditioner {
 public:
  void build(const SparseMatrix& /*matrix*/) override {}
  void apply(const Vector& input, Vector& output) const override { output = input; }
};

// Computes diag(A)^-1, one entry per row. Shared by JacobiPreconditioner
// (below) and cfd::gpu's GPU-resident Jacobi preconditioner
// (GpuPreconditioner.hpp -- P6-GPU-003), so both CPU and GPU paths apply
// the exact same validation instead of two independently-maintained
// copies. Fails clearly (throws InvalidArgumentError) rather than
// silently regularizing a missing, zero, near-zero, or non-finite
// diagonal entry -- that could hide a broken CFD matrix assembly:
//   - missing: matrix.diagonal(row) itself throws if no diagonal is
//     stored at all for that row.
//   - zero / near-zero: |A_ii| < cfd::constants::small (1e-12) is
//     treated as numerically dangerous to invert, not just literal 0.0.
//   - non-finite: NaN/Inf (defense in depth -- SparseMatrix already
//     rejects non-finite values at construction, so this can't currently
//     be reached through a normally-built matrix).
[[nodiscard]] Vector computeInverseDiagonal(const SparseMatrix& matrix);

// diag(A)^-1. See computeInverseDiagonal's own header comment for the
// exact validation policy.
class JacobiPreconditioner : public Preconditioner {
 public:
  void build(const SparseMatrix& matrix) override;
  void apply(const Vector& input, Vector& output) const override;

 private:
  Vector inverseDiagonal_;
};

// Shared by CG/BiCGSTAB: applies `preconditioner` if non-null, otherwise
// treats it as the identity (output = input) without requiring callers to
// allocate an IdentityPreconditioner just to mean "no preconditioning".
inline void applyPreconditionerOrIdentity(const Preconditioner* preconditioner, const Vector& input,
                                          Vector& output) {
  if (preconditioner != nullptr) {
    preconditioner->apply(input, output);
  } else {
    output = input;
  }
}

}  // namespace cfd::algebra
