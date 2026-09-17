// P12-MESH-001 -- production non-orthogonal structured meshes, end to end.
//
// Every run here goes through the normal production path -- a case
// directory on disk read by CaseReader, built by CaseBuilder (mesh-validity
// gate included), solved by ProjectRunner::run() (the function the CLI and
// GUI call) and exported by ResultExporter -- on "structured_quad" meshes
// (mesh.json with explicit vertices). Variant cases (refined grids, other
// physics) are written with CaseWriter from a committed case.
//
// Validation (independent references, not self-consistency):
//   * cases/poiseuille_distorted -- planar Poiseuille flow (analytical
//     u = 6U(y/H)(1-y/H), dp/dx = -12 mu U/H^2) on a smoothly distorted mesh
//     (max face non-orthogonality ~45 degrees, grid lines tilted at both
//     walls and wavy across the flow), incl. a three-grid refinement study
//     (P12-NUM-005 machinery) and the least-squares-gradient variant;
//   * thermal + species diffusion on a distorted slab (analytical linear
//     T(x), Y(x));
//   * CompressibleSIMPLE on a distorted isothermal compressible channel
//     (Arkilic et al. 1997 lubrication solution, p(x)^2 linear).
// Consistency (no analytical reference at these grids):
//   * k-epsilon / SST turbulent channel on a mesh tilted at the walls
//     against the same case on the Cartesian mesh.
//
// Every numerical bound below was fixed from the Release measurements
// recorded in results/p12-mesh-001/summary.md (with the margin stated at
// each bound) or from the Cartesian scheme's own discretization error.
// cases/poiseuille_distorted selects the Rhie-Chow face flux (P12-GRAD-002-
// DRIFT-001: the 2D linear flux leaves an undamped odd-even pressure mode on
// this open domain, so its solutions keep moving long after the outer
// tolerance is met); every variant written from it inherits that choice.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "CaseFixtureCopy.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
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
using cfd::mesh::Mesh;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kPoiseuilleCase = "cases/poiseuille_distorted";
constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kViscosity = 0.1;  // rho = 1, Re = 10
// Developed region used for every Poiseuille error (entrance length at
// Re = 10 is ~1.1 H; the outlet influence reaches < 0.5 H upstream).
constexpr Real kDevelopedStart = 0.50 * kLength;
constexpr Real kDevelopedEnd = 0.85 * kLength;
const Real kPi = std::acos(-1.0);

// The smooth mapping every distorted mesh here is generated with (and
// cases/poiseuille_distorted was generated with -- see its case.json):
//   x = xi  + ax sin(pi xi / L) sin(2 pi eta / H)
//   y = eta + ay sin(2 pi xi / lambda) sin(pi eta / H)
// on xi = L i / nx, eta = H j / ny, boundary vertices snapped exactly onto
// the rectangle. ax tilts the grid lines at the walls; ay makes the
// horizontal lines wavy (and the wall-normal spacing vary along the wall).
std::vector<Vector2> mappedVertices(Index nx, Index ny, Real length, Real height, Real ax, Real ay,
                                    Real lambda) {
  std::vector<Vector2> vertices;
  vertices.reserve((nx + 1) * (ny + 1));
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

std::vector<Vector2> poiseuilleVertices(Index nx, Index ny) {
  return mappedVertices(nx, ny, kLength, kHeight, 0.1, 0.05, 1.0);
}

void setQuadMesh(CaseDefinition& definition, Index nx, Index ny, std::vector<Vector2> vertices) {
  definition.mesh.type = "structured_quad";
  definition.mesh.nx = nx;
  definition.mesh.ny = ny;
  definition.mesh.vertices = std::move(vertices);
}

// A committed case copied to a temporary directory, then overwritten with
// `definition` by CaseWriter -- the same files a user or the GUI would save.
CaseFixtureCopy writeVariant(const char* baseCase, const CaseDefinition& definition) {
  CaseFixtureCopy fixture(baseCase);
  CaseWriter::write(fixture.path(), definition);
  return fixture;
}

bool allFinite(const ProjectRunResult& run) {
  const auto& r = *run.simpleResult;
  for (Index i = 0; i < r.velocity.size(); ++i) {
    if (!std::isfinite(r.velocity[i].x) || !std::isfinite(r.velocity[i].y) ||
        !std::isfinite(r.pressure[i])) {
      return false;
    }
  }
  for (Index f = 0; f < r.massFlux.size(); ++f) {
    if (!std::isfinite(r.massFlux[f])) return false;
  }
  return true;
}

// Mass flow through the i-th line of "vertical" faces (the faces between
// cell columns i-1 and i, j = 0..ny-1 -- a curve from wall to wall on a
// distorted mesh), taken positive downstream. Uses the documented
// structured topology (MeshGeometry.hpp): those faces are ids
// j * (nx + 1) + i, area vector toward +i.
Real columnFlow(const Mesh& mesh, const cfd::fields::SurfaceField& massFlux, Index nx, Index ny,
                Index i) {
  Real flow = 0.0;
  for (Index j = 0; j < ny; ++j) flow += massFlux[(j * (nx + 1)) + i];
  (void)mesh;
  return i == 0 ? -flow : flow;  // the inlet face area vectors point out of the domain (-x)
}

struct PoiseuilleMetrics {
  bool finite{false};
  Real velocityL2{0.0};          // volume-weighted L2 of |u - u_exact| over the developed region
  Real pressureGradient{0.0};    // least-squares fit of dp/dx over the developed region
  Real maxColumnFlowError{0.0};  // max |Q_i - U H| over every face column incl. inlet/outlet
  Real maxWallFlux{0.0};
  Real massImbalance{0.0};
};

// Volume-weighted linear least-squares fit p = a + g x over the developed
// cells, on a mesh whose cell columns are not at constant x. (The committed
// case selects the Rhie-Chow face flux, which removes the odd-even pressure
// mode the 2D linear flux leaves on this open domain --
// results/p12-grad-002/drift-001/summary.md; the fit is exact for a linear p.)
PoiseuilleMetrics poiseuilleMetrics(const ProjectRunResult& run, Index nx, Index ny) {
  PoiseuilleMetrics m;
  const Mesh& mesh = *run.mesh;
  const auto& r = *run.simpleResult;
  m.finite = allFinite(run);
  m.massImbalance = r.globalMassImbalance;
  Real errorSquared = 0.0;
  Real volume = 0.0;
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
    const Real exact = 6.0 * kMeanVelocity * (c.y / kHeight) * (1.0 - (c.y / kHeight));
    const Vector2 error = r.velocity[cell.id()] - Vector2{exact, 0.0};
    const Real v = cell.volume();
    errorSquared += dot(error, error) * v;
    volume += v;
    const Real p = r.pressure[cell.id()];
    sw += v;
    sx += v * c.x;
    sxx += v * c.x * c.x;
    sp += v * p;
    sxp += v * c.x * p;
  }
  m.velocityL2 = std::sqrt(errorSquared / volume);
  m.pressureGradient = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  for (Index i = 0; i <= nx; ++i) {
    m.maxColumnFlowError =
        std::max(m.maxColumnFlowError,
                 std::abs(columnFlow(mesh, r.massFlux, nx, ny, i) - (kMeanVelocity * kHeight)));
  }
  for (const char* wall : {"bottom", "top"}) {
    for (const Index f : mesh.boundaryPatch(wall).faceIds()) {
      m.maxWallFlux = std::max(m.maxWallFlux, std::abs(r.massFlux[f]));
    }
  }
  return m;
}

// The Cartesian scheme's own error at the same ny, over the same developed
// region. P12-DIFF-002 (second-order Dirichlet wall flux) changed that scheme,
// so the reference is its exact fully developed discrete solution as derived
// independently in exact rational arithmetic by P12-DIFF-002-UC-001
// (results/p12-diff-002/uc-001/acceptance_gate.md section 1): the continuum
// parabola exactly at the cell centres, with only the flow-rate-consistent
// gradient scaled, u_j = (G/2) y_j (H - y_j), G = 12 mu U/H^2 *
// 2 ny^2/(2 ny^2 + 1) / mu -- independent of nx. P12-DIFF-002 W8B migrated it
// from the superseded two-point form (factor ny^2/(ny^2 + 2) plus a G dy^2/8
// offset), and P12-GRAD-002-DRIFT-001 reproduced it to six digits on an
// orthogonal structured_quad mesh (results/p12-diff-002/w8b/).
Real cartesianVelocityL2(Index ny) {
  const Real n2 = static_cast<Real>(ny * ny);
  const Real g = 12.0 * kMeanVelocity / (kHeight * kHeight) * (2.0 * n2) / ((2.0 * n2) + 1.0);
  const Real dy = kHeight / static_cast<Real>(ny);
  Real sum = 0.0;
  for (Index j = 0; j < ny; ++j) {
    const Real y = (static_cast<Real>(j) + 0.5) * dy;
    const Real discrete = 0.5 * g * y * (kHeight - y);
    const Real exact = 6.0 * kMeanVelocity * (y / kHeight) * (1.0 - (y / kHeight));
    sum += (discrete - exact) * (discrete - exact);
  }
  return std::sqrt(sum / static_cast<Real>(ny));
}

// The relative dp/dx error of that discrete solution: exactly 1/(2 ny^2 + 1)
// (UC-001; the superseded two-point value was 2/(ny^2 + 2)).
Real cartesianPressureGradientError(Index ny) {
  const Real n2 = static_cast<Real>(ny * ny);
  return 1.0 / ((2.0 * n2) + 1.0);
}

constexpr Real kExactPressureGradient = -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight);

// The per-grid acceptance gates of the distorted Poiseuille case.
// Accuracy is bounded RELATIVE to the Cartesian scheme's own error at the
// same ny: the non-orthogonal mesh may cost at most 50% more error than a
// Cartesian mesh of the same resolution (the P12-MESH-001 design factor,
// unchanged). Measured against the DIFF-002 references above, with the
// case's Rhie-Chow flux: 1.19-1.29x for the velocity, 0.30-0.70x for dp/dx,
// on 64x8..144x18 (results/p12-grad-002/drift-001/summary.md section 3).
void expectPoiseuilleGates(const PoiseuilleMetrics& m, Index ny, const std::string& label) {
  EXPECT_TRUE(m.finite) << label;
  EXPECT_LE(m.massImbalance, 1e-6) << label;
  // Conservation: every face column (a wall-to-wall curve) carries the
  // prescribed flow -- the P0 inlet/outlet gate's 1e-6 (measured <= 4e-10).
  EXPECT_LE(m.maxColumnFlowError, 1e-6) << label;
  EXPECT_LE(m.maxWallFlux, 1e-12) << label;
  EXPECT_LE(m.velocityL2, 1.5 * cartesianVelocityL2(ny))
      << label << ": velocity L2 " << m.velocityL2 << " vs Cartesian " << cartesianVelocityL2(ny);
  const Real gradientError =
      std::abs(m.pressureGradient - kExactPressureGradient) / std::abs(kExactPressureGradient);
  EXPECT_LE(gradientError, 1.5 * cartesianPressureGradientError(ny))
      << label << ": dp/dx " << m.pressureGradient;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// =============================================================================
// Distorted Poiseuille (committed case)
// =============================================================================

// The committed case is the documented mapping and a genuinely non-
// orthogonal, valid production mesh.
TEST(StructuredQuadProductionCase, CommittedCaseIsAGenuinelyNonOrthogonalMesh) {
  const CaseDefinition definition = CaseReader{}.read(kPoiseuilleCase);
  ASSERT_EQ(definition.mesh.type, "structured_quad");
  ASSERT_EQ(definition.mesh.nx, 64u);
  ASSERT_EQ(definition.mesh.ny, 8u);
  const auto expected = poiseuilleVertices(64, 8);
  ASSERT_EQ(definition.mesh.vertices.size(), expected.size());
  for (std::size_t k = 0; k < expected.size(); ++k) {
    EXPECT_NEAR(definition.mesh.vertices[k].x, expected[k].x, 1e-12) << k;
    EXPECT_NEAR(definition.mesh.vertices[k].y, expected[k].y, 1e-12) << k;
  }
  const auto setup = cfd::io::CaseBuilder{}.build(definition);
  const auto quality = cfd::mesh::MeshQuality::evaluate(setup.mesh);
  EXPECT_TRUE(quality.valid);
  // Design targets of the case (measured 44.8 / 14.6 degrees, 0.132).
  EXPECT_GE(quality.maxNonOrthogonalityDegrees, 40.0);
  EXPECT_GE(quality.meanNonOrthogonalityDegrees, 10.0);
  EXPECT_GE(quality.maxSkewness, 0.1);
  std::printf(
      "poiseuille_distorted 64x8: non-orthogonality max %.2f mean %.2f deg, skewness max %.4f mean "
      "%.4f\n",
      quality.maxNonOrthogonalityDegrees, quality.meanNonOrthogonalityDegrees, quality.maxSkewness,
      quality.meanSkewness);
}

// The committed case through ProjectRunner: converged, finite, conservative,
// and in quantitative agreement with the analytical solution; exported with
// its true vertices.
TEST(StructuredQuadProductionCase, DistortedPoiseuilleMatchesAnalyticalSolution) {
  const CaseFixtureCopy fixture(kPoiseuilleCase);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.simpleResult.has_value());
  ASSERT_TRUE(run.mesh.has_value());
  ASSERT_NE(run.mesh->structuredGrid(), nullptr);

  const PoiseuilleMetrics m = poiseuilleMetrics(run, 64, 8);
  expectPoiseuilleGates(m, 8, "64x8");
  std::printf(
      "64x8: iterations %zu, velocity L2 %.4e (Cartesian %.4e), dp/dx %.6f (exact %.6f), "
      "max column flow error %.2e, mass imbalance %.2e\n",
      static_cast<std::size_t>(run.simpleResult->iterations), m.velocityL2, cartesianVelocityL2(8),
      m.pressureGradient, kExactPressureGradient, m.maxColumnFlowError, m.massImbalance);

  // Export: VTK points are the case's own vertices; JSON/CSV describe the
  // 64x8 structured grid.
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->vtkPath.has_value());
  std::istringstream vtk(readFile(*run.exportSummary->vtkPath));
  std::string line;
  while (std::getline(vtk, line) && line.rfind("POINTS", 0) != 0) {
  }
  ASSERT_EQ(line, "POINTS 585 double");
  const auto vertices = run.caseDefinition->mesh.vertices;
  for (const auto& v : vertices) {
    Real x = 0.0, y = 0.0, z = 1.0;
    vtk >> x >> y >> z;
    ASSERT_EQ(x, v.x);
    ASSERT_EQ(y, v.y);
  }
  const auto metadata = nlohmann::json::parse(readFile(run.exportSummary->metadataPath));
  EXPECT_EQ(metadata.at("mesh").at("nx").get<Index>(), 64u);
  EXPECT_EQ(metadata.at("mesh").at("ny").get<Index>(), 8u);
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  std::istringstream csv(readFile(*run.exportSummary->fieldsCsvPath));
  Index rows = 0;
  while (std::getline(csv, line)) ++rows;
  EXPECT_EQ(rows, 512u + 1u);  // header + one row per cell
}

// Three grids (r = 1.5, the P12-NUM-005 Poiseuille grids), each written by
// CaseWriter and run through ProjectRunner. Per-grid gates as above (the
// velocity and dp/dx accuracy of EVERY grid); convergence: the velocity error
// decreases at close to the formal second order, from an iteratively
// converged solution.
//
// P12-DIFF-002 W8B (results/p12-diff-002/w8b/acceptance_gate.md) replaced
// two assertions of the original gate here, on evidence
// (results/p12-grad-002/drift-001/summary.md section 3):
//   * the dp/dx pair order: the dp/dx error is the orthogonal discrete-exact
//     term +1/(2 ny^2 + 1) plus an opposite-sign distortion term that
//     dominates on these grids, so it crosses zero between ny = 8 and 12 and
//     no order is defined across or just after the crossing (measured 2.70,
//     then -0.05); the per-grid dp/dx gate bounds its magnitude instead;
//   * the dp/dx GCI bracket: a GCI is defined only in the asymptotic range,
//     and the NUM-005 analysis itself classifies this dp/dx triplet as
//     non-asymptotic (asymptotic ratio ~79), so no GCI is claimed for it.
// The velocity order is kept: it is asymptotic on this family (2.11, 2.07,
// then 2.03 at 144x18 -> 216x27; 2.09, 2.03 at r = 2).
TEST(StructuredQuadProductionCase, DistortedPoiseuilleGridConvergence) {
  using cfd::validation::GridSpec;
  const CaseDefinition base = CaseReader{}.read(kPoiseuilleCase);
  std::vector<PoiseuilleMetrics> metrics;
  const auto solve = [&](const GridSpec& grid) {
    CaseDefinition definition = base;
    setQuadMesh(definition, grid.nx, grid.ny, poiseuilleVertices(grid.nx, grid.ny));
    const CaseFixtureCopy fixture = writeVariant(kPoiseuilleCase, definition);
    const ProjectRunResult run = ProjectRunner::run(fixture.path());
    cfd::validation::GridSolveOutput output;
    if (!run.simpleResult.has_value()) {
      output.acceptance = {false, "none", run.errorMessage};
      return output;
    }
    output.acceptance = cfd::validation::assessSimpleSolve(*run.simpleResult, 1e-6);
    output.solverIterations = run.simpleResult->iterations;
    if (!output.acceptance.accepted) return output;
    const PoiseuilleMetrics m = poiseuilleMetrics(run, grid.nx, grid.ny);
    const auto quality = cfd::mesh::MeshQuality::evaluate(*run.mesh);
    expectPoiseuilleGates(m, grid.ny, std::to_string(grid.nx) + "x" + std::to_string(grid.ny));
    std::printf(
        "%zux%zu: non-orthogonality max %.2f deg, skewness max %.4f, iterations %zu, "
        "velocity L2 %.4e (Cartesian %.4e), dp/dx %.6f, column flow error %.2e\n",
        static_cast<std::size_t>(grid.nx), static_cast<std::size_t>(grid.ny),
        quality.maxNonOrthogonalityDegrees, quality.maxSkewness,
        static_cast<std::size_t>(run.simpleResult->iterations), m.velocityL2,
        cartesianVelocityL2(grid.ny), m.pressureGradient, m.maxColumnFlowError);
    metrics.push_back(m);
    output.quantities = {{"pressure_gradient", m.pressureGradient},
                         {"velocity_l2_error", m.velocityL2}};
    return output;
  };

  cfd::validation::QuantitySpec gradient;
  gradient.name = "pressure_gradient";
  gradient.description = "dp/dx, least-squares fit over 0.50 L..0.85 L; exact -12 mu U / H^2";
  gradient.reference = kExactPressureGradient;
  gradient.referenceKind = "analytical";
  gradient.options.formalOrder = 2.0;
  gradient.options.absoluteNoise = 1e-6;
  cfd::validation::QuantitySpec velocity = gradient;
  velocity.name = "velocity_l2_error";
  velocity.description = "volume-weighted L2 of |u - u_exact| over 0.50 L..0.85 L; exact 0";
  velocity.reference = 0.0;

  const auto study = cfd::validation::runGridConvergenceStudy(
      "poiseuille_distorted",
      "Distorted Poiseuille Re = 10 (structured_quad), 64x8 / 96x12 / 144x18",
      {GridSpec{"coarse", 64, 8, kLength, kHeight}, GridSpec{"medium", 96, 12, kLength, kHeight},
       GridSpec{"fine", 144, 18, kLength, kHeight}},
      solve, {gradient, velocity});
  ASSERT_TRUE(study.allSolvesAccepted) << study.rejectionReason;
  ASSERT_EQ(metrics.size(), 3u);
  cfd::validation::writeGridConvergenceReport(
      "results/validation/production/poiseuille_distorted_grid_convergence.json", study);
  std::printf("\n%s", cfd::validation::gridConvergenceReportMarkdown(study).c_str());

  // Observed velocity order of each successive grid pair (r = 1.5): the
  // formal order is 2; bound 1.5 (the original P12-MESH-001 criterion). The
  // signed dp/dx errors are reported, not order-tested (see above).
  const Real r = 1.5;
  for (std::size_t k = 0; k + 1 < metrics.size(); ++k) {
    const Real velocityOrder =
        std::log(metrics[k].velocityL2 / metrics[k + 1].velocityL2) / std::log(r);
    std::printf("pair %zu: observed order velocity %.3f; dp/dx signed error %+.4e -> %+.4e\n", k,
                velocityOrder, metrics[k].pressureGradient - kExactPressureGradient,
                metrics[k + 1].pressureGradient - kExactPressureGradient);
    EXPECT_GE(velocityOrder, 1.5) << "pair " << k;
  }
  const auto& g = study.quantities[0];
  std::printf("dp/dx triplet: NUM-005 status %s, asymptotic ratio %.4g (no GCI claimed)\n",
              std::string(cfd::validation::gridConvergenceStatusName(g.analysis.status)).c_str(),
              g.analysis.asymptoticRatio.value_or(-1.0));

  // W8B-4: an observed order is evidence only for an iteratively converged
  // solution. Re-solve the finest grid with every outer tolerance 100x tighter:
  // the velocity L2 error the order above uses must move by at most 10 % of
  // itself (W8-INV-001 section 13 R4; its exact value is 0).
  CaseDefinition tight = base;
  setQuadMesh(tight, 144, 18, poiseuilleVertices(144, 18));
  tight.solver.velocityTolerance *= 1e-2;
  tight.solver.pressureTolerance *= 1e-2;
  tight.solver.continuityTolerance *= 1e-2;
  const CaseFixtureCopy tightCase = writeVariant(kPoiseuilleCase, tight);
  const ProjectRunResult tightRun = ProjectRunner::run(tightCase.path());
  ASSERT_EQ(tightRun.status, ProjectRunStatus::Converged) << tightRun.errorMessage;
  const Real tightVelocityL2 = poiseuilleMetrics(tightRun, 144, 18).velocityL2;
  const Real iterativeChange = std::abs(tightVelocityL2 - metrics.back().velocityL2);
  std::printf(
      "144x18 iterative check: velocity L2 %.6e (gate) vs %.6e (tolerances / 100, %zu "
      "iterations): change %.3e = %.3f %% of the error\n",
      metrics.back().velocityL2, tightVelocityL2,
      static_cast<std::size_t>(tightRun.simpleResult->iterations), iterativeChange,
      100.0 * iterativeChange / metrics.back().velocityL2);
  EXPECT_LE(iterativeChange, 0.1 * metrics.back().velocityL2);
}

// gradient_scheme "least_squares" through the production path: the same
// gates (measured velocity L2 1.78e-2 = 1.17x Cartesian, dp/dx 2.4%).
TEST(StructuredQuadProductionCase, LeastSquaresGradientMatchesAnalyticalSolution) {
  CaseDefinition definition = CaseReader{}.read(kPoiseuilleCase);
  definition.solver.gradientScheme = "least_squares";
  const CaseFixtureCopy fixture = writeVariant(kPoiseuilleCase, definition);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  expectPoiseuilleGates(poiseuilleMetrics(run, 64, 8), 8, "least_squares 64x8");
}

// non_orthogonal_corrections reaches the distorted mesh through the
// production path and is what makes it accurate.
//
// P12-DIFF-002 A3-1: this test used to infer "the correction is active" indirectly, from
// `uncorrectedError > 1.5 * cartesianVelocityL2(8)` -- i.e. by requiring the
// non_orthogonal_corrections = 0 solution to be measurably BAD (it recorded 3.31e-2 vs the
// corrected 1.71e-2). P12-DIFF-002 A2 deliberately removed that premise: the Dirichlet wall flux
// is now second order at N = 0 too, so the N = 0 error improved to ~1.65e-2 and the proxy became
// meaningless. The indirect proxy is replaced by a DIRECT operator-level activation test, which
// measures the property the test actually exists for (see
// results/p12-diff-002/acceptance_gate_A3.md section 1):
//
//   D3  solver.json's value propagates into the production settings and options;
//   D1  rows of the production momentum-diffusion assembly whose cell touches NO boundary face --
//       so the Dirichlet wall treatment contributes nothing at all to them -- are bitwise
//       identical with the correction off and DIFFER with it on. That is exactly "not applied at
//       N = 0, applied at N >= 1", with the boundary treatment excluded by construction rather
//       than by a tolerance;
//   D2  those same interior rows are bitwise identical for N = 1 and N = 2, so the pass count
//       changes the iteration, not the operator;
//   S1  the solution-level check (P12-DIFF-002 W8B, results/p12-diff-002/w8b/): the corrected
//       solution meets the P12-MESH-001 accuracy requirement (velocity L2 <= 1.5x the Cartesian
//       scheme's own error at the same ny, via expectPoiseuilleGates) and the uncorrected one
//       does NOT -- the correction is what makes the case accurate. This is the test's original
//       pre-A3 formulation; A3 had to drop it only because that reference was the superseded
//       two-point solution, which W8B migrated. It replaces A3's empirical "uncorrected >= 1.5x
//       corrected", whose margin the Rhie-Chow case removed (measured 1.495).
TEST(StructuredQuadProductionCase, NonOrthogonalCorrectionIsActiveOnTheProductionPath) {
  CaseDefinition definition = CaseReader{}.read(kPoiseuilleCase);
  // D3: the setting reaches the solver settings and the shared correction options.
  ASSERT_EQ(definition.solver.nonOrthogonalCorrections, 1u) << "committed case expectation";
  const cfd::io::SimulationSetup correctedSetup = cfd::io::CaseBuilder{}.build(definition);
  EXPECT_EQ(correctedSetup.solverSettings.nonOrthogonalCorrections, 1u);
  EXPECT_TRUE(cfd::pressure_velocity::nonOrthogonalOptions(correctedSetup.solverSettings).enabled);

  definition.solver.nonOrthogonalCorrections = 0;
  const cfd::io::SimulationSetup uncorrectedSetup = cfd::io::CaseBuilder{}.build(definition);
  EXPECT_EQ(uncorrectedSetup.solverSettings.nonOrthogonalCorrections, 0u);
  EXPECT_FALSE(
      cfd::pressure_velocity::nonOrthogonalOptions(uncorrectedSetup.solverSettings).enabled);

  // D1/D2: assemble the production momentum diffusion term on the committed distorted mesh and
  // compare interior-only rows. |A| row sums and the RHS are compared bit for bit.
  const cfd::mesh::Mesh& mesh = correctedSetup.mesh;
  std::vector<bool> touchesBoundary(mesh.numberOfCells(), false);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) touchesBoundary[face.owner()] = true;
  }
  const auto assemble = [&](Index passes) {
    cfd::pressure_velocity::SIMPLESettings settings = correctedSetup.solverSettings;
    settings.nonOrthogonalCorrections = passes;
    const auto options = cfd::pressure_velocity::nonOrthogonalOptions(settings);
    cfd::algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
    cfd::algebra::Vector rhs(mesh.numberOfCells());
    cfd::physics::assembleDiffusionContribution(
        mesh, correctedSetup.fluid.dynamicViscosity(), correctedSetup.initialVelocity,
        correctedSetup.velocityBoundaries, cfd::physics::VelocityComponent::U, builder, rhs,
        passes > 0u && options.enabled, options.gradientScheme);
    return std::pair{builder.build(), rhs};
  };
  const auto rowSum = [](const cfd::algebra::SparseMatrix& m, Index row) {
    Real s = 0.0;
    for (Index k = m.rowOffsetsData()[row]; k < m.rowOffsetsData()[row + 1]; ++k) {
      s += std::abs(m.valuesData()[k]);
    }
    return s;
  };
  const auto interiorDiffering = [&](const auto& a, const auto& b) {
    std::size_t differing = 0;
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      if (touchesBoundary[i]) continue;
      if (rowSum(a.first, i) != rowSum(b.first, i) || a.second[i] != b.second[i]) ++differing;
    }
    return differing;
  };
  const auto off = assemble(0u);
  const auto on1 = assemble(1u);
  const auto on2 = assemble(2u);
  std::size_t interiorRows = 0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!touchesBoundary[i]) ++interiorRows;
  }
  ASSERT_GT(interiorRows, 0u);
  const std::size_t appliedRows = interiorDiffering(off, on1);
  std::printf(
      "activation: interior rows %zu, changed by the correction %zu, changed by a second "
      "pass %zu\n",
      interiorRows, appliedRows, interiorDiffering(on1, on2));
  EXPECT_GT(appliedRows, 0u) << "the internal-face correction is not reaching the assembly";
  EXPECT_EQ(interiorDiffering(on1, on2), 0u) << "the pass count must not change the operator";

  // S1: the correction is what makes the case meet its accuracy requirement.
  const CaseFixtureCopy uncorrectedCase = writeVariant(kPoiseuilleCase, definition);
  const ProjectRunResult uncorrected = ProjectRunner::run(uncorrectedCase.path());
  ASSERT_EQ(uncorrected.status, ProjectRunStatus::Converged) << uncorrected.errorMessage;
  const CaseFixtureCopy correctedCase(kPoiseuilleCase);
  const ProjectRunResult corrected = ProjectRunner::run(correctedCase.path());
  ASSERT_EQ(corrected.status, ProjectRunStatus::Converged) << corrected.errorMessage;
  const Real uncorrectedError = poiseuilleMetrics(uncorrected, 64, 8).velocityL2;
  const Real correctedError = poiseuilleMetrics(corrected, 64, 8).velocityL2;
  const Real requirement = 1.5 * cartesianVelocityL2(8);
  std::printf(
      "activation: velocity L2 uncorrected %.4e, corrected %.4e, requirement %.4e "
      "(1.5x Cartesian)\n",
      uncorrectedError, correctedError, requirement);
  expectPoiseuilleGates(poiseuilleMetrics(corrected, 64, 8), 8, "corrected 64x8");
  EXPECT_GT(uncorrectedError, requirement);
}

// =============================================================================
// Thermal and species on a distorted slab
// =============================================================================

// cases/heated_species_diffusion (quiescent 1.0 x 0.2 slab, T 310/290 K and
// Y 1/0 on left/right, adiabatic / zero-flux top and bottom) on a distorted
// 20x4 mesh (max non-orthogonality ~33 degrees, grid lines tilted at the
// adiabatic walls). The analytical solution is linear in x and the
// corrected discretization reproduces it to the solvers' own tolerance
// (measured T <= 1.5e-6 K, Y <= 4.3e-7 for both gradients; bounds 1e-5).
// Without the correction the error is 0.31 K / 1.6e-2. (20x4 only: the
// species solver's fixed linear-solver settings fail on this case from
// 40x8 up on the Cartesian mesh too -- a pre-existing limitation, see
// results/p12-mesh-001/summary.md.)
TEST(StructuredQuadProductionCase, ThermalAndSpeciesMatchLinearProfilesOnDistortedSlab) {
  for (const char* scheme : {"green_gauss", "least_squares"}) {
    CaseDefinition definition = CaseReader{}.read("cases/heated_species_diffusion");
    setQuadMesh(definition, 20, 4, mappedVertices(20, 4, 1.0, 0.2, 0.02, 0.01, 0.2));
    definition.solver.nonOrthogonalCorrections = 1;
    definition.solver.gradientScheme = scheme;
    const CaseFixtureCopy fixture = writeVariant("cases/heated_species_diffusion", definition);
    const ProjectRunResult run = ProjectRunner::run(fixture.path());
    ASSERT_EQ(run.status, ProjectRunStatus::Converged) << scheme << ": " << run.errorMessage;
    ASSERT_TRUE(run.thermalResult.has_value());
    ASSERT_TRUE(run.thermalResult->converged()) << scheme;
    ASSERT_EQ(run.speciesResults.size(), 1u);
    ASSERT_TRUE(run.speciesResults[0].result.converged()) << scheme;
    const auto quality = cfd::mesh::MeshQuality::evaluate(*run.mesh);
    EXPECT_GE(quality.maxNonOrthogonalityDegrees, 30.0);
    Real temperatureError = 0.0;
    Real concentrationError = 0.0;
    for (const auto& cell : run.mesh->cells()) {
      const Real x = cell.centroid().x;
      const Real t = run.thermalResult->temperature[cell.id()];
      const Real y = run.speciesResults[0].result.concentration[cell.id()];
      ASSERT_TRUE(std::isfinite(t) && std::isfinite(y));
      temperatureError = std::max(temperatureError, std::abs(t - (310.0 - (20.0 * x))));
      concentrationError = std::max(concentrationError, std::abs(y - (1.0 - x)));
    }
    std::printf("%s: max |T - T_exact| %.3e K, max |Y - Y_exact| %.3e\n", scheme, temperatureError,
                concentrationError);
    EXPECT_LE(temperatureError, 1e-5) << scheme;
    EXPECT_LE(concentrationError, 1e-5) << scheme;
  }
}

// =============================================================================
// CompressibleSIMPLE on a distorted mesh
// =============================================================================

// cases/compressible_channel_coupled (the P12-COMP-002 isothermal
// compressible-lubrication case, Ma ~0.14) on a distorted 48x8 mesh (max
// non-orthogonality ~21 degrees -- the cells are 3.3:1, so the same
// relative distortion gives smaller angles). Same analytical reference and
// bounds as test_compressible_coupled_production_case.cpp (5%, set there
// from the lubrication approximation's own O(Re H/L) error); measured
// 0.84% L2 / 2.2% Linf (Cartesian 1.29% / 2.55%). Mass conservation at
// every face column, as the coupled solver enforces it (measured 1.7e-8).
// physics.json is written directly: CaseWriter does not write
// compressible.coupled (pre-existing, recorded in the P12-MESH-001
// summary).
TEST(StructuredQuadProductionCase, CompressibleSimpleMatchesLubricationSolutionOnDistortedMesh) {
  constexpr Index nx = 48;
  constexpr Index ny = 8;
  CaseDefinition definition = CaseReader{}.read("cases/compressible_channel_coupled");
  setQuadMesh(definition, nx, ny,
              mappedVertices(nx, ny, 1.0, 0.05, 0.05 * 0.05, 0.05 * 0.025, 0.05));
  definition.solver.nonOrthogonalCorrections = 1;
  const CaseFixtureCopy fixture = writeVariant("cases/compressible_channel_coupled", definition);
  {
    auto physics = nlohmann::json::parse(readFile(fixture.path() / "physics.json"));
    physics["compressible"]["coupled"] = true;
    std::ofstream(fixture.path() / "physics.json") << physics.dump(2);
  }
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.compressibleSimpleResult.has_value());
  ASSERT_TRUE(run.compressibleResult.has_value());
  EXPECT_GE(cfd::mesh::MeshQuality::evaluate(*run.mesh).maxNonOrthogonalityDegrees, 15.0);

  const auto& massFlux = run.compressibleResult->massFlux;
  std::vector<Real> flows;
  for (Index i = 1; i < nx; ++i) flows.push_back(columnFlow(*run.mesh, massFlux, nx, ny, i));
  Real mdot = 0.0;
  for (const Real q : flows) mdot += q;
  mdot /= static_cast<Real>(flows.size());
  ASSERT_GT(mdot, 0.0);
  for (const Real q : flows) EXPECT_NEAR(q, mdot, 1e-6 * mdot);

  // p(x)^2 = p_out^2 + 24 mu R T mdot (L - x) / H^3 (see the Cartesian test).
  const Real slope = 24.0 * 0.58831 * 287.05 * 300.0 * mdot / (0.05 * 0.05 * 0.05);
  Real sumSquared = 0.0;
  Real maxError = 0.0;
  for (const auto& cell : run.mesh->cells()) {
    const Real predicted = std::sqrt((101325.0 * 101325.0) + (slope * (1.0 - cell.centroid().x)));
    const Real error =
        (run.compressibleResult->pressureAbsolute[cell.id()] - predicted) / predicted;
    ASSERT_TRUE(std::isfinite(error));
    sumSquared += error * error;
    maxError = std::max(maxError, std::abs(error));
  }
  const Real l2 = std::sqrt(sumSquared / static_cast<Real>(run.mesh->numberOfCells()));
  std::printf("distorted lubrication channel: iterations %zu, p error L2 %.4e Linf %.4e\n",
              static_cast<std::size_t>(run.compressibleSimpleResult->iterations), l2, maxError);
  EXPECT_LT(l2, 0.05);
  EXPECT_LT(maxError, 0.05);
}

// =============================================================================
// Turbulence (consistency with the Cartesian solution)
// =============================================================================

namespace {

// Planar turbulent channel H = 2, L = 16, Re_bulk 5600 (the P2-TURB-007
// channel, inlet 5% intensity / 0.07 H mixing length) through the case
// path, 48x12; Re_tau from the developed-region force balance
// tau_w = -(dp/dx) H/2 (a mesh-independent estimator).
struct ChannelResult {
  Real reTau{0.0};
  Real maxColumnFlowError{0.0};
};

ChannelResult runChannel(const std::string& model, bool distorted) {
  CaseDefinition definition = CaseReader{}.read("cases/poiseuille_flow");
  constexpr Index nx = 48;
  constexpr Index ny = 12;
  definition.geometry.length = 16.0;
  definition.geometry.height = 2.0;
  definition.physics.dynamicViscosity = 2.0 / 5600.0;
  definition.physics.reynoldsNumber.reset();
  const Real k = 1.5 * 0.05 * 0.05;
  const Real epsilon = std::pow(0.09, 0.75) * std::pow(k, 1.5) / (0.07 * 2.0);
  cfd::io::TurbulencePhysicsConfig turbulence;
  turbulence.model = model;
  turbulence.initialK = k;
  if (model == "k_epsilon") {
    turbulence.initialEpsilon = epsilon;
  } else {
    turbulence.initialOmega = epsilon / (0.09 * k);
  }
  definition.physics.turbulence = turbulence;
  definition.solver.maxIterations = 8000;
  definition.solver.velocityRelaxation = 0.5;
  definition.solver.pressureRelaxation = 0.3;
  definition.solver.pressureSolver.type = "CG";
  definition.solver.pressureSolver.absoluteTolerance = 1e-9;
  definition.solver.pressureSolver.relativeTolerance = 1e-7;
  if (distorted) {
    // Vertical grid lines tilted by up to ~31 degrees at both walls, rows
    // straight (uniform wall-normal spacing).
    setQuadMesh(definition, nx, ny, mappedVertices(nx, ny, 16.0, 2.0, 0.2, 0.0, 2.0));
    definition.solver.nonOrthogonalCorrections = 1;
  } else {
    definition.mesh.nx = nx;
    definition.mesh.ny = ny;
  }
  const CaseFixtureCopy fixture = writeVariant("cases/poiseuille_flow", definition);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  EXPECT_EQ(run.status, ProjectRunStatus::Converged) << model << ": " << run.errorMessage;
  ChannelResult result;
  if (!run.simpleResult.has_value() || !allFinite(run)) {
    ADD_FAILURE() << model << (distorted ? " distorted" : " Cartesian") << ": no finite result";
    return result;
  }
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  for (const auto& cell : run.mesh->cells()) {
    const Real x = cell.centroid().x;
    if (x < 0.7 * 16.0 || x > 0.9 * 16.0) continue;
    const Real v = cell.volume();
    const Real p = run.simpleResult->pressure[cell.id()];
    sw += v;
    sx += v * x;
    sxx += v * x * x;
    sp += v * p;
    sxp += v * x * p;
  }
  const Real dpdx = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  result.reTau = std::sqrt(-dpdx * 1.0) * 1.0 / (2.0 / 5600.0);  // u_tau delta / nu, delta = 1
  for (Index i = 0; i <= nx; ++i) {
    result.maxColumnFlowError =
        std::max(result.maxColumnFlowError,
                 std::abs(columnFlow(*run.mesh, run.simpleResult->massFlux, nx, ny, i) - 2.0));
  }
  return result;
}

}  // namespace

// k-epsilon: distorted and Cartesian Re_tau agree to 1% (measured 0.06% at
// 48x12, 0.02% at 96x24); mass conserved at every face column.
TEST(StructuredQuadProductionCase, KEpsilonChannelMatchesCartesianOnTiltedMesh) {
  const ChannelResult cartesian = runChannel("k_epsilon", false);
  const ChannelResult distorted = runChannel("k_epsilon", true);
  std::printf("k_epsilon Re_tau: Cartesian %.3f, distorted %.3f\n", cartesian.reTau,
              distorted.reTau);
  EXPECT_LE(std::abs(distorted.reTau - cartesian.reTau), 0.01 * cartesian.reTau);
  EXPECT_LE(distorted.maxColumnFlowError, 1e-6);
}

// SST (wall distance P12-MESH-001-exact on this mesh): agree to 5%
// (measured 3.3% at 48x12, 0.4% at 96x24 -- the difference shrinks under
// refinement).
TEST(StructuredQuadProductionCase, SstChannelMatchesCartesianOnTiltedMesh) {
  const ChannelResult cartesian = runChannel("sst", false);
  const ChannelResult distorted = runChannel("sst", true);
  std::printf("sst Re_tau: Cartesian %.3f, distorted %.3f\n", cartesian.reTau, distorted.reTau);
  EXPECT_LE(std::abs(distorted.reTau - cartesian.reTau), 0.05 * cartesian.reTau);
  EXPECT_LE(distorted.maxColumnFlowError, 1e-6);
}
