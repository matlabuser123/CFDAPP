// P12-GRAD-002-DRIFT-001: why do the distorted-Poiseuille (StructuredQuad) and curved-channel
// (MultiBlock) solutions keep moving after the committed convergence gate?
//
// Investigation probe only. It drives the PRODUCTION case path (CaseReader -> CaseBuilder ->
// SIMPLE with the case's own solver.json settings, reference cell 0, exactly as ProjectRunner
// does) and changes ONE solver setting at a time (the variant), so that the mechanism can be
// isolated. Nothing here is a proposed setting.
//
//   drift_probe <case> <n1> <n2> <variant> <mode> [args]
//     case     sq  : cases/poiseuille_distorted with the W8 test's structured_quad mapping (nx, ny)
//              mb  : cases/curved_channel_multiblock with the W8 test's polar blocks (nr, nt)
//     variant  base  committed solver.json
//              rc    face_flux = rhie_chow
//              n0    non_orthogonal_corrections = 0
//              n2    non_orthogonal_corrections = 2
//              ls    gradient_scheme = least_squares
//              up    convection_scheme = upwind
//              bicg  pressure linear solver = BiCGSTAB
//              tight both linear solvers: relative 1e-13, absolute 1e-15
//     mode     gate               committed tolerances; metrics
//              hist N K           unreachable tolerance, N iterations; residuals every K
//              ckpt N1,N2,...     unreachable tolerance; state at each N (separate runs from the
//                                 same initial state -- the solve is deterministic)
//
// The extraction (velocity L2 over the developed window / middle block, dp/dx least-squares fit,
// per-ring G fit) is copied from the W8 tests. The odd-even indicators are new diagnostics.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

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

// ---- probe --------------------------------------------------------------------------------------
struct Args {
  std::string kase, variant, mode;
  Index n1{0}, n2{0};
};

void applyVariant(pressure_velocity::SIMPLESettings& s, const std::string& v) {
  using algebra::LinearSolverType;
  if (v == "base") return;
  if (v == "rc") { s.faceFlux = pressure_velocity::FaceFluxScheme::RhieChow; return; }
  if (v == "n0") { s.nonOrthogonalCorrections = 0; return; }
  if (v == "n2") { s.nonOrthogonalCorrections = 2; return; }
  if (v == "ls") { s.gradientScheme = discretization::GradientScheme::LeastSquares; return; }
  if (v == "up") { s.convectionScheme = discretization::ConvectionScheme::Upwind; return; }
  if (v == "bicg") { s.pressureSolver.type = LinearSolverType::BiCGSTAB; return; }
  // Combined variants (round 4): Rhie-Chow plus one more change.
  if (v.rfind("rc+", 0) == 0) {
    s.faceFlux = pressure_velocity::FaceFluxScheme::RhieChow;
    applyVariant(s, v.substr(3));
    return;
  }
  if (v == "a5") { s.velocityRelaxation = 0.5; s.pressureRelaxation = 0.2; return; }
  if (v == "a3") { s.velocityRelaxation = 0.3; s.pressureRelaxation = 0.1; return; }
  if (v == "tight") {
    s.momentumSolver.relativeTolerance = 1e-13; s.momentumSolver.absoluteTolerance = 1e-15;
    s.pressureSolver.relativeTolerance = 1e-13; s.pressureSolver.absoluteTolerance = 1e-15;
    s.momentumSolver.maxIterations = 5000; s.pressureSolver.maxIterations = 50000;
    return;
  }
  std::fprintf(stderr, "unknown variant %s\n", v.c_str());
  std::exit(2);
}

struct Metrics {
  Real velocityL2{0}, q{0}, qErr{0};           // q = dp/dx (sq) or G (mb)
  Real cbAll{0}, cbI{0}, cbJ{0}, fitRms{0};    // odd-even projections of the fit residual
  Real uMaxAbs{0};
  Real qA{0}, qB{0};                           // sq: dp/dx over [0.50, 0.675] L and [0.675, 0.85] L
};

Metrics sqMetrics(const io::SimulationSetup& setup, const pressure_velocity::SIMPLEResult& r,
                  Index nx) {
  Metrics m;
  const auto& mesh = setup.mesh;
  Real e2 = 0, vol = 0, sw = 0, sx = 0, sxx = 0, sp = 0, sxp = 0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
    const Real exact = 6.0 * kMeanVelocity * (c.y / kHeight) * (1.0 - (c.y / kHeight));
    const Vector2 e = r.velocity[cell.id()] - Vector2{exact, 0.0};
    const Real v = cell.volume();
    e2 += dot(e, e) * v; vol += v;
    const Real p = r.pressure[cell.id()];
    sw += v; sx += v * c.x; sxx += v * c.x * c.x; sp += v * p; sxp += v * c.x * p;
  }
  m.velocityL2 = std::sqrt(e2 / vol);
  m.q = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  m.qErr = m.q - kExactDpdx;
  {
    const Real mid = 0.5 * (kDevelopedStart + kDevelopedEnd);
    Real w[2] = {0, 0}, x[2] = {0, 0}, xx[2] = {0, 0}, pp[2] = {0, 0}, xp[2] = {0, 0};
    for (const auto& cell : mesh.cells()) {
      const Vector2& c = cell.centroid();
      if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
      const int k = (c.x < mid) ? 0 : 1;
      const Real v = cell.volume(), p = r.pressure[cell.id()];
      w[k] += v; x[k] += v * c.x; xx[k] += v * c.x * c.x; pp[k] += v * p; xp[k] += v * c.x * p;
    }
    m.qA = ((w[0] * xp[0]) - (x[0] * pp[0])) / ((w[0] * xx[0]) - (x[0] * x[0])) - kExactDpdx;
    m.qB = ((w[1] * xp[1]) - (x[1] * pp[1])) / ((w[1] * xx[1]) - (x[1] * x[1])) - kExactDpdx;
  }
  const Real a = (sp - m.q * sx) / sw;
  Real n = 0, s2 = 0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
    const Index i = cell.id() % nx, j = cell.id() / nx;
    const Real res = r.pressure[cell.id()] - (a + m.q * c.x);
    m.cbAll += (((i + j) % 2) ? -1.0 : 1.0) * res;
    m.cbI += ((i % 2) ? -1.0 : 1.0) * res;
    m.cbJ += ((j % 2) ? -1.0 : 1.0) * res;
    s2 += res * res; n += 1;
  }
  m.cbAll /= n; m.cbI /= n; m.cbJ /= n; m.fitRms = std::sqrt(s2 / n);
  for (Index k = 0; k < r.velocity.size(); ++k) m.uMaxAbs = std::max(m.uMaxAbs, magnitude(r.velocity[k]));
  return m;
}

Metrics mbMetrics(const io::SimulationSetup& setup, const pressure_velocity::SIMPLEResult& r,
                  Index nr, Index nt) {
  Metrics m;
  const CurvedExact exact;
  const auto& mesh = setup.mesh;
  Real e2 = 0, vol = 0;
  std::vector<std::array<Real, 5>> ring(nr, {0, 0, 0, 0, 0});
  for (Index rw = nt; rw < 2 * nt; ++rw) {
    for (Index i = 0; i < nr; ++i) {
      const auto& cell = mesh.cell((rw * nr) + i);
      const Vector2& x = cell.centroid();
      const Real t = angleOf(x);
      const Vector2 e = r.velocity[cell.id()] - exact.u(magnitude(x)) * eTheta(t);
      e2 += dot(e, e) * cell.volume(); vol += cell.volume();
      const Real p = r.pressure[cell.id()];
      auto& s = ring[i];
      s[0] += 1; s[1] += t; s[2] += t * t; s[3] += p; s[4] += t * p;
    }
  }
  m.velocityL2 = std::sqrt(e2 / vol);
  std::vector<Real> gi(nr), ai(nr);
  for (Index i = 0; i < nr; ++i) {
    const auto& s = ring[i];
    gi[i] = ((s[0] * s[4]) - (s[1] * s[3])) / ((s[0] * s[2]) - (s[1] * s[1]));
    ai[i] = (s[3] - gi[i] * s[1]) / s[0];
    m.q += gi[i];
  }
  m.q /= static_cast<Real>(nr);
  m.qErr = m.q - exact.pressureGradient();
  Real n = 0, s2 = 0;
  for (Index rw = nt; rw < 2 * nt; ++rw) {
    for (Index i = 0; i < nr; ++i) {
      const auto& cell = mesh.cell((rw * nr) + i);
      const Real res = r.pressure[cell.id()] - (ai[i] + gi[i] * angleOf(cell.centroid()));
      m.cbAll += (((i + rw) % 2) ? -1.0 : 1.0) * res;
      m.cbI += ((i % 2) ? -1.0 : 1.0) * res;
      m.cbJ += ((rw % 2) ? -1.0 : 1.0) * res;
      s2 += res * res; n += 1;
    }
  }
  m.cbAll /= n; m.cbI /= n; m.cbJ /= n; m.fitRms = std::sqrt(s2 / n);
  for (Index k = 0; k < r.velocity.size(); ++k) m.uMaxAbs = std::max(m.uMaxAbs, magnitude(r.velocity[k]));
  return m;
}

// Any committed structured-Cartesian case, unchanged (kase "dir:<case directory>", n1 = nx): no
// analytical reference, so velocityL2 is the RMS speed and q the fitted mean x-gradient; the
// odd-even projections are of the residual of a least-squares plane a + b x + c y over all cells.
Metrics genericMetrics(const io::SimulationSetup& setup, const pressure_velocity::SIMPLEResult& r,
                       Index nx) {
  Metrics m;
  const auto& mesh = setup.mesh;
  // Normal equations for p ~ a + b x + c y (volume weighted).
  Real S[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, B[3] = {0, 0, 0}, s2 = 0, vol = 0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    const Real v = cell.volume();
    const Real phi[3] = {1.0, c.x, c.y};
    for (int a = 0; a < 3; ++a) {
      for (int b = 0; b < 3; ++b) S[a][b] += v * phi[a] * phi[b];
      B[a] += v * phi[a] * r.pressure[cell.id()];
    }
    s2 += v * dot(r.velocity[cell.id()], r.velocity[cell.id()]);
    vol += v;
  }
  m.velocityL2 = std::sqrt(s2 / vol);
  // Cramer's rule.
  const auto det3 = [](Real M[3][3]) {
    return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
           M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
           M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
  };
  const Real D = det3(S);
  Real coef[3];
  for (int k = 0; k < 3; ++k) {
    Real M[3][3];
    for (int a = 0; a < 3; ++a)
      for (int b = 0; b < 3; ++b) M[a][b] = (b == k) ? B[a] : S[a][b];
    coef[k] = det3(M) / D;
  }
  m.q = coef[1];
  Real n = 0, f2 = 0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    const Index i = cell.id() % nx, j = cell.id() / nx;
    const Real res = r.pressure[cell.id()] - (coef[0] + coef[1] * c.x + coef[2] * c.y);
    m.cbAll += (((i + j) % 2) ? -1.0 : 1.0) * res;
    m.cbI += ((i % 2) ? -1.0 : 1.0) * res;
    m.cbJ += ((j % 2) ? -1.0 : 1.0) * res;
    f2 += res * res; n += 1;
  }
  m.cbAll /= n; m.cbI /= n; m.cbJ /= n; m.fitRms = std::sqrt(f2 / n);
  for (Index k = 0; k < r.velocity.size(); ++k) m.uMaxAbs = std::max(m.uMaxAbs, magnitude(r.velocity[k]));
  return m;
}

const char* statusName(pressure_velocity::SIMPLEStatus s) {
  using S = pressure_velocity::SIMPLEStatus;
  switch (s) {
    case S::Converged: return "Converged";
    case S::MaxIterations: return "MaxIterations";
    case S::Diverging: return "Diverging";
    case S::Stagnated: return "Stagnated";
    case S::NonFiniteState: return "NonFinite";
    case S::MomentumFailure: return "MomentumFailure";
    case S::PressureCorrectionFailure: return "PressureFailure";
    default: return "Other";
  }
}

struct Run {
  pressure_velocity::SIMPLEResult result;
  Metrics metrics;
  double seconds{0};
};

Run runOnce(const Args& a, const io::SimulationSetup& setup, Real tolScale, Index maxIters,
            const pressure_velocity::SIMPLEProgressCallback& cb) {
  auto s = setup.solverSettings;
  applyVariant(s, a.variant);
  s.velocityTolerance *= tolScale;
  s.pressureTolerance *= tolScale;
  s.continuityTolerance *= tolScale;
  if (maxIters > 0) s.maxIterations = maxIters;
  const pressure_velocity::SIMPLE simple(s, 0, nullptr, nullptr, nullptr, cb);
  const auto t0 = std::chrono::steady_clock::now();
  Run run;
  run.result = simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries,
                            setup.pressureBoundaries, setup.initialVelocity,
                            setup.initialPressure);
  run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  run.metrics = (a.kase == "sq" || a.kase == "sq0") ? sqMetrics(setup, run.result, a.n1)
                : (a.kase == "mb") ? mbMetrics(setup, run.result, a.n1, a.n2)
                                   : genericMetrics(setup, run.result, a.n1);
  return run;
}

void printHeader(const char* q) {
  std::printf("  %7s %-13s %15s %16s %16s %11s %11s %11s %11s %9s %9s %9s %9s %8s\n", "iters",
              "status", "velocity L2", q, "q signed err", "cb(i+j)", "cb(i)", "cb(j)", "fit rms",
              "u res", "v res", "p res", "cont", "sec");
  std::printf("  (sq rows add: split-window dp/dx signed errors [0.50,0.675]L | [0.675,0.85]L)\n");
}
void printRow(const Run& r) {
  const auto& m = r.metrics;
  const auto& s = r.result;
  std::printf("  %7lld %-13s %15.9e %16.10f %+16.9e %+11.3e %+11.3e %+11.3e %11.4e %9.2e %9.2e"
              " %9.2e %9.2e %8.1f\n",
              (long long)s.iterations, statusName(s.status), m.velocityL2, m.q, m.qErr, m.cbAll,
              m.cbI, m.cbJ, m.fitRms, s.finalUResidual, s.finalVResidual, s.finalPressureResidual,
              s.finalContinuityResidual, r.seconds);
  if (m.qA != 0.0 || m.qB != 0.0) {
    std::printf("          split dp/dx err %+.6e | %+.6e\n", m.qA, m.qB);
  }
}

Real maxDiff(const fields::VectorField& a, const fields::VectorField& b) {
  Real d = 0;
  for (Index k = 0; k < a.size(); ++k) d = std::max(d, magnitude(a[k] - b[k]));
  return d;
}
Real maxDiff(const fields::ScalarField& a, const fields::ScalarField& b) {
  Real d = 0;
  for (Index k = 0; k < a.size(); ++k) d = std::max(d, std::abs(a[k] - b[k]));
  return d;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fprintf(stderr, "usage: drift_probe <sq|mb> <n1> <n2> <variant> <gate|hist|ckpt> ...\n");
    return 2;
  }
  Args a;
  a.kase = argv[1];
  a.n1 = std::atoll(argv[2]);
  a.n2 = std::atoll(argv[3]);
  a.variant = argv[4];
  a.mode = argv[5];
  const bool generic = a.kase.rfind("dir:", 0) == 0;
  const io::SimulationSetup setup = io::CaseBuilder{}.build(
      generic ? io::CaseReader{}.read(a.kase.substr(4))
              : (a.kase == "sq"    ? sqDefinition(a.n1, a.n2)
                 : a.kase == "sq0" ? sqDefinition(a.n1, a.n2, /*distorted=*/false)
                                   : mbDefinition(a.n1, a.n2)));
  const bool sqLike = a.kase == "sq" || a.kase == "sq0";
  const char* qName = generic ? "fit dp/dx" : (sqLike ? "dp/dx" : "G");
  {
    auto s = setup.solverSettings;
    applyVariant(s, a.variant);
    std::printf("# case %s %lldx%lld variant %s mode %s; cells %lld\n", a.kase.c_str(),
                (long long)a.n1, (long long)a.n2, a.variant.c_str(), a.mode.c_str(),
                (long long)setup.mesh.numberOfCells());
    std::printf("# settings: tol u %.1e p %.1e c %.1e | alpha %.2f/%.2f | nonOrth %lld | grad %s |"
                " conv %d | flux %s | mom %d rel %.0e | pres %d rel %.0e\n",
                s.velocityTolerance, s.pressureTolerance, s.continuityTolerance,
                s.velocityRelaxation, s.pressureRelaxation, (long long)s.nonOrthogonalCorrections,
                s.gradientScheme == discretization::GradientScheme::GreenGauss ? "GG" : "LS",
                static_cast<int>(s.convectionScheme), pressure_velocity::faceFluxSchemeName(s.faceFlux),
                static_cast<int>(s.momentumSolver.type), s.momentumSolver.relativeTolerance,
                static_cast<int>(s.pressureSolver.type), s.pressureSolver.relativeTolerance);
    if (generic) std::printf("# generic committed case (no analytical reference): 'velocity L2'"
                             " = RMS speed, q = fitted mean dp/dx, q signed err = q\n");
    else if (sqLike) std::printf("# exact dp/dx %.10f\n", kExactDpdx);
    else std::printf("# exact G %.10f\n", CurvedExact{}.pressureGradient());
  }

  if (a.mode == "gate") {
    const Run r = runOnce(a, setup, 1.0, 0, nullptr);
    printHeader(qName);
    printRow(r);
    return 0;
  }

  if (a.mode == "hist" && argc >= 8) {
    const Index n = std::atoll(argv[6]);
    const Index k = std::atoll(argv[7]);
    std::vector<std::array<Real, 5>> h;
    h.reserve(static_cast<std::size_t>(n));
    const Run r = runOnce(a, setup, 1e-9, n, [&](const pressure_velocity::SIMPLEIterationProgress& p) {
      h.push_back({p.uResidual, p.vResidual, p.pressureResidual, p.continuityResidual,
                   p.globalMassImbalance});
    });
    std::printf("\n  residual history (value at the iteration, and the window minimum/maximum"
                " since the previous row)\n");
    std::printf("  %7s %10s %10s %10s %10s %10s | %10s %10s | %10s %10s\n", "iter", "u", "v", "p",
                "cont", "global", "u min", "u max", "p min", "p max");
    Real umin = 1e300, umax = 0, pmin = 1e300, pmax = 0;
    for (std::size_t it = 0; it < h.size(); ++it) {
      umin = std::min(umin, h[it][0]); umax = std::max(umax, h[it][0]);
      pmin = std::min(pmin, h[it][2]); pmax = std::max(pmax, h[it][2]);
      if ((it + 1) % static_cast<std::size_t>(k) == 0 || it + 1 == h.size() || it < 3) {
        std::printf("  %7zu %10.3e %10.3e %10.3e %10.3e %10.3e | %10.3e %10.3e | %10.3e %10.3e\n",
                    it + 1, h[it][0], h[it][1], h[it][2], h[it][3], h[it][4], umin, umax, pmin, pmax);
        umin = 1e300; umax = 0; pmin = 1e300; pmax = 0;
      }
    }
    std::printf("\n  final state\n");
    printHeader(qName);
    printRow(r);
    return 0;
  }

  if (a.mode == "ckpt" && argc >= 7) {
    std::vector<Index> ns;
    std::stringstream ss(argv[6]);
    std::string tok;
    while (std::getline(ss, tok, ',')) ns.push_back(std::atoll(tok.c_str()));
    const Run gate = runOnce(a, setup, 1.0, 0, nullptr);
    std::printf("\n  committed gate\n");
    printHeader(qName);
    printRow(gate);
    std::printf("\n  checkpoints (unreachable tolerance; state after exactly N iterations)\n");
    printHeader(qName);
    std::vector<Run> runs;
    for (const Index n : ns) {
      runs.push_back(runOnce(a, setup, 1e-9, n, nullptr));
      printRow(runs.back());
      std::fflush(stdout);
    }
    std::printf("\n  field change: max |du| and max |dp| between consecutive rows (first row vs gate)\n");
    const Run* prev = &gate;
    for (const auto& r : runs) {
      std::printf("  %7lld -> %7lld  max|du| %.4e  max|dp| %.4e  d(velocity L2) %+.4e  d(q) %+.4e\n",
                  (long long)prev->result.iterations, (long long)r.result.iterations,
                  maxDiff(prev->result.velocity, r.result.velocity),
                  maxDiff(prev->result.pressure, r.result.pressure),
                  r.metrics.velocityL2 - prev->metrics.velocityL2, r.metrics.q - prev->metrics.q);
      prev = &r;
    }
    return 0;
  }
  std::fprintf(stderr, "bad mode/arguments\n");
  return 2;
}
