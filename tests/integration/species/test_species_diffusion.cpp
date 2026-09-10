// P3-PHYS-004 section 20: pure species diffusion validated against the
// closed-form 1D slab solution Y(x) = Yleft + (Yright-Yleft)*x/L. No
// flow (massFlux == 0 everywhere), constant D, zero source -- the
// species-transport analogue of ThermalSolverTest's own 1D-conduction
// smoke test, but run here as the primary validation gate with explicit
// L2/Linf error reporting and multiple grids, per this task's own
// section 20 requirement.
//
// This case is exactly reproduced by the FVM scheme regardless of grid
// (an affine field has zero second derivative, and orthogonal-mesh FVM
// diffusion is exact for affine fields -- the same property
// ThermalSolverTest's own 1D-conduction test already exploits) -- so
// unlike the advection-diffusion case (test_species_advection_diffusion.cpp),
// there is no genuine truncation error here to refine away; multiple
// grids are run anyway (section 20's own "run multiple grids if
// inexpensive") to demonstrate the error stays uniformly tiny, not to
// show a decreasing trend that would not exist for an exact-affine
// solution.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesProperties.hpp"
#include "cfd/species/SpeciesSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::species::SpeciesProperties;
using cfd::species::SpeciesResult;
using cfd::species::SpeciesSolver;
using cfd::species::SpeciesStatus;

namespace {

constexpr Real kLength = 1.0;
constexpr Real kHeight = 0.2;
constexpr Real kYLeft = 1.0;
constexpr Real kYRight = 0.0;

struct DiffusionErrors {
  Real l2;
  Real linf;
};

DiffusionErrors runSlabCase(Index nx) {
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, 4, kLength, kHeight);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(kYLeft));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(kYRight));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const ScalarField initialConcentration(n, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 2.0e-3);

  const SpeciesSolver solver{};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::Converged) << "nx=" << nx;

  Real sumSquaredError = 0.0;
  Real maxError = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact = kYLeft + (kYRight - kYLeft) * (cell.centroid().x / kLength);
    const Real error = result.concentration[cell.id()] - exact;
    sumSquaredError += error * error;
    maxError = std::max(maxError, std::abs(error));
  }
  return DiffusionErrors{std::sqrt(sumSquaredError / static_cast<Real>(n)), maxError};
}

}  // namespace

TEST(SpeciesDiffusionValidation, Grid20MatchesAnalyticalSlabProfile) {
  const auto errors = runSlabCase(20);
  EXPECT_LT(errors.l2, 1e-6);
  EXPECT_LT(errors.linf, 1e-6);
}

TEST(SpeciesDiffusionValidation, Grid40MatchesAnalyticalSlabProfile) {
  const auto errors = runSlabCase(40);
  EXPECT_LT(errors.l2, 1e-6);
  EXPECT_LT(errors.linf, 1e-6);
}

TEST(SpeciesDiffusionValidation, Grid80MatchesAnalyticalSlabProfile) {
  const auto errors = runSlabCase(80);
  EXPECT_LT(errors.l2, 1e-6);
  EXPECT_LT(errors.linf, 1e-6);
}

TEST(SpeciesDiffusionValidation, GlobalDiffusiveFluxBalanceIsExact) {
  // Steady, source-free, closed-except-at-the-two-Dirichlet-ends slab: the
  // diffusive flux entering at x=0 must exactly equal the flux leaving at
  // x=L (nothing is created or destroyed in between).
  const Mesh mesh = MeshGeometry::createCartesian2D(40, 4, kLength, kHeight);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(kYLeft));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(kYRight));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  const Index n = mesh.numberOfCells();
  const ScalarField initialConcentration(n, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const Real diffusivity = 2.0e-3;
  const SpeciesProperties species("tracer", diffusivity);

  const SpeciesSolver solver{};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  ASSERT_EQ(result.status, SpeciesStatus::Converged);

  // Analytical gradient is uniform: dY/dx = (Yright-Yleft)/L, so the exact
  // diffusive flux (per unit depth) at either end is
  // rho*D*|dY/dx|*height.
  const Real exactFlux =
      fluid.density() * diffusivity * std::abs((kYRight - kYLeft) / kLength) * kHeight;

  // Numerically: sum the boundary diffusive flux at the left patch and at
  // the right patch directly from the solved field (first-cell one-sided
  // difference against the known Dirichlet face value, same convention
  // NaturalConvectionValidationUtils's own computeWallHeatFluxIntoFluid
  // uses).
  Real leftFlux = 0.0, rightFlux = 0.0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) continue;
    const auto patchName = cfd::boundary::boundaryPatchNameForFace(mesh, face.id());
    if (patchName != "left" && patchName != "right") continue;
    const Index ownerId = face.owner();
    const Real distance =
        cfd::mesh::MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    const Real yBoundary = (patchName == "left") ? kYLeft : kYRight;
    const Real gradMagnitude = std::abs(result.concentration[ownerId] - yBoundary) / distance;
    const Real flux = fluid.density() * diffusivity * gradMagnitude * face.area();
    if (patchName == "left") {
      leftFlux += flux;
    } else {
      rightFlux += flux;
    }
  }
  EXPECT_NEAR(leftFlux, exactFlux, 1e-6);
  EXPECT_NEAR(rightFlux, exactFlux, 1e-6);
  EXPECT_NEAR(leftFlux, rightFlux, 1e-9);
}
