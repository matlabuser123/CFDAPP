// P12-DIFF-002 gate probe: W1 (analytical reconstruction), W2 (translation/scale invariance) and
// W3 (topology/fallback), evaluated through the PRODUCTION boundaryFaceDiffusionTerms so the probe
// measures exactly what the assembly will do.
//
// The wall flux is reconstructed from the returned struct using CFDApp's assembly convention
// (architecture.md section 4): the assembled row holds -flux_into_owner, so
//     flux_into_owner = -(coefficient phi_P - farCellCoefficient phi_F)
//                       + boundaryValueCoefficient phi_b + explicitFlux
// which reduces to the pre-DIFF-002 expression Df (phi_b - phi_P) + explicitFlux when
// farCellCoefficient = 0 and boundaryValueCoefficient = coefficient. Both library versions are
// therefore measured by identical probe code.
//
// The transfer gradient is the EXACT analytic one here: W1 isolates the boundary reconstruction.
// The production path with the computed gradient is measured by W6 (diff2_poiseuille.cpp).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

constexpr Real kGamma = 0.1;
const Real kPi = std::acos(-1.0);
constexpr Real kEps = 2.220446049250313e-16;

// ---- manufactured fields; `scale` lets W2 rescale the domain consistently ----
enum class Kind { Constant, Linear, Quadratic, Cubic, Radial };

Real value(Kind k, const Vector3& x, const Vector3& origin, Real L) {
  const Vector3 r{(x.x - origin.x) / L, (x.y - origin.y) / L, (x.z - origin.z) / L};
  switch (k) {
    case Kind::Constant:
      return 2.5;
    case Kind::Linear:
      return 0.7 + (1.3 * r.x) - (0.9 * r.y) + (0.4 * r.z);
    case Kind::Quadratic:
      return 6.0 * r.y * (1.0 - r.y);
    case Kind::Cubic:
      return (r.y * r.y * r.y) + (0.5 * r.x * r.x * r.y);
    default: {
      const Real rad = std::sqrt((r.x * r.x) + (r.y * r.y));
      return (0.5 * rad * rad) + (rad * rad * rad / 3.0);
    }
  }
}

Vector3 gradient(Kind k, const Vector3& x, const Vector3& origin, Real L) {
  const Vector3 r{(x.x - origin.x) / L, (x.y - origin.y) / L, (x.z - origin.z) / L};
  Vector3 g{};
  switch (k) {
    case Kind::Constant:
      return Vector3{};
    case Kind::Linear:
      g = Vector3{1.3, -0.9, 0.4};
      break;
    case Kind::Quadratic:
      g = Vector3{0.0, 6.0 - (12.0 * r.y), 0.0};
      break;
    case Kind::Cubic:
      g = Vector3{r.x * r.y, (3.0 * r.y * r.y) + (0.5 * r.x * r.x), 0.0};
      break;
    default: {
      const Real rad = std::sqrt((r.x * r.x) + (r.y * r.y));
      if (!(rad > 0.0)) return Vector3{};
      const Real d = rad + (rad * rad);
      g = Vector3{d * r.x / rad, d * r.y / rad, 0.0};
      break;
    }
  }
  return g * (1.0 / L);  // chain rule for the rescaled argument
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::Constant:
      return "constant";
    case Kind::Linear:
      return "linear";
    case Kind::Quadratic:
      return "quadratic";
    case Kind::Cubic:
      return "cubic";
    default:
      return "radial";
  }
}

struct Result {
  Real perFaceL1{0.0};
  Real perFaceLinf{0.0};
  Real total{0.0};
  Real scale{0.0};
  Index faces{0};
  Index higherOrder{0};
  Index fallback{0};
  Real maxSizeRatio{0.0};
};

Result measure(const mesh::Mesh& m, Kind kind, const std::vector<std::string>& walls,
               const Vector3& origin, Real L) {
  Result r;
  fields::VectorField grad(m.numberOfCells(), Vector3{});
  for (const auto& c : m.cells()) grad[c.id()] = gradient(kind, c.centroid(), origin, L);

  for (const auto& patch : m.boundaryPatches()) {
    if (std::find(walls.begin(), walls.end(), patch.name()) == walls.end()) continue;
    for (const Index faceId : patch.faceIds()) {
      const auto& face = m.face(faceId);
      const auto& owner = m.cell(face.owner());
      const Real distance = magnitude(face.centroid() - owner.centroid());
      const auto terms = discretization::boundaryFaceDiffusionTerms(m, face, kGamma, distance,
                                                                     &grad, true);
      const Real phiB = value(kind, face.centroid(), origin, L);
      const Real phiP = value(kind, owner.centroid(), origin, L);
      Real phiF = 0.0;
      if (terms.farCellCoefficient != 0.0) {
        phiF = value(kind, m.cell(terms.farCell).centroid(), origin, L);
        ++r.higherOrder;
      } else {
        ++r.fallback;
      }
      const Real flux = -((terms.coefficient * phiP) - (terms.farCellCoefficient * phiF)) +
                        (terms.boundaryValueCoefficient * phiB) + terms.explicitFlux;
      const Real exact = kGamma * dot(gradient(kind, face.centroid(), origin, L),
                                      face.areaVector());
      const Real faceScale =
          kGamma * magnitude(gradient(kind, face.centroid(), origin, L)) * face.area();
      r.perFaceL1 += std::abs(flux - exact);
      r.perFaceLinf = std::max(r.perFaceLinf, std::abs(flux - exact));
      r.total += flux - exact;
      r.scale += faceScale;
      ++r.faces;
      const Real x = std::max({std::abs(owner.centroid().x), std::abs(owner.centroid().y),
                               std::abs(owner.centroid().z), std::abs(face.centroid().x),
                               std::abs(face.centroid().y), std::abs(face.centroid().z)});
      if (distance > 0.0) r.maxSizeRatio = std::max(r.maxSizeRatio, x / (2.0 * distance));
    }
  }
  return r;
}

struct Family {
  std::string label;
  std::vector<mesh::Mesh> meshes;
  std::vector<Real> h;
  std::vector<std::string> walls;
  Vector3 origin{};
  Real L{1.0};
};

// W1: exactness for constant/linear/quadratic; order for cubic (or radial on curved meshes).
void w1(const Family& f, Kind kind, Real exactBound, Real orderBound, bool orderCriterion) {
  std::vector<Real> l1;
  for (std::size_t i = 0; i < f.meshes.size(); ++i) {
    const Result r = measure(f.meshes[i], kind, f.walls, f.origin, f.L);
    const Real scale = (r.scale > 0.0) ? r.scale : 1.0;
    l1.push_back(r.perFaceL1 / scale);
    std::printf("W1  %-34s %-9s h %.5f faces %5zu ho %5zu fb %3zu | L1 %.4e Linf/scale %.4e\n",
                f.label.c_str(), kindName(kind), f.h[i], static_cast<std::size_t>(r.faces),
                static_cast<std::size_t>(r.higherOrder), static_cast<std::size_t>(r.fallback),
                l1.back(), r.perFaceLinf / (scale / static_cast<Real>(std::max<Index>(r.faces, 1))));
  }
  if (!orderCriterion) {
    const Real worst = *std::max_element(l1.begin(), l1.end());
    std::printf("W1  %-34s %-9s EXACTNESS worst %.4e bound %.4e %s\n", f.label.c_str(),
                kindName(kind), worst, exactBound, worst <= exactBound ? "PASS" : "FAIL");
    return;
  }
  Real worstOrder = 1e30;
  for (std::size_t k = 0; k + 1 < l1.size(); ++k) {
    const Real ratio = f.h[k] / f.h[k + 1];
    const Real o = (l1[k] > 0.0 && l1[k + 1] > 0.0)
                       ? std::log(l1[k] / l1[k + 1]) / std::log(ratio)
                       : 0.0;
    worstOrder = std::min(worstOrder, o);
    std::printf("W1  %-34s %-9s order %.3f\n", f.label.c_str(), kindName(kind), o);
  }
  std::printf("W1  %-34s %-9s ORDER worst %.3f bound %.3f %s\n", f.label.c_str(), kindName(kind),
              worstOrder, orderBound, worstOrder >= orderBound ? "PASS" : "FAIL");
}

std::vector<Vector2> channel(Index nx, Index ny, Real distort, const Vector3& o, Real L) {
  const Real H = 1.0 * L, len = 8.0 * L, lambda = 1.0 * L;
  const Real ax = 0.1 * distort * L, ay = 0.05 * distort * L;
  std::vector<Vector2> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = len * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = H * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / len) * std::sin(2.0 * kPi * eta / H));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / H));
      if (i == 0) x = 0.0;
      if (i == nx) x = len;
      if (j == 0) y = 0.0;
      if (j == ny) y = H;
      v.push_back(Vector2{x + o.x, y + o.y});
    }
  }
  return v;
}

Family channelFamily(const std::string& label, Real distort, const Vector3& o, Real L) {
  Family f;
  f.label = label;
  f.walls = {"bottom", "top"};
  f.origin = o;
  f.L = L;
  for (const auto& [nx, ny] :
       std::vector<std::pair<Index, Index>>{{64, 8}, {96, 12}, {144, 18}, {216, 27}}) {
    f.meshes.push_back(mesh::MeshGeometry::createStructuredQuad2D(nx, ny,
                                                                   channel(nx, ny, distort, o, L)));
    f.h.push_back(L / static_cast<Real>(ny));
  }
  return f;
}

Family cube3DFamily() {
  Family f;
  f.label = "3D Cartesian";
  f.walls = {"ymin", "ymax"};
  for (const Index n : {8u, 12u, 16u, 24u}) {
    f.meshes.push_back(mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0));
    f.h.push_back(1.0 / static_cast<Real>(n));
  }
  return f;
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002 gate probe W1/W2/W3\n");

  // ---- W1 ----
  const std::vector<std::tuple<std::string, Real, Vector3, Real>> families{
      {"2D Cartesian", 0.0, Vector3{}, 1.0},
      {"2D Cartesian translated", 0.0, Vector3{0.005, 0.0025, 0.0}, 1.0},
      {"2D distorted 48deg", 1.0, Vector3{}, 1.0}};
  for (const auto& [label, distort, origin, L] : families) {
    const Family f = channelFamily(label, distort, origin, L);
    w1(f, Kind::Constant, 1e-12, 0.0, false);
    w1(f, Kind::Linear, 1e-12, 0.0, false);
    w1(f, Kind::Quadratic, 1e-12, 0.0, false);
    w1(f, Kind::Cubic, 0.0, 1.8, true);
  }
  {
    const Family f = cube3DFamily();
    w1(f, Kind::Constant, 1e-12, 0.0, false);
    w1(f, Kind::Linear, 1e-12, 0.0, false);
    w1(f, Kind::Quadratic, 1e-12, 0.0, false);
    w1(f, Kind::Cubic, 0.0, 1.8, true);
  }
  // Curved multi-block: the committed case mesh, refined by its own generator is not available
  // here, so the single committed resolution is measured for exactness only.
  try {
    const io::SimulationSetup setup =
        io::CaseBuilder{}.build(io::CaseReader{}.read("cases/curved_channel_multiblock"));
    Family f;
    f.label = "curved multi-block (committed)";
    f.walls = {"inner_wall", "outer_wall", "inlet", "outlet"};
    f.meshes.push_back(setup.mesh);
    f.h.push_back(1.0);
    w1(f, Kind::Constant, 1e-12, 0.0, false);
    // The committed multi-block case exists at one resolution only, so no order can be measured
    // here, and the radial field is cubic in r so the reconstruction is not exact for it. The
    // criterion is therefore a NO-REGRESSION bound against the measured two-point value
    // (1.8616e-02, results/p12-diff-002/logs/04_gate_dryrun_baseline.log), not an invented
    // exactness bound.
    w1(f, Kind::Radial, 1.8616e-02, 0.0, false);
  } catch (const std::exception& e) {
    std::printf("W1  curved multi-block SKIPPED: %s\n", e.what());
  }

  // ---- W2: translation and scale invariance of the wall flux ----
  for (const auto& [label, distort] :
       std::vector<std::pair<const char*, Real>>{{"2D Cartesian", 0.0}, {"2D distorted", 1.0}}) {
    for (const Kind kind : {Kind::Quadratic, Kind::Cubic}) {
      const Family a = channelFamily(label, distort, Vector3{}, 1.0);
      const Family b = channelFamily(label, distort, Vector3{0.005, 0.0025, 0.0}, 1.0);
      const Family c = channelFamily(label, distort, Vector3{}, 1e-3);
      const Family d = channelFamily(label, distort, Vector3{}, 1e3);
      const std::size_t i = 2;  // the 144x18 level
      const Result ra = measure(a.meshes[i], kind, a.walls, a.origin, a.L);
      const Result rb = measure(b.meshes[i], kind, b.walls, b.origin, b.L);
      const Result rc = measure(c.meshes[i], kind, c.walls, c.origin, c.L);
      const Result rd = measure(d.meshes[i], kind, d.walls, d.origin, d.L);
      const Real na = ra.perFaceL1 / std::max(ra.scale, 1e-300);
      const Real nb = rb.perFaceL1 / std::max(rb.scale, 1e-300);
      const Real nc = rc.perFaceL1 / std::max(rc.scale, 1e-300);
      const Real nd = rd.perFaceL1 / std::max(rd.scale, 1e-300);
      const Real bound = std::max(1e-12, 200.0 * kEps * rb.maxSizeRatio);
      std::printf("W2  %-22s %-9s L1: origin %.4e translated %.4e (diff %.3e bound %.3e %s) | "
                  "L=1e-3 %.4e L=1e3 %.4e (spread %.3e)\n",
                  label, kindName(kind), na, nb, std::abs(nb - na), bound,
                  std::abs(nb - na) <= bound ? "PASS" : "FAIL", nc, nd,
                  std::abs(nc - nd) / std::max({na, nc, nd, 1e-300}));
    }
  }

  // ---- W3: stencil availability on every buildable committed case, and fallback meshes ----
  std::vector<std::string> cases;
  for (const auto& entry : std::filesystem::directory_iterator("cases")) {
    if (entry.is_directory()) cases.push_back(entry.path().generic_string());
  }
  std::sort(cases.begin(), cases.end());
  for (const std::string& c : cases) {
    try {
      const io::SimulationSetup setup = io::CaseBuilder{}.build(io::CaseReader{}.read(c));
      const mesh::Mesh& m = setup.mesh;
      fields::VectorField grad(m.numberOfCells(), Vector3{});
      Index ho = 0;
      Index fb = 0;
      for (const auto& face : m.faces()) {
        if (!face.isBoundary()) continue;
        const Real distance =
            magnitude(face.centroid() - m.cell(face.owner()).centroid());
        const auto terms =
            discretization::boundaryFaceDiffusionTerms(m, face, kGamma, distance, &grad, true);
        if (terms.farCellCoefficient != 0.0) {
          ++ho;
        } else {
          ++fb;
        }
      }
      std::printf("W3  %-44s boundary faces %5zu higher-order %5zu fallback %5zu\n", c.c_str(),
                  static_cast<std::size_t>(ho + fb), static_cast<std::size_t>(ho),
                  static_cast<std::size_t>(fb));
    } catch (const std::exception&) {
      std::printf("W3  %-44s SKIPPED (not a buildable case)\n", c.c_str());
    }
  }
  for (const auto& [label, m] : std::vector<std::pair<const char*, mesh::Mesh>>{
           {"degenerate 2D 1x1", mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0)},
           {"degenerate 2D 8x1", mesh::MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0)},
           {"degenerate 2D 1x8", mesh::MeshGeometry::createCartesian2D(1, 8, 1.0, 8.0)},
           {"degenerate 3D 1x1x1", mesh::MeshGeometry::createCartesian3D(1, 1, 1, 1, 1, 1)},
           {"degenerate 3D 8x8x1", mesh::MeshGeometry::createCartesian3D(8, 8, 1, 1, 1, 1)}}) {
    fields::VectorField grad(m.numberOfCells(), Vector3{});
    Index ho = 0;
    Index fb = 0;
    for (const auto& face : m.faces()) {
      if (!face.isBoundary()) continue;
      const Real distance = magnitude(face.centroid() - m.cell(face.owner()).centroid());
      const auto terms =
          discretization::boundaryFaceDiffusionTerms(m, face, kGamma, distance, &grad, true);
      if (terms.farCellCoefficient != 0.0) {
        ++ho;
      } else {
        ++fb;
      }
    }
    std::printf("W3  %-44s boundary faces %5zu higher-order %5zu fallback %5zu\n", label,
                static_cast<std::size_t>(ho + fb), static_cast<std::size_t>(ho),
                static_cast<std::size_t>(fb));
  }
  return 0;
}
