// GPU-PIPE-001 Final Residency, Part 7 -- lifecycle.
//
// Persistent state is where "works once" and "works every time" come apart.
// Every property below is checked against an AUTHORITY: a run that only ever
// saw the state under test. "It converged" proves nothing about leakage; "it
// produced bit for bit what a solver with no history produced" does.
//
// The pointed one is the SHRINK. DeviceBuffer never shrinks, so after a large
// mesh the capacity from it is still there, and a length bug would read that
// capacity WITHOUT any allocation or transfer to give it away. The only thing
// that can catch it is a fresh solver that never saw the large mesh.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
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

void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++failures;
}

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

SIMPLESettings settings(Index outer, bool gpu) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  s.momentumSolver.backend = gpu ? LinearSolverBackend::GPU : LinearSolverBackend::CPU;
  s.pressureSolver.backend = gpu ? LinearSolverBackend::GPU : LinearSolverBackend::CPU;
  s.enableGpuDiscretization = gpu;
  return s;
}

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

// A whole production solve. Each call constructs and destroys its own SIMPLE,
// which is also the destruction/recreation case -- that is how production uses
// it, so the lifecycle test does not need a different shape to exercise it.
SIMPLEResult solve(const Case& c, Index outer, bool gpu) {
  const SIMPLE simple(settings(outer, gpu), /*referenceCell=*/0);
  return simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
}

bool identical(const Case& c, const SIMPLEResult& a, const SIMPLEResult& b) {
  if (a.status != b.status || a.iterations != b.iterations) return false;
  if (a.uResidualHistory != b.uResidualHistory) return false;
  if (a.vResidualHistory != b.vResidualHistory) return false;
  if (a.wResidualHistory != b.wResidualHistory) return false;
  if (a.pressureResidualHistory != b.pressureResidualHistory) return false;
  if (a.continuityHistory != b.continuityHistory) return false;
  if (a.momentumLinearIterations != b.momentumLinearIterations) return false;
  if (a.pressureLinearIterations != b.pressureLinearIterations) return false;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    if (!sameBits(a.pressure[i], b.pressure[i])) return false;
    if (!sameBits(a.velocity[i].x, b.velocity[i].x)) return false;
    if (!sameBits(a.velocity[i].y, b.velocity[i].y)) return false;
    if (!sameBits(a.velocity[i].z, b.velocity[i].z)) return false;
  }
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    if (!sameBits(a.massFlux[f], b.massFlux[f])) return false;
  }
  return true;
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

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001 final residency: lifecycle ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }

  const Case small = cavity("cavity 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0));
  const Case large = cavity("cavity 2d 24", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0));
  const Case other = inletOutlet("channel 2d 16",
                                 MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  const Case threeD = cavity("cavity 3d 6",
                             MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0));

  // --- AUTHORITIES: taken first, each in a solver with no history ---------
  const SIMPLEResult authoritySmall = solve(small, 3000, true);
  const SIMPLEResult authorityLarge = solve(large, 3000, true);
  const SIMPLEResult authorityOther = solve(other, 3000, true);
  const SIMPLEResult authority3D = solve(threeD, 3000, true);
  const SIMPLEResult authoritySmallCpu = solve(small, 3000, false);
  std::printf("  authorities: small[%s it=%lld] large[%s it=%lld] other[%s it=%lld] 3d[%s it=%lld]\n",
              statusName(authoritySmall.status), static_cast<long long>(authoritySmall.iterations),
              statusName(authorityLarge.status), static_cast<long long>(authorityLarge.iterations),
              statusName(authorityOther.status), static_cast<long long>(authorityOther.iterations),
              statusName(authority3D.status), static_cast<long long>(authority3D.iterations));
  check(authoritySmall.residentSimpleLoop && authorityLarge.residentSimpleLoop &&
            authorityOther.residentSimpleLoop && authority3D.residentSimpleLoop,
        "every GPU authority took the resident production path (non-vacuity)");

  std::printf("\n--- 1. first solve, then a second solve of the SAME case ---\n");
  {
    const SIMPLEResult first = solve(small, 3000, true);
    const SIMPLEResult second = solve(small, 3000, true);
    check(first.status == SIMPLEStatus::Converged, "the first solve converges");
    check(identical(small, first, second),
          "the second solve of the same case reproduces the first bit for bit");
    check(identical(small, second, authoritySmall),
          "and both equal the authority -- no state carried into either");
  }

  std::printf("\n--- 2. a different mesh size, both directions ---\n");
  {
    const SIMPLEResult grow = solve(large, 3000, true);
    check(identical(large, grow, authorityLarge),
          "growing the mesh after a smaller one reproduces the authority exactly");
    // THE SHRINK. Buffers still hold the large mesh's capacity.
    const SIMPLEResult shrink = solve(small, 3000, true);
    check(identical(small, shrink, authoritySmall),
          "shrinking back reproduces the small-mesh authority exactly -- no stale capacity from "
          "the larger mesh is read");
  }

  std::printf("\n--- 3. a NEW case: different boundary conditions and topology ---\n");
  {
    const SIMPLEResult afterCavity = solve(other, 3000, true);
    check(identical(other, afterCavity, authorityOther),
          "an open-boundary case solved after a pinned one reproduces its authority exactly");
    const SIMPLEResult back = solve(small, 3000, true);
    check(identical(small, back, authoritySmall),
          "and returning to the pinned case reproduces its authority exactly");
    const SIMPLEResult after3D = solve(threeD, 3000, true);
    check(identical(threeD, after3D, authority3D),
          "a 3D case solved after 2D ones reproduces its authority exactly (the W component is "
          "not inherited)");
    const SIMPLEResult back2D = solve(small, 3000, true);
    check(identical(small, back2D, authoritySmall),
          "and returning to 2D after 3D reproduces the 2D authority exactly");
  }

  std::printf("\n--- 4. CPU -> GPU and GPU -> CPU switches ---\n");
  {
    const SIMPLEResult cpuFirst = solve(small, 3000, false);
    check(identical(small, cpuFirst, authoritySmallCpu),
          "a CPU solve after GPU solves reproduces the CPU authority exactly");
    const SIMPLEResult gpuAfterCpu = solve(small, 3000, true);
    check(identical(small, gpuAfterCpu, authoritySmall),
          "a GPU solve immediately after a CPU solve reproduces the GPU authority exactly");
    check(!cpuFirst.gpuDiscretization && !cpuFirst.residentSimpleLoop,
          "the CPU arm reports that it used neither the device discretization nor the resident "
          "loop -- the backends stay independent");
  }

  std::printf("\n--- 5. solver destruction and recreation, repeated ---\n");
  {
    cfd::gpu::resetGpuExecutionStats();
    SIMPLEResult last;
    std::uint64_t afterFirst = 0;
    for (int k = 0; k < 5; ++k) {
      last = solve(small, 3000, true);
      if (k == 0) afterFirst = cfd::gpu::gpuExecutionStats().allocations;
    }
    const std::uint64_t afterFive = cfd::gpu::gpuExecutionStats().allocations;
    check(identical(small, last, authoritySmall),
          "the fifth construct-solve-destroy cycle reproduces the authority exactly");
    // Each SIMPLE owns its own facade, so each solve legitimately allocates its
    // buffers once. What must NOT happen is per-solve GROWTH: the later solves
    // must each cost the same as the first, not more.
    const double perSolveLater = static_cast<double>(afterFive - afterFirst) / 4.0;
    std::printf("      allocations: first solve %llu, each later solve %.1f\n",
                (unsigned long long)afterFirst, perSolveLater);
    check(perSolveLater <= static_cast<double>(afterFirst),
          "a later solve allocates no more than the first -- nothing accumulates across solver "
          "lifetimes");
  }

  std::printf("\n--- 6. a FAILED GPU solve, then a valid one ---\n");
  {
    // A 40x40 cavity that does NOT converge inside its budget. What this
    // section needs is a real unsuccessful solve to recover from, and that is
    // all this is.
    //
    // CORRECTION, after the first run: this comment previously called it "the
    // recorded GPU BiCGSTAB restart asymmetry". It is not. That reproducer is
    // defined by its own settings and exits on PressureCorrectionFailure after
    // 1845 iterations; under THIS probe's settings (Jacobi preconditioning,
    // 1e-10/1e-8 linear tolerances) the same mesh runs its full 3000-iteration
    // budget and reports MaxIterations. The assertion below only ever tested
    // `status != Converged`, so the measurement and the result are unaffected --
    // but the label was wrong and is corrected here. The recorded reproducer is
    // exercised properly, under its own settings, by known_debt_probe.cpp and by
    // production_equivalence.cpp's knownReproducer().
    const Case failing = cavity("cavity 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
    const SIMPLEResult failed = solve(failing, 3000, true);
    std::printf("      the failing case reports %s after %lld outer iterations\n",
                statusName(failed.status), static_cast<long long>(failed.iterations));
    check(failed.status != SIMPLEStatus::Converged,
          "the reproducer does fail, so this IS a recovery test and not a vacuous one");
    const SIMPLEResult afterFailure = solve(small, 3000, true);
    check(identical(small, afterFailure, authoritySmall),
          "a valid solve after a failed GPU solve reproduces its authority exactly -- the failure "
          "leaves no residue in the persistent state");
    const SIMPLEResult otherAfterFailure = solve(other, 3000, true);
    check(identical(other, otherAfterFailure, authorityOther),
          "and so does a different case after the failure");
  }

  std::printf("\n%s\n", failures == 0 ? "LIFECYCLE: PASS" : "LIFECYCLE: FAIL");
  return failures == 0 ? 0 : 1;
}
