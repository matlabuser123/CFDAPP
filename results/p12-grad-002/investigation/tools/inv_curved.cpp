// P12-GRAD-002-INV-001, step 2b / step 9: exact reproduction of MESH-003's curved-channel grid
// convergence, replicating tests/integration/case/test_multiblock_production_case.cpp's own
// annular block generator, developed region (block bend_b, 90..180 deg), metrics and error
// definitions -- so the same program runs against the pre-GRAD-002 library and GRAD-002.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"

using namespace cfd;

namespace {

constexpr const char* kCase = "cases/curved_channel_multiblock";
const Real kPi = std::acos(-1.0);
const Real kSweep = 1.5 * kPi;
constexpr Real kR1 = 1.0;
constexpr Real kR2 = 2.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kFlowRate = 1.0;

struct CurvedExact {
  Real ln2 = std::log(2.0);
  Real a = -(4.0 / 3.0) * ln2;
  Real amplitude = kFlowRate / ((2.0 * ln2) - 0.75 + (a * 1.5) - (a * ln2));
  [[nodiscard]] Real u(Real r) const { return amplitude * ((r * std::log(r)) + (a * r) - (a / r)); }
  [[nodiscard]] Real pressureGradient() const { return 2.0 * kViscosity * amplitude; }
  [[nodiscard]] Real radialRise(Real r0, Real r1) const {
    const int n = 4000;
    const Real h = (r1 - r0) / n;
    Real sum = 0.0;
    for (int k = 0; k <= n; ++k) {
      const Real r = r0 + (k * h);
      const Real w = (k == 0 || k == n) ? 1.0 : (k % 2 == 1 ? 4.0 : 2.0);
      sum += w * u(r) * u(r) / r;
    }
    return kDensity * sum * h / 3.0;
  }
};

Real angleOf(const Vector2& p) {
  Real t = std::atan2(p.y, p.x);
  if (t < -1e-9) t += 2.0 * kPi;
  return t;
}

Vector2 eTheta(Real t) { return Vector2{-std::sin(t), std::cos(t)}; }

std::vector<io::MeshBlockConfig> annularBlocks(Index nr, Index nt) {
  const std::array<const char*, 3> names{"bend_a", "bend_b", "bend_c"};
  std::vector<io::MeshBlockConfig> blocks;
  const Index total = 3 * nt;
  for (Index b = 0; b < 3; ++b) {
    io::MeshBlockConfig block{names[b], nr, nt, {}};
    for (Index j = 0; j <= nt; ++j) {
      const Real t = kSweep * static_cast<Real>((b * nt) + j) / static_cast<Real>(total);
      for (Index i = 0; i <= nr; ++i) {
        const Real r = kR1 + ((kR2 - kR1) * static_cast<Real>(i) / static_cast<Real>(nr));
        block.vertices.push_back(Vector2{r * std::cos(t), r * std::sin(t)});
      }
    }
    blocks.push_back(std::move(block));
  }
  return blocks;
}

struct Metrics {
  bool ok{false};
  std::size_t iterations{0};
  Real massImbalance{0.0};
  Real velocityL2{0.0};
  Real maxRadialVelocity{0.0};
  Real pressureGradient{0.0};
  Real radialRise{0.0};
  Real radialRiseExact{0.0};
};

Metrics solve(Index nr, Index nt) {
  Metrics m;
  io::CaseDefinition definition = io::CaseReader{}.read(kCase);
  definition.mesh.blocks = annularBlocks(nr, nt);
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() /
      ("inv_curved_" + std::to_string(nr) + "x" + std::to_string(nt));
  std::filesystem::remove_all(tmp);
  std::filesystem::copy(kCase, tmp, std::filesystem::copy_options::recursive);
  io::CaseWriter::write(tmp, definition);

  const app::ProjectRunResult run = app::ProjectRunner::run(tmp);
  if (!run.simpleResult.has_value() || !run.mesh.has_value()) {
    std::printf("  run failed: %s\n", run.errorMessage.c_str());
    std::filesystem::remove_all(tmp);
    return m;
  }
  const auto& r = *run.simpleResult;
  const mesh::Mesh& msh = *run.mesh;
  const CurvedExact exact;
  m.ok = true;
  m.iterations = static_cast<std::size_t>(r.iterations);
  m.massImbalance = r.globalMassImbalance;

  Real e2 = 0.0;
  Real volume = 0.0;
  std::vector<std::array<Real, 5>> ring(nr, {0.0, 0.0, 0.0, 0.0, 0.0});
  Real rise = 0.0;
  for (Index row = nt; row < 2 * nt; ++row) {
    for (Index i = 0; i < nr; ++i) {
      const auto& cell = msh.cell((row * nr) + i);
      const Vector2& x = cell.centroid();
      const Real rad = magnitude(x);
      const Real t = angleOf(x);
      const Vector2 uExact = eTheta(t) * exact.u(rad);
      const Vector2 e = r.velocity[cell.id()] - uExact;
      e2 += dot(e, e) * cell.volume();
      volume += cell.volume();
      const Vector2 eR{std::cos(t), std::sin(t)};
      m.maxRadialVelocity =
          std::max(m.maxRadialVelocity, std::abs(dot(r.velocity[cell.id()], eR)));
      const Real p = r.pressure[cell.id()];
      auto& s = ring[i];
      s[0] += 1.0;
      s[1] += t;
      s[2] += t * t;
      s[3] += p;
      s[4] += t * p;
    }
    rise += r.pressure[(row * nr) + nr - 1] - r.pressure[row * nr];
  }
  m.velocityL2 = std::sqrt(e2 / volume);
  for (const auto& s : ring) {
    m.pressureGradient += ((s[0] * s[4]) - (s[1] * s[3])) / ((s[0] * s[2]) - (s[1] * s[1]));
  }
  m.pressureGradient /= static_cast<Real>(nr);
  m.radialRise = rise / static_cast<Real>(nt);
  m.radialRiseExact = exact.radialRise(magnitude(msh.cell(nt * nr).centroid()),
                                       magnitude(msh.cell((nt * nr) + nr - 1).centroid()));
  std::filesystem::remove_all(tmp);
  return m;
}

}  // namespace

int main() {
  const CurvedExact exact;
  std::printf("# INV-001 MESH-003 curved channel reproduction (exact G %.6f)\n",
              exact.pressureGradient());
  struct Level {
    Index nr;
    Index nt;
  };
  const std::vector<Level> levels{{8, 20}, {12, 30}, {18, 45}};
  std::vector<Metrics> all;
  for (const Level& l : levels) {
    const Metrics m = solve(l.nr, l.nt);
    if (!m.ok) return 1;
    std::printf("C   %2zux%-3zu x3 iter %5zu | vel L2 %.4e max|u_r| %.3e | G %.6f rel err %.4e | "
                "rise %.6f exact %.6f err %.4e | mass %.2e\n",
                static_cast<std::size_t>(l.nr), static_cast<std::size_t>(l.nt), m.iterations,
                m.velocityL2, m.maxRadialVelocity, m.pressureGradient,
                std::abs(m.pressureGradient - exact.pressureGradient()) /
                    std::abs(exact.pressureGradient()),
                m.radialRise, m.radialRiseExact, std::abs(m.radialRise - m.radialRiseExact),
                m.massImbalance);
    all.push_back(m);
  }
  for (std::size_t k = 0; k + 1 < all.size(); ++k) {
    const Real velocityOrder = std::log(all[k].velocityL2 / all[k + 1].velocityL2) / std::log(1.5);
    const Real g0 = std::abs(all[k].pressureGradient - exact.pressureGradient());
    const Real g1 = std::abs(all[k + 1].pressureGradient - exact.pressureGradient());
    const Real r0 = std::abs(all[k].radialRise - all[k].radialRiseExact);
    const Real r1 = std::abs(all[k + 1].radialRise - all[k + 1].radialRiseExact);
    std::printf("O   pair %zu: order velocity %.3f, G %.3f, radial rise %.3f | rise error %.4e -> "
                "%.4e (%s)\n",
                k, velocityOrder, std::log(g0 / g1) / std::log(1.5),
                std::log(r0 / r1) / std::log(1.5), r0, r1,
                r1 < r0 ? "decreasing" : "NOT MONOTONE");
  }
  return 0;
}
