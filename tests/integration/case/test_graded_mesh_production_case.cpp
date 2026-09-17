// P12-MESH-002 -- graded (stretched) structured meshes, end to end through
// the production path (case directory -> CaseReader -> CaseBuilder ->
// ProjectRunner -> export), on cases/channel_transpiration_graded.
//
// Validation reference (independent, analytical): the exact fully developed
// Navier-Stokes solution of a channel with uniform wall transpiration --
// injection V through the bottom wall, suction V through the top wall
// (v = V, p = p(x)):
//   u(y) = C [H (e^{ly} - 1)/(e^{lH} - 1) - y],  l = V / nu,
//   C = U / (1/l - H/(e^{lH} - 1) - H/2),  dp/dx = rho C V,
//   tau_top = mu C (lH e^{lH}/(e^{lH} - 1) - 1).
// At V H / nu = 20 the suction wall carries a boundary layer of thickness
// 0.05 H -- a genuine wall-gradient problem (plain Poiseuille is not: its
// constant curvature makes the uniform grid optimal -- see
// results/p12-mesh-002/acceptance_gate.md, fixed before these tests ran).
//
// The acceptance gate (GradedMeshBeatsUniformAtEqualCellCount) is exactly
// the one recorded in results/p12-mesh-002/acceptance_gate.md.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGrading.hpp"
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
using cfd::mesh::AxisGrading;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCase = "cases/channel_transpiration_graded";
constexpr Real kLength = 4.0;
constexpr Real kHeight = 1.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kInletVelocity = 1.0;
constexpr Real kTranspiration = 2.0;  // V; wall Reynolds number V H / nu = 20

struct Exact {
  Real lambda = kTranspiration * kDensity / kViscosity;
  Real c = kInletVelocity /
           ((1.0 / lambda) - (kHeight / std::expm1(lambda * kHeight)) - (0.5 * kHeight));
  [[nodiscard]] Real u(Real y) const {
    return c * ((kHeight * std::expm1(lambda * y) / std::expm1(lambda * kHeight)) - y);
  }
  [[nodiscard]] Real pressureGradient() const { return kDensity * c * kTranspiration; }
  [[nodiscard]] Real topWallShear() const {
    return kViscosity * c *
           ((lambda * kHeight * std::exp(lambda * kHeight) / std::expm1(lambda * kHeight)) - 1.0);
  }
};

struct Metrics {
  bool converged{false};
  bool finite{false};
  Index iterations{0};
  double seconds{0.0};
  Real massImbalance{0.0};
  Real maxColumnFlowError{0.0};
  Real inletFlow{0.0};
  Real outletFlow{0.0};
  Real velocityL1{0.0};
  Real velocityL2{0.0};
  Real velocityLinf{0.0};
  Real pressureGradientError{0.0};  // relative; = integral wall-shear error (force balance)
  Real topWallShearError{0.0};      // mean relative, near-wall 2nd-order reconstruction
  Real firstCellHeight{0.0};
};

// Runs a case directory and measures everything the gate and the evidence
// need. Errors over the developed region 0.50 L <= x_c <= 0.85 L.
Metrics runAndMeasure(const std::filesystem::path& directory) {
  const auto start = std::chrono::steady_clock::now();
  const ProjectRunResult run = ProjectRunner::run(directory);
  Metrics m;
  m.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (!run.simpleResult.has_value() || !run.mesh.has_value()) {
    ADD_FAILURE() << "no solver result: " << run.errorMessage;
    return m;
  }
  const auto& r = *run.simpleResult;
  const auto& mesh = *run.mesh;
  const Index nx = run.caseDefinition->mesh.nx;
  const Index ny = run.caseDefinition->mesh.ny;
  const Exact exact;
  m.converged = run.status == ProjectRunStatus::Converged;
  m.iterations = r.iterations;
  m.massImbalance = r.globalMassImbalance;
  m.finite = true;
  for (Index i = 0; i < r.velocity.size(); ++i) {
    m.finite = m.finite && std::isfinite(r.velocity[i].x) && std::isfinite(r.velocity[i].y) &&
               std::isfinite(r.pressure[i]);
  }
  for (Index f = 0; f < r.massFlux.size(); ++f) m.finite = m.finite && std::isfinite(r.massFlux[f]);

  Real errorL1 = 0.0, errorL2 = 0.0, volume = 0.0;
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    if (c.x < 0.50 * kLength || c.x > 0.85 * kLength) continue;
    const Real e = magnitude(r.velocity[cell.id()] - Vector2{exact.u(c.y), kTranspiration});
    const Real v = cell.volume();
    errorL1 += e * v;
    errorL2 += e * e * v;
    volume += v;
    m.velocityLinf = std::max(m.velocityLinf, e);
    const Real p = r.pressure[cell.id()];
    sw += v;
    sx += v * c.x;
    sxx += v * c.x * c.x;
    sp += v * p;
    sxp += v * c.x * p;
  }
  m.velocityL1 = errorL1 / volume;
  m.velocityL2 = std::sqrt(errorL2 / volume);
  const Real gradient = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  m.pressureGradientError =
      std::abs(gradient - exact.pressureGradient()) / std::abs(exact.pressureGradient());

  // Suction-wall shear from the two top cells of each developed column:
  // du/dy at the wall of the quadratic through (wall, 0), (y_a, u_a),
  // (y_b, u_b), distances measured from the top wall.
  Real shearError = 0.0;
  Index samples = 0;
  for (Index i = 0; i < nx; ++i) {
    const auto& a = mesh.cell(((ny - 1) * nx) + i);
    if (a.centroid().x < 0.50 * kLength || a.centroid().x > 0.85 * kLength) continue;
    const auto& b = mesh.cell(((ny - 2) * nx) + i);
    const Real ya = kHeight - a.centroid().y;
    const Real yb = kHeight - b.centroid().y;
    const Real ua = r.velocity[a.id()].x;
    const Real ub = r.velocity[b.id()].x;
    const Real gradientAtWall = -((ua * yb * yb) - (ub * ya * ya)) / (ya * yb * (yb - ya));
    shearError += std::abs((kViscosity * gradientAtWall) - exact.topWallShear()) /
                  std::abs(exact.topWallShear());
    ++samples;
    m.firstCellHeight = ya;
  }
  m.topWallShearError = shearError / static_cast<Real>(samples);

  // Mass flow through every column of vertical faces (inlet i = 0 ..
  // outlet i = nx): injection and suction through the walls cancel per
  // column, so each carries U H.
  for (Index i = 0; i <= nx; ++i) {
    Real flow = 0.0;
    for (Index j = 0; j < ny; ++j) flow += r.massFlux[(j * (nx + 1)) + i];
    if (i == 0) flow = -flow;
    if (i == 0) m.inletFlow = flow;
    if (i == nx) m.outletFlow = flow;
    m.maxColumnFlowError =
        std::max(m.maxColumnFlowError, std::abs(flow - (kDensity * kInletVelocity * kHeight)));
  }
  return m;
}

void print(const char* label, const Metrics& m, Index cells) {
  std::printf(
      "%-26s cells %5zu | converged %d, %zu it, %.2f s | mass imbalance %.2e, inlet %.9f, "
      "outlet %.9f, max column error %.2e | y1 %.5f | velocity L1 %.4e L2 %.4e Linf %.4e | "
      "dp/dx (integral wall shear) error %.4e | suction-wall shear error %.4e\n",
      label, static_cast<std::size_t>(cells), int(m.converged),
      static_cast<std::size_t>(m.iterations), m.seconds, m.massImbalance, m.inletFlow, m.outletFlow,
      m.maxColumnFlowError, m.firstCellHeight, m.velocityL1, m.velocityL2, m.velocityLinf,
      m.pressureGradientError, m.topWallShearError);
}

void expectConservedAndConverged(const Metrics& m, const char* label) {
  EXPECT_TRUE(m.converged) << label;
  EXPECT_TRUE(m.finite) << label;
  EXPECT_LE(m.massImbalance, 1e-6) << label;
  EXPECT_LE(m.maxColumnFlowError, 1e-6) << label;
}

CaseFixtureCopy writeVariant(const CaseDefinition& definition) {
  CaseFixtureCopy fixture(kCase);
  CaseWriter::write(fixture.path(), definition);
  return fixture;
}

AxisGrading geometric(Real ratio, GradingCluster cluster) {
  return AxisGrading{GradingType::Geometric, ratio, cluster};
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// The committed graded case through ProjectRunner: converged, finite,
// conservative, and exported with the graded vertex coordinates.
TEST(GradedMeshProductionCase, CommittedCaseRunsConservesAndExportsGradedGeometry) {
  const CaseFixtureCopy fixture(kCase);
  const CaseDefinition definition = CaseReader{}.read(fixture.path());
  ASSERT_TRUE(definition.mesh.grading.has_value());
  EXPECT_EQ(definition.mesh.grading->y, geometric(1.2, GradingCluster::Both));
  EXPECT_EQ(definition.mesh.grading->x, AxisGrading{});

  const Metrics m = runAndMeasure(fixture.path());
  expectConservedAndConverged(m, "graded 48x24");
  print("committed graded 48x24", m, 48 * 24);

  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.exportSummary.has_value() && run.exportSummary->vtkPath.has_value());
  const auto yNodes = cfd::mesh::gradedNodeCoordinates(24, kHeight, definition.mesh.grading->y);
  std::istringstream vtk(readFile(*run.exportSummary->vtkPath));
  std::string line;
  while (std::getline(vtk, line) && line.rfind("POINTS", 0) != 0) {
  }
  ASSERT_EQ(line, "POINTS 1225 double");
  for (Index j = 0; j <= 24; ++j) {
    for (Index i = 0; i <= 48; ++i) {
      Real x = 0.0, y = 0.0, z = 1.0;
      vtk >> x >> y >> z;
      ASSERT_EQ(y, yNodes[j]) << i << "," << j;
      ASSERT_EQ(x, static_cast<Real>(i) * (kLength / 48.0)) << i << "," << j;
    }
  }
  const auto metadata = nlohmann::json::parse(readFile(run.exportSummary->metadataPath));
  EXPECT_EQ(metadata.at("mesh").at("nx").get<Index>(), 48u);
  EXPECT_EQ(metadata.at("mesh").at("ny").get<Index>(), 24u);
}

// THE P12-MESH-002 ACCEPTANCE GATE (results/p12-mesh-002/acceptance_gate.md,
// fixed before this test was run): at equal cell count 48 x 24, identical
// physics/solver/tolerances/scheme, the y-graded mesh (both walls, ratio
// 1.2) must cut the integral wall-shear (dp/dx) error and the suction-wall
// shear error to <= 0.75 x the uniform mesh's, without increasing the
// velocity L2 error, with both runs converged, finite and conservative.
TEST(GradedMeshProductionCase, GradedMeshBeatsUniformAtEqualCellCount) {
  const CaseFixtureCopy gradedCase(kCase);
  CaseDefinition uniformDefinition = CaseReader{}.read(kCase);
  uniformDefinition.mesh.grading.reset();
  const CaseFixtureCopy uniformCase = writeVariant(uniformDefinition);
  ASSERT_EQ(nlohmann::json::parse(readFile(uniformCase.path() / "mesh.json")),
            (nlohmann::json{{"type", "structured_cartesian"}, {"nx", 48}, {"ny", 24}}));

  const Metrics graded = runAndMeasure(gradedCase.path());
  const Metrics uniform = runAndMeasure(uniformCase.path());
  print("uniform 48x24", uniform, 48 * 24);
  print("graded 48x24 (both, 1.2)", graded, 48 * 24);
  std::printf(
      "graded / uniform: dp/dx error %.3f, suction-wall shear error %.3f, velocity L2 %.3f, "
      "L1 %.3f, Linf %.3f, iterations %.3f, time %.3f\n",
      graded.pressureGradientError / uniform.pressureGradientError,
      graded.topWallShearError / uniform.topWallShearError, graded.velocityL2 / uniform.velocityL2,
      graded.velocityL1 / uniform.velocityL1, graded.velocityLinf / uniform.velocityLinf,
      static_cast<double>(graded.iterations) / static_cast<double>(uniform.iterations),
      graded.seconds / uniform.seconds);

  expectConservedAndConverged(uniform, "uniform");
  expectConservedAndConverged(graded, "graded");
  EXPECT_LE(graded.pressureGradientError, 0.75 * uniform.pressureGradientError);
  EXPECT_LE(graded.topWallShearError, 0.75 * uniform.topWallShearError);
  EXPECT_LE(graded.velocityL2, 1.0 * uniform.velocityL2);
}

// Three grids with one grading law (r^m fixed, m = cells per half, so the
// node distribution samples the same mapping x(xi)): 32x16 / 48x24 / 72x36
// (r = 1.5), analysed with the P12-NUM-005 grid-convergence machinery.
// Asserted: every solve accepted and every error decreasing monotonically;
// the observed orders and the NUM-005 classification are reported as
// measured (not forced).
TEST(GradedMeshProductionCase, GradedGridConvergence) {
  using cfd::validation::GridSpec;
  const CaseDefinition base = CaseReader{}.read(kCase);
  const Real lawConstant = std::pow(1.2, 12);  // r^m of the 48x24 case
  std::vector<Metrics> metrics;
  const auto solve = [&](const GridSpec& grid) {
    CaseDefinition definition = base;
    definition.mesh.nx = grid.nx;
    definition.mesh.ny = grid.ny;
    const Real ratio = std::pow(lawConstant, 1.0 / static_cast<Real>(grid.ny / 2));
    definition.mesh.grading->y = geometric(ratio, GradingCluster::Both);
    const CaseFixtureCopy fixture = writeVariant(definition);
    const Metrics m = runAndMeasure(fixture.path());
    const std::string label = std::to_string(grid.nx) + "x" + std::to_string(grid.ny) + " (ratio " +
                              std::to_string(ratio) + ")";
    print(label.c_str(), m, grid.nx * grid.ny);
    expectConservedAndConverged(m, label.c_str());
    metrics.push_back(m);
    cfd::validation::GridSolveOutput output;
    output.acceptance = {m.converged && m.finite && m.massImbalance <= 1e-6,
                         m.converged ? "Converged" : "not converged", ""};
    output.solverIterations = m.iterations;
    output.quantities = {{"pressure_gradient_relative_error", m.pressureGradientError},
                         {"suction_wall_shear_relative_error", m.topWallShearError},
                         {"velocity_l2_error", m.velocityL2}};
    return output;
  };
  std::vector<cfd::validation::QuantitySpec> quantities;
  for (const char* name : {"pressure_gradient_relative_error", "suction_wall_shear_relative_error",
                           "velocity_l2_error"}) {
    cfd::validation::QuantitySpec q;
    q.name = name;
    q.description = std::string(name) + " vs the exact transpiration-channel solution; exact 0";
    q.reference = 0.0;
    q.referenceKind = "analytical";
    q.options.formalOrder = 2.0;
    quantities.push_back(q);
  }
  const auto study = cfd::validation::runGridConvergenceStudy(
      "channel_transpiration_graded",
      "Transpiration channel Re_w = 20, y-graded (both, r^m = 1.2^12): 32x16 / 48x24 / 72x36",
      {GridSpec{"coarse", 32, 16, kLength, kHeight}, GridSpec{"medium", 48, 24, kLength, kHeight},
       GridSpec{"fine", 72, 36, kLength, kHeight}},
      solve, quantities);
  ASSERT_TRUE(study.allSolvesAccepted) << study.rejectionReason;
  ASSERT_EQ(metrics.size(), 3u);
  cfd::validation::writeGridConvergenceReport(
      "results/validation/production/channel_transpiration_graded_grid_convergence.json", study);
  std::printf("\n%s", cfd::validation::gridConvergenceReportMarkdown(study).c_str());
  for (std::size_t k = 0; k + 1 < metrics.size(); ++k) {
    EXPECT_LT(metrics[k + 1].pressureGradientError, metrics[k].pressureGradientError) << k;
    EXPECT_LT(metrics[k + 1].topWallShearError, metrics[k].topWallShearError) << k;
    EXPECT_LT(metrics[k + 1].velocityL2, metrics[k].velocityL2) << k;
    std::printf(
        "pair %zu observed order: dp/dx %.3f, suction-wall shear %.3f, velocity L2 %.3f\n", k,
        std::log(metrics[k].pressureGradientError / metrics[k + 1].pressureGradientError) /
            std::log(1.5),
        std::log(metrics[k].topWallShearError / metrics[k + 1].topWallShearError) / std::log(1.5),
        std::log(metrics[k].velocityL2 / metrics[k + 1].velocityL2) / std::log(1.5));
  }
}

// x and y grading together (x clustered at the inlet, where the flow
// develops): the production path runs, converges and conserves, and the
// developed solution stays accurate (measured dp/dx error 0.24%, velocity
// L2 6.8e-3; bounds 5% and 1.2e-2 -- the uniform 48x24 values are larger).
TEST(GradedMeshProductionCase, XAndYGradingTogether) {
  CaseDefinition definition = CaseReader{}.read(kCase);
  definition.mesh.grading->x = geometric(1.05, GradingCluster::Start);
  const CaseFixtureCopy fixture = writeVariant(definition);
  const CaseDefinition reread = CaseReader{}.read(fixture.path());
  ASSERT_EQ(reread.mesh.grading, definition.mesh.grading);
  const Metrics m = runAndMeasure(fixture.path());
  print("x(left 1.05) + y(both 1.2)", m, 48 * 24);
  expectConservedAndConverged(m, "x+y graded");
  EXPECT_LE(m.pressureGradientError, 0.05);
  EXPECT_LE(m.velocityL2, 1.2e-2);
}

// Grading is compatible with the P12-MESH-001 non-orthogonal path: a
// structured_quad whose vertex grid is the graded one plus a smooth interior
// distortion (max non-orthogonality ~15 degrees) runs with
// non_orthogonal_corrections 1 and keeps the graded accuracy (measured
// dp/dx 1.8%, suction-wall shear 6.7%, velocity L2 7.8e-3 -- vs 3.7%,
// 17.4%, 3.1e-2 for the same distortion of the uniform grid).
TEST(GradedMeshProductionCase, GradedVertexGridOnNonOrthogonalStructuredQuad) {
  const auto quadCase = [](const AxisGrading& yGrading) {
    CaseDefinition definition = CaseReader{}.read(kCase);
    const auto xs = cfd::mesh::gradedNodeCoordinates(48, kLength, AxisGrading{});
    const auto ys = cfd::mesh::gradedNodeCoordinates(24, kHeight, yGrading);
    const Real pi = std::acos(-1.0);
    definition.mesh.type = "structured_quad";
    definition.mesh.grading.reset();
    definition.mesh.vertices.clear();
    for (Index j = 0; j <= 24; ++j) {
      for (Index i = 0; i <= 48; ++i) {
        const Real hx = i < 48 ? xs[i + 1] - xs[i] : xs[i] - xs[i - 1];
        const Real hy = j < 24 ? ys[j + 1] - ys[j] : ys[j] - ys[j - 1];
        Real x = xs[i] + (0.25 * hx * std::sin(pi * xs[i] / kLength) *
                          std::sin(2.0 * pi * ys[j] / kHeight));
        Real y = ys[j] + (0.25 * hy * std::sin(2.0 * pi * xs[i] / kHeight) *
                          std::sin(pi * ys[j] / kHeight));
        if (i == 0) x = 0.0;
        if (i == 48) x = kLength;
        if (j == 0) y = 0.0;
        if (j == 24) y = kHeight;
        definition.mesh.vertices.push_back(Vector2{x, y});
      }
    }
    definition.solver.nonOrthogonalCorrections = 1;
    return definition;
  };
  const CaseFixtureCopy gradedCase = writeVariant(quadCase(geometric(1.2, GradingCluster::Both)));
  const CaseFixtureCopy uniformCase = writeVariant(quadCase(AxisGrading{}));
  const Metrics graded = runAndMeasure(gradedCase.path());
  const Metrics uniform = runAndMeasure(uniformCase.path());
  print("structured_quad graded", graded, 48 * 24);
  print("structured_quad uniform", uniform, 48 * 24);
  expectConservedAndConverged(graded, "structured_quad graded");
  expectConservedAndConverged(uniform, "structured_quad uniform");
  EXPECT_GE(cfd::mesh::MeshQuality::evaluate(
                cfd::io::CaseBuilder{}.build(CaseReader{}.read(gradedCase.path())).mesh)
                .maxNonOrthogonalityDegrees,
            10.0);
  EXPECT_LE(graded.pressureGradientError, 0.75 * uniform.pressureGradientError);
  EXPECT_LE(graded.topWallShearError, 0.75 * uniform.topWallShearError);
}
