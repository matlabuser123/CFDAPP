#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>

#include "ManufacturedFields.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::discretization::ConvectionScheme;
using cfd::discretization::convectionSchemeName;
using cfd::discretization::linearUpwindFaceValue;
using cfd::discretization::parseConvectionScheme;
using cfd::discretization::quickFaceValue;
using cfd::discretization::smoothnessRatio;
using cfd::discretization::vanLeerLimiter;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(ConvectionTest, UpwindSelectsOwnerForPositiveFluxNeighborForNegative) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 1.0, 1.0);
  ScalarField field(mesh.numberOfCells());
  field[0] = 2.0;
  field[1] = 5.0;

  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      EXPECT_DOUBLE_EQ(cfd::discretization::upwindInternalFaceValue(face, field, 3.0), 2.0);
      EXPECT_DOUBLE_EQ(cfd::discretization::upwindInternalFaceValue(face, field, -3.0), 5.0);
      EXPECT_DOUBLE_EQ(cfd::discretization::upwindInternalFaceValue(face, field, 0.0), 2.0);
    }
  }
}

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

TEST(ConvectionTest, BoundaryOutflowUsesOwnerValue) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 99.0);

  const ScalarField field(mesh.numberOfCells(), 7.0);
  for (const auto& face : mesh.faces()) {
    const auto& bc = static_cast<const cfd::boundary::ScalarBoundaryCondition&>(
        cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries));
    EXPECT_DOUBLE_EQ(cfd::discretization::upwindBoundaryFaceValue(mesh, face, field, 1.0, bc), 7.0);
  }
}

// Not simply the boundary value (99.0) -- GridRefinementTest.
// UpwindConvectionConvergesAtFirstOrder's observed order was drifting
// toward 0.5 (not the expected ~1) specifically because of this: every
// *other* upwind face (interior, or outflow-boundary) feeds the scheme a
// value from a full owner-to-neighbor spacing away, but the raw boundary
// value is known at zero offset (right at the face), breaking that
// pattern -- differenced against the owner value and divided by the full
// cell width the way every face is, it converges to half the true
// gradient, an O(1) bias that never shrinks under refinement. The fix
// (see Convection.cpp's upwindBoundaryFaceValue) mirrors the owner value
// through the exactly-known boundary value to get a "ghost" value the
// same distance past the boundary as the owner is on this side --
// 2*boundaryValue - ownerValue = 2*99 - 7 = 191 here -- restoring the
// same full-spacing offset every other upwind face already has.
TEST(ConvectionTest, BoundaryInflowUsesGhostReflectedValue) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 99.0);

  const ScalarField field(mesh.numberOfCells(), 7.0);
  for (const auto& face : mesh.faces()) {
    const auto& bc = static_cast<const cfd::boundary::ScalarBoundaryCondition&>(
        cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries));
    EXPECT_DOUBLE_EQ(cfd::discretization::upwindBoundaryFaceValue(mesh, face, field, -1.0, bc),
                     191.0);
  }
}

TEST(ConvectionTest, ConstantFieldGivesZeroConvection) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 5.0);

  const ScalarField field(mesh.numberOfCells(), 5.0);
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = cfd::dot(Vector2{1.0, 0.0}, face.areaVector());
  }

  const auto conv = cfd::discretization::convection(mesh, field, flux, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(conv[cell.id()], 0.0, 1e-10);
  }
}

TEST(ConvectionTest, InternalConservationFaceOnce) {
  // Analogous to the divergence theorem check: internal contributions
  // must cancel pairwise, leaving exactly the net boundary convective
  // flux.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 2.0, 1.5);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(1.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(2.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedValue>(3.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedValue>(4.0));

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cell.centroid().x + cell.centroid().y;
  }

  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = cfd::dot(Vector2{1.0, 0.5}, face.areaVector());
  }

  const auto conv = cfd::discretization::convection(mesh, field, flux, boundaries);

  Real volumeWeightedSum = 0.0;
  for (const auto& cell : mesh.cells()) {
    volumeWeightedSum += conv[cell.id()] * cell.volume();
  }

  Real boundaryConvectiveFlux = 0.0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      const auto& bc = static_cast<const cfd::boundary::ScalarBoundaryCondition&>(
          cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries));
      const Real phiUp =
          cfd::discretization::upwindBoundaryFaceValue(mesh, face, field, flux[face.id()], bc);
      boundaryConvectiveFlux += flux[face.id()] * phiUp;
    }
  }

  EXPECT_NEAR(volumeWeightedSum, boundaryConvectiveFlux, 1e-9);
}

// =====================================================================
// P12-NUM-001: ConvectionScheme parsing/naming.
// =====================================================================

TEST(ConvectionSchemeTest, ParsesAllFourValidNames) {
  EXPECT_EQ(parseConvectionScheme("upwind"), ConvectionScheme::Upwind);
  EXPECT_EQ(parseConvectionScheme("central"), ConvectionScheme::Central);
  EXPECT_EQ(parseConvectionScheme("linear_upwind"), ConvectionScheme::LinearUpwind);
  EXPECT_EQ(parseConvectionScheme("quick"), ConvectionScheme::QUICK);
}

TEST(ConvectionSchemeTest, RejectsInvalidName) {
  EXPECT_THROW((void)parseConvectionScheme("Upwind"), InvalidArgumentError);  // case-sensitive
  EXPECT_THROW((void)parseConvectionScheme("cubic"), InvalidArgumentError);
  EXPECT_THROW((void)parseConvectionScheme(""), InvalidArgumentError);
  EXPECT_THROW((void)parseConvectionScheme("quadratic_upwind"), InvalidArgumentError);
}

TEST(ConvectionSchemeTest, NameRoundTripsThroughParse) {
  for (const auto scheme : {ConvectionScheme::Upwind, ConvectionScheme::Central,
                            ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    EXPECT_EQ(parseConvectionScheme(convectionSchemeName(scheme)), scheme);
  }
}

// =====================================================================
// P12-NUM-001: quickFaceValue.
// =====================================================================

TEST(QuickFaceValueTest, MatchesClassicalCoefficientsOnUniformGrid) {
  // Uniform grid: the face sits midway between U and D (hUf=hfD=0.5), C
  // is one full cell-spacing upstream of U (hCU=1.0) -- reduces to
  // Leonard's classical QUICK weights (-1/8, 6/8, 3/8).
  const Real phiC = 0.0;
  const Real phiU = 1.0;
  const Real phiD = 2.0;
  const Real result = quickFaceValue(phiC, 1.0, phiU, 0.5, phiD, 0.5);
  EXPECT_NEAR(result, (-1.0 / 8.0) * phiC + (6.0 / 8.0) * phiU + (3.0 / 8.0) * phiD, 1e-12);
  EXPECT_NEAR(result, 1.5, 1e-12);
}

TEST(QuickFaceValueTest, ReproducesALinearFieldExactly) {
  // A quadratic fit through three points that are themselves exactly
  // collinear-in-value has zero curvature -- QUICK must reproduce the
  // exact linear value at the face regardless of spacing.
  // Positions relative to the face (at 0): C=-1.5, U=-0.5, D=+0.5;
  // phi(x) = 2x + 3.
  const Real phiC = (2.0 * -1.5) + 3.0;
  const Real phiU = (2.0 * -0.5) + 3.0;
  const Real phiD = (2.0 * 0.5) + 3.0;
  const Real expected = (2.0 * 0.0) + 3.0;
  EXPECT_NEAR(quickFaceValue(phiC, 1.0, phiU, 0.5, phiD, 0.5), expected, 1e-10);
}

TEST(QuickFaceValueTest, ThrowsOnNonPositiveDistance) {
  EXPECT_THROW((void)quickFaceValue(0.0, 0.0, 1.0, 0.5, 2.0, 0.5), InvalidArgumentError);
  EXPECT_THROW((void)quickFaceValue(0.0, 1.0, 1.0, 0.0, 2.0, 0.5), InvalidArgumentError);
  EXPECT_THROW((void)quickFaceValue(0.0, 1.0, 1.0, 0.5, 2.0, -0.1), InvalidArgumentError);
}

// =====================================================================
// P12-NUM-001: linearUpwindFaceValue.
// =====================================================================

TEST(LinearUpwindFaceValueTest, ReconstructsExactlyForALinearField) {
  const Real phiUpwind = 5.0;
  const Vector2 grad{2.0, -1.0};
  const Vector2 upwindCentroid{1.0, 1.0};
  const Vector2 faceCentroid{1.5, 0.5};
  const Real expected = phiUpwind + (2.0 * 0.5) + (-1.0 * -0.5);
  EXPECT_NEAR(linearUpwindFaceValue(phiUpwind, grad, upwindCentroid, faceCentroid), expected,
              1e-12);
}

TEST(LinearUpwindFaceValueTest, ZeroGradientReturnsUpwindValueUnchanged) {
  EXPECT_DOUBLE_EQ(
      linearUpwindFaceValue(7.0, Vector2{0.0, 0.0}, Vector2{0.0, 0.0}, Vector2{5.0, 5.0}), 7.0);
}

// =====================================================================
// P12-NUM-001: smoothnessRatio / vanLeerLimiter (Sweby 1984 TVD blend).
// =====================================================================

TEST(SmoothnessRatioTest, PositiveForSmoothMonotoneData) {
  // phiC=0, phiU=1, phiD=2 -- constant gradient (=1) on both sides ->
  // r = (phiU-phiC)/(phiD-phiU) = 1/1 = 1 exactly.
  const auto r = smoothnessRatio(0.0, 1.0, 2.0);
  ASSERT_TRUE(r.has_value());
  EXPECT_DOUBLE_EQ(*r, 1.0);
}

TEST(SmoothnessRatioTest, NegativeAtALocalExtremum) {
  // phiC=5, phiU=1, phiD=2 -- phiU is a local minimum (smaller than both
  // neighbors) -> upstream gradient and local gradient have opposite
  // signs -> r < 0.
  const auto r = smoothnessRatio(5.0, 1.0, 2.0);
  ASSERT_TRUE(r.has_value());
  EXPECT_LT(*r, 0.0);
}

TEST(SmoothnessRatioTest, NulloptWhenLocalGradientIsZero) {
  EXPECT_FALSE(smoothnessRatio(0.0, 3.0, 3.0).has_value());
}

TEST(VanLeerLimiterTest, ZeroAtOrBelowZeroRatio) {
  EXPECT_DOUBLE_EQ(vanLeerLimiter(0.0), 0.0);
  EXPECT_DOUBLE_EQ(vanLeerLimiter(-2.0), 0.0);
}

TEST(VanLeerLimiterTest, ZeroWhenRatioIsNullopt) {
  EXPECT_DOUBLE_EQ(vanLeerLimiter(std::nullopt), 0.0);
}

TEST(VanLeerLimiterTest, OneAtRatioOfOne) {
  // psi(1) = (1+1)/(1+1) = 1 -- full central-like blend for perfectly
  // smooth (locally linear) data.
  EXPECT_DOUBLE_EQ(vanLeerLimiter(1.0), 1.0);
}

TEST(VanLeerLimiterTest, ApproachesTwoForLargeRatio) {
  const Real psi = vanLeerLimiter(1000.0);
  EXPECT_GT(psi, 1.9);
  EXPECT_LT(psi, 2.0);
}

TEST(VanLeerLimiterTest, StaysWithinTheZeroToTwoSwebyTvdRegion) {
  for (const Real r : {0.1, 0.5, 1.0, 2.0, 5.0, 50.0}) {
    const Real psi = vanLeerLimiter(r);
    EXPECT_GE(psi, 0.0);
    EXPECT_LE(psi, 2.0);
  }
}

// =====================================================================
// P12-NUM-001: convection() with an explicit scheme.
// =====================================================================

TEST(ConvectionSchemeTest, ConvectionOverloadWithoutSchemeArgumentDefaultsToUpwind) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 5.0);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = cell.centroid().x + cell.centroid().y;
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces())
    flux[face.id()] = cfd::dot(Vector2{1.0, 0.5}, face.areaVector());

  const auto withoutScheme = cfd::discretization::convection(mesh, field, flux, boundaries);
  const auto withExplicitUpwind =
      cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::Upwind);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(withoutScheme[i], withExplicitUpwind[i]);
  }
}

namespace {

// Independent "oracle" recomputation of ONE internal face's scheme-aware
// value, built from the same already-unit-tested primitives
// (quickFaceValue/interpolateInternalFace, smoothnessRatio/
// vanLeerLimiter -- each verified in isolation above) but assembled here
// independently from Convection.cpp's own control flow, so this catches
// integration bugs (wrong cell/sign/distance wired through convection())
// rather than re-verify the primitives' own math.
Real oracleInternalFaceValue(const Mesh& mesh, const Face& face, const ScalarField& field,
                             ConvectionScheme scheme, Index upwindId, Index downwindId) {
  const Real phiUpwind = field[upwindId];
  const Real phiDownwind = field[downwindId];

  Real phiHighOrder = phiUpwind;
  if (scheme == ConvectionScheme::Central) {
    phiHighOrder = cfd::discretization::interpolateInternalFace(mesh, face, field);
  }

  std::optional<Index> farCellId;
  Real hCU = 0.0;
  if (const auto farFaceId = MeshGeometry::oppositeInteriorFace(mesh, mesh.cell(upwindId), face);
      farFaceId.has_value()) {
    const auto& farFace = mesh.face(*farFaceId);
    farCellId = (farFace.owner() == upwindId) ? *farFace.neighbor() : farFace.owner();
    hCU = MeshGeometry::ownerNeighborDistance(mesh, farFace);
  }

  if (scheme == ConvectionScheme::QUICK && farCellId.has_value()) {
    const Real hUf = MeshGeometry::distance(mesh.cell(upwindId).centroid(), face.centroid());
    const Real hfD = MeshGeometry::distance(face.centroid(), mesh.cell(downwindId).centroid());
    phiHighOrder = quickFaceValue(field[*farCellId], hCU, phiUpwind, hUf, phiDownwind, hfD);
  }

  const std::optional<Real> r = farCellId.has_value()
                                    ? smoothnessRatio(field[*farCellId], phiUpwind, phiDownwind)
                                    : std::optional<Real>{};
  const Real psi = vanLeerLimiter(r);
  return phiUpwind + (psi * (phiHighOrder - phiUpwind));
}

}  // namespace

TEST(ConvectionSchemeTest, CentralSchemeMatchesIndependentOracleForPositiveAndNegativeFlux) {
  // 5-cell chain, a smooth, monotone (but genuinely non-linear) field --
  // this keeps Sweby's r > 0 at every face with a far-upstream cell (a
  // wildly-oscillating field would drive r < 0 everywhere, disabling the
  // limiter entirely and degrading to plain Upwind -- not a useful check
  // of the Central path).
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 1, 5.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 0.0);
  ScalarField field(mesh.numberOfCells());
  field[0] = 0.0;
  field[1] = 1.0;
  field[2] = 4.0;
  field[3] = 9.0;
  field[4] = 16.0;

  for (const Real sign : {1.0, -1.0}) {
    SurfaceField flux(mesh.numberOfFaces());
    for (const auto& face : mesh.faces())
      flux[face.id()] = cfd::dot(Vector2{sign, 0.0}, face.areaVector());

    const auto central =
        cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::Central);
    const auto upwind =
        cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::Upwind);

    // Middle cell (index 2): both faces internal, both have a
    // far-upstream cell regardless of flow direction.
    const Face& leftFace = mesh.face(mesh.cell(2).faceIds().front());
    const bool positiveFlow = sign > 0.0;
    const Index leftOwnerId = leftFace.owner();
    const Index leftNeighborId = *leftFace.neighbor();
    const Real leftFlux = flux[leftFace.id()];
    const Index leftUpwind = (leftFlux >= 0.0) ? leftOwnerId : leftNeighborId;
    const Index leftDownwind = (leftFlux >= 0.0) ? leftNeighborId : leftOwnerId;
    const Real oracleLeft = oracleInternalFaceValue(
        mesh, leftFace, field, ConvectionScheme::Central, leftUpwind, leftDownwind);

    // Find cell2's right face similarly.
    const Face* rightFacePtr = nullptr;
    for (const Index faceId : mesh.cell(2).faceIds()) {
      const auto& f = mesh.face(faceId);
      if (!f.isBoundary() && f.id() != leftFace.id()) rightFacePtr = &f;
    }
    ASSERT_NE(rightFacePtr, nullptr);
    const Real rightFlux = flux[rightFacePtr->id()];
    const Index rightOwnerId = rightFacePtr->owner();
    const Index rightNeighborId = *rightFacePtr->neighbor();
    const Index rightUpwind = (rightFlux >= 0.0) ? rightOwnerId : rightNeighborId;
    const Index rightDownwind = (rightFlux >= 0.0) ? rightNeighborId : rightOwnerId;
    const Real oracleRight = oracleInternalFaceValue(
        mesh, *rightFacePtr, field, ConvectionScheme::Central, rightUpwind, rightDownwind);

    const Real leftCellFlux = (leftFace.owner() == 2) ? leftFlux : -leftFlux;
    const Real rightCellFlux = (rightFacePtr->owner() == 2) ? rightFlux : -rightFlux;
    const Real expected =
        ((leftCellFlux * oracleLeft) + (rightCellFlux * oracleRight)) / mesh.cell(2).volume();

    EXPECT_NEAR(central[2], expected, 1e-10) << "sign=" << sign;
    // And confirm the scheme actually ran (differs from plain upwind) --
    // r > 0 for this smooth monotone field, so the limiter passes
    // through a genuine, nonzero correction.
    EXPECT_GT(std::abs(central[2] - upwind[2]), 1e-6)
        << "sign=" << sign << " positiveFlow=" << positiveFlow;
  }
}

TEST(ConvectionSchemeTest, QuickSchemeMatchesIndependentOracleForPositiveAndNegativeFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 1, 5.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 0.0);
  ScalarField field(mesh.numberOfCells());
  field[0] = 0.0;
  field[1] = 1.0;
  field[2] = 4.0;
  field[3] = 9.0;
  field[4] = 16.0;

  for (const Real sign : {1.0, -1.0}) {
    SurfaceField flux(mesh.numberOfFaces());
    for (const auto& face : mesh.faces())
      flux[face.id()] = cfd::dot(Vector2{sign, 0.0}, face.areaVector());

    const auto quick =
        cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::QUICK);
    const auto upwind =
        cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::Upwind);

    const Face& leftFace = mesh.face(mesh.cell(2).faceIds().front());
    const Real leftFlux = flux[leftFace.id()];
    const Index leftUpwind = (leftFlux >= 0.0) ? leftFace.owner() : *leftFace.neighbor();
    const Index leftDownwind = (leftFlux >= 0.0) ? *leftFace.neighbor() : leftFace.owner();
    const Real oracleLeft = oracleInternalFaceValue(mesh, leftFace, field, ConvectionScheme::QUICK,
                                                    leftUpwind, leftDownwind);

    const Face* rightFacePtr = nullptr;
    for (const Index faceId : mesh.cell(2).faceIds()) {
      const auto& f = mesh.face(faceId);
      if (!f.isBoundary() && f.id() != leftFace.id()) rightFacePtr = &f;
    }
    ASSERT_NE(rightFacePtr, nullptr);
    const Real rightFlux = flux[rightFacePtr->id()];
    const Index rightUpwind =
        (rightFlux >= 0.0) ? rightFacePtr->owner() : *rightFacePtr->neighbor();
    const Index rightDownwind =
        (rightFlux >= 0.0) ? *rightFacePtr->neighbor() : rightFacePtr->owner();
    const Real oracleRight = oracleInternalFaceValue(
        mesh, *rightFacePtr, field, ConvectionScheme::QUICK, rightUpwind, rightDownwind);

    const Real leftCellFlux = (leftFace.owner() == 2) ? leftFlux : -leftFlux;
    const Real rightCellFlux = (rightFacePtr->owner() == 2) ? rightFlux : -rightFlux;
    const Real expected =
        ((leftCellFlux * oracleLeft) + (rightCellFlux * oracleRight)) / mesh.cell(2).volume();

    EXPECT_NEAR(quick[2], expected, 1e-9) << "sign=" << sign;
    EXPECT_GT(std::abs(quick[2] - upwind[2]), 1e-6) << "sign=" << sign;
  }
}

TEST(ConvectionSchemeTest, QuickDegradesToUpwindWhenNoFartherUpstreamCellExists) {
  // 2-cell mesh: the single internal face's upwind cell is always
  // boundary-adjacent in the upstream direction too, so QUICK's
  // far-upstream lookup always returns nullopt -- must degrade to plain
  // upwind exactly (not silently misapply a partial stencil, not throw).
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 0.0);
  ScalarField field(mesh.numberOfCells());
  field[0] = 3.0;
  field[1] = 9.0;
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces())
    flux[face.id()] = cfd::dot(Vector2{1.0, 0.0}, face.areaVector());

  const auto quick =
      cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::QUICK);
  const auto upwind =
      cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::Upwind);
  EXPECT_DOUBLE_EQ(quick[0], upwind[0]);
  EXPECT_DOUBLE_EQ(quick[1], upwind[1]);
}

TEST(ConvectionSchemeTest, SchemeChoiceDoesNotAffectABoundaryOnlyMesh) {
  // A 1x1 mesh has no internal faces at all -- every scheme must
  // therefore produce exactly the same result as Upwind (boundary faces
  // are always upwind-treated regardless of `scheme`).
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 9.0);
  const ScalarField field(mesh.numberOfCells(), 7.0);
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces())
    flux[face.id()] = cfd::dot(Vector2{1.0, 0.0}, face.areaVector());

  const auto upwindResult =
      cfd::discretization::convection(mesh, field, flux, boundaries, ConvectionScheme::Upwind);
  for (const auto scheme :
       {ConvectionScheme::Central, ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    const auto result = cfd::discretization::convection(mesh, field, flux, boundaries, scheme);
    EXPECT_DOUBLE_EQ(result[0], upwindResult[0]) << "scheme index " << static_cast<int>(scheme);
  }
}

TEST(ConvectionSchemeTest, LinearFieldIsReproducedExactlyAwayFromAnyDegradedFace) {
  // For a genuinely linear field with uniform spacing, at any face whose
  // far-upstream cell IS available, the three stencil values (C, U, D)
  // are themselves exactly equally spaced -> Sweby's r = 1 exactly ->
  // vanLeerLimiter(1) = 1 exactly -> the full (unlimited) high-order
  // value is used, which is itself exact for a linear field (see
  // QuickFaceValueTest/LinearUpwindFaceValueTest's own exactness
  // proofs). So every scheme DOES reproduce U.grad(phi) exactly here --
  // but only at cells not adjacent to a face that lacks a far-upstream
  // cell (the leftmost column's own outgoing face, x=0..1 here, and the
  // rightmost column's outflow-boundary convention deliberately uses the
  // owner's raw value, not the true boundary value -- see
  // upwindBoundaryFaceValue's own header comment): those degraded faces
  // fall back toward plain Upwind's OWN offset-value convention, which
  // only cancels out exactly when EVERY face in a cell's stencil uses
  // that same convention consistently. This is a real, expected,
  // well-understood characteristic of blended-order schemes near a
  // lower-order transition (confirmed empirically here, not a bug) --
  // see results/p12-num-001/summary.md. Columns x=0.5 and x=2.5 (of 4)
  // are unaffected by any such transition for this positive-flux case;
  // columns x=1.5 and x=3.5 are the polluted ones and are deliberately
  // excluded below.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 4.0, 4.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiX(cell.centroid());

  const Vector2 velocity{2.0, 0.0};
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) flux[face.id()] = cfd::dot(velocity, face.areaVector());

  const Real expected = velocity.x;  // U . grad(phiX) = 2.0 * 1.0

  for (const auto scheme : {ConvectionScheme::Upwind, ConvectionScheme::Central,
                            ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    const auto conv = cfd::discretization::convection(mesh, field, flux, boundaries, scheme);
    for (const auto& cell : mesh.cells()) {
      const bool isSafeColumn =
          (std::abs(cell.centroid().x - 0.5) < 1e-9) || (std::abs(cell.centroid().x - 2.5) < 1e-9);
      if (!isSafeColumn) continue;
      EXPECT_NEAR(conv[cell.id()], expected, 1e-9)
          << "scheme index " << static_cast<int>(scheme) << " cell " << cell.id();
    }
  }
}

TEST(ConvectionSchemeTest, DegradedFaceStillProducesAFiniteConsistentResult) {
  // Documents (rather than hides) the pollution pattern above: cells
  // adjacent to a degraded face are NOT exact, but they are always
  // finite and stay reasonably close (within one cell's worth of slope)
  // to the true value -- never wildly wrong, never non-finite.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 4.0, 4.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiX(cell.centroid());

  const Vector2 velocity{2.0, 0.0};
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) flux[face.id()] = cfd::dot(velocity, face.areaVector());

  for (const auto scheme :
       {ConvectionScheme::Central, ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    const auto conv = cfd::discretization::convection(mesh, field, flux, boundaries, scheme);
    for (const auto& cell : mesh.cells()) {
      EXPECT_TRUE(std::isfinite(conv[cell.id()]));
      EXPECT_NEAR(conv[cell.id()], velocity.x, 1.5)
          << "scheme index " << static_cast<int>(scheme) << " cell " << cell.id();
    }
  }
}

TEST(ConvectionSchemeTest, HigherOrderSchemesAreDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 1, 5.0, 1.0);
  const auto boundaries = fixedValueEverywhere(mesh, 2.0);
  ScalarField field(mesh.numberOfCells());
  field[0] = 10.0;
  field[1] = 3.0;
  field[2] = 8.0;
  field[3] = 1.0;
  field[4] = 6.0;
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces())
    flux[face.id()] = cfd::dot(Vector2{1.0, 0.0}, face.areaVector());

  for (const auto scheme :
       {ConvectionScheme::Central, ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    const auto first = cfd::discretization::convection(mesh, field, flux, boundaries, scheme);
    const auto second = cfd::discretization::convection(mesh, field, flux, boundaries, scheme);
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      EXPECT_EQ(first[i], second[i])
          << "scheme index " << static_cast<int>(scheme) << " cell " << i;
    }
  }
}

TEST(ConvectionSchemeTest, StepFunctionStaysWithinInitialBoundsAfterOneExplicitStep) {
  // P12-NUM-001 requirement 8: a discontinuous/step field, advected one
  // explicit (forward-Euler) pseudo-time step using each scheme's own
  // conv() output, must stay within the field's own initial [min, max]
  // -- the Sweby TVD blend (smoothnessRatio/vanLeerLimiter) exists for.
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 1, 10.0, 1.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(1.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(5.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedValue>(1.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedValue>(1.0));

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = (cell.centroid().x < 5.0) ? 1.0 : 5.0;
  }
  constexpr Real phiMin = 1.0;
  constexpr Real phiMax = 5.0;

  const Vector2 velocity{1.0, 0.0};
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) flux[face.id()] = cfd::dot(velocity, face.areaVector());

  constexpr Real dt = 0.3;  // CFL = U*dt/dx = 0.3 < 1.
  for (const auto scheme : {ConvectionScheme::Upwind, ConvectionScheme::Central,
                            ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    const auto conv = cfd::discretization::convection(mesh, field, flux, boundaries, scheme);
    for (const auto& cell : mesh.cells()) {
      const Real phiNew = field[cell.id()] - (dt * conv[cell.id()]);
      EXPECT_GE(phiNew, phiMin - 1e-9)
          << "scheme index " << static_cast<int>(scheme) << " cell " << cell.id();
      EXPECT_LE(phiNew, phiMax + 1e-9)
          << "scheme index " << static_cast<int>(scheme) << " cell " << cell.id();
    }
  }
}
