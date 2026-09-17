// P12-GRAD-002 A1, criterion C7: the static translated-cavity reproducer, independent of ALE.
// Construction identical to the original MESH-007 diagnosis (results/p12-mesh-007/tools/
// diag_g63c_static.cpp), so the pre-fix numbers 5.418e-3 (step 1) and 2.487e-2 (step 20) are
// directly comparable; extended to a finer 32x32 mesh and to the large offset as A1 requires.
// Uses only pre-MESH-007 APIs, so it runs against base and new alike.
//
//   A  = Cartesian n x n lid cavity, 20 PISO steps, dt 0.01, identical settings on both meshes.
//   B  = the same cavity on a rigidly translated copy of that mesh, built by the pre-existing
//        createStructuredQuad2D from shifted vertices.
// Frozen requirement: max |u_B - u_A| <= 1e-9 over the 20 steps (10x below MESH-007 G6.3's 1e-8).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"

using namespace cfd;

namespace {

int exactlyParallelBoundaryFaces(const mesh::Mesh& m) {
  int count = 0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    if (cross(d, face.areaVector()) == Vector3{}) ++count;
  }
  return count;
}

std::vector<solver::TransientState> run(const mesh::Mesh& m) {
  const physics::FluidProperties fluid(1.0, 0.01);
  boundary::BoundaryConditionSet v, p;
  v.set(m, "left", std::make_unique<boundary::Wall>());
  v.set(m, "right", std::make_unique<boundary::Wall>());
  v.set(m, "bottom", std::make_unique<boundary::Wall>());
  v.set(m, "top", std::make_unique<boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
  for (const char* name : {"left", "right", "bottom", "top"}) {
    p.set(m, name, std::make_unique<boundary::FixedGradient>(0.0));
  }
  pressure_velocity::PISOSettings s;
  s.momentumSolver.absoluteTolerance = 1e-15;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver = s.momentumSolver;
  s.pressureSolver.maxIterations = 20000;
  const pressure_velocity::PISO piso(m, fluid, v, p, s, 0);
  solver::TransientState state{fields::VectorField(m.numberOfCells(), Vector3{}),
                               fields::ScalarField(m.numberOfCells(), 0.0),
                               {}};
  state.massFlux = physics::calculateMassFlux(m, state.velocity, fluid, v);
  std::vector<solver::TransientState> states;
  for (int n = 1; n <= 20; ++n) {
    const auto r = piso.solveTimeStep(state, 0.01);
    if (r.status != solver::TransientStepStatus::Converged) {
      std::printf("step %d status %d\n", n, static_cast<int>(r.status));
      break;
    }
    state = r.state;
    states.push_back(state);
  }
  return states;
}

void reproducer(Index n, const Vector3& offset, const char* offsetName) {
  const mesh::Mesh a = mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  std::vector<Vector3> shifted;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      shifted.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n)) + offset.x,
                                (static_cast<Real>(j) / static_cast<Real>(n)) + offset.y, 0.0});
    }
  }
  const mesh::Mesh b = mesh::MeshGeometry::createStructuredQuad2D(n, n, shifted);
  const auto sa = run(a);
  const auto sb = run(b);
  const std::size_t steps = std::min(sa.size(), sb.size());
  Real step1 = 0.0;
  Real step20 = 0.0;
  Real worst = 0.0;
  for (std::size_t s = 0; s < steps; ++s) {
    Real du = 0.0;
    for (Index c = 0; c < a.numberOfCells(); ++c) {
      du = std::max(du, magnitude(sb[s].velocity[c] - sa[s].velocity[c]));
    }
    if (s == 0) step1 = du;
    if (s == 19) step20 = du;
    worst = std::max(worst, du);
  }
  std::printf("C7  %3zu^2 offset %-6s | exactly-parallel boundary faces A %d B %d of %zu | step 1 "
              "%.3e step 20 %.3e worst %.3e bound 1.000e-09 %s\n",
              static_cast<std::size_t>(n), offsetName, exactlyParallelBoundaryFaces(a),
              exactlyParallelBoundaryFaces(b), static_cast<std::size_t>(4 * n), step1, step20,
              worst, worst <= 1e-9 ? "PASS" : "FAIL");
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 A1 C7: static translated-cavity reproducer (no ALE)\n");
  reproducer(16, Vector3{0.005, 0.0025, 0.0}, "small");
  reproducer(32, Vector3{0.005, 0.0025, 0.0}, "small");
  reproducer(16, Vector3{1234.5678, 987.6543, 0.0}, "LARGE");
  return 0;
}
