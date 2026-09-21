// GPU-PIPE-001 GPU-resident pressure solve -- failure behaviour.
//
// The resident entry point had to re-implement, on the device, three things the
// host entry point gets for free from host data: SparseMatrix::allFinite(),
// Vector::allFinite(), and computeInverseDiagonal() throwing on a diagonal a
// Jacobi preconditioner cannot use.
//
// Re-implementing a REJECTION is exactly where a "performance" change quietly
// becomes a behavioural one: a resident path that accepts a system the host
// path refuses would run a garbage solve and report success. So every case here
// is run through BOTH entry points on the SAME system and the statuses are
// required to match -- the host path is the reference, as it is everywhere else
// in this project.
//
// Includes healthy systems, because a probe that only ever sees rejections
// cannot tell "rejects correctly" from "rejects everything".
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuResidentSolve.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::algebra::LinearSystem;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::gpu::DeviceBuffer;
using cfd::gpu::DeviceCsrMatrix;
using cfd::gpu::GpuKrylovWorkspace;

namespace {

int failures = 0;

const char* statusName(SolverStatus s) {
  switch (s) {
    case SolverStatus::Converged: return "Converged";
    case SolverStatus::MaxIterations: return "MaxIterations";
    case SolverStatus::Breakdown: return "Breakdown";
    case SolverStatus::NonFiniteInput: return "NonFiniteInput";
    case SolverStatus::NonFiniteResidual: return "NonFiniteResidual";
    case SolverStatus::InvalidSystem: return "InvalidSystem";
  }
  return "?";
}

// A tridiagonal system, with one entry corruptible. Small and explicit so the
// defect under test is the only thing unusual about it.
struct System {
  std::vector<Index> rowOffsets, columnIndices;
  std::vector<Real> values, rhs;
  Index n{0};
};

System tridiagonal(Index n, Real diagonal) {
  System s;
  s.n = n;
  s.rowOffsets.push_back(0);
  for (Index r = 0; r < n; ++r) {
    if (r > 0) {
      s.columnIndices.push_back(r - 1);
      s.values.push_back(-1.0);
    }
    s.columnIndices.push_back(r);
    s.values.push_back(diagonal);
    if (r + 1 < n) {
      s.columnIndices.push_back(r + 1);
      s.values.push_back(-1.0);
    }
    s.rowOffsets.push_back(static_cast<Index>(s.columnIndices.size()));
    s.rhs.push_back(1.0);
  }
  return s;
}

// The exact same numbers, as a host LinearSystem, so the host entry point sees
// the same system rather than an equivalent one.
LinearSystem toHost(const System& s) {
  SparseMatrixBuilder builder(s.n, s.n);
  for (Index r = 0; r < s.n; ++r)
    for (Index k = s.rowOffsets[r]; k < s.rowOffsets[r + 1]; ++k)
      builder.add(r, s.columnIndices[k], s.values[k]);
  Vector b(static_cast<std::size_t>(s.n));
  for (Index i = 0; i < s.n; ++i) b[i] = s.rhs[i];
  return LinearSystem(builder.build(), b);
}

// Runs the resident entry point on `s`, adopting the numbers on the device.
SolverStatus residentStatus(const System& s, const LinearSolverSettings& ls) {
  // --- resident entry point, same numbers, adopted on the device ---------
  DeviceBuffer<Index> rowOffsets, columnIndices;
  DeviceBuffer<Real> values;
  rowOffsets.resize(static_cast<Index>(s.rowOffsets.size()));
  rowOffsets.uploadFrom(s.rowOffsets.data(), static_cast<Index>(s.rowOffsets.size()));
  columnIndices.resize(static_cast<Index>(s.columnIndices.size()));
  columnIndices.uploadFrom(s.columnIndices.data(), static_cast<Index>(s.columnIndices.size()));
  values.resize(static_cast<Index>(s.values.size()));
  values.uploadFrom(s.values.data(), static_cast<Index>(s.values.size()));

  DeviceCsrMatrix matrix;
  matrix.adoptDevice(rowOffsets.data(), columnIndices.data(), values.data(), s.n, s.n,
                     static_cast<Index>(s.values.size()));

  GpuKrylovWorkspace ws;
  ws.resize(s.n);
  Vector hostRhs(static_cast<std::size_t>(s.n));
  for (Index i = 0; i < s.n; ++i) hostRhs[i] = s.rhs[i];
  ws.b.uploadFrom(hostRhs);
  ws.x.uploadFrom(Vector(static_cast<std::size_t>(s.n), 0.0));

  const auto residentResult = cfd::gpu::solveBiCGSTABResident(ls, matrix, ws);

  // The resident path must never hand back a host solution -- that would mean
  // it downloaded one.
  if (!residentResult.solution.empty()) {
    std::printf("    FAIL resident result carries a host solution\n");
    ++failures;
  }
  return residentResult.status;
}

LinearSolverSettings settingsFor(PreconditionerType pc) {
  LinearSolverSettings ls;
  ls.type = LinearSolverType::BiCGSTAB;
  ls.backend = LinearSolverBackend::GPU;
  ls.preconditioner = pc;
  ls.maxIterations = 200;
  return ls;
}

// For systems a host LinearSystem can actually hold: both entry points are run
// and required to agree, with the host as the reference.
void expectSameStatus(const std::string& name, const System& s, PreconditionerType pc) {
  const auto ls = settingsFor(pc);
  const auto solver = cfd::algebra::makeLinearSolver(ls);
  const auto hostStatus = solver->solve(toHost(s)).status;
  const auto resident = residentStatus(s, ls);

  const bool ok = resident == hostStatus;
  if (!ok) ++failures;
  std::printf("  %-42s host %-18s resident %-18s %s\n", name.c_str(), statusName(hostStatus),
              statusName(resident), ok ? "match" : "*** MISMATCH ***");
}

// For systems a host LinearSystem CANNOT hold.
//
// SparseMatrix and Vector validate finiteness in their constructors, so a
// non-finite matrix or RHS is refused before any solver sees it -- which is why
// GpuBiCGSTAB::solveImpl's own allFinite() guards are unreachable through
// normal construction. The resident path has no such constructor: it adopts raw
// device buffers, so its device-side check is the ONLY thing standing between a
// poisoned system and a silently garbage solve. Asserted here directly, and the
// host side is asserted to refuse construction, so the two together show the
// rejection exists on both paths rather than only on one.
void expectHostRefusesAndResidentRejects(const std::string& name, const System& s,
                                         PreconditionerType pc, SolverStatus expected) {
  bool hostRefused = false;
  try {
    (void)toHost(s);
  } catch (const cfd::InvalidArgumentError&) {
    hostRefused = true;
  }
  const auto resident = residentStatus(s, settingsFor(pc));

  const bool ok = hostRefused && resident == expected;
  if (!ok) ++failures;
  std::printf("  %-42s host %-18s resident %-18s %s\n", name.c_str(),
              hostRefused ? "refuses to build" : "*** BUILT IT ***", statusName(resident),
              ok ? "both reject" : "*** MISMATCH ***");
}

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001 resident pressure solve: rejection behaviour ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no CUDA device\n");
    return 2;
  }
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();

  std::printf("\n-- healthy systems (non-vacuity: the probe must ACCEPT these) --\n");
  expectSameStatus("healthy, Jacobi", tridiagonal(64, 4.0), PreconditionerType::Jacobi);
  expectSameStatus("healthy, no preconditioner", tridiagonal(64, 4.0), PreconditionerType::None);

  std::printf("\n-- diagonals a Jacobi preconditioner cannot use --\n");
  {
    System s = tridiagonal(64, 4.0);
    // Row 10 is stored as columns (9, 10, 11), so offset+1 is its DIAGONAL.
    // Only that one entry changes: a second perturbation would leave it
    // ambiguous which one the rejection responded to.
    s.values[s.rowOffsets[10] + 1] = 0.0;
    expectSameStatus("zero diagonal", s, PreconditionerType::Jacobi);
  }
  {
    System s = tridiagonal(64, 4.0);
    s.values[s.rowOffsets[10] + 1] = 1e-20;  // below constants::small = 1e-12
    expectSameStatus("near-zero diagonal", s, PreconditionerType::Jacobi);
  }

  std::printf("\n-- non-finite inputs (a host LinearSystem cannot even hold these) --\n");
  {
    System s = tridiagonal(64, 4.0);
    s.values[s.rowOffsets[10] + 1] = nan;
    expectHostRefusesAndResidentRejects("NaN diagonal", s, PreconditionerType::Jacobi,
                                        SolverStatus::NonFiniteInput);
  }
  {
    System s = tridiagonal(64, 4.0);
    s.values[s.rowOffsets[5]] = inf;  // an OFF-diagonal, so not a diagonal rejection
    expectHostRefusesAndResidentRejects("infinite off-diagonal", s, PreconditionerType::Jacobi,
                                        SolverStatus::NonFiniteInput);
  }
  {
    // Vector does NOT validate finiteness (only SparseMatrix does), so a NaN
    // right-hand side reaches the solver on both paths and both must reject it
    // themselves. Measured, not assumed: the first version of this probe put
    // this case with the two above and the host cheerfully built the system.
    System s = tridiagonal(64, 4.0);
    s.rhs[7] = nan;
    expectSameStatus("NaN right-hand side", s, PreconditionerType::Jacobi);
  }

  std::printf("\n%s\n", failures == 0 ? "REJECTION PROBE: PASS" : "REJECTION PROBE: FAIL");
  return failures == 0 ? 0 : 1;
}
