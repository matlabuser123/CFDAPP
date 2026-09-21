// GPU-DISC-001O -- the production workload the CUDA sanitizers run.
//
// Drives `SIMPLE::solve` with enableGpuDiscretization, i.e. the real integrated
// production path qualified by GPU-DISC-001M and 001N. Nothing here is an
// isolated kernel harness.
//
// Every mode prints what it actually executed -- outer iterations, kernel
// launches, transfers -- so NON-VACUITY is provable from the sanitizer log
// itself rather than asserted. A run that exits before meaningful CUDA work
// reports zero kernels and fails.
//
// The `gpusolver` mode additionally puts the LINEAR SOLVER on the GPU. That is
// not decoration: DeviceVectorOpsKernel.cu's reductions are the ONLY code in
// the entire GPU path with `__shared__` memory and `__syncthreads`, so they are
// the only thing synccheck and racecheck can examine. See audit.md section 1.
//
// usage: diagnostic_workload <mode>
//   cavity2d | inletoutlet2d | case3d | schemes | nonorthogonal | gpusolver | all

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::discretization::GradientScheme;
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

int failures = 0;
int workloads = 0;
std::uint64_t totalKernels = 0;
Index totalIterations = 0;

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

Mesh warped2D(Index n) {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real x = static_cast<Real>(i) / static_cast<Real>(n);
      const Real y = static_cast<Real>(j) / static_cast<Real>(n);
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, v);
}

// Closed: every patch a wall, the last one moving. No FixedValue pressure
// patch, so production PINS the reference cell.
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

SIMPLESettings settings(Index outerIterations, ConvectionScheme scheme, GradientScheme gradient,
                        bool gpuLinearSolver) {
  SIMPLESettings s;
  s.maxIterations = outerIterations;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.convectionScheme = scheme;
  s.gradientScheme = gradient;
  s.enableGpuDiscretization = true;
  if (gpuLinearSolver) {
    s.momentumSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
    s.pressureSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
  }
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
  SIMPLE simple(s, /*referenceCell=*/0);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                                      c.velocity, c.pressure);
  const auto& stats = cfd::gpu::gpuExecutionStats();
  ++workloads;
  totalKernels += stats.kernelLaunches;
  totalIterations += r.iterations;

  // NON-VACUITY: the production GPU path must actually have run. Zero kernels
  // means the sanitizer examined nothing, whatever its summary says.
  const bool ranOnDevice = stats.kernelLaunches > 0 && r.gpuDiscretization;
  const bool ok = ranOnDevice && r.iterations > 0;
  if (!ok) ++failures;

  std::printf("  %s %-26s %-22s outer=%-5zu status=%-14s gpuDisc=%d kernels=%llu "
              "h2d=%llu/%lluB d2h=%llu/%lluB sync=%llu alloc=%llu\n",
              ok ? "RAN " : "VACUOUS", c.name.c_str(), note,
              static_cast<std::size_t>(r.iterations), statusName(r.status),
              // GPU-PIPE-001: printed as 2 when the pressure solve additionally
              // ran device-resident, so a diagnostics log SHOWS whether the
              // resident kernels were among what the sanitizer examined rather
              // than leaving it to be inferred from a kernel count.
              static_cast<int>(r.gpuDiscretization) +
                  static_cast<int>(r.residentPressureSolve),
              static_cast<unsigned long long>(stats.kernelLaunches),
              static_cast<unsigned long long>(stats.hostToDeviceCalls),
              static_cast<unsigned long long>(stats.hostToDeviceBytes),
              static_cast<unsigned long long>(stats.deviceToHostCalls),
              static_cast<unsigned long long>(stats.deviceToHostBytes),
              static_cast<unsigned long long>(stats.synchronizations),
              static_cast<unsigned long long>(stats.allocations));
  if (!r.gpuDiscretizationFallbackReason.empty()) {
    std::printf("       fallback reason: %s\n", r.gpuDiscretizationFallbackReason.c_str());
  }
}

void cavity2d() {
  // Multi-iteration: 40 outer iterations, so buffers are reused, plans persist
  // across iterations, and a stale-buffer or lifetime defect has room to show.
  runWorkload(cavity("cavity 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0)),
              settings(40, ConvectionScheme::Upwind, GradientScheme::GreenGauss, false),
              "closed, pinned, 40 outer");
}

void inletOutlet2d() {
  runWorkload(inletOutlet("channel 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0)),
              settings(40, ConvectionScheme::Upwind, GradientScheme::GreenGauss, false),
              "open, pin suppressed");
}

void case3d() {
  runWorkload(cavity("cavity 3d 5", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind, GradientScheme::GreenGauss, false),
              "3D U/V/W, GG gradient");
  // Least squares reaches the packed 2D layout and the 3D cofactor/adjugate
  // path -- neither is touched by the Green-Gauss default.
  runWorkload(cavity("cavity 3d 5 ls", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind, GradientScheme::LeastSquares, false),
              "3D cofactor/adjugate path");
  runWorkload(inletOutlet("channel 3d 5", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind, GradientScheme::GreenGauss, false),
              "3D open boundary");
}

void schemes() {
  runWorkload(cavity("cavity 2d 10 quick", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0)),
              settings(25, ConvectionScheme::QUICK, GradientScheme::GreenGauss, false), "QUICK");
  runWorkload(cavity("cavity 2d 10 linup", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0)),
              settings(25, ConvectionScheme::LinearUpwind, GradientScheme::GreenGauss, false),
              "LinearUpwind");
  runWorkload(cavity("cavity 2d 10 central", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0)),
              settings(25, ConvectionScheme::Central, GradientScheme::GreenGauss, false),
              "Central");
  // The 2D packed least-squares coefficient layout.
  runWorkload(cavity("cavity 2d 10 ls", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind, GradientScheme::LeastSquares, false),
              "2D packed LS layout");
}

void nonorthogonal() {
  runWorkload(cavity("cavity 2d 12 warped", warped2D(12)),
              settings(30, ConvectionScheme::Upwind, GradientScheme::GreenGauss, false),
              "skew sweeps, oblique BC");
  runWorkload(cavity("cavity 2d 12 warped ls", warped2D(12)),
              settings(30, ConvectionScheme::Upwind, GradientScheme::LeastSquares, false),
              "oblique-Neumann LS entries");
}

void gpusolver() {
  // THE reason synccheck and racecheck are not vacuous here: the linear solver
  // on the device brings in DeviceVectorOpsKernel's reductions, the only
  // __shared__ memory and __syncthreads in the whole GPU path (audit.md S1).
  // A small mesh, well away from the recorded 40x40 BiCGSTAB reproducer.
  runWorkload(cavity("cavity 2d 8 gpusolve", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind, GradientScheme::GreenGauss, true),
              "GPU SOLVER: shared mem + barriers");
  runWorkload(inletOutlet("channel 2d 8 gpusolve",
                          MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)),
              settings(25, ConvectionScheme::Upwind, GradientScheme::GreenGauss, true),
              "GPU SOLVER, open boundary");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? std::string(argv[1]) : std::string("all");
  std::printf("=== GPU-DISC-001O diagnostic workload: %s ===\n", mode.c_str());
  std::printf("CUDA available: %s\n", cfd::gpu::cudaAvailable() ? "yes" : "no");

  if (mode == "cavity2d" || mode == "all") cavity2d();
  if (mode == "inletoutlet2d" || mode == "all") inletOutlet2d();
  if (mode == "case3d" || mode == "all") case3d();
  if (mode == "schemes" || mode == "all") schemes();
  if (mode == "nonorthogonal" || mode == "all") nonorthogonal();
  if (mode == "gpusolver" || mode == "all") gpusolver();

  std::printf("\nworkloads=%d outerIterations=%zu kernelLaunches=%llu failures=%d\n", workloads,
              static_cast<std::size_t>(totalIterations),
              static_cast<unsigned long long>(totalKernels), failures);
  // The sanitizer log is only meaningful if this line says the path ran.
  std::printf("%s\n", failures == 0 ? "DIAGNOSTIC WORKLOAD: PRODUCTION GPU PATH EXERCISED"
                                    : "DIAGNOSTIC WORKLOAD: VACUOUS -- the GPU path did not run");
  return failures == 0 ? 0 : 1;
}
