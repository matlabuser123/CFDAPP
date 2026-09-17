// P12-MESH-003 -- general 2D geometry (conformal multi-block structured
// meshes) end to end through the production path: case directory ->
// CaseReader -> CaseBuilder (MeshGeometry::createMultiBlock2D + the
// MeshQuality gate) -> ProjectRunner (SIMPLE, thermal) -> export.
//
// The acceptance gate (G1-G5) is exactly the one recorded in
// results/p12-mesh-003/acceptance_gate.md, fixed before these tests were
// run:
//   cases/curved_channel_multiblock -- 270-degree curved channel (r = 1..2),
//     three 90-degree blocks; reference: the exact fully developed
//     Navier-Stokes solution u_theta = A (r ln r + a r - a / r), u_r = 0,
//     dp/dtheta = G = 2 mu A, dp/dr = rho u_theta^2 / r (Q = 1 fixes A);
//     the case selects the Rhie-Chow face flux (P12-GRAD-002-DRIFT-001: the
//     2D linear flux leaves an undamped odd-even pressure mode on this open
//     domain, and it contaminates the per-ring G slope);
//   cases/annular_sector_conduction_multiblock -- the same sector, pure
//     conduction; exact T = 1 - theta / (3 pi / 2), heat flow k ln 2 / (3 pi / 2).
// Regression cases (not gated by acceptance_gate.md, checked here):
//   cases/step_channel_multiblock -- L-shaped channel with a backward-facing step;
//   cases/obstacle_channel_multiblock -- channel with a square internal solid.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::io::MeshBlockConfig;
using cfd::mesh::Mesh;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCurvedCase = "cases/curved_channel_multiblock";
constexpr const char* kSectorCase = "cases/annular_sector_conduction_multiblock";
constexpr const char* kStepCase = "cases/step_channel_multiblock";
constexpr const char* kObstacleCase = "cases/obstacle_channel_multiblock";
const Real kPi = std::acos(-1.0);
const Real kSweep = 1.5 * kPi;  // 270 degrees
constexpr Real kR1 = 1.0;
constexpr Real kR2 = 2.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kFlowRate = 1.0;  // U (r2 - r1)

// Exact fully developed curved-channel flow (acceptance_gate.md).
struct CurvedExact {
  Real ln2 = std::log(2.0);
  Real a = -(4.0 / 3.0) * ln2;  // f(r) = r ln r + a r - a / r, f(1) = f(2) = 0
  Real amplitude = kFlowRate / ((2.0 * ln2) - 0.75 + (a * 1.5) - (a * ln2));
  [[nodiscard]] Real u(Real r) const { return amplitude * ((r * std::log(r)) + (a * r) - (a / r)); }
  [[nodiscard]] Real pressureGradient() const { return 2.0 * kViscosity * amplitude; }
  // rho * integral of u^2 / r from r0 to r1 (composite Simpson, 4000 panels).
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

// The generator's (results/p12-mesh-003/generate_multiblock_cases.py)
// vertex arithmetic: r = 1 + (2 - 1) i / nr, theta = 0 + (3 pi / 2 - 0) k / N.
std::vector<MeshBlockConfig> annularBlocks(const std::array<const char*, 3>& names, Index nr,
                                           Index nt) {
  std::vector<MeshBlockConfig> blocks;
  const Index total = 3 * nt;
  for (Index b = 0; b < 3; ++b) {
    MeshBlockConfig block{names[b], nr, nt, {}};
    for (Index j = 0; j <= nt; ++j) {
      const Real t =
          0.0 + ((kSweep - 0.0) * static_cast<Real>((b * nt) + j) / static_cast<Real>(total));
      for (Index i = 0; i <= nr; ++i) {
        const Real r = kR1 + ((kR2 - kR1) * static_cast<Real>(i) / static_cast<Real>(nr));
        block.vertices.push_back(Vector2{r * std::cos(t), r * std::sin(t)});
      }
    }
    blocks.push_back(std::move(block));
  }
  return blocks;
}

CaseDefinition curvedDefinition(Index nr, Index nt) {
  CaseDefinition d = CaseReader{}.read(kCurvedCase);
  d.mesh.blocks = annularBlocks({"bend_a", "bend_b", "bend_c"}, nr, nt);
  return d;
}

CaseFixtureCopy writeVariant(const char* base, const CaseDefinition& definition) {
  CaseFixtureCopy fixture(base);
  CaseWriter::write(fixture.path(), definition);
  return fixture;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Same vertex list up to one unit in the last place per coordinate.
bool withinOneUlp(const std::vector<Vector2>& a, const std::vector<Vector2>& b) {
  if (a.size() != b.size()) return false;
  const auto close = [](Real x, Real y) {
    const Real inf = std::numeric_limits<Real>::infinity();
    const Real ulp = std::max(std::nextafter(y, inf) - y, y - std::nextafter(y, -inf));
    return std::abs(x - y) <= ulp;
  };
  for (std::size_t k = 0; k < a.size(); ++k) {
    if (!close(a[k].x, b[k].x) || !close(a[k].y, b[k].y)) return false;
  }
  return true;
}

struct CurvedMetrics {
  bool converged{false};
  bool finite{false};
  Index iterations{0};
  double seconds{0.0};
  Real massImbalance{0.0};
  Real maxLineFlowError{0.0};  // over all 3 nt + 1 radial face lines
  Real interfaceFlowError{0.0};
  Real maxBlockNetFlow{0.0};
  Index interfaceFaces{0};
  bool interfaceOwnership{true};  // owner in the `first` (lower-angle) block
  Real velocityL2{0.0};
  Real maxRadialVelocity{0.0};
  Real pressureGradient{0.0};
  Real radialRise{0.0};
  Real radialRiseExact{0.0};
  std::vector<Vector2> velocity;
  std::vector<Real> pressure;
};

// Everything the gate needs from one curved-channel run. Cells are block by
// block, local j * nr + i, so for the 3-block and the 1-block mesh alike
// local i = id % nr and the global angular row = id / nr; the developed
// region is rows nt .. 2 nt - 1 (block bend_b, 90..180 degrees).
CurvedMetrics runCurved(const std::filesystem::path& directory, Index nr, Index nt) {
  const auto start = std::chrono::steady_clock::now();
  const ProjectRunResult run = ProjectRunner::run(directory);
  CurvedMetrics m;
  m.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (!run.simpleResult.has_value() || !run.mesh.has_value()) {
    ADD_FAILURE() << "no solver result: " << run.errorMessage;
    return m;
  }
  const auto& r = *run.simpleResult;
  const Mesh& mesh = *run.mesh;
  const CurvedExact exact;
  m.converged = run.status == ProjectRunStatus::Converged;
  m.iterations = r.iterations;
  m.massImbalance = r.globalMassImbalance;
  m.finite = true;
  for (Index c = 0; c < r.velocity.size(); ++c) {
    m.finite = m.finite && std::isfinite(r.velocity[c].x) && std::isfinite(r.velocity[c].y) &&
               std::isfinite(r.pressure[c]);
    m.velocity.push_back(r.velocity[c]);
    m.pressure.push_back(r.pressure[c]);
  }
  for (Index f = 0; f < r.massFlux.size(); ++f) m.finite = m.finite && std::isfinite(r.massFlux[f]);

  // Radial face lines theta_j = j * sweep / (3 nt): flow along +e_theta.
  const Index lines = (3 * nt) + 1;
  const Real dTheta = kSweep / static_cast<Real>(3 * nt);
  std::vector<Real> lineFlow(lines, 0.0);
  std::vector<Index> lineFaces(lines, 0);
  for (const auto& face : mesh.faces()) {
    const Real t = angleOf(face.centroid());
    const Real s = dot(face.areaVector(), eTheta(t)) / face.area();
    if (std::abs(s) < 0.99) continue;  // an arc face
    const Real jReal = t / dTheta;
    const auto j = static_cast<Index>(std::llround(jReal));
    EXPECT_NEAR(jReal, static_cast<Real>(j), 1e-6);
    lineFlow[j] += (s > 0.0 ? 1.0 : -1.0) * r.massFlux[face.id()];
    ++lineFaces[j];
  }
  for (Index j = 0; j < lines; ++j) {
    EXPECT_EQ(lineFaces[j], nr) << "line " << j;
    m.maxLineFlowError = std::max(m.maxLineFlowError, std::abs(lineFlow[j] - kFlowRate));
  }
  m.interfaceFlowError =
      std::max(std::abs(lineFlow[nt] - kFlowRate), std::abs(lineFlow[2 * nt] - kFlowRate));

  // Blocks: net outflow of each (cells' own face lists), interface faces.
  const Index perBlock = nr * nt;
  std::vector<Real> blockNet(3, 0.0);
  for (const auto& cell : mesh.cells()) {
    for (const Index f : cell.faceIds()) {
      blockNet[cell.id() / perBlock] +=
          (mesh.face(f).owner() == cell.id() ? 1.0 : -1.0) * r.massFlux[f];
    }
  }
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Index a = face.owner() / perBlock;
    const Index b = *face.neighbor() / perBlock;
    if (a == b) continue;
    ++m.interfaceFaces;
    m.interfaceOwnership = m.interfaceOwnership && b == a + 1;
  }
  // (Block 0 takes the inflow Q and block 2 gives it out; net ~ 0 for each
  // means everything entering a block through an interface leaves it.)
  for (const Real net : blockNet) m.maxBlockNetFlow = std::max(m.maxBlockNetFlow, std::abs(net));

  // Developed region: velocity error, radial velocity, G, radial rise.
  Real e2 = 0.0, volume = 0.0;
  std::vector<std::array<Real, 5>> ring(nr, {0.0, 0.0, 0.0, 0.0, 0.0});  // n, St, Stt, Sp, Stp
  Real rise = 0.0;
  for (Index row = nt; row < 2 * nt; ++row) {
    for (Index i = 0; i < nr; ++i) {
      const auto& cell = mesh.cell((row * nr) + i);
      const Vector2& x = cell.centroid();
      const Real rad = magnitude(x);
      const Real t = angleOf(x);
      const Vector2 uExact = exact.u(rad) * eTheta(t);
      const Vector2 e = r.velocity[cell.id()] - uExact;
      e2 += dot(e, e) * cell.volume();
      volume += cell.volume();
      const Vector2 eR{std::cos(t), std::sin(t)};
      m.maxRadialVelocity = std::max(m.maxRadialVelocity, std::abs(dot(r.velocity[cell.id()], eR)));
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
  m.radialRiseExact = exact.radialRise(magnitude(mesh.cell(nt * nr).centroid()),
                                       magnitude(mesh.cell((nt * nr) + nr - 1).centroid()));
  return m;
}

void printCurved(const char* label, const CurvedMetrics& m, Index cells) {
  const CurvedExact exact;
  std::printf(
      "%-28s cells %5zu | converged %d, %zu it, %.2f s | mass imbalance %.2e, max radial-line "
      "flow error %.2e (interfaces %.2e), max block net flow %.2e | velocity L2 %.4e, max |u_r| "
      "%.3e | G %.6f (exact %.6f, rel err %.4e) | radial rise %.6f (exact %.6f, rel err %.4e)\n",
      label, static_cast<std::size_t>(cells), int(m.converged),
      static_cast<std::size_t>(m.iterations), m.seconds, m.massImbalance, m.maxLineFlowError,
      m.interfaceFlowError, m.maxBlockNetFlow, m.velocityL2, m.maxRadialVelocity,
      m.pressureGradient, exact.pressureGradient(),
      std::abs(m.pressureGradient - exact.pressureGradient()) / std::abs(exact.pressureGradient()),
      m.radialRise, m.radialRiseExact,
      std::abs(m.radialRise - m.radialRiseExact) / std::abs(m.radialRiseExact));
}

// G2 (every grid).
void expectConserved(const CurvedMetrics& m, const std::string& label, bool threeBlocks = true) {
  EXPECT_TRUE(m.converged) << label;
  EXPECT_TRUE(m.finite) << label;
  EXPECT_LE(m.massImbalance, 1e-6) << label;
  EXPECT_LE(m.maxLineFlowError, 1e-6) << label;
  if (threeBlocks) {
    EXPECT_LE(m.maxBlockNetFlow, 1e-6) << label;
    EXPECT_TRUE(m.interfaceOwnership) << label;
  }
}

}  // namespace

// G1 + G2 + G4(d) on the committed case, and the export of a multi-block
// solution (VTK points = the block vertices, metadata lists the blocks).
TEST(MultiBlockProductionCase, CurvedChannelCommittedCaseGate) {
  const CaseFixtureCopy fixture(kCurvedCase);
  const CaseDefinition definition = CaseReader{}.read(fixture.path());
  ASSERT_EQ(definition.mesh.type, "multiblock");
  ASSERT_EQ(definition.geometry.type, "mesh_defined");
  ASSERT_EQ(definition.mesh.blocks.size(), 3u);
  // The committed mesh is the study's medium grid: every vertex within one
  // ulp of this test's regeneration (the generator's Python math.sin /
  // math.cos and an optimised build's fused sincos() differ by one ulp at
  // some angles; the committed interface vertices themselves are
  // bitwise-shared by construction and checked by the builder).
  const auto generated = annularBlocks({"bend_a", "bend_b", "bend_c"}, 12, 30);
  for (std::size_t b = 0; b < 3; ++b) {
    EXPECT_TRUE(withinOneUlp(definition.mesh.blocks[b].vertices, generated[b].vertices)) << b;
  }

  const auto setup = cfd::io::CaseBuilder{}.build(definition);
  const Mesh& mesh = setup.mesh;
  EXPECT_EQ(mesh.numberOfCells(), 1080u);
  const auto quality = cfd::mesh::MeshQuality::evaluate(mesh);
  EXPECT_TRUE(quality.valid);
  EXPECT_EQ(quality.connectedComponents, 1u);
  std::map<std::string, std::size_t> patchFaces;
  for (const auto& patch : mesh.boundaryPatches())
    patchFaces[patch.name()] = patch.faceIds().size();
  EXPECT_EQ(patchFaces,
            (std::map<std::string, std::size_t>{
                {"inlet", 12}, {"outlet", 12}, {"inner_wall", 90}, {"outer_wall", 90}}));
  std::printf(
      "curved channel mesh: %zu cells, %zu faces, max non-orthogonality %.3e deg, max "
      "skewness %.3e, min/max volume %.4e/%.4e\n",
      mesh.numberOfCells(), mesh.numberOfFaces(), quality.maxNonOrthogonalityDegrees,
      quality.maxSkewness, quality.minimumVolume, quality.maximumVolume);

  const CurvedMetrics m = runCurved(fixture.path(), 12, 30);
  printCurved("committed 12x30 x3", m, 1080);
  expectConserved(m, "committed");
  EXPECT_EQ(m.interfaceFaces, 24u);
  EXPECT_LE(m.interfaceFlowError, 1e-6);
  EXPECT_LE(m.maxRadialVelocity, m.velocityL2);  // G4(d)

  // Export.
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.exportSummary.has_value() && run.exportSummary->vtkPath.has_value());
  std::istringstream vtk(readFile(*run.exportSummary->vtkPath));
  std::string line;
  while (std::getline(vtk, line) && line.rfind("POINTS", 0) != 0) {
  }
  ASSERT_EQ(line, "POINTS 1209 double");  // 3 x 13 x 31
  for (const auto& block : definition.mesh.blocks) {
    for (const auto& v : block.vertices) {
      Real x = 0.0, y = 0.0, z = 1.0;
      vtk >> x >> y >> z;
      ASSERT_EQ(x, v.x);
      ASSERT_EQ(y, v.y);
    }
  }
  std::string keyword;
  Index cells = 0;
  vtk >> keyword >> cells;
  EXPECT_EQ(keyword, "CELLS");
  EXPECT_EQ(cells, 1080u);
  const auto metadata = nlohmann::json::parse(readFile(run.exportSummary->metadataPath));
  EXPECT_EQ(metadata.at("mesh").at("nx"), 0);
  EXPECT_EQ(metadata.at("mesh").at("blocks").size(), 3u);
  EXPECT_EQ(metadata.at("mesh").at("blocks")[1].at("name"), "bend_b");
}

// G3: the same geometry as ONE block (identical vertices, no interfaces)
// gives the same solution.
TEST(MultiBlockProductionCase, CurvedChannelInterfacesAreTransparent) {
  const Index nr = 12, nt = 30;
  const CaseDefinition three = CaseReader{}.read(kCurvedCase);
  CaseDefinition one = three;
  MeshBlockConfig bend{"bend", nr, 3 * nt, {}};
  for (std::size_t b = 0; b < 3; ++b) {
    const auto& v = three.mesh.blocks[b].vertices;
    // Rows 1.. of blocks b > 0 (row 0 is the previous block's last row).
    bend.vertices.insert(bend.vertices.end(), v.begin() + (b == 0 ? 0 : (nr + 1)), v.end());
  }
  ASSERT_EQ(bend.vertices.size(), (nr + 1) * ((3 * nt) + 1));
  one.mesh.blocks = {bend};
  one.mesh.interfaces.clear();
  one.mesh.patches = {{"inlet", {{"bend", "bottom"}}},
                      {"outlet", {{"bend", "top"}}},
                      {"inner_wall", {{"bend", "left"}}},
                      {"outer_wall", {{"bend", "right"}}}};
  const CaseFixtureCopy oneCase = writeVariant(kCurvedCase, one);
  const CaseFixtureCopy threeCase(kCurvedCase);
  const CurvedMetrics a = runCurved(threeCase.path(), nr, nt);
  const CurvedMetrics b = runCurved(oneCase.path(), nr, nt);
  printCurved("3 blocks", a, 1080);
  printCurved("1 block (same vertices)", b, 1080);
  expectConserved(a, "3 blocks");
  expectConserved(b, "1 block", false);
  ASSERT_EQ(a.velocity.size(), b.velocity.size());
  Real du = 0.0, dp = 0.0;
  for (std::size_t c = 0; c < a.velocity.size(); ++c) {
    du = std::max(du, magnitude(a.velocity[c] - b.velocity[c]));
    dp = std::max(dp, std::abs(a.pressure[c] - b.pressure[c]));
  }
  const Real pressureScale = std::abs(CurvedExact{}.pressureGradient()) * kSweep;
  std::printf(
      "3-block vs 1-block: max |du| %.3e, max |dp| %.3e (%.3e of the total pressure drop), "
      "iterations %zu vs %zu\n",
      du, dp, dp / pressureScale, static_cast<std::size_t>(a.iterations),
      static_cast<std::size_t>(b.iterations));
  EXPECT_LE(du, 1e-6);
  EXPECT_LE(dp, 1e-6 * pressureScale);
}

// G4(a)-(c) and G2 on every grid: 8x20, 12x30, 18x45 per block (r = 1.5),
// analysed with the P12-NUM-005 grid-convergence machinery.
TEST(MultiBlockProductionCase, CurvedChannelGridConvergence) {
  using cfd::validation::GridSpec;
  const CurvedExact exact;
  std::vector<CurvedMetrics> metrics;
  const auto solve = [&](const GridSpec& grid) {
    const Index nr = grid.nx;
    const Index nt = grid.ny / 3;
    const CaseFixtureCopy fixture = writeVariant(kCurvedCase, curvedDefinition(nr, nt));
    const CurvedMetrics m = runCurved(fixture.path(), nr, nt);
    const std::string label = std::to_string(nr) + "x" + std::to_string(nt) + " x3";
    printCurved(label.c_str(), m, 3 * nr * nt);
    expectConserved(m, label);
    metrics.push_back(m);
    cfd::validation::GridSolveOutput output;
    output.acceptance = {m.converged && m.finite && m.massImbalance <= 1e-6,
                         m.converged ? "Converged" : "not converged", ""};
    output.solverIterations = m.iterations;
    output.quantities = {{"velocity_l2_error", m.velocityL2},
                         {"pressure_gradient", m.pressureGradient},
                         {"radial_pressure_rise_ratio", m.radialRise / m.radialRiseExact}};
    return output;
  };
  cfd::validation::QuantitySpec velocity;
  velocity.name = "velocity_l2_error";
  velocity.description = "RMS |u - u_exact| over bend_b (90-180 deg); exact 0";
  velocity.reference = 0.0;
  velocity.referenceKind = "analytical";
  velocity.options.formalOrder = 2.0;
  cfd::validation::QuantitySpec gradient;
  gradient.name = "pressure_gradient";
  gradient.description = "dp/dtheta over bend_b (mean per-ring LS slope); exact 2 mu A";
  gradient.reference = exact.pressureGradient();
  gradient.referenceKind = "analytical";
  gradient.options.formalOrder = 2.0;
  cfd::validation::QuantitySpec rise;
  rise.name = "radial_pressure_rise_ratio";
  rise.description =
      "[p(outer cell) - p(inner cell)] / (rho int u^2/r dr between the same centroid radii) "
      "over bend_b (the radii move with the grid, so the ratio is analysed); exact 1";
  rise.reference = 1.0;
  rise.referenceKind = "analytical";
  rise.options.formalOrder = 2.0;
  const auto study = cfd::validation::runGridConvergenceStudy(
      "curved_channel_multiblock",
      "270-degree curved channel, 3 conformal blocks, Re 10: 8x20 / 12x30 / 18x45 per block",
      {GridSpec{"coarse", 8, 60, 1.0, 1.5 * kSweep}, GridSpec{"medium", 12, 90, 1.0, 1.5 * kSweep},
       GridSpec{"fine", 18, 135, 1.0, 1.5 * kSweep}},
      solve, {velocity, gradient, rise});
  ASSERT_TRUE(study.allSolvesAccepted) << study.rejectionReason;
  ASSERT_EQ(metrics.size(), 3u);
  cfd::validation::writeGridConvergenceReport(
      "results/validation/production/curved_channel_multiblock_grid_convergence.json", study);
  std::printf("\n%s", cfd::validation::gridConvergenceReportMarkdown(study).c_str());

  // G4(a): monotone decrease of every error.
  const auto gError = [&](const CurvedMetrics& m) {
    return std::abs(m.pressureGradient - exact.pressureGradient());
  };
  const auto riseError = [](const CurvedMetrics& m) {
    return std::abs(m.radialRise - m.radialRiseExact);
  };
  for (std::size_t k = 0; k + 1 < 3; ++k) {
    EXPECT_LT(metrics[k + 1].velocityL2, metrics[k].velocityL2) << k;
    EXPECT_LT(gError(metrics[k + 1]), gError(metrics[k])) << k;
    EXPECT_LT(riseError(metrics[k + 1]), riseError(metrics[k])) << k;
    // G4(b): observed order >= 1.5 on both pairs.
    const Real velocityOrder =
        std::log(metrics[k].velocityL2 / metrics[k + 1].velocityL2) / std::log(1.5);
    const Real gradientOrder =
        std::log(gError(metrics[k]) / gError(metrics[k + 1])) / std::log(1.5);
    const Real riseOrder =
        std::log(riseError(metrics[k]) / riseError(metrics[k + 1])) / std::log(1.5);
    std::printf("pair %zu observed order: velocity L2 %.3f, G error %.3f, radial rise error %.3f\n",
                k, velocityOrder, gradientOrder, riseOrder);
    EXPECT_GE(velocityOrder, 1.5) << k;
    EXPECT_GE(gradientOrder, 1.5) << k;
  }
  // G4(c): the fine-grid NUM-005 uncertainty brackets the exact value.
  for (const auto& q : study.quantities) {
    if (q.spec.name == "velocity_l2_error") continue;
    ASSERT_TRUE(q.analysis.uncertainty21.has_value())
        << q.spec.name << ": " << q.analysis.diagnostic;
    const Real fine = *q.values[2];
    const Real exactValue = *q.spec.reference;
    std::printf(
        "%s: fine %.8f, exact %.8f, |error| %.3e, U21 %.3e (GCI21 %.3e), extrapolated "
        "%.8f\n",
        q.spec.name.c_str(), fine, exactValue, std::abs(fine - exactValue),
        *q.analysis.uncertainty21, q.analysis.gci21.value_or(-1.0),
        q.analysis.extrapolated21.value_or(0.0));
    EXPECT_LE(std::abs(fine - exactValue), *q.analysis.uncertainty21) << q.spec.name;
  }

  // W8B-4 (results/p12-diff-002/w8b/acceptance_gate.md): an observed order is
  // evidence only for an iteratively converged solution. Re-solve the finest
  // grid with every outer tolerance 100x tighter: each quantity whose order is
  // asserted above (velocity L2, G) must move by at most 10 % of its own error
  // (W8-INV-001 section 13 R4). With the case's former linear flux the G error
  // moved by 140 % (results/p12-grad-002/drift-001/summary.md).
  CaseDefinition tight = curvedDefinition(18, 45);
  tight.solver.velocityTolerance *= 1e-2;
  tight.solver.pressureTolerance *= 1e-2;
  tight.solver.continuityTolerance *= 1e-2;
  const CaseFixtureCopy tightCase = writeVariant(kCurvedCase, tight);
  const CurvedMetrics t = runCurved(tightCase.path(), 18, 45);
  ASSERT_TRUE(t.converged);
  const CurvedMetrics& fine = metrics.back();
  const Real velocityChange = std::abs(t.velocityL2 - fine.velocityL2);
  const Real gChange = std::abs(t.pressureGradient - fine.pressureGradient);
  std::printf("18x45 iterative check (tolerances / 100, %zu iterations): velocity L2 %.6e -> %.6e "
              "(%.3f %% of the error), G %.8f -> %.8f (%.3f %% of the error)\n",
              static_cast<std::size_t>(t.iterations), fine.velocityL2, t.velocityL2,
              100.0 * velocityChange / fine.velocityL2, fine.pressureGradient, t.pressureGradient,
              100.0 * gChange / gError(fine));
  EXPECT_LE(velocityChange, 0.1 * fine.velocityL2);
  EXPECT_LE(gChange, 0.1 * gError(fine));
}

namespace {

struct SectorMetrics {
  bool converged{false};
  bool finite{false};
  Index iterations{0};
  Real temperatureL2{0.0};
  std::vector<Real> lineHeatFlow;  // along +e_theta, lines 0 .. 3 nt
  // P12-DIFF-002 A3-2: the largest per-cell net flux, from the same operator. The assembled steady
  // system makes this zero for every cell (there is no volumetric source), so it is the discrete
  // conservation residual and the quantity the cross-section agreement is derived from.
  Real maxCellImbalance{0.0};
};

// Conduction in the 270-degree sector: discrete heat flow through every radial face line, evaluated
// with the SAME face operator production assembles.
//
// P12-DIFF-002 A3-2: this used to read "from the solver's own two-point face coefficients
// (non_orthogonal_corrections 0: the flux is exactly coefficient * dT)" and evaluated boundary
// faces as boundaryFaceDiffusionTerms(..., nullptr, false).coefficient * dT. P12-DIFF-002 A2 made
// the Dirichlet wall flux the DIFF-002 three-point form regardless of non_orthogonal_corrections,
// so on a BOUNDARY face the flux is no longer `coefficient * dT` and that estimator no longer
// measured the shipped discretization -- it reported a spurious hot/cold-end vs interface spread
// of 8.6e-05 while the solver itself conserved to 1e-12. Boundary faces now use the documented
// assembled-row convention (results/p12-diff-002/architecture.md section 4)
//     flux_into_owner = -(coefficient phi_P - farCellCoefficient phi_F)
//                       + boundaryValueCoefficient phi_b + explicitFlux
// while interior faces keep the two-point evaluation, which still matches production exactly (the
// interior correction is still gated and this case runs with 0 passes). The conservation statement
// itself is still formed independently here -- production computes neither a cross-section heat
// flow nor a per-cell imbalance -- and is still compared against the analytical log(2)/sweep.
SectorMetrics runSector(const std::filesystem::path& directory, Index nr, Index nt) {
  const ProjectRunResult run = ProjectRunner::run(directory);
  SectorMetrics m;
  if (!run.thermalResult.has_value() || !run.mesh.has_value()) {
    ADD_FAILURE() << "no thermal result: " << run.errorMessage;
    return m;
  }
  const Mesh& mesh = *run.mesh;
  const auto& temperature = run.thermalResult->temperature;
  const Real k = run.caseDefinition->physics.thermal->conductivity;
  m.converged = run.thermalResult->converged();
  m.iterations = run.thermalResult->iterations;
  m.finite = true;
  for (Index c = 0; c < temperature.size(); ++c)
    m.finite = m.finite && std::isfinite(temperature[c]);
  Real e2 = 0.0, volume = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact = 1.0 - (angleOf(cell.centroid()) / kSweep);
    const Real e = temperature[cell.id()] - exact;
    e2 += e * e * cell.volume();
    volume += cell.volume();
  }
  m.temperatureL2 = std::sqrt(e2 / volume);
  const Real dTheta = kSweep / static_cast<Real>(3 * nt);
  m.lineHeatFlow.assign((3 * nt) + 1, 0.0);
  std::vector<Index> count((3 * nt) + 1, 0);
  // The gradient the production thermal assembly uses for the boundary reconstruction's tangential
  // transfer term, rebuilt from the converged temperature and the case's own boundary conditions.
  const cfd::io::SimulationSetup setup = cfd::io::CaseBuilder{}.build(*run.caseDefinition);
  const cfd::fields::VectorField gradT =
      cfd::discretization::gradient(mesh, temperature, *setup.temperatureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);
  std::vector<Real> cellNet(mesh.numberOfCells(), 0.0);

  for (const auto& face : mesh.faces()) {
    const Index owner = face.owner();
    Real flux = 0.0;  // out of the owner, along the stored face normal
    if (face.isBoundary()) {
      const auto& bc =
          cfd::boundary::boundaryConditionForFace(mesh, face.id(), *setup.temperatureBoundaries);
      const Real distance =
          cfd::mesh::MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
      const Real tb = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition&>(bc).boundaryValue(
          temperature[owner], distance);
      const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
          mesh, face, k, distance, &gradT, cfd::discretization::prescribesBoundaryValue(bc.type()));
      const Real intoOwner = -((terms.coefficient * temperature[owner]) -
                               (terms.farCellCoefficient * temperature[terms.farCell])) +
                             (terms.boundaryValueCoefficient * tb) + terms.explicitFlux;
      cellNet[owner] += intoOwner;
      flux = -intoOwner;
    } else {
      const Real dPN = cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face);
      flux =
          cfd::discretization::internalFaceDiffusionTerms(mesh, face, k, dPN, nullptr).coefficient *
          (temperature[owner] - temperature[*face.neighbor()]);
      cellNet[owner] -= flux;
      cellNet[*face.neighbor()] += flux;
    }
    const Real t = angleOf(face.centroid());
    const Real s = dot(face.areaVector(), eTheta(t)) / face.area();
    if (std::abs(s) < 0.99) continue;
    const auto j = static_cast<Index>(std::llround(t / dTheta));
    m.lineHeatFlow[j] += (s > 0.0 ? 1.0 : -1.0) * flux;
    ++count[j];
  }
  for (const Index c : count) EXPECT_EQ(c, nr);
  for (const Real net : cellNet) m.maxCellImbalance = std::max(m.maxCellImbalance, std::abs(net));
  return m;
}

}  // namespace

// G5: heat crosses both interfaces exactly as it enters and leaves, and the
// temperature converges at second order.
TEST(MultiBlockProductionCase, SectorConductionInterfaceConservation) {
  const Real exactFlow = std::log(2.0) / kSweep;  // k = 1
  std::vector<SectorMetrics> metrics;
  std::vector<Real> fluxError;
  for (const auto& [nr, nt] : {std::pair<Index, Index>{8, 20}, {12, 30}, {18, 45}}) {
    CaseDefinition d = CaseReader{}.read(kSectorCase);
    d.mesh.blocks = annularBlocks({"sector_a", "sector_b", "sector_c"}, nr, nt);
    const CaseFixtureCopy fixture = writeVariant(kSectorCase, d);
    const SectorMetrics m = runSector(fixture.path(), nr, nt);
    const auto& q = m.lineHeatFlow;
    const std::array<Real, 4> key = {q[0], q[nt], q[2 * nt], q[3 * nt]};
    const Real mean = (key[0] + key[1] + key[2] + key[3]) / 4.0;
    Real spread = 0.0, allLines = 0.0;
    for (const Real v : key) spread = std::max(spread, std::abs(v - mean));
    for (const Real v : q) allLines = std::max(allLines, std::abs(v - mean));
    std::printf(
        "sector %zux%zu x3: thermal converged %d (%zu it) | T L2 %.4e | heat flow hot end "
        "%.10f, interface 1 %.10f, interface 2 %.10f, cold end %.10f (exact %.10f, rel "
        "err %.3e) | max spread %.3e (all lines %.3e) = %.3e Q\n",
        static_cast<std::size_t>(nr), static_cast<std::size_t>(nt), int(m.converged),
        static_cast<std::size_t>(m.iterations), m.temperatureL2, key[0], key[1], key[2], key[3],
        exactFlow, std::abs(mean - exactFlow) / exactFlow, spread, allLines, spread / exactFlow);
    EXPECT_TRUE(m.converged);
    EXPECT_TRUE(m.finite);
    // P12-DIFF-002 A3-2: threshold derived from the quantity under test rather than chosen. The
    // assembled steady system makes every cell's net flux zero, satisfied to the linear solver's
    // own tolerance; the case configures absolute 1e-10 / relative 1e-8, so the per-cell residual
    // target is max(1e-10, 1e-8 |b|) and with the flux scale Q = log(2)/sweep the bound is
    // 1e-8 * Q. Cross-section agreement then follows by telescoping the cell balances. This is 100x
    // TIGHTER than the 1e-6 * Q it replaces, and the pre-A2 estimator misses it by 3-4 orders
    // (results/p12-diff-002/acceptance_gate_A3.md section 2).
    const Real conservationBound = 1e-8 * exactFlow;
    std::printf("  max per-cell imbalance %.3e vs derived bound %.3e\n", m.maxCellImbalance,
                conservationBound);
    EXPECT_LE(m.maxCellImbalance, conservationBound);
    EXPECT_LE(spread, conservationBound);
    // Energy conservation: heat in through the hot end = heat out through
    // the cold end (the only non-insulated boundaries).
    std::printf("  energy balance: in %.12f, out %.12f, |in - out| = %.3e Q\n", key[0], key[3],
                std::abs(key[0] - key[3]) / exactFlow);
    EXPECT_LE(std::abs(key[0] - key[3]), conservationBound);
    metrics.push_back(m);
    fluxError.push_back(std::abs(mean - exactFlow) / exactFlow);
  }
  // Added coverage: the cross-section flux itself converges to the analytical value (formal order
  // 2; the same >= 1.5 bound the temperature uses).
  for (std::size_t j = 0; j + 1 < fluxError.size(); ++j) {
    const Real order = std::log(fluxError[j] / fluxError[j + 1]) / std::log(1.5);
    std::printf("sector pair %zu: heat-flow error observed order %.3f\n", j, order);
    EXPECT_LT(fluxError[j + 1], fluxError[j]) << j;
    EXPECT_GE(order, 1.5) << j;
  }
  for (std::size_t k = 0; k + 1 < 3; ++k) {
    EXPECT_LT(metrics[k + 1].temperatureL2, metrics[k].temperatureL2) << k;
    const Real order =
        std::log(metrics[k].temperatureL2 / metrics[k + 1].temperatureL2) / std::log(1.5);
    std::printf("sector pair %zu: temperature L2 observed order %.3f\n", k, order);
    EXPECT_GE(order, 1.5) << k;
  }
  // The committed case is the medium grid (see withinOneUlp).
  const auto committed = CaseReader{}.read(kSectorCase).mesh.blocks;
  const auto medium = annularBlocks({"sector_a", "sector_b", "sector_c"}, 12, 30);
  for (std::size_t b = 0; b < 3; ++b) {
    EXPECT_TRUE(withinOneUlp(committed[b].vertices, medium[b].vertices)) << b;
  }
}

namespace {

// Mass flow through the vertical face line x = x0 (faces whose normal is
// +-x), along +x.
Real flowThroughX(const Mesh& mesh, const cfd::fields::SurfaceField& massFlux, Real x0,
                  Index& faces) {
  Real flow = 0.0;
  faces = 0;
  for (const auto& face : mesh.faces()) {
    if (std::abs(face.centroid().x - x0) > 1e-12 || std::abs(face.areaVector().y) > 1e-12) continue;
    flow += (face.areaVector().x > 0.0 ? 1.0 : -1.0) * massFlux[face.id()];
    ++faces;
  }
  return flow;
}

}  // namespace

// Case A: the L-shaped step channel -- a non-rectangular domain whose
// boundary patches span several blocks. Conservation through every vertical
// face line (including both interfaces), a recirculation zone behind the
// step, and the exact Poiseuille profile far downstream.
TEST(MultiBlockProductionCase, StepChannelRegression) {
  const CaseFixtureCopy fixture(kStepCase);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.simpleResult.has_value() && run.mesh.has_value()) << run.errorMessage;
  const auto& r = *run.simpleResult;
  const Mesh& mesh = *run.mesh;
  EXPECT_EQ(run.status, ProjectRunStatus::Converged);
  EXPECT_EQ(mesh.numberOfCells(), 1664u);
  EXPECT_LE(r.globalMassImbalance, 1e-6);
  std::map<std::string, std::size_t> patchFaces;
  for (const auto& patch : mesh.boundaryPatches())
    patchFaces[patch.name()] = patch.faceIds().size();
  EXPECT_EQ(
      patchFaces,
      (std::map<std::string, std::size_t>{
          {"inlet", 8}, {"outlet", 16}, {"top_wall", 112}, {"bottom_wall", 112}, {"step", 8}}));
  // No cell in the removed corner; every vertical face line carries U h = 0.5.
  for (const auto& cell : mesh.cells()) {
    EXPECT_FALSE(cell.centroid().x < 2.0 && cell.centroid().y < 0.5) << cell.id();
  }
  Real maxFlowError = 0.0;
  for (Index i = 0; i <= 112; ++i) {
    const Real x0 = i <= 16 ? 2.0 * static_cast<Real>(i) / 16.0
                            : 2.0 + (12.0 * static_cast<Real>(i - 16) / 96.0);
    Index faces = 0;
    const Real flow = flowThroughX(mesh, r.massFlux, x0, faces);
    EXPECT_EQ(faces, i < 16 ? 8u : 16u) << "x = " << x0;  // x = 2: interface + step
    maxFlowError = std::max(maxFlowError, std::abs(flow - 0.5));
  }
  EXPECT_LE(maxFlowError, 1e-6);
  // Recirculation behind the step: reversed flow in the bottom cell row.
  Index reversed = 0;
  Real reattachment = 2.0;
  for (const auto& cell : mesh.cells()) {
    if (cell.centroid().y < 1.0 / 16.0 && r.velocity[cell.id()].x < 0.0) {
      ++reversed;
      reattachment = std::max(reattachment, cell.centroid().x);
    }
  }
  // Far field: u = 6 U_mean y (1 - y) with U_mean = 0.5.
  Real farError = 0.0, farV = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& x = cell.centroid();
    if (x.x < 11.0 || x.x > 13.0) continue;
    farError = std::max(farError, std::abs(r.velocity[cell.id()].x - (3.0 * x.y * (1.0 - x.y))));
    farV = std::max(farV, std::abs(r.velocity[cell.id()].y));
  }
  std::printf(
      "step channel: %zu it, mass imbalance %.2e, max face-line flow error %.2e, reversed "
      "bottom-row cells %zu (last at x = %.4f), far-field max |u - 3y(1-y)| %.3e, max |v| "
      "%.3e\n",
      static_cast<std::size_t>(r.iterations), r.globalMassImbalance, maxFlowError,
      static_cast<std::size_t>(reversed), reattachment, farError, farV);
  EXPECT_GT(reversed, 0u);
  EXPECT_GT(reattachment, 2.5);
  EXPECT_LT(reattachment, 6.0);
  EXPECT_LE(farError, 0.02);  // 2.7% of the centreline velocity 0.75
  EXPECT_LE(farV, 1e-3);
}

// Case C: an internal solid region -- the obstacle is a hole in the mesh,
// not masked cells. No cell inside it, conservation through every vertical
// face line (above and below the obstacle where it cuts the line), and the
// mirror symmetry of the geometry reproduced by the solution.
TEST(MultiBlockProductionCase, ObstacleChannelInternalSolid) {
  const CaseFixtureCopy fixture(kObstacleCase);
  const CaseDefinition definition = CaseReader{}.read(fixture.path());
  EXPECT_EQ(definition.mesh.blocks.size(), 8u);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.simpleResult.has_value() && run.mesh.has_value()) << run.errorMessage;
  const auto& r = *run.simpleResult;
  const Mesh& mesh = *run.mesh;
  EXPECT_EQ(run.status, ProjectRunStatus::Converged);
  EXPECT_EQ(mesh.numberOfCells(), 1504u);
  EXPECT_LE(r.globalMassImbalance, 1e-6);
  const auto quality = cfd::mesh::MeshQuality::evaluate(mesh);
  EXPECT_EQ(quality.connectedComponents, 1u);
  Real solidArea = 6.0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& x = cell.centroid();
    EXPECT_FALSE(x.x > 2.0 && x.x < 2.5 && x.y > 0.375 && x.y < 0.625) << cell.id();
    solidArea -= cell.volume();
  }
  EXPECT_NEAR(solidArea, 0.5 * 0.25, 1e-12);  // the domain area excludes exactly the obstacle
  Index obstacleFaces = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() != "obstacle") continue;
    for (const Index f : patch.faceIds()) {
      ++obstacleFaces;
      // Outward from the fluid = into the solid.
      EXPECT_GT(dot(Vector2{2.25, 0.5} - mesh.face(f).centroid(), mesh.face(f).areaVector()), 0.0);
    }
  }
  EXPECT_EQ(obstacleFaces, 24u);  // 8 + 4 + 8 + 4
  Real maxFlowError = 0.0;
  for (Index i = 0; i <= 96; ++i) {
    const Real x0 = i <= 32   ? 2.0 * static_cast<Real>(i) / 32.0
                    : i <= 40 ? 2.0 + (0.5 * static_cast<Real>(i - 32) / 8.0)
                              : 2.5 + (3.5 * static_cast<Real>(i - 40) / 56.0);
    Index faces = 0;
    const Real flow = flowThroughX(mesh, r.massFlux, x0, faces);
    EXPECT_EQ(faces, (i > 32 && i < 40) ? 12u : 16u) << "x = " << x0;
    maxFlowError = std::max(maxFlowError, std::abs(flow - 1.0));
  }
  EXPECT_LE(maxFlowError, 1e-6);
  // Mirror symmetry about y = 0.5 (mesh and boundary conditions symmetric).
  std::map<std::pair<long long, long long>, Index> byPosition;
  const auto key = [](Real x, Real y) {
    return std::pair<long long, long long>{std::llround(x * 1e9), std::llround(y * 1e9)};
  };
  for (const auto& cell : mesh.cells())
    byPosition[key(cell.centroid().x, cell.centroid().y)] = cell.id();
  Real du = 0.0, dv = 0.0, dp = 0.0;
  for (const auto& cell : mesh.cells()) {
    const auto it = byPosition.find(key(cell.centroid().x, 1.0 - cell.centroid().y));
    ASSERT_NE(it, byPosition.end()) << cell.id();
    du = std::max(du, std::abs(r.velocity[cell.id()].x - r.velocity[it->second].x));
    dv = std::max(dv, std::abs(r.velocity[cell.id()].y + r.velocity[it->second].y));
    dp = std::max(dp, std::abs(r.pressure[cell.id()] - r.pressure[it->second]));
  }
  std::printf(
      "obstacle channel: %zu it, mass imbalance %.2e, max face-line flow error %.2e, "
      "symmetry max |du| %.2e |dv| %.2e |dp| %.2e\n",
      static_cast<std::size_t>(r.iterations), r.globalMassImbalance, maxFlowError, du, dv, dp);
  EXPECT_LE(du, 1e-6);
  EXPECT_LE(dv, 1e-6);
  EXPECT_LE(dp, 1e-6 * 10.0);  // pressure level ~10
}
