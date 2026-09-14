// P12-NUM-004: the linear-solver fallback policy (LinearSolverFallback.hpp).
// Real, deterministic primary failures are used wherever one exists:
// BiCGSTAB breaks down at its first iteration on the symmetric INDEFINITE
// system A = diag(1, -1), b = (1, 1) (rHat . A p = 1 - 1 = 0 exactly). A
// stub primary is used only to isolate policy logic (attempt caps, status
// eligibility) from any particular matrix.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSolverFallback.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::analyzeMatrix;
using cfd::algebra::fallbackCandidates;
using cfd::algebra::FallbackLinearSolver;
using cfd::algebra::LinearSolver;
using cfd::algebra::LinearSolverFallbackSettings;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::LinearSystem;
using cfd::algebra::MatrixProperties;
using cfd::algebra::PreconditionerType;
using cfd::algebra::SolverResult;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

SparseMatrix fromDense(Index n, std::initializer_list<Real> values) {
  SparseMatrixBuilder builder(n, n);
  Index k = 0;
  for (const Real v : values) {
    if (v != 0.0) builder.add(k / n, k % n, v);
    ++k;
  }
  return builder.build();
}

// diag(1, -1): symmetric, indefinite (negative diagonal entry).
LinearSystem indefiniteSystem() {
  return LinearSystem(fromDense(2, {1.0, 0.0, 0.0, -1.0}), Vector{1.0, 1.0});
}

// 1D Dirichlet Laplacian (SPD, irreducibly diagonally dominant).
SparseMatrix laplacian1D(Index n) {
  SparseMatrixBuilder builder(n, n);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, 2.0);
    if (i > 0) builder.add(i, i - 1, -1.0);
    if (i + 1 < n) builder.add(i, i + 1, -1.0);
  }
  return builder.build();
}

LinearSolverSettings settingsFor(LinearSolverType type) {
  LinearSolverSettings settings;
  settings.type = type;
  settings.absoluteTolerance = 1e-12;
  settings.relativeTolerance = 1e-10;
  settings.maxIterations = 200;
  return settings;
}

LinearSolverFallbackSettings enabled(Index attempts = 1) {
  return LinearSolverFallbackSettings{true, attempts};
}

// A primary that always reports `status` without iterating.
class StubSolver final : public LinearSolver {
 public:
  StubSolver(LinearSolverSettings settings, SolverStatus status)
      : LinearSolver(settings), status_(status) {}
  using LinearSolver::solve;
  [[nodiscard]] SolverResult solve(const LinearSystem& system,
                                   const Vector& initialGuess) const override {
    SolverResult result;
    result.solution = initialGuess;
    result.status = status_;
    result.initialResidual = l2Norm(system.rhs() - system.matrix().multiply(initialGuess));
    result.finalResidual = result.initialResidual;
    result.residualHistory = {result.initialResidual};
    return result;
  }

 private:
  SolverStatus status_;
};

}  // namespace

TEST(LinearFallbackTest, PrimaryBiCGSTABGenuinelyBreaksDownOnIndefiniteSystem) {
  const auto result =
      cfd::algebra::BiCGSTAB(settingsFor(LinearSolverType::BiCGSTAB)).solve(indefiniteSystem());
  EXPECT_EQ(result.status, SolverStatus::Breakdown);
  EXPECT_EQ(result.iterations, 0u);
}

TEST(LinearFallbackTest, DisabledPropagatesFailure) {
  const auto settings = settingsFor(LinearSolverType::BiCGSTAB);
  // makeLinearSolverWithFallback with the policy off is the plain solver.
  const auto solver =
      cfd::algebra::makeLinearSolverWithFallback(settings, LinearSolverFallbackSettings{});
  const auto result = solver->solve(indefiniteSystem());
  EXPECT_EQ(result.status, SolverStatus::Breakdown);
  EXPECT_FALSE(result.fallback.attempted);
  // maxAttempts == 0 with the flag on is also "no fallback".
  const auto zero =
      cfd::algebra::makeLinearSolverWithFallback(settings, LinearSolverFallbackSettings{true, 0});
  EXPECT_EQ(zero->solve(indefiniteSystem()).status, SolverStatus::Breakdown);
  EXPECT_FALSE(zero->solve(indefiniteSystem()).fallback.attempted);
}

TEST(LinearFallbackTest, EligibleBreakdownUsesFallback) {
  const LinearSystem system = indefiniteSystem();
  const auto solver = cfd::algebra::makeLinearSolverWithFallback(
      settingsFor(LinearSolverType::BiCGSTAB), enabled());
  const auto result = solver->solve(system);
  ASSERT_EQ(result.status, SolverStatus::Converged);
  EXPECT_NEAR(result.solution[0], 1.0, 1e-12);
  EXPECT_NEAR(result.solution[1], -1.0, 1e-12);
  ASSERT_TRUE(result.fallback.attempted);
  EXPECT_TRUE(result.fallback.recovered());
}

TEST(LinearFallbackTest, SuccessRecorded) {
  const LinearSystem system = indefiniteSystem();
  const auto result =
      cfd::algebra::makeLinearSolverWithFallback(settingsFor(LinearSolverType::BiCGSTAB), enabled())
          ->solve(system);
  const auto& report = result.fallback;
  EXPECT_EQ(report.primaryType, LinearSolverType::BiCGSTAB);
  EXPECT_EQ(report.primaryStatus, SolverStatus::Breakdown);
  EXPECT_EQ(report.primaryIterations, 0u);
  EXPECT_FALSE(report.cgEligible);
  ASSERT_EQ(report.attempts.size(), 1u);
  EXPECT_EQ(report.attempts[0].type, LinearSolverType::GMRES);
  EXPECT_EQ(report.attempts[0].status, SolverStatus::Converged);
  EXPECT_EQ(report.attempts[0].preconditioner, PreconditionerType::None);
  EXPECT_EQ(report.finalType(), LinearSolverType::GMRES);
  // The combined result keeps the PRIMARY's initial residual (the outer
  // solvers' residual definition) and counts every iteration.
  EXPECT_DOUBLE_EQ(result.initialResidual, std::sqrt(2.0));
  EXPECT_EQ(result.iterations, report.primaryIterations + report.attempts[0].iterations);
  EXPECT_EQ(result.residualHistory.size(), result.iterations + 1);
  // Converged to exactly the primary's target: max(abs, rel * ||r0||).
  EXPECT_LE(result.finalResidual, std::max(1e-12, 1e-10 * std::sqrt(2.0)));
}

TEST(LinearFallbackTest, FallbackFailurePropagates) {
  // One iteration is not enough for GMRES on diag(1, -1) from x0 = 0 (the
  // first Krylov space span{(1,1)} does not contain the solution (1,-1)).
  LinearSolverSettings settings = settingsFor(LinearSolverType::BiCGSTAB);
  settings.maxIterations = 1;
  const auto result =
      cfd::algebra::makeLinearSolverWithFallback(settings, enabled())->solve(indefiniteSystem());
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_FALSE(result.converged());
  ASSERT_TRUE(result.fallback.attempted);
  EXPECT_FALSE(result.fallback.recovered());
  ASSERT_EQ(result.fallback.attempts.size(), 1u);
  EXPECT_EQ(result.fallback.attempts[0].status, SolverStatus::MaxIterations);
}

TEST(LinearFallbackTest, MaxAttemptsRespected) {
  // SPD matrix -> candidates [CG, GMRES] for a BiCGSTAB primary. A budget of
  // 1 iteration makes every attempt fail, so the number of attempts is
  // exactly min(maxAttempts, candidates) -- never more (no retry loop).
  const Index n = 8;
  const SparseMatrix a = laplacian1D(n);
  const LinearSystem system(a, Vector(n, 1.0));
  LinearSolverSettings settings = settingsFor(LinearSolverType::BiCGSTAB);
  settings.maxIterations = 1;
  for (const Index attempts : {0u, 1u, 2u, 3u}) {
    FallbackLinearSolver solver(settings, enabled(attempts),
                                std::make_unique<StubSolver>(settings, SolverStatus::Breakdown));
    const auto result = solver.solve(system);
    const Index expected = std::min<Index>(attempts, 2);
    EXPECT_EQ(result.fallback.attempts.size(), expected) << "maxAttempts=" << attempts;
    EXPECT_EQ(result.fallback.attempted, expected > 0);
    if (expected >= 1) {
      EXPECT_EQ(result.fallback.attempts[0].type, LinearSolverType::CG);
    }
    if (expected >= 2) {
      EXPECT_EQ(result.fallback.attempts[1].type, LinearSolverType::GMRES);
    }
  }
  EXPECT_THROW((FallbackLinearSolver(settings, enabled(4))), cfd::InvalidArgumentError);
}

TEST(LinearFallbackTest, DoesNotUseCGForUnsupportedMatrix) {
  // Symmetric but indefinite (negative diagonal): not CG-eligible.
  const MatrixProperties indefinite = analyzeMatrix(indefiniteSystem().matrix());
  EXPECT_TRUE(indefinite.symmetric);
  EXPECT_FALSE(indefinite.positiveDiagonal);
  EXPECT_FALSE(indefinite.cgEligible());
  // Non-symmetric, even though diagonally dominant with a positive diagonal.
  const MatrixProperties nonSymmetric =
      analyzeMatrix(fromDense(3, {4.0, -1.0, 0.0, -2.0, 4.0, -1.0, 0.0, -1.0, 4.0}));
  EXPECT_FALSE(nonSymmetric.symmetric);
  EXPECT_FALSE(nonSymmetric.cgEligible());
  for (const auto& properties : {indefinite, nonSymmetric}) {
    for (const auto primary :
         {LinearSolverType::BiCGSTAB, LinearSolverType::CG, LinearSolverType::GMRES}) {
      for (const auto candidate : fallbackCandidates(primary, properties)) {
        EXPECT_NE(candidate, LinearSolverType::CG);
      }
    }
  }
  // Even with the maximum number of attempts, no CG attempt is made.
  const auto result = cfd::algebra::makeLinearSolverWithFallback(
                          settingsFor(LinearSolverType::BiCGSTAB), enabled(3))
                          ->solve(indefiniteSystem());
  for (const auto& attempt : result.fallback.attempts)
    EXPECT_NE(attempt.type, LinearSolverType::CG);
}

TEST(LinearFallbackTest, SpdMatrixIsCGEligibleAndPreferred) {
  const MatrixProperties spd = analyzeMatrix(laplacian1D(10));
  EXPECT_TRUE(spd.symmetric);
  EXPECT_TRUE(spd.positiveDiagonal);
  EXPECT_TRUE(spd.diagonallyDominant);
  EXPECT_TRUE(spd.strictlyDominantRow);
  EXPECT_TRUE(spd.irreducible);
  EXPECT_TRUE(spd.cgEligible());
  const auto candidates = fallbackCandidates(LinearSolverType::BiCGSTAB, spd);
  ASSERT_EQ(candidates.size(), 2u);
  EXPECT_EQ(candidates[0], LinearSolverType::CG);
  EXPECT_EQ(candidates[1], LinearSolverType::GMRES);
}

TEST(LinearFallbackTest, SingularOrReducibleMatricesAreNotCGEligible) {
  // Pure-Neumann Laplacian (singular): no strictly dominant row.
  const MatrixProperties neumann =
      analyzeMatrix(fromDense(3, {1.0, -1.0, 0.0, -1.0, 2.0, -1.0, 0.0, -1.0, 1.0}));
  EXPECT_FALSE(neumann.strictlyDominantRow);
  EXPECT_FALSE(neumann.cgEligible());
  // Two decoupled blocks, one of them singular: reducible.
  const MatrixProperties reducible =
      analyzeMatrix(fromDense(3, {2.0, 0.0, 0.0, 0.0, 1.0, -1.0, 0.0, -1.0, 1.0}));
  EXPECT_FALSE(reducible.irreducible);
  EXPECT_FALSE(reducible.cgEligible());
}

// The attempt reuses the primary's preconditioner type (recorded) -- Jacobi
// here -- and converges with it.
TEST(LinearFallbackTest, FallbackKeepsAndRecordsThePreconditioner) {
  const SparseMatrix a = fromDense(3, {4.0, -1.0, 0.0, -2.0, 4.0, -1.0, 0.0, -1.0, 4.0});
  const LinearSystem system(a, Vector{1.0, 2.0, 3.0});
  LinearSolverSettings settings = settingsFor(LinearSolverType::BiCGSTAB);
  settings.preconditioner = PreconditionerType::Jacobi;
  FallbackLinearSolver solver(settings, enabled(),
                              std::make_unique<StubSolver>(settings, SolverStatus::Breakdown));
  const auto result = solver.solve(system);
  ASSERT_EQ(result.status, SolverStatus::Converged);
  ASSERT_EQ(result.fallback.attempts.size(), 1u);
  EXPECT_EQ(result.fallback.attempts[0].type, LinearSolverType::GMRES);
  EXPECT_EQ(result.fallback.attempts[0].preconditioner, PreconditionerType::Jacobi);
  EXPECT_LE(l2Norm(system.rhs() - a.multiply(result.solution)),
            1e-10 * l2Norm(system.rhs()) + 1e-12);
}

// Only algorithmic failures are eligible: MaxIterations, NonFiniteInput and
// InvalidSystem come back untouched (no attempt).
TEST(LinearFallbackTest, OnlyAlgorithmicFailuresAreEligible) {
  EXPECT_TRUE(cfd::algebra::isFallbackEligible(SolverStatus::Breakdown));
  EXPECT_TRUE(cfd::algebra::isFallbackEligible(SolverStatus::NonFiniteResidual));
  const LinearSystem system(laplacian1D(4), Vector(4, 1.0));
  for (const auto status : {SolverStatus::MaxIterations, SolverStatus::NonFiniteInput,
                            SolverStatus::InvalidSystem, SolverStatus::Converged}) {
    EXPECT_FALSE(cfd::algebra::isFallbackEligible(status));
    const auto settings = settingsFor(LinearSolverType::BiCGSTAB);
    FallbackLinearSolver solver(settings, enabled(3),
                                std::make_unique<StubSolver>(settings, status));
    const auto result = solver.solve(system);
    EXPECT_EQ(result.status, status);
    EXPECT_FALSE(result.fallback.attempted);
  }
}

// A healthy solve through the policy is the primary's result bit for bit.
TEST(LinearFallbackTest, HealthySolveIsUntouched) {
  const SparseMatrix a = laplacian1D(12);
  const LinearSystem system(a, Vector(12, 1.0));
  const auto settings = settingsFor(LinearSolverType::BiCGSTAB);
  const auto plain = cfd::algebra::makeLinearSolver(settings)->solve(system);
  const auto wrapped =
      cfd::algebra::makeLinearSolverWithFallback(settings, enabled(3))->solve(system);
  ASSERT_EQ(plain.status, SolverStatus::Converged);
  EXPECT_EQ(wrapped.status, plain.status);
  EXPECT_EQ(wrapped.iterations, plain.iterations);
  EXPECT_FALSE(wrapped.fallback.attempted);
  for (Index i = 0; i < 12; ++i) EXPECT_EQ(wrapped.solution[i], plain.solution[i]);
  ASSERT_EQ(wrapped.residualHistory.size(), plain.residualHistory.size());
  for (std::size_t k = 0; k < plain.residualHistory.size(); ++k) {
    EXPECT_EQ(wrapped.residualHistory[k], plain.residualHistory[k]);
  }
}

TEST(LinearFallbackTest, Deterministic) {
  const auto settings = settingsFor(LinearSolverType::BiCGSTAB);
  const auto a =
      cfd::algebra::makeLinearSolverWithFallback(settings, enabled(2))->solve(indefiniteSystem());
  const auto b =
      cfd::algebra::makeLinearSolverWithFallback(settings, enabled(2))->solve(indefiniteSystem());
  ASSERT_EQ(a.fallback.attempts.size(), b.fallback.attempts.size());
  for (std::size_t k = 0; k < a.fallback.attempts.size(); ++k) {
    EXPECT_EQ(a.fallback.attempts[k].type, b.fallback.attempts[k].type);
    EXPECT_EQ(a.fallback.attempts[k].status, b.fallback.attempts[k].status);
    EXPECT_EQ(a.fallback.attempts[k].iterations, b.fallback.attempts[k].iterations);
  }
  EXPECT_EQ(a.solution[0], b.solution[0]);
  EXPECT_EQ(a.solution[1], b.solution[1]);
}
