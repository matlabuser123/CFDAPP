// P12-DIFF-002-INV-001: does the derived one-sided reconstruction reach second order on real
// CFDApp geometry (orthogonal, translated, distorted, curved, 3D)?
//
// Reconstruction under test (plan.md section 2), per boundary face:
//   m = -n                                     inward unit normal
//   h1 = (x_f - x_P) . n,  h2 = (x_f - x_F) . n        normal projections
//   delta_P = x_P - (x_f + h1 m)                       tangential offset (non-zero off-orthogonal)
//   phi~_P  = phi_P - grad(phi)_P . delta_P             transferred onto the normal ray
//   b = [ (phi~_F - phi_b)/h2 - (phi~_P - phi_b)/h1 ] / (h2 - h1)
//   a = (phi~_P - phi_b)/h1 - b h1                      = dphi/ds at the face, s inward
//   flux_into_owner = -Gamma a |S|
// Reference: Gamma grad(phi)_exact(x_f) . S_out, which is the exact flux through the straight
// discrete face for fields up to quadratic (midpoint rule exact for a linear integrand).
//
// The transfer gradient is taken BOTH exactly (isolating the reconstruction's own order) and from
// the library's GRAD-002 Green-Gauss gradient (what production would have available).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

constexpr Real kGamma = 0.1;
const Real kPi = std::acos(-1.0);

// ---- manufactured fields (functions of position, so they work on any geometry) ----
enum class Kind { Constant, Linear, Quadratic, Cubic };

Real value(Kind k, const Vector3& x) {
  switch (k) {
    case Kind::Constant:
      return 2.5;
    case Kind::Linear:
      return 0.7 + (1.3 * x.x) - (0.9 * x.y) + (0.4 * x.z);
    case Kind::Quadratic:
      return 6.0 * x.y * (1.0 - x.y);  // the Poiseuille profile DIFF-001 used
    default:
      return (x.y * x.y * x.y) + (0.5 * x.x * x.x * x.y);
  }
}

Vector3 gradient(Kind k, const Vector3& x) {
  switch (k) {
    case Kind::Constant:
      return Vector3{};
    case Kind::Linear:
      return Vector3{1.3, -0.9, 0.4};
    case Kind::Quadratic:
      return Vector3{0.0, 6.0 - (12.0 * x.y), 0.0};
    default:
      return Vector3{x.x * x.y, (3.0 * x.y * x.y) + (0.5 * x.x * x.x), 0.0};
  }
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::Constant:
      return "constant";
    case Kind::Linear:
      return "linear";
    case Kind::Quadratic:
      return "quadratic";
    default:
      return "cubic";
  }
}

struct FaceOutcome {
  Real twoPoint{0.0};
  Real threePointExactGradient{0.0};
  Real threePointComputedGradient{0.0};
  Real exact{0.0};
  Real scale{0.0};
  Index faces{0};
  Index withoutNeighbour{0};
  // Per-face errors: the total can hide per-face error through wall-to-wall cancellation
  // (visible for the cubic field on orthogonal meshes), so both are reported.
  Real perFaceTwoL1{0.0};
  Real perFaceThreeL1{0.0};
  Real perFaceTwoLinf{0.0};
  Real perFaceThreeLinf{0.0};
  Real perFaceScale{0.0};
};

// The reconstruction. `gradAt` supplies the gradient used for the tangential transfer.
Real threePointFlux(const mesh::Mesh& m, const mesh::Face& face, Kind kind,
                    const std::function<Vector3(Index)>& gradAt, bool& ok) {
  ok = false;
  const auto& owner = m.cell(face.owner());
  const auto opposite = mesh::MeshGeometry::oppositeInteriorFace(m, owner, face);
  if (!opposite.has_value()) return 0.0;
  const auto& of = m.face(*opposite);
  const Index farId = (of.owner() == owner.id()) ? *of.neighbor() : of.owner();
  const Vector3 n = face.areaVector() * (1.0 / face.area());
  const Vector3 mHat = n * -1.0;
  const Vector3& xf = face.centroid();
  const Real h1 = dot(xf - owner.centroid(), n);
  const Real h2 = dot(xf - m.cell(farId).centroid(), n);
  if (!(h1 > 0.0) || !(h2 > h1)) return 0.0;  // degenerate stencil: no usable inward neighbour
  const Vector3 deltaP = owner.centroid() - (xf + (mHat * h1));
  const Vector3 deltaF = m.cell(farId).centroid() - (xf + (mHat * h2));
  const Real phiB = value(kind, xf);
  const Real phiP = value(kind, owner.centroid()) - dot(gradAt(owner.id()), deltaP);
  const Real phiF = value(kind, m.cell(farId).centroid()) - dot(gradAt(farId), deltaF);
  const Real bb = (((phiF - phiB) / h2) - ((phiP - phiB) / h1)) / (h2 - h1);
  const Real aa = ((phiP - phiB) / h1) - (bb * h1);
  ok = true;
  return -kGamma * aa * face.area();
}

FaceOutcome measure(const mesh::Mesh& m, Kind kind, const std::vector<std::string>& walls,
                    const fields::VectorField* computedGradient) {
  FaceOutcome o;
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = value(kind, c.centroid());
  const auto exactGrad = [&](Index cellId) { return gradient(kind, m.cell(cellId).centroid()); };
  const auto compGrad = [&](Index cellId) {
    return (computedGradient == nullptr) ? gradient(kind, m.cell(cellId).centroid())
                                         : (*computedGradient)[cellId];
  };

  for (const std::string& wall : walls) {
    for (const Index faceId : m.boundaryPatch(wall).faceIds()) {
      const auto& face = m.face(faceId);
      const auto& owner = m.cell(face.owner());
      const Vector3 d = face.centroid() - owner.centroid();
      // Production: the shipped two-point form, with the exact gradient for its explicit part so
      // the comparison isolates the boundary treatment (DIFF-001 showed the explicit part is tiny).
      fields::VectorField g(m.numberOfCells(), Vector3{});
      // (built once per face would be wasteful; use the analytic gradient directly below instead)
      const auto decomposition = mesh::MeshGeometry::decomposeBoundaryFaceArea(m, face);
      const Real coefficient = decomposition.valid
                                   ? (kGamma * magnitude(decomposition.orthogonal) / magnitude(d))
                                   : (kGamma * face.area() / magnitude(d));
      const Real explicitFlux =
          decomposition.valid
              ? (kGamma * dot(decomposition.nonOrthogonal, gradient(kind, owner.centroid())))
              : 0.0;
      o.twoPoint += (coefficient * (value(kind, face.centroid()) - phi[owner.id()])) + explicitFlux;

      bool okExact = false;
      bool okComputed = false;
      const Real fluxExact = threePointFlux(m, face, kind, exactGrad, okExact);
      const Real fluxComputed = threePointFlux(m, face, kind, compGrad, okComputed);
      if (!okExact) {
        ++o.withoutNeighbour;
        // Fallback to the production form, as a production implementation would have to.
        o.threePointExactGradient +=
            (coefficient * (value(kind, face.centroid()) - phi[owner.id()])) + explicitFlux;
        o.threePointComputedGradient +=
            (coefficient * (value(kind, face.centroid()) - phi[owner.id()])) + explicitFlux;
      } else {
        o.threePointExactGradient += fluxExact;
        o.threePointComputedGradient += okComputed ? fluxComputed : fluxExact;
      }
      const Real exactFace = kGamma * dot(gradient(kind, face.centroid()), face.areaVector());
      const Real faceScale = kGamma * magnitude(gradient(kind, face.centroid())) * face.area();
      const Real twoFace = (coefficient * (value(kind, face.centroid()) - phi[owner.id()])) +
                           explicitFlux;
      const Real threeFace = okExact ? fluxExact : twoFace;
      o.perFaceTwoL1 += std::abs(twoFace - exactFace);
      o.perFaceThreeL1 += std::abs(threeFace - exactFace);
      o.perFaceTwoLinf = std::max(o.perFaceTwoLinf, std::abs(twoFace - exactFace));
      o.perFaceThreeLinf = std::max(o.perFaceThreeLinf, std::abs(threeFace - exactFace));
      o.perFaceScale += faceScale;
      o.exact += exactFace;
      o.scale += faceScale;
      ++o.faces;
    }
  }
  return o;
}

// Exact per-patch boundary conditions where they exist, so the library gradient is meaningful.
// Returns nullptr-equivalent (empty) when the field cannot be represented patch-wise.
bool exactConditions(const mesh::Mesh& m, Kind kind, boundary::BoundaryConditionSet& set) {
  if (kind == Kind::Constant) {
    for (const auto& p : m.boundaryPatches()) {
      set.set(m, p.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
    return true;
  }
  if (kind == Kind::Quadratic) {
    // phi = 6y(1-y): zero on y = 0 and y = 1, zero normal derivative on x-normal patches.
    bool ok = true;
    for (const auto& p : m.boundaryPatches()) {
      const auto& f = m.face(p.faceIds().front());
      const Vector3 n = f.areaVector() * (1.0 / f.area());
      bool flat = true;
      for (const Index id : p.faceIds()) {
        const Vector3 nn = m.face(id).areaVector() * (1.0 / m.face(id).area());
        if (magnitude(nn - n) > 1e-12) flat = false;
      }
      if (!flat) { ok = false; break; }
      if (std::abs(std::abs(n.y) - 1.0) < 1e-12) {
        set.set(m, p.name(), std::make_unique<boundary::FixedValue>(0.0));
      } else if (std::abs(n.y) < 1e-12) {
        set.set(m, p.name(), std::make_unique<boundary::FixedGradient>(0.0));
      } else {
        ok = false;
        break;
      }
    }
    return ok;
  }
  return false;
}

void study(const char* label, const std::vector<std::pair<const mesh::Mesh*, Real>>& family,
           const std::vector<std::string>& walls, Kind kind) {
  std::vector<Real> e2, e3, e3c, f2, f3, i2, i3;
  std::vector<Real> hs;
  Index worstNoNeighbour = 0;
  for (const auto& [meshPtr, h] : family) {
    const mesh::Mesh& m = *meshPtr;
    boundary::BoundaryConditionSet set;
    fields::VectorField computed;
    const bool haveBc = exactConditions(m, kind, set);
    if (haveBc) {
      fields::ScalarField phi(m.numberOfCells());
      for (const auto& c : m.cells()) phi[c.id()] = value(kind, c.centroid());
      computed = discretization::gradient(m, phi, set, discretization::GradientScheme::GreenGauss);
    }
    const FaceOutcome o = measure(m, kind, walls, haveBc ? &computed : nullptr);
    const Real scale = (o.scale > 0.0) ? o.scale : 1.0;
    e2.push_back(std::abs(o.twoPoint - o.exact) / scale);
    e3.push_back(std::abs(o.threePointExactGradient - o.exact) / scale);
    e3c.push_back(std::abs(o.threePointComputedGradient - o.exact) / scale);
    const Real fscale = (o.perFaceScale > 0.0) ? o.perFaceScale : 1.0;
    f2.push_back(o.perFaceTwoL1 / fscale);
    f3.push_back(o.perFaceThreeL1 / fscale);
    i2.push_back(o.perFaceTwoLinf / (fscale / static_cast<Real>(o.faces)));
    i3.push_back(o.perFaceThreeLinf / (fscale / static_cast<Real>(o.faces)));
    hs.push_back(h);
    worstNoNeighbour = std::max(worstNoNeighbour, o.withoutNeighbour);
    std::printf("W   %-34s %-9s h %.4f faces %4zu no-neighbour %2zu | 2pt %.4e | 3pt(exact g) "
                "%.4e | 3pt(computed g) %s\n",
                label, kindName(kind), h, static_cast<std::size_t>(o.faces),
                static_cast<std::size_t>(o.withoutNeighbour), e2.back(), e3.back(),
                haveBc ? (std::to_string(e3c.back())).c_str() : "n/a (no exact patch BC)",
                f2.back(), f3.back(), i2.back(), i3.back());
  }
  for (std::size_t k = 0; k + 1 < hs.size(); ++k) {
    const Real r = hs[k] / hs[k + 1];
    const auto ord = [&](const std::vector<Real>& e) {
      return (e[k] > 0.0 && e[k + 1] > 0.0) ? std::log(e[k] / e[k + 1]) / std::log(r) : 0.0;
    };
    std::printf("P   %-34s %-9s order: 2pt %6.3f | 3pt(exact g) %6.3f | 3pt(computed g) %6.3f\n",
                label, kindName(kind), ord(e2), ord(e3), ord(e3c));
  }
  (void)worstNoNeighbour;
}

std::vector<Vector2> cartesian2D(Index nx, Index ny, Real distort, const Vector3& offset) {
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
      v.push_back(Vector2{x + offset.x, y + offset.y});
    }
  }
  return v;
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002-INV-001 wall-flux reconstruction on CFDApp geometry\n");
  const std::vector<std::string> walls2D{"bottom", "top"};

  struct Level {
    Index nx;
    Index ny;
  };
  const std::vector<Level> levels{{64, 8}, {96, 12}, {144, 18}, {216, 27}};

  for (const auto& [label, distort, offset] :
       std::vector<std::tuple<const char*, Real, Vector3>>{
           {"2D orthogonal Cartesian", 0.0, Vector3{}},
           {"2D Cartesian translated", 0.0, Vector3{0.005, 0.0025, 0.0}},
           {"2D distorted (48 deg)", 1.0, Vector3{}}}) {
    for (const Kind kind : {Kind::Constant, Kind::Linear, Kind::Quadratic, Kind::Cubic}) {
      std::vector<mesh::Mesh> meshes;
      std::vector<std::pair<const mesh::Mesh*, Real>> family;
      meshes.reserve(levels.size());
      for (const Level& l : levels) {
        meshes.push_back(mesh::MeshGeometry::createStructuredQuad2D(
            l.nx, l.ny, cartesian2D(l.nx, l.ny, distort, offset)));
      }
      for (std::size_t i = 0; i < levels.size(); ++i) {
        family.emplace_back(&meshes[i], 1.0 / static_cast<Real>(levels[i].ny));
      }
      study(label, family, walls2D, kind);
    }
  }
  return 0;
}
