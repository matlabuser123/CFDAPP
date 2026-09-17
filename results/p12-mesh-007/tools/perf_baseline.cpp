// P12-MESH-007 performance baseline (acceptance_gate.md, "Performance (recorded after the gate;
// measurement only)"). Release build, one thread, no optimization work. Reports the median
// wall-clock time per step of:
//   (1) MeshMotion::advance -- geometry update, swept volumes and GCL residuals -- for a
//       sinusoidal deformation (the gate's SN2 / SN3) on 2D 128^2 and 256^2 and 3D 32^3 and 64^3;
//   (2) one static PISO step and one AlePISO step (SN2, walls sliding tangentially only) of the
//       2D 128^2 lid-driven cavity, with the gate's solver settings.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/AlePISO.hpp"
#include "cfd/pressure_velocity/PISO.hpp"

using namespace cfd;
using Clock = std::chrono::steady_clock;

namespace {

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}
double lowest(const std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); }
double highest(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); }

void geometry(const char* label, mesh::Mesh m, const Vector3& amplitude, int dim) {
  const Vector3 upper = dim == 3 ? Vector3{1, 1, 1} : Vector3{1, 1, 0};
  mesh::MeshMotion motion(m, std::make_shared<mesh::SinusoidalMotion>(Vector3{0, 0, 0}, upper,
                                                                      amplitude,
                                                                      constants::twoPi / 0.4));
  std::vector<double> ms;
  for (int n = 1; n <= 20; ++n) {
    const auto t0 = Clock::now();
    const auto& step = motion.advance(n * 0.02);
    const auto t1 = Clock::now();
    ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    (void)step;
  }
  std::printf("GEOM %-10s cells %8zu faces %8zu | advance (geometry + swept volumes + GCL) median "
              "%9.3f ms/step, min %9.3f, max %9.3f, first %9.3f ms, max GCL %.2e\n",
              label, m.numberOfCells(), m.numberOfFaces(), median(ms), lowest(ms), highest(ms),
              ms.front(), motion.lastStep().maxAbsGclResidual);
}

pressure_velocity::PISOSettings settings() {
  pressure_velocity::PISOSettings s;
  s.momentumSolver.absoluteTolerance = 1e-15;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-15;
  s.pressureSolver.relativeTolerance = 1e-12;
  s.pressureSolver.maxIterations = 20000;
  return s;
}

void solvers(Index n) {
  const physics::FluidProperties fluid(1.0, 0.01);
  const Real dt = 0.25 / static_cast<Real>(n);  // CFL ~ 0.25 at the lid
  auto make = [&](mesh::Mesh& m, boundary::BoundaryConditionSet& vbc,
                  boundary::BoundaryConditionSet& pbc) {
    for (const auto& patch : m.boundaryPatches()) {
      if (patch.name() == "top") {
        vbc.set(m, patch.name(), std::make_unique<boundary::MovingWall>(Vector3{1, 0, 0}));
      } else {
        vbc.set(m, patch.name(), std::make_unique<boundary::Wall>());
      }
      pbc.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  };
  // static PISO
  {
    mesh::Mesh m = mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
    boundary::BoundaryConditionSet vbc;
    boundary::BoundaryConditionSet pbc;
    make(m, vbc, pbc);
    const pressure_velocity::PISO piso(m, fluid, vbc, pbc, settings());
    fields::VectorField u(m.numberOfCells(), Vector3{});
    solver::TransientState state{u, fields::ScalarField(m.numberOfCells(), 0.0),
                                 physics::calculateMassFlux(m, u, fluid, vbc)};
    std::vector<double> ms;
    for (int s = 0; s < 10; ++s) {
      const auto t0 = Clock::now();
      auto r = piso.solveTimeStep(state, dt);
      const auto t1 = Clock::now();
      if (r.status != solver::TransientStepStatus::Converged) {
        std::printf("PISO step %d failed\n", s);
        return;
      }
      state = std::move(r.state);
      ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::printf("SOLV %4zu^2 static PISO   median %9.3f ms/step, min %9.3f, max %9.3f (10 steps, dt %.4g)\n",
                static_cast<std::size_t>(n), median(ms), lowest(ms), highest(ms), dt);
  }
  // AlePISO with the gate's SN2 deformation (vanishes on the walls, so they only slide)
  {
    mesh::Mesh m = mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
    boundary::BoundaryConditionSet vbc;
    boundary::BoundaryConditionSet pbc;
    make(m, vbc, pbc);
    mesh::MeshMotion motion(m, std::make_shared<mesh::SinusoidalMotion>(
                                   Vector3{0, 0, 0}, Vector3{1, 1, 0}, Vector3{0.05, 0.05, 0},
                                   constants::twoPi / 0.4));
    const pressure_velocity::AlePISO ale(motion, fluid, vbc, pbc, settings());
    fields::VectorField u(m.numberOfCells(), Vector3{});
    solver::TransientState state{u, fields::ScalarField(m.numberOfCells(), 0.0),
                                 physics::calculateMassFlux(m, u, fluid, vbc)};
    std::vector<double> ms;
    for (int s = 0; s < 10; ++s) {
      const auto t0 = Clock::now();
      auto r = ale.solveTimeStep(state, dt);
      const auto t1 = Clock::now();
      if (r.status != solver::TransientStepStatus::Converged) {
        std::printf("AlePISO step %d failed\n", s);
        return;
      }
      state = std::move(r.state);
      ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::printf("SOLV %4zu^2 AlePISO (SN2) median %9.3f ms/step, min %9.3f, max %9.3f (10 steps, dt %.4g)\n",
                static_cast<std::size_t>(n), median(ms), lowest(ms), highest(ms), dt);
  }
}

}  // namespace

int main() {
  std::printf("# P12-MESH-007 performance baseline (Release, one thread)\n");
  geometry("2D 128^2", mesh::MeshGeometry::createCartesian2D(128, 128, 1.0, 1.0),
           Vector3{0.05, 0.05, 0}, 2);
  geometry("2D 256^2", mesh::MeshGeometry::createCartesian2D(256, 256, 1.0, 1.0),
           Vector3{0.05, 0.05, 0}, 2);
  geometry("3D 32^3", mesh::MeshGeometry::createCartesian3D(32, 32, 32, 1.0, 1.0, 1.0),
           Vector3{0.05, 0.025, -0.0375}, 3);
  geometry("3D 64^3", mesh::MeshGeometry::createCartesian3D(64, 64, 64, 1.0, 1.0, 1.0),
           Vector3{0.05, 0.025, -0.0375}, 3);
  solvers(128);
  return 0;
}
