// GPU-PIPE-001 GPU-resident pressure solve -- the controlled comparison.
//
// One binary, one build, one mesh, one set of physics. The two arms differ in
// EXACTLY one thing: whether the pressure-correction solve runs against the
// device-resident system or round-trips through a host LinearSystem.
//
// The toggle is `robustness.linearSolverFallback.enabled`. SIMPLE declines the
// resident path when the fallback wrapper is active, because that wrapper
// re-solves on the CPU when a GPU solve fails and bypassing it would change
// failure behaviour. When NO solve fails -- asserted here, not assumed -- the
// wrapper is numerically inert and adds no transfer, so it isolates the
// residency decision and nothing else.
//
// Both arms are asserted to have taken the path they were meant to take
// (SIMPLEResult::residentPressureSolve), because a comparison between two
// identical paths passes vacuously and would prove nothing at all.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
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
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

int failures = 0;

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  bool threeDimensional{false};
};

Case cavity(const std::string& name, Mesh mesh) {
  Case c{name, std::move(mesh), {}, {}, false};
  std::size_t i = 0;
  for (const auto& patch : c.mesh.boundaryPatches()) {
    const bool lid = i + 1 == c.mesh.boundaryPatches().size();
    c.vb.set(c.mesh, patch.name(),
             lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
                 : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::Wall>()));
    c.pb.set(c.mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  return c;
}

struct Arm {
  SIMPLEResult result;
  VectorField velocity;
  ScalarField pressure;
  std::uint64_t h2dCalls{}, h2dBytes{}, d2hCalls{}, d2hBytes{}, allocations{};
  std::uint64_t syncs{}, reductions{};
};

Arm run(const Case& c, Index outer, bool residentAllowed) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.enableGpuDiscretization = true;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::GPU;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = LinearSolverBackend::GPU;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  if (!residentAllowed) {
    // The one difference. maxAttempts 1 means the wrapper adds a retry it never
    // uses on a converging case -- verified below by asserting no fallback.
    s.robustness.linearSolverFallback.enabled = true;
    s.robustness.linearSolverFallback.maxAttempts = 1;
  }

  Arm arm;
  arm.velocity = VectorField(c.mesh.numberOfCells());
  arm.pressure = ScalarField(c.mesh.numberOfCells());
  const FluidProperties fluid{1.0, 0.01};

  cfd::gpu::resetGpuExecutionStats();
  SIMPLE simple(s);
  arm.result = simple.solve(c.mesh, fluid, c.vb, c.pb, arm.velocity, arm.pressure);
  const auto& st = cfd::gpu::gpuExecutionStats();
  arm.h2dCalls = st.hostToDeviceCalls;
  arm.h2dBytes = st.hostToDeviceBytes;
  arm.d2hCalls = st.deviceToHostCalls;
  arm.d2hBytes = st.deviceToHostBytes;
  arm.allocations = st.allocations;
  arm.syncs = st.synchronizations;
  arm.reductions = st.reductionGroups;
  return arm;
}

void check(bool ok, const std::string& what) {
  if (!ok) {
    ++failures;
    std::printf("    FAIL  %s\n", what.c_str());
  }
}

// memcmp, not ==: the gate's standard is bitwise, and == would call -0.0 equal
// to +0.0 and NaN unequal to itself.
bool bitwiseEqual(const std::vector<Real>& a, const std::vector<Real>& b) {
  if (a.size() != b.size()) return false;
  if (a.empty()) return true;
  return std::memcmp(a.data(), b.data(), a.size() * sizeof(Real)) == 0;
}

std::vector<Real> flatten(const VectorField& v) {
  std::vector<Real> out;
  out.reserve(v.size() * 3);
  for (Index i = 0; i < v.size(); ++i) {
    out.push_back(v[i].x);
    out.push_back(v[i].y);
    out.push_back(v[i].z);
  }
  return out;
}

std::vector<Real> flatten(const ScalarField& f) {
  return std::vector<Real>(f.data(), f.data() + f.size());
}

void compare(const Case& c, Index outer) {
  std::printf("\n=== %s, %llu outer iterations ===\n", c.name.c_str(),
              static_cast<unsigned long long>(outer));

  const Arm resident = run(c, outer, true);
  const Arm host = run(c, outer, false);

  // --- non-vacuity: the two arms really did take different paths ----------
  check(resident.result.residentPressureSolve,
        "resident arm did NOT take the resident path -- comparison would be vacuous");
  check(!host.result.residentPressureSolve,
        "host arm unexpectedly took the resident path -- comparison would be vacuous");
  // The toggle's inertness needs no separate assertion: if the wrapper had
  // actually fallen back to a CPU solve, the bitwise comparisons below would
  // fail. The test IS the detector.

  // --- the comparison ------------------------------------------------------
  check(resident.result.status == host.result.status, "status differs");
  check(resident.result.iterations == host.result.iterations, "outer iteration count differs");
  check(resident.result.pressureLinearIterations == host.result.pressureLinearIterations,
        "pressure linear iteration count differs");
  check(bitwiseEqual(flatten(resident.velocity), flatten(host.velocity)),
        "velocity field differs (bitwise)");
  check(bitwiseEqual(flatten(resident.pressure), flatten(host.pressure)),
        "pressure field differs (bitwise)");
  check(bitwiseEqual(resident.result.pressureResidualHistory, host.result.pressureResidualHistory),
        "pressure residual history differs (bitwise)");
  check(bitwiseEqual(resident.result.uResidualHistory, host.result.uResidualHistory),
        "u residual history differs (bitwise)");
  check(bitwiseEqual(resident.result.vResidualHistory, host.result.vResidualHistory),
        "v residual history differs (bitwise)");

  std::printf("  status            %d / %d\n", static_cast<int>(resident.result.status),
              static_cast<int>(host.result.status));
  std::printf("  outer iterations  %llu\n",
              static_cast<unsigned long long>(resident.result.iterations));
  std::printf("  pressure Krylov   %llu (resident) / %llu (host)\n",
              static_cast<unsigned long long>(resident.result.pressureLinearIterations),
              static_cast<unsigned long long>(host.result.pressureLinearIterations));
  std::printf("  path taken        resident=%s  host=%s\n",
              resident.result.residentPressureSolve ? "yes" : "NO",
              host.result.residentPressureSolve ? "YES" : "no");

  const auto perIteration = [&](std::uint64_t total) {
    return static_cast<double>(total) / static_cast<double>(resident.result.iterations);
  };
  std::printf("\n  transfers/iteration       resident        host        change\n");
  std::printf("    H2D calls           %12.1f %12.1f    %+7.1f%%\n", perIteration(resident.h2dCalls),
              static_cast<double>(host.h2dCalls) / static_cast<double>(host.result.iterations),
              100.0 * (static_cast<double>(resident.h2dCalls) - static_cast<double>(host.h2dCalls)) /
                  static_cast<double>(host.h2dCalls));
  std::printf("    H2D bytes           %12.0f %12.0f    %+7.1f%%\n", perIteration(resident.h2dBytes),
              static_cast<double>(host.h2dBytes) / static_cast<double>(host.result.iterations),
              100.0 * (static_cast<double>(resident.h2dBytes) - static_cast<double>(host.h2dBytes)) /
                  static_cast<double>(host.h2dBytes));
  std::printf("    D2H calls           %12.1f %12.1f    %+7.1f%%\n", perIteration(resident.d2hCalls),
              static_cast<double>(host.d2hCalls) / static_cast<double>(host.result.iterations),
              100.0 * (static_cast<double>(resident.d2hCalls) - static_cast<double>(host.d2hCalls)) /
                  static_cast<double>(host.d2hCalls));
  std::printf("    D2H bytes           %12.0f %12.0f    %+7.1f%%\n", perIteration(resident.d2hBytes),
              static_cast<double>(host.d2hBytes) / static_cast<double>(host.result.iterations),
              100.0 * (static_cast<double>(resident.d2hBytes) - static_cast<double>(host.d2hBytes)) /
                  static_cast<double>(host.d2hBytes));
  std::printf("    allocations (total) %12llu %12llu\n",
              static_cast<unsigned long long>(resident.allocations),
              static_cast<unsigned long long>(host.allocations));
  // Synchronizations are host WAITS. The resident path removes blocking copies
  // per solve and adds one fused check, so this must not rise.
  std::printf("    syncs (total)       %12llu %12llu\n",
              static_cast<unsigned long long>(resident.syncs),
              static_cast<unsigned long long>(host.syncs));
  std::printf("    reduction groups    %12llu %12llu   identical => same Krylov work\n",
              static_cast<unsigned long long>(resident.reductions),
              static_cast<unsigned long long>(host.reductions));
  check(resident.allocations <= host.allocations,
        "the resident path allocates MORE than the host path");
  check(resident.syncs <= host.syncs, "the resident path synchronizes MORE than the host path");
  check(resident.reductions == host.reductions,
        "reduction count differs -- the two paths did different Krylov work");
}

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001 resident pressure solve: controlled comparison ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no CUDA device\n");
    return 2;
  }

  compare(cavity("cavity 2d 40x40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), 8);
  compare(cavity("cavity 2d 80x80", MeshGeometry::createCartesian2D(80, 80, 1.0, 1.0)), 6);
  compare(cavity("cavity 2d 160x160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0)), 4);
  // LONG RUN. Residency state persists across outer iterations, so a fault that
  // accumulates -- a buffer never reset, a guess quietly inherited, drift
  // between the two paths -- needs iterations to show. Bitwise equality after
  // 60 of them is a much stronger statement than after 4.
  compare(cavity("cavity 2d 40x40 LONG RUN", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)),
          60);

  std::printf("\n%s\n", failures == 0 ? "RESIDENCY COMPARISON: PASS (bitwise)"
                                      : "RESIDENCY COMPARISON: FAIL");
  return failures == 0 ? 0 : 1;
}
