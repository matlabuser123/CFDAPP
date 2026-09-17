// P12-MESH-005 finding probe (2D API only, so it compiles against the pre-MESH-005 headers too):
// explicit diffusion() of phi = x^2 + y^2 (exact Laplacian 4) with exactly two cells along x.
// With n = 2 along a boundary normal, Diffusion.cpp's nextInteriorFaceAwayFrom finds no face that
// continues away from the boundary and accepts a perpendicular one, so the boundary cubic fit uses a
// transverse point. Run against the BASE (pre-MESH-005) and the final library: identical output shows
// the defect is pre-existing and unchanged by MESH-005. n = 3 along x is the control.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using mesh::Mesh;
using mesh::MeshGeometry;

namespace {

Mesh perFace(const Mesh& base) {
  std::vector<mesh::BoundaryPatch> patches;
  for (const auto& f : base.faces())
    if (f.isBoundary()) patches.emplace_back("b" + std::to_string(f.id()), std::vector<Index>{f.id()});
  return Mesh(base.cells(), base.faces(), std::move(patches));
}

Real phi(const Vector2& p) { return (p.x * p.x) + (p.y * p.y); }

void run(Index nx, Index ny) {
  const Mesh m = perFace(MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0));
  fields::ScalarField s(m.numberOfCells());
  for (const auto& c : m.cells()) s[c.id()] = phi(c.centroid());
  boundary::BoundaryConditionSet b;
  for (const auto& p : m.boundaryPatches())
    b.set(m, p.name(), std::make_unique<boundary::FixedValue>(phi(m.face(p.faceIds().front()).centroid())));
  const auto r = discretization::diffusion(m, s, 1.0, b);
  Real worst = 0.0;
  for (Index i = 0; i < m.numberOfCells(); ++i) worst = std::fmax(worst, std::fabs(r[i] - 4.0));
  std::printf("createCartesian2D(%zu, %zu): max |diffusion - 4| = %.6e; per cell:", static_cast<std::size_t>(nx),
              static_cast<std::size_t>(ny), worst);
  for (Index i = 0; i < m.numberOfCells(); ++i) std::printf(" %.17g", r[i]);
  std::printf("\n");
}

}  // namespace

int main() {
  run(2, 4);
  run(4, 2);
  run(3, 4);  // control: three cells along x
  return 0;
}
