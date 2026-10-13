// P3-PHYS-006 sections 11-12: the canonical compressible mass flux --
// uniform-density equivalence to physics::calculateMassFlux, and face-
// density interpolation for a non-uniform field.
//
// P12-COMP-001: boundary-face density is now EOS-evaluated at that
// face's own boundary pressure/temperature state (see
// CompressibleMassFlux.hpp's own header comment), superseding the
// previous owner-cell-reuse simplification -- the tests below replace
// the old `BoundaryFaceUsesOwnerCellsOwnDensity` test (which asserted
// exactly the behavior this task supersedes) with coverage of the new
// treatment per boundary-condition type, EOS consistency, invalid
// states, and the low-Mach/uniform limiting case.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::compressible::calculateCompressibleMassFlux;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

// Shared reference thermodynamic state for every test below: chosen so
// that EOS.density(kReferencePressure, kReferenceTemperature) ==
// kReferenceDensity exactly (rho = P/(R*T)), letting tests that don't
// care about compressible specifics reproduce the old "uniform density"
// test cases exactly, and tests that do care state their expected
// boundary density in closed form.
constexpr Real kGasConstant = 287.05;
constexpr Real kSpecificHeatPressure = 1005.0;
constexpr Real kReferenceTemperature = 300.0;
constexpr Real kReferenceDensity = 2.0;
// P = rho * R * T.
constexpr Real kReferencePressure = kReferenceDensity * kGasConstant * kReferenceTemperature;

ThermodynamicProperties makeThermo() {
  return ThermodynamicProperties(kGasConstant, kSpecificHeatPressure);
}

Mesh makeChannelMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeChannelVelocityBoundaries(const Mesh& mesh, Vector2 inletVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(inletVelocity));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

// Zero-gradient gauge pressure at every patch -- pairs with a uniform
// (zero) `pressureGauge` field to make every boundary face's evaluated
// absolute pressure exactly `kReferencePressure`, for tests that just
// need a valid, uniform, non-throwing boundary thermodynamic state.
BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

}  // namespace

TEST(CompressibleMassFluxTest, UniformDensityMatchesIncompressibleMassFluxExactly) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{3.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{3.0, 0.0});
  const ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  const FluidProperties fluid(kReferenceDensity, 1.0);
  const SurfaceField incompressible = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  const SurfaceField compressible = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    EXPECT_NEAR(compressible[faceId], incompressible[faceId], 1e-9) << "face " << faceId;
  }
}

TEST(CompressibleMassFluxTest, InternalFaceUsesArithmeticMeanDensity) {
  // Same setup as physics::MassFluxTest.InternalFaceValue, but with
  // per-cell density instead of one constant: rho_P=1, rho_N=3 ->
  // rho_f=2 (arithmetic mean on this uniform grid), U_P=(1,0), U_N=(3,0)
  // -> U_f=(2,0), internal face area=1, Sf=(1,0) -> F = 2*2*1 = 4.
  // Unaffected by P12-COMP-001 (internal-face density interpolation is
  // unchanged) -- boundary-related parameters below are only present
  // because the boundary faces this mesh also has must still evaluate to
  // a valid (non-throwing) state; their actual values are irrelevant to
  // this test's assertion.
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  VectorField velocity(mesh.numberOfCells());
  velocity[0] = Vector2{1.0, 0.0};
  velocity[1] = Vector2{3.0, 0.0};
  ScalarField density(mesh.numberOfCells());
  density[0] = 1.0;
  density[1] = 3.0;
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  const SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  bool found = false;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      EXPECT_NEAR(massFlux[face.id()], 4.0, 1e-12);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// P12-COMP-001: an Outlet paired with a Dirichlet (fixed_value) gauge-
// pressure BC -- the boundary's absolute pressure is *exactly*
// referencePressure regardless of the owner cell's own (here,
// deliberately different) gauge pressure, so the boundary density must
// come from the EOS at that state, not the owner cell's own density.
TEST(CompressibleMassFluxTest, OutletBoundaryUsesEosEvaluatedPressureNotOwnerDensity) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  BoundaryConditionSet pressureBoundaries;
  pressureBoundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  pressureBoundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));  // Dirichlet, gauge=0.
  pressureBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  pressureBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));

  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  // Owner cell's own gauge pressure is deliberately far from 0 (and its
  // own "density" entry deliberately wrong/unrelated) -- the outlet's
  // Dirichlet BC must override both.
  ScalarField pressureGauge(mesh.numberOfCells(), 50000.0);
  const ScalarField density(mesh.numberOfCells(), 999.0);  // must be ignored at the outlet face.
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  const SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  const Index rightFaceId = mesh.boundaryPatch("right").faceIds().front();
  const Real expectedDensity = thermo.density(kReferencePressure, kReferenceTemperature);
  const Real area = mesh.face(rightFaceId).area();
  // Sf=(+area,0) on the right/outlet patch, U=(1,0) -> U.Sf = +area.
  EXPECT_NEAR(massFlux[rightFaceId], expectedDensity * area, 1e-9);
  // Confirms it is NOT the (deliberately different) owner-cell density.
  EXPECT_NE(expectedDensity, density[mesh.face(rightFaceId).owner()]);
}

// P12-COMP-001: an Inlet paired with a zero-gradient pressure BC (this
// codebase's typical convention) reduces exactly to the owner cell's own
// EOS-consistent density when the owner cell's density already *is* the
// EOS value at its own (gauge pressure, temperature) -- i.e. the new
// treatment recovers the old owner-cell result in this case, it does not
// always differ from it.
TEST(CompressibleMassFluxTest, InletBoundaryReducesToOwnerDensityUnderZeroPressureGradient) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  // Owner-cell density set to exactly the EOS-consistent value at
  // (kReferencePressure, kReferenceTemperature) -- so the new
  // boundary-EOS evaluation and the old "just use the owner's density"
  // approximation must agree here.
  const ScalarField density(mesh.numberOfCells(),
                            thermo.density(kReferencePressure, kReferenceTemperature));

  const SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  const Index leftFaceId = mesh.boundaryPatch("left").faceIds().front();
  const Real area = mesh.face(leftFaceId).area();
  // Sf=(-area,0) on the left/inlet patch, U=(1,0) -> U.Sf = -area.
  EXPECT_NEAR(massFlux[leftFaceId], density[mesh.face(leftFaceId).owner()] * (-area), 1e-9);
}

// P12-COMP-001: a Wall's velocity BC makes u_f.Sf exactly 0 there, so the
// boundary-density choice can never affect conservation at a wall --
// confirmed here with a deliberately extreme boundary pressure that
// would give a wildly different density if it mattered.
TEST(CompressibleMassFluxTest, WallBoundaryMassFluxIsZeroRegardlessOfBoundaryDensity) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  BoundaryConditionSet pressureBoundaries;
  pressureBoundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  pressureBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  // Deliberately extreme (but still physically valid) wall pressure --
  // if it affected the wall's mass flux at all, that would be a bug.
  pressureBoundaries.set(mesh, "bottom", std::make_unique<FixedValue>(5.0e6));
  pressureBoundaries.set(mesh, "top", std::make_unique<FixedValue>(5.0e6));

  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  const ThermodynamicProperties thermo = makeThermo();

  const SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  for (const std::string& patchName : {std::string("bottom"), std::string("top")}) {
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      EXPECT_NEAR(massFlux[faceId], 0.0, 1e-12) << "patch " << patchName << " face " << faceId;
    }
  }
}

// EOS consistency: the boundary mass flux must correspond to *exactly*
// the EOS evaluated at that face's own boundary-interpolated absolute
// pressure/temperature -- verified independently for a non-trivial
// (non-uniform, non-zero-gradient) boundary pressure BC.
TEST(CompressibleMassFluxTest, BoundaryDensityMatchesDirectEosEvaluation) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  BoundaryConditionSet pressureBoundaries;
  const Real outletGauge = -1234.5;
  pressureBoundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  pressureBoundaries.set(mesh, "right", std::make_unique<FixedValue>(outletGauge));
  pressureBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  pressureBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));

  const VectorField velocity(mesh.numberOfCells(), Vector2{2.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  const ThermodynamicProperties thermo = makeThermo();

  const SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  const Index rightFaceId = mesh.boundaryPatch("right").faceIds().front();
  const Real expectedDensity =
      thermo.density(kReferencePressure + outletGauge, kReferenceTemperature);
  const Real area = mesh.face(rightFaceId).area();
  // Sf=(+area,0) on the right/outlet patch, U=(2,0) -> U.Sf = 2*area.
  EXPECT_NEAR(massFlux[rightFaceId], expectedDensity * 2.0 * area, 1e-6);
}

// Invalid/non-finite boundary state: in isothermal mode (no
// temperatureBoundaries) the owner cell's own temperature is used
// directly at the boundary -- a non-positive owner temperature must
// still be rejected by the EOS's own validation, propagated through the
// boundary-density path exactly like it already is for interior cells.
TEST(CompressibleMassFluxTest, ThrowsOnNonPositiveBoundaryTemperature) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  temperature[0] = -1.0;  // owner of the "left" boundary face.
  const ThermodynamicProperties thermo = makeThermo();

  EXPECT_THROW((void)calculateCompressibleMassFlux(
                   mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                   kReferencePressure, thermo, temperature, nullptr),
               InvalidArgumentError);
}

// Low-Mach/constant-state limiting behavior: with a spatially uniform
// pressure and temperature field (no gradients anywhere -- the
// degenerate case this project's own low-Mach regression exercises),
// every boundary face's EOS-evaluated density equals the uniform
// interior density exactly, matching what the pre-P12-COMP-001 owner-
// cell approximation also gave in this specific (but only this) case.
// Checked at the inlet/outlet faces, where velocity is genuinely
// nonzero -- Wall's own no-slip velocity BC already makes wall-face flux
// exactly zero regardless of density (covered by
// WallBoundaryMassFluxIsZeroRegardlessOfBoundaryDensity above), so
// including wall faces here would just be checking 0 == 0.
TEST(CompressibleMassFluxTest, UniformStateGivesUniformBoundaryDensityMatchingInterior) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  const ScalarField density(mesh.numberOfCells(), kReferenceDensity);

  const SurfaceField massFlux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  for (const std::string& patchName : {std::string("left"), std::string("right")}) {
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      const Real area = mesh.face(faceId).area();
      const Real expectedMagnitude = kReferenceDensity * 1.0 * area;
      EXPECT_NEAR(std::abs(massFlux[faceId]), expectedMagnitude, 1e-9)
          << "patch " << patchName << " face " << faceId;
    }
  }
}

TEST(CompressibleMassFluxTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{1.0, 0.0});
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  EXPECT_THROW((void)calculateCompressibleMassFlux(
                   mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                   kReferencePressure, thermo, temperature, nullptr),
               InvalidArgumentError);
}

TEST(CompressibleMassFluxTest, MismatchedDensitySizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField density(mesh.numberOfCells() + 1, 1.0);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  EXPECT_THROW((void)calculateCompressibleMassFlux(
                   mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                   kReferencePressure, thermo, temperature, nullptr),
               InvalidArgumentError);
}

TEST(CompressibleMassFluxTest, MismatchedPressureGaugeSizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const ScalarField pressureGauge(mesh.numberOfCells() + 1, 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  EXPECT_THROW((void)calculateCompressibleMassFlux(
                   mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                   kReferencePressure, thermo, temperature, nullptr),
               InvalidArgumentError);
}

TEST(CompressibleMassFluxTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells() + 1, kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  EXPECT_THROW((void)calculateCompressibleMassFlux(
                   mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                   kReferencePressure, thermo, temperature, nullptr),
               InvalidArgumentError);
}

// ---------------------------------------------------------------------------
// P17.1 -- the density VALUE precondition.
//
// The pre-P17.1 tree validated only array SIZES, so a bad cell density reached
// interpolateInternalFace (a plain weighted mean with no validation of its own) and
// then the mass flux, unexamined. Measured then, in
// results/p17/variable_density/logs/00_dryrun.log:
//
//   rho[0] = -2.0 against rho[1] = 2.0   ->  face rho_f = 0,   mDot_f = 0
//   rho[0] =  0.0                        ->  face rho_f = 1,   mDot_f = 1
//   rho[0] = NaN / +Inf / -Inf           ->  propagated
//
// The first two are why this is INPUT validation and not a downstream finiteness
// sweep: both are finite, so the allFinite() check further down -- the only protection
// there was -- cannot detect either. A face silently carrying no mass, and a plausible
// non-zero flux from an impossible state, are exactly what a finiteness test misses.
// ---------------------------------------------------------------------------

TEST(CompressibleMassFluxTest, RejectsNegativeCellDensity) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  density[0] = -kReferenceDensity;

  // The message must name the offending cell and the field, the same way
  // evaluateDensityField and IdealGasEOS already do -- a bare "invalid argument" on a
  // large mesh is not actionable.
  try {
    (void)calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                        pressureBoundaries, kReferencePressure, thermo, temperature,
                                        nullptr);
    FAIL() << "a negative cell density must be rejected, not averaged into the face density";
  } catch (const InvalidArgumentError& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("cell 0"), std::string::npos) << what;
    EXPECT_NE(what.find("density"), std::string::npos) << what;
  }
}

TEST(CompressibleMassFluxTest, RejectsZeroCellDensity) {
  // The boundary is > 0, not >= 0: rho = 0 is not a state, and on the pre-P17.1 tree
  // it produced a plausible-looking finite flux (face rho_f = 1, mDot_f = 1).
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  density[1] = 0.0;

  EXPECT_THROW((void)calculateCompressibleMassFlux(
                   mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                   kReferencePressure, thermo, temperature, nullptr),
               InvalidArgumentError);
}

TEST(CompressibleMassFluxTest, RejectsNonFiniteCellDensity) {
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  const Real badValues[] = {std::numeric_limits<Real>::quiet_NaN(),
                            std::numeric_limits<Real>::infinity(),
                            -std::numeric_limits<Real>::infinity()};
  for (const Real bad : badValues) {
    ScalarField density(mesh.numberOfCells(), kReferenceDensity);
    density[0] = bad;
    EXPECT_THROW((void)calculateCompressibleMassFlux(
                     mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
                     kReferencePressure, thermo, temperature, nullptr),
                 InvalidArgumentError)
        << "bad value: " << bad;
  }
}

TEST(CompressibleMassFluxTest, FaceDensityEvaluationRejectsInvalidCellDensity) {
  // evaluateCompressibleFaceDensity is a public entry point in its own right, and both
  // it and calculateCompressibleMassFlux share one validator -- checked directly so the
  // guarantee is not merely a property of the outer function.
  const Mesh mesh = makeChannelMesh();
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  density[0] = -1.0;

  EXPECT_THROW((void)cfd::compressible::evaluateCompressibleFaceDensity(
                   mesh, density, pressureGauge, pressureBoundaries, kReferencePressure, thermo,
                   temperature, nullptr),
               InvalidArgumentError);
}

TEST(CompressibleMassFluxTest, ValidUniformDensityIsUnaffectedByTheNewValidation) {
  // The reference measured in the dry-run before the validation existed: internal face
  // density 2, internal flux 2. Asserted exactly, so the added precondition is shown to
  // reject bad input without perturbing good input.
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  const ThermodynamicProperties thermo = makeThermo();

  const SurfaceField flux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    if (!mesh.face(f).isBoundary()) {
      EXPECT_EQ(flux[f], 2.0) << "internal face " << f;
    }
  }
}

// ---------------------------------------------------------------------------
// P17.1 -- spatial variation is genuinely consumed, and mass-flux mechanics.
// ---------------------------------------------------------------------------

TEST(CompressibleMassFluxTest, ReversingVelocityReversesMassFluxExactly) {
  // Sign and direction. Exact negation, not a tolerance: negating the velocity negates
  // dot(U_f, Sf) and leaves the face density untouched, so every flux is the exact IEEE
  // negation of its counterpart.
  const Mesh mesh = makeChannelMesh();
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();
  ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  density[0] = 1.5;  // non-uniform, so this is not a check about a symmetric field

  const auto forwardBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto reverseBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{-1.0, 0.0});
  const VectorField forward(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const VectorField reverse(mesh.numberOfCells(), Vector2{-1.0, 0.0});

  const SurfaceField a = calculateCompressibleMassFlux(
      mesh, forward, density, forwardBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);
  const SurfaceField b = calculateCompressibleMassFlux(
      mesh, reverse, density, reverseBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) EXPECT_EQ(b[f], -a[f]) << "face " << f;
}

TEST(CompressibleMassFluxTest, FaceDensityIsRecomputedPerCallNotCached) {
  // Staleness: mutating the cell field must change the next call's flux. If a face
  // density were cached anywhere, this would silently keep the old value -- the exact
  // failure mode the "one authoritative mass-flux path" invariant exists to prevent.
  const Mesh mesh = makeChannelMesh();
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);
  const ScalarField temperature(mesh.numberOfCells(), kReferenceTemperature);
  const ThermodynamicProperties thermo = makeThermo();

  ScalarField density(mesh.numberOfCells(), kReferenceDensity);
  const SurfaceField before = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  density[0] = 4.0 * kReferenceDensity;
  const SurfaceField after = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  Index changed = 0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    if (after[f] != before[f]) ++changed;
  }
  EXPECT_GT(changed, 0) << "the face density did not follow the cell field -- stale or cached";
}

TEST(CompressibleMassFluxTest, RowVaryingDensityConservesMassAndScalesFluxPerRow) {
  // P17.1 conservation, constructed so the discrete answer is EXACT rather than
  // toleranced, while the density genuinely varies.
  //
  // Setup: a 3x2 Cartesian mesh. Temperature varies by ROW only and each cell density
  // is the EOS value at that row's temperature, so the field is thermodynamically
  // self-consistent -- including at boundary faces, where in isothermal mode (no
  // temperature boundaries) the face takes its owner cell's own temperature and the EOS
  // therefore reproduces that row's density exactly. Velocity is uniform along +x.
  //
  // Consequences, all exact:
  //   * a cell's left and right faces lie in the same row, so both take the same face
  //     density; their fluxes are equal and opposite and cancel bit-for-bit;
  //   * top and bottom faces have a normal along y while U has no y component, so they
  //     carry exactly zero;
  //   * so every per-cell imbalance is exactly 0, with a non-uniform density.
  //
  // The DISCRIMINATING clause is the second block: each row's x-normal face flux must
  // equal that row's OWN rho * U * A. Conservation alone cannot detect a face density
  // collapsed to a single constant -- the fluxes would still cancel -- so the per-row
  // magnitude is what proves the local density is really being consumed.
  const Index nx = 3;
  const Index ny = 2;
  const Real lx = 3.0;
  const Real ly = 2.0;
  const Real u = 2.0;
  const Real tBottom = 300.0;
  const Real tTop = 600.0;  // exactly 2x, so the densities are exactly 2:1

  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
  const ThermodynamicProperties thermo = makeThermo();

  const Real rhoBottom = thermo.density(kReferencePressure, tBottom);
  const Real rhoTop = thermo.density(kReferencePressure, tTop);
  ASSERT_EQ(rhoBottom, 2.0 * rhoTop) << "a 2:1 temperature ratio must give an exact 1:2 density";
  ASSERT_GT(rhoBottom - rhoTop, 0.0) << "density must genuinely vary across the mesh";

  ScalarField density(mesh.numberOfCells());
  ScalarField temperature(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const bool top = cell.centroid().y > 0.5 * ly;
    temperature[cell.id()] = top ? tTop : tBottom;
    density[cell.id()] = top ? rhoTop : rhoBottom;
  }

  const VectorField velocity(mesh.numberOfCells(), Vector2{u, 0.0});
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{u, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const ScalarField pressureGauge(mesh.numberOfCells(), 0.0);

  const SurfaceField flux = calculateCompressibleMassFlux(
      mesh, velocity, density, velocityBoundaries, pressureGauge, pressureBoundaries,
      kReferencePressure, thermo, temperature, nullptr);

  // (1) conservation -- exactly zero per cell, and exactly zero globally.
  const cfd::physics::ContinuityResult continuity = cfd::physics::evaluateContinuity(mesh, flux);
  for (const auto& cell : mesh.cells()) {
    EXPECT_EQ(continuity.cellImbalance[cell.id()], 0.0) << "cell " << cell.id();
  }
  EXPECT_EQ(continuity.globalNetFlux, 0.0);
  EXPECT_EQ(continuity.totalAbsoluteImbalance, 0.0);

  // (2) the discriminating clause -- each row's x-normal faces carry that row's own
  // density. A constant face density would still pass (1) but fail here.
  Index checkedBottom = 0;
  Index checkedTop = 0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    if (face.areaVector().x == 0.0) continue;  // skip y-normal faces
    const bool top = mesh.cell(face.owner()).centroid().y > 0.5 * ly;
    const Real expected = (top ? rhoTop : rhoBottom) * u * std::abs(face.areaVector().x);
    EXPECT_NEAR(std::abs(flux[f]), expected, 1e-12 * expected) << "face " << f;
    if (top) {
      ++checkedTop;
    } else {
      ++checkedBottom;
    }
  }
  EXPECT_GT(checkedBottom, 0);
  EXPECT_GT(checkedTop, 0);
}

// ---------------------------------------------------------------------------
// P17.3 -- the rho(x) U(x) = const manufactured case, as a REFINEMENT study.
//
// This is deliberately NOT an exactness test, and that distinction is the point. With
// linear face interpolation the discrete flux is
//
//     rho_f U_f = mean(rho) * mean(U)   !=   mean(rho * U)
//
// so a cell's left and right face fluxes differ and the imbalance is O(h^2), NOT zero. A
// test asserting exact zero here would fail a CORRECT implementation. What must hold is
// that the imbalance DECREASES at second order under refinement.
//
// Measured in the P17.3 dry-run (logs/00_dryrun.log group G):
//     nx  8 -> 16 -> 32 -> 64     ratio 3.947, 3.971, 3.985    converging to 4
// ---------------------------------------------------------------------------
TEST(CompressibleMassFluxTest, RhoTimesUConstantConvergesAtSecondOrder) {
  const Real gasConstant = kGasConstant;
  const ThermodynamicProperties thermo = makeThermo();
  const Real pRef = kReferencePressure;
  const Real massFlowPerArea = 2.0;

  std::vector<Real> imbalance;
  for (const Index nx : {8, 16, 32, 64}) {
    const Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, 1.0, 1.0 / static_cast<Real>(nx));
    const Index n = mesh.numberOfCells();
    ScalarField density(n);
    ScalarField temperature(n);
    const ScalarField pressureGauge(n, 0.0);
    VectorField velocity(n);
    for (const auto& cell : mesh.cells()) {
      // rho varies linearly in x; U = C/rho so that rho*U is analytically constant.
      const Real r = 1.0 + 0.5 * cell.centroid().x;
      density[cell.id()] = r;
      velocity[cell.id()] = Vector2{massFlowPerArea / r, 0.0};
      // keep the thermodynamic state self-consistent with the EOS
      temperature[cell.id()] = pRef / (r * gasConstant);
    }

    BoundaryConditionSet velocityBoundaries;
    velocityBoundaries.set(mesh, "left",
                           std::make_unique<Inlet>(Vector2{massFlowPerArea / 1.0, 0.0}));
    velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
    velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
    velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
    const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

    const SurfaceField flux =
        calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                      pressureBoundaries, pRef, thermo, temperature, nullptr);
    const cfd::physics::ContinuityResult c = cfd::physics::evaluateContinuity(mesh, flux);
    imbalance.push_back(c.maxCellImbalance);

    // The density must genuinely vary, or the test proves nothing about variable density.
    Real minRho = density[0], maxRho = density[0];
    for (const auto& cell : mesh.cells()) {
      minRho = std::min(minRho, density[cell.id()]);
      maxRho = std::max(maxRho, density[cell.id()]);
    }
    EXPECT_GT(maxRho - minRho, 0.3) << "nx=" << nx;
  }

  ASSERT_EQ(imbalance.size(), 4u);
  for (std::size_t i = 0; i < imbalance.size(); ++i) {
    EXPECT_GT(imbalance[i], 0.0) << "level " << i
                                 << ": a non-zero imbalance is EXPECTED here -- if it were "
                                    "exactly zero the test would not be measuring O(h^2)";
  }

  // Monotone decrease, and second order at the finest (most asymptotic) pair.
  for (std::size_t i = 1; i < imbalance.size(); ++i) {
    EXPECT_LT(imbalance[i], imbalance[i - 1]) << "level " << i;
  }
  const Real finestRatio = imbalance[2] / imbalance[3];
  // Second order gives 4 per halving of h; first order gives 2. A threshold of 3.5 admits
  // pre-asymptotic behaviour while excluding first order by a wide margin. Derived from the
  // orders themselves, not fitted to the measurement (which was 3.985).
  EXPECT_GT(finestRatio, 3.5) << "finest refinement ratio " << finestRatio
                              << " -- second order requires ~4, first order would give ~2";
}
