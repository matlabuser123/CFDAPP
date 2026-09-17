// P12-GRAD-002-INV-001, step 2: exact reproduction of MESH-001's distorted-Poiseuille grid
// convergence through the production path, replicating
// tests/integration/case/test_structured_quad_production_case.cpp's own mesh mapping, developed
// region, metrics and order/GCI arithmetic -- so the SAME program can be linked against the
// pre-GRAD-002 library and against GRAD-002 and the two compared directly.
//
// Investigation only: production source is untouched, no test, threshold or golden output is
// modified, and nothing here is shipped.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

constexpr const char* kCase = "cases/poiseuille_distorted";
constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kDevelopedStart = 0.50 * kLength;
constexpr Real kDevelopedEnd = 0.85 * kLength;
const Real kPi = std::acos(-1.0);
constexpr Real kExactPressureGradient = -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight);

// Identical to the test's mappedVertices(nx, ny, L, H, 0.1, 0.05, 1.0).
Real gDistort = 1.0;  // scales the mesh distortion amplitudes (investigation only)

std::vector<Vector2> poiseuilleVertices(Index nx, Index ny) {
  const Real ax = 0.1 * gDistort;
  const Real ay = 0.05 * gDistort;
  const Real lambda = 1.0;
  std::vector<Vector2> vertices;
  vertices.reserve((nx + 1) * (ny + 1));
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
      vertices.push_back(Vector2{x, y});
    }
  }
  return vertices;
}

Real columnFlow(const cfd::fields::SurfaceField& massFlux, Index nx, Index ny, Index i) {
  Real flow = 0.0;
  for (Index j = 0; j < ny; ++j) flow += massFlux[(j * (nx + 1)) + i];
  return i == 0 ? -flow : flow;
}

struct Metrics {
  bool ok{false};
  Real velocityL1{0.0};
  Real velocityL2{0.0};
  Real velocityLinf{0.0};
  Real pressureGradient{0.0};
  Real maxColumnFlowError{0.0};
  Real massImbalance{0.0};
  Real maxNonOrthogonality{0.0};
  Real maxSkewness{0.0};
  std::size_t iterations{0};
};

// The test's own Cartesian reference: the exact fully developed solution of the same
// discretization on a uniform Cartesian grid, sampled at the cell-centre rows.
Real cartesianVelocityL2(Index ny) {
  const Real n2 = static_cast<Real>(ny) * static_cast<Real>(ny);
  const Real g = 12.0 * kMeanVelocity / (kHeight * kHeight) * n2 / (n2 + 2.0);
  const Real dy = kHeight / static_cast<Real>(ny);
  Real sum = 0.0;
  for (Index j = 0; j < ny; ++j) {
    const Real y = (static_cast<Real>(j) + 0.5) * dy;
    const Real discrete = (0.5 * g * y * (kHeight - y)) + (g * dy * dy / 8.0);
    const Real exact = 6.0 * kMeanVelocity * (y / kHeight) * (1.0 - (y / kHeight));
    sum += (discrete - exact) * (discrete - exact) * dy;
  }
  return std::sqrt(sum / kHeight);
}

Metrics solve(Index nx, Index ny, bool tight, int nonOrth, const std::string& convection,
              const std::string& gradientScheme) {
  Metrics m;
  io::CaseDefinition definition = io::CaseReader{}.read(kCase);
  definition.mesh.type = "structured_quad";
  definition.mesh.nx = nx;
  definition.mesh.ny = ny;
  definition.mesh.vertices = poiseuilleVertices(nx, ny);
  // --tight: drive the outer iteration far past the case's own stopping criteria, to separate
  // discretization error from incomplete outer convergence. The discretization is untouched.
  if (nonOrth >= 0) definition.solver.nonOrthogonalCorrections = static_cast<Index>(nonOrth);
  if (!convection.empty()) definition.solver.convectionScheme = convection;
  if (!gradientScheme.empty()) definition.solver.gradientScheme = gradientScheme;
  if (tight) {
    definition.solver.maxIterations = 20000;
    definition.solver.velocityTolerance = 1e-8;
    definition.solver.pressureTolerance = 1e-8;
    definition.solver.continuityTolerance = 1e-10;
  }

  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() /
      ("inv_poiseuille_" + std::to_string(nx) + "x" + std::to_string(ny));
  std::filesystem::remove_all(tmp);
  std::filesystem::copy(kCase, tmp, std::filesystem::copy_options::recursive);
  io::CaseWriter::write(tmp, definition);

  const app::ProjectRunResult run = app::ProjectRunner::run(tmp);
  if (!run.simpleResult.has_value()) {
    std::printf("  run failed: %s\n", run.errorMessage.c_str());
    std::filesystem::remove_all(tmp);
    return m;
  }
  const auto& r = *run.simpleResult;
  const mesh::Mesh& msh = *run.mesh;
  m.ok = true;
  m.iterations = static_cast<std::size_t>(r.iterations);
  m.massImbalance = r.globalMassImbalance;

  Real errorSquared = 0.0;
  Real errorAbs = 0.0;
  Real volume = 0.0;
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  for (const auto& cell : msh.cells()) {
    const Vector2& c = cell.centroid();
    if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
    const Real exact = 6.0 * kMeanVelocity * (c.y / kHeight) * (1.0 - (c.y / kHeight));
    const Vector2 error = r.velocity[cell.id()] - Vector2{exact, 0.0};
    const Real e = magnitude(error);
    const Real v = cell.volume();
    errorSquared += e * e * v;
    errorAbs += e * v;
    volume += v;
    m.velocityLinf = std::max(m.velocityLinf, e);
    const Real p = r.pressure[cell.id()];
    sw += v;
    sx += v * c.x;
    sxx += v * c.x * c.x;
    sp += v * p;
    sxp += v * c.x * p;
  }
  m.velocityL2 = std::sqrt(errorSquared / volume);
  m.velocityL1 = errorAbs / volume;
  m.pressureGradient = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  for (Index i = 0; i <= nx; ++i) {
    m.maxColumnFlowError = std::max(m.maxColumnFlowError,
                                    std::abs(columnFlow(r.massFlux, nx, ny, i) -
                                             (kMeanVelocity * kHeight)));
  }
  // Residual history: does the OUTER iteration actually reach a fixed point?
  const auto& uh = r.uResidualHistory;
  const auto& ch = r.continuityHistory;
  std::printf("H   status %d converged %d | continuity at it", static_cast<int>(r.status),
              static_cast<int>(r.converged()));
  for (const std::size_t k : {std::size_t(100), std::size_t(500), std::size_t(1000),
                              std::size_t(2000), std::size_t(5000), std::size_t(10000),
                              std::size_t(19999)}) {
    if (k < ch.size()) std::printf(" %zu:%.2e", k, ch[k]);
  }
  std::printf(" | last continuity %.3e last U %.3e\n", ch.empty() ? 0.0 : ch.back(),
              uh.empty() ? 0.0 : uh.back());
  const auto quality = mesh::MeshQuality::evaluate(msh);
  m.maxNonOrthogonality = quality.maxNonOrthogonalityDegrees;
  m.maxSkewness = quality.maxSkewness;
  std::filesystem::remove_all(tmp);
  return m;
}

}  // namespace

int main(int argc, char** argv) {
  bool extraLevel = false;
  bool tight = false;
  bool fineOnly = false;
  int nonOrth = -1;
  std::string convection;
  std::string gradientScheme;
  for (int i = 1; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--refine") extraLevel = true;
    if (a == "--tight") tight = true;
    if (a == "--fine-only") fineOnly = true;
    if (a.rfind("--nonorth=", 0) == 0) nonOrth = std::atoi(a.c_str() + 10);
    if (a.rfind("--convection=", 0) == 0) convection = a.substr(13);
    if (a.rfind("--gradient=", 0) == 0) gradientScheme = a.substr(11);
    if (a.rfind("--distort=", 0) == 0) gDistort = std::atof(a.c_str() + 10);
  }
  std::printf("# INV-001 MESH-001 distorted Poiseuille reproduction (exact dp/dx %.6f)\n",
              kExactPressureGradient);
  struct Level {
    Index nx;
    Index ny;
  };
  std::vector<Level> levels{{64, 8}, {96, 12}, {144, 18}};
  if (fineOnly) levels = {{144, 18}};
  if (extraLevel) levels.push_back({216, 27});  // r = 1.5 continued, refinement study only

  std::vector<Metrics> all;
  for (const Level& l : levels) {
    const Metrics m = solve(l.nx, l.ny, tight, nonOrth, convection, gradientScheme);
    if (!m.ok) {
      std::printf("P   %zux%zu FAILED TO SOLVE\n", static_cast<std::size_t>(l.nx),
                  static_cast<std::size_t>(l.ny));
      return 1;
    }
    const Real dpError = std::abs(m.pressureGradient - kExactPressureGradient) /
                         std::abs(kExactPressureGradient);
    std::printf("P   %3zux%-3zu iter %5zu | vel L1 %.4e L2 %.4e Linf %.4e | L2/Cartesian %.4f | "
                "dp/dx %.6f err %.4f%% | column flow %.2e | mass %.2e | nonorth %.2f skew %.4f\n",
                static_cast<std::size_t>(l.nx), static_cast<std::size_t>(l.ny), m.iterations,
                m.velocityL1, m.velocityL2, m.velocityLinf,
                m.velocityL2 / cartesianVelocityL2(l.ny), m.pressureGradient, 100.0 * dpError,
                m.maxColumnFlowError, m.massImbalance, m.maxNonOrthogonality, m.maxSkewness);
    all.push_back(m);
  }

  const Real r = 1.5;
  for (std::size_t k = 0; k + 1 < all.size(); ++k) {
    const Real velocityOrder = std::log(all[k].velocityL2 / all[k + 1].velocityL2) / std::log(r);
    const Real e0 = std::abs(all[k].pressureGradient - kExactPressureGradient);
    const Real e1 = std::abs(all[k + 1].pressureGradient - kExactPressureGradient);
    const Real gradientOrder = std::log(e0 / e1) / std::log(r);
    std::printf("O   pair %zu: observed order velocity %.3f, dp/dx %.3f (test asserts >= 1.5)\n", k,
                velocityOrder, gradientOrder);
  }
  // Richardson extrapolation and GCI (NUM-005 convention, Fs = 1.25) on the finest pair of dp/dx.
  if (all.size() >= 3) {
    const std::size_t n = all.size();
    const Real f3 = all[n - 3].pressureGradient;
    const Real f2 = all[n - 2].pressureGradient;
    const Real f1 = all[n - 1].pressureGradient;
    const Real p = std::log(std::abs((f3 - f2) / (f2 - f1))) / std::log(r);
    const Real extrapolated = f1 + ((f1 - f2) / (std::pow(r, p) - 1.0));
    const Real relError21 = std::abs((f2 - f1) / f1);
    const Real gci21 = 1.25 * relError21 / (std::pow(r, p) - 1.0);
    const Real trueError = std::abs((f1 - kExactPressureGradient) / kExactPressureGradient);
    std::printf("R   dp/dx: observed p %.3f, Richardson %.6f (exact %.6f), GCI21 %.6f, true error "
                "%.6f -> test asserts true <= GCI: %s\n",
                p, extrapolated, kExactPressureGradient, gci21, trueError,
                trueError <= gci21 ? "PASS" : "FAIL");
  }
  return 0;
}
