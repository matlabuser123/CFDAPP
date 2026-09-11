#pragma once

#include <vector>

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::algebra {

// Use SolverStatus for legitimate numerical solve outcomes; use exceptions
// (InvalidArgumentError etc.) for invalid programming/configuration states
// (size mismatches, invalid tolerances, malformed CSR, ...). This keeps
// control flow clean: a caller never needs to catch an exception just to
// find out an iterative solve didn't converge.
enum class SolverStatus {
  Converged,
  MaxIterations,
  Breakdown,
  NonFiniteInput,
  NonFiniteResidual,
  InvalidSystem,
};

// P6-GPU-002 -- Performance: which Krylov method to run. A selection
// parameter for cfd::algebra::makeLinearSolver() (LinearSolverFactory.hpp)
// -- the concrete CG/BiCGSTAB (or GpuCG/GpuBiCGSTAB) classes themselves
// don't carry this; it exists purely so a caller building a solver from
// data (case-file JSON, GUI settings) can pick a concrete class without a
// hand-written if/else at every call site.
enum class LinearSolverType {
  CG,
  BiCGSTAB,
};

// Which execution backend actually ran (or should be requested to run)
// the solve. GPU means "prefer a persistent-GPU-resident CUDA
// implementation if this binary was built with CUDA support and a usable
// device is available at runtime" -- never a hard requirement: see
// makeLinearSolver()'s own header comment for the deterministic,
// logged fallback to CPU when GPU is requested but unusable.
enum class LinearSolverBackend {
  CPU,
  GPU,
};

// P6-GPU-003 -- Performance: which preconditioner cfd::algebra::
// makeLinearSolver() should build, when the caller does not pass an
// explicit std::shared_ptr<Preconditioner> of its own (that explicit
// argument always wins -- see LinearSolverFactory.hpp's own header
// comment). Data, not a polymorphic object, for the same reason type/
// backend above are: a caller building a solver from a case file or GUI
// settings picks a preconditioner without a hand-written if/else, and
// SIMPLE itself only ever sets this enum -- it never constructs or knows
// about a concrete Preconditioner (or GPU-resident Jacobi) class.
enum class PreconditionerType {
  None,
  Jacobi,
};

// residualHistory stores the absolute residual norm ||b - A x||_2 --
// element 0 is the initial residual, element k is the residual after
// iteration k, so residualHistory.size() == iterations + 1 for an
// ordinary solve. Relative residual is derived from this (against the
// initial residual) rather than stored separately.
struct SolverResult {
  Vector solution;
  SolverStatus status{SolverStatus::MaxIterations};
  Index iterations{0};
  Real initialResidual{};
  Real finalResidual{};
  std::vector<Real> residualHistory;

  // P6-GPU-002 -- Performance: which backend actually produced this
  // result -- every concrete solver sets this itself (CG/BiCGSTAB always
  // set CPU; GpuCG/GpuBiCGSTAB always set GPU), so a caller never has to
  // infer it from which class it happened to construct. In particular,
  // this is how a caller distinguishes "GPU was requested and ran" from
  // "GPU was requested but makeLinearSolver() fell back to CPU" -- the
  // latter reports CPU here even though LinearSolverSettings::backend
  // said GPU (see makeLinearSolver()'s own header comment).
  LinearSolverBackend backendUsed{LinearSolverBackend::CPU};

  [[nodiscard]] bool converged() const noexcept { return status == SolverStatus::Converged; }
};

struct LinearSolverSettings {
  Real absoluteTolerance{1e-12};
  Real relativeTolerance{1e-10};
  Index maxIterations{1000};

  // P6-GPU-002 -- Performance: both default to the exact values every
  // pre-P6-GPU-002 call site already got (BiCGSTAB, CPU) -- adding these
  // fields changes no existing behavior unless a caller explicitly sets
  // them. type/backend are selection data for
  // cfd::algebra::makeLinearSolver() (LinearSolverFactory.hpp); the CG/
  // BiCGSTAB classes themselves don't read these fields, so constructing
  // one directly (as every pre-existing call site still does) ignores
  // them entirely -- only the factory acts on them.
  LinearSolverType type{LinearSolverType::BiCGSTAB};
  LinearSolverBackend backend{LinearSolverBackend::CPU};

  // P6-GPU-003 -- Performance: defaults to None, the exact behavior every
  // pre-P6-GPU-003 call site already got (makeLinearSolver()'s own
  // explicit `preconditioner` parameter defaulted to nullptr, and no
  // production call site passed one) -- adding this field changes no
  // existing behavior unless a caller explicitly sets it. See
  // PreconditionerType's own header comment.
  PreconditionerType preconditioner{PreconditionerType::None};
};

// Common base for iterative Krylov solvers (CG, BiCGSTAB). Settings are
// fixed at construction: clearer than re-passing them to every solve()
// call, since one solver instance is typically configured once and reused.
class LinearSolver {
 public:
  explicit LinearSolver(LinearSolverSettings settings);
  virtual ~LinearSolver() = default;

  [[nodiscard]] virtual SolverResult solve(const LinearSystem& system,
                                           const Vector& initialGuess) const = 0;

  // Convenience overload: solves starting from x0 = 0.
  [[nodiscard]] SolverResult solve(const LinearSystem& system) const;

  [[nodiscard]] const LinearSolverSettings& settings() const noexcept;

 protected:
  LinearSolverSettings settings_;
};

}  // namespace cfd::algebra
