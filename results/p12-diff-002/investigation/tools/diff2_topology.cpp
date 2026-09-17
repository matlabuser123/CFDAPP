// P12-DIFF-002-INV-001 production feasibility: can the required inward stencil be obtained
// robustly on every mesh CFDApp actually produces, and what happens where it cannot?
//
// For every boundary face of every committed case mesh (built through the production
// CaseReader -> CaseBuilder path) and of constructed graded / degenerate meshes, the probe asks:
//   - does oppositeInteriorFace give a far cell?
//   - are the normal projections usable (h1 > 0 and h2 > h1)?
//   - how anti-parallel is the chosen opposite face (the stencil's directional quality)?
//   - how large is the tangential offset that must be transferred?
// and reports the count of faces that would have to fall back to the existing two-point form.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

struct Audit {
  Index boundaryFaces{0};
  Index noOppositeFace{0};   // topological: no interior face across the cell
  Index badProjection{0};    // h1 <= 0 or h2 <= h1
  Index usable{0};
  Real worstAlignment{1.0};  // most anti-parallel wins; closer to 0 is a poorer stencil
  Real worstTangent{0.0};    // max |d_t| / d_n
  Real worstSpacing{0.0};    // max h2 / h1  (stencil stretch)
  Real minSpacing{1e30};
};

Audit audit(const mesh::Mesh& m) {
  Audit a;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    ++a.boundaryFaces;
    const auto& owner = m.cell(face.owner());
    const Vector3 n = face.areaVector() * (1.0 / face.area());
    const auto opposite = mesh::MeshGeometry::oppositeInteriorFace(m, owner, face);
    if (!opposite.has_value()) {
      ++a.noOppositeFace;
      continue;
    }
    const auto& of = m.face(*opposite);
    const Index farId = (of.owner() == owner.id()) ? *of.neighbor() : of.owner();
    const Real h1 = dot(face.centroid() - owner.centroid(), n);
    const Real h2 = dot(face.centroid() - m.cell(farId).centroid(), n);
    if (!(h1 > 0.0) || !(h2 > h1)) {
      ++a.badProjection;
      continue;
    }
    ++a.usable;
    const Vector3 oppositeNormal = (of.owner() == owner.id())
                                       ? mesh::MeshGeometry::unitNormal(of)
                                       : (mesh::MeshGeometry::unitNormal(of) * -1.0);
    a.worstAlignment = std::min(a.worstAlignment, -dot(n, oppositeNormal));
    const Vector3 d = face.centroid() - owner.centroid();
    a.worstTangent = std::max(a.worstTangent, magnitude(d - (n * h1)) / h1);
    a.worstSpacing = std::max(a.worstSpacing, h2 / h1);
    a.minSpacing = std::min(a.minSpacing, h2 / h1);
  }
  return a;
}

void report(const std::string& label, const mesh::Mesh& m) {
  const Audit a = audit(m);
  const auto q = mesh::MeshQuality::evaluate(m);
  std::printf("T   %-42s cells %6zu bfaces %5zu | usable %5zu no-opposite %3zu bad-projection %3zu "
              "| min anti-parallel %.3f | max |d_t|/d_n %.3e | h2/h1 in [%.2f, %.2f] | nonorth "
              "%5.2f\n",
              label.c_str(), static_cast<std::size_t>(m.numberOfCells()),
              static_cast<std::size_t>(a.boundaryFaces), static_cast<std::size_t>(a.usable),
              static_cast<std::size_t>(a.noOppositeFace),
              static_cast<std::size_t>(a.badProjection), a.worstAlignment, a.worstTangent,
              (a.minSpacing < 1e29) ? a.minSpacing : 0.0, a.worstSpacing,
              q.maxNonOrthogonalityDegrees);
}

void reportCase(const std::string& directory) {
  try {
    const io::CaseDefinition definition = io::CaseReader{}.read(directory);
    const io::SimulationSetup setup = io::CaseBuilder{}.build(definition);
    report(directory, setup.mesh);
  } catch (const std::exception& e) {
    std::printf("T   %-42s SKIPPED: %s\n", directory.c_str(), e.what());
  }
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002-INV-001 stencil availability on production meshes\n");
  // Every committed case, through the production build path.
  std::vector<std::string> cases;
  for (const auto& entry : std::filesystem::directory_iterator("cases")) {
    if (entry.is_directory()) cases.push_back(entry.path().generic_string());
  }
  std::sort(cases.begin(), cases.end());
  for (const std::string& c : cases) reportCase(c);

  // Constructed extremes.
  std::printf("# constructed extremes\n");
  report("graded 2D 16x16 ratio 1.2 both walls",
         mesh::MeshGeometry::createGraded2D(16, 16, 1.0, 1.0,
                                            mesh::AxisGrading{mesh::GradingType::Geometric, 1.2},
                                            mesh::AxisGrading{mesh::GradingType::Geometric, 1.2}));
  report("graded 2D 16x16 ratio 1.5 both walls",
         mesh::MeshGeometry::createGraded2D(16, 16, 1.0, 1.0,
                                            mesh::AxisGrading{mesh::GradingType::Geometric, 1.5},
                                            mesh::AxisGrading{mesh::GradingType::Geometric, 1.5}));
  report("Cartesian 2D 1x1 (every side a boundary)",
         mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0));
  report("Cartesian 2D 8x1 (one cell across)",
         mesh::MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0));
  report("Cartesian 2D 1x8 (one cell along)",
         mesh::MeshGeometry::createCartesian2D(1, 8, 1.0, 8.0));
  report("Cartesian 3D 1x1x1", mesh::MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0));
  report("Cartesian 3D 8x8x1 (one cell in z)",
         mesh::MeshGeometry::createCartesian3D(8, 8, 1, 1.0, 1.0, 1.0));
  return 0;
}
