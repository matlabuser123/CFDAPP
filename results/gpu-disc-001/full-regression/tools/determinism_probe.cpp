// GPU-DISC-001R Phase I -- determinism of repeated production solves.
//
// The project's existing policy is BITWISE for a repeated solve on the same
// backend: GPU-DISC-001M's layer R already asserts "GPU discretization
// repeated, identical", and GPU-DISC-001N compares whole residual histories
// bitwise. No weaker policy is invented for this final gate.
//
// Each arm is solved TWICE and the two results compared across exactly the
// quantities the brief names: convergence state, final fields, residual
// history, mass imbalance, iteration count.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
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

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

struct Case {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
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
    c.velocityBoundaries.set(
        m, patch.name(),
        lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
            : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::Wall>()));
    c.pressureBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  return c;
}

// The project's production tolerances, unchanged. These solves are allowed to
// converge normally -- no artificial budget.
SIMPLESettings settings(LinearSolverBackend backend, bool gpuDiscretization) {
  SIMPLESettings s;
  s.maxIterations = 400;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = backend;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::None;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = backend;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = PreconditionerType::None;
  s.enableGpuDiscretization = gpuDiscretization;
  return s;
}

const char* statusName(SIMPLEStatus s) {
  switch (s) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    default: return "other";
  }
}

void compare(const char* label, const Case& c, const SIMPLEResult& a, const SIMPLEResult& b) {
  std::size_t fieldDiff = 0, historyDiff = 0;
  Real maxAbs = 0.0;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    const Real d[4] = {std::abs(a.velocity[i].x - b.velocity[i].x),
                       std::abs(a.velocity[i].y - b.velocity[i].y),
                       std::abs(a.velocity[i].z - b.velocity[i].z),
                       std::abs(a.pressure[i] - b.pressure[i])};
    for (Real v : d) {
      if (v != 0.0) ++fieldDiff;
      maxAbs = std::max(maxAbs, v);
    }
  }
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    if (!sameBits(a.massFlux[f], b.massFlux[f])) ++fieldDiff;
  }
  const auto cmp = [&](const std::vector<Real>& x, const std::vector<Real>& y) {
    if (x.size() != y.size()) { historyDiff += 1; return; }
    for (std::size_t i = 0; i < x.size(); ++i)
      if (!sameBits(x[i], y[i])) ++historyDiff;
  };
  cmp(a.uResidualHistory, b.uResidualHistory);
  cmp(a.vResidualHistory, b.vResidualHistory);
  cmp(a.pressureResidualHistory, b.pressureResidualHistory);
  cmp(a.continuityHistory, b.continuityHistory);

  const bool ok = fieldDiff == 0 && historyDiff == 0 && a.status == b.status &&
                  a.iterations == b.iterations && sameBits(a.globalMassImbalance,
                                                           b.globalMassImbalance);
  if (!ok) ++failures;
  std::printf("  %s %-24s status=%s/%s  iters=%lld/%lld  fields[d=%zu maxAbs=%.3g]  "
              "history[d=%zu]  massImbalance %s\n",
              ok ? "PASS" : "FAIL", label, statusName(a.status), statusName(b.status),
              static_cast<long long>(a.iterations), static_cast<long long>(b.iterations),
              fieldDiff, maxAbs, historyDiff,
              sameBits(a.globalMassImbalance, b.globalMassImbalance) ? "identical" : "DIFFERS");
}

SIMPLEResult run(const Case& c, LinearSolverBackend backend, bool gpuDisc) {
  const SIMPLE simple(settings(backend, gpuDisc), /*referenceCell=*/0);
  return simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, c.velocity,
                      c.pressure);
}

}  // namespace

int main() {
  std::printf("=== GPU-DISC-001R Phase I: determinism of repeated production solves ===\n");
  std::printf("policy: BITWISE for a repeated solve on the same backend -- the project's own\n");
  std::printf("        (GPU-DISC-001M layer R, GPU-DISC-001N history comparison). Not weakened.\n\n");
  const bool gpu = cfd::gpu::cudaAvailable();
  std::printf("cuda available: %s\n\n", gpu ? "yes" : "no");

  const Case c = cavity(MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
  std::printf("case: 40x40 lid-driven cavity, production tolerances 1e-6, budget 400\n\n");

  {
    const SIMPLEResult a = run(c, LinearSolverBackend::CPU, false);
    const SIMPLEResult b = run(c, LinearSolverBackend::CPU, false);
    compare("CPU solver + CPU disc", c, a, b);
  }
  if (gpu) {
    {
      const SIMPLEResult a = run(c, LinearSolverBackend::GPU, false);
      const SIMPLEResult b = run(c, LinearSolverBackend::GPU, false);
      compare("GPU solver + CPU disc", c, a, b);
    }
    {
      const SIMPLEResult a = run(c, LinearSolverBackend::GPU, true);
      const SIMPLEResult b = run(c, LinearSolverBackend::GPU, true);
      compare("GPU solver + GPU disc", c, a, b);
    }
    {
      const SIMPLEResult a = run(c, LinearSolverBackend::CPU, true);
      const SIMPLEResult b = run(c, LinearSolverBackend::CPU, true);
      compare("CPU solver + GPU disc", c, a, b);
    }
  }

  std::printf("\n%s\n", failures == 0 ? "DETERMINISM: PASS -- every repeated solve is bitwise identical"
                                      : "DETERMINISM: FAIL");
  return failures == 0 ? 0 : 1;
}
