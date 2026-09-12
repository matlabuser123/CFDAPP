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

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
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

ThermodynamicProperties makeThermo() { return ThermodynamicProperties(kGasConstant, kSpecificHeatPressure); }

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

  const SurfaceField massFlux =
      calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                    pressureBoundaries, kReferencePressure, thermo, temperature,
                                    nullptr);

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

  const SurfaceField massFlux =
      calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                    pressureBoundaries, kReferencePressure, thermo, temperature,
                                    nullptr);

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
  const ScalarField density(mesh.numberOfCells(), thermo.density(kReferencePressure,
                                                                  kReferenceTemperature));

  const SurfaceField massFlux =
      calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                    pressureBoundaries, kReferencePressure, thermo, temperature,
                                    nullptr);

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

  const SurfaceField massFlux =
      calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                    pressureBoundaries, kReferencePressure, thermo, temperature,
                                    nullptr);

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

  const SurfaceField massFlux =
      calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                    pressureBoundaries, kReferencePressure, thermo, temperature,
                                    nullptr);

  const Index rightFaceId = mesh.boundaryPatch("right").faceIds().front();
  const Real expectedDensity = thermo.density(kReferencePressure + outletGauge, kReferenceTemperature);
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

  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries,
                                                    pressureGauge, pressureBoundaries,
                                                    kReferencePressure, thermo, temperature,
                                                    nullptr),
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

  const SurfaceField massFlux =
      calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries, pressureGauge,
                                    pressureBoundaries, kReferencePressure, thermo, temperature,
                                    nullptr);

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
  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries,
                                                    pressureGauge, pressureBoundaries,
                                                    kReferencePressure, thermo, temperature,
                                                    nullptr),
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
  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries,
                                                    pressureGauge, pressureBoundaries,
                                                    kReferencePressure, thermo, temperature,
                                                    nullptr),
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
  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries,
                                                    pressureGauge, pressureBoundaries,
                                                    kReferencePressure, thermo, temperature,
                                                    nullptr),
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
  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, velocityBoundaries,
                                                    pressureGauge, pressureBoundaries,
                                                    kReferencePressure, thermo, temperature,
                                                    nullptr),
               InvalidArgumentError);
}
