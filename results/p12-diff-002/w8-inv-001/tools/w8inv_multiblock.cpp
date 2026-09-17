// P12-DIFF-002-W8-INV-001 step 5 (+6, +7): extended refinement of the CURVED CHANNEL multiblock
// case (W8 test 2), raw values recorded before any order is computed.
//
// Investigation only. Geometry, exact solution, solver settings, boundary conditions and the
// extraction are copied verbatim from tests/integration/case/test_multiblock_production_case.cpp
// (frozen hash bcbbadb7...) and cases/curved_channel_multiblock/{solver,boundaries}.json.
//
// NOTE ON THE FAMILY. The test's r = 1.5 family is nr = 8, 12, 18 with nt = 20, 30, 45. It CANNOT
// be extended at r = 1.5: the next nt would be 67.5, not an integer. So this probe runs
//   A: the test's own three grids (reproduction check)
//   B: an independent r = 2 family  nr = 8, 16, 32 / nt = 20, 40, 80
// The polar (r, theta) mapping is exactly self-similar under refinement, unlike the
// structured-quad case's fixed-amplitude distortion.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

const Real kPi = std::acos(-1.0);
const Real kSweep = 1.5 * kPi;
constexpr Real kR1 = 1.0, kR2 = 2.0;
constexpr Real kDensity = 1.0, kViscosity = 0.1, kFlowRate = 1.0;

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

// The generator's vertex arithmetic, verbatim -- applied to the COMMITTED case definition so the
// interfaces, patches and boundary conditions are exactly the production ones.
io::CaseDefinition curvedDefinition(Index nr, Index nt) {
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

pressure_velocity::SIMPLESettings settings() {
  pressure_velocity::SIMPLESettings s;
  s.maxIterations = 8000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5;
  s.pressureTolerance = 5e-4;
  s.continuityTolerance = 1e-6;
  s.convectionScheme = discretization::ConvectionScheme::LinearUpwind;
  s.gradientScheme = discretization::GradientScheme::GreenGauss;
  s.nonOrthogonalCorrections = 1;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  return s;
}

struct Row {
  Index nr{0}, nt{0};
  bool converged{false};
  Index iterations{0};
  Real velocityL2{0.0};
  Real G{0.0}, gSignedError{0.0};
  Real rise{0.0}, riseExact{0.0}, riseSignedError{0.0};
  Real maxNonOrtho{0.0}, meanNonOrtho{0.0}, maxSkew{0.0}, maxAspect{0.0};
  Real r0{0.0}, r1{0.0};  // the radii the rise reference uses (they move with the grid)
  Real seconds{0.0};
};

// forceMetrics / maxIters: used only by the plateau diagnostic, to compare the field at the
// committed gate against the field much further along the same iteration history. Never proposed
// as test behaviour.
Row run(Index nr, Index nt, Real tolMultiplier = 1.0, Index maxIters = 0,
        bool forceMetrics = false) {
  const CurvedExact exact;
  Row row;
  row.nr = nr; row.nt = nt;
  const auto setup = io::CaseBuilder{}.build(curvedDefinition(nr, nt));
  const Mesh& mesh = setup.mesh;
  const auto q = mesh::MeshQuality::evaluate(mesh);
  row.maxNonOrtho = q.maxNonOrthogonalityDegrees;
  row.meanNonOrtho = q.meanNonOrthogonalityDegrees;
  row.maxSkew = q.maxSkewness;
  row.maxAspect = q.maximumAspectRatio;

  auto st = settings();
  st.velocityTolerance *= tolMultiplier;
  st.pressureTolerance *= tolMultiplier;
  st.continuityTolerance *= tolMultiplier;
  if (maxIters > 0) st.maxIterations = maxIters;
  const pressure_velocity::SIMPLE simple(st, 0);
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = simple.solve(mesh, setup.fluid, setup.velocityBoundaries,
                              setup.pressureBoundaries, setup.initialVelocity,
                              setup.initialPressure);
  row.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  row.converged = r.converged();
  row.iterations = r.iterations;
  if (!row.converged && !forceMetrics) return row;

  // VERBATIM extraction (middle block rows nt..2nt-1).
  Real e2 = 0.0, volume = 0.0, rise = 0.0;
  std::vector<std::array<Real, 5>> ring(nr, {0.0, 0.0, 0.0, 0.0, 0.0});
  for (Index rw = nt; rw < 2 * nt; ++rw) {
    for (Index i = 0; i < nr; ++i) {
      const auto& cell = mesh.cell((rw * nr) + i);
      const Vector2& x = cell.centroid();
      const Real rad = magnitude(x);
      const Real t = angleOf(x);
      const Vector2 uExact = exact.u(rad) * eTheta(t);
      const Vector2 e = r.velocity[cell.id()] - uExact;
      e2 += dot(e, e) * cell.volume();
      volume += cell.volume();
      const Real p = r.pressure[cell.id()];
      auto& s = ring[i];
      s[0] += 1.0; s[1] += t; s[2] += t * t; s[3] += p; s[4] += t * p;
    }
    rise += r.pressure[(rw * nr) + nr - 1] - r.pressure[rw * nr];
  }
  row.velocityL2 = std::sqrt(e2 / volume);
  for (const auto& s : ring) {
    row.G += ((s[0] * s[4]) - (s[1] * s[3])) / ((s[0] * s[2]) - (s[1] * s[1]));
  }
  row.G /= static_cast<Real>(nr);
  row.gSignedError = row.G - exact.pressureGradient();
  row.rise = rise / static_cast<Real>(nt);
  row.r0 = magnitude(mesh.cell(nt * nr).centroid());
  row.r1 = magnitude(mesh.cell((nt * nr) + nr - 1).centroid());
  row.riseExact = exact.radialRise(row.r0, row.r1);
  row.riseSignedError = row.rise - row.riseExact;
  return row;
}

void report(const char* title, const std::vector<Row>& rows, Real ratio) {
  const CurvedExact exact;
  std::printf("\n%s   (exact G = %.10f)\n", title, exact.pressureGradient());
  std::printf("  %4s %5s %5s %7s %14s %14s %16s %16s %8s\n", "nr", "nt", "conv", "iters",
              "velocity L2", "G", "G SIGNED err", "rise SIGNED err", "time s");
  for (const auto& r : rows) {
    std::printf("  %4lld %5lld %5s %7lld %14.8e %14.10f %+16.8e %+16.8e %8.1f\n",
                (long long)r.nr, (long long)r.nt, r.converged ? "yes" : "NO",
                (long long)r.iterations, r.velocityL2, r.G, r.gSignedError, r.riseSignedError,
                r.seconds);
  }
  std::printf("  %4s %5s %10s %10s %9s %9s %10s %10s\n", "nr", "nt", "maxNonOrt", "meanNonOrt",
              "maxSkew", "maxAspect", "rise r0", "rise r1");
  for (const auto& r : rows) {
    std::printf("  %4lld %5lld %10.6f %10.6f %9.6f %9.4f %10.6f %10.6f\n", (long long)r.nr,
                (long long)r.nt, r.maxNonOrtho, r.meanNonOrtho, r.maxSkew, r.maxAspect, r.r0, r.r1);
  }
  std::printf("\n  observed orders (r = %.2f)\n", ratio);
  for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
    const Real v = std::log(rows[k].velocityL2 / rows[k + 1].velocityL2) / std::log(ratio);
    const Real g = std::log(std::abs(rows[k].gSignedError) / std::abs(rows[k + 1].gSignedError)) /
                   std::log(ratio);
    const Real ri = std::log(std::abs(rows[k].riseSignedError) /
                             std::abs(rows[k + 1].riseSignedError)) / std::log(ratio);
    const bool gCross = (rows[k].gSignedError * rows[k + 1].gSignedError) < 0.0;
    const bool rCross = (rows[k].riseSignedError * rows[k + 1].riseSignedError) < 0.0;
    std::printf("    pair %zu: velocity %7.4f | G %7.4f%s | rise %7.4f%s\n", k, v, g,
                gCross ? " (SIGN CHANGE)" : "", ri, rCross ? " (SIGN CHANGE)" : "");
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = (argc > 1 && std::string(argv[1]) == "quick");

  // ---- W8-R4 CALIBRATION -----------------------------------------------------------------------
  // §13's R4 defines the precondition as |q(gate) - q(20000 iterations)| <= 10 % of |q - q_exact|.
  // A literal 20000-iteration re-solve inside the production test costs ~650 s. This measures how
  // much of the 20000-iteration drift is already visible at smaller budgets, so R4 can be
  // implemented at a defensible cost with a MEASURED justification rather than a guessed one.
  if (argc > 1 && std::string(argv[1]) == "r4cal") {
    std::printf("# W8-R4 CALIBRATION -- velocity L2 drift vs extra iteration budget\n");
    std::printf("# finest grid 18x45 only; exact velocity reference is analytical (error = L2)\n\n");
    const Row gate = run(18, 45);
    std::printf("  gate:           iters %5lld  velocity L2 %.10e\n", (long long)gate.iterations,
                gate.velocityL2);
    for (const Index budget : {5000, 10000, 20000}) {
      const Row p = run(18, 45, 1e-7, budget, /*forceMetrics=*/true);
      const Real drift = std::abs(p.velocityL2 - gate.velocityL2);
      std::printf("  budget %6lld:  iters %5lld  velocity L2 %.10e   drift %.4e"
                  "  = %.3f %% of the error\n",
                  (long long)budget, (long long)p.iterations, p.velocityL2, drift,
                  100.0 * drift / gate.velocityL2);
    }
    return 0;
  }

  // Same residual-plateau diagnostic as the structured probe: how far does the SOLUTION move
  // between the committed gate and 20000 iterations? If it moves materially, the observed order
  // W8 computes is not a discretisation order. Diagnosis only.
  if (argc > 1 && std::string(argv[1]) == "plateau") {
    std::printf("# W8-INV-001 MULTIBLOCK RESIDUAL-PLATEAU DIAGNOSTIC\n");
    std::printf("# committed gate vs 20000 iterations at an unreachable tolerance.\n");
    std::vector<Row> gate, plateau;
    for (auto [nr, nt] : std::vector<std::pair<Index, Index>>{{8, 20}, {12, 30}, {18, 45}}) {
      gate.push_back(run(nr, nt));
      plateau.push_back(run(nr, nt, 1e-7, 20000, /*forceMetrics=*/true));
    }
    report("COMMITTED GATE (what W8 measures)", gate, 1.5);
    report("PLATEAU (20000 iterations, metrics force-extracted)", plateau, 1.5);
    std::printf("\n  DRIFT between the committed gate and the plateau\n");
    std::printf("  %8s %16s %16s %11s | %16s %16s %11s\n", "grid", "velL2 gate",
                "velL2 plateau", "rel change", "G err gate", "G err plateau", "rel change");
    for (std::size_t k = 0; k < gate.size(); ++k) {
      std::printf("  %3lldx%-4lld %16.8e %16.8e %11.3e | %+16.8e %+16.8e %11.3e\n",
                  (long long)gate[k].nr, (long long)gate[k].nt, gate[k].velocityL2,
                  plateau[k].velocityL2,
                  std::abs(plateau[k].velocityL2 - gate[k].velocityL2) /
                      std::max(gate[k].velocityL2, 1e-300),
                  gate[k].gSignedError, plateau[k].gSignedError,
                  std::abs(plateau[k].gSignedError - gate[k].gSignedError) /
                      std::max(std::abs(gate[k].gSignedError), 1e-300));
    }
    return 0;
  }

  std::printf("# W8-INV-001 curved-channel multiblock, extended refinement\n");
  std::printf("# polar (r,theta) family -- exactly self-similar under refinement\n");
  std::printf("# RAW values first; orders afterwards. Velocity / G / rise reported SEPARATELY.\n");

  std::vector<Row> famA;
  for (auto [nr, nt] : std::vector<std::pair<Index, Index>>{{8, 20}, {12, 30}, {18, 45}}) {
    if (quick && nr > 12) break;
    famA.push_back(run(nr, nt));
  }
  report("FAMILY A -- the test's own r = 1.5 family (reproduction check)", famA, 1.5);

  std::vector<Row> famB;
  for (auto [nr, nt] : std::vector<std::pair<Index, Index>>{{8, 20}, {16, 40}, {32, 80}}) {
    if (quick && nr > 16) break;
    famB.push_back(run(nr, nt));
  }
  report("FAMILY B -- independent r = 2 family (the r=1.5 family cannot extend: nt would be 67.5)",
         famB, 2.0);

  std::printf("\n#CSV family,nr,nt,conv,iters,velocityL2,G,gSignedError,riseSignedError,"
              "maxNonOrtho,maxSkew,r0,r1\n");
  const auto dump = [](const char* f, const std::vector<Row>& rows) {
    for (const auto& r : rows) {
      std::printf("CSV,%s,%lld,%lld,%d,%lld,%.12e,%.12e,%.12e,%.12e,%.6f,%.6f,%.8f,%.8f\n", f,
                  (long long)r.nr, (long long)r.nt, r.converged ? 1 : 0, (long long)r.iterations,
                  r.velocityL2, r.G, r.gSignedError, r.riseSignedError, r.maxNonOrtho, r.maxSkew,
                  r.r0, r.r1);
    }
  };
  dump("A", famA);
  dump("B", famB);
  return 0;
}
