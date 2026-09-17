// P12-MESH-004 -- mesh quality & validation campaign on the production path,
// against the gate fixed before the final runs:
// results/p12-mesh-004/acceptance_gate.md (rules R1-R7, M1-M4, GC1-GC2).
//
// Regular tests (fast): the manufactured solution is checked independently
// (R4), every family's targeted quality metric responds to its control
// parameter (R1, R2), invalid meshes are rejected before the solver runs
// (R6), and a small production-path MMS smoke run.
//
// DISABLED_ tests (the campaign; minutes to an hour each, run explicitly
// with --gtest_also_run_disabled_tests, output kept in
// results/p12-mesh-004/logs and results/p12-mesh-004/data): the degradation
// campaign D1-D4 (R3, R5, R7), the MMS refinement study (M1-M4) and the
// P12-NUM-005 grid-convergence studies (GC1, GC2).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "MeshQualityCampaign.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/validation/GridConvergence.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using namespace cfd::test::meshq;
namespace fs = std::filesystem;
// (gtest's Test::Run() hides an unqualified Run inside TEST bodies.)
using CampaignRun = cfd::test::meshq::Run;

constexpr Real kEnergyImbalanceTolerance = 1e-8;  // R7
constexpr Real kVelocityOrderMinimum = 1.5;       // M3 (formal 2)
constexpr Real kTemperatureOrderMinimum = 0.75;   // M3 (formal 1)

fs::path workDirectory(const std::string& label) {
  return fs::temp_directory_path() / "cfdapp_mesh004" / label;
}

fs::path evidenceDirectory() {
  const fs::path directory = "results/p12-mesh-004/data";
  fs::create_directories(directory);
  return directory;
}

// --- The degradation families (acceptance_gate.md table) ---------------------

struct Level {
  std::string name;
  Real parameter;
  cfd::io::MeshConfig mesh;
};

struct Family {
  std::string id;
  std::string description;
  std::string targetedMetric;
  std::function<Real(const cfd::mesh::MeshQualityReport&)> metric;
  std::vector<Level> levels;  // Q0..Q4
};

Real expansionMaximum(const cfd::mesh::MeshQualityReport& q) {
  return q.expansionRatio.count > 0 ? q.expansionRatio.maximum : 1.0;
}

Family familyD1() {
  Family f{"D1",
           "smooth non-orthogonality (amplitude A)",
           "max non-orthogonality [deg]",
           [](const cfd::mesh::MeshQualityReport& q) { return q.nonOrthogonality.maximum; },
           {}};
  const Real amplitudes[] = {0.0, 0.025, 0.05, 0.075, 0.10};
  for (int k = 0; k < 5; ++k) {
    f.levels.push_back(
        {"Q" + std::to_string(k), amplitudes[k], smoothDistortedMesh(32, amplitudes[k])});
  }
  return f;
}

Family familyD2() {
  Family f{"D2",
           "cell-scale irregularity (fraction f)",
           "max skewness",
           [](const cfd::mesh::MeshQualityReport& q) { return q.skewness.maximum; },
           {}};
  const Real fractions[] = {0.0, 0.075, 0.15, 0.225, 0.30};
  for (int k = 0; k < 5; ++k) {
    f.levels.push_back(
        {"Q" + std::to_string(k), fractions[k], roughDistortedMesh(32, fractions[k])});
  }
  return f;
}

Family familyD3() {
  Family f{"D3",
           "aspect ratio (nx x ny, uniform)",
           "max aspect ratio",
           [](const cfd::mesh::MeshQualityReport& q) { return q.aspectRatio.maximum; },
           {}};
  const Index shapes[5][2] = {{32, 32}, {45, 23}, {64, 16}, {91, 11}, {128, 8}};
  for (int k = 0; k < 5; ++k) {
    const Real nominal = static_cast<Real>(shapes[k][0]) / static_cast<Real>(shapes[k][1]);
    f.levels.push_back(
        {"Q" + std::to_string(k), nominal, cartesianMesh(shapes[k][0], shapes[k][1])});
  }
  return f;
}

Family familyD4() {
  Family f{
      "D4", "expansion (geometric y-grading ratio r)", "max expansion ratio", expansionMaximum, {}};
  const Real ratios[] = {1.0, 1.1, 1.2, 1.3, 1.4};
  for (int k = 0; k < 5; ++k) {
    f.levels.push_back({"Q" + std::to_string(k), ratios[k], gradedMesh(32, ratios[k])});
  }
  return f;
}

// CaseWriter -> CaseReader -> CaseBuilder only (no solve).
cfd::io::SimulationSetup buildOnly(const std::string& label, const cfd::io::CaseDefinition& d) {
  const fs::path directory = workDirectory(label);
  cfd::io::CaseWriter::write(directory, d);
  return cfd::io::CaseBuilder{}.build(cfd::io::CaseReader{}.read(directory));
}

// --- R4: the manufactured solution, checked independently ---------------------

TEST(MeshQualityCampaign, ManufacturedForcingMatchesFiniteDifferences) {
  const Real e = 1e-5;
  Real worstMomentum = 0.0;
  Real worstHeat = 0.0;
  Real worstDivergence = 0.0;
  for (int k = 0; k < 50; ++k) {
    const Vector2 p{0.05 + (0.9 * std::fmod(k * 0.6180339887, 1.0)),
                    0.05 + (0.9 * std::fmod(k * 0.4142135623, 1.0))};
    const Vector2 xp{p.x + e, p.y};
    const Vector2 xm{p.x - e, p.y};
    const Vector2 yp{p.x, p.y + e};
    const Vector2 ym{p.x, p.y - e};
    const Vector2 ux = (exactVelocity(xp) - exactVelocity(xm)) * (0.5 / e);
    const Vector2 uy = (exactVelocity(yp) - exactVelocity(ym)) * (0.5 / e);
    const Vector2 lap = (exactVelocity(xp) + exactVelocity(xm) + exactVelocity(yp) +
                         exactVelocity(ym) - (exactVelocity(p) * 4.0)) *
                        (1.0 / (e * e));
    const Real px = (exactPressure(xp) - exactPressure(xm)) / (2.0 * e);
    const Real py = (exactPressure(yp) - exactPressure(ym)) / (2.0 * e);
    const Vector2 u = exactVelocity(p);
    const Vector2 f{(kDensity * ((u.x * ux.x) + (u.y * uy.x))) + px - (kViscosity * lap.x),
                    (kDensity * ((u.x * ux.y) + (u.y * uy.y))) + py - (kViscosity * lap.y)};
    worstMomentum =
        std::max(worstMomentum, magnitude(f - momentumForcing(p)) / (1.0 + magnitude(f)));
    const Real tx = (exactTemperature(xp) - exactTemperature(xm)) / (2.0 * e);
    const Real ty = (exactTemperature(yp) - exactTemperature(ym)) / (2.0 * e);
    const Real tl = (exactTemperature(xp) + exactTemperature(xm) + exactTemperature(yp) +
                     exactTemperature(ym) - (4.0 * exactTemperature(p))) /
                    (e * e);
    const Real q = (kDensity * kSpecificHeat * ((u.x * tx) + (u.y * ty))) - (kConductivity * tl);
    worstHeat = std::max(worstHeat, std::abs(q - heatSource(p)) / (1.0 + std::abs(q)));
    worstDivergence = std::max(worstDivergence, std::abs(ux.x + uy.y));
  }
  std::printf(
      "forcing vs central differences: max relative difference momentum %.2e, heat %.2e; "
      "max |div u| %.2e\n",
      worstMomentum, worstHeat, worstDivergence);
  EXPECT_LE(worstMomentum, 1e-6);
  EXPECT_LE(worstHeat, 1e-6);
  EXPECT_LE(worstDivergence, 1e-6);
}

// The exact solution satisfies the case's own boundary conditions (walls,
// zero-gradient pressure, T = 0) exactly -- so the uniform production
// boundary conditions are the manufactured problem's.
TEST(MeshQualityCampaign, ManufacturedSolutionSatisfiesTheCaseBoundaryConditions) {
  const Real e = 1e-6;
  Real worst = 0.0;
  for (int k = 0; k <= 20; ++k) {
    const Real s = static_cast<Real>(k) / 20.0;
    for (const Vector2& p : {Vector2{0.0, s}, Vector2{1.0, s}, Vector2{s, 0.0}, Vector2{s, 1.0}}) {
      worst = std::max(worst, magnitude(exactVelocity(p)));
      worst = std::max(worst, std::abs(exactTemperature(p)));
    }
    // dp/dn on the four sides: central difference across the wall (the
    // closed form extends smoothly past it), O(e^2).
    const auto dn = [&](const Vector2& inside, const Vector2& outside) {
      return std::abs(exactPressure(inside) - exactPressure(outside)) / (2.0 * e);
    };
    worst = std::max(worst, dn({e, s}, {-e, s}));
    worst = std::max(worst, dn({1.0 + e, s}, {1.0 - e, s}));
    worst = std::max(worst, dn({s, e}, {s, -e}));
    worst = std::max(worst, dn({s, 1.0 + e}, {s, 1.0 - e}));
  }
  EXPECT_LE(worst, 1e-9);
}

// --- R1 / R2: the quality metrics respond to each control parameter -----------

TEST(MeshQualityCampaign, EachFamilysTargetedMetricIncreasesWithItsParameter) {
  for (const Family& family : {familyD1(), familyD2(), familyD3(), familyD4()}) {
    std::printf("%s %s -- targeted metric: %s\n", family.id.c_str(), family.description.c_str(),
                family.targetedMetric.c_str());
    Real previousParameter = -std::numeric_limits<Real>::infinity();
    Real previousMetric = -std::numeric_limits<Real>::infinity();
    for (const Level& level : family.levels) {
      const auto setup =
          buildOnly(family.id + "_" + level.name, caseDefinition(level.mesh, Discretization{}));
      const auto& q = setup.meshQuality;
      const Real metric = family.metric(q);
      std::printf(
          "  %s parameter %-8.4g %-19s metric %10.5f | non-orth %7.3f skew %.4f AR %8.3f "
          "expansion %6.3f\n",
          level.name.c_str(), level.parameter, cfd::mesh::meshQualityStatusName(q.status), metric,
          q.nonOrthogonality.maximum, q.skewness.maximum, q.aspectRatio.maximum,
          expansionMaximum(q));
      EXPECT_TRUE(q.valid) << family.id << " " << level.name;
      EXPECT_GT(level.parameter, previousParameter) << family.id << " " << level.name;  // R1
      EXPECT_GT(metric, previousMetric) << family.id << " " << level.name;              // R2
      previousParameter = level.parameter;
      previousMetric = metric;
    }
    // Q0 is the orthogonal uniform mesh.
    const auto q0 =
        buildOnly(family.id + "_Q0", caseDefinition(family.levels[0].mesh, {})).meshQuality;
    EXPECT_NEAR(q0.nonOrthogonality.maximum, 0.0, 1e-9) << family.id;
    EXPECT_NEAR(q0.skewness.maximum, 0.0, 1e-9) << family.id;
    EXPECT_NEAR(q0.aspectRatio.maximum, 1.0, 1e-9) << family.id;
    EXPECT_NEAR(expansionMaximum(q0), 1.0, 1e-9) << family.id;
  }
}

// --- R6: invalid meshes are rejected before the solver runs -------------------

cfd::io::MeshConfig uniformQuad(Index n) {
  cfd::io::MeshConfig mesh;
  mesh.type = "structured_quad";
  mesh.nx = n;
  mesh.ny = n;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      mesh.vertices.push_back(Vector2{static_cast<Real>(i) / static_cast<Real>(n),
                                      static_cast<Real>(j) / static_cast<Real>(n)});
    }
  }
  return mesh;
}

std::vector<Vector2> blockVertices(Real x0, Real x1, Real y0, Real y1, Index nx, Index ny) {
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      vertices.push_back(Vector2{x0 + ((x1 - x0) * static_cast<Real>(i) / static_cast<Real>(nx)),
                                 y0 + ((y1 - y0) * static_cast<Real>(j) / static_cast<Real>(ny))});
    }
  }
  return vertices;
}

// A multiblock case with the given blocks / interfaces; every listed block
// side is one "walls" patch with the campaign's wall conditions.
cfd::io::CaseDefinition multiBlockDefinition(std::vector<cfd::io::MeshBlockConfig> blocks,
                                             std::vector<cfd::io::MeshInterfaceConfig> interfaces,
                                             std::vector<cfd::io::MeshSideRefConfig> wallSides) {
  cfd::io::CaseDefinition d = caseDefinition(cartesianMesh(4, 4), Discretization{});
  const cfd::io::PatchBoundaryConfig wall = d.boundaries.patches.at("left");
  d.geometry = cfd::io::GeometryConfig{};
  d.geometry.type = "mesh_defined";
  d.mesh = cfd::io::MeshConfig{};
  d.mesh.type = "multiblock";
  d.mesh.blocks = std::move(blocks);
  d.mesh.interfaces = std::move(interfaces);
  d.mesh.patches = {cfd::io::MeshPatchConfig{"walls", std::move(wallSides)}};
  d.boundaries.patches.clear();
  d.boundaries.patches.emplace("walls", wall);
  return d;
}

std::vector<cfd::io::MeshSideRefConfig> allSides(const std::string& block) {
  return {{block, "left"}, {block, "right"}, {block, "bottom"}, {block, "top"}};
}

// Two 4 x 8 blocks side by side on the unit square, joined by one interface;
// `shiftB` lifts block b, `detachB` moves it away and drops the interface.
cfd::io::CaseDefinition twoBlockDefinition(Real shiftB, bool detachB) {
  const Real bx0 = detachB ? 1.5 : 0.5;
  std::vector<cfd::io::MeshBlockConfig> blocks = {
      {"a", 4, 8, blockVertices(0.0, 0.5, 0.0, 1.0, 4, 8)},
      {"b", 4, 8, blockVertices(bx0, bx0 + 0.5, shiftB, 1.0 + shiftB, 4, 8)}};
  std::vector<cfd::io::MeshInterfaceConfig> interfaces;
  std::vector<cfd::io::MeshSideRefConfig> walls = {{"a", "left"},  {"a", "bottom"}, {"a", "top"},
                                                   {"b", "right"}, {"b", "bottom"}, {"b", "top"}};
  if (detachB) {
    walls.push_back({"a", "right"});
    walls.push_back({"b", "left"});
  } else {
    interfaces.push_back({{"a", "right"}, {"b", "left"}, false});
  }
  return multiBlockDefinition(std::move(blocks), std::move(interfaces), std::move(walls));
}

struct RejectionOutcome {
  cfd::app::ProjectRunResult result;
  int progressCalls{0};
  bool resultsWritten{false};
};

RejectionOutcome runThroughProjectRunner(const fs::path& directory) {
  RejectionOutcome outcome;
  cfd::app::ProjectRunOptions options;
  options.progressCallback = [&](const cfd::pressure_velocity::SIMPLEIterationProgress&) {
    ++outcome.progressCalls;
  };
  outcome.result = cfd::app::ProjectRunner::run(directory, options);
  outcome.resultsWritten = fs::exists(directory / "results");
  return outcome;
}

void expectRejectedBeforeSolving(const std::string& label, const fs::path& directory,
                                 const std::vector<std::string>& anyOf) {
  const RejectionOutcome o = runThroughProjectRunner(directory);
  std::printf(
      "%-34s status %d, SIMPLE entered %s, progress callbacks %d, results written %s\n"
      "    error: %s\n",
      label.c_str(), static_cast<int>(o.result.status),
      o.result.simpleResult.has_value() ? "YES" : "no", o.progressCalls,
      o.resultsWritten ? "YES" : "no", o.result.errorMessage.c_str());
  EXPECT_EQ(o.result.status, cfd::app::ProjectRunStatus::InvalidCase) << label;
  EXPECT_EQ(cfd::app::exitCodeFor(o.result.status), 2) << label;  // the CLI's invalid-case code
  EXPECT_FALSE(o.result.simpleResult.has_value()) << label;
  EXPECT_FALSE(o.result.thermalResult.has_value()) << label;
  EXPECT_FALSE(o.result.exportSummary.has_value()) << label;
  EXPECT_EQ(o.progressCalls, 0) << label;
  EXPECT_FALSE(o.resultsWritten) << label;
  bool named = false;
  for (const std::string& s : anyOf)
    named = named || o.result.errorMessage.find(s) != std::string::npos;
  EXPECT_TRUE(named) << label << ": " << o.result.errorMessage;
}

void expectCaseRejectedBeforeSolving(const std::string& label, const cfd::io::CaseDefinition& d,
                                     const std::vector<std::string>& anyOf) {
  const fs::path directory = workDirectory("invalid_" + label);
  fs::remove_all(directory);
  cfd::io::CaseWriter::write(directory, d);
  expectRejectedBeforeSolving(label, directory, anyOf);
}

TEST(MeshQualityCampaign, InvalidMeshesAreRejectedBeforeTheSolverRuns) {
  const Discretization recipe;
  {
    // Zero-area cell: a flat block (its top row on its bottom line, shifted
    // half a cell -- four distinct collinear corners per cell).
    std::vector<Vector2> flat = blockVertices(0.0, 1.0, 0.0, 0.0, 4, 1);
    for (Index i = 0; i <= 4; ++i) flat[5 + i].x += 0.125;
    expectCaseRejectedBeforeSolving(
        "zero_area_cell", multiBlockDefinition({{"flat", 4, 1, flat}}, {}, allSides("flat")),
        {"zero area"});
  }
  {
    // Inverted cell: interior vertex (1,1) moved through its neighbour (2,1).
    cfd::io::MeshConfig mesh = uniformQuad(4);
    mesh.vertices[6] = Vector2{0.6, 0.25};
    expectCaseRejectedBeforeSolving("inverted_cell_vertex_through", caseDefinition(mesh, recipe),
                                    {"inverted", "not convex"});
  }
  {
    // Inverted block: a mirrored vertex grid (every cell clockwise).
    std::vector<Vector2> mirrored = blockVertices(1.0, 0.0, 0.0, 1.0, 4, 4);
    expectCaseRejectedBeforeSolving(
        "inverted_block_mirrored",
        multiBlockDefinition({{"mirror", 4, 4, mirrored}}, {}, allSides("mirror")), {"inverted"});
  }
  {
    // Degenerate face: vertex (1,1) placed on vertex (2,1).
    cfd::io::MeshConfig mesh = uniformQuad(4);
    mesh.vertices[6] = mesh.vertices[7];
    expectCaseRejectedBeforeSolving("degenerate_face", caseDefinition(mesh, recipe),
                                    {"degenerate edge"});
  }
  {
    // Non-finite coordinate, in memory (a GUI draft / API caller) -> CaseBuilder.
    cfd::io::MeshConfig mesh = uniformQuad(4);
    mesh.vertices[6] = Vector2{std::numeric_limits<Real>::quiet_NaN(), 0.25};
    std::string error = "no error";
    try {
      (void)cfd::io::CaseBuilder{}.build(caseDefinition(mesh, recipe));
    } catch (const cfd::Error& e) {
      error = e.what();
    }
    std::printf("%-34s CaseBuilder error: %s\n", "non_finite_coordinate (memory)", error.c_str());
    EXPECT_NE(error.find("non-finite"), std::string::npos) << error;
  }
  {
    // Non-finite coordinate in mesh.json (1e999 overflows to infinity).
    cfd::io::MeshConfig mesh = uniformQuad(4);
    mesh.vertices[6] = Vector2{0.3125, 0.25};
    const fs::path directory = workDirectory("invalid_non_finite_coordinate_file");
    fs::remove_all(directory);
    cfd::io::CaseWriter::write(directory, caseDefinition(mesh, recipe));
    std::ifstream in(directory / "mesh.json");
    std::stringstream text;
    text << in.rdbuf();
    in.close();
    std::string json = text.str();
    const auto at = json.find("0.3125");
    ASSERT_NE(at, std::string::npos);
    ASSERT_EQ(json.find("0.3125", at + 1), std::string::npos);
    json.replace(at, 6, "1e999");
    std::ofstream(directory / "mesh.json") << json;
    expectRejectedBeforeSolving("non_finite_coordinate (mesh.json)", directory, {"mesh.json"});
  }
  // Broken connectivity: the interface's vertices do not coincide.
  expectCaseRejectedBeforeSolving("broken_connectivity", twoBlockDefinition(0.01, false),
                                  {"does not coincide"});
  // Disconnected domain: two blocks, no interface.
  expectCaseRejectedBeforeSolving("disconnected_domain", twoBlockDefinition(0.0, true),
                                  {"disconnected cell regions"});
  // Beyond the degradation ranges.
  expectCaseRejectedBeforeSolving("D1_beyond_range_A0.2",
                                  caseDefinition(smoothDistortedMesh(32, 0.2), recipe),
                                  {"is not a strictly convex counter-clockwise quadrilateral"});
  expectCaseRejectedBeforeSolving("D2_beyond_range_f0.45",
                                  caseDefinition(roughDistortedMesh(32, 0.45), recipe),
                                  {"is not a strictly convex counter-clockwise quadrilateral"});
}

// --- Production-path smoke run (regular) ---------------------------------------

TEST(MeshQualityCampaign, ProductionPathManufacturedSolutionSmoke) {
  std::vector<CampaignRun> runs;
  for (const Index n : {8u, 16u}) {
    runs.push_back(run(workDirectory("smoke_" + std::to_string(n)),
                       caseDefinition(smoothDistortedMesh(n, 0.05), Discretization{})));
    std::printf("%s\n", describe("smooth A=0.05 n=" + std::to_string(n), runs.back()).c_str());
  }
  for (const CampaignRun& r : runs) {
    ASSERT_TRUE(r.built) << r.buildError;
    EXPECT_TRUE(r.converged) << r.status;
    EXPECT_TRUE(r.thermalConverged) << r.thermalStatus;
    EXPECT_TRUE(r.finite);
    EXPECT_LE(r.massImbalance, kMassImbalanceTolerance);
    EXPECT_LE(r.energyImbalance, kEnergyImbalanceTolerance);
  }
  EXPECT_LT(runs[1].u.l2, runs[0].u.l2);
  EXPECT_LT(runs[1].v.l2, runs[0].v.l2);
  EXPECT_LT(runs[1].temperature.l2, runs[0].temperature.l2);
}

// --- The campaign (DISABLED_: run explicitly) ----------------------------------

class CsvLog {
 public:
  explicit CsvLog(const std::string& name) : path_(evidenceDirectory() / name) {
    std::ofstream(path_) << csvHeader() << "\n";
  }
  void add(const std::string& row) const { std::ofstream(path_, std::ios::app) << row << "\n"; }

 private:
  fs::path path_;
};

bool acceptedRun(const CampaignRun& r) {
  return r.built && r.converged && r.thermalConverged && r.finite;
}

void checkConservation(const std::string& label, const CampaignRun& r) {  // R7
  EXPECT_LE(r.massImbalance, kMassImbalanceTolerance) << label;
  EXPECT_LE(r.energyImbalance, kEnergyImbalanceTolerance) << label;
}

Real ratio(Real value, Real reference) { return reference > 0.0 ? value / reference : 0.0; }

void runDegradationFamily(const Family& family, bool uncorrectedComparison,
                          const std::vector<Level>& reportedLevels) {
  CsvLog csv("degradation_" + family.id + ".csv");
  Discretization corrected;
  Discretization uncorrected;
  uncorrected.nonOrthogonalCorrections = 0;
  std::vector<CampaignRun> correctedRuns;
  std::vector<CampaignRun> uncorrectedRuns;
  std::printf("=== %s %s; corrected recipe (2 corrections, least_squares)\n", family.id.c_str(),
              family.description.c_str());
  for (const Level& level : family.levels) {
    const std::string label = family.id + "_" + level.name;
    correctedRuns.push_back(run(workDirectory(label), caseDefinition(level.mesh, corrected)));
    const CampaignRun& r = correctedRuns.back();
    std::printf("%s\n", describe(label + " " + std::to_string(level.parameter), r).c_str());
    std::fflush(stdout);
    csv.add(csvRow(family.id, level.name, level.parameter, "corrected", r));
    // R3
    EXPECT_TRUE(r.built) << label << ": " << r.buildError;
    EXPECT_TRUE(r.converged) << label << ": " << r.status;
    EXPECT_TRUE(r.thermalConverged) << label << ": " << r.thermalStatus;
    EXPECT_TRUE(r.finite) << label;
    if (acceptedRun(r)) checkConservation(label, r);
  }
  if (uncorrectedComparison) {
    std::printf("--- %s uncorrected recipe (0 corrections, least_squares) -- reported\n",
                family.id.c_str());
    for (const Level& level : family.levels) {
      const std::string label = family.id + "_" + level.name + "_uncorrected";
      uncorrectedRuns.push_back(run(workDirectory(label), caseDefinition(level.mesh, uncorrected)));
      const CampaignRun& r = uncorrectedRuns.back();
      std::printf("%s\n", describe(label + " " + std::to_string(level.parameter), r).c_str());
      std::fflush(stdout);
      csv.add(csvRow(family.id, level.name, level.parameter, "uncorrected", r));
      if (acceptedRun(r)) checkConservation(label, r);
    }
  }
  for (const Level& level : reportedLevels) {
    const std::string label = family.id + "_" + level.name;
    const CampaignRun r = run(workDirectory(label), caseDefinition(level.mesh, corrected));
    std::printf("%s (reported)\n",
                describe(label + " " + std::to_string(level.parameter), r).c_str());
    std::fflush(stdout);
    csv.add(csvRow(family.id, level.name, level.parameter, "corrected", r));
  }

  // R5: the quantitative degradation-vs-error report.
  const CampaignRun& q0 = correctedRuns.front();
  std::printf("--- %s R5: error and cost relative to Q0 (corrected recipe)\n", family.id.c_str());
  std::printf(
      "level  parameter  %-22s  u L2/Q0   v L2/Q0   p L2/Q0   T L2/Q0   SIMPLE it/Q0  "
      "contraction  thermal it\n",
      family.targetedMetric.c_str());
  for (std::size_t k = 0; k < correctedRuns.size(); ++k) {
    const CampaignRun& r = correctedRuns[k];
    std::printf("%-5s  %9.4g  %22.5f  %8.3f  %8.3f  %8.3f  %8.3f  %12.3f  %11.6f  %10zu\n",
                family.levels[k].name.c_str(), family.levels[k].parameter, family.metric(r.quality),
                ratio(r.u.l2, q0.u.l2), ratio(r.v.l2, q0.v.l2),
                ratio(r.pressure.l2, q0.pressure.l2), ratio(r.temperature.l2, q0.temperature.l2),
                ratio(static_cast<Real>(r.iterations), static_cast<Real>(q0.iterations)),
                r.continuityContraction, static_cast<std::size_t>(r.thermalIterations));
  }
  if (uncorrectedComparison) {
    std::printf("--- %s R5: uncorrected / corrected L2 error (same mesh)\n", family.id.c_str());
    for (std::size_t k = 0; k < uncorrectedRuns.size(); ++k) {
      const CampaignRun& c = correctedRuns[k];
      const CampaignRun& u = uncorrectedRuns[k];
      if (!acceptedRun(u)) {
        std::printf("%-5s  uncorrected run not accepted: SIMPLE %s, thermal %s, finite %d\n",
                    family.levels[k].name.c_str(), u.status.c_str(), u.thermalStatus.c_str(),
                    u.finite ? 1 : 0);
        continue;
      }
      std::printf("%-5s  u %8.3f  v %8.3f  p %8.3f  T %8.3f  SIMPLE iterations %zu vs %zu\n",
                  family.levels[k].name.c_str(), ratio(u.u.l2, c.u.l2), ratio(u.v.l2, c.v.l2),
                  ratio(u.pressure.l2, c.pressure.l2), ratio(u.temperature.l2, c.temperature.l2),
                  static_cast<std::size_t>(u.iterations), static_cast<std::size_t>(c.iterations));
    }
  }
}

TEST(MeshQualityCampaign, DISABLED_DegradationD1NonOrthogonality) {
  runDegradationFamily(familyD1(), true, {{"Q5", 0.12, smoothDistortedMesh(32, 0.12)}});
}

TEST(MeshQualityCampaign, DISABLED_DegradationD2Irregularity) {
  runDegradationFamily(familyD2(), true, {});
}

TEST(MeshQualityCampaign, DISABLED_DegradationD3AspectRatio) {
  runDegradationFamily(familyD3(), false, {});
}

TEST(MeshQualityCampaign, DISABLED_DegradationD4Expansion) {
  runDegradationFamily(familyD4(), false, {});
}

// --- MMS refinement study (M1-M4) and P12-NUM-005 grid convergence (GC1, GC2) ---

Real observedOrder(Real coarse, Real fine) { return std::log(coarse / fine) / std::log(2.0); }

std::vector<cfd::validation::QuantitySpec> gridQuantities() {
  cfd::validation::QuantitySpec kinetic;
  kinetic.name = "kinetic_energy";
  kinetic.description = "1/2 sum |u_c|^2 V_c of the manufactured-solution cavity";
  kinetic.reference = kExactKineticEnergy;
  kinetic.referenceKind = "analytical";
  kinetic.options.formalOrder = 2.0;
  kinetic.options.absoluteNoise = 1e-7;
  cfd::validation::QuantitySpec temperature;
  temperature.name = "mean_temperature";
  temperature.description = "sum T_c V_c of the manufactured-solution cavity";
  temperature.reference = exactMeanTemperature();
  temperature.referenceKind = "analytical";
  temperature.options.formalOrder = 1.0;
  temperature.options.absoluteNoise = 1e-7;
  return {kinetic, temperature};
}

cfd::validation::GridStudyEntry gridEntry(Index n, const CampaignRun& r) {
  cfd::validation::GridStudyEntry entry;
  entry.spec = cfd::validation::GridSpec{"n" + std::to_string(n), n, n, 1.0, 1.0};
  entry.cells = r.cells;
  entry.h = cfd::validation::representativeGridSize(1.0, r.cells);
  entry.runtimeSeconds = r.seconds;
  entry.output.acceptance = r.acceptance;
  if (r.acceptance.accepted && !(r.thermalConverged && r.finite)) {
    entry.output.acceptance.accepted = false;
    entry.output.acceptance.reason = "thermal solve " + r.thermalStatus;
  }
  entry.output.quantities = {{"kinetic_energy", r.kineticEnergy},
                             {"mean_temperature", r.meanTemperature}};
  entry.output.solverIterations = r.iterations;
  return entry;
}

// Runs the P12-NUM-005 analysis on the 16 / 32 / 64 runs, writes and
// validates the report; `gated` applies GC1 and GC2.
void gridConvergence(const std::string& family, const std::vector<Index>& sizes,
                     const std::vector<CampaignRun>& runs, bool gated) {
  std::vector<cfd::validation::GridStudyEntry> entries;
  for (std::size_t k = 0; k < sizes.size(); ++k) {
    if (sizes[k] >= 16) entries.push_back(gridEntry(sizes[k], runs[k]));
  }
  ASSERT_EQ(entries.size(), 3u);
  const auto study = cfd::validation::analyzeGridStudy(
      "p12_mesh_004_" + family, "P12-MESH-004 manufactured-solution cavity, family " + family,
      entries, gridQuantities());
  const fs::path path = evidenceDirectory() / ("grid_convergence_" + family + ".json");
  cfd::validation::writeGridConvergenceReport(path.string(), study);
  EXPECT_TRUE(cfd::validation::validateGridConvergenceReportFile(path.string()).empty());
  std::printf("\n%s", cfd::validation::gridConvergenceReportMarkdown(study).c_str());
  for (const auto& q : study.quantities) {
    const auto& a = q.analysis;
    std::printf("GC %s %s: status %s%s", family.c_str(), q.spec.name.c_str(),
                std::string(cfd::validation::gridConvergenceStatusName(a.status)).c_str(),
                a.observedOrder ? "" : " (no order / GCI computed)");
    if (a.observedOrder) std::printf(", observed order %.3f", *a.observedOrder);
    if (a.uncertainty21 && q.values[2]) {
      std::printf(", fine %.10f, U21 %.3e, |fine - exact| %.3e", *q.values[2], *a.uncertainty21,
                  std::abs(*q.values[2] - *q.spec.reference));
    }
    std::printf("\n");
    if (gated && a.status == cfd::validation::GridConvergenceStatus::Asymptotic) {  // GC2
      ASSERT_TRUE(a.uncertainty21.has_value());
      EXPECT_LE(std::abs(*q.values[2] - *q.spec.reference), *a.uncertainty21)
          << family << " " << q.spec.name;
    }
  }
  if (gated) {
    EXPECT_TRUE(study.allSolvesAccepted) << study.rejectionReason;  // GC1
  }
}

void manufacturedSolutionStudy(const std::string& family, Real amplitude,
                               const Discretization& recipe, bool gated) {
  CsvLog csv("mms_" + family + ".csv");
  const std::vector<Index> sizes = {8, 16, 32, 64};
  std::vector<CampaignRun> runs;
  for (const Index n : sizes) {
    const std::string label = family + "_n" + std::to_string(n);
    const cfd::io::MeshConfig mesh =
        amplitude > 0.0 ? smoothDistortedMesh(n, amplitude) : cartesianMesh(n, n);
    runs.push_back(run(workDirectory(label), caseDefinition(mesh, recipe)));
    const CampaignRun& r = runs.back();
    std::printf("%s\n", describe(label, r).c_str());
    std::fflush(stdout);
    csv.add(csvRow(family, "n" + std::to_string(n), static_cast<Real>(n),
                   recipe.nonOrthogonalCorrections > 0 ? "corrected" : "uncorrected", r));
    if (gated) {  // M1
      EXPECT_TRUE(r.built) << label << ": " << r.buildError;
      EXPECT_TRUE(r.converged) << label << ": " << r.status;
      EXPECT_TRUE(r.thermalConverged) << label << ": " << r.thermalStatus;
      EXPECT_TRUE(r.finite) << label;
    }
    if (acceptedRun(r)) checkConservation(label, r);  // R7
  }
  std::printf("--- %s observed orders p = ln(e_n / e_2n) / ln 2 (L2; L1; Linf)\n", family.c_str());
  for (std::size_t k = 0; k + 1 < runs.size(); ++k) {
    const CampaignRun& c = runs[k];
    const CampaignRun& f = runs[k + 1];
    if (!acceptedRun(c) || !acceptedRun(f)) {
      std::printf("pair %zu -> %zu: not computed (a run was not accepted)\n",
                  static_cast<std::size_t>(sizes[k]), static_cast<std::size_t>(sizes[k + 1]));
      continue;
    }
    std::printf(
        "pair %2zu -> %2zu: u %.3f; %.3f; %.3f | v %.3f; %.3f; %.3f | p %.3f; %.3f; %.3f"
        " | T %.3f; %.3f; %.3f\n",
        static_cast<std::size_t>(sizes[k]), static_cast<std::size_t>(sizes[k + 1]),
        observedOrder(c.u.l2, f.u.l2), observedOrder(c.u.l1, f.u.l1),
        observedOrder(c.u.linf, f.u.linf), observedOrder(c.v.l2, f.v.l2),
        observedOrder(c.v.l1, f.v.l1), observedOrder(c.v.linf, f.v.linf),
        observedOrder(c.pressure.l2, f.pressure.l2), observedOrder(c.pressure.l1, f.pressure.l1),
        observedOrder(c.pressure.linf, f.pressure.linf),
        observedOrder(c.temperature.l2, f.temperature.l2),
        observedOrder(c.temperature.l1, f.temperature.l1),
        observedOrder(c.temperature.linf, f.temperature.linf));
    if (gated) {  // M2
      EXPECT_LT(f.u.l2, c.u.l2) << family << " pair " << k;
      EXPECT_LT(f.v.l2, c.v.l2) << family << " pair " << k;
      EXPECT_LT(f.temperature.l2, c.temperature.l2) << family << " pair " << k;
    }
  }
  if (gated) {  // M3, finest pair
    const CampaignRun& c = runs[2];
    const CampaignRun& f = runs[3];
    EXPECT_GE(observedOrder(c.u.l2, f.u.l2), kVelocityOrderMinimum) << family;
    EXPECT_GE(observedOrder(c.v.l2, f.v.l2), kVelocityOrderMinimum) << family;
    EXPECT_GE(observedOrder(c.temperature.l2, f.temperature.l2), kTemperatureOrderMinimum)
        << family;
  }
  gridConvergence(family, sizes, runs, gated);
}

TEST(MeshQualityCampaign, DISABLED_ManufacturedSolutionCartesian) {
  manufacturedSolutionStudy("cartesian", 0.0, Discretization{}, true);
}

TEST(MeshQualityCampaign, DISABLED_ManufacturedSolutionSmoothCorrected) {
  manufacturedSolutionStudy("smooth_A0.05_corrected", 0.05, Discretization{}, true);
}

TEST(MeshQualityCampaign, DISABLED_ManufacturedSolutionSmoothUncorrected) {  // M4, reported
  Discretization uncorrected;
  uncorrected.nonOrthogonalCorrections = 0;
  manufacturedSolutionStudy("smooth_A0.05_uncorrected", 0.05, uncorrected, false);
}

TEST(MeshQualityCampaign, DISABLED_GridConvergenceIrregularFamily) {  // reported
  CsvLog csv("mms_rough_f0.15_corrected.csv");
  const std::vector<Index> sizes = {16, 32, 64};
  std::vector<CampaignRun> runs;
  for (const Index n : sizes) {
    const std::string label = "rough_f0.15_n" + std::to_string(n);
    runs.push_back(run(workDirectory(label), caseDefinition(roughDistortedMesh(n, 0.15), {})));
    std::printf("%s\n", describe(label, runs.back()).c_str());
    std::fflush(stdout);
    csv.add(csvRow("rough_f0.15", "n" + std::to_string(n), static_cast<Real>(n), "corrected",
                   runs.back()));
    if (acceptedRun(runs.back())) checkConservation(label, runs.back());
  }
  for (std::size_t k = 0; k + 1 < runs.size(); ++k) {
    if (!acceptedRun(runs[k]) || !acceptedRun(runs[k + 1])) continue;
    std::printf("pair %2zu -> %2zu: L2 order u %.3f v %.3f p %.3f T %.3f\n",
                static_cast<std::size_t>(sizes[k]), static_cast<std::size_t>(sizes[k + 1]),
                observedOrder(runs[k].u.l2, runs[k + 1].u.l2),
                observedOrder(runs[k].v.l2, runs[k + 1].v.l2),
                observedOrder(runs[k].pressure.l2, runs[k + 1].pressure.l2),
                observedOrder(runs[k].temperature.l2, runs[k + 1].temperature.l2));
  }
  gridConvergence("rough_f0.15_corrected", sizes, runs, false);
}

}  // namespace
