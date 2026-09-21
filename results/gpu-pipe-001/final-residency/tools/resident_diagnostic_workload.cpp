// GPU-PIPE-001 Final Residency, Part 6 -- the workload the CUDA sanitizers run.
//
// A sanitizer result means nothing unless the thing under test actually
// executed. This workload is separate from GPU-DISC-001O's because it is
// checking different things: that one examined the DISCRETIZATION kernels;
// this one must exercise
//
//   * many full SIMPLE outer iterations of the RESIDENT loop,
//   * the GPU-resident momentum solves and the GPU-resident pressure solve,
//   * persistent fields carried across iterations,
//   * persistent matrices and the persistent Krylov workspace, reused,
//   * case resize and lifecycle -- grow, shrink, new case, 3D after 2D,
//   * 2D and 3D.
//
// Every workload prints what it executed and asserts it was not vacuous.
// `resident=` is 3 only when the device discretization, the resident pressure
// solve AND the resident SIMPLE loop all ran, so a diagnostics log SHOWS that
// the sanitizer saw the resident path rather than leaving it to be inferred.
//
// usage: resident_diagnostic_workload <mode>
//   cavity2d | case3d | gpusolver | lifecycle | all
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
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
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

int workloads = 0;
int failures = 0;
int residentWorkloads = 0;
std::uint64_t totalKernels = 0;
std::uint64_t totalIterations = 0;

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

Case cavity(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
  const auto& m = c.mesh;
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
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
  const auto& m = c.mesh;
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

SIMPLESettings settings(Index outer, ConvectionScheme scheme) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  // Deliberately unreachable, so the workload runs its whole budget and the
  // sanitizer sees many iterations of the persistent state rather than a case
  // that converges in three.
  s.velocityTolerance = 1e-14;
  s.pressureTolerance = 1e-14;
  s.continuityTolerance = 1e-14;
  s.convectionScheme = scheme;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::GPU;
  s.momentumSolver.maxIterations = 300;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = LinearSolverBackend::GPU;
  s.pressureSolver.maxIterations = 500;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  s.enableGpuDiscretization = true;
  return s;
}

const char* statusName(SIMPLEStatus s) {
  switch (s) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::Diverging: return "Diverging";
    case SIMPLEStatus::Stagnated: return "Stagnated";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration: return "InvalidConfiguration";
    case SIMPLEStatus::Cancelled: return "Cancelled";
  }
  return "?";
}

void runWorkload(const Case& c, const SIMPLESettings& s, const char* note) {
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(s, /*referenceCell=*/0);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
  const auto& stats = cfd::gpu::gpuExecutionStats();
  ++workloads;
  totalKernels += stats.kernelLaunches;
  totalIterations += static_cast<std::uint64_t>(r.iterations);

  const int residency = static_cast<int>(r.gpuDiscretization) +
                        static_cast<int>(r.residentPressureSolve) +
                        static_cast<int>(r.residentSimpleLoop);
  if (residency == 3) ++residentWorkloads;
  // NON-VACUITY: zero kernels means the sanitizer examined nothing, whatever
  // its summary says; residency < 3 means it examined the wrong thing.
  const bool ok = stats.kernelLaunches > 0 && r.iterations > 0 && residency == 3;
  if (!ok) ++failures;

  std::printf("  %s %-28s %-30s outer=%-5lld status=%-14s resident=%d kernels=%llu "
              "h2d=%llu/%lluB d2h=%llu/%lluB sync=%llu alloc=%llu realloc=%llu\n",
              ok ? "RAN " : "VACUOUS", c.name.c_str(), note,
              static_cast<long long>(r.iterations), statusName(r.status), residency,
              (unsigned long long)stats.kernelLaunches,
              (unsigned long long)stats.hostToDeviceCalls,
              (unsigned long long)stats.hostToDeviceBytes,
              (unsigned long long)stats.deviceToHostCalls,
              (unsigned long long)stats.deviceToHostBytes,
              (unsigned long long)stats.synchronizations, (unsigned long long)stats.allocations,
              (unsigned long long)stats.reallocations);
  if (!r.gpuDiscretizationFallbackReason.empty()) {
    std::printf("       fallback reason: %s\n", r.gpuDiscretizationFallbackReason.c_str());
  }
}

// Many outer iterations, so persistent fields, persistent matrices and the
// reused Krylov workspace all get exercised repeatedly -- which is where a
// lifetime or stale-buffer defect surfaces.
void cavity2d() {
  runWorkload(cavity("cavity 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0)),
              settings(40, ConvectionScheme::Upwind), "resident loop, 40 outer");
  runWorkload(inletOutlet("channel 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0)),
              settings(30, ConvectionScheme::Upwind), "resident loop, open boundary");
  runWorkload(cavity("cavity 2d 12 quick", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0)),
              settings(25, ConvectionScheme::QUICK), "resident loop, higher-order convection");
}

// The 3D contract is a different code path, not a bigger one: W momentum,
// 3D gradients, the third response coefficient and the third correction.
void case3d() {
  runWorkload(cavity("cavity 3d 5", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind), "resident loop, 3D, 25 outer");
  runWorkload(inletOutlet("channel 3d 5", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0)),
              settings(20, ConvectionScheme::Upwind), "resident loop, 3D, open boundary");
}

// The reductions -- the only __shared__ memory and __syncthreads in this path.
// With BOTH momentum and pressure resident there are now three resident solves
// per outer iteration sharing one Krylov workspace, which is exactly the
// pattern racecheck and synccheck exist to examine.
void gpusolver() {
  runWorkload(cavity("cavity 2d 8 gpusolve", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind),
              "shared memory + barriers, shared workspace");
  runWorkload(cavity("cavity 3d 4 gpusolve", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)),
              settings(20, ConvectionScheme::Upwind), "3D, four resident solves per iteration");
}

// Resize and lifecycle inside ONE process, so a stale pointer or a buffer
// reused at the wrong length has somewhere to go wrong under the sanitizer.
void lifecycle() {
  runWorkload(cavity("cavity 2d 8 (grow 1/3)", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)),
              settings(12, ConvectionScheme::Upwind), "first size");
  runWorkload(cavity("cavity 2d 20 (grow 2/3)", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0)),
              settings(12, ConvectionScheme::Upwind), "grown -- buffers resize");
  runWorkload(cavity("cavity 2d 8 (shrink 3/3)", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)),
              settings(12, ConvectionScheme::Upwind), "shrunk -- capacity is NOT released");
  runWorkload(inletOutlet("channel 2d 10 (new case)",
                          MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0)),
              settings(12, ConvectionScheme::Upwind), "new case after a resize");
  runWorkload(cavity("cavity 3d 4 (2D -> 3D)",
                     MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)),
              settings(12, ConvectionScheme::Upwind), "3D after 2D -- W becomes live");
  runWorkload(cavity("cavity 2d 8 (3D -> 2D)", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)),
              settings(12, ConvectionScheme::Upwind), "2D after 3D -- W must be zeroed");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? std::string(argv[1]) : std::string("all");
  std::printf("=== GPU-PIPE-001 final residency diagnostic workload: %s ===\n", mode.c_str());
  std::printf("CUDA available: %s\n", cfd::gpu::cudaAvailable() ? "yes" : "no");

  if (mode == "cavity2d" || mode == "all") cavity2d();
  if (mode == "case3d" || mode == "all") case3d();
  if (mode == "gpusolver" || mode == "all") gpusolver();
  if (mode == "lifecycle" || mode == "all") lifecycle();

  std::printf("\nworkloads=%d residentWorkloads=%d outerIterations=%llu kernelLaunches=%llu "
              "failures=%d\n",
              workloads, residentWorkloads, (unsigned long long)totalIterations,
              (unsigned long long)totalKernels, failures);
  if (workloads == 0) {
    std::printf("no workload ran for mode '%s'\n", mode.c_str());
    return 2;
  }
  std::printf("%s\n", failures == 0 ? "DIAGNOSTIC WORKLOAD: RESIDENT GPU PATH EXERCISED"
                                    : "DIAGNOSTIC WORKLOAD: VACUOUS OR FAILED");
  return failures == 0 ? 0 : 1;
}
