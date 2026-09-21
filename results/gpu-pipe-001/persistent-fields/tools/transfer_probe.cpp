// GPU-PIPE-001 Persistent Fields -- what actually crosses the PCIe boundary,
// per outer SIMPLE iteration, on the production GPU path.
//
// Method: run the SAME case at several outer budgets and fit
//
//     calls(n) = setup + perIteration * n
//
// Two budgets determine both terms exactly; a third confirms the model is
// linear (it must be -- anything else means something allocates or transfers
// as a function of iteration index, which is itself a finding).
//
// This is the BEFORE measurement. The same probe is re-run after the residency
// work and the two are compared, so "avoidable per-iteration transfers removed"
// is a measured delta rather than a claim.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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

SIMPLESettings settings(Index outer, LinearSolverBackend backend, bool gpuDisc) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  // Unreachable, so every run executes its FULL budget and calls(n) is a clean
  // function of n.
  s.velocityTolerance = 1e-12;
  s.pressureTolerance = 1e-12;
  s.continuityTolerance = 1e-12;
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
  s.enableGpuDiscretization = gpuDisc;
  return s;
}

struct Sample {
  Index iterations;
  std::uint64_t h2dCalls, h2dBytes, d2hCalls, d2hBytes, allocations, reallocations;
};

Sample run(const Case& c, Index outer, LinearSolverBackend backend, bool gpuDisc) {
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(settings(outer, backend, gpuDisc), /*referenceCell=*/0);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
  const auto& s = cfd::gpu::gpuExecutionStats();
  return Sample{r.iterations, s.hostToDeviceCalls, s.hostToDeviceBytes, s.deviceToHostCalls,
                s.deviceToHostBytes, s.allocations, s.reallocations};
}

void report(const char* label, const Case& c, LinearSolverBackend backend, bool gpuDisc) {
  const Index nc = c.mesh.numberOfCells();
  const Index nf = c.mesh.numberOfFaces();
  std::printf("\n--- %s  (cells=%lld faces=%lld) ---\n", label, static_cast<long long>(nc),
              static_cast<long long>(nf));
  // Warm-up so context creation is not attributed to a measured run.
  (void)run(c, 2, backend, gpuDisc);

  std::vector<Sample> s;
  for (Index n : {4, 8, 16}) s.push_back(run(c, n, backend, gpuDisc));

  std::printf("  %6s %10s %12s %10s %12s %8s %9s\n", "iters", "H2D calls", "H2D bytes",
              "D2H calls", "D2H bytes", "allocs", "reallocs");
  for (const Sample& x : s) {
    std::printf("  %6lld %10llu %12llu %10llu %12llu %8llu %9llu\n",
                static_cast<long long>(x.iterations),
                static_cast<unsigned long long>(x.h2dCalls),
                static_cast<unsigned long long>(x.h2dBytes),
                static_cast<unsigned long long>(x.d2hCalls),
                static_cast<unsigned long long>(x.d2hBytes),
                static_cast<unsigned long long>(x.allocations),
                static_cast<unsigned long long>(x.reallocations));
  }

  // Fit from the two extreme points; the middle one checks linearity.
  const auto fit = [&](std::uint64_t Sample::*field, const char* name, double perUnit) {
    const double y0 = static_cast<double>(s.front().*field);
    const double y2 = static_cast<double>(s.back().*field);
    const double n0 = static_cast<double>(s.front().iterations);
    const double n2 = static_cast<double>(s.back().iterations);
    const double per = (y2 - y0) / (n2 - n0);
    const double setup = y0 - per * n0;
    const double predictMid = setup + per * static_cast<double>(s[1].iterations);
    const double actualMid = static_cast<double>(s[1].*field);
    const bool linear = std::abs(predictMid - actualMid) < 1e-6 * std::max(1.0, actualMid);
    std::printf("  %-11s setup=%12.0f  per-iteration=%10.1f%s   linear=%s\n", name, setup, per,
                perUnit > 0 ? "" : "", linear ? "yes" : "NO -- iteration-dependent!");
  };
  fit(&Sample::h2dCalls, "H2D calls", 0);
  fit(&Sample::h2dBytes, "H2D bytes", 0);
  fit(&Sample::d2hCalls, "D2H calls", 0);
  fit(&Sample::d2hBytes, "D2H bytes", 0);
  fit(&Sample::allocations, "allocations", 0);

  // What a single full field costs, so the per-iteration figure can be read as
  // "N fields' worth".
  std::printf("  one cell-field = %lld bytes, one face-field = %lld bytes\n",
              static_cast<long long>(nc) * 8, static_cast<long long>(nf) * 8);
}

}  // namespace

int main(int argc, char** argv) {
  const Index n = argc > 1 ? std::atoi(argv[1]) : 160;
  std::printf("=== GPU-PIPE-001 persistent fields: transfer profile ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }
  std::printf("case: %lldx%lld lid-driven cavity\n", static_cast<long long>(n),
              static_cast<long long>(n));

  const Case c = cavity(MeshGeometry::createCartesian2D(n, n, 1.0, 1.0));
  report("gpu-disc (GPU solver + GPU discretization)", c, LinearSolverBackend::GPU, true);
  report("disc-only (CPU solver + GPU discretization)", c, LinearSolverBackend::CPU, true);
  report("gpu-pipe (GPU solver + CPU discretization)", c, LinearSolverBackend::GPU, false);
  return 0;
}
