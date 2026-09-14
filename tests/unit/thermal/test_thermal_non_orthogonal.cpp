// P12-NUM-003 continuation: non-orthogonal correction of thermal
// conduction (thermal::assembleThermalDiffusionContribution, through the
// shared cfd::discretization::NonOrthogonalDiffusion.hpp formula), end to
// end through thermal::ThermalSolver. Verification against an analytical
// conduction solution (not a re-derivation): uniform source Q, exact
//   T = -(Q/4k)(x^2 + y^2) + sin(pi x) sinh(pi y) / sinh(pi)
// (k lap T + Q = 0), exact per-face wall temperatures, distorted meshes.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalSolverSettings;

namespace {

constexpr Real kSource = 3.0;
constexpr Real kConductivity = 0.5;

Real exactTemperature(const Vector2& p) {
  return (-(kSource / (4.0 * kConductivity)) * ((p.x * p.x) + (p.y * p.y))) +
         (std::sin(cfd::constants::pi * p.x) * std::sinh(cfd::constants::pi * p.y) /
          std::sinh(cfd::constants::pi));
}

ThermalSolverSettings solverSettings(bool correct, GradientScheme scheme) {
  ThermalSolverSettings settings;
  settings.linearSolver.maxIterations = 20000;
  settings.linearSolver.absoluteTolerance = 1e-11;
  settings.linearSolver.relativeTolerance = 1e-10;
  settings.linearSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  settings.tolerance = 1e-10;
  settings.maxIterations = 500;
  settings.nonOrthogonal.enabled = correct;
  settings.nonOrthogonal.gradientScheme = scheme;
  return settings;
}

struct Errors {
  Real l1;
  Real l2;
  Real linf;
  Real energyBalance;
};

Errors solveConduction(Index n, Real fraction, bool correct, GradientScheme scheme) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      fraction > 0.0
          ? cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, fraction / static_cast<Real>(n))
          : cfd::mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0));
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(),
                   std::make_unique<cfd::boundary::FixedTemperature>(
                       exactTemperature(mesh.face(patch.faceIds().front()).centroid())));
  }
  const ThermalProperties properties(kConductivity, 1000.0);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const ThermalSolverSettings settings = solverSettings(correct, scheme);
  const ThermalSolver solver(settings);
  const auto result = solver.solve(mesh, ScalarField(mesh.numberOfCells(), 0.0), noFlow, properties,
                                   boundaries, kSource);
  EXPECT_TRUE(result.converged()) << "n=" << n;

  Errors errors{0.0, 0.0, 0.0, 0.0};
  Real volume = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real e = std::abs(result.temperature[cell.id()] - exactTemperature(cell.centroid()));
    errors.l1 += e * cell.volume();
    errors.l2 += e * e * cell.volume();
    errors.linf = std::max(errors.linf, e);
    volume += cell.volume();
  }
  errors.l1 /= volume;
  errors.l2 = std::sqrt(errors.l2 / volume);

  // Global energy balance of the discrete solution: internal (and explicit
  // correction) fluxes cancel face by face, so the sum of every row of
  // A T - b is the net boundary conduction minus Q * area -- zero if the
  // correction introduces no artificial source/sink.
  const auto assembly = cfd::thermal::assembleEnergyEquation(
      mesh, result.temperature, noFlow, properties, boundaries, kSource, settings.nonOrthogonal);
  cfd::algebra::Vector x(mesh.numberOfCells());
  for (Index i = 0; i < x.size(); ++i) x[i] = result.temperature[i];
  const auto ax = assembly.system.matrix().multiply(x);
  for (Index i = 0; i < x.size(); ++i) errors.energyBalance += ax[i] - assembly.system.rhs()[i];
  return errors;
}

}  // namespace

// Thermal.NonOrthogonalDiffusion (solution level): on a strongly distorted
// mesh the uncorrected conduction solve converges at only ~1st order; the
// corrected one at 2nd order (L1/L2), with errors matching the Cartesian
// solve's. Global energy balance closes in every case. Measured tables
// printed as evidence (results/p12-num-003/summary.md).
TEST(ThermalNonOrthogonalTest, NonOrthogonalDiffusion) {
  const Index grids[3] = {8, 16, 32};
  for (int mode = 0; mode < 3; ++mode) {
    const bool correct = mode > 0;
    const GradientScheme scheme =
        (mode == 2) ? GradientScheme::LeastSquares : GradientScheme::GreenGauss;
    Errors e[3];
    for (int g = 0; g < 3; ++g) {
      e[g] = solveConduction(grids[g], 0.45, correct, scheme);
      EXPECT_LT(std::abs(e[g].energyBalance), 1e-8) << "energy balance, n=" << grids[g];
    }
    std::printf(
        "\nConduction, distorted 0.45h, %s: L1 %.4g %.4g %.4g (p %.3f %.3f) | L2 %.4g %.4g %.4g "
        "(p %.3f %.3f) | Linf %.4g %.4g %.4g (p %.3f %.3f)\n",
        mode == 0 ? "uncorrected" : (mode == 1 ? "corrected(GG)" : "corrected(LS)"), e[0].l1,
        e[1].l1, e[2].l1, std::log2(e[0].l1 / e[1].l1), std::log2(e[1].l1 / e[2].l1), e[0].l2,
        e[1].l2, e[2].l2, std::log2(e[0].l2 / e[1].l2), std::log2(e[1].l2 / e[2].l2), e[0].linf,
        e[1].linf, e[2].linf, std::log2(e[0].linf / e[1].linf), std::log2(e[1].linf / e[2].linf));
    std::printf("  energy balance sum(A T - b): %.3g %.3g %.3g\n", e[0].energyBalance,
                e[1].energyBalance, e[2].energyBalance);
    const Real l2Order = std::log2(e[1].l2 / e[2].l2);
    const Real l1Order = std::log2(e[1].l1 / e[2].l1);
    if (correct) {
      EXPECT_GT(l2Order, 1.8);
      EXPECT_GT(l1Order, 1.8);
      EXPECT_GT(std::log2(e[1].linf / e[2].linf), 1.5);
    } else {
      EXPECT_LT(l2Order, 1.3);
    }
  }
  const Errors uncorrected = solveConduction(32, 0.45, false, GradientScheme::GreenGauss);
  const Errors corrected = solveConduction(32, 0.45, true, GradientScheme::LeastSquares);
  const Errors cartesian = solveConduction(32, 0.0, false, GradientScheme::GreenGauss);
  EXPECT_LT(corrected.l2, 0.1 * uncorrected.l2);
  EXPECT_LT(corrected.l2, 1.1 * cartesian.l2);
}

// Cartesian regression: the corrected thermal solve is bit-identical to the
// uncorrected one on a Cartesian mesh (every decomposition exactly {Sf, 0}).
TEST(ThermalNonOrthogonalTest, CartesianSolveIsBitIdentical) {
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(9, 7, 1.0, 0.7);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedTemperature>(300.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedTemperature>(340.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::HeatFlux>(200.0, 0.6));
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const ThermalProperties properties(0.6, 1000.0);
  const auto off = ThermalSolver(solverSettings(false, GradientScheme::GreenGauss))
                       .solve(mesh, ScalarField(mesh.numberOfCells(), 310.0), noFlow, properties,
                              boundaries, 25.0);
  for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    const auto on = ThermalSolver(solverSettings(true, scheme))
                        .solve(mesh, ScalarField(mesh.numberOfCells(), 310.0), noFlow, properties,
                               boundaries, 25.0);
    ASSERT_EQ(off.iterations, on.iterations);
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      EXPECT_EQ(off.temperature[i], on.temperature[i]);
    }
  }
}

// Prescribed-flux walls are never corrected: with HeatFlux/Adiabatic walls
// on a distorted mesh, the converged solve still satisfies the global
// energy balance (net conduction through the walls = prescribed wall flux
// + Q * area) to solver precision, with the correction on.
TEST(ThermalNonOrthogonalTest, PrescribedFluxWallsKeepEnergyBalance) {
  const Mesh mesh = cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.4 / 10.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedTemperature>(300.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::HeatFlux>(-150.0, 0.6));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedTemperature>(320.0));
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const ThermalProperties properties(0.6, 1000.0);
  ThermalSolverSettings settings = solverSettings(true, GradientScheme::LeastSquares);
  // The pre-existing Picard lag of a HeatFlux wall contracts only ~5% per
  // outer iteration on this case (with or without the correction --
  // measured), so it needs more than solverSettings()'s 500 iterations.
  settings.maxIterations = 3000;
  const auto result = ThermalSolver(settings).solve(mesh, ScalarField(mesh.numberOfCells(), 310.0),
                                                    noFlow, properties, boundaries, 10.0);
  ASSERT_TRUE(result.converged());
  const auto assembly = cfd::thermal::assembleEnergyEquation(
      mesh, result.temperature, noFlow, properties, boundaries, 10.0, settings.nonOrthogonal);
  cfd::algebra::Vector x(mesh.numberOfCells());
  for (Index i = 0; i < x.size(); ++i) {
    x[i] = result.temperature[i];
    EXPECT_TRUE(std::isfinite(x[i]));
  }
  const auto ax = assembly.system.matrix().multiply(x);
  Real balance = 0.0;
  for (Index i = 0; i < x.size(); ++i) balance += ax[i] - assembly.system.rhs()[i];
  std::printf(
      "\nPrescribed-flux walls, distorted 10x10 0.4h, corrected(LS): %llu iterations,"
      " energy balance sum(A T - b) %.3g\n",
      static_cast<unsigned long long>(result.iterations), balance);
  EXPECT_LT(std::abs(balance), 1e-8);
}
