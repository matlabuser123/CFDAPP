// P12-GRAD-002 A2 dry-run: the lagged-sweep truncation of the production Green-Gauss gradient
// (kGreenGaussSkewCorrectionSweeps) on the COMMITTED 2D production meshes, whose boundary
// non-orthogonality does not vanish under refinement (poiseuille_distorted: grid lines meet the
// walls at up to 48 degrees). Reports max |e_K - e_64| / |grad| for a linear and a smooth field,
// with every patch Neumann (the truncation needs no exact boundary data).
// Mesh builders: copied verbatim from drift-001/tools/drift_probe.cpp (the W8 tests' own).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"

using namespace cfd;

namespace {

const Real kPi = std::acos(-1.0);

// ---- StructuredQuad (verbatim from test_structured_quad_production_case.cpp) --------------------
constexpr Real kLength = 8.0, kHeight = 1.0, kMeanVelocity = 1.0, kViscosity = 0.1;
constexpr Real kDevelopedStart = 0.50 * kLength, kDevelopedEnd = 0.85 * kLength;
const Real kExactDpdx = -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight);

std::vector<Vector2> mappedVertices(Index nx, Index ny, Real length, Real height, Real ax, Real ay,
                                    Real lambda) {
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = length * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = height * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / length) * std::sin(2.0 * kPi * eta / height));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / height));
      if (i == 0) x = 0.0;
      if (i == nx) x = length;
      if (j == 0) y = 0.0;
      if (j == ny) y = height;
      vertices.push_back(Vector2{x, y});
    }
  }
  return vertices;
}

io::CaseDefinition sqDefinition(Index nx, Index ny, bool distorted = true) {
  io::CaseDefinition d = io::CaseReader{}.read("cases/poiseuille_distorted");
  d.mesh.type = "structured_quad";
  d.mesh.nx = nx;
  d.mesh.ny = ny;
  // distorted == false: the same structured_quad path on an ORTHOGONAL mapping (the control).
  d.mesh.vertices = distorted ? mappedVertices(nx, ny, kLength, kHeight, 0.1, 0.05, 1.0)
                              : mappedVertices(nx, ny, kLength, kHeight, 0.0, 0.0, 1.0);
  return d;
}

// ---- MultiBlock (verbatim from test_multiblock_production_case.cpp / w8inv_multiblock.cpp) -----
const Real kSweep = 1.5 * kPi;
constexpr Real kR1 = 1.0, kR2 = 2.0, kDensity = 1.0, kMbViscosity = 0.1, kFlowRate = 1.0;
struct CurvedExact {
  Real ln2 = std::log(2.0);
  Real a = -(4.0 / 3.0) * ln2;
  Real amplitude = kFlowRate / ((2.0 * ln2) - 0.75 + (a * 1.5) - (a * ln2));
  [[nodiscard]] Real u(Real r) const { return amplitude * ((r * std::log(r)) + (a * r) - (a / r)); }
  [[nodiscard]] Real pressureGradient() const { return 2.0 * kMbViscosity * amplitude; }
};
Real angleOf(const Vector2& p) {
  Real t = std::atan2(p.y, p.x);
  if (t < -1e-9) t += 2.0 * kPi;
  return t;
}
Vector2 eTheta(Real t) { return Vector2{-std::sin(t), std::cos(t)}; }

io::CaseDefinition mbDefinition(Index nr, Index nt) {
  io::CaseDefinition d = io::CaseReader{}.read("cases/curved_channel_multiblock");
  const Index total = 3 * nt;
  const std::array<const char*, 3> names{"bend_a", "bend_b", "bend_c"};
  std::vector<io::MeshBlockConfig> blocks;
  for (Index b = 0; b < 3; ++b) {
    io::MeshBlockConfig block{names[b], nr, nt, {}};
    for (Index j = 0; j <= nt; ++j) {
      const Real t = (kSweep * static_cast<Real>((b * nt) + j)) / static_cast<Real>(total);
      for (Index i = 0; i <= nr; ++i) {
        const Real r = kR1 + ((kR2 - kR1) * static_cast<Real>(i) / static_cast<Real>(nr));
        block.vertices.push_back(Vector2{r * std::cos(t), r * std::sin(t)});
      }
    }
    blocks.push_back(std::move(block));
  }
  d.mesh.blocks = std::move(blocks);
  return d;
}


Real linearField(const Vector3& x) { return 0.7 + (1.3 * x.x) - (0.9 * x.y); }
Vector3 linearGradient(const Vector3&) { return Vector3{1.3, -0.9, 0.0}; }
Real smoothField(const Vector3& x) { return std::sin(0.8 * x.x + 0.1) * std::cos(1.7 * x.y - 0.2); }
Vector3 smoothGradient(const Vector3& x) {
  return Vector3{0.8 * std::cos(0.8 * x.x + 0.1) * std::cos(1.7 * x.y - 0.2),
                 -1.7 * std::sin(0.8 * x.x + 0.1) * std::sin(1.7 * x.y - 0.2), 0.0};
}

void study(const char* label, const mesh::Mesh& m, bool dirichlet) {
  Real mf = 0.0;
  for (const auto& f : m.faces()) {
    if (!f.isBoundary()) continue;
    const Vector3 d = f.centroid() - m.cell(f.owner()).centroid();
    mf = std::max(mf, magnitude(cross(d, f.areaVector())) / (magnitude(d) * f.area()));
  }
  for (int which = 0; which < 2; ++which) {
    const auto value = which == 0 ? linearField : smoothField;
    const auto grad = which == 0 ? linearGradient : smoothGradient;
    boundary::BoundaryConditionSet bc;
    for (const auto& patch : m.boundaryPatches()) {
      if (dirichlet) {
        bc.set(m, patch.name(), std::make_unique<boundary::FixedValue>(0.3));
      } else {
        bc.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(-0.2));
      }
    }
    fields::ScalarField phi(m.numberOfCells());
    Real scale = 0.0;
    for (const auto& c : m.cells()) {
      phi[c.id()] = value(c.centroid());
      scale = std::max(scale, magnitude(grad(c.centroid())));
    }
    const auto g64 = discretization::greenGaussGradient(m, phi, bc, 64);
    std::printf("TRUNC %-26s %-6s %-9s m_f %.3f |", label, which == 0 ? "linear" : "smooth",
                dirichlet ? "dirichlet" : "neumann", mf);
    for (const Index k : {1u, 2u, 3u, 4u, 5u, 6u, 8u, 12u, 16u}) {
      const auto gk = discretization::greenGaussGradient(m, phi, bc, k);
      Real w = 0.0;
      for (Index i = 0; i < m.numberOfCells(); ++i) w = std::max(w, magnitude(gk[i] - g64[i]));
      std::printf(" %zu:%.2e", static_cast<std::size_t>(k), w / scale);
    }
    std::printf("\n");
  }
}

}  // namespace

int main() {
  std::printf("# A2 production-mesh sweep truncation: max|g_K - g_64| / max|grad|, K = production %zu\n",
              static_cast<std::size_t>(discretization::kGreenGaussSkewCorrectionSweeps));
  for (const auto& [nx, ny] : std::vector<std::pair<Index, Index>>{{64, 8}, {144, 18}, {216, 27}, {512, 64}}) {
    const io::SimulationSetup s = io::CaseBuilder{}.build(sqDefinition(nx, ny));
    char label[64];
    std::snprintf(label, sizeof(label), "poiseuille_distorted %zux%zu", static_cast<std::size_t>(nx),
                  static_cast<std::size_t>(ny));
    study(label, s.mesh, false);
    study(label, s.mesh, true);
  }
  for (const auto& [nr, nt] : std::vector<std::pair<Index, Index>>{{8, 20}, {18, 45}, {36, 90}}) {
    const io::SimulationSetup s = io::CaseBuilder{}.build(mbDefinition(nr, nt));
    char label[64];
    std::snprintf(label, sizeof(label), "curved_channel %zux%zu", static_cast<std::size_t>(nr),
                  static_cast<std::size_t>(nt));
    study(label, s.mesh, false);
    study(label, s.mesh, true);
  }
  return 0;
}
