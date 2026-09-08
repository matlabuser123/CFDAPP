#pragma once

#include <vector>

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

// diag(A)^-1. Fails clearly (throws InvalidArgumentError) rather than
// silently regularizing a missing, zero, or non-finite diagonal entry --
// that could hide a broken CFD matrix assembly.
class JacobiPreconditioner : public Preconditioner {
 public:
  void build(const SparseMatrix& matrix) override;
  void apply(const Vector& input, Vector& output) const override;

 private:
  std::vector<Real> inverseDiagonal_;
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
