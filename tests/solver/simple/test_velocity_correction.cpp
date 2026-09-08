#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::correctVelocity;

namespace {
BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}
}  // namespace

TEST(VelocityCorrectionTest, ConstantPressureCorrectionGivesZeroVelocityCorrection) {
  // TODO.md section 28/55: p'=C -> grad(p')=0 -> velocity correction = 0.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField predictorVelocity(mesh.numberOfCells(), Vector2{2.0, 3.0});
  const ScalarField uResponse(mesh.numberOfCells(), 0.5);
  const ScalarField vResponse(mesh.numberOfCells(), 0.7);
  const ScalarField pPrime(mesh.numberOfCells(), 42.0);

  const VectorField corrected = correctVelocity(mesh, predictorVelocity, uResponse, vResponse,
                                                pPrime, makeZeroGradientPressureBoundaries(mesh));
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(corrected[i].x, 2.0, 1e-9);
    EXPECT_NEAR(corrected[i].y, 3.0, 1e-9);
  }
}

namespace {

bool cellTouchesBoundary(const Mesh& mesh, const cfd::mesh::Cell& cell) {
  for (const Index faceId : cell.faceIds()) {
    if (mesh.face(faceId).isBoundary()) return true;
  }
  return false;
}

}  // namespace

TEST(VelocityCorrectionTest, LinearPressureCorrectionGivesExpectedComponentwiseCorrection) {
  // TODO.md section 58: p'=x -> grad(p')=(1,0), so
  //   u_corrected = u* - d_u * 1
  //   v_corrected = v* - d_v * 0 = v*
  // Checked on interior cells only: correctVelocity's internal gradient
  // uses a zero-gradient boundary condition (matching this file's
  // zero-coupling pressure-correction boundary treatment), which is
  // deliberately inconsistent with a manufactured p'=x field's *true*
  // boundary value -- but that only affects boundary-adjacent cells'
  // own gradient, not interior cells (whose 4 faces are all internal).
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  ScalarField pPrime(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) pPrime[cell.id()] = cell.centroid().x;

  const VectorField predictorVelocity(mesh.numberOfCells(), Vector2{5.0, -1.0});
  const Real dU = 0.3;
  const Real dV = 0.9;
  const ScalarField uResponse(mesh.numberOfCells(), dU);
  const ScalarField vResponse(mesh.numberOfCells(), dV);

  const VectorField corrected = correctVelocity(mesh, predictorVelocity, uResponse, vResponse,
                                                pPrime, makeZeroGradientPressureBoundaries(mesh));
  for (const auto& cell : mesh.cells()) {
    if (cellTouchesBoundary(mesh, cell)) continue;
    EXPECT_NEAR(corrected[cell.id()].x, 5.0 - dU, 1e-9);
    EXPECT_NEAR(corrected[cell.id()].y, -1.0, 1e-9);
  }
}

TEST(VelocityCorrectionTest, DifferentUAndVResponseCoefficientsApplyToTheirOwnComponent) {
  // Sanity: a per-cell-varying d_u/d_v must not get cross-applied to the
  // wrong velocity component. Interior cells only -- see the comment
  // above.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  ScalarField pPrime(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) pPrime[cell.id()] = cell.centroid().y;  // grad = (0,1)

  const VectorField predictorVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField uResponse(mesh.numberOfCells(), 100.0);  // should have NO effect (grad.x=0)
  const ScalarField vResponse(mesh.numberOfCells(), 0.25);

  const VectorField corrected = correctVelocity(mesh, predictorVelocity, uResponse, vResponse,
                                                pPrime, makeZeroGradientPressureBoundaries(mesh));
  for (const auto& cell : mesh.cells()) {
    if (cellTouchesBoundary(mesh, cell)) continue;
    EXPECT_NEAR(corrected[cell.id()].x, 0.0, 1e-9);
    EXPECT_NEAR(corrected[cell.id()].y, -0.25, 1e-9);
  }
}

TEST(VelocityCorrectionTest, MismatchedSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const VectorField predictorVelocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField uResponse(mesh.numberOfCells(), 1.0);
  const ScalarField vResponse(mesh.numberOfCells(), 1.0);
  const ScalarField pPrime(mesh.numberOfCells(), 0.0);

  EXPECT_THROW((void)correctVelocity(mesh, predictorVelocity, uResponse, vResponse, pPrime,
                                     makeZeroGradientPressureBoundaries(mesh)),
               InvalidArgumentError);
}
