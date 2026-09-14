#include "cfd/algebra/LinearSolverFallback.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <string>
#include <utility>

#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

namespace {

// a_(row, column) from CSR with strictly ascending columns per row, or 0.
Real entry(const SparseMatrix& matrix, Index row, Index column) {
  const Index* offsets = matrix.rowOffsetsData();
  const Index* columns = matrix.columnIndicesData();
  const Index* begin = columns + offsets[row];
  const Index* end = columns + offsets[row + 1];
  const Index* found = std::lower_bound(begin, end, column);
  if (found == end || *found != column) return 0.0;
  return matrix.valuesData()[found - columns];
}

bool allFiniteVector(const Vector& v) { return v.allFinite(); }

}  // namespace

void validateLinearSolverFallbackSettings(const LinearSolverFallbackSettings& settings) {
  if (settings.maxAttempts > kMaxLinearSolverFallbackAttempts) {
    throw InvalidArgumentError("LinearSolverFallbackSettings: maxAttempts must be <= " +
                               std::to_string(kMaxLinearSolverFallbackAttempts));
  }
}

MatrixProperties analyzeMatrix(const SparseMatrix& matrix) {
  MatrixProperties properties;
  const Index n = matrix.rows();
  properties.square = (matrix.rows() == matrix.columns()) && n > 0;
  if (!properties.square) return properties;

  const Index* offsets = matrix.rowOffsetsData();
  const Index* columns = matrix.columnIndicesData();
  const Real* values = matrix.valuesData();

  properties.symmetric = true;
  properties.positiveDiagonal = true;
  properties.diagonallyDominant = true;
  for (Index row = 0; row < n; ++row) {
    Real diagonal = 0.0;
    bool diagonalStored = false;
    Real offDiagonalSum = 0.0;
    for (Index k = offsets[row]; k < offsets[row + 1]; ++k) {
      const Index column = columns[k];
      const Real value = values[k];
      if (column == row) {
        diagonal = value;
        diagonalStored = true;
        continue;
      }
      offDiagonalSum += std::abs(value);
      if (properties.symmetric) {
        const Real transposed = entry(matrix, column, row);
        const Real scale = std::max(std::abs(value), std::abs(transposed));
        if (std::abs(value - transposed) > kSymmetryRelativeTolerance * scale) {
          properties.symmetric = false;
        }
      }
    }
    if (!diagonalStored || !(diagonal > 0.0)) properties.positiveDiagonal = false;
    if (diagonal < (1.0 - kDominanceRelativeTolerance) * offDiagonalSum) {
      properties.diagonallyDominant = false;
    }
    if (diagonal > (1.0 + kDominanceRelativeTolerance) * offDiagonalSum) {
      properties.strictlyDominantRow = true;
    }
  }

  // Irreducibility: the (symmetrized) sparsity graph is connected. For a
  // symmetric matrix the row pattern already is the symmetric graph; for a
  // non-symmetric one the result is not needed (cgEligible is false anyway).
  std::vector<char> visited(n, 0);
  std::queue<Index> frontier;
  frontier.push(0);
  visited[0] = 1;
  Index reached = 1;
  while (!frontier.empty()) {
    const Index row = frontier.front();
    frontier.pop();
    for (Index k = offsets[row]; k < offsets[row + 1]; ++k) {
      const Index column = columns[k];
      if (values[k] != 0.0 && !visited[column]) {
        visited[column] = 1;
        ++reached;
        frontier.push(column);
      }
    }
  }
  properties.irreducible = (reached == n);
  return properties;
}

bool isFallbackEligible(SolverStatus status) noexcept {
  return status == SolverStatus::Breakdown || status == SolverStatus::NonFiniteResidual;
}

std::vector<LinearSolverType> fallbackCandidates(LinearSolverType primary,
                                                 const MatrixProperties& properties) {
  const bool cg = properties.cgEligible();
  switch (primary) {
    case LinearSolverType::BiCGSTAB:
      if (cg) return {LinearSolverType::CG, LinearSolverType::GMRES};
      return {LinearSolverType::GMRES};
    case LinearSolverType::CG:
      return {LinearSolverType::GMRES, LinearSolverType::BiCGSTAB};
    case LinearSolverType::GMRES:
      if (cg) return {LinearSolverType::CG, LinearSolverType::BiCGSTAB};
      return {LinearSolverType::BiCGSTAB};
  }
  return {LinearSolverType::GMRES};
}

std::string_view linearSolverTypeName(LinearSolverType type) noexcept {
  switch (type) {
    case LinearSolverType::CG:
      return "CG";
    case LinearSolverType::BiCGSTAB:
      return "BiCGSTAB";
    case LinearSolverType::GMRES:
      return "GMRES";
  }
  return "Unknown";
}

std::string_view solverStatusName(SolverStatus status) noexcept {
  switch (status) {
    case SolverStatus::Converged:
      return "Converged";
    case SolverStatus::MaxIterations:
      return "MaxIterations";
    case SolverStatus::Breakdown:
      return "Breakdown";
    case SolverStatus::NonFiniteInput:
      return "NonFiniteInput";
    case SolverStatus::NonFiniteResidual:
      return "NonFiniteResidual";
    case SolverStatus::InvalidSystem:
      return "InvalidSystem";
  }
  return "Unknown";
}

FallbackLinearSolver::FallbackLinearSolver(LinearSolverSettings settings,
                                           LinearSolverFallbackSettings fallback,
                                           std::unique_ptr<LinearSolver> primary)
    : LinearSolver(settings), fallback_(fallback), primary_(std::move(primary)) {
  validateLinearSolverFallbackSettings(fallback_);
  if (primary_ == nullptr) {
    primary_ = makeLinearSolver(settings_);
  }
}

SolverResult FallbackLinearSolver::solve(const LinearSystem& system,
                                         const Vector& initialGuess) const {
  SolverResult primaryResult = primary_->solve(system, initialGuess);
  if (primaryResult.converged() || !fallback_.enabled || fallback_.maxAttempts == 0 ||
      !isFallbackEligible(primaryResult.status)) {
    return primaryResult;
  }

  LinearSolverFallbackReport report;
  report.attempted = true;
  report.primaryType = settings_.type;
  report.primaryStatus = primaryResult.status;
  report.primaryIterations = primaryResult.iterations;
  const MatrixProperties properties = analyzeMatrix(system.matrix());
  report.cgEligible = properties.cgEligible();
  const std::vector<LinearSolverType> candidates = fallbackCandidates(settings_.type, properties);

  // Exactly the primary's convergence target (see the header comment).
  const Real target = std::max(settings_.absoluteTolerance,
                               settings_.relativeTolerance * primaryResult.initialResidual);

  Vector start =
      (primaryResult.solution.size() == system.size() && allFiniteVector(primaryResult.solution))
          ? primaryResult.solution
          : initialGuess;

  SolverResult combined = primaryResult;
  const Index attempts = std::min<Index>(fallback_.maxAttempts, candidates.size());
  for (Index a = 0; a < attempts; ++a) {
    LinearSolverSettings attemptSettings = settings_;
    attemptSettings.type = candidates[a];
    attemptSettings.backend = LinearSolverBackend::CPU;
    attemptSettings.absoluteTolerance = target;
    attemptSettings.relativeTolerance = 0.0;
    const std::unique_ptr<LinearSolver> solver = makeLinearSolver(attemptSettings);
    SolverResult attempt = solver->solve(system, start);

    report.attempts.push_back(LinearSolverAttempt{candidates[a], attemptSettings.preconditioner,
                                                  attempt.status, attempt.iterations,
                                                  attempt.finalResidual});
    combined.status = attempt.status;
    combined.solution = attempt.solution;
    combined.finalResidual = attempt.finalResidual;
    combined.iterations += attempt.iterations;
    combined.backendUsed = attempt.backendUsed;
    // Keep residualHistory.size() == iterations + 1: the attempt's element
    // 0 is the residual at its (re)start, already the primary's last value
    // or the previous attempt's.
    if (!attempt.residualHistory.empty()) {
      combined.residualHistory.insert(combined.residualHistory.end(),
                                      attempt.residualHistory.begin() + 1,
                                      attempt.residualHistory.end());
    }
    if (attempt.converged()) break;
    if (attempt.solution.size() == system.size() && allFiniteVector(attempt.solution)) {
      start = attempt.solution;
    }
  }
  combined.initialResidual = primaryResult.initialResidual;
  combined.fallback = std::move(report);
  return combined;
}

std::unique_ptr<LinearSolver> makeLinearSolverWithFallback(
    const LinearSolverSettings& settings, const LinearSolverFallbackSettings& fallback) {
  validateLinearSolverFallbackSettings(fallback);
  if (!fallback.enabled || fallback.maxAttempts == 0) {
    return makeLinearSolver(settings);
  }
  return std::make_unique<FallbackLinearSolver>(settings, fallback);
}

}  // namespace cfd::algebra
