// P12-COMP-002: production-path proof for the coupled compressible
// solver -- drives the real production entry point
// (cfd::app::ProjectRunner::run()) against a real case directory
// (cases/compressible_channel_coupled, physics.compressible.coupled:
// true), confirming physics.json's "coupled" flag genuinely dispatches to
// cfd::compressible::CompressibleSIMPLE through the same path the CLI/GUI
// use -- never a second, test-only construction of that class.
//
// Also the independent physical-validation gate for P12-COMP-002: the
// converged solution's pressure field is compared against the isothermal
// compressible-channel (lubrication) analytical solution of Arkilic et
// al. (1997) -- p(x)^2 linear in x -- using the converged solution's own
// measured mass flow rate, not an assumed one. See
// results/p12-comp-002/summary.md for the full derivation, the root
// cause this case's own convergence needed fixed (an unpreconditioned
// BiCGSTAB breakdown on this symmetric pressure system -- fixed by
// configuring this case's own pressure_linear_solver as CG, a pure
// case-configuration choice, not a change to any shared solver code),
// and the exact recorded numbers.
//
// This case's own solve is slow (order minutes, ~12,000 SIMPLE
// iterations at tight tolerances, since the analytical comparison needs
// a genuinely converged state, not just a finite one). An earlier draft
// of this file tried to share one run across several TEST_F cases via a
// SetUpTestSuite()/TearDownTestSuite() fixture -- that does not actually
// save anything under this project's `gtest_discover_tests` setup, which
// registers each named test as its own CTest entry invoked as a separate
// `--gtest_filter=Suite.Test` process, so SetUpTestSuite() (and its own
// expensive solve) reran once per test anyway (measured: ~243s per test,
// confirmed real, not assumed). Reverted to plain, independent TEST()
// cases instead -- the honest cost model, and it also means
// NonCoupledCaseStillUsesPostHocPathUnchanged (which uses a different,
// fast case) no longer needlessly pays for the slow fixture it never
// used.
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <map>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCaseDirectory = "cases/compressible_channel_coupled";

// Mirrors this case's own physics.json/geometry.json/boundaries.json --
// see results/p12-comp-002/summary.md for the full derivation.
constexpr Real kGasConstant = 287.05;
constexpr Real kTemperature = 300.0;
constexpr Real kReferencePressure = 101325.0;  // the "right" patch's exact Dirichlet outlet BC.
constexpr Real kDynamicViscosity = 0.58831;
constexpr Real kHeight = 0.05;
constexpr Real kLength = 1.0;

// Sums |massFlux| over every internal x-normal face sharing a given
// (rounded) x-coordinate -- the total mass flow rate through that
// vertical cross-section. This is the actual quantity the compressible
// pressure-correction equation enforces conservation of (unlike a naive
// cell-center density*velocity reconstruction, which is *not* required
// to satisfy this on a collocated grid with strong density variation --
// see results/p12-comp-002/summary.md for why an earlier, wrong analysis
// during development mistook this for a conservation defect).
std::map<long long, Real> massFlowByStation(const cfd::mesh::Mesh& mesh,
                                             const cfd::fields::SurfaceField& massFlux) {
  std::map<long long, Real> totals;
  for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const auto& sf = face.areaVector();
    if (sf.y != 0.0 || sf.x == 0.0) continue;  // internal x-normal faces only.
    const auto key = static_cast<long long>(std::llround(face.centroid().x * 1.0e6));
    totals[key] += std::abs(massFlux[f]);
  }
  return totals;
}

}  // namespace

TEST(CompressibleCoupledProductionCaseTest, DispatchesToCompressibleSimpleAndConverges) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.compressibleSimpleResult.has_value())
      << "physics.json's compressible.coupled=true must produce a "
         "CompressibleSIMPLEResult, not just the post-hoc CompressibleRunResult";
  EXPECT_TRUE(run.compressibleSimpleResult->converged());
  ASSERT_TRUE(run.compressibleResult.has_value());
}

// The actual conservation invariant this solver's pressure-correction
// equation enforces: constant across every internal cross-section, to
// near machine precision.
TEST(CompressibleCoupledProductionCaseTest, MassFlowRateIsConservedAtEveryInternalCrossSection) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.mesh.has_value());
  ASSERT_TRUE(run.compressibleResult.has_value());
  const auto& mesh = *run.mesh;
  const auto& massFlux = run.compressibleResult->massFlux;
  ASSERT_EQ(massFlux.size(), mesh.numberOfFaces());

  const auto totals = massFlowByStation(mesh, massFlux);
  ASSERT_GT(totals.size(), 40u) << "expected ~47 internal x-stations on this 48x8 mesh";
  const Real reference = totals.begin()->second;
  ASSERT_GT(reference, 0.0);
  for (const auto& [key, total] : totals) {
    EXPECT_NEAR(total, reference, 1e-6 * reference) << "cross-section x*1e6=" << key;
  }
}

// The independent physical-validation gate: Arkilic et al. (1997)
// isothermal compressible-lubrication result. Mass conservation
// (rho*u = const per unit width) + isothermal ideal gas (rho = p/(R T))
// + the no-slip parabolic lubrication-limit velocity profile
// (Q = -H^3/(12 mu) dp/dx) together give
// p * dp/dx = -12 mu R T mdot / H^3, i.e.
// d(p^2)/dx = -24 mu R T mdot / H^3 (the factor of 2 from
// p dp/dx = (1/2) d(p^2)/dx) -- so p(x)^2 is linear in x. Anchored at the
// outlet's own exact Dirichlet BC (p(L) = kReferencePressure,
// boundaries.json's "right" patch), not a fitted value; the slope uses
// the converged solution's own measured mass flow rate (proved constant
// by MassFlowRateIsConservedAtEveryInternalCrossSection above), not an
// assumed one.
TEST(CompressibleCoupledProductionCaseTest, MatchesArkilicIsothermalLubricationPressureProfile) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.mesh.has_value());
  ASSERT_TRUE(run.compressibleResult.has_value());
  const auto& mesh = *run.mesh;
  const auto& pAbs = run.compressibleResult->pressureAbsolute;
  const auto& massFlux = run.compressibleResult->massFlux;

  const auto totals = massFlowByStation(mesh, massFlux);
  ASSERT_FALSE(totals.empty());
  Real mdot = 0.0;
  for (const auto& [key, total] : totals) mdot += total;
  mdot /= static_cast<Real>(totals.size());

  const Real slope =
      24.0 * kDynamicViscosity * kGasConstant * kTemperature * mdot / (kHeight * kHeight * kHeight);
  const Real pOutSquared = kReferencePressure * kReferencePressure;
  auto analytical = [&](Real x) { return std::sqrt(pOutSquared + slope * (kLength - x)); };

  Real sumSquaredRelError = 0.0;
  Real maxRelError = 0.0;
  Index n = 0;
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    const Real numeric = pAbs[cell.id()];
    const Real predicted = analytical(x);
    ASSERT_GT(predicted, 0.0);
    const Real relError = (numeric - predicted) / predicted;
    sumSquaredRelError += relError * relError;
    maxRelError = std::max(maxRelError, std::abs(relError));
    ++n;
  }
  ASSERT_GT(n, 0);
  const Real l2RelError = std::sqrt(sumSquaredRelError / static_cast<Real>(n));

  // Tolerance: the lubrication approximation's own leading-order error is
  // O(reduced Reynolds number) = O(Re*(H/L)) -- ~0.17 for this case (see
  // results/p12-comp-002/summary.md) -- so a few percent residual is the
  // *expected*, physically-explained level of agreement, not FVM
  // discretization error. 5% comfortably bounds the measured ~1.3%
  // L2 / ~2.6% Linf error while staying tight enough to catch a genuine
  // regression in the pressure-density coupling.
  EXPECT_LT(l2RelError, 0.05) << "L2 relative error too large: " << l2RelError;
  EXPECT_LT(maxRelError, 0.05) << "Linf relative error too large: " << maxRelError;
}

TEST(CompressibleCoupledProductionCaseTest, ExportsCoupledCompressibleFields) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary->fieldsCsvPath.has_value());
  {
    std::ifstream csv(*run.exportSummary->fieldsCsvPath);
    ASSERT_TRUE(csv.is_open());
    std::string header;
    std::getline(csv, header);
    EXPECT_NE(header.find(",density"), std::string::npos) << header;
    EXPECT_NE(header.find("pressure_absolute"), std::string::npos) << header;
    EXPECT_NE(header.find("mach_number"), std::string::npos) << header;
  }
  {
    std::ifstream json(run.exportSummary->metadataPath);
    ASSERT_TRUE(json.is_open());
    const std::string content((std::istreambuf_iterator<char>(json)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"coupled\": true"), std::string::npos) << content;
    EXPECT_NE(content.find("\"status\": \"Converged\""), std::string::npos) << content;
  }
}

TEST(CompressibleCoupledProductionCaseTest, RepeatedRunIsDeterministic) {
  const CaseFixtureCopy firstFixture(kCaseDirectory);
  const ProjectRunResult first = ProjectRunner::run(firstFixture.path());
  ASSERT_EQ(first.status, ProjectRunStatus::Converged) << first.errorMessage;
  ASSERT_TRUE(first.compressibleResult.has_value());
  ASSERT_TRUE(first.compressibleSimpleResult.has_value());

  const CaseFixtureCopy secondFixture(kCaseDirectory);
  const ProjectRunResult second = ProjectRunner::run(secondFixture.path());
  ASSERT_EQ(second.status, ProjectRunStatus::Converged) << second.errorMessage;
  ASSERT_TRUE(second.compressibleResult.has_value());
  ASSERT_TRUE(second.compressibleSimpleResult.has_value());

  const auto& a = *first.compressibleResult;
  const auto& b = *second.compressibleResult;
  ASSERT_EQ(a.density.size(), b.density.size());
  for (Index i = 0; i < a.density.size(); ++i) {
    EXPECT_EQ(a.density[i], b.density[i]) << "cell " << i;
    EXPECT_EQ(a.pressureAbsolute[i], b.pressureAbsolute[i]) << "cell " << i;
  }
  EXPECT_EQ(first.compressibleSimpleResult->iterations, second.compressibleSimpleResult->iterations);
}

TEST(CompressibleCoupledProductionCaseTest, NonCoupledCaseStillUsesPostHocPathUnchanged) {
  // Reuses cases/compressible_validation (coupled absent/false, the
  // existing P10-APP-003 post-hoc case) to confirm the default behavior
  // this whole file's own case opted out of is completely unaffected --
  // fast (a few seconds), independent of every slow test above.
  const CaseFixtureCopy fixture("cases/compressible_validation");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.compressibleResult.has_value());
  EXPECT_FALSE(run.compressibleSimpleResult.has_value());
}
