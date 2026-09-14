// P12-NUM-003 continuation: non-orthogonal correction of the k / epsilon /
// omega diffusion terms. Every turbulence model (KEpsilonModel,
// KOmegaModel, SSTModel) assembles its diffusion through ONE function,
// turbulence::assembleScalarDiffusionContribution, which now uses the
// shared cfd::discretization::NonOrthogonalDiffusion.hpp formula -- no
// per-model geometry. Operator-level verification plus model-level wiring.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::GradientScheme;
using cfd::discretization::NonOrthogonalCorrectionOptions;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

namespace {

struct Assembled {
  cfd::algebra::SparseMatrix matrix;
  Vector rhs;
};

Assembled assemble(const Mesh& mesh, const ScalarField& gamma, const ScalarField& phi,
                   const BoundaryConditionSet& boundaries,
                   const NonOrthogonalCorrectionOptions& options) {
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  cfd::turbulence::assembleScalarDiffusionContribution(mesh, gamma, phi, boundaries, builder, rhs,
                                                       options);
  return Assembled{builder.build(), rhs};
}

Real maxResidual(const Assembled& a, const ScalarField& phi) {
  Vector x(phi.size());
  for (Index i = 0; i < phi.size(); ++i) x[i] = phi[i];
  const Vector ax = a.matrix.multiply(x);
  Real worst = 0.0;
  for (Index i = 0; i < x.size(); ++i) worst = std::max(worst, std::abs(ax[i] - a.rhs[i]));
  return worst;
}

}  // namespace

// Turbulence.NonOrthogonalDiffusion (operator level): for a LINEAR scalar
// with exact Dirichlet boundary values and constant diffusivity, the
// continuous diffusion term vanishes, so the discrete residual A phi - b
// must vanish in every row: it does to round-off with the correction (both
// gradient schemes), and does not without it. Cartesian: the corrected
// system is bit-identical to the uncorrected one.
TEST(TurbulenceNonOrthogonalTest, NonOrthogonalDiffusion) {
  const auto phiExact = [](const Vector2& p) { return (0.3 * p.x) - (0.7 * p.y) + 1.5; };
  {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.4 / 10.0));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, phiExact);
    ScalarField phi(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) phi[cell.id()] = phiExact(cell.centroid());
    const ScalarField gamma(mesh.numberOfCells(), 0.02);
    const Real uncorrected = maxResidual(assemble(mesh, gamma, phi, boundaries, {}), phi);
    const Real correctedLs = maxResidual(
        assemble(mesh, gamma, phi, boundaries, {true, GradientScheme::LeastSquares}), phi);
    const Real correctedGg = maxResidual(
        assemble(mesh, gamma, phi, boundaries, {true, GradientScheme::GreenGauss}), phi);
    std::printf(
        "\nk/epsilon/omega diffusion operator, linear scalar, distorted 10x10 (0.4h): "
        "max|A phi - b| uncorrected %.4g, corrected(GG) %.3g, corrected(LS) %.3g\n",
        uncorrected, correctedGg, correctedLs);
    EXPECT_GT(uncorrected, 1e-5);
    EXPECT_LT(correctedLs, 1e-14);
    EXPECT_LT(correctedGg, 1e-12);
  }
  {
    const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(6, 5, 1.0, 1.0);
    BoundaryConditionSet boundaries;
    boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.0));
    boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedValue>(0.2));
    boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ScalarField phi(mesh.numberOfCells());
    ScalarField gamma(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      phi[cell.id()] = std::sin(3.0 * cell.centroid().x) + cell.centroid().y;
      gamma[cell.id()] = 0.01 + (0.02 * cell.centroid().y);
    }
    const auto off = assemble(mesh, gamma, phi, boundaries, {});
    const auto on = assemble(mesh, gamma, phi, boundaries, {true, GradientScheme::LeastSquares});
    ASSERT_EQ(off.matrix.nonZeros(), on.matrix.nonZeros());
    for (Index k = 0; k < off.matrix.nonZeros(); ++k) {
      EXPECT_EQ(off.matrix.valuesData()[k], on.matrix.valuesData()[k]);
    }
    for (Index i = 0; i < mesh.numberOfCells(); ++i) EXPECT_EQ(off.rhs[i], on.rhs[i]);
  }
}

// Model-level wiring: KEpsilonModel / KOmegaModel with the correction
// enabled in their config actually route it through the shared operator --
// on a distorted mesh the corrected model state differs from the
// uncorrected one after correct(), stays finite and positive (floors), and
// on a Cartesian mesh the two are bit-identical.
TEST(TurbulenceNonOrthogonalTest, ModelsRouteTheCorrectionThroughTheSharedOperator) {
  const cfd::physics::FluidProperties fluid(1.0, 0.01);
  for (const bool distorted : {true, false}) {
    const Mesh mesh = distorted ? cfd::test::createDistortedQuad2D(8, 8, 1.0, 1.0, 0.4 / 8.0)
                                : cfd::mesh::MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
    BoundaryConditionSet velocityBoundaries;
    velocityBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
    velocityBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
    velocityBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
    velocityBoundaries.set(mesh, "top",
                           std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));
    BoundaryConditionSet kBoundaries;
    BoundaryConditionSet secondBoundaries;
    for (const auto& patch : mesh.boundaryPatches()) {
      kBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
      secondBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    VectorField velocity(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      const Vector2 c = cell.centroid();
      velocity[cell.id()] = Vector2{c.y * c.y * std::sin(3.14159 * c.x), -0.2 * c.x * c.y};
    }
    const ScalarField pressure(mesh.numberOfCells(), 0.0);

    cfd::turbulence::KEpsilonConfig kEpsilonOff;
    kEpsilonOff.initialK = 0.05;
    kEpsilonOff.initialEpsilon = 0.02;
    cfd::turbulence::KEpsilonConfig kEpsilonOn = kEpsilonOff;
    kEpsilonOn.nonOrthogonal = {true, GradientScheme::LeastSquares};
    cfd::turbulence::KEpsilonModel a(mesh, fluid, velocityBoundaries, kBoundaries, secondBoundaries,
                                     kEpsilonOff);
    cfd::turbulence::KEpsilonModel b(mesh, fluid, velocityBoundaries, kBoundaries, secondBoundaries,
                                     kEpsilonOn);
    a.correct(mesh, velocity, pressure);
    b.correct(mesh, velocity, pressure);

    cfd::turbulence::KOmegaConfig kOmegaOff;
    kOmegaOff.initialK = 0.05;
    kOmegaOff.initialOmega = 2.0;
    cfd::turbulence::KOmegaConfig kOmegaOn = kOmegaOff;
    kOmegaOn.nonOrthogonal = {true, GradientScheme::GreenGauss};
    cfd::turbulence::KOmegaModel c(mesh, fluid, velocityBoundaries, kBoundaries, secondBoundaries,
                                   kOmegaOff);
    cfd::turbulence::KOmegaModel d(mesh, fluid, velocityBoundaries, kBoundaries, secondBoundaries,
                                   kOmegaOn);
    c.correct(mesh, velocity, pressure);
    d.correct(mesh, velocity, pressure);

    Real maxKEpsilonDifference = 0.0;
    Real maxKOmegaDifference = 0.0;
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      ASSERT_TRUE(std::isfinite(b.k()[i]) && std::isfinite(b.epsilon()[i]));
      ASSERT_TRUE(std::isfinite(d.k()[i]) && std::isfinite(d.omega()[i]));
      EXPECT_GT(b.epsilon()[i], 0.0);
      EXPECT_GT(d.omega()[i], 0.0);
      maxKEpsilonDifference = std::max({maxKEpsilonDifference, std::abs(a.k()[i] - b.k()[i]),
                                        std::abs(a.epsilon()[i] - b.epsilon()[i])});
      maxKOmegaDifference = std::max({maxKOmegaDifference, std::abs(c.k()[i] - d.k()[i]),
                                      std::abs(c.omega()[i] - d.omega()[i])});
    }
    if (distorted) {
      EXPECT_GT(maxKEpsilonDifference, 0.0);
      EXPECT_GT(maxKOmegaDifference, 0.0);
    } else {
      EXPECT_EQ(maxKEpsilonDifference, 0.0);
      EXPECT_EQ(maxKOmegaDifference, 0.0);
    }
  }
}
