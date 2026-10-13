// TD-003 criterion 2 -- "a solver demonstrated adequate on the plug-flow matrix class".
//
// THE MATRIX CLASS. Pure first-order upwind 1D convection with a transient/Patankar diagonal and
// NO diffusion is LOWER BIDIAGONAL:
//
//     row 0:  a_00 = d
//     row i:  a_{i,i-1} = -s,   a_{ii} = d
//
// That is the class P18.7 broke on (results/p18/conservation_validation/logs/03_plugflow_diagnosis
// .log: diag 2.0 on every row, BiCGSTAB returning -3.05e12 while reporting Breakdown). It is NOT
// the class tests/unit/algebra/test_bicgstab_breakdown.cpp already covers -- that one is
// convection-DIFFUSION, a tridiagonal M-matrix at cell Peclet 0.5 (row: -(1+Pe), 2+Pe, -1).
// Removing the diffusion removes the superdiagonal and the M-matrix property with it.
//
// WHY A SWEEP RATHER THAN ONE CAPTURED MATRIX. The criterion says "class", and the behaviour is
// governed by one dimensionless number, r = s/d:
//     r <  1  strictly diagonally dominant -- benign
//     r == 1  marginal; the exact solution's partial sums neither grow nor decay
//     r >  1  every forward-substitution step amplifies by r, so ||A^-1|| grows like r^n
// A single capture fixes r at one value and cannot show where the cliff is.
//
// THE REFERENCE IS INDEPENDENT (CLAUDE.md §6). A lower bidiagonal matrix is lower TRIANGULAR, so
// forward substitution solves it exactly in O(n) with no iteration and no tolerance. Every error
// below is a TRUE error against that, not a residual -- which matters because TD-003's criterion 3
// turns on residual-vs-error (kappa) and a residual cannot see kappa.
//
// NON-VACUITY (§5.2). Three built-in controls, each of which must behave or the probe is void:
//   POSITIVE  r = 0.5 is strictly diagonally dominant: every solver must converge.
//   NEGATIVE  CG is applied to a matrix that is NOT symmetric. It must fail or return a wrong
//             answer; if CG "passes" here the harness is not measuring what it claims.
//   EXACTNESS forward substitution is checked against the constructed x* at r = 0.5, where no
//             amplification can hide an indexing error.
//
// Build (from the repo root, after a release configure):
//   g++ -std=c++20 -O2 -I include -I build/release/include \
//       results/technical_debt/TD003_linear_solver/tools/plugflow_matrix_class_probe.cpp \
//       build/release/src/libcfdcore.a -o /tmp/td003_probe

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/GMRES.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::CG;
using cfd::algebra::GMRES;
using cfd::algebra::JacobiPreconditioner;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::Preconditioner;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

const char* statusName(SolverStatus s) {
  switch (s) {
    case SolverStatus::Converged:
      return "Converged";
    case SolverStatus::MaxIterations:
      return "MaxIterations";
    case SolverStatus::Breakdown:
      return "Breakdown";
    case SolverStatus::NonFiniteInput:
      return "NonFiniteInput";
    case SolverStatus::NonFiniteResidual:
      return "NonFiniteResid";
    case SolverStatus::InvalidSystem:
      return "InvalidSystem";
  }
  return "?";
}

// The exact solution x*, chosen so no component is zero and the profile is not constant (a
// constant vector would be solved exactly by almost anything and would hide errors).
std::vector<Real> exactSolution(Index n) {
  std::vector<Real> x(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    x[static_cast<std::size_t>(i)] = 1.0 + 0.25 * std::sin(0.7 * static_cast<Real>(i));
  }
  return x;
}

SparseMatrix buildBidiagonal(Index n, Real d, Real s) {
  SparseMatrixBuilder builder(n, n);
  builder.reserve(2 * n);
  for (Index i = 0; i < n; ++i) {
    if (i > 0) builder.add(i, i - 1, -s);
    builder.add(i, i, d);
  }
  return builder.build();
}

// b = A x*, formed from the bidiagonal structure directly so the probe does not depend on a
// matrix-vector product it is also testing.
Vector rhsFor(Index n, Real d, Real s, const std::vector<Real>& x) {
  Vector b(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    Real v = d * x[static_cast<std::size_t>(i)];
    if (i > 0) v -= s * x[static_cast<std::size_t>(i - 1)];
    b[static_cast<std::size_t>(i)] = v;
  }
  return b;
}

// THE INDEPENDENT EXACT SOLVE. Forward substitution on a lower bidiagonal system.
std::vector<Real> forwardSubstitute(Index n, Real d, Real s, const Vector& b) {
  std::vector<Real> x(static_cast<std::size_t>(n), 0.0);
  for (Index i = 0; i < n; ++i) {
    Real rhs = b[static_cast<std::size_t>(i)];
    if (i > 0) rhs += s * x[static_cast<std::size_t>(i - 1)];
    x[static_cast<std::size_t>(i)] = rhs / d;
  }
  return x;
}

Real relativeError(const Vector& got, const std::vector<Real>& want) {
  Real num = 0.0;
  Real den = 0.0;
  for (std::size_t i = 0; i < want.size(); ++i) {
    const Real g = (i < got.size()) ? got[i] : 0.0;
    const Real diff = g - want[i];
    num += diff * diff;
    den += want[i] * want[i];
  }
  if (den <= 0.0) return std::numeric_limits<Real>::quiet_NaN();
  return std::sqrt(num) / std::sqrt(den);
}

// kappa_inf = ||A||_inf * ||A^-1||_inf, both computed exactly for this structure.
// ||A^-1||_inf is the largest absolute row sum of the inverse; column j of A^-1 is a forward
// substitution against e_j, so the whole inverse costs O(n^2) -- affordable at these sizes.
Real conditionNumberInf(Index n, Real d, Real s) {
  Real normA = 0.0;
  for (Index i = 0; i < n; ++i) {
    normA = std::max(normA, std::abs(d) + ((i > 0) ? std::abs(s) : 0.0));
  }
  std::vector<Real> rowSum(static_cast<std::size_t>(n), 0.0);
  for (Index j = 0; j < n; ++j) {
    Vector e(static_cast<std::size_t>(n), 0.0);
    e[static_cast<std::size_t>(j)] = 1.0;
    const std::vector<Real> col = forwardSubstitute(n, d, s, e);
    for (Index i = 0; i < n; ++i) {
      rowSum[static_cast<std::size_t>(i)] += std::abs(col[static_cast<std::size_t>(i)]);
    }
  }
  Real normInv = 0.0;
  for (Index i = 0; i < n; ++i) normInv = std::max(normInv, rowSum[static_cast<std::size_t>(i)]);
  return normA * normInv;
}

struct Attempt {
  std::string label;
  SolverStatus status{SolverStatus::MaxIterations};
  Index iterations{0};
  Real finalResidual{};
  Real trueError{};
};

LinearSolverSettings flameLikeSettings(Index n) {
  // The flame path's own numbers: PremixedFlame.cpp:624-625 sets maxIterations 5000 and
  // relativeTolerance 1e-11, then :686 overrides absoluteTolerance to 1e-9 * ||b||. The RELATIVE
  // criterion is what governs there, so the probe uses the same relative tolerance.
  LinearSolverSettings st;
  st.absoluteTolerance = 0.0;
  st.relativeTolerance = 1.0e-11;
  st.maxIterations = 5000;
  st.gmresRestart = 30;
  (void)n;
  return st;
}

Attempt run(const std::string& label, const cfd::algebra::LinearSolver& solver,
            const LinearSystem& system, Index n, const std::vector<Real>& want) {
  Attempt a;
  a.label = label;
  const Vector guess(static_cast<std::size_t>(n), 0.0);
  const auto r = solver.solve(system, guess);
  a.status = r.status;
  a.iterations = r.iterations;
  a.finalResidual = r.finalResidual;
  a.trueError = relativeError(r.solution, want);
  return a;
}

}  // namespace

int main() {
  std::printf("TD-003 criterion 2 -- the plug-flow (pure-upwind) matrix class\n");
  std::printf("A is LOWER BIDIAGONAL: a_ii = d = 2.0, a_{i,i-1} = -s, r = s/d.\n");
  std::printf("Reference = forward substitution (exact, O(n), no tolerance).\n");
  std::printf("Errors are TRUE relative errors against that, NOT residuals.\n\n");

  const Real d = 2.0;  // the diagonal the P18.7 diagnosis recorded on every row
  const std::vector<Real> ratios = {0.5, 0.9, 1.0, 1.05, 2.0, 16.0};
  const std::vector<Index> sizes = {10, 50, 200};

  // ---- CONTROL 3: forward substitution is exact where nothing can hide an error.
  {
    const Index n = 10;
    const Real s = 0.5 * d;
    const auto want = exactSolution(n);
    const Vector b = rhsFor(n, d, s, want);
    const auto got = forwardSubstitute(n, d, s, b);
    Real worst = 0.0;
    for (Index i = 0; i < n; ++i) {
      worst = std::max(worst, std::abs(got[static_cast<std::size_t>(i)] -
                                      want[static_cast<std::size_t>(i)]));
    }
    std::printf("CONTROL 3 (exactness of the reference, n=10, r=0.5): worst |x - x*| = %.3e\n",
                worst);
    std::printf("  EXPECT <= 1e-15 -> %s\n\n", (worst <= 1e-15) ? "PASS" : "*** FAIL ***");
  }

  for (const Index n : sizes) {
    const auto want = exactSolution(n);
    std::printf("================ n = %lld\n", static_cast<long long>(n));
    std::printf("  %5s %12s %16s %7s %6s %12s %12s\n", "r", "kappa_inf", "solver", "status",
                "iters", "residual", "TRUE error");
    for (const Real ratio : ratios) {
      const Real s = ratio * d;
      const SparseMatrix A = buildBidiagonal(n, d, s);
      const Vector b = rhsFor(n, d, s, want);
      const LinearSystem system(A, b);
      const Real kappa = conditionNumberInf(n, d, s);
      const auto settings = flameLikeSettings(n);
      const auto jacobi = std::make_shared<JacobiPreconditioner>();

      std::vector<Attempt> attempts;
      attempts.push_back(run("BiCGSTAB/none", BiCGSTAB(settings), system, n, want));
      attempts.push_back(run("BiCGSTAB/jac", BiCGSTAB(settings, jacobi), system, n, want));
      attempts.push_back(run("GMRES/none", GMRES(settings), system, n, want));
      attempts.push_back(run("GMRES/jac", GMRES(settings, jacobi), system, n, want));
      // CONTROL 2 (NEGATIVE): A is not symmetric, so CG has no right to succeed.
      attempts.push_back(run("CG/jac [NEG]", CG(settings, jacobi), system, n, want));

      bool first = true;
      for (const auto& a : attempts) {
        if (first) {
          std::printf("  %5.2f %12.3e %16s %7s %6lld %12.3e %12.3e\n", ratio, kappa,
                      a.label.c_str(), statusName(a.status), static_cast<long long>(a.iterations),
                      a.finalResidual, a.trueError);
          first = false;
        } else {
          std::printf("  %5s %12s %16s %7s %6lld %12.3e %12.3e\n", "", "", a.label.c_str(),
                      statusName(a.status), static_cast<long long>(a.iterations), a.finalResidual,
                      a.trueError);
        }
      }
      // The forward substitution, for the same system.
      const auto direct = forwardSubstitute(n, d, s, b);
      Vector directV(static_cast<std::size_t>(n));
      for (Index i = 0; i < n; ++i) directV[static_cast<std::size_t>(i)] = direct[static_cast<std::size_t>(i)];
      std::printf("  %5s %12s %16s %7s %6s %12s %12.3e\n", "", "", "fwd-subst [REF]", "exact", "n",
                  "-", relativeError(directV, want));
      std::printf("\n");
    }
  }

  std::printf("READ THIS OUTPUT AS:\n");
  std::printf("  * r = 0.5 is CONTROL 1 (positive): strictly diagonally dominant. Every solver\n");
  std::printf("    must converge with a small TRUE error. If one does not, the class is worse\n");
  std::printf("    than TD-003 describes.\n");
  std::printf("  * CG is CONTROL 2 (negative): A is NOT symmetric. CG must fail or be wrong.\n");
  std::printf("  * A small residual beside a large TRUE error is the kappa effect, and it is\n");
  std::printf("    exactly what TD-003's criterion 3 turns on: |sum Y - 1| is an ERROR, while\n");
  std::printf("    residualAcceptanceFraction bounds a RESIDUAL.\n");
  return 0;
}
