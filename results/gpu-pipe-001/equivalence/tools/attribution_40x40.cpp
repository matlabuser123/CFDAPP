// GPU-PIPE-001 -- attribution probe for the 40x40 PressureCorrectionFailure.
//
// The equivalence campaign found that at 40x40, run to convergence with a 3000
// outer budget, the GPU arm exits with SIMPLEStatus::PressureCorrectionFailure
// (3) at outer iteration ~1845 while the CPU arm runs to its budget. That is
// the failure mode GPU-PCORR-001 repaired, so it must be attributed before any
// GPU-PIPE-001 result is reported: is it caused by the GPU-PIPE-001 changes, or
// does it already happen at HEAD?
//
// CLAUDE.md 6: attribution needs TWO libraries. This single source is built
// against both the HEAD worktree and the working tree, and run identically.
//
// usage: attribution_40x40 [edge] [outer]

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLESettings;

int main(int argc, char** argv) {
  const Index edge = argc > 1 ? static_cast<Index>(std::atoi(argv[1])) : 40;
  const Index outer = argc > 2 ? static_cast<Index>(std::atoi(argv[2])) : 3000;

  const Mesh mesh = MeshGeometry::createCartesian2D(edge, edge, 1.0, 1.0);
  BoundaryConditionSet vbc;
  for (const auto& p : mesh.boundaryPatches()) {
    if (p.name() == "top") {
      vbc.set(mesh, p.name(), std::make_unique<cfd::boundary::MovingWall>(cfd::Vector2{1.0, 0.0}));
    } else {
      vbc.set(mesh, p.name(), std::make_unique<cfd::boundary::Wall>());
    }
  }
  BoundaryConditionSet pbc;
  for (const auto& p : mesh.boundaryPatches()) {
    pbc.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }

  for (const auto backend : {LinearSolverBackend::CPU, LinearSolverBackend::GPU}) {
    SIMPLESettings s;
    s.maxIterations = outer;
    s.velocityRelaxation = 0.7;
    s.pressureRelaxation = 0.3;
    s.velocityTolerance = 1e-6;
    s.pressureTolerance = 1e-6;
    s.continuityTolerance = 1e-6;
    s.momentumSolver.type = LinearSolverType::BiCGSTAB;
    s.momentumSolver.backend = backend;
    s.momentumSolver.maxIterations = 1000;
    s.momentumSolver.absoluteTolerance = 1e-10;
    s.momentumSolver.relativeTolerance = 1e-8;
    s.momentumSolver.preconditioner = PreconditionerType::None;
    s.pressureSolver.type = LinearSolverType::BiCGSTAB;
    s.pressureSolver.backend = backend;
    s.pressureSolver.maxIterations = 5000;
    s.pressureSolver.absoluteTolerance = 1e-10;
    s.pressureSolver.relativeTolerance = 1e-8;
    s.pressureSolver.preconditioner = PreconditionerType::None;

    cfd::gpu::resetGpuExecutionStats();
    const FluidProperties fluid(1.0, 0.01);
    cfd::fields::VectorField velocity(mesh.numberOfCells());
    cfd::fields::ScalarField pressure(mesh.numberOfCells());
    const SIMPLE simple(s);
    const auto r = simple.solve(mesh, fluid, vbc, pbc, velocity, pressure);
    const auto& st = cfd::gpu::gpuExecutionStats();
    std::printf(
        "  %-3s status=%d outer=%lld p_lin_it=%lld p_res=%.17g cont=%.3e mass=%.3e "
        "kernels=%llu fallbacks=%llu\n",
        backend == LinearSolverBackend::CPU ? "CPU" : "GPU", static_cast<int>(r.status),
        static_cast<long long>(r.iterations), static_cast<long long>(r.pressureLinearIterations),
        r.finalPressureResidual, r.finalContinuityResidual, r.globalMassImbalance,
        static_cast<unsigned long long>(st.kernelLaunches),
        static_cast<unsigned long long>(st.gpuBackendFallbacks));
    if (!r.robustness.statusDetail.empty()) {
      std::printf("      statusDetail: %s\n", r.robustness.statusDetail.c_str());
    }
    std::fflush(stdout);
  }
  return 0;
}
