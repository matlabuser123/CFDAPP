// P12-MESH-005 evidence probe: the MEASURED worst-case deviations behind the pass/fail unit tests of
// acceptance-gate sections A, C and D (production API only). Prints one line per quantity.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/validation/ErrorNorms.hpp"

using namespace cfd;
using mesh::Mesh;
using mesh::MeshGeometry;

namespace {

struct Box {
  const char* name;
  Index nx, ny, nz;
  Real lx, ly, lz;
  Vector3 origin;
  Mesh build() const { return MeshGeometry::createCartesian3D(nx, ny, nz, lx, ly, lz, origin); }
};
const std::vector<Box> kBoxes = {{"M1 1x1x1", 1, 1, 1, 1.0, 1.0, 1.0, Vector3{}},
                                 {"M2 2x1x1", 2, 1, 1, 1.0, 1.0, 1.0, Vector3{}},
                                 {"M3 2x2x1", 2, 2, 1, 1.0, 1.0, 1.0, Vector3{}},
                                 {"M4 2x2x2", 2, 2, 2, 1.0, 1.0, 1.0, Vector3{}},
                                 {"M5 3x4x5 translated", 3, 4, 5, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1}},
                                 {"M6 8x8x8", 8, 8, 8, 1.0, 1.0, 1.0, Vector3{}}};

Mesh perFace(const Mesh& base) {
  std::vector<mesh::BoundaryPatch> patches;
  for (const auto& f : base.faces())
    if (f.isBoundary()) patches.emplace_back("b" + std::to_string(f.id()), std::vector<Index>{f.id()});
  return Mesh(base.cells(), base.faces(), std::move(patches));
}
template <class F>
boundary::BoundaryConditionSet dirichlet(const Mesh& m, F f) {
  boundary::BoundaryConditionSet b;
  for (const auto& p : m.boundaryPatches())
    b.set(m, p.name(), std::make_unique<boundary::FixedValue>(f(m.face(p.faceIds().front()).centroid())));
  return b;
}
boundary::BoundaryConditionSet neumann(const Mesh& m, const Vector3& g) {
  boundary::BoundaryConditionSet b;
  for (const auto& p : m.boundaryPatches()) {
    const auto& f = m.face(p.faceIds().front());
    b.set(m, p.name(), std::make_unique<boundary::FixedGradient>(dot(g, f.areaVector() * (1.0 / f.area()))));
  }
  return b;
}
fields::ScalarField sample(const Mesh& m, const std::function<Real(const Vector3&)>& f) {
  fields::ScalarField s(m.numberOfCells());
  for (const auto& c : m.cells()) s[c.id()] = f(c.centroid());
  return s;
}
Vector3 vel(const Vector3& p) { return Vector3{1.0 + 0.5 * p.y, 1.0 + 0.5 * p.z, 1.0 + 0.5 * p.x}; }

}  // namespace

int main() {
  std::printf("== A: geometry (measured maxima over every cell/face of each mesh)\n");
  std::printf("%-22s %12s %12s %12s %12s %12s %12s %12s\n", "mesh", "|dV|/V", "|sumV-Vd|/Vd", "||S|-A|/A",
              "|closure|/S", "centroid/L", "|t-1/2|", "AR rel err");
  for (const auto& b : kBoxes) {
    const Mesh m = b.build();
    const Real dx = b.lx / b.nx, dy = b.ly / b.ny, dz = b.lz / b.nz, L = std::max({b.lx, b.ly, b.lz});
    Real dv = 0, sv = 0, ds = 0, cl = 0, ce = 0, dt = 0;
    for (const auto& c : m.cells()) {
      dv = std::max(dv, std::abs(c.volume() - dx * dy * dz) / (dx * dy * dz));
      sv += c.volume();
      const Index i = c.id() % b.nx, j = (c.id() / b.nx) % b.ny, k = c.id() / (b.nx * b.ny);
      const Vector3 e{b.origin.x + (i + 0.5) * dx, b.origin.y + (j + 0.5) * dy, b.origin.z + (k + 0.5) * dz};
      ce = std::max(ce, magnitude(c.centroid() - e) / L);
      Vector3 closure{};
      Real s = 0;
      for (const Index fid : c.faceIds()) {
        const auto& f = m.face(fid);
        closure += f.owner() == c.id() ? f.areaVector() : f.areaVector() * -1.0;
        s += f.area();
      }
      cl = std::max(cl, magnitude(closure) / s);
    }
    for (const auto& f : m.faces()) {
      const Real a = f.areaVector().x != 0 ? dy * dz : (f.areaVector().y != 0 ? dx * dz : dx * dy);
      ds = std::max(ds, std::abs(f.area() - a) / a);
      if (!f.isBoundary()) {
        const Vector3 n = f.areaVector() * (1.0 / f.area());
        const Vector3& p = m.cell(f.owner()).centroid();
        const Real t = dot(f.centroid() - p, n) / dot(m.cell(*f.neighbor()).centroid() - p, n);
        dt = std::max(dt, std::abs(t - 0.5));
      }
    }
    const Real vd = b.lx * b.ly * b.lz;
    const Real ar = std::max({dx, dy, dz}) / std::min({dx, dy, dz});
    const auto q = mesh::MeshQuality::evaluate(m);
    std::printf("%-22s %12.3e %12.3e %12.3e %12.3e %12.3e %12.3e %12.3e   quality: %s\n", b.name, dv,
                std::abs(sv - vd) / vd, ds, cl, ce, dt, std::abs(q.aspectRatio.maximum - ar) / ar,
                q.summaryLine().c_str());
  }

  std::printf("\n== C1: interpolation, max |face value - exact| (interior faces)\n");
  for (const std::size_t k : {std::size_t{4}, std::size_t{5}}) {
    const Mesh m = perFace(kBoxes[k].build());
    const auto lin = [](const Vector3& p) { return 1.0 + p.x - 2.0 * p.y + 3.0 * p.z; };
    const auto s = discretization::interpolate(m, sample(m, lin), dirichlet(m, lin));
    const auto c = discretization::interpolate(m, sample(m, [](const Vector3&) { return 3.7; }),
                                               dirichlet(m, [](const Vector3&) { return 3.7; }));
    Real el = 0, ec = 0, eb = 0;
    for (const auto& f : m.faces()) {
      ec = std::max(ec, std::abs(c[f.id()] - 3.7));
      if (f.isBoundary()) eb = std::max(eb, std::abs(s[f.id()] - lin(f.centroid())));
      else el = std::max(el, std::abs(s[f.id()] - lin(f.centroid())));
    }
    std::printf("%-22s constant %.3e  linear interior %.3e  linear boundary %.3e\n", kBoxes[k].name, ec, el, eb);
  }

  std::printf("\n== C2: gradients, max component error over cells and the fields const, x, y, z, 2x-3y+4z\n");
  const std::vector<Vector3> gs = {Vector3{0, 0, 0}, Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1},
                                   Vector3{2, -3, 4}};
  for (std::size_t bi = 0; bi < kBoxes.size(); ++bi) {
    const Mesh m = perFace(kBoxes[bi].build());
    for (const auto scheme : {discretization::GradientScheme::GreenGauss,
                              discretization::GradientScheme::LeastSquares}) {
      Real ed = 0, en = 0;
      for (std::size_t gi = 0; gi < gs.size(); ++gi) {
        const Vector3 g = gs[gi];
        const Real off = gi == 0 ? 2.5 : 0.0;
        const auto f = [&](const Vector3& p) { return off + dot(g, p); };
        const auto vals = sample(m, f);
        const auto gd = discretization::gradient(m, vals, dirichlet(m, f), scheme);
        for (Index c = 0; c < m.numberOfCells(); ++c)
          ed = std::max({ed, std::abs(gd[c].x - g.x), std::abs(gd[c].y - g.y), std::abs(gd[c].z - g.z)});
        if (bi >= 4) {
          const auto gn = discretization::gradient(m, vals, neumann(m, g), scheme);
          for (Index c = 0; c < m.numberOfCells(); ++c)
            en = std::max({en, std::abs(gn[c].x - g.x), std::abs(gn[c].y - g.y), std::abs(gn[c].z - g.z)});
        }
      }
      // Neumann printed in %.3e (std::to_string's fixed 6 decimals would hide the magnitude).
      char neumannText[32] = "--";
      if (bi >= 4) std::snprintf(neumannText, sizeof neumannText, "%.3e", en);
      std::printf("%-22s %-14s Dirichlet %.3e  Neumann %s\n", kBoxes[bi].name,
                  std::string(discretization::gradientSchemeName(scheme)).c_str(), ed, neumannText);
    }
  }

  std::printf("\n== C3: explicit diffusion of the quadratic (exact 1.7 * 12 = 20.4)\n");
  const auto quad = [](const Vector3& p) {
    return p.x * p.x + 2 * p.y * p.y + 3 * p.z * p.z + p.x * p.y - p.y * p.z + 2 * p.x * p.z - p.x + 1.0;
  };
  for (const Box& b : {Box{"6x5x4 on M5 domain", 6, 5, 4, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1}}, kBoxes[5]}) {
    const Mesh m = perFace(b.build());
    const auto r = discretization::diffusion(m, sample(m, quad), 1.7, dirichlet(m, quad));
    Real e = 0, sum = 0;
    for (const auto& c : m.cells()) {
      e = std::max(e, std::abs(r[c.id()] - 20.4));
      sum += r[c.id()] * c.volume();
    }
    const Real exact = 20.4 * b.lx * b.ly * b.lz;
    std::printf("%-22s max |error| %.3e  telescoping rel. error %.3e\n", b.name, e, std::abs(sum - exact) / exact);
  }

  // Audit finding (pre-existing, dimension-independent): with exactly two cells along a boundary
  // normal, Diffusion.cpp's nextInteriorFaceAwayFrom has no face continuing away from the boundary
  // and accepts a perpendicular one, so the boundary cubic fit uses a transverse point.
  std::printf("\n== C3 finding: explicit diffusion of x^2 + y^2 (+ z^2) with n = 2 along x\n");
  {
    const auto q2 = [](const Vector3& p) { return p.x * p.x + p.y * p.y; };
    const Mesh m2 = perFace(MeshGeometry::createCartesian2D(2, 4, 1.0, 1.0));
    const auto r2 = discretization::diffusion(m2, sample(m2, q2), 1.0, dirichlet(m2, q2));
    Real e2 = 0;
    for (Index i = 0; i < m2.numberOfCells(); ++i) e2 = std::max(e2, std::abs(r2[i] - 4.0));
    const auto q3 = [](const Vector3& p) { return p.x * p.x + p.y * p.y + p.z * p.z; };
    const Mesh m3 = perFace(MeshGeometry::createCartesian3D(2, 4, 4, 1.0, 1.0, 1.0));
    const auto r3 = discretization::diffusion(m3, sample(m3, q3), 1.0, dirichlet(m3, q3));
    Real e3 = 0;
    for (Index i = 0; i < m3.numberOfCells(); ++i) e3 = std::max(e3, std::abs(r3[i] - 6.0));
    const Mesh m4 = perFace(MeshGeometry::createCartesian3D(3, 4, 4, 1.0, 1.0, 1.0));
    const auto r4 = discretization::diffusion(m4, sample(m4, q3), 1.0, dirichlet(m4, q3));
    Real e4 = 0;
    for (Index i = 0; i < m4.numberOfCells(); ++i) e4 = std::max(e4, std::abs(r4[i] - 6.0));
    std::printf("2D 2x4: max |error| %.3e   3D 2x4x4: max |error| %.3e   3D 3x4x4 (control): %.3e\n", e2, e3,
                e4);
  }

  std::printf("\n== C4: convection with rho u(x_f).Sf, rho = 1.3\n");
  {
    const Mesh m = perFace(kBoxes[5].build());
    fields::SurfaceField flux(m.numberOfFaces());
    for (const auto& f : m.faces()) flux[f.id()] = 1.3 * dot(vel(f.centroid()), f.areaVector());
    Real net = 0;
    for (const auto& c : m.cells()) {
      Real s = 0, t = 0;
      for (const Index fid : c.faceIds()) {
        s += m.face(fid).owner() == c.id() ? flux[fid] : -flux[fid];
        t += std::abs(flux[fid]);
      }
      net = std::max(net, std::abs(s) / t);
    }
    std::printf("max |net cell flux| / sum|mdot| = %.3e\n", net);
    const auto band = validation::boundaryAdjacentCells(m, 2);
    const Vector3 g{1.0, -2.0, 3.0};
    const auto lin = [&](const Vector3& p) { return 1.0 + dot(g, p); };
    for (const auto sc : {discretization::ConvectionScheme::Upwind, discretization::ConvectionScheme::Central,
                          discretization::ConvectionScheme::LinearUpwind, discretization::ConvectionScheme::QUICK}) {
      const auto c = discretization::convection(m, sample(m, [](const Vector3&) { return 3.7; }), flux,
                                                dirichlet(m, [](const Vector3&) { return 3.7; }), sc);
      Real ec = 0;
      for (Index i = 0; i < m.numberOfCells(); ++i) ec = std::max(ec, std::abs(c[i]));
      std::string linear = "--";
      if (sc != discretization::ConvectionScheme::Upwind) {
        const auto l = discretization::convection(m, sample(m, lin), flux, dirichlet(m, lin), sc);
        Real el = 0;
        for (const auto& cell : m.cells())
          if (!band[cell.id()]) el = std::max(el, std::abs(l[cell.id()] - 1.3 * dot(vel(cell.centroid()), g)));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3e", el);
        linear = buf;
      }
      std::printf("%-14s constant max |conv| %.3e  linear interior max error %s\n",
                  std::string(discretization::convectionSchemeName(sc)).c_str(), ec, linear.c_str());
    }
  }

  std::printf("\n== D: assembled energy systems (k = 2, cp = 1, Q = 5, FixedValue 1; CSR row by row)\n");
  for (std::size_t bi = 0; bi < 4; ++bi) {
    const Mesh m = kBoxes[bi].build();
    boundary::BoundaryConditionSet bcs;
    for (const auto& p : m.boundaryPatches()) bcs.set(m, p.name(), std::make_unique<boundary::FixedValue>(1.0));
    const auto a = thermal::assembleEnergyEquation(m, fields::ScalarField(m.numberOfCells(), 0.0),
                                                   fields::SurfaceField(m.numberOfFaces(), 0.0),
                                                   thermal::ThermalProperties(2.0, 1.0), bcs, 5.0);
    const auto& A = a.system.matrix();
    std::printf("%s:\n", kBoxes[bi].name);
    for (Index r = 0; r < A.rows(); ++r) {
      std::printf("  row %llu:", static_cast<unsigned long long>(r));
      for (Index k = A.rowOffsetsData()[r]; k < A.rowOffsetsData()[r + 1]; ++k)
        std::printf(" (%llu) %.17g", static_cast<unsigned long long>(A.columnIndicesData()[k]), A.valuesData()[k]);
      std::printf("  | rhs %.17g\n", a.system.rhs()[r]);
    }
  }
  return 0;
}
