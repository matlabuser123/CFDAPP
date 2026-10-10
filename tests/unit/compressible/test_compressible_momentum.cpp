// P3-PHYS-006 sections 13-14, 35: compressible momentum transient-
// storage term (hand-derived, and the mandatory constant-density-
// reduces-to-the-existing-formulation regression) plus the full
// component assembly.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/CompressibleMomentum.hpp"
#include "cfd/compressible/CompressibleRelaxedMomentum.hpp"
#include "cfd/compressible/DilatationalStress.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::compressible::assembleCompressibleMomentumComponent;
using cfd::compressible::compressibleMomentumTimeDerivative;
using cfd::compressible::CompressibleTimeDerivativeCoefficients;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::assembleTransientMomentumComponent;

namespace {

BoundaryConditionSet makeConstantVelocityBoundaries(const Mesh& mesh, Vector2 velocity) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(velocity));
  }
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

ScalarField selectComponent(const VectorField& velocity, VelocityComponent component) {
  ScalarField result(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    result[i] = (component == VelocityComponent::U) ? velocity[i].x : velocity[i].y;
  }
  return result;
}

}  // namespace

// --- compressibleMomentumTimeDerivative -----------------------------------

TEST(CompressibleMomentumTimeDerivativeTest, HandDerivedDiagonalAndSource) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cell volume = 1 each.
  const Index n = mesh.numberOfCells();
  ScalarField velocityOld(n);
  velocityOld[0] = 2.0;
  velocityOld[1] = -1.0;
  ScalarField densityOld(n);
  densityOld[0] = 1.0;
  densityOld[1] = 1.2;
  ScalarField densityNew(n);
  densityNew[0] = 1.1;
  densityNew[1] = 1.3;
  const Real dt = 0.5;

  const CompressibleTimeDerivativeCoefficients result =
      compressibleMomentumTimeDerivative(mesh, velocityOld, densityOld, densityNew, dt);

  // diagonal = rho_new*V/dt.
  EXPECT_NEAR(result.diagonal[0], 1.1 * 1.0 / 0.5, 1e-12);
  EXPECT_NEAR(result.diagonal[1], 1.3 * 1.0 / 0.5, 1e-12);
  // source = rho_old*V/dt*u_old.
  EXPECT_NEAR(result.source[0], 1.0 * 1.0 / 0.5 * 2.0, 1e-12);
  EXPECT_NEAR(result.source[1], 1.2 * 1.0 / 0.5 * (-1.0), 1e-12);
}

TEST(CompressibleMomentumTimeDerivativeTest, ConstantDensityMatchesIncompressibleFormula) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  ScalarField velocityOld(n, 2.5);
  const Real rho = 1.2;
  const ScalarField densityOld(n, rho);
  const ScalarField densityNew(n, rho);
  const Real dt = 0.02;

  const CompressibleTimeDerivativeCoefficients result =
      compressibleMomentumTimeDerivative(mesh, velocityOld, densityOld, densityNew, dt);

  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Real expectedDiagonal = rho * cell.volume() / dt;
    const Real expectedSource = expectedDiagonal * velocityOld[id];
    EXPECT_NEAR(result.diagonal[id], expectedDiagonal, 1e-9) << "cell " << id;
    EXPECT_NEAR(result.source[id], expectedSource, 1e-9) << "cell " << id;
  }
}

TEST(CompressibleMomentumTimeDerivativeTest, MismatchedSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField velocityOld(mesh.numberOfCells() + 1, 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  EXPECT_THROW((void)compressibleMomentumTimeDerivative(mesh, velocityOld, density, density, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleMomentumTimeDerivativeTest, RejectsNonPositiveDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField velocityOld(mesh.numberOfCells(), 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  EXPECT_THROW((void)compressibleMomentumTimeDerivative(mesh, velocityOld, density, density, 0.0),
               InvalidArgumentError);
}

// --- assembleCompressibleMomentumComponent (section 35) --------------------

TEST(CompressibleMomentumComponentTest, ConstantDensityReproducesTransientMomentumExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.5, -0.2});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  VectorField velocity(n, Vector2{0.3, 0.1});
  ScalarField pressure(n, 0.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.15;
  }
  const FluidProperties fluid(1.3, 0.02);
  const Real dt = 0.05;
  const VelocityComponent component = VelocityComponent::U;

  const ScalarField previousU = selectComponent(velocity, component);
  const MomentumAssembly reference = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, component,
      previousU, dt);

  const ScalarField densityField(n, fluid.density());
  const MomentumAssembly compressible = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, densityField, densityField,
      fluid.dynamicViscosity(), velocityBoundaries, pressureBoundaries, component, dt);

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(compressible.diagonal[row], reference.diagonal[row], 1e-9) << "row " << row;
    EXPECT_NEAR(compressible.system.rhs()[row], reference.system.rhs()[row], 1e-9) << "row " << row;
  }
}

TEST(CompressibleMomentumComponentTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);

  EXPECT_THROW((void)assembleCompressibleMomentumComponent(
                   mesh, velocity, previousU, pressure, massFlux, density, density, 0.01,
                   velocityBoundaries, pressureBoundaries, VelocityComponent::U, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleMomentumComponentTest, NonUniformDensityChangesTheAssembledDiagonal) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto velocityBoundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});
  const ScalarField previousU(n, 0.0);
  const ScalarField pressure(n, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const Real dt = 0.01;

  const ScalarField uniformDensity(n, 1.0);
  const MomentumAssembly baseline = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, uniformDensity, uniformDensity, 0.02,
      velocityBoundaries, pressureBoundaries, VelocityComponent::U, dt);

  ScalarField perturbedDensity(n, 1.0);
  perturbedDensity[0] = 5.0;  // cell 0's rho_new only.
  const MomentumAssembly perturbed = assembleCompressibleMomentumComponent(
      mesh, velocity, previousU, pressure, massFlux, uniformDensity, perturbedDensity, 0.02,
      velocityBoundaries, pressureBoundaries, VelocityComponent::U, dt);

  EXPECT_GT(perturbed.diagonal[0], baseline.diagonal[0]);
  // A cell sharing no face with cell 0 and with unchanged density must
  // have an unchanged diagonal.
  EXPECT_DOUBLE_EQ(perturbed.diagonal[8], baseline.diagonal[8]);
}

// ---------------------------------------------------------------------------
// P17.4 -- the dilatational part of the compressible viscous stress, plus the term-by-term
// verification of the four contributions that were already correct.
//
// Measured on the pre-P17.4 tree (results/p17/compressible_momentum/logs/00_dryrun.log):
//   grad(div / dilatation / bulkViscosity / twoThirds / Stokes / divU   ALL 0 hits
//   pressure force, linear p: interior worst relative error 0           already exact
//   transient: diagonal 8 = rho_new V/dt, source 12 = rho_old V/dt u_old
//              the WRONG rho dU/dt form would give 24 -- a factor of 2 apart
//   body force: rhs -9.81 / -19.62 / -39.24 for rho 1 / 2 / 4 -- exactly linear
//   the MISSING term for U = (x^2/2,0): -(2/3) mu * 1 * V = -0.0666666666666667 per cell
// ---------------------------------------------------------------------------

namespace p17_4 {

// Vector-valued zero-gradient on every patch. Outlet IS the zero-gradient vector BC;
// FixedGradient is scalar-valued and divergence() would reject it.
BoundaryConditionSet zeroGradientVelocity(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Outlet>());
  }
  return boundaries;
}

BoundaryConditionSet zeroGradientScalar(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// True when the cell is at least two cells from any boundary. grad(div U) is exact only
// there: div U itself is exact everywhere, but the artificial zero-gradient BC on the
// derived scalar makes it wrong in the boundary-adjacent layer, and a Green-Gauss gradient
// of a wrong neighbour pollutes one cell further in. Measured in the dry-run: deep-interior
// cells give grad(div U).x == 1 exactly, while the worst error over all nominally interior
// cells of an 8x4 mesh is 2. See DilatationalStress.hpp.
bool isDeepInterior(const Mesh& mesh, Index cellId, Real lx, Real ly, Real h) {
  const auto& c = mesh.cell(cellId);
  return c.centroid().x > 2.0 * h && c.centroid().x < lx - 2.0 * h && c.centroid().y > 2.0 * h &&
         c.centroid().y < ly - 2.0 * h;
}

}  // namespace p17_4

TEST(DilatationalStressTest, MatchesMinusTwoThirdsMuGradDivergenceTimesVolume) {
  // U = (x^2/2, 0)  =>  div U = x  =>  grad(div U) = (1, 0) analytically, so the
  // contribution is -(2/3) mu * 1 * V in the x component, checked in the DEEP interior.
  // 10x10 so that deep-interior cells EXIST. An 8x4 mesh has h = 1 and needs y > 2 AND
  // y < 4 - 2 = 2 simultaneously, which is impossible -- attempt 1 of this gate failed on
  // exactly that, caught by the ASSERT_GT(checked, 0) guard below. See
  // results/p17/compressible_momentum/logs/PRESERVED_vacuous_deep_interior/WHY_INVALID.md
  // 20.0 x 20.0 on a 10x10 mesh => h = 2 and cell volume = 4, NOT 1. A unit volume makes
  // the V_P factor undetectable, which is what made control N2 vacuous. div U = x is linear
  // and Green-Gauss is exact for a linear field, so div U and grad(div U) = 1 stay exact in
  // the deep interior at the coarser spacing. See AMENDMENT_N2_N6_vacuous.md §1.
  const Real lx = 20.0;
  const Real ly = 20.0;
  const Index nx = 10;
  const Index ny = 10;
  const Real h = lx / static_cast<Real>(nx);
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
  const Index n = mesh.numberOfCells();
  const Real mu = 0.1;

  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    velocity[cell.id()] = Vector2{0.5 * x * x, 0.0};
  }
  const auto vbc = p17_4::zeroGradientVelocity(mesh);

  cfd::algebra::Vector rhs(n, 0.0);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, mu,
                                                            VelocityComponent::U, rhs);

  Index checked = 0;
  for (const auto& cell : mesh.cells()) {
    if (!p17_4::isDeepInterior(mesh, cell.id(), lx, ly, h)) continue;
    ++checked;
    const Real expected = -(2.0 / 3.0) * mu * 1.0 * cell.volume();
    EXPECT_NEAR(rhs[cell.id()], expected, 1e-12 * std::abs(expected)) << "cell " << cell.id();
  }
  ASSERT_GT(checked, 0) << "no deep-interior cells -- the mesh is too small to verify anything";
}

TEST(DilatationalStressTest, IsExactlyZeroForADivergenceFreeField) {
  // The safety property: for div U = 0 the term must contribute NOTHING, so enabling it
  // cannot disturb an incompressible or uniform-flow result. A solid-body rotation
  // U = (-y, x) is divergence-free exactly.
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 6.0, 6.0);
  const Index n = mesh.numberOfCells();
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector2{-cell.centroid().y, cell.centroid().x};
  }
  const auto vbc = p17_4::zeroGradientVelocity(mesh);

  for (const auto component : {VelocityComponent::U, VelocityComponent::V}) {
    cfd::algebra::Vector rhs(n, 0.0);
    cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.7, component,
                                                              rhs);
    for (Index i = 0; i < n; ++i) {
      EXPECT_EQ(rhs[i], 0.0) << "cell " << i;
    }
  }
}

TEST(DilatationalStressTest, UniformVelocityContributesNothing) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 4.0, 4.0);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{2.5, -1.25});
  const auto vbc = p17_4::zeroGradientVelocity(mesh);
  cfd::algebra::Vector rhs(n, 0.0);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.3,
                                                            VelocityComponent::U, rhs);
  for (Index i = 0; i < n; ++i) EXPECT_EQ(rhs[i], 0.0) << "cell " << i;
}

TEST(DilatationalStressTest, ScalesLinearlyWithViscosityAndVanishesAtZeroViscosity) {
  const Real lx = 10.0;
  const Real ly = 10.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 10, lx, ly);
  const Index n = mesh.numberOfCells();
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    velocity[cell.id()] = Vector2{0.5 * x * x, 0.0};
  }
  const auto vbc = p17_4::zeroGradientVelocity(mesh);

  cfd::algebra::Vector rhsOne(n, 0.0);
  cfd::algebra::Vector rhsTwo(n, 0.0);
  cfd::algebra::Vector rhsZero(n, 0.0);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.1,
                                                            VelocityComponent::U, rhsOne);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.2,
                                                            VelocityComponent::U, rhsTwo);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.0,
                                                            VelocityComponent::U, rhsZero);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(rhsTwo[i], 2.0 * rhsOne[i], 1e-12 * std::abs(2.0 * rhsOne[i]) + 1e-300)
        << "cell " << i;
    EXPECT_EQ(rhsZero[i], 0.0) << "cell " << i << ": mu = 0 must contribute nothing";
  }
}

TEST(DilatationalStressTest, RejectsInvalidInputs) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 3.0, 2.0);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  const auto vbc = p17_4::zeroGradientVelocity(mesh);

  cfd::algebra::Vector rhs(n, 0.0);
  const Real qnan = std::numeric_limits<Real>::quiet_NaN();
  const Real pinf = std::numeric_limits<Real>::infinity();
  for (const Real badMu : {qnan, pinf, -1.0}) {
    cfd::algebra::Vector r(n, 0.0);
    EXPECT_THROW(cfd::compressible::assembleDilatationalStressContribution(
                     mesh, velocity, vbc, badMu, VelocityComponent::U, r),
                 InvalidArgumentError)
        << "mu = " << badMu;
  }
  // mu == 0 is admissible
  EXPECT_NO_THROW(cfd::compressible::assembleDilatationalStressContribution(
      mesh, velocity, vbc, 0.0, VelocityComponent::U, rhs));

  const VectorField wrongSize(n + 1, Vector2{1.0, 0.0});
  EXPECT_THROW(cfd::compressible::assembleDilatationalStressContribution(mesh, wrongSize, vbc, 0.1,
                                                                         VelocityComponent::U, rhs),
               InvalidArgumentError);
  cfd::algebra::Vector wrongRhs(n + 1, 0.0);
  EXPECT_THROW(cfd::compressible::assembleDilatationalStressContribution(
                   mesh, velocity, vbc, 0.1, VelocityComponent::U, wrongRhs),
               InvalidArgumentError);
}

TEST(DilatationalStressTest, RepeatedAssemblyIsBitwiseIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 4, 6.0, 4.0);
  const Index n = mesh.numberOfCells();
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    const Real y = cell.centroid().y;
    velocity[cell.id()] = Vector2{0.5 * x * x, 0.25 * y * y};
  }
  const auto vbc = p17_4::zeroGradientVelocity(mesh);

  cfd::algebra::Vector reference(n, 0.0);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.13,
                                                            VelocityComponent::U, reference);
  for (int repeat = 0; repeat < 8; ++repeat) {
    cfd::algebra::Vector again(n, 0.0);
    cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, 0.13,
                                                              VelocityComponent::U, again);
    for (Index i = 0; i < n; ++i) {
      EXPECT_EQ(again[i], reference[i]) << "repeat " << repeat << " cell " << i;
    }
  }
}

// ---------------------------------------------------------------------------
// P17.4 -- term-by-term verification of the four contributions already correct.
// ---------------------------------------------------------------------------

TEST(CompressibleMomentumTermsTest, PressureForceMatchesMinusVolumeTimesAnalyticalGradient) {
  // Linear p => grad(p) constant => rhs = -V * dp/dx, exactly, at deep-interior cells.
  // 10x10 for the same deep-interior reason as the dilatational test above.
  const Real lx = 10.0;
  const Real ly = 10.0;
  const Real h = 1.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 10, lx, ly);
  const Index n = mesh.numberOfCells();
  const Real dpdx = 7.0;
  ScalarField pressure(n);
  for (const auto& cell : mesh.cells()) {
    pressure[cell.id()] = 100.0 + dpdx * cell.centroid().x;
  }
  const auto pbc = p17_4::zeroGradientScalar(mesh);

  cfd::algebra::Vector rhs(n, 0.0);
  cfd::physics::assemblePressureSourceContribution(mesh, pressure, pbc, VelocityComponent::U, rhs,
                                                   cfd::discretization::GradientScheme::GreenGauss);

  Index checked = 0;
  for (const auto& cell : mesh.cells()) {
    if (!p17_4::isDeepInterior(mesh, cell.id(), lx, ly, h)) continue;
    ++checked;
    const Real expected = -cell.volume() * dpdx;
    EXPECT_NEAR(rhs[cell.id()], expected, 1e-12 * std::abs(expected)) << "cell " << cell.id();
  }
  ASSERT_GT(checked, 0);
}

TEST(CompressibleMomentumTermsTest, BodyForceScalesLinearlyWithDensity) {
  // The API takes a force per unit VOLUME, so supplying rho*f gives the correct rho f
  // weighting. Verified by scaling rho and requiring exact linearity.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 3.0, 2.0);
  const Index n = mesh.numberOfCells();
  const Real g = -9.81;

  cfd::algebra::Vector rhsOne(n, 0.0);
  cfd::algebra::Vector rhsFour(n, 0.0);
  VectorField srcOne(n);
  VectorField srcFour(n);
  for (Index i = 0; i < n; ++i) {
    srcOne[i] = Vector2{0.0, 1.0 * g};
    srcFour[i] = Vector2{0.0, 4.0 * g};
  }
  cfd::physics::assembleMomentumSourceContribution(mesh, srcOne, VelocityComponent::V, rhsOne);
  cfd::physics::assembleMomentumSourceContribution(mesh, srcFour, VelocityComponent::V, rhsFour);

  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    EXPECT_NEAR(rhsOne[i], cell.volume() * 1.0 * g, 1e-12 * std::abs(cell.volume() * g));
    EXPECT_NEAR(rhsFour[i], 4.0 * rhsOne[i], 1e-12 * std::abs(4.0 * rhsOne[i]));
  }
}

TEST(CompressibleMomentumTermsTest, TransientTermDistinguishesDRhoUDtFromRhoDUDt) {
  // The distinction that matters for variable-density flow. With rho DOUBLING and u_old
  // non-zero the two forms differ by a factor of two in the source, so this test would fail
  // against the incorrect rho_new (u_new - u_old) form.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const Index n = mesh.numberOfCells();
  const Real dt = 0.25;
  const Real rhoOldValue = 1.0;
  const Real rhoNewValue = 2.0;
  const Real uOldValue = 3.0;
  const ScalarField rhoOld(n, rhoOldValue);
  const ScalarField rhoNew(n, rhoNewValue);
  const ScalarField uOld(n, uOldValue);

  const CompressibleTimeDerivativeCoefficients coeffs =
      compressibleMomentumTimeDerivative(mesh, uOld, rhoOld, rhoNew, dt);

  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    const Real V = cell.volume();
    // the CORRECT conservative form
    EXPECT_NEAR(coeffs.diagonal[i], rhoNewValue * V / dt, 1e-12 * rhoNewValue * V / dt);
    EXPECT_NEAR(coeffs.source[i], rhoOldValue * V / dt * uOldValue,
                1e-12 * rhoOldValue * V / dt * uOldValue);
    // and it is NOT the incorrect rho_new-weighted form
    const Real wrongSource = rhoNewValue * V / dt * uOldValue;
    EXPECT_GT(std::abs(coeffs.source[i] - wrongSource), 1.0)
        << "cell " << i << ": the test cannot distinguish the two forms";
  }
}

// ---------------------------------------------------------------------------
// P17.4 gate criterion D3 -- the WIRED path.
//
// This test exists because control N6 (defaulting the option to enabled) disturbed
// NOTHING: every other dilatational test calls assembleDilatationalStressContribution
// DIRECTLY, so no test would have failed if the wiring through
// assembleRelaxedCompressibleMomentumComponent were wrong, absent or inverted. A wiring no
// test can detect is unverified wiring. See AMENDMENT_N2_N6_vacuous.md §2.
// ---------------------------------------------------------------------------
TEST(DilatationalStressTest, IsWiredIntoTheRelaxedMomentumAssembly) {
  // 10x10 over 20.0x20.0 so cell volume is 4, not 1 -- a unit volume would make the V_P
  // factor undetectable, which is what made control N2 vacuous.
  const Real lx = 20.0;
  const Real ly = 20.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 10, lx, ly);
  const Index n = mesh.numberOfCells();
  const Real mu = 0.1;
  const Real alpha = 1.0;  // no relaxation, so the comparison is of raw assemblies
  const Real pseudoTimeStep = 1.0;

  // div U = x != 0, so the term is genuinely non-zero.
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    velocity[cell.id()] = Vector2{0.5 * x * x, 0.0};
  }
  const ScalarField pressure(n, 1000.0);
  const ScalarField density(n, 1.3);
  const ScalarField previousComponent(n, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const auto vbc = p17_4::zeroGradientVelocity(mesh);
  const auto pbc = p17_4::zeroGradientScalar(mesh);

  const auto assemble = [&](bool includeDilatational) {
    return cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
        mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
        previousComponent, alpha, pseudoTimeStep, {},
        cfd::discretization::GradientScheme::GreenGauss, nullptr, includeDilatational);
  };

  const MomentumAssembly off = assemble(false);
  const MomentumAssembly on = assemble(true);

  // (1) the wiring is LIVE: the RHS must differ.
  Index differing = 0;
  for (Index i = 0; i < n; ++i) {
    if (on.system.rhs()[i] != off.system.rhs()[i]) ++differing;
  }
  EXPECT_GT(differing, 0) << "enabling the option changed nothing -- the wiring is dead";

  // (2) the wiring is CORRECT, not merely present: the difference must equal the
  // standalone operator's contribution, cell by cell.
  cfd::algebra::Vector standalone(n, 0.0);
  cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, mu,
                                                            VelocityComponent::U, standalone);
  for (Index i = 0; i < n; ++i) {
    const Real wired = on.system.rhs()[i] - off.system.rhs()[i];
    EXPECT_NEAR(wired, standalone[i], 1e-10 * (std::abs(standalone[i]) + 1e-30))
        << "cell " << i << ": the wired contribution does not match the operator";
  }

  // (3) it cannot disturb a divergence-free field: off and on must be BITWISE identical.
  VectorField solenoidal(n);
  for (const auto& cell : mesh.cells()) {
    solenoidal[cell.id()] = Vector2{-cell.centroid().y, cell.centroid().x};
  }
  const auto assembleSolenoidal = [&](bool includeDilatational) {
    return cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
        mesh, solenoidal, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
        previousComponent, alpha, pseudoTimeStep, {},
        cfd::discretization::GradientScheme::GreenGauss, nullptr, includeDilatational);
  };
  const MomentumAssembly solOff = assembleSolenoidal(false);
  const MomentumAssembly solOn = assembleSolenoidal(true);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(solOn.system.rhs()[i], solOff.system.rhs()[i])
        << "cell " << i << ": a divergence-free field must be untouched";
  }
}

// ---------------------------------------------------------------------------
// P17.4 gate criterion D1, measured as BEHAVIOUR rather than as source text.
//
// D1 was originally probed with `grep -c 'bool includeDilatationalStress = false'`, which is
// a string in a header, not a property of the assembly. Control N6 -- flipping that default
// to true -- disturbed NOTHING, because every other test passes the flag EXPLICITLY, so no
// test could observe the default at all. This test observes it: it calls the assembly with
// the argument OMITTED and pins the result to the disabled branch.
//
// The third assertion is what keeps the first from being vacuous: if enabling the term made
// no difference on this mesh and this field, `defaulted == off` would hold no matter what the
// default was. See AMENDMENT_N2_N6_vacuous.md §2.
// ---------------------------------------------------------------------------
TEST(DilatationalStressTest, DefaultIsDisabledBehaviourally) {
  const Real lx = 20.0;
  const Real ly = 20.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 10, lx, ly);
  const Index n = mesh.numberOfCells();
  const Real mu = 0.1;
  const Real alpha = 1.0;
  const Real pseudoTimeStep = 1.0;

  // div U = x, so the dilatational term is genuinely non-zero and the branches differ.
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector2{0.5 * cell.centroid().x * cell.centroid().x, 0.0};
  }
  const ScalarField pressure(n, 1000.0);
  const ScalarField density(n, 1.3);
  const ScalarField previousComponent(n, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const auto vbc = p17_4::zeroGradientVelocity(mesh);
  const auto pbc = p17_4::zeroGradientScalar(mesh);

  // NOTE the missing final argument: this call takes the header's default.
  const MomentumAssembly defaulted =
      cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
          mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
          previousComponent, alpha, pseudoTimeStep, {},
          cfd::discretization::GradientScheme::GreenGauss, nullptr);
  const MomentumAssembly off = cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
      mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
      previousComponent, alpha, pseudoTimeStep, {}, cfd::discretization::GradientScheme::GreenGauss,
      nullptr, false);
  const MomentumAssembly on = cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
      mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
      previousComponent, alpha, pseudoTimeStep, {}, cfd::discretization::GradientScheme::GreenGauss,
      nullptr, true);

  // (1) the default is the DISABLED branch, bit for bit.
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(defaulted.system.rhs()[i], off.system.rhs()[i])
        << "cell " << i << ": the default does not reproduce includeDilatationalStress=false";
  }

  // (2) and it is NOT the enabled branch.
  Index differingFromOn = 0;
  for (Index i = 0; i < n; ++i) {
    if (defaulted.system.rhs()[i] != on.system.rhs()[i]) ++differingFromOn;
  }
  EXPECT_GT(differingFromOn, 0) << "the default matches the ENABLED branch -- default is not off";

  // (3) non-vacuity of (1) and (2): the two branches must actually be distinguishable here.
  Index branchesDiffer = 0;
  for (Index i = 0; i < n; ++i) {
    if (on.system.rhs()[i] != off.system.rhs()[i]) ++branchesDiffer;
  }
  ASSERT_GT(branchesDiffer, 0)
      << "on and off are identical on this mesh/field -- this test cannot observe the default";
}

// ---------------------------------------------------------------------------
// P17.4 gate criteria C2 and C4 -- CONSERVATION.
//
// Both were frozen with a derived bound and NEITHER had a test. The gap was found by auditing
// acceptance_gate.md against the tests that actually exist, which is the same kind of gap
// control N6 exposed for D3: a criterion can be written down, reviewed, and still be measured
// by nothing at all.
//
// The bound is the project's existing form, reused verbatim rather than re-derived:
//     bound = kAccum * eps * N * scale      kAccum = 8, EMPIRICAL and labelled as such
// N and scale are taken from the case, so the bound scales with the mesh and the magnitude and
// is never an absolute constant.
//
// physics/MomentumEquation.cpp may not be touched (criterion L5), so neither criterion can have
// a planted-defect control. Each therefore carries an IN-TEST control that perturbs the INPUT,
// and the control assertion is part of the test: if the perturbation did NOT break the
// criterion, the test fails and says so. Measured values are in logs/11_conservation.log.
// ---------------------------------------------------------------------------

namespace p17_4 {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kAccum = 8.0;  // EMPIRICAL

BoundaryConditionSet allWallVelocity(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
  }
  return boundaries;
}

}  // namespace p17_4

// C2: uniform rho, U and p is an exact discrete solution, so the residual is round-off.
TEST(CompressibleMomentumConservationTest, UniformFlowResidualIsAtRoundOff) {
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 8.0, 8.0);
  const Index n = mesh.numberOfCells();
  const Real rho = 1.3;
  const Real uUniform = 2.0;
  const Real mu = 0.05;
  const Real dt = 0.5;

  const VectorField velocity(n, Vector2{uUniform, 0.0});
  const ScalarField pressure(n, 1000.0);
  const ScalarField density(n, rho);
  const ScalarField previous(n, uUniform);
  const auto vbc = p17_4::zeroGradientVelocity(mesh);
  const auto pbc = p17_4::zeroGradientScalar(mesh);

  // The mass flux a uniform field actually produces. Face exposes areaVector(), the outward
  // normal already scaled by the area, so no separate area factor belongs here.
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    massFlux[face.id()] = rho * (uUniform * face.areaVector().x);
  }

  const MomentumAssembly a = cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
      mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
      previous, 1.0, dt, {}, cfd::discretization::GradientScheme::GreenGauss, nullptr, false);

  const auto& matrix = a.system.matrix();
  cfd::algebra::Vector u(n, uUniform);
  const cfd::algebra::Vector Au = matrix.multiply(u);

  Real worst = 0.0;
  Real scale = 0.0;
  for (Index row = 0; row < n; ++row) {
    worst = std::max(worst, std::abs(Au[row] - a.system.rhs()[row]));
    scale = std::max(scale, std::abs(matrix.diagonal(row) * uUniform));
  }
  const Real bound = p17_4::kAccum * p17_4::kEps * static_cast<Real>(n) * scale;

  EXPECT_GT(scale, 0.0) << "the operator is empty -- nothing was assembled";
  EXPECT_LE(worst, bound) << "uniform flow is an exact discrete solution; residual " << worst
                          << " exceeds the derived bound " << bound;

  // IN-TEST CONTROL: break uniformity in one cell. The residual MUST leave the bound behind,
  // otherwise "the residual is small" only meant the operator is small.
  cfd::algebra::Vector perturbed(n, uUniform);
  perturbed[n / 2] += 1.0e-3;
  const cfd::algebra::Vector perturbedAu = matrix.multiply(perturbed);
  Real worstPerturbed = 0.0;
  for (Index row = 0; row < n; ++row) {
    worstPerturbed = std::max(worstPerturbed, std::abs(perturbedAu[row] - a.system.rhs()[row]));
  }
  EXPECT_GT(worstPerturbed, bound)
      << "CONTROL FAILED: perturbing one cell by 1e-3 did not break the criterion, so the "
         "criterion is vacuous";
}

// C4: the assembly is globally conservative -- internal faces cancel in pairs, so a closed box
// sums to zero for ANY field and ANY internal flux. No divergence-free assumption is needed:
// the cancellation is per FACE, not per cell.
TEST(CompressibleMomentumConservationTest, ConvectiveAssemblyIsGloballyConservative) {
  const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 7.0, 5.0);
  const Index n = mesh.numberOfCells();
  const auto vbc = p17_4::allWallVelocity(mesh);  // impermeable: zero normal flux everywhere

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;  // a closed box
    const Real f = static_cast<Real>(face.id());
    massFlux[face.id()] = 0.37 * std::sin(0.7 * f) + 0.11 * static_cast<Real>(face.id() % 5);
  }

  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    const Real x = cell.centroid().x;
    const Real y = cell.centroid().y;
    velocity[cell.id()] = Vector2{0.9 + 0.4 * std::sin(x) + 0.2 * y, 0.3 * std::cos(y)};
  }

  const auto globalSum = [&](const SurfaceField& flux, Real& worstCell) {
    cfd::algebra::SparseMatrixBuilder builder(n, n);
    cfd::algebra::Vector rhs(n, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, flux, velocity, vbc, VelocityComponent::U,
                                                 builder, rhs);
    const cfd::algebra::SparseMatrix matrix = builder.build();
    cfd::algebra::Vector ux(n, 0.0);
    for (Index i = 0; i < n; ++i) ux[i] = velocity[i].x;
    const cfd::algebra::Vector Au = matrix.multiply(ux);
    Real total = 0.0;
    worstCell = 0.0;
    for (Index row = 0; row < n; ++row) {
      const Real cellValue = Au[row] - rhs[row];
      total += cellValue;
      worstCell = std::max(worstCell, std::abs(cellValue));
    }
    return total;
  };

  Real worstCell = 0.0;
  const Real total = globalSum(massFlux, worstCell);
  const Real bound =
      p17_4::kAccum * p17_4::kEps * static_cast<Real>(mesh.numberOfFaces()) * worstCell;

  // Non-vacuity of the sum itself: the per-cell contributions must be large, so that the
  // near-zero total is genuine cancellation and not an operator that is simply zero.
  EXPECT_GT(worstCell, 1.0) << "per-cell contributions are tiny -- the sum cancels nothing";
  EXPECT_LE(std::abs(total), bound) << "the interior must telescope: global sum " << total
                                    << " exceeds the derived bound " << bound;

  // IN-TEST CONTROL: open the box. One boundary face with non-zero flux has no partner to
  // cancel against, so the global sum must become O(1).
  SurfaceField opened = massFlux;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      opened[face.id()] = 1.7;
      break;
    }
  }
  Real openedWorstCell = 0.0;
  const Real openedTotal = globalSum(opened, openedWorstCell);
  EXPECT_GT(std::abs(openedTotal), bound)
      << "CONTROL FAILED: an unbalanced boundary flux did not break global conservation, so the "
         "criterion is vacuous";
}

// ---------------------------------------------------------------------------
// P17.4 gate criterion V3: "the existing assembly still throws NumericalError on a non-finite
// assembled system." The header documents that behaviour and, before this test, nothing
// measured it -- found by the same audit of the gate against the suite that surfaced C2 and C4.
//
// The distinction the test has to respect: a non-finite SIZE or pseudoTimeStep is an
// InvalidArgumentError raised by a precondition, whereas a non-finite ASSEMBLED SYSTEM is a
// NumericalError raised by the allFinite() check after the build. Pressure values are not
// precondition-validated, so a NaN there reaches the assembly and is the right way in. Asserting
// the wrong exception type would pass while testing nothing.
// ---------------------------------------------------------------------------
TEST(CompressibleMomentumConservationTest, NonFiniteAssembledSystemThrowsNumericalError) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 4.0, 4.0);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  const ScalarField density(n, 1.2);
  const ScalarField previous(n, 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const auto vbc = p17_4::zeroGradientVelocity(mesh);
  const auto pbc = p17_4::zeroGradientScalar(mesh);

  const auto assembleWithPressure = [&](const ScalarField& pressure) {
    return cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
        mesh, velocity, pressure, massFlux, density, density, 0.01, vbc, pbc, VelocityComponent::U,
        previous, 1.0, 0.1, {}, cfd::discretization::GradientScheme::GreenGauss, nullptr, false);
  };

  // Non-vacuity first: the same call with finite pressure must SUCCEED, so a throw below is
  // caused by the NaN and not by the fixture being invalid in some other way.
  const ScalarField finitePressure(n, 100.0);
  EXPECT_NO_THROW((void)assembleWithPressure(finitePressure));

  ScalarField nanPressure(n, 100.0);
  nanPressure[n / 2] = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW((void)assembleWithPressure(nanPressure), cfd::NumericalError);

  ScalarField infPressure(n, 100.0);
  infPressure[0] = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)assembleWithPressure(infPressure), cfd::NumericalError);
}

// ---------------------------------------------------------------------------
// P17.4 gate criterion B3: "Symmetry contributes zero normal momentum flux."
//
// Before this test, NO momentum test used the Symmetry boundary condition at all. Symmetry has
// its own unit test, but nothing exercised it through a flux or an assembly, so the B3 row in
// formulation.md section 7 was an expectation rather than a measurement. The gate's verdict
// requires Group B to pass, so the row is measured here instead of being quietly downgraded.
//
// The claim is a COMPOSITE one and that is the point: Symmetry::boundaryValue returns
// U_P - n (U_P . n), so the face velocity has no normal component, and therefore the solver's
// own mass flux rho_f (U_f . n) A must be EXACTLY zero at a symmetry face. Asserting only that
// Symmetry removes the normal component would re-test the BC; asserting only that a zero flux
// contributes nothing would be a tautology. The two halves together are the criterion:
//   (1) the mass flux the solver computes at a symmetry face is exactly 0
//   (2) so those faces contribute nothing to the momentum assembly -- shown by zeroing them and
//       requiring a BITWISE identical assembly, which would fail if the flux were non-zero
// ---------------------------------------------------------------------------
TEST(CompressibleMomentumConservationTest, SymmetryCarriesZeroNormalMomentumFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = mesh.numberOfCells();

  // Symmetry on "top"; the rest is an ordinary channel so the flux field is genuinely non-zero
  // elsewhere -- a globally zero flux would make part (2) vacuous.
  BoundaryConditionSet vbc;
  vbc.set(mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{3.0, 1.5}));
  vbc.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  vbc.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  vbc.set(mesh, "top", std::make_unique<cfd::boundary::Symmetry>());
  const auto pbc = p17_4::zeroGradientScalar(mesh);

  // A velocity with a real normal component at the top boundary, so Symmetry has something to
  // remove. A purely tangential field would make part (1) vacuous.
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector2{3.0, 1.5 + 0.3 * cell.centroid().x};
  }

  constexpr Real kGasConstant = 287.05;
  constexpr Real kSpecificHeatPressure = 1005.0;
  constexpr Real kReferenceTemperature = 300.0;
  constexpr Real kReferenceDensity = 2.0;
  const Real referencePressure = kReferenceDensity * kGasConstant * kReferenceTemperature;
  const cfd::compressible::ThermodynamicProperties thermo(kGasConstant, kSpecificHeatPressure);
  const ScalarField density(n, kReferenceDensity);
  const ScalarField temperature(n, kReferenceTemperature);
  const ScalarField pressureGauge(n, 0.0);

  const SurfaceField massFlux = cfd::compressible::calculateCompressibleMassFlux(
      mesh, velocity, density, vbc, pressureGauge, pbc, referencePressure, thermo, temperature,
      nullptr);

  // (1) exactly zero on every symmetry face.
  Index symmetryFaces = 0;
  for (const Index faceId : mesh.boundaryPatch("top").faceIds()) {
    ++symmetryFaces;
    EXPECT_EQ(massFlux[faceId], 0.0)
        << "face " << faceId << ": a symmetry face must carry exactly zero mass flux, got "
        << massFlux[faceId];
  }
  ASSERT_GT(symmetryFaces, 0) << "the mesh has no symmetry faces -- nothing was tested";

  // Non-vacuity: the flux must be substantial SOMEWHERE, otherwise zero at the top proves
  // nothing about Symmetry.
  Real maxAbsFlux = 0.0;
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    maxAbsFlux = std::max(maxAbsFlux, std::abs(massFlux[faceId]));
  }
  EXPECT_GT(maxAbsFlux, 1.0) << "the whole flux field is tiny -- zero at the top is not evidence";

  // (2) those faces therefore contribute nothing: explicitly zeroing them must leave the
  // assembled momentum system BITWISE identical.
  SurfaceField zeroedAtSymmetry = massFlux;
  for (const Index faceId : mesh.boundaryPatch("top").faceIds()) {
    zeroedAtSymmetry[faceId] = 0.0;
  }
  const ScalarField previous(n, 3.0);
  const auto assemble = [&](const SurfaceField& flux) {
    return cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
        mesh, velocity, pressureGauge, flux, density, density, 0.02, vbc, pbc, VelocityComponent::U,
        previous, 1.0, 0.1, {}, cfd::discretization::GradientScheme::GreenGauss, nullptr, false);
  };
  const MomentumAssembly asComputed = assemble(massFlux);
  const MomentumAssembly asZeroed = assemble(zeroedAtSymmetry);
  for (Index row = 0; row < n; ++row) {
    EXPECT_EQ(asComputed.system.rhs()[row], asZeroed.system.rhs()[row])
        << "row " << row
        << ": zeroing the symmetry-face flux changed the assembly, so those "
           "faces were carrying momentum";
    EXPECT_EQ(asComputed.diagonal[row], asZeroed.diagonal[row]) << "row " << row << ": diagonal";
  }
}
