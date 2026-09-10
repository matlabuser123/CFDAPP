// P2-TURB-004 sections 5, 6, 30, 35: cfd::turbulence::
// computeTurbulentProduction -- P_k = mu_t*(2*(du/dx)^2 + 2*(dv/dy)^2 +
// (du/dy+dv/dx)^2), including the mandatory hand-derived shear test.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::discretization::computeVelocityGradient;
using cfd::discretization::VelocityGradientField;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::computeTurbulentProduction;

namespace {

Mesh makePerFaceMesh(const Mesh& source) {
  std::vector<cfd::mesh::Cell> cells = source.cells();
  std::vector<cfd::mesh::Face> faces = source.faces();
  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }
  return Mesh(std::move(cells), std::move(faces), std::move(patches));
}

}  // namespace

TEST(TurbulenceProductionTest, HandDerivedShearProductionMatchesExactly) {
  // P2-TURB-004 section 6 (mandatory): u=3y, v=0, mu_t=2 -> du/dy=3,
  // every other gradient component 0, so
  //   P_k = mu_t*(2*0^2 + 2*0^2 + (3+0)^2) = 2*9 = 18
  // exactly (see VectorGradientTest.LinearShearFieldIsExactEverywhere for
  // why the gradient itself is exact at every cell here).
  const Mesh base = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh mesh = makePerFaceMesh(base);
  const Real a = 3.0;
  const auto u = [a](const Vector2& p) { return Vector2{a * p.y, 0.0}; };

  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    boundaries.set(mesh, patch.name(),
                   std::make_unique<MovingWall>(u(mesh.face(faceId).centroid())));
  }
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) velocity[cell.id()] = u(cell.centroid());

  const VelocityGradientField gradient = computeVelocityGradient(mesh, velocity, boundaries);
  const ScalarField muT(mesh.numberOfCells(), 2.0);
  const ScalarField production = computeTurbulentProduction(mesh, muT, gradient);

  ASSERT_EQ(production.size(), mesh.numberOfCells());
  for (Index i = 0; i < production.size(); ++i) {
    EXPECT_NEAR(production[i], 18.0, 1e-9) << "cell " << i;
  }
}

TEST(TurbulenceProductionTest, ZeroShearGivesZeroProductionEverywhere) {
  // Section 35 (mandatory): uniform velocity -> grad(U)=0 -> P_k=0
  // exactly, regardless of mu_t.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  const Vector2 uniform{5.0, -3.0};
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(uniform));
  }
  const VectorField velocity(mesh.numberOfCells(), uniform);
  const VelocityGradientField gradient = computeVelocityGradient(mesh, velocity, boundaries);
  const ScalarField muT(mesh.numberOfCells(),
                        7.0);  // nonzero mu_t -- would hide a P_k bug if ignored.

  const ScalarField production = computeTurbulentProduction(mesh, muT, gradient);
  for (Index i = 0; i < production.size(); ++i) {
    EXPECT_NEAR(production[i], 0.0, 1e-12) << "cell " << i;
  }
}

TEST(TurbulenceProductionTest, NormalStrainContributesWithFactorTwo) {
  // Pure normal strain, no shear: u = a*x, v = -a*y (a divergence-free,
  // exactly-representable-by-the-plain-gradient field) ->
  // du/dx=a, dv/dy=-a, du/dy=dv/dx=0, so
  //   P_k = mu_t*(2*a^2 + 2*a^2 + 0) = 4*mu_t*a^2
  // -- exercises the "2*" coefficients on the normal-strain terms
  // specifically (distinct from the shear test above, which exercises
  // only the cross term).
  const Mesh base = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh mesh = makePerFaceMesh(base);
  const Real a = 2.0;
  const auto u = [a](const Vector2& p) { return Vector2{a * p.x, -a * p.y}; };

  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    boundaries.set(mesh, patch.name(),
                   std::make_unique<MovingWall>(u(mesh.face(faceId).centroid())));
  }
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) velocity[cell.id()] = u(cell.centroid());

  const VelocityGradientField gradient = computeVelocityGradient(mesh, velocity, boundaries);
  const Real muT = 1.5;
  const ScalarField production =
      computeTurbulentProduction(mesh, ScalarField(mesh.numberOfCells(), muT), gradient);

  const Real expected = 4.0 * muT * a * a;
  for (Index i = 0; i < production.size(); ++i) {
    EXPECT_NEAR(production[i], expected, 1e-9) << "cell " << i;
  }
}

TEST(TurbulenceProductionTest, MismatchedFieldSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const VelocityGradientField gradient{VectorField(n, Vector2{0.0, 0.0}),
                                       VectorField(n, Vector2{0.0, 0.0})};
  const ScalarField wrongSizeMuT(n + 1, 1.0);

  EXPECT_THROW((void)computeTurbulentProduction(mesh, wrongSizeMuT, gradient),
               InvalidArgumentError);
}
