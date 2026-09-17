// P12-MESH-004 solver robustness -- BiCGSTAB breakdown detection
// (results/p12-mesh-004/summary.md, "Solver robustness"): the breakdown tests
// are scale-relative (an inner product is zero only when its vectors are
// orthogonal to working precision), so a healthy iteration of a system whose
// residual is merely small is never reported as a breakdown, while a true
// breakdown still is.
//
// A numerical breakdown after progress restarts the Krylov sequence (bounded,
// see BiCGSTAB.hpp); one without progress is reported.
//
// The two stored systems (tests/data/linear_systems/) are the exact thermal
// energy systems on which the pre-fix solver reported Breakdown and the
// production thermal solve failed with LinearSolveFailure (P12-MESH-004 gate
// criteria R3 / M1 / GC1).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::SolverResult;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

struct StoredSystem {
  LinearSystem system;
  Vector initialGuess;
};

// Reads the bicg_capture text format: '#' comment lines, then "rows
// nonzeros", row offsets, column indices, values, rhs, initial guess (reals
// written with %.17g, so the round trip is exact).
StoredSystem loadSystem(const std::string& name) {
  const std::string path = std::string(CFDAPP_TEST_DATA_DIR) + "/linear_systems/" + name;
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << path;
  std::stringstream body;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] != '#') body << line << '\n';
  }
  std::size_t rows = 0;
  std::size_t nonZeros = 0;
  body >> rows >> nonZeros;
  std::vector<Index> offsets(rows + 1);
  std::vector<Index> columns(nonZeros);
  std::vector<Real> values(nonZeros);
  for (auto& o : offsets) body >> o;
  for (auto& c : columns) body >> c;
  for (auto& v : values) body >> v;
  Vector rhs(rows);
  Vector guess(rows);
  for (std::size_t i = 0; i < rows; ++i) body >> rhs[i];
  for (std::size_t i = 0; i < rows; ++i) body >> guess[i];
  EXPECT_FALSE(body.fail()) << path;
  return {LinearSystem(
              SparseMatrix(rows, rows, std::move(values), std::move(columns), std::move(offsets)),
              std::move(rhs)),
          std::move(guess)};
}

Vector toVector(const std::vector<Real>& values) {
  Vector v(values.size());
  for (std::size_t i = 0; i < values.size(); ++i) v[i] = values[i];
  return v;
}

Real trueResidual(const LinearSystem& system, const Vector& x) {
  return cfd::algebra::l2Norm(system.rhs() - system.matrix().multiply(x));
}

SparseMatrix dense(const std::vector<std::vector<Real>>& rows) {
  SparseMatrixBuilder builder(rows.size(), rows.size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    for (std::size_t j = 0; j < rows[i].size(); ++j) {
      if (rows[i][j] != 0.0) builder.add(i, j, rows[i][j]);
    }
  }
  return builder.build();
}

// Absolute tolerance 0 and a relative tolerance no O(1) residual of the
// small systems below can meet: those solves end in exact termination, a
// breakdown or the iteration limit. (Settings require one tolerance > 0.)
LinearSolverSettings strictSettings() {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 0.0;
  settings.relativeTolerance = 1e-14;
  settings.maxIterations = 50;
  return settings;
}

// 1D convection-diffusion, first-order upwind, cell Peclet 0.5: a
// non-symmetric tridiagonal M-matrix (row i: -(1 + Pe), 2 + Pe, -1) whose
// conditioning grows like n^2, scaled by `scale`.
LinearSystem convectionDiffusion(Index n, Real scale) {
  const Real pe = 0.5;
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n);
  for (Index i = 0; i < n; ++i) {
    if (i > 0) builder.add(i, i - 1, -(1.0 + pe) * scale);
    builder.add(i, i, (2.0 + pe) * scale);
    if (i + 1 < n) builder.add(i, i + 1, -1.0 * scale);
    rhs[i] = std::sin(0.1 * static_cast<Real>(i)) * scale;
  }
  return LinearSystem(builder.build(), rhs);
}

void printOutcome(const char* label, const SolverResult& r, Real trueRes) {
  std::printf("%-44s status %d, %4zu iterations, recursive residual %.4e, true residual %.4e\n",
              label, static_cast<int>(r.status), static_cast<std::size_t>(r.iterations),
              r.finalResidual, trueRes);
}

}  // namespace

// --- The P12-MESH-004 reproducers ------------------------------------------------

TEST(BiCGSTABBreakdown, Mesh004ThermalSystemsConvergeToTheRequestedTolerance) {
  for (const char* name :
       {"p12_mesh004_thermal_smooth_a005_n32.txt", "p12_mesh004_thermal_rough_f03_n32.txt"}) {
    const StoredSystem stored = loadSystem(name);
    ASSERT_EQ(stored.system.size(), 1024u) << name;
    // The production thermal linear settings (ThermalSolverSettings defaults).
    const LinearSolverSettings settings;
    ASSERT_EQ(settings.absoluteTolerance, 1e-12);
    ASSERT_EQ(settings.relativeTolerance, 1e-10);
    const SolverResult r = BiCGSTAB(settings).solve(stored.system, stored.initialGuess);
    const Real trueRes = trueResidual(stored.system, r.solution);
    printOutcome(name, r, trueRes);
    EXPECT_EQ(r.status, SolverStatus::Converged) << name;
    EXPECT_EQ(r.restarts, 0u) << name;  // the scale-invariant test alone fixes it
    EXPECT_LE(r.finalResidual, settings.absoluteTolerance) << name;
    // Not a silently accepted solution: the recomputed residual meets the
    // tolerance too.
    EXPECT_LE(trueRes, settings.absoluteTolerance) << name;
  }
}

// --- Scale invariance ---------------------------------------------------------------

// Scaling the whole system by a power of two scales every vector of the
// iteration exactly, so the solve must take the same decisions and give the
// same, exactly scaled, residual history. (The former absolute test broke
// down at the first iteration of the 2^-100 system: rho = |r0|^2 ~ 1e-61.)
TEST(BiCGSTABBreakdown, ScalingTheSystemByAPowerOfTwoChangesNothing) {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 0.0;
  settings.relativeTolerance = 1e-10;
  settings.maxIterations = 500;
  const SolverResult reference =
      BiCGSTAB(settings).solve(convectionDiffusion(60, 1.0), Vector(60, 0.0));
  ASSERT_EQ(reference.status, SolverStatus::Converged);
  for (const Real exponent : {-100.0, 100.0}) {
    const Real scale = std::ldexp(1.0, static_cast<int>(exponent));
    const SolverResult scaled =
        BiCGSTAB(settings).solve(convectionDiffusion(60, scale), Vector(60, 0.0));
    EXPECT_EQ(scaled.status, SolverStatus::Converged) << exponent;
    EXPECT_EQ(scaled.iterations, reference.iterations) << exponent;
    ASSERT_EQ(scaled.residualHistory.size(), reference.residualHistory.size()) << exponent;
    for (std::size_t k = 0; k < reference.residualHistory.size(); ++k) {
      EXPECT_EQ(scaled.residualHistory[k], reference.residualHistory[k] * scale) << k;
    }
    for (Index i = 0; i < 60; ++i) {
      EXPECT_EQ(scaled.solution[i], reference.solution[i]) << i;  // x is scale-free
    }
  }
}

// A slowly converging non-symmetric system (n = 200, 249 iterations) at
// normal scale and scaled by 2^-40 (residuals ~1e-11 .. 1e-21, where the
// former absolute test broke down): both solved, identically, and the
// recomputed residual meets the tolerance.
TEST(BiCGSTABBreakdown, IllConditionedSystemIsSolvedAtAnyScale) {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 0.0;
  settings.relativeTolerance = 1e-10;
  settings.maxIterations = 2000;
  const LinearSystem normal = convectionDiffusion(200, 1.0);
  const LinearSystem small = convectionDiffusion(200, std::ldexp(1.0, -40));
  const SolverResult a = BiCGSTAB(settings).solve(normal, Vector(200, 0.0));
  const SolverResult b = BiCGSTAB(settings).solve(small, Vector(200, 0.0));
  printOutcome("convection-diffusion n=200", a, trueResidual(normal, a.solution));
  printOutcome("convection-diffusion n=200 scaled 2^-40", b, trueResidual(small, b.solution));
  for (const auto* r : {&a, &b}) {
    EXPECT_EQ(r->status, SolverStatus::Converged);
    EXPECT_LE(r->finalResidual, 1e-10 * r->initialResidual);
  }
  EXPECT_LE(trueResidual(normal, a.solution), 1e-9 * a.initialResidual);
  EXPECT_LE(trueResidual(small, b.solution), 1e-9 * b.initialResidual);
  EXPECT_EQ(a.iterations, b.iterations);
}

// --- True breakdowns: reported when no progress was made, else restarted -----------
// Nonsingular 3x3 integer systems, x0 = 0, whose BiCGSTAB recurrences are
// exactly dyadic, so the breaking inner product is EXACTLY zero in IEEE
// double arithmetic (found by an exact rational search).

SolverResult solveStrict(const std::vector<std::vector<Real>>& a, const std::vector<Real>& b,
                         const char* label) {
  const LinearSystem system(dense(a), toVector(b));
  const SolverResult r = BiCGSTAB(strictSettings()).solve(system, Vector(b.size(), 0.0));
  std::printf("%-44s status %d, %zu iterations, %zu restarts, residual %.3e (true %.3e)\n", label,
              static_cast<int>(r.status), static_cast<std::size_t>(r.iterations),
              static_cast<std::size_t>(r.restarts), r.finalResidual,
              trueResidual(system, r.solution));
  return r;
}

// A breakdown in the first iteration of a Krylov sequence -- nothing to
// restart from -- is reported as Breakdown, and the reported residual is
// the returned iterate's (no silent acceptance).
void expectBreakdown(const std::vector<std::vector<Real>>& a, const std::vector<Real>& b,
                     Index completedIterations, Index restarts, const char* label) {
  const SolverResult r = solveStrict(a, b, label);
  EXPECT_EQ(r.status, SolverStatus::Breakdown) << label;
  EXPECT_FALSE(r.converged()) << label;
  EXPECT_EQ(r.iterations, completedIterations) << label;
  EXPECT_EQ(r.restarts, restarts) << label;
  EXPECT_GT(r.finalResidual, 0.0) << label;
  const LinearSystem system(dense(a), toVector(b));
  EXPECT_NEAR(r.finalResidual, trueResidual(system, r.solution), 1e-14) << label;
}

TEST(BiCGSTABBreakdown, TrueBreakdownWithoutProgressIsReported) {
  // A 90-degree rotation: (r0, A r0) = 0 in the first iteration.
  expectBreakdown({{0, 1}, {-1, 0}}, {1, 0}, 0, 0, "rotation: (rHat, v) = 0 at iteration 1");
  // (t, s) = 0 with t != 0 in the first iteration.
  expectBreakdown({{-2, -2, -2}, {-2, -2, -1}, {-2, -1, 0}}, {1, 1, 0}, 0, 0,
                  "omega: (t, s) = 0 at iteration 1");
  // rho_2 = (r0, r1) = 0 after a first iteration that made no progress
  // (|r1| = |r0| = 1): the progress guard reports it instead of restarting.
  expectBreakdown({{-2, -2, -2}, {-2, -2, -1}, {2, -2, -2}}, {1, 0, 0}, 1, 0,
                  "rho = 0 at iteration 2, |r1| = |r0|");
}

// (rHat, v) = 0 in the second iteration, after the first made progress: the
// sequence is restarted from the current iterate and the solve converges --
// verified on the recomputed residual, not on the recursion.
TEST(BiCGSTABBreakdown, BreakdownAfterProgressIsRecoveredByARestart) {
  const std::vector<std::vector<Real>> a = {{-2, -2, -2}, {-2, -2, 2}, {2, -2, -2}};
  const std::vector<Real> b = {1, 0, 1};
  const SolverResult r = solveStrict(a, b, "(rHat, v) = 0 at iteration 2");
  EXPECT_EQ(r.status, SolverStatus::Converged);
  EXPECT_EQ(r.restarts, 1u);
  EXPECT_LE(r.iterations, 50u);
  const LinearSystem system(dense(a), toVector(b));
  EXPECT_LE(trueResidual(system, r.solution), 1e-13);
  // Deterministic: the same solve gives the same result.
  const SolverResult again = BiCGSTAB(strictSettings()).solve(system, Vector(3, 0.0));
  EXPECT_EQ(again.iterations, r.iterations);
  EXPECT_EQ(again.restarts, r.restarts);
  EXPECT_EQ(again.finalResidual, r.finalResidual);
}

// --- Convergence before breakdown -----------------------------------------------------

// A 1x1 system is solved exactly by the first half-step (s = 0); the
// stabilizing step's t . t would then be 0. Convergence is tested first, so
// the result is Converged, not Breakdown.
TEST(BiCGSTABBreakdown, ExactTerminationIsConvergedNotBreakdown) {
  const LinearSystem system(dense({{2.0}}), Vector{2.0});
  const SolverResult r = BiCGSTAB(strictSettings()).solve(system, Vector(1, 0.0));
  EXPECT_EQ(r.status, SolverStatus::Converged);
  EXPECT_EQ(r.iterations, 1u);
  EXPECT_EQ(r.finalResidual, 0.0);
  EXPECT_EQ(r.solution[0], 1.0);
}

TEST(BiCGSTABBreakdown, ZeroRhsAndExactGuessNeedNoIteration) {
  const LinearSystem zero(dense({{4, 1}, {2, 3}}), Vector(2, 0.0));
  const SolverResult a = BiCGSTAB(strictSettings()).solve(zero, Vector(2, 0.0));
  EXPECT_EQ(a.status, SolverStatus::Converged);
  EXPECT_EQ(a.iterations, 0u);
  const LinearSystem exact(dense({{4, 1}, {2, 3}}), Vector{7.0, 11.0});
  const SolverResult b = BiCGSTAB(strictSettings()).solve(exact, Vector{1.0, 3.0});
  EXPECT_EQ(b.status, SolverStatus::Converged);
  EXPECT_EQ(b.iterations, 0u);
  EXPECT_EQ(b.finalResidual, 0.0);
}
