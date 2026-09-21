// GPU-PIPE-001 GPU-resident pressure solve -- the detector for residency
// regressions that change no number.
//
// Re-uploading a matrix that is already on the device produces bit-for-bit the
// same solution. So does downloading a solution that did not need to come back.
// Every equivalence gate in this project passes both. A transfer count is the
// only thing that can tell them apart, which is exactly why this exists --
// without it, "the pressure solve is resident" is unfalsifiable.
//
// Measures the PRESSURE-SOLVE window specifically, by driving the production
// facade through the same call order SIMPLE uses and snapshotting the counters
// around the assemble/solve/carry-back sequence only. A whole-solve counter
// would bury the pressure solve under the momentum solves' Krylov reductions.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
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
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

// The window is split into three, because they have genuinely different
// budgets and a single combined number would hide the one that matters.
//
//   ASSEMBLE   0 H2D, 0 D2H, 0 bytes.
//              The old path downloaded four arrays here -- rowOffsets, column
//              indices, values and the RHS: 2,447,368 bytes at 160x160. The
//              resident path downloads nothing at all, so the budget is zero
//              and any traffic whatsoever is a failure.
//
//   SOLVE      0 H2D. D2H calls, EXCLUDING Krylov reductions, at most 1.
//              BiCGSTAB reduces every iteration and each reduction is a host
//              round trip by nature -- 842 iterations at 160x160 means ~3,368
//              of them. That traffic is the reduction pattern, not the round
//              trip this gate removes, and the audit put it explicitly out of
//              scope. GPUExecutionStats::reductionGroups counts exactly those,
//              so they are SUBTRACTED rather than budgeted around: the budget
//              then says what it means -- besides reducing, the solve brings
//              back one thing, the fused input/diagonal check.
//
//   CARRY      0 H2D, 1 D2H, 4 bytes -- the p' finiteness guard.
//
// Before this gate the same three windows cost 5 H2D / 1,838,064 bytes and
// 5 non-reduction D2H / 2,652,168 bytes at 160x160.
constexpr std::uint64_t kAssembleH2d = 0;
constexpr std::uint64_t kAssembleD2h = 0;
constexpr std::uint64_t kSolveH2d = 0;
constexpr std::uint64_t kSolveNonReductionD2h = 1;
constexpr std::uint64_t kCarryH2d = 0;
constexpr std::uint64_t kCarryD2h = 1;
// Non-reduction bytes across all three windows. The two host decisions move 12
// bytes; 64 leaves headroom while staying three orders of magnitude below a
// single field (204,800 at 160x160).
constexpr std::uint64_t kMaxNonReductionBytes = 64;
constexpr std::uint64_t kMaxAllocations = 0;

int failures = 0;

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
    std::printf("  prepare refused: %s\n", reason.c_str());
    ++failures;
    return;
  }
  gpu.uploadInitialState(velocity, pressure, massFlux, viscosity);

  LinearSolverSettings ls;
  ls.type = LinearSolverType::BiCGSTAB;
  ls.preconditioner = PreconditionerType::Jacobi;
  ls.backend = LinearSolverBackend::GPU;
  auto solver = cfd::algebra::makeLinearSolver(ls);

  const Real alpha = 0.7;
  const ScalarField zeroComponent(nc);
  auto iterate = [&]() {
    auto momentum = [&](Index component) {
      const auto system = gpu.assembleMomentum(component, zeroComponent, alpha, 0, false);
      gpu.setMomentumSolution(component, solver->solve(system).solution);
    };
    momentum(0);
    momentum(1);
    gpu.computeResponseCoefficients(false);
    gpu.computePredictedFaceFlux(true, fluid.density(), alpha, 0, false);
  };

  // First solve: warms every persistent buffer, so its one-time structure
  // upload and workspace allocation are outside the measured window.
  iterate();
  gpu.assemblePressureCorrectionResident(false, 0, fluid.density(), false);
  (void)gpu.solvePressureCorrectionResident(ls);
  (void)gpu.residentPressureCorrectionAllFinite();

  // Second solve: STEADY STATE, the thing under budget. Each stage is
  // snapshotted separately.
  iterate();
  const auto& st = cfd::gpu::gpuExecutionStats();
  struct Snap {
    std::uint64_t h2d, d2h, bytes, alloc, reductions;
  };
  const auto snap = [&]() {
    return Snap{st.hostToDeviceCalls, st.deviceToHostCalls, st.deviceToHostBytes, st.allocations,
                st.reductionGroups};
  };
  const auto delta = [](const Snap& a, const Snap& b) {
    return Snap{a.h2d - b.h2d, a.d2h - b.d2h, a.bytes - b.bytes, a.alloc - b.alloc,
                a.reductions - b.reductions};
  };

  const Snap s0 = snap();
  gpu.assemblePressureCorrectionResident(false, 0, fluid.density(), false);
  const Snap s1 = snap();
  const auto result = gpu.solvePressureCorrectionResident(ls);
  const Snap s2 = snap();
  const bool finite = gpu.residentPressureCorrectionAllFinite();
  const Snap s3 = snap();

  const Snap assemble = delta(s1, s0);
  const Snap solve = delta(s2, s1);
  const Snap carry = delta(s3, s2);
  const Snap total = delta(s3, s0);
  // Reduction round trips are the BiCGSTAB pattern, not the round trip under
  // audit. Subtracted, not budgeted around -- see the budget comment above.
  const std::uint64_t solveNonReductionD2h = solve.d2h - solve.reductions;

  std::printf("\n=== %llux%llu  cells=%llu ===\n", static_cast<unsigned long long>(n),
              static_cast<unsigned long long>(n), static_cast<unsigned long long>(nc));
  std::printf("  per pressure solve, steady state\n");
  std::printf("    %-10s %6s %6s %12s %s\n", "window", "H2D", "D2H", "D2H bytes", "note");
  std::printf("    %-10s %6llu %6llu %12llu   budget 0 / 0\n", "assemble",
              static_cast<unsigned long long>(assemble.h2d),
              static_cast<unsigned long long>(assemble.d2h),
              static_cast<unsigned long long>(assemble.bytes));
  std::printf("    %-10s %6llu %6llu %12llu   of which %llu are Krylov reductions\n", "solve",
              static_cast<unsigned long long>(solve.h2d),
              static_cast<unsigned long long>(solve.d2h),
              static_cast<unsigned long long>(solve.bytes),
              static_cast<unsigned long long>(solve.reductions));
  std::printf("    %-10s %6llu %6llu %12llu   budget 0 / 1\n", "carry",
              static_cast<unsigned long long>(carry.h2d),
              static_cast<unsigned long long>(carry.d2h),
              static_cast<unsigned long long>(carry.bytes));
  std::printf("    non-reduction D2H calls  %llu   (budget %llu)\n",
              static_cast<unsigned long long>(assemble.d2h + solveNonReductionD2h + carry.d2h),
              static_cast<unsigned long long>(kAssembleD2h + kSolveNonReductionD2h + kCarryD2h));
  std::printf("    allocations              %llu   (budget %llu)\n",
              static_cast<unsigned long long>(total.alloc),
              static_cast<unsigned long long>(kMaxAllocations));

  // NON-VACUITY: a window that solved nothing would trivially satisfy every
  // budget above. Assert the solve actually happened and actually converged.
  if (result.iterations == 0) {
    std::printf("    FAIL the measured window ran no Krylov iterations -- budget is vacuous\n");
    ++failures;
  }
  if (!result.converged()) {
    std::printf("    FAIL the measured solve did not converge (status %d)\n",
                static_cast<int>(result.status));
    ++failures;
  }
  if (!finite) {
    std::printf("    FAIL p' is not finite\n");
    ++failures;
  }
  if (!result.solution.empty()) {
    std::printf("    FAIL the resident solve returned a HOST solution -- it was downloaded\n");
    ++failures;
  }
  std::printf("    Krylov iterations %llu, converged\n",
              static_cast<unsigned long long>(result.iterations));

  if (assemble.h2d > kAssembleH2d || solve.h2d > kSolveH2d || carry.h2d > kCarryH2d) {
    std::printf("    FAIL something was uploaded -- the system is not resident\n");
    ++failures;
  }
  if (assemble.d2h > kAssembleD2h || assemble.bytes > 0) {
    std::printf("    FAIL the assembly still downloads the system\n");
    ++failures;
  }
  if (solveNonReductionD2h > kSolveNonReductionD2h) {
    std::printf("    FAIL the solve brings back more than its input check\n");
    ++failures;
  }
  if (carry.d2h > kCarryD2h) {
    std::printf("    FAIL the carry-back brings back more than the finiteness guard\n");
    ++failures;
  }
  // Bytes, excluding the reduction downloads, which are the only large
  // legitimate traffic left in the window.
  const std::uint64_t nonReductionBytes =
      assemble.bytes + carry.bytes + (solveNonReductionD2h == 0 ? 0 : 8);
  if (nonReductionBytes > kMaxNonReductionBytes) {
    std::printf("    FAIL %llu non-reduction bytes crossed -- that is data, not a decision\n",
                static_cast<unsigned long long>(nonReductionBytes));
    ++failures;
  }
  if (total.alloc > kMaxAllocations) {
    std::printf("    FAIL the pressure solve allocates in steady state\n");
    ++failures;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("=== GPU-PIPE-001 resident pressure solve: transfer guard ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no CUDA device\n");
    return 2;
  }
  std::vector<Index> grids{40, 160};
  if (argc > 1) {
    grids.clear();
    for (int i = 1; i < argc; ++i) grids.push_back(static_cast<Index>(std::stoull(argv[i])));
  }
  for (const Index n : grids) run(n);
  std::printf("\n%s\n", failures == 0 ? "SOLVE TRANSFER GUARD: PASS" : "SOLVE TRANSFER GUARD: FAIL");
  return failures == 0 ? 0 : 1;
}
