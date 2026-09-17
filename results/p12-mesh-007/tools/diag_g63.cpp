// P12-MESH-007 diagnosis of the G6.3 failure (Galilean invariance of the translating cavity), step 1.
// Evidence only: prints where u_B - b - u_A differs after one step, and compares the ingredients.
#include <cmath>
#include <cstdio>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/AlePISO.hpp"
#include "cfd/pressure_velocity/PISO.hpp"

using namespace cfd;

int main() {
  const Vector3 b{0.5, 0.25, 0.0};
  const physics::FluidProperties fluid(1.0, 0.01);
  pressure_velocity::PISOSettings s;
  s.momentumSolver.absoluteTolerance = 1e-15;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver = s.momentumSolver;
  s.pressureSolver.maxIterations = 20000;
  const mesh::Mesh meshA = mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  mesh::Mesh meshB = mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  boundary::BoundaryConditionSet vA, pA, vB, pB, vC;
  vA.set(meshA, "left", std::make_unique<boundary::Wall>());
  vA.set(meshA, "right", std::make_unique<boundary::Wall>());
  vA.set(meshA, "bottom", std::make_unique<boundary::Wall>());
  vA.set(meshA, "top", std::make_unique<boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
  for (const char* p : {"left", "right", "bottom", "top"}) {
    pA.set(meshA, p, std::make_unique<boundary::FixedGradient>(0.0));
    pB.set(meshB, p, std::make_unique<boundary::FixedGradient>(0.0));
  }
  vB.set(meshB, "left", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "right", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "bottom", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "top", std::make_unique<boundary::MovingWall>(b + Vector3{1.0, 0.0, 0.0}));
  // C: the FIXED cavity with walls given as MovingWall(0) instead of Wall -- isolates BC-type effects.
  vC.set(meshA, "left", std::make_unique<boundary::MovingWall>(Vector3{}));
  vC.set(meshA, "right", std::make_unique<boundary::MovingWall>(Vector3{}));
  vC.set(meshA, "bottom", std::make_unique<boundary::MovingWall>(Vector3{}));
  vC.set(meshA, "top", std::make_unique<boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
  const pressure_velocity::PISO pisoA(meshA, fluid, vA, pA, s, 0);
  const pressure_velocity::PISO pisoC(meshA, fluid, vC, pA, s, 0);
  mesh::MeshMotion motion(meshB, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{}, b));
  const pressure_velocity::AlePISO ale(motion, fluid, vB, pB, s, 0);
  const Index n = meshA.numberOfCells();
  solver::TransientState a{fields::VectorField(n, Vector3{}), fields::ScalarField(n, 0.0), {}};
  a.massFlux = physics::calculateMassFlux(meshA, a.velocity, fluid, vA);
  solver::TransientState c = a;
  solver::TransientState bs{fields::VectorField(n, b), fields::ScalarField(n, 0.0), {}};
  bs.massFlux = physics::calculateMassFlux(meshB, bs.velocity, fluid, vB);
  const auto ra = pisoA.solveTimeStep(a, 0.01);
  const auto rc = pisoC.solveTimeStep(c, 0.01);
  const auto rb = ale.solveTimeStep(bs, 0.01);
  std::printf("status A %d C %d B %d\n", static_cast<int>(ra.status), static_cast<int>(rc.status),
              static_cast<int>(rb.status));
  Real dAC = 0.0;
  for (Index i = 0; i < n; ++i) dAC = std::max(dAC, magnitude(ra.state.velocity[i] - rc.state.velocity[i]));
  std::printf("A (Wall) vs C (MovingWall(0)), fixed mesh: max |u_A - u_C| = %.3e\n", dAC);
  std::printf("cells with |u_B - b - u_A| > 1e-6 (i, j): (dx, dy)\n");
  for (Index j = 0; j < 16; ++j) {
    for (Index i = 0; i < 16; ++i) {
      const Index id = j * 16 + i;
      const Vector3 d = (rb.state.velocity[id] - b) - ra.state.velocity[id];
      if (magnitude(d) > 1e-6) std::printf("  (%2zu,%2zu): (%.3e, %.3e)\n", i, j, d.x, d.y);
    }
  }
  return 0;
}
