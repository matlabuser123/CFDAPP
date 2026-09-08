#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
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
