// P12-MESH-007 diagnosis of the G6.3 failure, part 3 -- uses ONLY pre-MESH-007 APIs, so it compiles and
// runs against BASE (the pre-MESH-007 library) as well as NEW. Evidence only.
//
// Claim: the STATIC PISO is not invariant under a pure translation of the mesh. The Green-Gauss
// gradient gives a boundary cell its "paired" quadratic-fit treatment only when cross(d, S_f) is
// EXACTLY zero (src/discretization/Gradient.cpp, tryPairedBoundaryContribution); on a Cartesian mesh
// translated by a non-dyadic offset the centroids carry round-off, the test fails, and the boundary
// gradient switches to the plain Green-Gauss form.
//
// A  : createCartesian2D(16, 16, 1, 1) lid cavity, 20 PISO steps, dt 0.01.
// AT : the same cavity on createStructuredQuad2D with every vertex shifted by (0.005, 0.0025) -- a
//      translated copy of A's mesh, built by the pre-existing builder; static.
// Prints, per mesh, the number of boundary faces with cross(d, S_f) == 0 exactly, and max |u_AT - u_A|.
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
                               fields::ScalarField(m.numberOfCells(), 0.0), {}};
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

}  // namespace

int main() {
  const mesh::Mesh a = mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  std::vector<Vector3> shifted;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      shifted.push_back(Vector3{(static_cast<Real>(i) / 16.0) + 0.005, (static_cast<Real>(j) / 16.0) + 0.0025, 0.0});
    }
  }
  const mesh::Mesh at = mesh::MeshGeometry::createStructuredQuad2D(16, 16, shifted);
  std::printf("boundary faces with cross(d, S_f) == 0 exactly: A %d of 64, AT %d of 64\n",
              exactlyParallelBoundaryFaces(a), exactlyParallelBoundaryFaces(at));
  const auto sa = run(a);
  const auto st = run(at);
  for (std::size_t n = 0; n < std::min(sa.size(), st.size()); ++n) {
    Real du = 0.0;
    for (Index c = 0; c < a.numberOfCells(); ++c) du = std::max(du, magnitude(st[n].velocity[c] - sa[n].velocity[c]));
    if (n == 0 || n == 4 || n == 9 || n == 19) std::printf("step %2zu: static A vs static translated AT: max |u_AT - u_A| = %.3e\n", n + 1, du);
  }
  return 0;
}
