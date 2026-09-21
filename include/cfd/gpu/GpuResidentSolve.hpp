#pragma once

// GPU-PIPE-001 (GPU-resident pressure solve) -- CUDA-only. See
// DeviceBuffer.hpp's own header comment for the "never include from a
// CPU-only-compiled file" rule this file inherits.
//
// A SECOND ENTRY POINT INTO THE EXISTING SOLVER, NOT A SECOND SOLVER.
//
// GpuBiCGSTAB::solveImpl used to be: validate a host LinearSystem, upload it,
// run the algorithm, download the solution. The algorithm itself never touched
// the host -- every operand was already a DeviceVector/DeviceCsrMatrix. So the
// algorithm was lifted out verbatim into one core routine, and the two entry
// points differ only in how the device state is established and whether the
// solution is downloaded:
//
//   solve(LinearSystem, guess)  upload A, b, x0  ->  core  ->  download x
//   solveBiCGSTABResident(...)  adopt A, b, x0   ->  core  ->  x stays resident
//
// There is exactly one copy of the BiCGSTAB iteration, one set of breakdown
// tests and one convergence test. A change to either entry point cannot make
// the two disagree numerically, because neither owns any numerics.
//
// See results/gpu-pipe-001/gpu-resident-pressure-solve/audit.md section 7.

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceVector.hpp"

namespace cfd::gpu {

// The Krylov scratch the BiCGSTAB iteration needs, held by whoever drives the
// solve so it survives across calls. GpuBiCGSTAB owns one; a resident caller
// (the SIMPLE pressure stage) owns its own, which is what makes "zero
// steady-state allocations" hold for the resident path too -- resize() is a
// no-op once the vectors are the right length.
//
// `x` is the solution vector and is deliberately part of the workspace rather
// than a separate argument: on the resident path it is both the initial guess
// and the result, and it stays on the device afterwards.
struct GpuKrylovWorkspace {
  DeviceVector x, b, r, rHat, p, v, s, t, pHat, sHat;
  // Only populated when settings.preconditioner == Jacobi.
  DeviceVector jacobiInverseDiagonal;
  // Two counters read back in ONE 8-byte D2H: [0] non-finite input entries
  // across the matrix, the RHS and the initial guess; [1] rows whose diagonal
  // makes a Jacobi preconditioner impossible.
  //
  // Caller-owned for the same measured reason DevicePersistentFields.hpp gives:
  // a locally allocated counter allocates, and does so once per solve, which
  // shows up directly in the steady-state allocation count this gate has to
  // keep at zero. The first version of this code did exactly that and the
  // comparison harness caught it -- allocations rose 375 -> 406 over 8 outer
  // iterations.
  DeviceBuffer<unsigned int> counters;

  // Sizes every vector to `n`. A no-op for a vector already at least that long
  // (DeviceBuffer::resize()'s own contract), so repeated calls allocate nothing.
  void resize(cfd::Index n);
  [[nodiscard]] std::size_t residentBytes() const noexcept;
};

// What the host entry point learns from Vector::allFinite(),
// SparseMatrix::allFinite() and computeInverseDiagonal() throwing, decided on
// the device instead -- because the resident path has no host copy to ask, and
// skipping those questions rather than moving them would make the two entry
// points disagree about what a bad system does.
//
// Every one of them is answered in ONE 8-byte read. The kernels all accumulate
// into `workspace.counters`, so the cost is a single host round trip per solve
// rather than one per question. Five separate reads was the first version, and
// the transfer measurement showed it cancelling out most of what the gate had
// removed: D2H CALLS did not fall at all, even as D2H bytes fell 25%.
//
// `inputsFinite` is checked first by the caller, so a non-finite matrix reports
// NonFiniteInput rather than InvalidSystem -- the host path's order, preserved.
struct ResidentSystemCheck {
  bool inputsFinite{false};
  bool diagonalUsable{false};
};
[[nodiscard]] ResidentSystemCheck checkSystemAndBuildJacobi(const DeviceCsrMatrix& matrix,
                                                            GpuKrylovWorkspace& workspace,
                                                            bool wantJacobi);

// Runs BiCGSTAB entirely on the device against `matrix` and `workspace.b`,
// starting from `workspace.x` and leaving the solution there.
//
// `matrix` may adopt buffers the caller owns (DeviceCsrMatrix::adoptDevice), so
// the assembly's own CSR can be solved in place with no copy at all.
//
// The returned SolverResult carries status, iterations, residual history and
// final residual exactly as the host entry point does -- but `solution` is left
// EMPTY, because downloading it is the one thing this entry point exists to
// avoid. A caller that wants it on the host downloads workspace.x itself.
[[nodiscard]] cfd::algebra::SolverResult solveBiCGSTABResident(
    const cfd::algebra::LinearSolverSettings& settings, const DeviceCsrMatrix& matrix,
    GpuKrylovWorkspace& workspace);

// Device-to-device moves between a caller's buffers and the workspace. The
// assembly writes its RHS into a DeviceBuffer and the corrections read p' from
// one, while the solver works in DeviceVectors; these bridge the two without
// PCIe traffic.
//
// Deliberately NOT counted in GPUExecutionStats as transfers -- they never
// cross the boundary, and a device-side copy that showed up as an H2D would
// make the residency measurement this gate rests on report a number that is
// not true. Same rule, and the same reason, as carryFieldDevice().
void copyToWorkspaceRhs(const DeviceBuffer<cfd::Real>& rhs, GpuKrylovWorkspace& workspace);

// The INITIAL GUESS, device to device.
//
// The pressure solve starts from all zeros and uses fill(). The momentum
// solves do not: the host path warm-starts them from the start-of-iteration
// velocity component (`momentumSolver->solve(system, toVector(previousU))`),
// and SolverResult::initialResidual -- which IS SIMPLE's convergence
// measure -- is computed from that guess. A resident momentum solve that
// started anywhere else would report a different residual history, so this
// copies the same bytes the host path uploaded.
void copyToWorkspaceGuess(const DeviceBuffer<cfd::Real>& guess, GpuKrylovWorkspace& workspace);
void copyWorkspaceSolution(const GpuKrylovWorkspace& workspace, cfd::Index count,
                           DeviceBuffer<cfd::Real>& out);

}  // namespace cfd::gpu
