// P12-NUM-003 continuation: non-orthogonal correction of species diffusion
// (species::assembleSpeciesDiffusionContribution through the shared
// cfd::discretization::NonOrthogonalDiffusion.hpp formula), end to end
// through species::SpeciesSolver. Analytical check: pure diffusion with a
// uniform source S, rho*D lap Y + S = 0, exact
//   Y = -(S/(4 rho D))(x^2 + y^2) + cos(pi x) cosh(pi y) / cosh(pi) + 2
// with exact per-face FixedValue concentrations, on distorted meshes.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/species/SpeciesSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::physics::FluidProperties;
using cfd::species::SpeciesProperties;
using cfd::species::SpeciesSolver;
using cfd::species::SpeciesSolverSettings;

namespace {

constexpr Real kSource = 0.8;
constexpr Real kDensity = 1.2;
constexpr Real kDiffusivity = 0.05;

Real exactConcentration(const Vector2& p) {
  return (-(kSource / (4.0 * kDensity * kDiffusivity)) * ((p.x * p.x) + (p.y * p.y))) +
         (std::cos(cfd::constants::pi * p.x) * std::cosh(cfd::constants::pi * p.y) /
          std::cosh(cfd::constants::pi)) +
         2.0;
}

SpeciesSolverSettings solverSettings(bool correct) {
  SpeciesSolverSettings settings;
  settings.linearSolver.maxIterations = 20000;
  // 1e-10 (the repo's usual): the Picard loop's late, warm-started linear
  // solves start from ~1e-8 residuals, where BiCGSTAB was measured to stall
  // just above 1e-11 (pre-existing solver behaviour, see summary.md).
  settings.linearSolver.absoluteTolerance = 1e-10;
  settings.linearSolver.relativeTolerance = 1e-10;
  settings.linearSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  settings.tolerance = 1e-10;
  settings.maxIterations = 500;
  settings.nonOrthogonal.enabled = correct;
  settings.nonOrthogonal.gradientScheme = GradientScheme::LeastSquares;
  return settings;
}

struct Outcome {
  Real l2;
  Real balance;
  bool finite;
};

Outcome solveDiffusion(Index n, bool correct) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.45 / static_cast<Real>(n)));
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(),
                   std::make_unique<cfd::boundary::FixedValue>(
                       exactConcentration(mesh.face(patch.faceIds().front()).centroid())));
  }
  const FluidProperties fluid(kDensity, 0.01);
  const SpeciesProperties species("A", kDiffusivity);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const SpeciesSolverSettings settings = solverSettings(correct);
  const auto result = SpeciesSolver(settings).solve(mesh, ScalarField(mesh.numberOfCells(), 1.0),
                                                    noFlow, fluid, species, boundaries, kSource);
  EXPECT_TRUE(result.converged()) << "n=" << n;
  Outcome outcome{0.0, 0.0, true};
  Real volume = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real e = result.concentration[cell.id()] - exactConcentration(cell.centroid());
    outcome.l2 += e * e * cell.volume();
    volume += cell.volume();
    outcome.finite = outcome.finite && std::isfinite(result.concentration[cell.id()]);
  }
  outcome.l2 = std::sqrt(outcome.l2 / volume);
  const auto assembly = cfd::species::assembleSpeciesTransportEquation(
      mesh, result.concentration, noFlow, fluid, species, boundaries, kSource,
      settings.nonOrthogonal);
  cfd::algebra::Vector x(mesh.numberOfCells());
  for (Index i = 0; i < x.size(); ++i) x[i] = result.concentration[i];
  const auto ax = assembly.system.matrix().multiply(x);
  for (Index i = 0; i < x.size(); ++i) outcome.balance += ax[i] - assembly.system.rhs()[i];
  return outcome;
}

}  // namespace

// Species.NonOrthogonalDiffusion: finite values, conservation (the discrete
// solution's global balance closes -- no artificial source/sink from the
// correction), distorted-mesh accuracy (corrected ~2nd order and far more
// accurate than uncorrected ~1st order). Measured values printed.
TEST(SpeciesNonOrthogonalTest, NonOrthogonalDiffusion) {
  const Index grids[3] = {8, 16, 32};
  Real uncorrected[3];
  Real corrected[3];
  for (int g = 0; g < 3; ++g) {
    const Outcome off = solveDiffusion(grids[g], false);
    const Outcome on = solveDiffusion(grids[g], true);
    EXPECT_TRUE(off.finite && on.finite);
    // Bounded by the linear solve (sum of N row residuals of a system solved
    // to |r|_2 <= 1e-10 is up to sqrt(N)*1e-10 ~ 3e-9 at 32x32) -- an
    // artificial source from the correction would appear at the explicit-
    // flux scale (~1e-2), against a total source S*area = 0.8.
    EXPECT_LT(std::abs(off.balance), 1e-8);
    EXPECT_LT(std::abs(on.balance), 1e-8);
    std::printf(
        "\nSpecies balance sum(A c - b), distorted 0.45h n=%llu: uncorrected %.3g, corrected %.3g",
        static_cast<unsigned long long>(grids[g]), off.balance, on.balance);
    uncorrected[g] = off.l2;
    corrected[g] = on.l2;
  }
  std::printf(
      "\nSpecies diffusion, distorted 0.45h, L2: uncorrected %.4g %.4g %.4g (p %.3f %.3f), "
      "corrected %.4g %.4g %.4g (p %.3f %.3f)\n",
      uncorrected[0], uncorrected[1], uncorrected[2], std::log2(uncorrected[0] / uncorrected[1]),
      std::log2(uncorrected[1] / uncorrected[2]), corrected[0], corrected[1], corrected[2],
      std::log2(corrected[0] / corrected[1]), std::log2(corrected[1] / corrected[2]));
  EXPECT_GT(std::log2(corrected[1] / corrected[2]), 1.8);
  EXPECT_LT(std::log2(uncorrected[1] / uncorrected[2]), 1.3);
  EXPECT_LT(corrected[2], 0.1 * uncorrected[2]);
}

// Cartesian regression: species solve with the correction on is
// bit-identical to the uncorrected one (FixedValue + FixedGradient walls).
TEST(SpeciesNonOrthogonalTest, CartesianSolveIsBitIdentical) {
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(8, 6, 1.0, 0.6);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(1.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.5));
  const FluidProperties fluid(1.1, 0.01);
  const SpeciesProperties species("A", 0.03);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const auto off = SpeciesSolver(solverSettings(false))
                       .solve(mesh, ScalarField(mesh.numberOfCells(), 0.5), noFlow, fluid, species,
                              boundaries, 0.2);
  const auto on = SpeciesSolver(solverSettings(true))
                      .solve(mesh, ScalarField(mesh.numberOfCells(), 0.5), noFlow, fluid, species,
                             boundaries, 0.2);
  ASSERT_EQ(off.iterations, on.iterations);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(off.concentration[i], on.concentration[i]);
  }
}
