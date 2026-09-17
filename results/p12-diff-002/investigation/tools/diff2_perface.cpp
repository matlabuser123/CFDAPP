// P12-DIFF-002-INV-001: PER-FACE wall-flux error and its order, plus curved and 3D geometry.
//
// Why per-face: the total wall force can hide per-face error through wall-to-wall cancellation.
// For phi = y^3 on an orthogonal mesh the one-sided quadratic fit errs by -0.75 h^2 at y = 0 and
// +0.75 h^2 at y = 1, so the total cancels to round-off while each face carries an O(h^2) error.
// Per-face L1/Linf therefore give the honest order.
//
// Same reconstruction as diff2_wallflux.cpp (plan.md section 2), with the EXACT gradient used for
// the tangential transfer so this isolates the reconstruction's own order.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

constexpr Real kGamma = 0.1;
const Real kPi = std::acos(-1.0);

enum class Kind { Quadratic, Cubic, Radial };

Real value(Kind k, const Vector3& x) {
  switch (k) {
    case Kind::Quadratic:
      return 6.0 * x.y * (1.0 - x.y);
    case Kind::Cubic:
      return x.y * x.y * x.y;
    default: {  // radial, for the annulus: phi = r^2 / 2 + r^3 / 3
      const Real r = std::sqrt((x.x * x.x) + (x.y * x.y));
      return (0.5 * r * r) + (r * r * r / 3.0);
    }
  }
}

Vector3 gradient(Kind k, const Vector3& x) {
  switch (k) {
    case Kind::Quadratic:
      return Vector3{0.0, 6.0 - (12.0 * x.y), 0.0};
    case Kind::Cubic:
      return Vector3{0.0, 3.0 * x.y * x.y, 0.0};
    default: {
      const Real r = std::sqrt((x.x * x.x) + (x.y * x.y));
      if (!(r > 0.0)) return Vector3{};
      const Real dphidr = r + (r * r);
      return Vector3{dphidr * x.x / r, dphidr * x.y / r, 0.0};
    }
  }
}

const char* kindName(Kind k) {
  return k == Kind::Quadratic ? "quadratic" : (k == Kind::Cubic ? "cubic" : "radial");
}

struct Metrics {
  Real twoL1{0.0};
  Real threeL1{0.0};
  Real twoLinf{0.0};
  Real threeLinf{0.0};
  Real scalePerFace{0.0};
  Index faces{0};
  Index noNeighbour{0};
};

Metrics measure(const mesh::Mesh& m, Kind kind, const std::vector<std::string>& walls) {
  Metrics r;
  for (const std::string& wall : walls) {
    for (const Index faceId : m.boundaryPatch(wall).faceIds()) {
      const auto& face = m.face(faceId);
      const auto& owner = m.cell(face.owner());
      const Vector3 n = face.areaVector() * (1.0 / face.area());
      const Vector3 mHat = n * -1.0;
      const Vector3& xf = face.centroid();
      const Real phiB = value(kind, xf);
      const Real phiP = value(kind, owner.centroid());
      const Vector3 d = xf - owner.centroid();

      // production two-point form
      const auto decomposition = mesh::MeshGeometry::decomposeBoundaryFaceArea(m, face);
      const Real coefficient = decomposition.valid
                                   ? (kGamma * magnitude(decomposition.orthogonal) / magnitude(d))
                                   : (kGamma * face.area() / magnitude(d));
      const Real explicitFlux =
          decomposition.valid
              ? (kGamma * dot(decomposition.nonOrthogonal, gradient(kind, owner.centroid())))
              : 0.0;
      const Real twoFace = (coefficient * (phiB - phiP)) + explicitFlux;

      // three-point reconstruction
      Real threeFace = twoFace;
      const auto opposite = mesh::MeshGeometry::oppositeInteriorFace(m, owner, face);
      bool ok = false;
      if (opposite.has_value()) {
        const auto& of = m.face(*opposite);
        const Index farId = (of.owner() == owner.id()) ? *of.neighbor() : of.owner();
        const Real h1 = dot(xf - owner.centroid(), n);
        const Real h2 = dot(xf - m.cell(farId).centroid(), n);
        if (h1 > 0.0 && h2 > h1) {
          const Vector3 deltaP = owner.centroid() - (xf + (mHat * h1));
          const Vector3 deltaF = m.cell(farId).centroid() - (xf + (mHat * h2));
          const Real pP = phiP - dot(gradient(kind, owner.centroid()), deltaP);
          const Real pF =
              value(kind, m.cell(farId).centroid()) - dot(gradient(kind, m.cell(farId).centroid()),
                                                          deltaF);
          const Real bb = (((pF - phiB) / h2) - ((pP - phiB) / h1)) / (h2 - h1);
          const Real aa = ((pP - phiB) / h1) - (bb * h1);
          threeFace = -kGamma * aa * face.area();
          ok = true;
        }
      }
      if (!ok) ++r.noNeighbour;

      const Real exactFace = kGamma * dot(gradient(kind, xf), face.areaVector());
      const Real faceScale = kGamma * magnitude(gradient(kind, xf)) * face.area();
      r.twoL1 += std::abs(twoFace - exactFace);
      r.threeL1 += std::abs(threeFace - exactFace);
      r.twoLinf = std::max(r.twoLinf, std::abs(twoFace - exactFace) / std::max(faceScale, 1e-300));
      r.threeLinf =
          std::max(r.threeLinf, std::abs(threeFace - exactFace) / std::max(faceScale, 1e-300));
      r.scalePerFace += faceScale;
      ++r.faces;
    }
  }
  return r;
}

void family(const char* label, Kind kind, const std::vector<std::string>& walls,
            const std::vector<std::pair<mesh::Mesh, Real>>& meshes) {
  std::vector<Real> a1, b1, ai, bi, hs;
  for (const auto& [m, h] : meshes) {
    const Metrics r = measure(m, kind, walls);
    const Real scale = (r.scalePerFace > 0.0) ? r.scalePerFace : 1.0;
    a1.push_back(r.twoL1 / scale);
    b1.push_back(r.threeL1 / scale);
    ai.push_back(r.twoLinf);
    bi.push_back(r.threeLinf);
    hs.push_back(h);
    const auto q = mesh::MeshQuality::evaluate(m);
    std::printf("M   %-30s %-9s h %.5f faces %5zu no-neighbour %2zu nonorth %5.2f | L1: 2pt %.4e "
                "3pt %.4e | Linf: 2pt %.4e 3pt %.4e\n",
                label, kindName(kind), h, static_cast<std::size_t>(r.faces),
                static_cast<std::size_t>(r.noNeighbour), q.maxNonOrthogonalityDegrees,
                a1.back(), b1.back(), ai.back(), bi.back());
  }
  for (std::size_t k = 0; k + 1 < hs.size(); ++k) {
    const Real ratio = hs[k] / hs[k + 1];
    const auto ord = [&](const std::vector<Real>& e) {
      return (e[k] > 0.0 && e[k + 1] > 0.0) ? std::log(e[k] / e[k + 1]) / std::log(ratio) : 0.0;
    };
    std::printf("Q   %-30s %-9s order L1: 2pt %6.3f 3pt %6.3f | order Linf: 2pt %6.3f 3pt %6.3f\n",
                label, kindName(kind), ord(a1), ord(b1), ord(ai), ord(bi));
  }
}

std::vector<Vector2> channel(Index nx, Index ny, Real distort) {
  const Real L = 8.0, H = 1.0, lambda = 1.0;
  const Real ax = 0.1 * distort, ay = 0.05 * distort;
  std::vector<Vector2> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = L * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = H * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / L) * std::sin(2.0 * kPi * eta / H));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / H));
      if (i == 0) x = 0.0;
      if (i == nx) x = L;
      if (j == 0) y = 0.0;
      if (j == ny) y = H;
      v.push_back(Vector2{x, y});
    }
  }
  return v;
}

// Annulus over 270 degrees: MESH-003's curved walls, as a single block (the wall geometry is what
// matters here, and no interfaces are needed for a boundary measurement).
std::vector<Vector2> annulus(Index nr, Index nt) {
  const Real sweep = 1.5 * kPi;
  std::vector<Vector2> v;
  for (Index j = 0; j <= nt; ++j) {
    const Real t = sweep * static_cast<Real>(j) / static_cast<Real>(nt);
    for (Index i = 0; i <= nr; ++i) {
      const Real r = 1.0 + (static_cast<Real>(i) / static_cast<Real>(nr));
      v.push_back(Vector2{r * std::cos(t), r * std::sin(t)});
    }
  }
  return v;
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002-INV-001 per-face wall-flux error, exact transfer gradient\n");
  const std::vector<std::string> walls2D{"bottom", "top"};
  const std::vector<std::string> radial{"left", "right"};

  for (const Real distort : {0.0, 1.0}) {
    for (const Kind kind : {Kind::Quadratic, Kind::Cubic}) {
      std::vector<std::pair<mesh::Mesh, Real>> ms;
      for (const auto& [nx, ny] :
           std::vector<std::pair<Index, Index>>{{64, 8}, {96, 12}, {144, 18}, {216, 27}}) {
        ms.emplace_back(mesh::MeshGeometry::createStructuredQuad2D(nx, ny, channel(nx, ny, distort)),
                        1.0 / static_cast<Real>(ny));
      }
      family(distort == 0.0 ? "2D orthogonal" : "2D distorted 48deg", kind, walls2D, ms);
    }
  }
  {
    // Curved walls: the radial field's normal derivative at the inner/outer wall is what a
    // curved-channel pressure/velocity profile produces.
    std::vector<std::pair<mesh::Mesh, Real>> ms;
    for (const auto& [nr, nt] :
         std::vector<std::pair<Index, Index>>{{8, 60}, {12, 90}, {18, 135}, {27, 203}}) {
      ms.emplace_back(mesh::MeshGeometry::createStructuredQuad2D(nr, nt, annulus(nr, nt)),
                      1.0 / static_cast<Real>(nr));
    }
    family("curved annulus 270deg", Kind::Radial, radial, ms);
  }
  {
    // 3D Cartesian: the y-normal walls of a cube.
    std::vector<std::pair<mesh::Mesh, Real>> ms;
    for (const Index n : {8u, 12u, 16u, 24u}) {
      ms.emplace_back(mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0),
                      1.0 / static_cast<Real>(n));
    }
    family("3D Cartesian", Kind::Quadratic, {"ymin", "ymax"}, ms);
    std::vector<std::pair<mesh::Mesh, Real>> ms2;
    for (const Index n : {8u, 12u, 16u, 24u}) {
      ms2.emplace_back(mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0),
                       1.0 / static_cast<Real>(n));
    }
    family("3D Cartesian", Kind::Cubic, {"ymin", "ymax"}, ms2);
  }
  return 0;
}
