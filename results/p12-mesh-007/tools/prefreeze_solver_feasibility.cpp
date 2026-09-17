// P12-MESH-007 pre-freeze feasibility check (NOT a gate result; no MESH-007 code exists yet): can the
// EXISTING static PISO reach the tight linear-solver tolerances the gate will prescribe for its flow runs
// (momentum and pressure BiCGSTAB: absolute 1e-15, relative 1e-12) on the gate's cavity configuration
// (16x16 unit cavity, Re = 100, dt = 0.01, 20 steps, impulsive start)? Prints every step's status,
// continuity residual and global mass imbalance, and the final velocity extrema.
#include <cstdio>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"

using namespace cfd;

int main() {
  const mesh::Mesh m = mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  const physics::FluidProperties fluid(1.0, 0.01);
  boundary::BoundaryConditionSet vbc;
  vbc.set(m, "left", std::make_unique<boundary::Wall>());
  vbc.set(m, "right", std::make_unique<boundary::Wall>());
  vbc.set(m, "bottom", std::make_unique<boundary::Wall>());
  vbc.set(m, "top", std::make_unique<boundary::MovingWall>(Vector2{1.0, 0.0}));
  boundary::BoundaryConditionSet pbc;
  for (const auto& p : m.boundaryPatches()) {
    pbc.set(m, p.name(), std::make_unique<boundary::FixedGradient>(0.0));
  }
  pressure_velocity::PISOSettings settings;
  settings.momentumSolver.absoluteTolerance = 1e-15;
  settings.momentumSolver.relativeTolerance = 1e-12;
  settings.momentumSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-15;
  settings.pressureSolver.relativeTolerance = 1e-12;
  settings.pressureSolver.maxIterations = 20000;
  const pressure_velocity::PISO piso(m, fluid, vbc, pbc, settings, 0);
  solver::TransientState state{fields::VectorField(m.numberOfCells(), Vector2{0.0, 0.0}),
                               fields::ScalarField(m.numberOfCells(), 0.0),
                               fields::SurfaceField(m.numberOfFaces(), 0.0)};
  state.massFlux = physics::calculateMassFlux(m, state.velocity, fluid, vbc);
  int converged = 0;
  for (int step = 1; step <= 20; ++step) {
    const auto r = piso.solveTimeStep(state, 0.01);
    std::printf("step %2d status %d continuity %.3e mass %.3e cfl %.3f\n", step,
                static_cast<int>(r.status), r.continuityResidual, r.massImbalance, r.maxCFL);
    if (r.status != solver::TransientStepStatus::Converged) break;
    ++converged;
    state = r.state;
  }
  Real umin = 1e300, umax = -1e300;
  for (Index i = 0; i < state.velocity.size(); ++i) {
    umin = std::min(umin, state.velocity[i].x);
    umax = std::max(umax, state.velocity[i].x);
  }
  std::printf("converged steps %d of 20; u range [%.6f, %.6f]\n", converged, umin, umax);
  return converged == 20 ? 0 : 1;
}
