// GPU-DISC-001Q -- production CPU vs GPU performance qualification.
//
// Methodology is INHERITED from benchmarks/gpu/benchmark_cuda_end_to_end.cpp,
// the tool that produced the GPU-PIPE-001 numbers this gate must compare
// against, so old and new are measured the same way:
//
//   case                2D lid-driven cavity (plus the extra cases below)
//   grids               20^2 40^2 80^2 160^2 320^2 640^2
//   outer budgets       200 200 200 60 20 8   -- IDENTICAL on every arm
//   outer tolerances    1e-10, deliberately unreachable, so every run executes
//                       its FULL budget and the arms do identical work. This is
//                       the load-bearing choice: it removes "the GPU converged
//                       in fewer iterations" as a confound.
//   momentum solver     BiCGSTAB 1000 it, abs 1e-8, rel 1e-6, no preconditioner
//   pressure solver     BiCGSTAB 5000 it, abs 1e-7, rel 1e-5, no preconditioner
//   relaxation          velocity 0.7, pressure 0.3
//
// FOUR arms, all measured in ONE session so cross-session drift (~10%, measured
// in GPU-PIPE-001 Phase 1) cannot be mistaken for a speed-up:
//
//   cpu        CPU solver, CPU discretization        -- the reference
//   gpu-pipe   GPU solver, CPU discretization        -- the OLD path
//   gpu-disc   GPU solver, GPU discretization        -- the NEW path
//   disc-only  CPU solver, GPU discretization        -- isolates the change
//
// disc-only exists because without it "the new path is faster" cannot be
// attributed: a change between gpu-pipe and gpu-disc could come from moving
// discretization to the device, or from that move changing what the linear
// solver is handed.
//
// Nothing here weakens a tolerance, shortens a budget, or throttles the CPU.
// The CPU arm keeps OpenMP at its default thread count (the SpMV is the only
// parallel region in src/; it is bit-identical at any thread count), and a
// 1-thread run is available separately via OMP_NUM_THREADS for the record.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// ---------------------------------------------------------------------------
// Cases -- the builders qualified in GPU-DISC-001O, reused unchanged
// ---------------------------------------------------------------------------

struct Case {
  Case(std::string n, Mesh m) : name(std::move(n)), mesh(std::move(m)) {}
  std::string name;
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

// Closed cavity: every patch a wall, the last one moving. No FixedValue
// pressure patch, so production PINS the reference cell.
Case cavity(const std::string& name, Mesh mesh) {
  Case c(name, std::move(mesh));
  const auto& m = c.mesh;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    c.velocityBoundaries.set(
        m, patch.name(),
        lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
            : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::Wall>()));
    c.pressureBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  const Index nc = m.numberOfCells();
  c.velocity = VectorField(nc);
  c.pressure = ScalarField(nc);
  for (Index cell = 0; cell < nc; ++cell) {
    c.velocity[cell] = Vector3{0.0, 0.0, 0.0};
    c.pressure[cell] = 0.0;
  }
  return c;
}

// Open: Inlet, FixedValue-pressure Outlet, Symmetry sides. The pin is
// SUPPRESSED and there is a genuine net through-flow.
Case inletOutlet(const std::string& name, Mesh mesh) {
  Case c(name, std::move(mesh));
  const auto& m = c.mesh;
  std::size_t i = 0;
  const std::size_t patchCount = m.boundaryPatches().size();
  for (const auto& patch : m.boundaryPatches()) {
    if (i == 0) {
      c.velocityBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::Inlet>(Vector3{1.0, 0.0, 0.0}));
      c.pressureBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (i + 1 == patchCount) {
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Outlet>());
      c.pressureBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Symmetry>());
      c.pressureBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    ++i;
  }
  const Index nc = m.numberOfCells();
  c.velocity = VectorField(nc);
  c.pressure = ScalarField(nc);
  const Real pi = cfd::constants::pi;
  for (Index cell = 0; cell < nc; ++cell) {
    const Vector3 x = m.cell(cell).centroid();
    c.velocity[cell] = Vector3{0.9 + (0.1 * std::sin(pi * x.y)), 0.05 * std::sin(pi * x.x),
                               0.02 * std::sin(pi * x.z)};
    c.pressure[cell] = 5.0 * (1.0 - x.x);
  }
  return c;
}

// ---------------------------------------------------------------------------
// Settings -- benchmark_cuda_end_to_end.cpp's, verbatim
// ---------------------------------------------------------------------------

SIMPLESettings benchmarkSettings(Index outerIterations, LinearSolverBackend backend,
                                 bool gpuDiscretization, ConvectionScheme scheme) {
  SIMPLESettings s;
  s.maxIterations = outerIterations;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  // Deliberately unreachable -- every run executes its full budget, so both
  // arms do identical work. Inherited from benchmark_runner.cpp.
  s.velocityTolerance = 1e-10;
  s.pressureTolerance = 1e-10;
  s.continuityTolerance = 1e-10;
  s.convectionScheme = scheme;

  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = backend;
  s.momentumSolver.maxIterations = 1000;
  s.momentumSolver.absoluteTolerance = 1e-8;
  s.momentumSolver.relativeTolerance = 1e-6;
  s.momentumSolver.preconditioner = PreconditionerType::None;

  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = backend;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-7;
  s.pressureSolver.relativeTolerance = 1e-5;
  s.pressureSolver.preconditioner = PreconditionerType::None;

  s.enableGpuDiscretization = gpuDiscretization;
  return s;
}

// ---------------------------------------------------------------------------
// Arms
// ---------------------------------------------------------------------------

struct Arm {
  const char* name;
  LinearSolverBackend backend;
  bool gpuDiscretization;
};

const Arm kArms[] = {
    {"cpu", LinearSolverBackend::CPU, false},
    {"gpu-pipe", LinearSolverBackend::GPU, false},
    {"gpu-disc", LinearSolverBackend::GPU, true},
    {"disc-only", LinearSolverBackend::CPU, true},
};

// ---------------------------------------------------------------------------
// One measured run
// ---------------------------------------------------------------------------

struct RunResult {
  double wallSeconds{};
  SIMPLEResult::StageSeconds stage;
  Index iterations{};
  Index momentumLinearIterations{};
  Index pressureLinearIterations{};
  SIMPLEStatus status{};
  bool gpuDiscretization{};
  Real massImbalance{};
  Real uResidual{}, vResidual{}, pResidual{}, continuityResidual{};
  bool allFinite{};
  // GPU counter deltas across this run.
  std::uint64_t h2dCalls{}, h2dBytes{}, d2hCalls{}, d2hBytes{};
  std::uint64_t allocations{}, reallocations{}, kernels{}, syncs{};
  std::uint64_t peakDeviceBytes{};
  double gpuSolveSeconds{};
  // GPU-PIPE-001 final residency: the transfer cost the stage table has
  // to attribute, and the reduction round trips that must be separated
  // from it. `residentLoop` is the non-vacuity flag -- a speed-up
  // measured on an arm that silently declined residency would be a
  // measurement of something else.
  double uploadSeconds{}, downloadSeconds{};
  std::uint64_t reductionGroups{};
  bool residentLoop{};
  // Absolute device memory, from the driver rather than our own accounting.
  std::uint64_t deviceUsedBytesAfter{};
  // Kept for the equivalence check.
  VectorField velocity;
  ScalarField pressure;
  SurfaceField massFlux;
};

bool fieldsFinite(const SIMPLEResult& r) {
  for (Index i = 0; i < static_cast<Index>(r.pressure.size()); ++i) {
    if (!std::isfinite(r.pressure[i])) return false;
    if (!std::isfinite(r.velocity[i].x) || !std::isfinite(r.velocity[i].y) ||
        !std::isfinite(r.velocity[i].z))
      return false;
  }
  for (Index f = 0; f < static_cast<Index>(r.massFlux.size()); ++f) {
    if (!std::isfinite(r.massFlux[f])) return false;
  }
  return true;
}

std::uint64_t deviceUsedBytes() {
  std::size_t freeBytes = 0, totalBytes = 0;
  if (cudaMemGetInfo(&freeBytes, &totalBytes) != cudaSuccess) return 0;
  return static_cast<std::uint64_t>(totalBytes - freeBytes);
}

RunResult runOnce(const Case& c, Index outerIterations, const Arm& arm, ConvectionScheme scheme,
                  bool keepFields) {
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLESettings settings =
      benchmarkSettings(outerIterations, arm.backend, arm.gpuDiscretization, scheme);
  const SIMPLE simple(settings, /*referenceCell=*/0);

  cfd::Timer wall;
  SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                                c.velocity, c.pressure);
  RunResult out;
  out.wallSeconds = wall.elapsedSeconds();
  out.deviceUsedBytesAfter = deviceUsedBytes();

  const auto& s = cfd::gpu::gpuExecutionStats();
  out.h2dCalls = s.hostToDeviceCalls;
  out.h2dBytes = s.hostToDeviceBytes;
  out.d2hCalls = s.deviceToHostCalls;
  out.d2hBytes = s.deviceToHostBytes;
  out.allocations = s.allocations;
  out.reallocations = s.reallocations;
  out.kernels = s.kernelLaunches;
  out.syncs = s.synchronizations;
  out.peakDeviceBytes = s.peakDeviceBytes;
  out.gpuSolveSeconds = s.gpuSolveSeconds;
  out.uploadSeconds = s.uploadSeconds;
  out.downloadSeconds = s.downloadSeconds;
  out.reductionGroups = s.reductionGroups;

  out.stage = r.stageSeconds;
  out.iterations = r.iterations;
  out.momentumLinearIterations = r.momentumLinearIterations;
  out.pressureLinearIterations = r.pressureLinearIterations;
  out.status = r.status;
  out.gpuDiscretization = r.gpuDiscretization;
  out.residentLoop = r.residentSimpleLoop;
  out.massImbalance = r.globalMassImbalance;
  out.uResidual = r.finalUResidual;
  out.vResidual = r.finalVResidual;
  out.pResidual = r.finalPressureResidual;
  out.continuityResidual = r.finalContinuityResidual;
  out.allFinite = fieldsFinite(r);
  if (keepFields) {
    out.velocity = std::move(r.velocity);
    out.pressure = std::move(r.pressure);
    out.massFlux = std::move(r.massFlux);
  }
  return out;
}

const char* statusName(SIMPLEStatus s) {
  switch (s) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration: return "InvalidConfiguration";
    case SIMPLEStatus::Diverging: return "Diverging";
    case SIMPLEStatus::Stagnated: return "Stagnated";
    default: return "Cancelled";
  }
}

double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

std::ofstream runsCsv;

void writeHeader() {
  runsCsv << "case,grid,cells,faces,arm,scheme,repeat,outer_budget,outer_iterations,"
             "wall_seconds,setup_s,momentum_assembly_s,momentum_solve_s,response_s,"
             "predicted_flux_s,pressure_assembly_s,pressure_solve_s,velocity_correction_s,"
             "face_flux_correction_s,bookkeeping_s,other_s,discretization_s,linear_solve_s,"
             "status,gpu_discretization,all_finite,mass_imbalance,u_residual,v_residual,"
             "p_residual,continuity_residual,momentum_lin_iters,pressure_lin_iters,"
             "h2d_calls,h2d_bytes,d2h_calls,d2h_bytes,allocations,reallocations,kernels,syncs,"
             "peak_device_bytes,device_used_bytes_after,gpu_solve_s,upload_s,download_s,reduction_groups,resident_loop\n";
}

void writeRun(const std::string& caseName, const std::string& grid, Index cells, Index faces,
              const char* arm, const char* scheme, int repeat, Index budget, const RunResult& r) {
  runsCsv << caseName << "," << grid << "," << cells << "," << faces << "," << arm << "," << scheme
          << "," << repeat << "," << budget << "," << r.iterations << "," << r.wallSeconds << ","
          << r.stage.setup << "," << r.stage.momentumAssembly << "," << r.stage.momentumSolve << ","
          << r.stage.responseCoefficients << "," << r.stage.predictedFaceFlux << ","
          << r.stage.pressureAssembly << "," << r.stage.pressureSolve << ","
          << r.stage.velocityCorrection << "," << r.stage.faceFluxCorrection << ","
          << r.stage.bookkeeping << "," << r.stage.other() << "," << r.stage.discretization() << ","
          << r.stage.linearSolve() << "," << statusName(r.status) << ","
          << (r.gpuDiscretization ? 1 : 0) << "," << (r.allFinite ? 1 : 0) << "," << r.massImbalance
          << "," << r.uResidual << "," << r.vResidual << "," << r.pResidual << ","
          << r.continuityResidual << "," << r.momentumLinearIterations << ","
          << r.pressureLinearIterations << "," << r.h2dCalls << "," << r.h2dBytes << ","
          << r.d2hCalls << "," << r.d2hBytes << "," << r.allocations << "," << r.reallocations << ","
          << r.kernels << "," << r.syncs << "," << r.peakDeviceBytes << ","
          << r.deviceUsedBytesAfter << "," << r.gpuSolveSeconds << ","
          << r.uploadSeconds << "," << r.downloadSeconds << "," << r.reductionGroups
          << "," << (r.residentLoop ? 1 : 0) << "\n";
  runsCsv.flush();
}

// ---------------------------------------------------------------------------
// A benchmark point: one case, one grid, every arm, N repeats
// ---------------------------------------------------------------------------

int failures = 0;

// When set, only arms whose name matches are run. Used by the `cpuscale` mode,
// which re-measures ONLY the CPU arm at different OpenMP thread counts --
// re-running the GPU arms there would cost wall-clock time and tell us nothing,
// since OMP_NUM_THREADS does not touch them.
std::string armFilter;

void benchmarkPoint(const Case& c, const std::string& grid, Index budget, int repeats,
                    ConvectionScheme scheme, const char* schemeName, bool equivalenceCheck) {
  const Index cells = c.mesh.numberOfCells();
  const Index faces = c.mesh.numberOfFaces();
  std::printf("\n%-16s %-9s cells=%-8lld budget=%-4lld repeats=%d\n", c.name.c_str(), grid.c_str(),
              static_cast<long long>(cells), static_cast<long long>(budget), repeats);
  std::fflush(stdout);

  std::vector<RunResult> reference;
  for (const Arm& arm : kArms) {
    if (!armFilter.empty() && armFilter != arm.name) continue;
    std::vector<double> wall;
    RunResult last;
    for (int r = 0; r < repeats; ++r) {
      const bool keep = equivalenceCheck && (r == repeats - 1);
      last = runOnce(c, budget, arm, scheme, keep);
      wall.push_back(last.wallSeconds);
      writeRun(c.name, grid, cells, faces, arm.name, schemeName, r, budget, last);

      // Numerical integrity, on EVERY timed run -- a fast wrong answer is not a
      // result. The budget is unreachable by design, so MaxIterations is the
      // expected status and anything else is a finding.
      if (!last.allFinite) {
        std::printf("  FAIL %-10s repeat %d produced a non-finite field\n", arm.name, r);
        ++failures;
      }
      if (last.status != SIMPLEStatus::MaxIterations) {
        std::printf("  FAIL %-10s repeat %d status=%s (expected MaxIterations)\n", arm.name, r,
                    statusName(last.status));
        ++failures;
      }
      if (last.iterations != budget) {
        std::printf("  FAIL %-10s repeat %d ran %lld of %lld outer iterations -- the arms are not "
                    "doing equal work\n",
                    arm.name, r, static_cast<long long>(last.iterations),
                    static_cast<long long>(budget));
        ++failures;
      }
      if (arm.gpuDiscretization && !last.gpuDiscretization) {
        std::printf("  FAIL %-10s repeat %d asked for GPU discretization and did not get it\n",
                    arm.name, r);
        ++failures;
      }
    }
    const double med = median(wall);
    const double lo = *std::min_element(wall.begin(), wall.end());
    const double hi = *std::max_element(wall.begin(), wall.end());
    std::printf("  %-10s median %8.3f s   min %8.3f   max %8.3f   spread %5.1f%%   "
                "disc %6.3f  solve %6.3f\n",
                arm.name, med, lo, hi, med > 0 ? 100.0 * (hi - lo) / med : 0.0,
                last.stage.discretization(), last.stage.linearSolve());
    std::fflush(stdout);
    if (equivalenceCheck) reference.push_back(std::move(last));
  }

  // Numerical equivalence at this benchmark point. A speed-up measured on a
  // different answer is not a speed-up -- but WHICH equality applies depends on
  // what the pair changes, and conflating the two would either assert something
  // the project has never claimed or miss a real discretization defect.
  //
  //   SAME-SOLVER pairs isolate the discretization change and MUST be BITWISE.
  //     cpu      vs disc-only   CPU solver both sides, CPU vs GPU assembly
  //     gpu-pipe vs gpu-disc    GPU solver both sides, CPU vs GPU assembly
  //   The second is the load-bearing one: it is the old-vs-new speed-up pair,
  //   so a bitwise result there means the speed-up is measured on identical
  //   numbers.
  //
  //   CROSS-SOLVER pairs are NOT bitwise by nature: GPU BiCGSTAB reduces in a
  //   different order than the serial CPU sum. GPU-DISC-001N qualified those
  //   against the project's converged tolerance, not bitwise. These runs stop
  //   at a fixed budget far from convergence, where an unconverged intermediate
  //   state legitimately carries a larger difference, so the discrepancy is
  //   REPORTED here and the converged claim is left to full_solve_equivalence,
  //   which is the gate that actually tests it.
  struct Pair {
    std::size_t a, b;
    bool bitwise;
    const char* why;
  };
  static const Pair kPairs[] = {
      {0, 3, true, "same CPU solver -- isolates GPU discretization"},
      {1, 2, true, "same GPU solver -- isolates GPU discretization (old vs new)"},
      {0, 1, false, "cross-solver: CPU vs GPU BiCGSTAB, not bitwise by nature"},
      {0, 2, false, "cross-solver: CPU vs GPU BiCGSTAB, not bitwise by nature"},
  };
  if (equivalenceCheck && reference.size() == 4) {
    for (const Pair& p : kPairs) {
      std::size_t differing = 0;
      Real maxAbs = 0.0;
      const auto& x = reference[p.a];
      const auto& y = reference[p.b];
      for (Index i = 0; i < cells; ++i) {
        const Real d[4] = {std::abs(x.velocity[i].x - y.velocity[i].x),
                           std::abs(x.velocity[i].y - y.velocity[i].y),
                           std::abs(x.velocity[i].z - y.velocity[i].z),
                           std::abs(x.pressure[i] - y.pressure[i])};
        for (double v : d) {
          if (v != 0.0) ++differing;
          maxAbs = std::max(maxAbs, v);
        }
      }
      for (Index f = 0; f < faces; ++f) {
        const Real v = std::abs(x.massFlux[f] - y.massFlux[f]);
        if (v != 0.0) ++differing;
        maxAbs = std::max(maxAbs, v);
      }
      if (p.bitwise) {
        const bool ok = differing == 0;
        if (!ok) ++failures;
        std::printf("  %s equivalence %-10s vs %-10s BITWISE differing=%zu maxAbs=%.3g  (%s)\n",
                    ok ? "PASS" : "FAIL", kArms[p.a].name, kArms[p.b].name, differing, maxAbs,
                    p.why);
      } else {
        std::printf("       report    %-10s vs %-10s differing=%zu maxAbs=%.3g  (%s)\n",
                    kArms[p.a].name, kArms[p.b].name, differing, maxAbs, p.why);
      }
    }
    std::fflush(stdout);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "all";

  std::printf("=== GPU-DISC-001Q: production performance qualification ===\n");
  std::printf("cuda available: %s\n", cfd::gpu::cudaAvailable() ? "yes" : "no");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no usable CUDA device -- cannot measure CPU vs GPU. Exiting.\n");
    return 2;
  }
  std::printf("mode: %s\n", mode.c_str());

  const std::string out = "results/gpu-disc-001/performance/raw/runs-" + mode + ".csv";
  runsCsv.open(out);
  if (!runsCsv) {
    std::printf("cannot open %s\n", out.c_str());
    return 2;
  }
  writeHeader();

  // Warm-up: one untimed GPU solve absorbs CUDA context creation, module load
  // and first-kernel JIT so they do not land inside a measured point. Reported
  // separately rather than hidden -- cold start is a real production cost.
  {
    std::printf("\ncold start (CUDA context, module load, first kernel) -- untimed for every "
                "point below, reported here\n");
    const Case warm = cavity("warmup", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
    cfd::Timer cold;
    (void)runOnce(warm, 3, kArms[2], ConvectionScheme::Upwind, false);
    const double coldSeconds = cold.elapsedSeconds();
    cfd::Timer warmTimer;
    (void)runOnce(warm, 3, kArms[2], ConvectionScheme::Upwind, false);
    const double warmSeconds = warmTimer.elapsedSeconds();
    std::printf("  cold_start_seconds=%.4f  second_identical_run_seconds=%.4f  "
                "one-off cost=%.4f\n",
                coldSeconds, warmSeconds, coldSeconds - warmSeconds);
    std::fflush(stdout);
  }

  // The GPU-PIPE-001 grid series and budgets, unchanged, so the comparison
  // against results/gpu-pipe-001/benchmarks/data/runs.csv is like for like.
  struct GridPoint {
    Index n;
    Index budget;
    int repeats;
  };
  const std::vector<GridPoint> cavityGrids = {
      {20, 200, 5}, {40, 200, 5}, {80, 200, 5}, {160, 60, 3}, {320, 20, 3}, {640, 8, 3},
  };

  if (mode == "all" || mode == "cavity") {
    std::printf("\n########## 2D lid-driven cavity -- the canonical scaling series ##########\n");
    for (const auto& g : cavityGrids) {
      const std::string grid = std::to_string(g.n) + "x" + std::to_string(g.n);
      const Case c = cavity("cavity2d", MeshGeometry::createCartesian2D(g.n, g.n, 1.0, 1.0));
      benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::Upwind, "upwind",
                     /*equivalenceCheck=*/true);
    }
  }

  // GPU-DISC-001R Phase L: a two-point sanity check on the final clean build,
  // at the grids the qualified baseline also holds, so the regression guard can
  // compare like with like. Not a re-qualification.
  if (mode == "smoke") {
    std::printf("\n########## performance smoke: 160^2 and 320^2 ##########\n");
    for (const auto& g : {GridPoint{160, 60, 3}, GridPoint{320, 20, 3}}) {
      const std::string grid = std::to_string(g.n) + "x" + std::to_string(g.n);
      const Case c = cavity("cavity2d", MeshGeometry::createCartesian2D(g.n, g.n, 1.0, 1.0));
      benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::Upwind, "upwind",
                     /*equivalenceCheck=*/true);
    }
  }

  if (mode == "all" || mode == "inletoutlet") {
    std::printf("\n########## 2D inlet/outlet -- open boundaries, real through-flow ##########\n");
    for (const auto& g : {GridPoint{40, 200, 5}, GridPoint{160, 60, 3}, GridPoint{320, 20, 3}}) {
      const std::string grid = std::to_string(g.n) + "x" + std::to_string(g.n);
      const Case c =
          inletOutlet("inletoutlet2d", MeshGeometry::createCartesian2D(g.n, g.n, 4.0, 1.0));
      benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::Upwind, "upwind",
                     /*equivalenceCheck=*/true);
    }
  }

  if (mode == "all" || mode == "3d") {
    std::printf("\n########## 3D cavity -- production 3D SIMPLE (U/V/W) ##########\n");
    for (const auto& g : {GridPoint{12, 60, 3}, GridPoint{24, 30, 3}, GridPoint{40, 12, 3}}) {
      const std::string grid =
          std::to_string(g.n) + "x" + std::to_string(g.n) + "x" + std::to_string(g.n);
      const Case c =
          cavity("cavity3d", MeshGeometry::createCartesian3D(g.n, g.n, g.n, 1.0, 1.0, 1.0));
      benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::Upwind, "upwind",
                     /*equivalenceCheck=*/true);
    }
  }

  if (mode == "all" || mode == "schemes") {
    std::printf("\n########## higher-order convection -- QUICK and LinearUpwind ##########\n");
    for (const auto& g : {GridPoint{160, 60, 3}, GridPoint{320, 20, 3}}) {
      const std::string grid = std::to_string(g.n) + "x" + std::to_string(g.n);
      {
        const Case c =
            cavity("cavity2d-quick", MeshGeometry::createCartesian2D(g.n, g.n, 1.0, 1.0));
        benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::QUICK, "quick",
                       /*equivalenceCheck=*/true);
      }
      {
        const Case c =
            cavity("cavity2d-linup", MeshGeometry::createCartesian2D(g.n, g.n, 1.0, 1.0));
        benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::LinearUpwind,
                       "linear_upwind", /*equivalenceCheck=*/true);
      }
    }
  }

  // CPU-only, at whatever OMP_NUM_THREADS the environment sets. Only the two
  // largest grids, where the OpenMP SpMV is the dominant cost and a thread-count
  // effect is actually visible. Proves the CPU baseline is not throttled.
  if (mode == "cpuscale") {
    std::printf("\n########## CPU thread scaling (cpu arm only) ##########\n");
    armFilter = "cpu";
    // Default 320^2. A grid can be named as argv[2] so the headline 640^2 point
    // can be re-measured against a tuned CPU without paying for the whole
    // sweep -- the 640^2 CPU run is ~90 s serial and is only worth repeating at
    // the thread count that actually turned out to be fastest.
    const Index scaleN = argc > 2 ? static_cast<Index>(std::atoi(argv[2])) : 320;
    const Index scaleBudget = scaleN >= 640 ? 8 : 20;
    for (const auto& g : {GridPoint{scaleN, scaleBudget, 3}}) {
      const std::string grid = std::to_string(g.n) + "x" + std::to_string(g.n);
      const Case c = cavity("cavity2d", MeshGeometry::createCartesian2D(g.n, g.n, 1.0, 1.0));
      benchmarkPoint(c, grid, g.budget, g.repeats, ConvectionScheme::Upwind, "upwind", false);
    }
    armFilter.clear();
  }

  // A single short run of the NEW path, for the profiler to attribute. Kept
  // deliberately small: nsys traces every kernel launch, and the point is which
  // kernels dominate, not how long a long run takes.
  if (mode == "profile") {
    std::printf("\n########## profiling workload: 320x320 cavity, gpu-disc, 10 outer "
                "iterations ##########\n");
    const Case c = cavity("cavity2d", MeshGeometry::createCartesian2D(320, 320, 1.0, 1.0));
    const RunResult r = runOnce(c, 10, kArms[2], ConvectionScheme::Upwind, false);
    writeRun(c.name, "320x320", c.mesh.numberOfCells(), c.mesh.numberOfFaces(), "gpu-disc",
             "upwind", 0, 10, r);
    std::printf("  wall=%.3f s  kernels=%llu  iterations=%lld  status=%s\n", r.wallSeconds,
                static_cast<unsigned long long>(r.kernels), static_cast<long long>(r.iterations),
                statusName(r.status));
    if (r.kernels == 0) {
      std::printf("  FAIL no kernels launched -- the profile would be vacuous\n");
      ++failures;
    }
  }

  runsCsv.close();
  std::printf("\nwrote %s\n", out.c_str());
  std::printf("%s\n", failures == 0 ? "PERFORMANCE BENCHMARK: all integrity checks passed"
                                    : "PERFORMANCE BENCHMARK: INTEGRITY FAILURES");
  return failures == 0 ? 0 : 1;
}
