// GPU-PIPE-001 Final Residency, Part 1 -- the BEFORE state.
//
// Two jobs, and they are separate on purpose:
//
//   A. Re-verify the persistent-field properties the brief lists, on the tree
//      as it stands now (persistent fields + resident pressure solve), rather
//      than trusting the earlier gate's record of them.
//   B. Measure the transfer baseline per SIMPLE iteration, so the SIMPLE-loop
//      work that follows is measured against a number taken on this build and
//      not against a remembered one.
//
// Per-iteration quantities are fitted from a DELTA between two runs of the
// same case at different outer-iteration budgets. A single run cannot separate
// per-solve setup from per-iteration cost, and the difference between the two
// is exactly what "steady state" means here. Linearity is checked with a third
// point rather than assumed -- a nonlinear fit would mean the "per-iteration"
// figure is meaningless.
//
// usage: residency_baseline [--quick]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Mesh.hpp"
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
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::GpuSimpleDiscretization;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
  bool threeD = false;
};

Case cavity(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}, false};
  const auto& m = c.mesh;
  c.threeD = m.dimension() == 3;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    c.vb.set(m, patch.name(),
             lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
                 : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::Wall>()));
    c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  return c;
}

Case inletOutlet(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}, false};
  const auto& m = c.mesh;
  c.threeD = m.dimension() == 3;
  const std::size_t patchCount = m.boundaryPatches().size();
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    if (i == 0) {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{1.0, 0.0, 0.0}));
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (i + 1 == patchCount) {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Outlet>());
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Wall>());
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  for (Index cell = 0; cell < m.numberOfCells(); ++cell) c.velocity[cell] = Vector3{1.0, 0.0, 0.0};
  return c;
}

// Two arms. `gpuSolvers` selects the PRODUCTION path (device discretization,
// device linear solvers, resident pressure solve); false selects the
// `disc-only` arm, whose CPU linear solvers make every remaining device
// transfer a FIELD transfer -- no Krylov reduction traffic to subtract.
SIMPLESettings settingsFor(bool gpuSolvers, Index outer) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  // Deliberately unreachable: the fit needs both budgets to be EXHAUSTED, or
  // the delta divides by the wrong iteration count.
  s.velocityTolerance = 1e-14;
  s.pressureTolerance = 1e-14;
  s.continuityTolerance = 1e-14;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.maxIterations = 1000;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  s.momentumSolver.backend = gpuSolvers ? LinearSolverBackend::GPU : LinearSolverBackend::CPU;
  s.pressureSolver.backend = gpuSolvers ? LinearSolverBackend::GPU : LinearSolverBackend::CPU;
  s.enableGpuDiscretization = true;
  return s;
}

struct Sample {
  Index iterations = 0;
  std::uint64_t h2dCalls = 0, h2dBytes = 0, d2hCalls = 0, d2hBytes = 0;
  std::uint64_t allocations = 0, reallocations = 0, syncs = 0, kernels = 0, reductions = 0;
  bool resident = false;
  bool gpuDisc = false;
};

Sample run(const Case& c, bool gpuSolvers, Index outer) {
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(settingsFor(gpuSolvers, outer), /*referenceCell=*/0);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
  const auto& st = cfd::gpu::gpuExecutionStats();
  Sample s;
  s.iterations = r.iterations;
  s.h2dCalls = st.hostToDeviceCalls;
  s.h2dBytes = st.hostToDeviceBytes;
  s.d2hCalls = st.deviceToHostCalls;
  s.d2hBytes = st.deviceToHostBytes;
  s.allocations = st.allocations;
  s.reallocations = st.reallocations;
  s.syncs = st.synchronizations;
  s.kernels = st.kernelLaunches;
  s.reductions = st.reductionGroups;
  s.resident = r.residentPressureSolve;
  s.gpuDisc = r.gpuDiscretization;
  return s;
}

struct PerIteration {
  double h2dCalls = 0, h2dBytes = 0, d2hCalls = 0, d2hBytes = 0;
  double allocations = 0, reallocations = 0, syncs = 0, kernels = 0, reductions = 0;
  bool linear = false;
  Index n1 = 0, n2 = 0, n3 = 0;
};

double per(std::uint64_t hi, std::uint64_t lo, double n) {
  return (static_cast<double>(hi) - static_cast<double>(lo)) / n;
}

// Three budgets: the fit uses (a, c) and (a, b) is the linearity control. If a
// counter is not linear in the iteration count, no single "per iteration"
// number describes it and saying one does would be wrong.
PerIteration measure(const Case& c, bool gpuSolvers, Index lo, Index mid, Index hi,
                     bool& engagedResident) {
  (void)run(c, gpuSolvers, 2);  // warm up: first-touch allocation, module load
  const Sample a = run(c, gpuSolvers, lo);
  const Sample b = run(c, gpuSolvers, mid);
  const Sample d = run(c, gpuSolvers, hi);
  engagedResident = d.resident;
  PerIteration p;
  p.n1 = a.iterations;
  p.n2 = b.iterations;
  p.n3 = d.iterations;
  const double nAC = static_cast<double>(d.iterations - a.iterations);
  const double nAB = static_cast<double>(b.iterations - a.iterations);
  p.h2dCalls = per(d.h2dCalls, a.h2dCalls, nAC);
  p.h2dBytes = per(d.h2dBytes, a.h2dBytes, nAC);
  p.d2hCalls = per(d.d2hCalls, a.d2hCalls, nAC);
  p.d2hBytes = per(d.d2hBytes, a.d2hBytes, nAC);
  p.allocations = per(d.allocations, a.allocations, nAC);
  p.reallocations = per(d.reallocations, a.reallocations, nAC);
  p.syncs = per(d.syncs, a.syncs, nAC);
  p.kernels = per(d.kernels, a.kernels, nAC);
  p.reductions = per(d.reductions, a.reductions, nAC);
  // Field traffic is EXACTLY linear; Krylov traffic is not (iteration counts
  // vary per solve), so linearity is asserted only on the counters that must
  // be constant per iteration.
  const double h2dAB = per(b.h2dCalls, a.h2dCalls, nAB);
  p.linear = std::abs(h2dAB - p.h2dCalls) < 1e-9;
  return p;
}

void report(const char* label, const PerIteration& p) {
  std::printf("  %-26s  H2D %7.2f calls %12.1f B | D2H %8.2f calls %12.1f B | "
              "sync %6.2f | alloc %.2f | realloc %.2f | kern %7.2f | red %7.2f\n",
              label, p.h2dCalls, p.h2dBytes, p.d2hCalls, p.d2hBytes, p.syncs, p.allocations,
              p.reallocations, p.kernels, p.reductions);
  std::printf("  %-26s  budgets n=%lld,%lld,%lld  per-iteration linearity: %s\n", "", static_cast<long long>(p.n1), static_cast<long long>(p.n2), static_cast<long long>(p.n3),
              p.linear ? "LINEAR (field traffic constant per iteration)" : "NOT LINEAR");
}

// --- stage-level residency, driven through the facade ----------------------
//
// SIMPLE's own counters cannot say WHICH stage transferred; the facade can,
// because each stage is a separate call. This is what makes "the response
// coefficients stay on the device" a measurement instead of a claim.
void stageResidency(const Case& c) {
  using Field = GpuSimpleDiscretization::PersistentField;
  using Authority = GpuSimpleDiscretization::FieldAuthority;
  GpuSimpleDiscretization gpu;
  std::string reason;
  if (!gpu.prepare(c.mesh, c.vb, c.pb, reason)) {
    std::printf("  FAIL prepare: %s\n", reason.c_str());
    ++failures;
    return;
  }
  const Index nc = c.mesh.numberOfCells();
  ScalarField viscosity(nc);
  for (Index i = 0; i < nc; ++i) viscosity[i] = 0.01;
  SurfaceField flux(c.mesh.numberOfFaces());
  gpu.uploadInitialState(c.velocity, c.pressure, flux, viscosity);

  ScalarField previous(nc);
  const auto& st = cfd::gpu::gpuExecutionStats();

  // one full iteration, stage by stage
  const std::uint64_t d0 = st.deviceToHostCalls;
  (void)gpu.assembleMomentum(0, previous, 0.7, 0, false);
  (void)gpu.assembleMomentum(1, previous, 0.7, 0, false);
  if (c.threeD) (void)gpu.assembleMomentum(2, previous, 0.7, 0, false);
  std::printf("       momentum assembly D2H: +%llu (host LinearSystem round trip)\n",
              static_cast<unsigned long long>(st.deviceToHostCalls - d0));

  cfd::algebra::Vector solution(nc);
  gpu.setMomentumSolution(0, solution);
  gpu.setMomentumSolution(1, solution);
  if (c.threeD) gpu.setMomentumSolution(2, solution);

  const std::uint64_t d1 = st.deviceToHostCalls;
  const std::uint64_t h1 = st.hostToDeviceCalls;
  const std::uint64_t a1 = st.allocations;
  gpu.computeResponseCoefficients(c.threeD);
  std::printf("       responseCoefficients: D2H +%llu  H2D +%llu  alloc +%llu\n",
              static_cast<unsigned long long>(st.deviceToHostCalls - d1),
              static_cast<unsigned long long>(st.hostToDeviceCalls - h1),
              static_cast<unsigned long long>(st.allocations - a1));
  // The residency property is that the coefficients never come BACK -- nothing
  // on the device path reads a host copy. D2H == 0 is that property.
  check(st.deviceToHostCalls == d1,
        "momentum response coefficients: computed and kept on the device, zero D2H");
  // The H2D side of this stage. UPDATED after Part 3, and the history matters:
  //
  // When this probe was written for Part 1 it asserted `== 1`, because on a 2D
  // mesh the stage uploaded a host zero-vector into velocityStar.z every
  // iteration (GPU-DISC-001, so that a 2D solve holds zeros there rather than
  // stale values). That was recorded as a Part 3 elimination candidate, and
  // Part 3 eliminated it with a device fill. The `== 1` was therefore an
  // OBSOLETE CONSTANT -- a pre-change fact, not a property worth preserving --
  // and it is replaced here by the independently derived post-change value:
  // the device writes the zeros itself, so the stage uploads NOTHING, in 2D
  // exactly as in 3D.
  //
  // The pre-change measurement of 1 is preserved in transfers/before.log.
  // The elimination is not taken on trust: it is gated by the transfer guard's
  // C1 (H2D bytes per iteration == 0) and by negative control rl11, which
  // removes the device fill and is DETECTED.
  check(st.hostToDeviceCalls == h1,
        c.threeD ? "3D: the response-coefficient stage uploads nothing"
                 : "2D: the response-coefficient stage uploads nothing -- the W-predictor zeros "
                   "are written on the device (was 1 host upload before Part 3)");

  const std::uint64_t d2 = st.deviceToHostCalls;
  gpu.computePredictedFaceFlux(false, 1.0, 0.7, 0, c.threeD);
  check(st.deviceToHostCalls == d2, "predicted face flux stays resident, zero D2H");

  const std::uint64_t d3 = st.deviceToHostCalls;
  const std::uint64_t a3 = st.allocations;
  gpu.assemblePressureCorrectionResident(false, 0, 1.0, c.threeD);
  check(st.deviceToHostCalls == d3,
        "resident pressure assembly: matrix and RHS stay on the device, zero D2H");

  cfd::algebra::LinearSolverSettings ps;
  ps.type = LinearSolverType::BiCGSTAB;
  ps.backend = LinearSolverBackend::GPU;
  ps.maxIterations = 2000;
  ps.absoluteTolerance = 1e-10;
  ps.relativeTolerance = 1e-8;
  ps.preconditioner = PreconditionerType::Jacobi;
  const auto r1 = gpu.solvePressureCorrectionResident(ps);
  check(r1.solution.size() == 0, "resident solve leaves p' on the device (empty host solution)");
  const std::size_t workspace1 = gpu.residentSolveBytes();
  check(workspace1 > 0, "the resident Krylov workspace is allocated and attributable");

  // A SECOND resident assembly + solve must allocate nothing: that is what
  // "persistent matrix, RHS and workspace" means, and it is the property a
  // re-allocating implementation would fail while still producing the right
  // answer.
  const std::uint64_t a4 = st.allocations;
  const std::uint64_t r4 = st.reallocations;
  gpu.assemblePressureCorrectionResident(false, 0, 1.0, c.threeD);
  const auto r2 = gpu.solvePressureCorrectionResident(ps);
  check(st.allocations == a4 && st.reallocations == r4,
        "repeating assembly + resident solve allocates and reallocates NOTHING");
  check(gpu.residentSolveBytes() == workspace1, "the Krylov workspace is the same memory, reused");
  check(r1.iterations == r2.iterations && r1.status == r2.status,
        "the repeated resident solve reproduces the first exactly");
  (void)a3;

  gpu.correctVelocityResident(0, c.threeD);
  gpu.correctFaceMassFluxResident(false);
  check(gpu.authority(Field::Velocity) == Authority::DeviceOwned,
        "velocity is DeviceOwned after the resident correction");
  check(gpu.authority(Field::MassFlux) == Authority::DeviceOwned,
        "face flux is DeviceOwned after the resident correction");
  gpu.updatePressure(0.3);
  check(gpu.authority(Field::Pressure) == Authority::DeviceOwned,
        "pressure is DeviceOwned after the device update");
  check(gpu.authority(Field::Viscosity) == Authority::Synchronized,
        "viscosity stays Synchronized -- never written by the device");

  // 2D/3D contract: the downloaded field sizes must match the mesh, and W must
  // be zero on a 2D mesh rather than uninitialised.
  VectorField outV;
  gpu.downloadVelocity(outV);
  check(outV.size() == static_cast<std::size_t>(nc), "downloaded velocity has numberOfCells entries");
  if (!c.threeD) {
    bool zeroW = true;
    for (Index i = 0; i < nc; ++i)
      if (outV[i].z != 0.0) zeroW = false;
    check(zeroW, "2D contract: the W component of the resident velocity is exactly zero");
  } else {
    check(true, "3D contract: W is carried as a live component");
  }
  SurfaceField outF;
  gpu.downloadMassFlux(outF);
  check(outF.size() == static_cast<std::size_t>(c.mesh.numberOfFaces()),
        "downloaded face flux has numberOfFaces entries");
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-PIPE-001 final residency, Part 1: persistent fields + transfer baseline ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no CUDA device\n");
    return 2;
  }

  std::printf("\n--- A. stage-level residency, 2D ---\n");
  stageResidency(cavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)));
  std::printf("\n--- A. stage-level residency, 3D ---\n");
  stageResidency(cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0)));

  std::printf("\n--- B. transfer baseline per SIMPLE iteration (BEFORE the SIMPLE-loop work) ---\n");
  struct Row {
    std::string label;
    Case c;
    bool gpuSolvers;
  };
  std::vector<Row> rows;
  rows.push_back({"cavity 2d 16 disc-only",
                  cavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)), false});
  rows.push_back({"cavity 2d 16 production",
                  cavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)), true});
  rows.push_back({"cavity 3d 6  disc-only",
                  cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0)),
                  false});
  rows.push_back({"cavity 3d 6  production",
                  cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0)),
                  true});
  rows.push_back({"channel 2d 16 production",
                  inletOutlet("channel 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)),
                  true});
  if (!quick) {
    rows.push_back({"cavity 2d 160 disc-only",
                    cavity("cavity 2d 160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0)),
                    false});
    rows.push_back({"cavity 2d 160 production",
                    cavity("cavity 2d 160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0)),
                    true});
  }

  for (const Row& row : rows) {
    bool resident = false;
    const PerIteration p = measure(row.c, row.gpuSolvers, 4, 8, 16, resident);
    report(row.label.c_str(), p);
    if (row.gpuSolvers) {
      check(resident, (row.label + ": the resident pressure solve is ENGAGED (non-vacuity)").c_str());
    }
    check(p.allocations == 0.0,
          (row.label + ": steady-state allocations per iteration are exactly 0").c_str());
    check(p.reallocations == 0.0,
          (row.label + ": steady-state reallocations per iteration are exactly 0").c_str());
    if (!row.gpuSolvers) {
      check(p.linear, (row.label + ": field traffic is linear in the iteration count").c_str());
    }
  }

  std::printf("\n%s\n", failures == 0 ? "RESIDENCY BASELINE: PASS" : "RESIDENCY BASELINE: FAIL");
  return failures == 0 ? 0 : 1;
}
