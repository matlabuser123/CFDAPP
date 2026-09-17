// P12-MESH-007 diagnosis of the G6.3 failure, part 4 (evidence only, NOT a gate run): Galilean
// invariance of ALE PISO on a mesh where the Green-Gauss "paired" boundary branch (exact
// cross(d, S_f) == 0) is off in BOTH runs -- the distorted Q16 mesh of the gate, whose boundary faces are
// met obliquely by their cells' centroids. Same cavity setup, dt, steps and solver settings as G6.3.
// Prints the number of exactly-parallel boundary faces of each mesh and max |u_B - b - u_A| per step.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/AlePISO.hpp"
#include "cfd/pressure_velocity/PISO.hpp"

using namespace cfd;

namespace {

mesh::Mesh q16() {
  std::vector<Vector3> v;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0, y = static_cast<Real>(j) / 16.0, pi = constants::pi;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return mesh::MeshGeometry::createStructuredQuad2D(16, 16, v);
}

int exactlyParallel(const mesh::Mesh& m) {
  int count = 0;
  for (const auto& face : m.faces()) {
    if (face.isBoundary() && cross(face.centroid() - m.cell(face.owner()).centroid(), face.areaVector()) == Vector3{}) {
      ++count;
    }
  }
  return count;
}

}  // namespace

int main() {
  const Vector3 b{0.5, 0.25, 0.0};
  const physics::FluidProperties fluid(1.0, 0.01);
  pressure_velocity::PISOSettings s;
  s.momentumSolver.absoluteTolerance = 1e-15;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver = s.momentumSolver;
  s.pressureSolver.maxIterations = 20000;
  const mesh::Mesh meshA = q16();
  mesh::Mesh meshB = q16();
  boundary::BoundaryConditionSet vA, vB, pA, pB;
  vA.set(meshA, "left", std::make_unique<boundary::Wall>());
  vA.set(meshA, "right", std::make_unique<boundary::Wall>());
  vA.set(meshA, "bottom", std::make_unique<boundary::Wall>());
  vA.set(meshA, "top", std::make_unique<boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
  vB.set(meshB, "left", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "right", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "bottom", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "top", std::make_unique<boundary::MovingWall>(b + Vector3{1.0, 0.0, 0.0}));
  for (const char* p : {"left", "right", "bottom", "top"}) {
    pA.set(meshA, p, std::make_unique<boundary::FixedGradient>(0.0));
    pB.set(meshB, p, std::make_unique<boundary::FixedGradient>(0.0));
  }
  const pressure_velocity::PISO pisoA(meshA, fluid, vA, pA, s, 0);
  mesh::MeshMotion motion(meshB, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{}, b));
  const pressure_velocity::AlePISO ale(motion, fluid, vB, pB, s, 0);
  const Index n = meshA.numberOfCells();
  solver::TransientState a{fields::VectorField(n, Vector3{}), fields::ScalarField(n, 0.0), {}};
  a.massFlux = physics::calculateMassFlux(meshA, a.velocity, fluid, vA);
  solver::TransientState bs{fields::VectorField(n, b), fields::ScalarField(n, 0.0), {}};
  bs.massFlux = physics::calculateMassFlux(meshB, bs.velocity, fluid, vB);
  std::printf("Q16 exactly-parallel boundary faces: A %d of 64, B at t0 %d of 64\n", exactlyParallel(meshA),
              exactlyParallel(meshB));
  Real worst = 0.0;
  int maxParallelB = 0;
  for (int step = 1; step <= 20; ++step) {
    const auto ra = pisoA.solveTimeStep(a, 0.01);
    const auto rb = ale.solveTimeStep(bs, 0.01);
    if (ra.status != solver::TransientStepStatus::Converged || rb.status != solver::TransientStepStatus::Converged) {
      std::printf("step %d: status A %d B %d\n", step, static_cast<int>(ra.status), static_cast<int>(rb.status));
      return 1;
    }
    a = ra.state;
    bs = rb.state;
    maxParallelB = std::max(maxParallelB, exactlyParallel(meshB));
    Real d = 0.0;
    for (Index c = 0; c < n; ++c) d = std::max(d, magnitude((bs.velocity[c] - b) - a.velocity[c]));
    worst = std::max(worst, d);
    if (step == 1 || step == 5 || step == 10 || step == 20) {
      std::printf("step %2d: max |u_B - b - u_A| = %.3e\n", step, d);
    }
  }
  std::printf("Q16 translating cavity vs fixed Q16 cavity, 20 steps: max |u_B - b - u_A| = %.3e; exactly-parallel "
              "boundary faces of the moving mesh, max over steps: %d\n",
              worst, maxParallelB);
  return 0;
}
