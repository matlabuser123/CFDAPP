#include <gtest/gtest.h>

#include <cstdio>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::discretization::GradientScheme;
using cfd::discretization::gradientSchemeName;
using cfd::discretization::LeastSquaresGradientResult;
using cfd::discretization::parseGradientScheme;
using cfd::discretization::solveLeastSquaresGradient;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(GradientTest, ConstantFieldHasZeroGradient) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries =
      cfd::test::makeExactBoundaries(mesh, [](const cfd::Vector2&) { return 7.0; });
  const ScalarField field(mesh.numberOfCells(), 7.0);

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 0.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 0.0, 1e-10);
  }
}

TEST(GradientTest, PhiXGivesGradientOneZero) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiX(cell.centroid());
  }

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 1.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 0.0, 1e-10);
  }
}

TEST(GradientTest, PhiYGivesGradientZeroOne) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiY);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiY(cell.centroid());
  }

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 0.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 1.0, 1e-10);
  }
}

TEST(GradientTest, QuadraticFieldMatchesAnalyticalGradient) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(16, 16, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  const Real error = cfd::test::l2CellErrorVector(mesh, grad, cfd::test::gradQuadratic);
  EXPECT_LT(error, 1e-9);
}

TEST(GradientTest, DefaultSchemeArgumentMatchesExplicitGreenGauss) {
  // P12-NUM-002: the new `scheme` parameter defaults to GreenGauss, so
  // every pre-P12-NUM-002 call site (which never passes it) is
  // byte-identical.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());

  const auto withoutScheme = cfd::discretization::gradient(mesh, field, boundaries);
  const auto withExplicitGreenGauss =
      cfd::discretization::gradient(mesh, field, boundaries, GradientScheme::GreenGauss);
  for (const auto& cell : mesh.cells()) {
    EXPECT_DOUBLE_EQ(withoutScheme[cell.id()].x, withExplicitGreenGauss[cell.id()].x);
    EXPECT_DOUBLE_EQ(withoutScheme[cell.id()].y, withExplicitGreenGauss[cell.id()].y);
  }
}

// =====================================================================
// P12-NUM-002: GradientScheme parsing/naming.
// =====================================================================

TEST(GradientSchemeTest, ParsesBothValidNames) {
  EXPECT_EQ(parseGradientScheme("green_gauss"), GradientScheme::GreenGauss);
  EXPECT_EQ(parseGradientScheme("least_squares"), GradientScheme::LeastSquares);
}

TEST(GradientSchemeTest, RejectsInvalidName) {
  EXPECT_THROW((void)parseGradientScheme("GreenGauss"), InvalidArgumentError);  // case-sensitive
  EXPECT_THROW((void)parseGradientScheme("weighted_least_squares"), InvalidArgumentError);
  EXPECT_THROW((void)parseGradientScheme(""), InvalidArgumentError);
}

TEST(GradientSchemeTest, NameRoundTripsThroughParse) {
  for (const auto scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    EXPECT_EQ(parseGradientScheme(gradientSchemeName(scheme)), scheme);
  }
}

// =====================================================================
// P12-NUM-002: solveLeastSquaresGradient (pure primitive).
// =====================================================================

TEST(SolveLeastSquaresGradientTest, ReconstructsExactGradientForConsistentLinearData) {
  // phi(x,y) = 2x + 3y -- any set of displacements exactly consistent
  // with this plane must reconstruct (2,3) exactly, regardless of
  // weighting (the weighted-least-squares residual is exactly zero).
  const std::vector<Vector2> displacements = {
      {1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}, {2.0, 1.0}};
  std::vector<Real> valueDifferences;
  for (const auto& d : displacements) valueDifferences.push_back((2.0 * d.x) + (3.0 * d.y));

  const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
  ASSERT_TRUE(result.wellConditioned);
  EXPECT_NEAR(result.gradient.x, 2.0, 1e-10);
  EXPECT_NEAR(result.gradient.y, 3.0, 1e-10);
}

TEST(SolveLeastSquaresGradientTest, ZeroDataGivesZeroGradient) {
  const std::vector<Vector2> displacements = {{1.0, 0.0}, {0.0, 1.0}, {-1.0, -1.0}};
  const std::vector<Real> valueDifferences = {0.0, 0.0, 0.0};
  const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
  ASSERT_TRUE(result.wellConditioned);
  EXPECT_NEAR(result.gradient.x, 0.0, 1e-12);
  EXPECT_NEAR(result.gradient.y, 0.0, 1e-12);
}

TEST(SolveLeastSquaresGradientTest, DetectsAColinearStencilAsIllConditioned) {
  // Every displacement lies on the x-axis -- no information about the
  // y-derivative exists, so the 2x2 system is exactly singular.
  const std::vector<Vector2> displacements = {{1.0, 0.0}, {-1.0, 0.0}, {2.0, 0.0}};
  const std::vector<Real> valueDifferences = {5.0, -5.0, 10.0};
  const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
  EXPECT_FALSE(result.wellConditioned);
}

TEST(SolveLeastSquaresGradientTest, DetectsASingleNeighborAsIllConditioned) {
  // One displacement can only ever determine the directional derivative
  // along it, never the full 2D gradient.
  const std::vector<Vector2> displacements = {{1.0, 1.0}};
  const std::vector<Real> valueDifferences = {4.0};
  const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
  EXPECT_FALSE(result.wellConditioned);
}

TEST(SolveLeastSquaresGradientTest, DetectsNoNeighborsAsIllConditioned) {
  const auto result = solveLeastSquaresGradient({}, {});
  EXPECT_FALSE(result.wellConditioned);
}

TEST(SolveLeastSquaresGradientTest, IgnoresAZeroDistanceEntryRatherThanDividingByZero) {
  // A zero-length displacement carries no directional information and
  // must never introduce a division by zero -- it is simply skipped, so
  // a stencil that is otherwise well-conditioned still resolves cleanly.
  const std::vector<Vector2> displacements = {
      {0.0, 0.0}, {1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}};
  const std::vector<Real> valueDifferences = {0.0, 2.0, -2.0, 3.0, -3.0};
  const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
  ASSERT_TRUE(result.wellConditioned);
  EXPECT_NEAR(result.gradient.x, 2.0, 1e-10);
  EXPECT_NEAR(result.gradient.y, 3.0, 1e-10);
}

TEST(SolveLeastSquaresGradientTest, NearlyColinearStencilIsAlsoRejected) {
  // Not exactly colinear, but close enough that the conditioning
  // threshold must still reject it (a "just barely not singular" system
  // is still numerically unreliable for a gradient estimate).
  const std::vector<Vector2> displacements = {
      {1.0, 1e-8}, {-1.0, -1e-8}, {2.0, 2e-8}, {-2.0, -2e-8}};
  const std::vector<Real> valueDifferences = {1.0, -1.0, 2.0, -2.0};
  const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
  EXPECT_FALSE(result.wellConditioned);
}

TEST(SolveLeastSquaresGradientTest, MismatchedSizesThrows) {
  EXPECT_THROW((void)solveLeastSquaresGradient({{1.0, 0.0}, {0.0, 1.0}}, {1.0}),
               InvalidArgumentError);
}

TEST(SolveLeastSquaresGradientTest, NeverProducesNonFiniteResultOnAcceptedInput) {
  // A broad sweep of well-conditioned inputs at very different scales --
  // never NaN/Inf, and never falsely reported well-conditioned with a
  // non-finite gradient.
  for (const Real scale : {1e-6, 1e-3, 1.0, 1e3, 1e6}) {
    const std::vector<Vector2> displacements = {
        {scale, 0.0}, {-scale, 0.0}, {0.0, scale}, {0.0, -scale}};
    const std::vector<Real> valueDifferences = {scale, -scale, 2.0 * scale, -2.0 * scale};
    const auto result = solveLeastSquaresGradient(displacements, valueDifferences);
    if (result.wellConditioned) {
      EXPECT_TRUE(std::isfinite(result.gradient.x)) << "scale=" << scale;
      EXPECT_TRUE(std::isfinite(result.gradient.y)) << "scale=" << scale;
    }
  }
}

// =====================================================================
// P12-NUM-002: leastSquaresGradient (mesh-level).
// =====================================================================

namespace {

cfd::boundary::BoundaryConditionSet fixedValueEverywhere(const Mesh& mesh, Real value) {
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(value));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(value));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedValue>(value));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedValue>(value));
  return boundaries;
}

}  // namespace

TEST(LeastSquaresTest, ConstantFieldHasZeroGradient) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, [](const Vector2&) { return -3.5; });
  const ScalarField field(mesh.numberOfCells(), -3.5);

  const auto grad = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 0.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 0.0, 1e-10);
  }
}

TEST(LeastSquaresTest, LinearFieldCartesianIsExactAtEveryCellIncludingBoundaryAdjacent) {
  // phi = 2x + 3y + 5 -- every cell (interior AND boundary-adjacent) must
  // reconstruct grad = (2,3) to near machine precision: the boundary
  // "virtual neighbor" (the assigned condition's own exact value at the
  // face's own true position) is itself an exact data point for a linear
  // field, so no offset/ghost trick (unlike Convection.cpp's upwind
  // boundary treatment) is needed for exactness here.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto phi = [](const Vector2& p) { return (2.0 * p.x) + (3.0 * p.y) + 5.0; };
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = phi(cell.centroid());

  const auto grad = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  Real maxError = 0.0;
  for (const auto& cell : mesh.cells()) {
    maxError =
        std::max({maxError, std::abs(grad[cell.id()].x - 2.0), std::abs(grad[cell.id()].y - 3.0)});
  }
  EXPECT_LT(maxError, 1e-9);
}

TEST(LeastSquaresTest, LinearFieldDistortedIsExactAtEveryCell) {
  // Same linear field, now on a genuinely distorted quadrilateral mesh
  // (P12-NUM-002 requirement 6/7) -- least-squares reconstructs the
  // gradient exactly regardless of mesh distortion (a well-known
  // property: a linear field makes every neighbor's data point satisfy
  // the plane equation exactly, so the weighted-least-squares residual
  // is identically zero, independent of geometry/weights). Contrast with
  // GreenGaussDistortedIsNotExactUnlikeLeastSquares below.
  const Index n = 8;
  const Mesh base = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.3 * (1.0 / n));
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto phi = [](const Vector2& p) { return (2.0 * p.x) + (3.0 * p.y) + 5.0; };
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = phi(cell.centroid());

  const auto grad = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  Real maxError = 0.0;
  for (const auto& cell : mesh.cells()) {
    maxError =
        std::max({maxError, std::abs(grad[cell.id()].x - 2.0), std::abs(grad[cell.id()].y - 3.0)});
  }
  EXPECT_LT(maxError, 1e-9);
}

TEST(LeastSquaresTest, GreenGaussDistortedIsNotExactUnlikeLeastSquares) {
  // The headline reason distorted-mesh gradient reconstruction needs
  // least-squares or a correction: PLAIN Green-Gauss (no skewness
  // correction -- greenGaussGradient with 0 sweeps, exactly the
  // pre-P12-NUM-003 formula) has a genuine, non-vanishing error for a
  // LINEAR field on a distorted mesh (its internal-face values sit at the
  // owner-neighbor-line crossing, not at the face centroid), while
  // least-squares stays exact. P12-NUM-003 made the production
  // GradientScheme::GreenGauss skewness-corrected: its error for the same
  // field collapses to ~1e-9 (not exact -- a fixed number of sweeps of a
  // contracting iteration). All three measured, not assumed -- see
  // results/p12-num-002/summary.md and results/p12-num-003/summary.md.
  const Index n = 8;
  const Mesh base = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.3 * (1.0 / n));
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto phi = [](const Vector2& p) { return (2.0 * p.x) + (3.0 * p.y) + 5.0; };
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = phi(cell.centroid());

  const auto gradPlainGreenGauss =
      cfd::discretization::greenGaussGradient(mesh, field, boundaries, /*skewCorrectionSweeps=*/0);
  const auto gradGreenGauss =
      cfd::discretization::gradient(mesh, field, boundaries, GradientScheme::GreenGauss);
  const auto gradLeastSquares =
      cfd::discretization::gradient(mesh, field, boundaries, GradientScheme::LeastSquares);

  const auto maxError = [&](const cfd::fields::VectorField& grad) {
    Real worst = 0.0;
    for (const auto& cell : mesh.cells()) {
      worst =
          std::max({worst, std::abs(grad[cell.id()].x - 2.0), std::abs(grad[cell.id()].y - 3.0)});
    }
    return worst;
  };
  EXPECT_LT(maxError(gradLeastSquares), 1e-9);
  EXPECT_GT(maxError(gradPlainGreenGauss), 1e-3)
      << "expected a genuine, non-vanishing plain Green-Gauss error on a distorted mesh";
  EXPECT_LT(maxError(gradGreenGauss), 1e-8);
  EXPECT_GT(maxError(gradGreenGauss), maxError(gradLeastSquares));
}

TEST(LeastSquaresTest, BoundaryCellsUseTheAssignedConditionAtTheFacesOwnPosition) {
  // A direct, hand-checkable boundary-treatment test: a 1x1 mesh has
  // every face on the boundary, so the WHOLE stencil comes from
  // boundary conditions. phi = x (phiX): FixedValue(exact) at every
  // face reproduces grad = (1,0) exactly (already covered generally by
  // LinearFieldCartesianIsExactAtEveryCellIncludingBoundaryAdjacent, but
  // this isolates the single-cell, all-boundary-faces case explicitly).
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(1, 1, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);
  ScalarField field(mesh.numberOfCells());
  field[0] = cfd::test::phiX(mesh.cell(0).centroid());

  const auto grad = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  EXPECT_NEAR(grad[0].x, 1.0, 1e-9);
  EXPECT_NEAR(grad[0].y, 0.0, 1e-9);
}

TEST(LeastSquaresTest, RejectsNonScalarBoundaryCondition) {
  // Mirrors Gradient.cpp's own GreenGauss behavior: a boundary patch
  // assigned a non-scalar condition is a configuration error, not a
  // silently-wrong reconstruction.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField field(mesh.numberOfCells(), 1.0);
  const cfd::boundary::BoundaryConditionSet emptyBoundaries;  // no condition assigned at all.
  EXPECT_THROW((void)cfd::discretization::leastSquaresGradient(mesh, field, emptyBoundaries),
               InvalidArgumentError);
}

TEST(LeastSquaresTest, MismatchedFieldSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 0.0);
  const ScalarField field(mesh.numberOfCells() + 1, 0.0);
  EXPECT_THROW((void)cfd::discretization::leastSquaresGradient(mesh, field, boundaries),
               InvalidArgumentError);
}

TEST(LeastSquaresTest, RepeatedCallsAreDeterministic) {
  const Index n = 8;
  const Mesh base = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.2 * (1.0 / n));
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiSmooth(cell.centroid());

  const auto first = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  const auto second = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_EQ(first[cell.id()].x, second[cell.id()].x);
    EXPECT_EQ(first[cell.id()].y, second[cell.id()].y);
  }
}

TEST(LeastSquaresTest, SmoothNonlinearFieldIsFiniteAndReasonablyAccurate) {
  // Not an exactness claim (only linear fields are exact) -- just a
  // sanity/no-NaN check on a genuinely curved field, ahead of the formal
  // convergence study in test_grid_refinement.cpp.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(16, 16, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiSmooth(cell.centroid());

  const auto grad = cfd::discretization::leastSquaresGradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    ASSERT_TRUE(std::isfinite(grad[cell.id()].x));
    ASSERT_TRUE(std::isfinite(grad[cell.id()].y));
    const Vector2 exact = cfd::test::gradSmooth(cell.centroid());
    EXPECT_NEAR(grad[cell.id()].x, exact.x, 0.5);
    EXPECT_NEAR(grad[cell.id()].y, exact.y, 0.5);
  }
}

// ===========================================================================
// P12-NUM-003 continuation: skewness correction in PRODUCTION -- the
// Green-Gauss gradient (GradientScheme::GreenGauss) evaluates its internal
// face values with interpolateInternalFaceSkewCorrected.
// ===========================================================================

// The production Green-Gauss gradient IS the skewness-corrected one: on a
// distorted mesh it equals greenGaussGradient with the documented sweep
// count bit-for-bit and differs from the plain (0-sweep) formula; on a
// Cartesian mesh (no skewed face) it equals the plain formula bit-for-bit.
TEST(SkewnessTest, ProductionInterpolationUsesCorrection) {
  const auto phi = [](const Vector2& p) { return std::sin(2.0 * p.x) + (p.y * p.y); };
  {
    const Mesh mesh =
        cfd::test::perFaceBoundaryMesh(cfd::test::createDistortedQuad2D(9, 9, 1.0, 1.0, 0.4 / 9.0));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) field[cell.id()] = phi(cell.centroid());
    const auto production = cfd::discretization::gradient(mesh, field, boundaries);
    const auto swept = cfd::discretization::greenGaussGradient(
        mesh, field, boundaries, cfd::discretization::kGreenGaussSkewCorrectionSweeps);
    const auto plain = cfd::discretization::greenGaussGradient(mesh, field, boundaries, 0);
    Real maxDifferenceFromPlain = 0.0;
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      EXPECT_EQ(production[i].x, swept[i].x);
      EXPECT_EQ(production[i].y, swept[i].y);
      maxDifferenceFromPlain =
          std::max(maxDifferenceFromPlain, cfd::magnitude(production[i] - plain[i]));
    }
    EXPECT_GT(maxDifferenceFromPlain, 1e-4) << "the correction must actually act on a skewed mesh";
  }
  {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(7, 6, 1.0, 1.0);
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) field[cell.id()] = phi(cell.centroid());
    const auto production = cfd::discretization::gradient(mesh, field, boundaries);
    const auto plain = cfd::discretization::greenGaussGradient(mesh, field, boundaries, 0);
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      EXPECT_EQ(production[i].x, plain[i].x);
      EXPECT_EQ(production[i].y, plain[i].y);
    }
  }
}

// The corrected production operator improves error: Green-Gauss gradient
// of a linear field on distorted meshes (0.10h / 0.25h / 0.45h), plain vs
// production (skewness-corrected) -- each measured and printed; and the
// sweep study behind kGreenGaussSkewCorrectionSweeps (error contracts every
// sweep).
TEST(SkewnessTest, CorrectedProductionOperatorImprovesError) {
  const auto phi = [](const Vector2& p) { return (2.0 * p.x) + (3.0 * p.y) + 5.0; };
  for (const Real fraction : {0.10, 0.25, 0.45}) {
    const Index n = 10;
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, fraction / static_cast<Real>(n)));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) field[cell.id()] = phi(cell.centroid());
    const auto maxError = [&](const cfd::fields::VectorField& grad) {
      Real worst = 0.0;
      for (Index i = 0; i < mesh.numberOfCells(); ++i) {
        worst = std::max(worst, cfd::magnitude(grad[i] - Vector2{2.0, 3.0}));
      }
      return worst;
    };
    std::printf(
        "\nGreen-Gauss gradient, linear field, distorted 10x10 (%.2fh): max error by sweep:",
        fraction);
    Real previous = 0.0;
    for (Index sweeps = 0; sweeps <= 5; ++sweeps) {
      const Real error =
          maxError(cfd::discretization::greenGaussGradient(mesh, field, boundaries, sweeps));
      std::printf(" %llu:%.3g", static_cast<unsigned long long>(sweeps), error);
      if (sweeps > 0 && previous > 1e-12) {
        EXPECT_LT(error, 0.1 * previous) << "sweep " << sweeps;
      }
      previous = error;
    }
    std::printf("\n");
    const Real plain =
        maxError(cfd::discretization::greenGaussGradient(mesh, field, boundaries, 0));
    const Real production = maxError(cfd::discretization::gradient(mesh, field, boundaries));
    EXPECT_GT(plain, 1e-3);
    EXPECT_LT(production, 1e-7);
  }
}
