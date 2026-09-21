// GPU-PIPE-001 GPU-resident pressure solve -- Phase A BASELINE.
//
// What the host round trip actually costs, measured at the exact boundary the
// gate proposes to remove: the window that begins when the device pressure
// system is assembled and ends when p' is back on the device.
//
//   assemblePressureCorrection()  device system -> host LinearSystem   (D2H x4)
//     toHostSystem()              SparseMatrixBuilder rebuild + sort   (CPU)
//   pressureSolver->solve()       syncMatrix / b / x0 up, solution down
//   setPressureCorrection()       p' back to the device                (H2D x1)
//
// The facade is driven directly through its public API in the same order
// SIMPLE drives it, so the numbers are the production numbers rather than a
// model of them. Counters are snapshotted around the window only -- momentum
// assembly and the corrections are outside it and must not contaminate it.
//
// Also reports the host-side sparsity pattern against the device one. That
// comparison is the gate's central structural question: toHostSystem() drops
// entries whose assembled value is exactly 0.0, so the solver does NOT
// currently see the device matrix -- it sees a compacted copy of it.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::GpuSimpleDiscretization;
using cfd::gpu::gpuExecutionStats;
using cfd::gpu::resetGpuExecutionStats;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

int failures = 0;

struct Snap {
  std::uint64_t h2dCalls{}, h2dBytes{}, d2hCalls{}, d2hBytes{}, allocs{};
};

Snap snap() {
  const auto& s = gpuExecutionStats();
  return Snap{s.hostToDeviceCalls, s.hostToDeviceBytes, s.deviceToHostCalls, s.deviceToHostBytes,
              s.allocations};
}

Snap operator-(const Snap& a, const Snap& b) {
  return Snap{a.h2dCalls - b.h2dCalls, a.h2dBytes - b.h2dBytes, a.d2hCalls - b.d2hCalls,
              a.d2hBytes - b.d2hBytes, a.allocs - b.allocs};
}

BoundaryConditionSet velocityBc(const Mesh& m) {
  BoundaryConditionSet bc;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    bc.set(m, patch.name(),
           lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                     std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
               : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                     std::make_unique<cfd::boundary::Wall>()));
    ++i;
  }
  return bc;
}

BoundaryConditionSet pressureBc(const Mesh& m) {
  BoundaryConditionSet bc;
  for (const auto& patch : m.boundaryPatches())
    bc.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  return bc;
}

void run(Index n) {
  const Mesh mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  const Index nc = mesh.numberOfCells();
  const FluidProperties fluid{1.0, 0.01};
  const auto vbc = velocityBc(mesh);
  const auto pbc = pressureBc(mesh);

  VectorField velocity(nc);
  ScalarField pressure(nc);
  ScalarField viscosity(nc, fluid.dynamicViscosity());
  SurfaceField massFlux(mesh.numberOfFaces());

  GpuSimpleDiscretization gpu;
  std::string reason;
  if (!gpu.prepare(mesh, vbc, pbc, reason)) {
    std::printf("  n=%llu  prepare() refused: %s\n", static_cast<unsigned long long>(n),
                reason.c_str());
    return;
  }
  gpu.uploadInitialState(velocity, pressure, massFlux, viscosity);

  LinearSolverSettings ls;
  ls.type = LinearSolverType::BiCGSTAB;
  ls.preconditioner = PreconditionerType::Jacobi;
  ls.backend = LinearSolverBackend::GPU;
  ls.absoluteTolerance = 1e-12;
  ls.relativeTolerance = 1e-10;
  ls.maxIterations = 1000;
  auto solver = cfd::algebra::makeLinearSolver(ls);

  // Drive one production iteration up to the pressure stage.
  const Real alpha = 0.7;
  const ScalarField zeroComponent(nc);
  auto momentum = [&](Index component) {
    const auto system = gpu.assembleMomentum(component, zeroComponent, alpha, 0, false);
    const auto r = solver->solve(system);
    gpu.setMomentumSolution(component, r.solution);
  };
  momentum(0);
  momentum(1);
  gpu.computeResponseCoefficients(false);
  gpu.computePredictedFaceFlux(true, fluid.density(), alpha, 0, false);

  // ---- the window under audit -------------------------------------------
  const Snap a0 = snap();
  cfd::Timer assembleTimer;
  const auto system = gpu.assemblePressureCorrection(false, 0, fluid.density(), nullptr, false);
  const double assembleSeconds = assembleTimer.elapsedSeconds();
  const Snap a1 = snap();

  cfd::Timer solveTimer;
  const auto pResult = solver->solve(system);
  const double solveSeconds = solveTimer.elapsedSeconds();
  const Snap a2 = snap();

  gpu.setPressureCorrection(pResult.solution);
  const Snap a3 = snap();
  // -----------------------------------------------------------------------

  // A SECOND solve, so steady-state (structure already uploaded) is separated
  // from the first call's one-off structure upload.
  const Snap b0 = snap();
  const auto system2 = gpu.assemblePressureCorrection(false, 0, fluid.density(), nullptr, false);
  const auto pResult2 = solver->solve(system2);
  gpu.setPressureCorrection(pResult2.solution);
  const Snap b1 = snap();

  const Snap assemble = a1 - a0;
  const Snap solve = a2 - a1;
  const Snap back = a3 - a2;
  const Snap steady = b1 - b0;

  // Device sparsity, derived from the assemble download itself rather than
  // recomputed from the mesh -- the plan's structure is what matters, and
  // reading it back out of the measured bytes also cross-checks the counter:
  //   D2H bytes = 8 * ((nc+1) + nnz + nnz + nc)
  const Index deviceNnz = static_cast<Index>((assemble.d2hBytes / 8 - (nc + 1) - nc) / 2);
  // The FULL pattern the plan would carry with no row pinned, for contrast.
  Index fullNnz = 0;
  for (Index c = 0; c < nc; ++c) {
    Index row = 1;  // diagonal
    for (const Index faceId : mesh.cell(c).faceIds())
      if (!mesh.face(faceId).isBoundary()) ++row;
    fullNnz += row;
  }
  const Index hostNnz = static_cast<Index>(system.matrix().nonZeros());

  std::printf("\n=== %llux%llu  cells=%llu ===\n", static_cast<unsigned long long>(n),
              static_cast<unsigned long long>(n), static_cast<unsigned long long>(nc));
  std::printf("  sparsity   full %llu   device nnz %llu   host nnz %llu   dropped %lld  (%s)\n",
              static_cast<unsigned long long>(fullNnz),
              static_cast<unsigned long long>(deviceNnz),
              static_cast<unsigned long long>(hostNnz),
              static_cast<long long>(deviceNnz) - static_cast<long long>(hostNnz),
              deviceNnz == hostNnz ? "IDENTICAL -- device matrix == host matrix"
                                   : "COMPACTED BY toHostSystem");
  std::printf("  %-22s %6s %12s %6s %12s\n", "window", "H2D", "H2D bytes", "D2H", "D2H bytes");
  auto row = [](const char* what, const Snap& s) {
    std::printf("  %-22s %6llu %12llu %6llu %12llu\n", what,
                static_cast<unsigned long long>(s.h2dCalls),
                static_cast<unsigned long long>(s.h2dBytes),
                static_cast<unsigned long long>(s.d2hCalls),
                static_cast<unsigned long long>(s.d2hBytes));
  };
  row("assemble (D2H out)", assemble);
  row("solve (up + down)", solve);
  row("setPressureCorrection", back);
  row("FIRST SOLVE TOTAL", Snap{assemble.h2dCalls + solve.h2dCalls + back.h2dCalls,
                                assemble.h2dBytes + solve.h2dBytes + back.h2dBytes,
                                assemble.d2hCalls + solve.d2hCalls + back.d2hCalls,
                                assemble.d2hBytes + solve.d2hBytes + back.d2hBytes, 0});
  row("STEADY STATE / solve", steady);
  std::printf("  host rebuild (toHostSystem+sort)  %8.2f ms   solve %8.2f ms  (%.1f%% of the pair)\n",
              assembleSeconds * 1e3, solveSeconds * 1e3,
              100.0 * assembleSeconds / (assembleSeconds + solveSeconds));
  std::printf("  steady allocations/solve          %llu\n",
              static_cast<unsigned long long>(steady.allocs));
  std::printf("  pressure iterations %llu / %llu\n",
              static_cast<unsigned long long>(pResult.iterations),
              static_cast<unsigned long long>(pResult2.iterations));

  // A DETECTOR, not only a report. The resident solve is equivalent to the host
  // round trip *because* the device matrix is the same matrix the host solver
  // receives; if the plan stops compacting the pinned row, that stops being
  // true and nothing else in this gate would necessarily notice -- the extra
  // entries are explicit zeros, and adding 0.0 to a sum usually changes
  // nothing. "Usually" is not a basis for a bitwise claim, so the structural
  // property is asserted directly.
  if (deviceNnz != hostNnz) {
    std::printf("    FAIL device nnz %llu != host nnz %llu -- the solver would not see the "
                "matrix the host path sees\n",
                static_cast<unsigned long long>(deviceNnz),
                static_cast<unsigned long long>(hostNnz));
    ++failures;
  }
  // Non-vacuity: a zero-sized or unsolved system satisfies the above trivially.
  if (nc == 0 || hostNnz == 0 || pResult.iterations == 0) {
    std::printf("    FAIL nothing was assembled or solved -- the check is vacuous\n");
    ++failures;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no GPU available\n");
    return 1;
  }
  resetGpuExecutionStats();
  std::vector<Index> grids{40, 160, 320};
  if (argc > 1) {
    grids.clear();
    for (int i = 1; i < argc; ++i) grids.push_back(static_cast<Index>(std::stoull(argv[i])));
  }
  for (const Index n : grids) run(n);
  std::printf("\n%s\n", failures == 0 ? "ROUND-TRIP PROBE: PASS" : "ROUND-TRIP PROBE: FAIL");
  return failures == 0 ? 0 : 1;
}
