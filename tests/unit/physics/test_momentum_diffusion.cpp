#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet makeConstantVelocityBoundaries(const Mesh& mesh, Vector2 velocity) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(velocity));
  }
  return boundaries;
}

bool cellTouchesBoundary(const Mesh& mesh, const cfd::mesh::Cell& cell) {
  for (const Index faceId : cell.faceIds()) {
    if (mesh.face(faceId).isBoundary()) return true;
  }
  return false;
}

}  // namespace

TEST(MomentumDiffusionTest, ConstantVelocityGivesZeroContributionEverywhere) {
  // TODO.md section 29/54: a constant field with a matching Dirichlet
  // boundary has zero gradient everywhere, so the diffusion contribution
  // (A*u - b) must be exactly zero in every cell -- this is the
  // "constant field preservation" identity applied to the assembled
  // system rather than the evaluate-style operator.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 7.0;
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{value, 0.0});
  const Index n = mesh.numberOfCells();

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const VectorField velocity(n, Vector2{value, 0.0});
  assembleDiffusionContribution(mesh, /*dynamicViscosity=*/2.0, velocity, boundaries,
                                VelocityComponent::U, builder, rhs);

  const auto matrix = builder.build();
  const Vector uExact(n, value);
  const Vector residual = matrix.multiply(uExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10);
  }
}

TEST(MomentumDiffusionTest, InteriorCellsMatchKnownQuadraticLaplacian) {
  // TODO.md section 54: u=x^2+y^2 -> Laplacian(u)=4 exactly, so for
  // interior cells (where the boundary-treatment asymmetry documented in
  // Diffusion.cpp does not apply) A*u_exact - b == -mu*4*V_P: this
  // equation's diffusion term enters as -mu*Laplacian(u) (see
  // MomentumEquation.hpp's governing-equation comment).
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0);
  const auto phi = [](const Vector2& p) { return (p.x * p.x) + (p.y * p.y); };
  BoundaryConditionSet boundaries;
  // Approximate per-patch exact value isn't possible with one MovingWall
  // per side for a quadratic field; use the cell-centered field itself
  // for boundary velocity via a per-face patch, matching the
  // discretization tests' ManufacturedFields pattern.
  std::vector<cfd::mesh::Cell> cells = mesh.cells();
  std::vector<cfd::mesh::Face> faces = mesh.faces();
  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }
  const Mesh perFaceMesh(std::move(cells), std::move(faces), std::move(patches));
  for (const auto& patch : perFaceMesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    const Real value = phi(perFaceMesh.face(faceId).centroid());
    boundaries.set(perFaceMesh, patch.name(), std::make_unique<MovingWall>(Vector2{value, 0.0}));
  }

  const Index n = perFaceMesh.numberOfCells();
  VectorField velocity(n);
  for (const auto& cell : perFaceMesh.cells()) {
    velocity[cell.id()] = Vector2{phi(cell.centroid()), 0.0};
  }

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const Real mu = 2.0;
  assembleDiffusionContribution(perFaceMesh, mu, velocity, boundaries, VelocityComponent::U,
                                builder, rhs);
  const auto matrix = builder.build();

  Vector uExact(n);
  for (const auto& cell : perFaceMesh.cells()) uExact[cell.id()] = phi(cell.centroid());
  const Vector residual = matrix.multiply(uExact) - rhs;

  for (const auto& cell : perFaceMesh.cells()) {
    if (cellTouchesBoundary(perFaceMesh, cell)) continue;
    EXPECT_NEAR(residual[cell.id()], -mu * 4.0 * cell.volume(), 1e-9);
  }
}

TEST(MomentumDiffusionTest, InternalFaceCoefficientsAreSymmetric) {
  // Cells 0 (bottom-left) and 1 (its right neighbor) share an internal face on this 3x3 mesh.
  //
  // P12-DIFF-002 A5, entry 13 (validation-migration/acceptance_gate_A5.md, class M-A/2). The
  // "equal/opposite" neighbour-row contribution of TODO.md section 10 is unchanged and is still
  // asserted below. What changed is that A(0,1) is no longer a pure internal coupling: cell 0's
  // xmin wall reaches its far cell THROUGH the face it shares with cell 1, so the one-sided
  // far-cell coefficient of the second-order wall reconstruction lands on that same entry. Cell 1
  // is in the middle column and has no x-normal wall, so A(1,0) stays a pure internal coupling.
  // The asymmetry is deliberate (results/p12-diff-002/architecture.md section 2) and is why these
  // systems are solved with BiCGSTAB rather than CG.
  //
  // Derived independently (a5/tools/derive_expected.py, block D): h = 1/3, |S| = 1/3, mu = 1:
  //   cInt = mu |S| / dPN = 1        cF = mu |S| h1 / (h2 (h2 - h1)) = 1/3
  //   A(1,0) = -cInt = -1            A(0,1) = -(cInt + cF) = -4/3
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const VectorField velocity(n, Vector2{0.0, 0.0});
  assembleDiffusionContribution(mesh, 1.0, velocity, boundaries, VelocityComponent::U, builder,
                                rhs);
  const auto matrix = builder.build();

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  Vector e1(n, 0.0);
  e1[1] = 1.0;
  const Real cInt = 1.0;      // mu |S| / dPN = 1 * (1/3) / (1/3)
  const Real cF = 1.0 / 3.0;  // mu |S| h1 / (h2 (h2 - h1)), h1 = 1/6, h2 = 1/2, |S| = 1/3

  const Real a10 = matrix.multiply(e0)[1];  // A(1,0) -- pure internal coupling
  const Real a01 = matrix.multiply(e1)[0];  // A(0,1) -- internal coupling + cell 0's far-cell term
  EXPECT_NEAR(a10, -cInt, 1e-12);
  EXPECT_NEAR(a01, -(cInt + cF), 1e-12);
  // The one-sided far-cell coefficient is the ONLY thing that breaks entry-level symmetry.
  EXPECT_NEAR(a01 - a10, -cF, 1e-12);
  EXPECT_LT(a01, 0.0);  // off-diagonal diffusion coefficients are negative
  EXPECT_LT(a10, 0.0);
  EXPECT_TRUE(matrix.allFinite());

  // The equal/opposite property itself, where no far-cell term can reach: two INTERIOR cells of a
  // 5x5 mesh (cells 6 = (1,1) and 7 = (2,1) have no boundary face, so neither row receives a
  // far-cell entry) must couple exactly symmetrically.
  const Mesh wide = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const auto wideBcs = makeConstantVelocityBoundaries(wide, Vector2{0.0, 0.0});
  const Index m = wide.numberOfCells();
  SparseMatrixBuilder wideBuilder(m, m);
  Vector wideRhs(m, 0.0);
  const VectorField wideVelocity(m, Vector2{0.0, 0.0});
  assembleDiffusionContribution(wide, 1.0, wideVelocity, wideBcs, VelocityComponent::U, wideBuilder,
                                wideRhs);
  const auto wideMatrix = wideBuilder.build();
  Vector e6(m, 0.0);
  e6[6] = 1.0;
  Vector e7(m, 0.0);
  e7[7] = 1.0;
  EXPECT_DOUBLE_EQ(wideMatrix.multiply(e6)[7], wideMatrix.multiply(e7)[6]);
  EXPECT_LT(wideMatrix.multiply(e6)[7], 0.0);
}

TEST(MomentumDiffusionTest, DiagonalIsFinitePositiveAndNonzero) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const VectorField velocity(n, Vector2{0.0, 0.0});
  assembleDiffusionContribution(mesh, 1.5, velocity, boundaries, VelocityComponent::U, builder,
                                rhs);
  const auto matrix = builder.build();

  for (Index row = 0; row < n; ++row) {
    const Real aP = matrix.diagonal(row);
    EXPECT_TRUE(std::isfinite(aP));
    EXPECT_GT(aP, 0.0);
  }
}

TEST(MomentumDiffusionTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(assembleDiffusionContribution(mesh, 1.0, velocity, boundaries, VelocityComponent::U,
                                             builder, rhs),
               InvalidArgumentError);
}

// --- P2-TURB-003: field-based (per-cell effective viscosity) overload ------

TEST(MomentumDiffusionTest, UniformEffectiveViscosityFieldMatchesConstantOverload) {
  // The "reduces to the old laminar result" proof required by P2-TURB-003:
  // a uniform effectiveViscosity field equal to a constant mu must give
  // the same assembled system as the plain scalar overload, to within
  // floating-point associativity noise -- not necessarily bit-identical,
  // because the field overload's internal faces go through
  // cfd::discretization::interpolateInternalFace's
  // ((dNf*phiP + dPf*phiN) / (dPf+dNf)) formula, which is not guaranteed
  // to collapse to exactly `mu` bit-for-bit when phiP == phiN == mu (two
  // separate multiplications can each round differently than one), unlike
  // the scalar overload's direct `mu * area / distance`. TODO.md P2-TURB-003
  // accepts "bit-identical or strict numerical equality" for exactly this
  // reason -- checked here to 1e-12 relative, many orders tighter than any
  // physical tolerance elsewhere in this codebase.
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 1.0, 1.0);
  const Real mu = 0.0173;
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{3.0, -2.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.5});

  SparseMatrixBuilder scalarBuilder(n, n);
  Vector scalarRhs(n, 0.0);
  assembleDiffusionContribution(mesh, mu, velocity, boundaries, VelocityComponent::U, scalarBuilder,
                                scalarRhs);
  const auto scalarMatrix = scalarBuilder.build();

  const ScalarField uniformMu(n, mu);
  SparseMatrixBuilder fieldBuilder(n, n);
  Vector fieldRhs(n, 0.0);
  assembleDiffusionContribution(mesh, uniformMu, velocity, boundaries, VelocityComponent::U,
                                fieldBuilder, fieldRhs);
  const auto fieldMatrix = fieldBuilder.build();

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(fieldMatrix.diagonal(row), scalarMatrix.diagonal(row), 1e-12 * mu) << "row " << row;
    EXPECT_NEAR(fieldRhs[row], scalarRhs[row],
                1e-12 * std::max<Real>(1.0, std::abs(scalarRhs[row])))
        << "row " << row;
  }
  // Off-diagonal entries too, probed via matrix-vector products the same
  // way InternalFaceCoefficientsAreSymmetric above does.
  for (Index i = 0; i < n; ++i) {
    Vector e(n, 0.0);
    e[i] = 1.0;
    const Vector scalarColumn = scalarMatrix.multiply(e);
    const Vector fieldColumn = fieldMatrix.multiply(e);
    for (Index row = 0; row < n; ++row) {
      EXPECT_NEAR(fieldColumn[row], scalarColumn[row], 1e-12 * mu) << "col " << i << " row " << row;
    }
  }
}

TEST(MomentumDiffusionTest, NonUniformEffectiveViscosityChangesDiffusionCoefficients) {
  // The "critical" proof (TODO.md P2-TURB-003) that mu_eff genuinely
  // drives the assembled diffusion coefficients rather than being
  // silently ignored: giving cell 0 a much larger effective viscosity
  // than the rest of a uniform field must strictly increase the
  // face-interpolated diffusion coefficient on every face touching cell
  // 0, relative to the all-uniform baseline, and must leave faces that do
  // not touch cell 0 completely unchanged (mu_eff is a genuinely local,
  // per-cell quantity, not some global scalar in disguise).
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Real muBase = 0.01;
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});

  ScalarField uniformMu(n, muBase);
  SparseMatrixBuilder baselineBuilder(n, n);
  Vector baselineRhs(n, 0.0);
  assembleDiffusionContribution(mesh, uniformMu, velocity, boundaries, VelocityComponent::U,
                                baselineBuilder, baselineRhs);
  const auto baselineMatrix = baselineBuilder.build();

  ScalarField perturbedMu(n, muBase);
  perturbedMu[0] = muBase * 50.0;  // cell 0 only -- a strong, unmistakable perturbation.
  SparseMatrixBuilder perturbedBuilder(n, n);
  Vector perturbedRhs(n, 0.0);
  assembleDiffusionContribution(mesh, perturbedMu, velocity, boundaries, VelocityComponent::U,
                                perturbedBuilder, perturbedRhs);
  const auto perturbedMatrix = perturbedBuilder.build();

  // Cell 0's own diagonal (sum of face conductances touching cell 0) must
  // have strictly increased.
  EXPECT_GT(perturbedMatrix.diagonal(0), baselineMatrix.diagonal(0));

  // Cell 0's off-diagonal coupling to its mesh neighbors (cells 1 and 3 on
  // this 3x3 Cartesian mesh: right and above) must have strictly
  // increased in magnitude too.
  Vector e0(n, 0.0);
  e0[0] = 1.0;
  const Vector baselineColumn0 = baselineMatrix.multiply(e0);
  const Vector perturbedColumn0 = perturbedMatrix.multiply(e0);
  EXPECT_LT(perturbedColumn0[1], baselineColumn0[1]);  // more negative -- see A(1,0) < 0.
  EXPECT_LT(perturbedColumn0[3], baselineColumn0[3]);

  // Cell 8 (the mesh's opposite corner) never shares a face with cell 0,
  // so its diagonal must be completely unaffected -- proves mu_eff is
  // applied per-cell/per-face, not folded into some global scalar.
  EXPECT_DOUBLE_EQ(perturbedMatrix.diagonal(8), baselineMatrix.diagonal(8));
}

TEST(MomentumDiffusionTest, MismatchedEffectiveViscosityFieldSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});
  const ScalarField wrongSizeMu(n + 1, 0.01);
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  EXPECT_THROW((void)assembleDiffusionContribution(mesh, wrongSizeMu, velocity, boundaries,
                                                   VelocityComponent::U, builder, rhs),
               InvalidArgumentError);
}

TEST(MomentumDiffusionTest, NonFiniteOrNonPositiveEffectiveViscosityFieldThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  ScalarField nanMu(n, 0.01);
  nanMu[1] = std::nan("");
  EXPECT_THROW((void)assembleDiffusionContribution(mesh, nanMu, velocity, boundaries,
                                                   VelocityComponent::U, builder, rhs),
               InvalidArgumentError);

  ScalarField infMu(n, 0.01);
  infMu[2] = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)assembleDiffusionContribution(mesh, infMu, velocity, boundaries,
                                                   VelocityComponent::U, builder, rhs),
               InvalidArgumentError);

  ScalarField zeroMu(n, 0.01);
  zeroMu[3] = 0.0;
  EXPECT_THROW((void)assembleDiffusionContribution(mesh, zeroMu, velocity, boundaries,
                                                   VelocityComponent::U, builder, rhs),
               InvalidArgumentError);

  ScalarField negativeMu(n, 0.01);
  negativeMu[0] = -0.001;
  EXPECT_THROW((void)assembleDiffusionContribution(mesh, negativeMu, velocity, boundaries,
                                                   VelocityComponent::U, builder, rhs),
               InvalidArgumentError);
}
