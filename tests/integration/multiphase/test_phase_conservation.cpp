// P3-PHYS-005 sections 15-16: phase-volume conservation.
//
// Closed domain (section 15): summing the assembled transient equation's
// own rows telescopes internal-face contributions to exactly zero (every
// internal face adds +F*alphaUpwind to one row and -F*alphaUpwind to the
// other) and a zero-flux boundary contributes nothing either, leaving
// only the time-derivative term:
//   sum_cells(V_i/dt * (alphaNew_i - alphaOld_i)) = 0
// i.e. sum(V_i*alphaNew_i) == sum(V_i*alphaOld_i) *exactly* (up to the
// linear solve's own tolerance) -- true even with a nonzero, non-trivial
// *internal* circulating flux, as long as every *boundary* face carries
// zero flux. This is checked here directly through an actual solve
// (phaseVolume before vs. after), not just the linear-algebra residual.
//
// Open domain (section 16): accumulation = inflow - outflow, checked
// directly from the solved field against boundary fluxes computed
// independently of the assembled system.
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
using cfd::multiphase::phaseVolume;
using cfd::multiphase::VolumeFractionSolver;
using cfd::multiphase::VolumeFractionSolverSettings;
using cfd::multiphase::VolumeFractionStatus;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

VolumeFractionSolverSettings makeLoosenedSettings() {
  VolumeFractionSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-6;
  settings.linearSolver.relativeTolerance = 1e-5;
  return settings;
}

// The open-channel case below (nonzero boundary flux, not just a
// circulating internal one) needs the same further-loosened tolerance
// diagnosed in test_alpha_advection.cpp/test_volume_fraction_solver.cpp
// -- same BiCGSTAB-breakdown-on-a-pure-advection-operator finding.
VolumeFractionSolverSettings makeOpenChannelSettings() {
  VolumeFractionSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-4;
  settings.linearSolver.relativeTolerance = 1e-3;
  return settings;
}

}  // namespace

TEST(PhaseConservationTest, ClosedDomainConservesPhaseVolumeWithCirculatingInternalFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  const Index n = mesh.numberOfCells();
  ScalarField alpha(n);
  for (Index i = 0; i < n; ++i) alpha[i] = 0.2 + 0.05 * static_cast<Real>((i * 7) % 5);
  const Real initialVolume = phaseVolume(mesh, alpha);

  // Nonzero on every internal face, exactly zero on every boundary face
  // (a genuinely closed domain, not merely divergence-free).
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.3;
  }

  const VolumeFractionSolver solver{makeLoosenedSettings()};
  const auto result = solver.step(mesh, alpha, massFlux, boundaries, 0.01);
  ASSERT_EQ(result.status, VolumeFractionStatus::Converged);

  const Real finalVolume = phaseVolume(mesh, result.alpha);
  EXPECT_NEAR(finalVolume, initialVolume, 1e-5 * std::max<Real>(1.0, std::abs(initialVolume)));
}

TEST(PhaseConservationTest, ClosedDomainConservesPhaseVolumeOverManySteps) {
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  const Index n = mesh.numberOfCells();
  ScalarField alpha(n, 0.0);
  for (const auto& cell : mesh.cells()) {
    if (cell.centroid().x < 0.4) alpha[cell.id()] = 1.0;  // a block, not uniform.
  }
  const Real initialVolume = phaseVolume(mesh, alpha);

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.2;
  }

  const VolumeFractionSolver solver{makeLoosenedSettings()};
  for (int step = 0; step < 15; ++step) {
    const auto result = solver.step(mesh, alpha, massFlux, boundaries, 0.01);
    ASSERT_EQ(result.status, VolumeFractionStatus::Converged) << "step " << step;
    alpha = result.alpha;
  }

  const Real finalVolume = phaseVolume(mesh, alpha);
  EXPECT_NEAR(finalVolume, initialVolume, 1e-4 * std::max<Real>(1.0, std::abs(initialVolume)));
}

TEST(PhaseConservationTest, OpenChannelAccumulationMatchesNetInflowMinusOutflow) {
  const Real length = 1.0, height = 0.2, u = 1.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, length, height);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{u, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  BoundaryConditionSet alphaBoundaries;
  alphaBoundaries.set(mesh, "left", std::make_unique<FixedValue>(1.0));
  alphaBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{u, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  ScalarField alpha(n, 0.0);

  const VolumeFractionSolver solver{makeOpenChannelSettings()};
  const Real dt = 0.001;  // small dt for diagonal dominance -- see this file's own header comment.
  const Real initialVolume = phaseVolume(mesh, alpha);

  // Accumulate the *actual* net boundary convective flux leaving the
  // domain, timestep by timestep, evaluated against each step's own
  // (owner, boundary) values -- independent of the assembled system,
  // matching how test_species_conservation.cpp's own open-domain check
  // is computed.
  Real accumulatedNetOut = 0.0;
  for (int step = 0; step < 150; ++step) {
    const auto result = solver.step(mesh, alpha, massFlux, alphaBoundaries, dt);
    ASSERT_EQ(result.status, VolumeFractionStatus::Converged) << "step " << step;

    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) continue;
      const Index ownerId = face.owner();
      const cfd::boundary::BoundaryCondition& bc =
          cfd::boundary::boundaryConditionForFace(mesh, face.id(), alphaBoundaries);
      const auto& scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition&>(bc);
      const Real distance = cfd::mesh::MeshGeometry::distance(mesh.cell(ownerId).centroid(),
                                                              face.centroid());
      // Evaluated against the *old* (pre-step) alpha, matching the
      // equation's own lagged-boundary convention (see
      // VolumeFractionEquation.hpp's own header comment).
      const Real alphaB = scalarBc.boundaryValue(alpha[ownerId], distance);
      const Real ownerFlux = massFlux[face.id()];
      const Real convectiveOut = ownerFlux * ((ownerFlux >= 0.0) ? alpha[ownerId] : alphaB);
      accumulatedNetOut += convectiveOut * dt;
    }

    alpha = result.alpha;
  }

  const Real finalVolume = phaseVolume(mesh, alpha);
  const Real actualChange = finalVolume - initialVolume;
  const Real scale = std::max<Real>(std::abs(accumulatedNetOut), 1e-6);
  EXPECT_LT(std::abs(actualChange - (-accumulatedNetOut)) / scale, 0.05);
}
