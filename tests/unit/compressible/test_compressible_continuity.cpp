// P3-PHYS-006 sections 10, 28, 33: the transient compressible continuity
// diagnostic, hand-derived, plus the 1D-duct constant-mDot conservation
// check section 33 asks for ("this test should not require the entire
// nonlinear CFD solver").
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "cfd/compressible/CompressibleContinuity.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::compressible::CompressibleContinuityResult;
using cfd::compressible::evaluateCompressibleContinuity;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(CompressibleContinuityTest, TimeOnlyImbalanceWithZeroFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 2.0, 2.0);  // cell volume = 1 each.
  const Index n = mesh.numberOfCells();
  const ScalarField densityOld(n, 1.0);
  ScalarField densityNew(n, 1.0);
  densityNew[0] = 1.2;
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const Real dt = 0.1;

  const CompressibleContinuityResult result =
      evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, dt);

  // cellImbalance[0] = (1.2-1.0)*1.0/0.1 = 2.0; all other cells 0.
  EXPECT_NEAR(result.cellImbalance[0], 2.0, 1e-12);
  for (Index i = 1; i < n; ++i) {
    EXPECT_NEAR(result.cellImbalance[i], 0.0, 1e-12) << "cell " << i;
  }
  EXPECT_NEAR(result.totalMassOld, 4.0, 1e-12);  // 4 cells * rho=1 * V=1.
  EXPECT_NEAR(result.totalMassNew, 4.2, 1e-12);  // one cell's rho raised by 0.2.
  EXPECT_NEAR(result.maxCellImbalance, 2.0, 1e-12);
}

TEST(CompressibleContinuityTest, SteadyStateWithBalancedFluxGivesZeroImbalance) {
  // densityOld == densityNew (no time term); massFlux is a genuinely
  // steady, mass-conservative field (constant on every internal face) --
  // the FVM face-once assembly already guarantees this telescopes to
  // exactly zero at every interior cell, same identity proven for every
  // other transport equation in this codebase.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 2, 4.0, 1.0);  // a 1D duct, 4 cells long.
  const Index n = mesh.numberOfCells();
  const ScalarField density(n, 1.0);
  // A uniform mDot on every x-normal face (internal or boundary) in the
  // +x sense -- the 1D-duct constant-mass-flow-rate condition, section
  // 33's own "mDot = rho u A = constant"; y-normal (top/bottom) faces
  // get exactly 0 (no cross-duct flow).
  // Owner-oriented: a face's own Sf sets its positive sense. Internal
  // x-normal faces and the right (outlet) boundary all have Sf pointing
  // in +x, so +2 there means "flow in the +x sense"; the left (inlet)
  // boundary's Sf points in -x (outward from the domain, to the left),
  // so the SAME physical +x-directed flow is -2 there (mass entering,
  // not leaving) -- this is exactly what makes the duct's mass flow rate
  // actually constant in the physical sense, not merely numerically
  // uniform in the raw (sign-ambiguous) owner-oriented array.
  SurfaceField ductFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (std::abs(face.areaVector().x) > 1e-12) {
      ductFlux[face.id()] = (face.areaVector().x > 0.0) ? 2.0 : -2.0;
    }
  }

  const Real dt = 0.05;
  const CompressibleContinuityResult result =
      evaluateCompressibleContinuity(mesh, density, density, ductFlux, dt);

  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(result.cellImbalance[cell.id()], 0.0, 1e-10)
        << "cell " << cell.id() << " (constant duct mass flow rate must balance exactly)";
  }
  EXPECT_NEAR(result.totalMassOld, result.totalMassNew, 1e-12);
}

TEST(CompressibleContinuityTest, CombinedTimeAndFluxHandDerived) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cell volume = 1 each.
  const Index n = mesh.numberOfCells();
  ScalarField densityOld(n, 1.0);
  ScalarField densityNew(n);
  densityNew[0] = 1.1;
  densityNew[1] = 0.9;
  const Real dt = 0.2;

  Index internalFaceId = 0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) internalFaceId = face.id();
  }
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[internalFaceId] = 3.0;  // owner (cell0) -> neighbor (cell1).

  const CompressibleContinuityResult result =
      evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, dt);

  // cell0: (1.1-1.0)*1/0.2 + 3.0 = 0.5 + 3.0 = 3.5.
  EXPECT_NEAR(result.cellImbalance[0], 3.5, 1e-10);
  // cell1: (0.9-1.0)*1/0.2 - 3.0 = -0.5 - 3.0 = -3.5.
  EXPECT_NEAR(result.cellImbalance[1], -3.5, 1e-10);
}

TEST(CompressibleContinuityTest, MismatchedDensityOldSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField densityOld(mesh.numberOfCells() + 1, 1.0);
  const ScalarField densityNew(mesh.numberOfCells(), 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleContinuityTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleContinuityTest, RejectsNonPositiveOrNonFiniteDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux, 0.0),
               InvalidArgumentError);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux, -0.1),
               InvalidArgumentError);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux,
                                                    std::numeric_limits<Real>::quiet_NaN()),
               InvalidArgumentError);
}
