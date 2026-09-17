// P12-DIFF-002 ThermalInterface fix, criterion T7: the DIFF-002 boundary accuracy is now actually
// reached BY CONJUGATE CONDUCTION -- constant/linear/quadratic exact to 1e-12, cubic at observed
// order >= 1.8 (the W1c bound).
//
// The measured quantity is extracted FROM THE ASSEMBLED CONJUGATE SYSTEM, not by re-calling
// boundaryFaceDiffusionTerms (that would only re-measure W1). For cell P the assembled row holds
// -flux_into_owner per face (results/p12-diff-002/architecture.md section 4), so
//
//     (A T)_P - b_P = sum over internal faces of  c_f (T_P - T_N)
//                   + sum over boundary faces of  (-flux_into_P)
//
// and therefore
//
//     sum of boundary flux into P = sum_internal c_f (T_P - T_N) - [ (A T)_P - b_P ].
//
// The internal coefficient c_f = k |Sf| / d_PN is hand-written here (the documented same-region
// rule, verified bitwise against the assembly in the T5 probe), so the boundary flux is recovered
// from the matrix and RHS the conjugate assembly actually produced.
//
// Fields, the exact flux k grad(T) . S_out, and the per-face L1 normalisation by
// sum k |grad| |Sf| are W1's own conventions (results/p12-diff-002/tools/diff2_gate.cpp), so the
// numbers are comparable with W1a/W1b/W1c.
//
// Two configurations are measured for every field and mesh: ONE region, and TWO regions with a
// conductivity jump of 4x at mid-height -- so the accuracy claim is made with a genuine material
// discontinuity present in the same assembly, not only in the degenerate single-material case.
//
// Scope note, stated rather than hidden: the manufactured fields vary along the wall NORMAL only
// (the "axial" fields of W1b), because a Dirichlet patch carries one value and cannot represent a
// wall-tangential profile. Measured walls are therefore the y-normal pair, and any cell also
// touching another patch is excluded -- the same "walls" filter W1 uses. Non-orthogonal geometry is
// covered instead by T2-T4, which prove the conjugate assembly is bitwise identical to the
// single-material one on a distorted mesh, where W1b/W1c already measured exactness and order.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/ThermalInterface.hpp"

using namespace cfd;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kA = 2.5;         // conductivity of the region containing the measured walls
constexpr Real kB = 4.0 * kA;    // the other region, for the two-region configuration
constexpr Real kHeight = 1.0;    // domain height, so r = y

enum class Kind { Constant, Linear, Quadratic, Cubic };

const char* kindName(Kind k) {
  switch (k) {
    case Kind::Constant: return "constant";
    case Kind::Linear: return "linear";
    case Kind::Quadratic: return "quadratic";
    default: return "cubic";
  }
}

// Wall-normal (axial) manufactured fields: constant along each measured wall, so a Dirichlet patch
// represents the boundary value exactly.
Real value(Kind k, const Vector3& x) {
  const Real r = x.y / kHeight;
  switch (k) {
    case Kind::Constant: return 2.5;
    case Kind::Linear: return 0.7 - (0.9 * r);
    case Kind::Quadratic: return 6.0 * r * (1.0 - r);
    default: return r * r * r;
  }
}

Vector3 gradient(Kind k, const Vector3& x) {
  const Real r = x.y / kHeight;
  Real dy = 0.0;
  switch (k) {
    case Kind::Constant: dy = 0.0; break;
    case Kind::Linear: dy = -0.9; break;
    case Kind::Quadratic: dy = 6.0 - (12.0 * r); break;
    default: dy = 3.0 * r * r; break;
  }
  return Vector3{0.0, dy / kHeight, 0.0};
}

bool isMeasuredWall(const std::string& patch) {
  return patch == "bottom" || patch == "top" || patch == "ymin" || patch == "ymax";
}

struct Config {
  bool twoRegions{};
  const char* label{};
};

// Region A holds y < mid (and therefore both measured walls' cells when `twoRegions` is false; with
// two regions the top wall sits in region B, so both conductivities are exercised at a wall).
thermal::ThermalRegionMap regionsFor(const Mesh& mesh, const Config& cfg) {
  std::vector<Index> cellRegion(mesh.numberOfCells(), 0);
  if (cfg.twoRegions) {
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      cellRegion[i] = (mesh.cell(i).centroid().y > 0.5 * kHeight) ? 1 : 0;
    }
  }
  return thermal::ThermalRegionMap(
      {thermal::ThermalRegion{"a", thermal::ThermalRegionType::Solid,
                              thermal::ThermalProperties(kA, 1.0)},
       thermal::ThermalRegion{"b", thermal::ThermalRegionType::Solid,
                              thermal::ThermalProperties(kB, 1.0)}},
      cellRegion);
}

boundary::BoundaryConditionSet boundaries(const Mesh& mesh, Kind kind) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    Real v = 0.0;
    if (isMeasuredWall(patch.name())) {
      // Exact, and genuinely constant along this wall.
      v = value(kind, mesh.face(patch.faceIds().front()).centroid());
    } else {
      // Cells touching these patches are excluded from the measurement; the value only has to be
      // finite and physical.
      v = value(kind, Vector3{0.0, 0.5 * kHeight, 0.0});
    }
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(v));
  }
  return bcs;
}

struct Result {
  Real l1{};
  Real scale{};
  std::size_t cells{};
  std::size_t faces{};
};

Result measure(const Mesh& mesh, Kind kind, const Config& cfg) {
  const auto bcs = boundaries(mesh, kind);
  const auto regions = regionsFor(mesh, cfg);

  fields::ScalarField t(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) t[i] = value(kind, mesh.cell(i).centroid());

  const auto assembly = thermal::assembleConjugateConductionEquation(mesh, t, regions, bcs);
  algebra::Vector tv(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) tv[i] = t[i];
  const algebra::Vector residual = assembly.system.matrix().multiply(tv);  // (A T)

  // Which cells may be measured: those with at least one face on a measured wall and none on any
  // other patch.
  std::vector<int> measuredFaces(mesh.numberOfCells(), 0);
  std::vector<bool> excluded(mesh.numberOfCells(), false);
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      const Index owner = mesh.face(faceId).owner();
      if (isMeasuredWall(patch.name())) {
        ++measuredFaces[owner];
      } else {
        excluded[owner] = true;
      }
    }
  }

  Result r;
  for (Index p = 0; p < mesh.numberOfCells(); ++p) {
    if (measuredFaces[p] == 0 || excluded[p]) continue;

    // Internal part, hand-written from the documented same-region rule.
    Real internalOut = 0.0;
    for (const Index faceId : mesh.cell(p).faceIds()) {
      const Face& face = mesh.face(faceId);
      if (face.isBoundary()) continue;
      const Index other = (face.owner() == p) ? *face.neighbor() : face.owner();
      const Real k = regions.regionForCell(p).properties.conductivity();
      Real c = 0.0;
      if (regions.sameRegion(p, other)) {
        c = k * face.area() / MeshGeometry::ownerNeighborDistance(mesh, face);
      } else {
        const Real d1 = MeshGeometry::distance(mesh.cell(p).centroid(), face.centroid());
        const Real d2 = MeshGeometry::distance(face.centroid(), mesh.cell(other).centroid());
        c = thermal::interfaceConductance(k, d1,
                                          regions.regionForCell(other).properties.conductivity(),
                                          d2, face.area());
      }
      internalOut += c * (t[p] - t[other]);
    }

    const Real assembled = internalOut - (residual[p] - assembly.system.rhs()[p]);

    // Exact boundary flux into P, and the W1 normalisation scale, over P's measured wall faces.
    Real exact = 0.0;
    Real scale = 0.0;
    for (const Index faceId : mesh.cell(p).faceIds()) {
      const Face& face = mesh.face(faceId);
      if (!face.isBoundary()) continue;
      const Real k = regions.regionForCell(p).properties.conductivity();
      const Vector3 g = gradient(kind, face.centroid());
      exact += k * dot(g, face.areaVector());
      scale += k * magnitude(g) * face.area();
      ++r.faces;
    }

    r.l1 += std::abs(assembled - exact);
    r.scale += scale;
    ++r.cells;
  }
  return r;
}

struct Family {
  std::string label;
  std::vector<Mesh> meshes;
  std::vector<Real> h;
};

bool report(const Family& f, Kind kind, const Config& cfg, bool orderCriterion) {
  std::vector<Real> l1;
  for (std::size_t i = 0; i < f.meshes.size(); ++i) {
    const Result r = measure(f.meshes[i], kind, cfg);
    const Real scale = (r.scale > 0.0) ? r.scale : 1.0;
    l1.push_back(r.l1 / scale);
    std::printf("T7  %-22s %-10s %-11s h %.5f cells %4zu faces %4zu | L1 %.4e\n", f.label.c_str(),
                kindName(kind), cfg.label, f.h[i], r.cells, r.faces, l1.back());
  }
  if (!orderCriterion) {
    const Real worst = *std::max_element(l1.begin(), l1.end());
    const bool pass = worst <= 1e-12;
    std::printf("T7  %-22s %-10s %-11s EXACTNESS worst %.4e bound 1.0000e-12 %s\n",
                f.label.c_str(), kindName(kind), cfg.label, worst, pass ? "PASS" : "FAIL");
    return pass;
  }
  Real worstOrder = 1e30;
  for (std::size_t k = 0; k + 1 < l1.size(); ++k) {
    const Real ratio = f.h[k] / f.h[k + 1];
    const Real o =
        (l1[k] > 0.0 && l1[k + 1] > 0.0) ? std::log(l1[k] / l1[k + 1]) / std::log(ratio) : 0.0;
    worstOrder = std::min(worstOrder, o);
    std::printf("T7  %-22s %-10s %-11s order %.3f\n", f.label.c_str(), kindName(kind), cfg.label,
                o);
  }
  const bool pass = worstOrder >= 1.8;
  std::printf("T7  %-22s %-10s %-11s ORDER worst %.3f bound 1.800 %s\n", f.label.c_str(),
              kindName(kind), cfg.label, worstOrder, pass ? "PASS" : "FAIL");
  return pass;
}

Family cartesian2D() {
  Family f;
  f.label = "Cartesian 2D";
  for (const Index ny : {8, 16, 32, 64}) {
    f.meshes.push_back(MeshGeometry::createCartesian2D(12, ny, 1.0, kHeight));
    f.h.push_back(kHeight / static_cast<Real>(ny));
  }
  return f;
}

Family cartesian3D() {
  Family f;
  f.label = "Cartesian 3D";
  for (const Index ny : {6, 12, 24}) {
    f.meshes.push_back(MeshGeometry::createCartesian3D(6, ny, 6, 1.0, kHeight, 1.0));
    f.h.push_back(kHeight / static_cast<Real>(ny));
  }
  return f;
}

Family graded2D() {
  Family f;
  f.label = "graded 2D r=1.2";
  for (const Index ny : {8, 16, 32}) {
    f.meshes.push_back(MeshGeometry::createGraded2D(
        12, ny, 1.0, kHeight, mesh::AxisGrading{},
        mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both}));
    f.h.push_back(kHeight / static_cast<Real>(ny));
  }
  return f;
}

}  // namespace

int main() {
  std::printf("# T7: boundary-flux accuracy of the CONJUGATE assembly, recovered from the\n");
  std::printf("# assembled matrix and RHS. W1's fields, exact flux and L1 normalisation.\n");
  std::printf("# 'two-region' carries a 4x conductivity jump at mid-height in the same assembly.\n\n");

  const Family c2 = cartesian2D();
  const Family c3 = cartesian3D();
  const Family g2 = graded2D();

  bool pass = true;
  for (const Config cfg : {Config{false, "one-region"}, Config{true, "two-region"}}) {
    std::printf("## %s\n", cfg.label);
    for (const Kind kind : {Kind::Constant, Kind::Linear, Kind::Quadratic}) {
      pass = report(c2, kind, cfg, false) && pass;
      pass = report(c3, kind, cfg, false) && pass;
      pass = report(g2, kind, cfg, false) && pass;
    }
    pass = report(c2, Kind::Cubic, cfg, true) && pass;
    pass = report(c3, Kind::Cubic, cfg, true) && pass;
    std::printf("\n");
  }

  std::printf("T7 RESULT: %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
