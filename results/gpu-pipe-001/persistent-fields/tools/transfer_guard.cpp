// GPU-PIPE-001 Persistent Fields -- a detector for residency regressions that
// do NOT change any number.
//
// This exists because of one specific negative control: re-uploading the whole
// field set every iteration is numerically IDENTICAL to keeping it resident.
// Every equivalence gate in this project passes it. The only thing that can
// tell the difference is a transfer count, so residency needs a detector that
// counts transfers -- otherwise "the fields are resident" is an unfalsifiable
// claim.
//
// Measures per-iteration H2D on the disc-only arm (CPU solver, GPU
// discretization), which isolates FIELD traffic from the GPU solver's Krylov
// reductions, and fails if it exceeds the qualified budget.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
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
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

// Qualified budget, from transfers/after.log on the disc-only arm:
//   6.0 H2D calls per iteration   (2 momentum solutions, 1 p', 2 previous
//                                  components, 1 assembly-internal)
// Before residency it was 12.0. A budget of 8 leaves headroom for a legitimate
// small change while still catching a re-upload of the six-field set, which
// would put it back at 12.
constexpr double kMaxH2DCallsPerIteration = 8.0;
// Per-iteration ALLOCATIONS must be exactly zero -- acceptance criterion 5.
constexpr double kMaxAllocationsPerIteration = 0.0;

struct Case {
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

Case cavity(Mesh mesh) {
  Case c{std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
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

struct Sample {
  Index iterations;
  std::uint64_t h2d, allocations;
};

Sample run(const Case& c, Index outer) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-12;
  s.pressureTolerance = 1e-12;
  s.continuityTolerance = 1e-12;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::CPU;
  s.momentumSolver.maxIterations = 1000;
  s.momentumSolver.absoluteTolerance = 1e-8;
  s.momentumSolver.relativeTolerance = 1e-6;
  s.momentumSolver.preconditioner = PreconditionerType::None;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = LinearSolverBackend::CPU;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-7;
  s.pressureSolver.relativeTolerance = 1e-5;
  s.pressureSolver.preconditioner = PreconditionerType::None;
  s.enableGpuDiscretization = true;
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(s, /*referenceCell=*/0);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
  const auto& st = cfd::gpu::gpuExecutionStats();
  return Sample{r.iterations, st.hostToDeviceCalls, st.allocations};
}

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001: transfer guard (residency regressions that change no number) ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }

  const Case c = cavity(MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  (void)run(c, 2);  // warm-up

  const Sample a = run(c, 4);
  const Sample b = run(c, 16);
  const double n = static_cast<double>(b.iterations - a.iterations);
  const double h2dPer = static_cast<double>(b.h2d - a.h2d) / n;
  const double allocPer = static_cast<double>(b.allocations - a.allocations) / n;

  std::printf("  measured  H2D calls/iteration   %.2f   (budget %.1f)\n", h2dPer,
              kMaxH2DCallsPerIteration);
  std::printf("  measured  allocations/iteration %.2f   (budget %.1f)\n", allocPer,
              kMaxAllocationsPerIteration);

  int failures = 0;
  if (h2dPer > kMaxH2DCallsPerIteration) {
    std::printf("  FAIL per-iteration H2D exceeds the qualified residency budget -- fields are "
                "being re-uploaded\n");
    ++failures;
  }
  if (allocPer > kMaxAllocationsPerIteration) {
    std::printf("  FAIL per-iteration allocations are not zero\n");
    ++failures;
  }
  std::printf("%s\n", failures == 0 ? "TRANSFER GUARD: PASS" : "TRANSFER GUARD: FAIL");
  return failures == 0 ? 0 : 1;
}
