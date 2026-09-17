// P12-GRAD-002-INV-001, steps 3-6: operator-level localization on MESH-001's distorted Poiseuille
// mesh, with the EXACT analytic field imposed, so "which reconstruction is locally more accurate"
// is answered directly instead of inferred from a converged CFD observable.
//
// On this mesh both analytic fields have exactly representable per-patch boundary conditions:
//   u = 6 U (y/H)(1 - y/H):  walls u = 0 (FixedValue, exact -- the wall faces lie exactly on
//                            y = 0 and y = H because the generator snaps boundary vertices),
//                            inlet/outlet du/dn = 0 (FixedGradient, exact).
//   p = G x:                 inlet/outlet p = G x_patch (FixedValue, exact -- those faces lie
//                            exactly on x = 0 and x = L), walls dp/dn = 0 (exact, wall normals
//                            are exactly +/-y).
// So every number below is a genuine discretization error, not a boundary-data artefact.
//
// The probe re-implements all three boundary treatments itself and cross-checks each against the
// library it is linked with, then blends them (step 5). Production source is untouched.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <tuple>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kGradient = -1.2;
const Real kPi = std::acos(-1.0);

std::vector<Vector2> poiseuilleVertices(Index nx, Index ny) {
  const Real ax = 0.1, ay = 0.05, lambda = 1.0;
  std::vector<Vector2> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = kLength * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = kHeight * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / kLength) * std::sin(2.0 * kPi * eta / kHeight));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / kHeight));
      if (i == 0) x = 0.0;
      if (i == nx) x = kLength;
      if (j == 0) y = 0.0;
      if (j == ny) y = kHeight;
      v.push_back(Vector2{x, y});
    }
  }
  return v;
}

Real uExact(const Vector3& x) {
  return 6.0 * kMeanVelocity * (x.y / kHeight) * (1.0 - (x.y / kHeight));
}
Vector3 uExactGradient(const Vector3& x) {
  return Vector3{0.0, 6.0 * kMeanVelocity * ((1.0 / kHeight) - (2.0 * x.y / (kHeight * kHeight))),
                 0.0};
}
Real pExact(const Vector3& x) { return kGradient * x.x; }
Vector3 pExactGradient(const Vector3&) { return Vector3{kGradient, 0.0, 0.0}; }

boundary::BoundaryConditionSet velocityConditions(const mesh::Mesh& m) {
  boundary::BoundaryConditionSet set;
  set.set(m, "bottom", std::make_unique<boundary::FixedValue>(0.0));
  set.set(m, "top", std::make_unique<boundary::FixedValue>(0.0));
  set.set(m, "left", std::make_unique<boundary::FixedGradient>(0.0));
  set.set(m, "right", std::make_unique<boundary::FixedGradient>(0.0));
  return set;
}

boundary::BoundaryConditionSet pressureConditions(const mesh::Mesh& m) {
  boundary::BoundaryConditionSet set;
  set.set(m, "bottom", std::make_unique<boundary::FixedGradient>(0.0));
  set.set(m, "top", std::make_unique<boundary::FixedGradient>(0.0));
  set.set(m, "left", std::make_unique<boundary::FixedValue>(pExact(Vector3{0, 0, 0})));
  set.set(m, "right", std::make_unique<boundary::FixedValue>(pExact(Vector3{kLength, 0, 0})));
  return set;
}

// ---- the three boundary treatments, re-implemented here ----

struct Pairing {
  bool exists{false};
  Index oppositeFace{0};
  Index farCell{0};
  bool oldApplies{false};  // the pre-GRAD-002 exact-zero predicate plus its equal-area test
};

Pairing pairingFor(const mesh::Mesh& m, const mesh::Cell& cell, const mesh::Face& boundaryFace) {
  Pairing p;
  const auto opposite = mesh::MeshGeometry::oppositeInteriorFace(m, cell, boundaryFace);
  if (!opposite.has_value()) return p;
  const mesh::Face& of = m.face(*opposite);
  p.exists = true;
  p.oppositeFace = *opposite;
  p.farCell = (of.owner() == cell.id()) ? *of.neighbor() : of.owner();
  const Vector3 d = boundaryFace.centroid() - cell.centroid();
  const bool aligned = cross(d, boundaryFace.areaVector()) == Vector3{};
  const bool equalArea =
      std::abs(boundaryFace.area() - of.area()) <= 1e-12 * boundaryFace.area();
  p.oldApplies = aligned && equalArea;
  return p;
}

// Correction vectors (already multiplied by cell volume, as the sum is divided by V once).
struct Corrections {
  Vector3 oldDelta{};  // pair replacement minus the two plain contributions
  Vector3 newDelta{};  // S_O * e_O
  bool oldActive{false};
  bool newActive{false};
};

Corrections correctionsFor(const mesh::Mesh& m, const mesh::Cell& cell,
                           const mesh::Face& boundaryFace, const fields::ScalarField& phi,
                           const fields::SurfaceField& faceValues, const Vector3& ownerGradient) {
  Corrections c;
  const Pairing p = pairingFor(m, cell, boundaryFace);
  if (!p.exists) return c;
  const mesh::Face& of = m.face(p.oppositeFace);
  const Vector3 d = m.cell(p.farCell).centroid() - cell.centroid();
  const Real L = magnitude(d);
  if (!(L > 0.0)) return c;
  const Vector3 inward = d * (1.0 / L);
  const auto crossing = mesh::MeshGeometry::ownerNeighborCrossing(m, of);
  if (!crossing.has_value()) return c;

  const Vector3 sB = boundaryFace.areaVector();  // outward from the owner by construction
  const Vector3 sO = (of.owner() == cell.id()) ? of.areaVector() : (of.areaVector() * -1.0);
  const Real phiP = phi[cell.id()];
  const Real phiFar = phi[p.farCell];
  const Real phiB = faceValues[boundaryFace.id()];
  const Real phiO = faceValues[p.oppositeFace];

  // OLD: replace {B, O} by n_out * (-dphi/dn_in) * V, using straight-line distances.
  if (p.oldApplies) {
    const Real h1 = magnitude(boundaryFace.centroid() - cell.centroid());
    const Real h2 = magnitude(d);
    const Real a = -h2 / (h1 * (h1 + h2));
    const Real b = (h2 - h1) / (h1 * h2);
    const Real cc = h1 / (h2 * (h1 + h2));
    const Real dPhiDInward = (a * phiB) + (b * phiP) + (cc * phiFar);
    const Vector3 nOut = sB * (1.0 / boundaryFace.area());
    c.oldDelta = (nOut * (-dPhiDInward * cell.volume())) - ((sB * phiB) + (sO * phiO));
    c.oldActive = true;
  }

  // NEW: boundary-consistent value at the opposite face.
  const Real denom = dot(inward, sB);
  if (denom < -1e-6 * L * boundaryFace.area()) {
    const Real t = dot(cell.centroid() - boundaryFace.centroid(), sB) / denom;
    if (t > 0.0) {
      const Vector3 bPrime = cell.centroid() - (inward * t);
      const Real phiBPrime = phiB + dot(ownerGradient, bPrime - boundaryFace.centroid());
      const Real second =
          2.0 * (((phiBPrime - phiP) / t) + ((phiFar - phiP) / L)) / (t + L);
      const Real w = crossing->t;
      c.newDelta = sO * (-0.5 * w * (1.0 - w) * L * L * second);
      c.newActive = true;
    }
  }
  return c;
}

// Green-Gauss with the boundary treatment blended by alpha: 0 = pre-GRAD-002, 1 = GRAD-002.
// Mirrors greenGaussGradient exactly otherwise: one initial sweep, then `sweeps` correction sweeps
// in which every skewed internal face value is re-evaluated with P12-NUM-003's skew correction and
// every oblique Neumann boundary face with P12-MESH-001's normal/tangential split, both using the
// previous sweep's gradient. Verified against the library by the X rows in main().
struct ObliqueFace {
  Index faceId{0};
  Real normalDistance{0.0};
  Vector3 tangentialOffset{};
};

fields::VectorField blendedGradient(const mesh::Mesh& m, const fields::ScalarField& phi,
                                    const boundary::BoundaryConditionSet& bc, Real alpha,
                                    Index sweeps) {
  fields::SurfaceField faceValues = discretization::interpolate(m, phi, bc);

  std::vector<Index> skewed;
  for (const auto& face : m.faces()) {
    if (face.isBoundary()) continue;
    const auto crossing = mesh::MeshGeometry::ownerNeighborCrossing(m, face);
    if (crossing.has_value() && crossing->skewVector != Vector3{}) skewed.push_back(face.id());
  }
  std::vector<ObliqueFace> oblique;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    if (discretization::prescribesBoundaryValue(
            boundary::boundaryConditionForFace(m, face.id(), bc).type())) {
      continue;
    }
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    const Vector3 n = face.areaVector() * (1.0 / face.area());
    const Real dn = dot(d, n);
    if (!(dn > 0.0)) continue;
    const Vector3 t = d - (n * dn);
    if (t == Vector3{}) continue;  // exactly aligned: the two formulations agree bit for bit
    oblique.push_back(ObliqueFace{face.id(), dn, t});
  }

  const auto sweep = [&](const fields::VectorField* previous) {
    fields::VectorField out(m.numberOfCells(), Vector3{});
    for (const auto& cell : m.cells()) {
      Vector3 sum{};
      for (const Index faceId : cell.faceIds()) {
        const auto& face = m.face(faceId);
        const Vector3 sf =
            (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
        sum += sf * faceValues[faceId];
      }
      for (const Index faceId : cell.faceIds()) {
        const auto& face = m.face(faceId);
        if (!face.isBoundary()) continue;
        const Vector3 g = (previous == nullptr) ? Vector3{} : (*previous)[cell.id()];
        const Corrections c = correctionsFor(m, cell, face, phi, faceValues, g);
        if (c.oldActive) sum += c.oldDelta * (1.0 - alpha);
        if (c.newActive) sum += c.newDelta * alpha;
      }
      out[cell.id()] = sum * (1.0 / cell.volume());
    }
    return out;
  };

  fields::VectorField result = sweep(nullptr);
  for (Index s = 0; s < sweeps && (!skewed.empty() || !oblique.empty() || true); ++s) {
    for (const Index faceId : skewed) {
      faceValues[faceId] =
          discretization::interpolateInternalFaceSkewCorrected(m, m.face(faceId), phi, result);
    }
    for (const ObliqueFace& o : oblique) {
      const auto& face = m.face(o.faceId);
      const auto& condition = boundary::boundaryConditionForFace(m, o.faceId, bc);
      const auto* scalar = dynamic_cast<const boundary::ScalarBoundaryCondition*>(&condition);
      if (scalar == nullptr) continue;
      faceValues[o.faceId] = scalar->boundaryValue(phi[face.owner()], o.normalDistance) +
                             dot(result[face.owner()], o.tangentialOffset);
    }
    result = sweep(&result);
  }
  return result;
}

enum class Region { Interior, Wall, InletOutlet, Corner };

Region regionOf(const mesh::Mesh& m, const mesh::Cell& cell) {
  bool wall = false;
  bool io = false;
  for (const Index f : cell.faceIds()) {
    const auto& face = m.face(f);
    if (!face.isBoundary()) continue;
    const Vector3 n = face.areaVector() * (1.0 / face.area());
    if (std::abs(n.y) > 0.5) wall = true;
    if (std::abs(n.x) > 0.5) io = true;
  }
  if (wall && io) return Region::Corner;
  if (wall) return Region::Wall;
  if (io) return Region::InletOutlet;
  return Region::Interior;
}

const char* regionName(Region r) {
  switch (r) {
    case Region::Interior:
      return "interior";
    case Region::Wall:
      return "wall";
    case Region::InletOutlet:
      return "inlet/outlet";
    default:
      return "corner";
  }
}

struct RegionError {
  Real linf{0.0};
  Real l2{0.0};
  Real volume{0.0};
  Index cells{0};
};

void report(const char* label, const mesh::Mesh& m, const fields::VectorField& g,
            Vector3 (*exact)(const Vector3&)) {
  RegionError per[4];
  for (const auto& cell : m.cells()) {
    const Real e = magnitude(g[cell.id()] - exact(cell.centroid()));
    RegionError& r = per[static_cast<int>(regionOf(m, cell))];
    r.linf = std::max(r.linf, e);
    r.l2 += e * e * cell.volume();
    r.volume += cell.volume();
    ++r.cells;
  }
  for (int i = 0; i < 4; ++i) {
    if (per[i].cells == 0) continue;
    std::printf("R   %-34s %-12s cells %5zu Linf %.4e L2 %.4e\n", label,
                regionName(static_cast<Region>(i)), static_cast<std::size_t>(per[i].cells),
                per[i].linf, std::sqrt(per[i].l2 / per[i].volume));
  }
}

}  // namespace

int main() {
  const Index nx = 144;
  const Index ny = 18;
  const mesh::Mesh m = mesh::MeshGeometry::createStructuredQuad2D(nx, ny,
                                                                  poiseuilleVertices(nx, ny));
  std::printf("# INV-001 operator study on the distorted Poiseuille mesh %zux%zu\n",
              static_cast<std::size_t>(nx), static_cast<std::size_t>(ny));

  // How many boundary faces did the OLD exact-zero predicate accept?
  Index total = 0;
  Index oldAligned = 0;
  for (const auto& cell : m.cells()) {
    for (const Index f : cell.faceIds()) {
      const auto& face = m.face(f);
      if (!face.isBoundary()) continue;
      ++total;
      if (pairingFor(m, cell, face).oldApplies) ++oldAligned;
    }
  }
  std::printf("# old exact-zero predicate accepted %zu of %zu boundary faces\n",
              static_cast<std::size_t>(oldAligned), static_cast<std::size_t>(total));

  for (const auto& [name, phiFn, gradFn, bcFn] :
       std::vector<std::tuple<const char*, Real (*)(const Vector3&), Vector3 (*)(const Vector3&),
                              boundary::BoundaryConditionSet (*)(const mesh::Mesh&)>>{
           {"u (parabolic)", uExact, uExactGradient, velocityConditions},
           {"p (linear)", pExact, pExactGradient, pressureConditions}}) {
    fields::ScalarField phi(m.numberOfCells());
    for (const auto& c : m.cells()) phi[c.id()] = phiFn(c.centroid());
    const auto bc = bcFn(m);

    // Cross-check: the library's own gradient against the probe's alpha endpoint.
    const auto library = discretization::gradient(m, phi, bc,
                                                  discretization::GradientScheme::GreenGauss);
    for (const Real alpha : {0.0, 1.0}) {
      const auto probe = blendedGradient(m, phi, bc, alpha, 4);
      Real diff = 0.0;
      for (Index i = 0; i < probe.size(); ++i) {
        diff = std::max(diff, magnitude(probe[i] - library[i]));
      }
      std::printf("X   %-14s alpha %.2f vs library gradient: max difference %.3e\n", name, alpha,
                  diff);
    }
    report((std::string("library GG ") + name).c_str(), m, library, gradFn);
    // Least squares: more accurate than either Green-Gauss variant, and the scheme with which the
    // two libraries agree exactly and SIMPLE converges fast -- so it discriminates "accuracy slows
    // convergence" from "the cell-local Green-Gauss correction slows convergence".
    const auto ls = discretization::gradient(m, phi, bc,
                                             discretization::GradientScheme::LeastSquares);
    report((std::string("library LS ") + name).c_str(), m, ls, gradFn);
    for (const Real alpha : {0.0, 0.25, 0.5, 0.75, 1.0}) {
      const auto probe = blendedGradient(m, phi, bc, alpha, 4);
      Real linf = 0.0;
      Real l2 = 0.0;
      Real vol = 0.0;
      Real wallLinf = 0.0;
      for (const auto& cell : m.cells()) {
        const Real e = magnitude(probe[cell.id()] - gradFn(cell.centroid()));
        linf = std::max(linf, e);
        l2 += e * e * cell.volume();
        vol += cell.volume();
        if (regionOf(m, cell) == Region::Wall) wallLinf = std::max(wallLinf, e);
      }
      std::printf("A   %-14s alpha %.2f | Linf %.4e L2 %.4e wall Linf %.4e\n", name, alpha,
                  linf, std::sqrt(l2 / vol), wallLinf);
    }
  }
  return 0;
}
