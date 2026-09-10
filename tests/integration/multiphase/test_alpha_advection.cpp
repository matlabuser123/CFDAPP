// P3-PHYS-005 sections 27+30: translation/advection validation plus grid
// refinement -- a phase-1 slug in a uniform prescribed cross-flow is
// advected downstream; correct direction, approximate displacement,
// boundedness, and (via two grids) reduced interface smearing under
// refinement are all checked. First-order upwind convection introduces
// numerical diffusion (this task's own section 22/27 explicit warning),
// so an exactly sharp interface is not expected -- interface *thickness*
// (not a step discontinuity) is the metric refinement is judged against.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/multiphase/VolumeFractionEquation.hpp"
#include "cfd/multiphase/VolumeFractionSolver.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::multiphase::AlphaBounds;
using cfd::multiphase::phaseVolume;
using cfd::multiphase::volumeFractionBounds;
using cfd::multiphase::VolumeFractionSolver;
using cfd::multiphase::VolumeFractionSolverSettings;
using cfd::multiphase::VolumeFractionStatus;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

constexpr Real kLength = 2.0;
constexpr Real kHeight = 0.4;
constexpr Real kVelocity = 1.0;
constexpr Real kSlugStart = 0.2;
constexpr Real kSlugEnd = 0.6;

VolumeFractionSolverSettings makeLoosenedSettings() {
  VolumeFractionSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-4;
  settings.linearSolver.relativeTolerance = 1e-3;
  return settings;
}

struct AdvectionOutcome {
  Real initialPhaseVolume;
  Real finalPhaseVolume;
  Real initialCentroidX;
  Real finalCentroidX;
  AlphaBounds bounds;
  Real interfaceThickness;  // x-extent over which alpha crosses 0.1..0.9 at the leading edge.
};

Real alphaCentroidX(const Mesh& mesh, const ScalarField& alpha) {
  Real numerator = 0.0, denominator = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real weight = alpha[cell.id()] * cell.volume();
    numerator += weight * cell.centroid().x;
    denominator += weight;
  }
  return numerator / denominator;
}

// Leading-edge thickness: among cells near the domain's y-midline,
// distance in x between the last cell with alpha>=0.9 and the first cell
// with alpha<=0.1, scanning from the slug's leading (downstream) side.
Real leadingEdgeThickness(const Mesh& mesh, const ScalarField& alpha, Index ny) {
  const Index midRow = ny / 2;
  std::vector<std::pair<Real, Real>> rowValues;  // (x, alpha) for the mid row only.
  for (const auto& cell : mesh.cells()) {
    // Identify the mid-row cells by y-centroid proximity (structured grid,
    // so a simple half-height comparison suffices).
    if (std::abs(cell.centroid().y -
                 (kHeight * (static_cast<Real>(midRow) + 0.5) / static_cast<Real>(ny))) < 1e-9) {
      rowValues.emplace_back(cell.centroid().x, alpha[cell.id()]);
    }
  }
  std::sort(rowValues.begin(), rowValues.end());
  Real xHigh = -1.0, xLow = -1.0;
  for (const auto& [x, value] : rowValues) {
    if (value >= 0.9) xHigh = x;
    if (value <= 0.1 && xHigh >= 0.0 && xLow < 0.0) xLow = x;
  }
  if (xHigh < 0.0 || xLow < 0.0) return 0.0;  // no clear crossing found -- caller handles.
  return xLow - xHigh;
}

AdvectionOutcome runAdvection(Index nx, Index ny, Index numSteps, Real dt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, kLength, kHeight);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{kVelocity, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  BoundaryConditionSet alphaBoundaries;
  alphaBoundaries.set(mesh, "left", std::make_unique<FixedValue>(0.0));  // ambient fluid at inlet.
  alphaBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{kVelocity, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  ScalarField alpha(n, 0.0);
  for (const auto& cell : mesh.cells()) {
    if (cell.centroid().x >= kSlugStart && cell.centroid().x < kSlugEnd) {
      alpha[cell.id()] = 1.0;
    }
  }

  AdvectionOutcome outcome;
  outcome.initialPhaseVolume = phaseVolume(mesh, alpha);
  outcome.initialCentroidX = alphaCentroidX(mesh, alpha);

  const VolumeFractionSolver solver{makeLoosenedSettings()};
  for (Index step = 0; step < numSteps; ++step) {
    const auto result = solver.step(mesh, alpha, massFlux, alphaBoundaries, dt);
    if (result.status != VolumeFractionStatus::Converged) {
      ADD_FAILURE() << "step " << step
                    << " did not converge (status=" << static_cast<int>(result.status) << ")";
      break;
    }
    alpha = result.alpha;
  }

  outcome.finalPhaseVolume = phaseVolume(mesh, alpha);
  outcome.finalCentroidX = alphaCentroidX(mesh, alpha);
  outcome.bounds = volumeFractionBounds(alpha);
  outcome.interfaceThickness = leadingEdgeThickness(mesh, alpha, ny);
  return outcome;
}

}  // namespace

TEST(AlphaAdvectionValidation, SlugTranslatesDownstreamWithApproximatelyCorrectDisplacement) {
  const Index nx = 80, ny = 4;
  const Real dt = 0.005;
  const Index numSteps = 40;  // total elapsed time = 0.2 -> expected displacement = 0.2.
  const auto outcome = runAdvection(nx, ny, numSteps, dt);

  const Real expectedDisplacement = kVelocity * static_cast<Real>(numSteps) * dt;
  const Real actualDisplacement = outcome.finalCentroidX - outcome.initialCentroidX;

  // Correct direction: must move downstream (+x), not stay put or reverse.
  EXPECT_GT(actualDisplacement, 0.0);
  // Approximately correct magnitude -- generous tolerance (25%) since
  // first-order upwind smearing shifts the *apparent* centroid slightly
  // versus a perfectly sharp translated profile, and the slug has not yet
  // reached the outlet at this elapsed time (kSlugEnd + expectedDisplacement
  // = 0.6+0.2 = 0.8 << kLength = 2.0).
  EXPECT_NEAR(actualDisplacement, expectedDisplacement, 0.25 * expectedDisplacement);

  // No unexpected source/sink: phase volume approximately conserved (the
  // slug is still fully inside the domain, away from both boundaries).
  EXPECT_NEAR(outcome.finalPhaseVolume, outcome.initialPhaseVolume,
              0.1 * outcome.initialPhaseVolume);

  // Boundedness.
  EXPECT_GE(outcome.bounds.minimum, -1e-3);
  EXPECT_LE(outcome.bounds.maximum, 1.0 + 1e-3);
}

TEST(AlphaAdvectionValidation, GridRefinementReducesInterfaceSmearing) {
  const Real dt = 0.005;
  const Index numSteps = 40;
  const auto coarse = runAdvection(40, 4, numSteps, dt);
  const auto fine = runAdvection(80, 4, numSteps, dt);

  ASSERT_GT(coarse.interfaceThickness, 0.0) << "coarse grid: no clear 0.1/0.9 crossing found";
  ASSERT_GT(fine.interfaceThickness, 0.0) << "fine grid: no clear 0.1/0.9 crossing found";
  // Finer grid (smaller dx) -> less first-order-upwind numerical
  // diffusion -> a thinner interface.
  EXPECT_LT(fine.interfaceThickness, coarse.interfaceThickness);
}
