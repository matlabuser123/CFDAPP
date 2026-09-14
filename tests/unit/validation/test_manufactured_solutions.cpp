// P12-NUM-006: unit-level checks of the system-level MMS building blocks --
// the analytical manufactured fields and forcing terms
// (tests/unit/discretization/ManufacturedFields.hpp, namespace mms), the
// generic volumetric source contributions they enter through, the one
// authoritative error-norm / pressure-gauge implementation
// (cfd/validation/ErrorNorms.hpp) and the MMS report
// (cfd/validation/ManufacturedSolutionStudy.hpp).
//
// Independence of the forcing: ManufacturedFieldsTest.* re-derive every
// derivative and forcing term from the CONTINUOUS closed-form functions
// with 4th-order central finite differences (step 1e-3, no mesh, no
// discrete operator of the code under test) and compare with the
// hand-derived analytical expressions. Agreement to ~1e-7 confirms the
// derivation; the FD step bounds are stated per check.
#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalSolver.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/ManufacturedSolutionStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
namespace mms = cfd::test::mms;
namespace validation = cfd::validation;

namespace {

constexpr Real kStep = 1e-3;

// 4th-order central differences of a continuous function of (x, y).
template <typename F>
Real ddx(const F& f, const Vector2& p) {
  const auto at = [&](Real dx) { return f(Vector2{p.x + dx, p.y}); };
  return (at(-2 * kStep) - (8 * at(-kStep)) + (8 * at(kStep)) - at(2 * kStep)) / (12 * kStep);
}
template <typename F>
Real ddy(const F& f, const Vector2& p) {
  const auto at = [&](Real dy) { return f(Vector2{p.x, p.y + dy}); };
  return (at(-2 * kStep) - (8 * at(-kStep)) + (8 * at(kStep)) - at(2 * kStep)) / (12 * kStep);
}
template <typename F>
Real d2dx2(const F& f, const Vector2& p) {
  const auto at = [&](Real dx) { return f(Vector2{p.x + dx, p.y}); };
  return (-at(-2 * kStep) + (16 * at(-kStep)) - (30 * at(0)) + (16 * at(kStep)) - at(2 * kStep)) /
         (12 * kStep * kStep);
}
template <typename F>
Real d2dy2(const F& f, const Vector2& p) {
  const auto at = [&](Real dy) { return f(Vector2{p.x, p.y + dy}); };
  return (-at(-2 * kStep) + (16 * at(-kStep)) - (30 * at(0)) + (16 * at(kStep)) - at(2 * kStep)) /
         (12 * kStep * kStep);
}

// Interior sample points (the FD stencil must stay inside the smooth
// closed forms -- they are smooth everywhere, so the box edge is fine too).
std::vector<Vector2> samplePoints() {
  std::vector<Vector2> points;
  for (int i = 0; i <= 10; ++i) {
    for (int j = 0; j <= 10; ++j) points.push_back(Vector2{0.1 * i, 0.1 * j});
  }
  points.push_back(Vector2{0.137, 0.861});
  points.push_back(Vector2{0.733, 0.291});
  return points;
}

// FD error bounds (step 1e-3): first derivatives ~1e-10, second ~1e-9
// (round-off eps*|f|/h^2 dominates) -- 1e-7 relative leaves margin while
// still catching any sign/coefficient slip (which is O(1)).
void expectClose(Real analytical, Real finiteDifference, const char* what, const Vector2& p) {
  EXPECT_NEAR(analytical, finiteDifference, 1e-7 * std::max(1.0, std::abs(analytical)))
      << what << " at (" << p.x << ", " << p.y << ")";
}

const auto uOf = [](const Vector2& p) { return mms::velocity(p).x; };
const auto vOf = [](const Vector2& p) { return mms::velocity(p).y; };

}  // namespace

// --- Analytical fields -------------------------------------------------------

TEST(ManufacturedFieldsTest, VelocityIsAnalyticallyDivergenceFree) {
  int bothNonTrivial = 0;
  int interiorPoints = 0;
  for (const Vector2& p : samplePoints()) {
    const auto d = mms::vortexVelocity(p);
    // Analytical: u_x + v_y = X'Y'/pi - X'Y'/pi, identically zero.
    EXPECT_NEAR(d.dx.x + d.dy.y, 0.0, 1e-13);
    // The velocity really is the streamfunction's curl (FD of psi).
    expectClose(d.value.x, ddy(mms::streamfunction, p), "u = psi_y", p);
    expectClose(d.value.y, -ddx(mms::streamfunction, p), "v = -psi_x", p);
    // ... and the velocity closed form is divergence-free by FD too.
    EXPECT_NEAR(ddx(uOf, p) + ddy(vOf, p), 0.0, 1e-8);
    const bool interior = p.x > 0.05 && p.x < 0.95 && p.y > 0.05 && p.y < 0.95;
    if (interior) {
      ++interiorPoints;
      if (std::abs(d.value.x) > 0.02 && std::abs(d.value.y) > 0.02) ++bothNonTrivial;
    }
  }
  // Both components non-zero over most of the domain (they vanish only on
  // isolated lines, e.g. u = 0 where Y' = 0).
  EXPECT_GT(static_cast<Real>(bothNonTrivial) / interiorPoints, 0.75);
  // Closed box: zero normal velocity on every wall, non-zero tangential.
  Real maxTangential = 0.0;
  for (int k = 0; k <= 20; ++k) {
    const Real s = 0.05 * k;
    EXPECT_NEAR(mms::velocity(Vector2{0.0, s}).x, 0.0, 1e-15);
    EXPECT_NEAR(mms::velocity(Vector2{1.0, s}).x, 0.0, 1e-15);
    EXPECT_NEAR(mms::velocity(Vector2{s, 0.0}).y, 0.0, 1e-15);
    EXPECT_NEAR(mms::velocity(Vector2{s, 1.0}).y, 0.0, 1e-15);
    maxTangential = std::max({maxTangential, std::abs(mms::velocity(Vector2{0.0, s}).y),
                              std::abs(mms::velocity(Vector2{s, 1.0}).x)});
  }
  EXPECT_GT(maxTangential, 0.5);
}

TEST(ManufacturedFieldsTest, VelocityDerivativesExact) {
  for (const Vector2& p : samplePoints()) {
    const auto d = mms::vortexVelocity(p);
    expectClose(d.dx.x, ddx(uOf, p), "u_x", p);
    expectClose(d.dy.x, ddy(uOf, p), "u_y", p);
    expectClose(d.dx.y, ddx(vOf, p), "v_x", p);
    expectClose(d.dy.y, ddy(vOf, p), "v_y", p);
    expectClose(d.dxx.x, d2dx2(uOf, p), "u_xx", p);
    expectClose(d.dyy.x, d2dy2(uOf, p), "u_yy", p);
    expectClose(d.dxx.y, d2dx2(vOf, p), "v_xx", p);
    expectClose(d.dyy.y, d2dy2(vOf, p), "v_yy", p);
  }
}

TEST(ManufacturedFieldsTest, PressureGradientExact) {
  int bothNonZero = 0;
  for (const Vector2& p : samplePoints()) {
    const Vector2 g = mms::pressureGradient(p);
    expectClose(g.x, ddx(mms::pressure, p), "p_x", p);
    expectClose(g.y, ddy(mms::pressure, p), "p_y", p);
    if (std::abs(g.x) > 0.05 && std::abs(g.y) > 0.05) ++bothNonZero;
  }
  EXPECT_GT(bothNonZero, static_cast<int>(samplePoints().size() / 2));
  // Non-trivial Neumann data: the normal derivative is non-zero on walls.
  EXPECT_GT(std::abs(mms::pressureGradient(Vector2{0.0, 0.8}).x), 0.1);
  EXPECT_GT(std::abs(mms::pressureGradient(Vector2{0.3, 1.0}).y), 0.1);
}

TEST(ManufacturedFieldsTest, MomentumForcingExact) {
  // f re-derived independently from the continuous PDE with FD derivatives
  // of the closed-form u, v, p (not the hand-derived derivatives).
  for (const Real density : {1.0, 1.7}) {
    for (const Real viscosity : {0.1, 0.02}) {
      for (const Vector2& p : samplePoints()) {
        const Vector2 u = mms::velocity(p);
        const Real fx = (density * ((u.x * ddx(uOf, p)) + (u.y * ddy(uOf, p)))) +
                        ddx(mms::pressure, p) - (viscosity * (d2dx2(uOf, p) + d2dy2(uOf, p)));
        const Real fy = (density * ((u.x * ddx(vOf, p)) + (u.y * ddy(vOf, p)))) +
                        ddy(mms::pressure, p) - (viscosity * (d2dx2(vOf, p) + d2dy2(vOf, p)));
        const Vector2 f = mms::momentumForcing(p, density, viscosity);
        expectClose(f.x, fx, "f_x", p);
        expectClose(f.y, fy, "f_y", p);
      }
    }
  }
}

TEST(ManufacturedFieldsTest, ScalarForcingExact) {
  const Real rho = 1.2;
  const Real cp = 1.5;
  const Real k = 0.2;
  for (const Vector2& p : samplePoints()) {
    expectClose(mms::scalarGradient(p).x, ddx(mms::scalar, p), "phi_x", p);
    expectClose(mms::scalarGradient(p).y, ddy(mms::scalar, p), "phi_y", p);
    expectClose(mms::scalarLaplacian(p), d2dx2(mms::scalar, p) + d2dy2(mms::scalar, p), "lap phi",
                p);
    const Vector2 U = mms::advectingVelocity(p);
    const Real q = (rho * cp * ((U.x * ddx(mms::scalar, p)) + (U.y * ddy(mms::scalar, p)))) -
                   (k * (d2dx2(mms::scalar, p) + d2dy2(mms::scalar, p)));
    expectClose(mms::scalarForcing(p, rho, cp, k), q, "Q", p);
  }
  // The advecting velocity is divergence-free and has real through-flow.
  const auto Ux = [](const Vector2& p) { return mms::advectingVelocity(p).x; };
  const auto Uy = [](const Vector2& p) { return mms::advectingVelocity(p).y; };
  for (const Vector2& p : samplePoints()) EXPECT_NEAR(ddx(Ux, p) + ddy(Uy, p), 0.0, 1e-8);
  EXPECT_GT(mms::advectingVelocity(Vector2{0.0, 0.5}).x, 0.5);  // inflow, left wall
}

// Compressible (isothermal ideal gas): U = m / rho with the vortex mass
// flux m; every derivative and the forcing re-derived by FD of the
// continuous closed forms; the density is the production EOS's.
TEST(ManufacturedFieldsTest, CompressibleForcingExact) {
  // P_ref = R T0 = 5 (strong density coupling) and 20 (the system study's
  // CompressibleSimpleMMS parameters).
  for (const Real pRef : {5.0, 20.0}) {
    const Real rt = pRef;  // R T0 with R = 1
    const cfd::compressible::ThermodynamicProperties thermo(1.0, 1005.0);
    const auto Ux = [&](const Vector2& p) {
      return mms::compressibleVelocity(p, pRef, rt).value.x;
    };
    const auto Uy = [&](const Vector2& p) {
      return mms::compressibleVelocity(p, pRef, rt).value.y;
    };
    const auto rhoU = [&](const Vector2& p) {
      return mms::compressibleDensity(p, pRef, rt) * mms::compressibleVelocity(p, pRef, rt).value;
    };
    const Real mu = 0.1;
    for (const Vector2& p : samplePoints()) {
      // EOS consistency: rho = p_abs / (R T) of the production ideal gas.
      EXPECT_NEAR(mms::compressibleDensity(p, pRef, rt),
                  thermo.density(pRef + mms::pressure(p), rt), 1e-14);
      // The mass flux is exactly the vortex field -> div(rho U) = 0.
      const Vector2 m = rhoU(p);
      EXPECT_NEAR(m.x, mms::velocity(p).x, 1e-14);
      EXPECT_NEAR(m.y, mms::velocity(p).y, 1e-14);
      const auto mx = [&](const Vector2& q) { return rhoU(q).x; };
      const auto my = [&](const Vector2& q) { return rhoU(q).y; };
      EXPECT_NEAR(ddx(mx, p) + ddy(my, p), 0.0, 1e-8);
      // Quotient-rule derivatives vs FD.
      const auto d = mms::compressibleVelocity(p, pRef, rt);
      expectClose(d.dx.x, ddx(Ux, p), "U_x", p);
      expectClose(d.dy.x, ddy(Ux, p), "U_y", p);
      expectClose(d.dx.y, ddx(Uy, p), "V_x", p);
      expectClose(d.dy.y, ddy(Uy, p), "V_y", p);
      expectClose(d.dxx.x, d2dx2(Ux, p), "U_xx", p);
      expectClose(d.dyy.x, d2dy2(Ux, p), "U_yy", p);
      expectClose(d.dxx.y, d2dx2(Uy, p), "V_xx", p);
      expectClose(d.dyy.y, d2dy2(Uy, p), "V_yy", p);
      // f = (m.grad)U + grad p - mu lap U from FD of the continuous fields.
      const Real fx = (m.x * ddx(Ux, p)) + (m.y * ddy(Ux, p)) + ddx(mms::pressure, p) -
                      (mu * (d2dx2(Ux, p) + d2dy2(Ux, p)));
      const Real fy = (m.x * ddx(Uy, p)) + (m.y * ddy(Uy, p)) + ddy(mms::pressure, p) -
                      (mu * (d2dx2(Uy, p) + d2dy2(Uy, p)));
      const Vector2 f = mms::compressibleMomentumForcing(p, pRef, rt, mu);
      expectClose(f.x, fx, "compressible f_x", p);
      expectClose(f.y, fy, "compressible f_y", p);
    }
    // Genuinely compressible: the density varies over the box (> 30 % for
    // P_ref = 5, > 10 % for 20) and div U = -m.grad(rho)/rho^2 != 0.
    Real lo = 1e9;
    Real hi = 0.0;
    for (const Vector2& p : samplePoints()) {
      lo = std::min(lo, mms::compressibleDensity(p, pRef, rt));
      hi = std::max(hi, mms::compressibleDensity(p, pRef, rt));
    }
    EXPECT_GT((hi - lo) / lo, pRef < 10.0 ? 0.3 : 0.1);
    const Vector2 q{0.3, 0.6};
    EXPECT_GT(std::abs(ddx(Ux, q) + ddy(Uy, q)), 1e-3);
  }
}

// The exact face flux psi(B) - psi(A) is the integral of U.n over the face:
// it telescopes to zero net flux per cell (to round-off) on Cartesian AND
// distorted meshes, and agrees with the midpoint value U(c).Sf to O(h^3).
TEST(ManufacturedFieldsTest, ExactFaceFluxIsDiscretelyDivergenceFree) {
  for (const bool distorted : {false, true}) {
    const Mesh mesh = distorted ? cfd::test::createDistortedQuad2D(16, 16, 1.0, 1.0, 0.25 / 16.0)
                                : cfd::mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
    cfd::fields::SurfaceField flux(mesh.numberOfFaces());
    Real maxMidpointDifference = 0.0;
    for (const auto& face : mesh.faces()) {
      flux[face.id()] = mms::exactVortexVolumeFlux(face);
      maxMidpointDifference = std::max(
          maxMidpointDifference,
          std::abs(flux[face.id()] - dot(mms::velocity(face.centroid()), face.areaVector())));
    }
    const auto continuity = cfd::physics::evaluateContinuity(mesh, flux);
    EXPECT_LT(continuity.maxCellImbalance, 1e-15) << "distorted=" << distorted;
    EXPECT_LT(std::abs(continuity.globalNetFlux), 1e-15);
    // |U''| h^3 / 24 with h = 1/16: ~1e-4 bound, measured well inside.
    EXPECT_LT(maxMidpointDifference, 2e-4);
    EXPECT_GT(maxMidpointDifference, 0.0);  // not trivially identical
  }
}

// --- Generic volumetric sources -----------------------------------------------

TEST(MMSSourceTest, CellVolumeScaling) {
  // A distorted mesh: every cell has a different volume, so V_P scaling is
  // actually exercised.
  const Mesh mesh = cfd::test::createDistortedQuad2D(12, 10, 2.0, 1.0, 0.3 / 12.0);
  const auto source = [](const Vector2& p) {
    return Vector2{1.0 + p.x + (2.0 * p.y), std::sin(3.0 * p.x)};
  };
  VectorField force(mesh.numberOfCells());
  ScalarField heat(mesh.numberOfCells());
  Real minVolume = 1e9;
  Real maxVolume = 0.0;
  for (const auto& cell : mesh.cells()) {
    force[cell.id()] = source(cell.centroid());
    heat[cell.id()] = source(cell.centroid()).x;
    minVolume = std::min(minVolume, cell.volume());
    maxVolume = std::max(maxVolume, cell.volume());
  }
  ASSERT_GT(maxVolume / minVolume, 1.1);  // measured 1.17: volumes genuinely differ

  cfd::algebra::Vector rhsU(mesh.numberOfCells(), 0.0);
  cfd::algebra::Vector rhsV(mesh.numberOfCells(), 0.0);
  cfd::algebra::Vector rhsT(mesh.numberOfCells(), 0.0);
  cfd::physics::assembleMomentumSourceContribution(mesh, force, cfd::physics::VelocityComponent::U,
                                                   rhsU);
  cfd::physics::assembleMomentumSourceContribution(mesh, force, cfd::physics::VelocityComponent::V,
                                                   rhsV);
  cfd::thermal::assembleThermalSourceContribution(mesh, heat, rhsT);
  Real integralU = 0.0;
  for (const auto& cell : mesh.cells()) {
    EXPECT_EQ(rhsU[cell.id()], cell.volume() * force[cell.id()].x);
    EXPECT_EQ(rhsV[cell.id()], cell.volume() * force[cell.id()].y);
    EXPECT_EQ(rhsT[cell.id()], heat[cell.id()] * cell.volume());
    integralU += rhsU[cell.id()];
  }
  // Midpoint rule of the linear f_x = 1 + x + 2y over [0,2]x[0,1] is exact
  // for straight-sided cells with true centroids: 2 + 2 + 2 = 6.
  EXPECT_NEAR(integralU, 6.0, 1e-12);

  // A uniform field reproduces the uniform-source overload bit for bit.
  ScalarField uniform(mesh.numberOfCells(), 2.5);
  cfd::algebra::Vector a(mesh.numberOfCells(), 0.0);
  cfd::algebra::Vector b(mesh.numberOfCells(), 0.0);
  cfd::thermal::assembleThermalSourceContribution(mesh, uniform, a);
  cfd::thermal::assembleThermalSourceContribution(mesh, 2.5, b);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) EXPECT_EQ(a[i], b[i]);

  // Validation: size and finiteness.
  VectorField wrongSize(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  EXPECT_THROW(cfd::physics::assembleMomentumSourceContribution(
                   mesh, wrongSize, cfd::physics::VelocityComponent::U, rhsU),
               cfd::InvalidArgumentError);
  VectorField nonFinite = force;
  nonFinite[3].y = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(cfd::physics::assembleMomentumSourceContribution(
                   mesh, nonFinite, cfd::physics::VelocityComponent::U, rhsU),
               cfd::InvalidArgumentError);
  ScalarField badHeat = heat;
  badHeat[0] = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(cfd::thermal::assembleThermalSourceContribution(mesh, badHeat, rhsT),
               cfd::InvalidArgumentError);
}

// The source enters the production relaxed-momentum assembly as a pure RHS
// term: the matrix is unchanged, rhs grows by exactly V_P f_P, and a null
// source is the pre-existing assembly.
TEST(MMSSourceTest, RelaxedMomentumSourceIsAPureRhsTerm) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(6, 5, 1.0, 1.0);
  const auto vbc = mms::makeExactVelocityBoundaries(mesh);
  const auto pbc = mms::makeExactPressureBoundaries(mesh);
  const cfd::physics::FluidProperties fluid(1.0, 0.1);
  const VectorField velocity = mms::sampleVector(mesh, mms::velocity);
  const ScalarField pressure = mms::sampleScalar(mesh, mms::pressure);
  const ScalarField mu(mesh.numberOfCells(), 0.1);
  const auto flux = cfd::physics::calculateMassFlux(mesh, velocity, fluid, vbc);
  ScalarField previousU(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) previousU[i] = velocity[i].x;
  const VectorField force =
      mms::sampleVector(mesh, [](const Vector2& p) { return mms::momentumForcing(p, 1.0, 0.1); });

  const auto base = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
      mesh, velocity, pressure, flux, mu, vbc, pbc, cfd::physics::VelocityComponent::U, previousU,
      0.7);
  const auto withSource = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
      mesh, velocity, pressure, flux, mu, vbc, pbc, cfd::physics::VelocityComponent::U, previousU,
      0.7, nullptr, nullptr, cfd::discretization::ConvectionScheme::Upwind,
      cfd::discretization::GradientScheme::GreenGauss, false, nullptr, &force);
  for (Index row = 0; row < mesh.numberOfCells(); ++row) {
    EXPECT_EQ(base.diagonal[row], withSource.diagonal[row]);
    EXPECT_NEAR(withSource.system.rhs()[row] - base.system.rhs()[row],
                mesh.cell(row).volume() * force[row].x, 1e-14);
  }
}

TEST(MMSSourceTest, SolversRejectMalformedSourceFields) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto vbc = mms::makeExactVelocityBoundaries(mesh);
  const auto pbc = mms::makeExactPressureBoundaries(mesh);
  const cfd::physics::FluidProperties fluid(1.0, 0.1);
  cfd::pressure_velocity::SIMPLE simple(cfd::pressure_velocity::SIMPLESettings{}, 0);
  VectorField wrongSize(mesh.numberOfCells() - 1, Vector2{0.0, 0.0});
  simple.setMomentumSource(&wrongSize);
  EXPECT_EQ(simple.momentumSource(), &wrongSize);
  EXPECT_EQ(simple
                .solve(mesh, fluid, vbc, pbc, VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                       ScalarField(mesh.numberOfCells(), 0.0))
                .status,
            cfd::pressure_velocity::SIMPLEStatus::InvalidConfiguration);
  VectorField nonFinite(mesh.numberOfCells(), Vector2{0.0, 0.0});
  nonFinite[2].x = std::numeric_limits<Real>::quiet_NaN();
  simple.setMomentumSource(&nonFinite);
  EXPECT_EQ(simple
                .solve(mesh, fluid, vbc, pbc, VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                       ScalarField(mesh.numberOfCells(), 0.0))
                .status,
            cfd::pressure_velocity::SIMPLEStatus::InvalidConfiguration);

  const auto tbc = cfd::test::makeExactBoundaries(mesh, mms::scalar);
  const auto flux = mms::exactAdvectingMassFlux(mesh, 1.0);
  const cfd::thermal::ThermalSolver thermalSolver;
  const cfd::thermal::ThermalProperties properties(0.2, 1.0);
  EXPECT_EQ(thermalSolver
                .solve(mesh, ScalarField(mesh.numberOfCells(), 0.0), flux, properties, tbc,
                       ScalarField(mesh.numberOfCells() + 2, 0.0))
                .status,
            cfd::thermal::ThermalStatus::InvalidConfiguration);
  ScalarField badHeat(mesh.numberOfCells(), 1.0);
  badHeat[1] = std::numeric_limits<Real>::infinity();
  EXPECT_EQ(thermalSolver
                .solve(mesh, ScalarField(mesh.numberOfCells(), 0.0), flux, properties, tbc, badHeat)
                .status,
            cfd::thermal::ThermalStatus::NonFiniteState);
}

// --- Error norms ---------------------------------------------------------------

TEST(MMSErrorNormsTest, KnownField) {
  const Index n = 10;
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  // A constant error c: every norm is |c|.
  const auto constant =
      validation::computeErrorNorms(mesh, ScalarField(mesh.numberOfCells(), -0.25));
  EXPECT_NEAR(constant.l1, 0.25, 1e-15);
  EXPECT_NEAR(constant.l2, 0.25, 1e-15);
  EXPECT_DOUBLE_EQ(constant.linf, 0.25);
  EXPECT_EQ(constant.cells, n * n);
  EXPECT_NEAR(constant.volume, 1.0, 1e-14);
  // e = x at the centroids x_i = (i + 1/2)/n: mean |e| = 1/2,
  // mean e^2 = 1/3 - 1/(12 n^2), max = 1 - 1/(2n) -- exact midpoint sums.
  const ScalarField x = mms::sampleScalar(mesh, [](const Vector2& p) { return p.x; });
  const auto linear =
      validation::computeErrorNorms(mesh, x, ScalarField(mesh.numberOfCells(), 0.0));
  EXPECT_NEAR(linear.l1, 0.5, 1e-14);
  EXPECT_NEAR(linear.l2, std::sqrt((1.0 / 3.0) - (1.0 / (12.0 * n * n))), 1e-14);
  EXPECT_NEAR(linear.linf, 1.0 - (0.5 / n), 1e-14);
  // Volume weighting on unequal cells: e = 1 on cells left of x = 0.5.
  const Mesh distorted = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.3 / n);
  ScalarField indicator(distorted.numberOfCells(), 0.0);
  Real leftVolume = 0.0;
  for (const auto& cell : distorted.cells()) {
    if (cell.centroid().x < 0.5) {
      indicator[cell.id()] = 1.0;
      leftVolume += cell.volume();
    }
  }
  const auto weighted = validation::computeErrorNorms(distorted, indicator);
  EXPECT_NEAR(weighted.l1, leftVolume, 1e-14);
  EXPECT_NEAR(weighted.l2, std::sqrt(leftVolume), 1e-14);
  // Vector: a constant (3, 4) error has magnitude 5.
  const auto vector = validation::computeVectorErrorNorms(
      mesh, VectorField(mesh.numberOfCells(), Vector2{3.0, 4.0}),
      VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}));
  // (volume-weighted sums: equal to round-off, not bit-exact)
  EXPECT_NEAR(vector.x.l2, 3.0, 1e-14);
  EXPECT_NEAR(vector.y.linf, 4.0, 1e-14);
  EXPECT_NEAR(vector.magnitude.l1, 5.0, 5e-14);  // 100-term sum: ~2e-15 relative
  // Masks: the boundary ring of an n x n grid has 4n - 4 cells, two layers
  // 8n - 16; interior + ring partitions the mesh.
  const auto ring = validation::boundaryAdjacentCells(mesh, 1);
  const auto ring2 = validation::boundaryAdjacentCells(mesh, 2);
  const auto interior = validation::invertMask(ring);
  EXPECT_EQ(validation::computeErrorNorms(mesh, x, x, &ring).cells, 4 * n - 4);
  EXPECT_EQ(validation::computeErrorNorms(mesh, x, x, &ring2).cells, 8 * n - 16);
  EXPECT_EQ(validation::computeErrorNorms(mesh, x, x, &interior).cells, (n - 2) * (n - 2));
  // Rejections: non-finite values, size mismatch, empty selection.
  ScalarField bad(mesh.numberOfCells(), 0.0);
  bad[7] = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW((void)validation::computeErrorNorms(mesh, bad), cfd::InvalidArgumentError);
  EXPECT_THROW((void)validation::computeErrorNorms(mesh, ScalarField(3, 0.0)),
               cfd::InvalidArgumentError);
  const validation::CellMask none(mesh.numberOfCells(), false);
  EXPECT_THROW((void)validation::computeErrorNorms(mesh, x, &none), cfd::InvalidArgumentError);
  EXPECT_THROW((void)validation::boundaryAdjacentCells(mesh, 0), cfd::InvalidArgumentError);
}

TEST(MMSErrorNormsTest, PressureGaugeRemoval) {
  const Mesh mesh = cfd::test::createDistortedQuad2D(12, 12, 1.0, 1.0, 0.3 / 12.0);
  const ScalarField exact = mms::sampleScalar(mesh, mms::pressure);
  // Any constant offset (either field) is invisible to the gauge-invariant error.
  for (const Real offset : {0.0, 123.456, -7.0}) {
    ScalarField shifted = exact;
    for (Index i = 0; i < shifted.size(); ++i) shifted[i] += offset;
    const auto e = validation::computeGaugeInvariantErrorNorms(mesh, shifted, exact);
    EXPECT_LT(e.linf, 1e-12) << offset;
    const auto reverse = validation::computeGaugeInvariantErrorNorms(mesh, exact, shifted);
    EXPECT_LT(reverse.linf, 1e-12) << offset;
  }
  // The gauge representative has zero volume-weighted mean.
  EXPECT_NEAR(
      validation::volumeWeightedMean(mesh, validation::removeVolumeWeightedMean(mesh, exact)), 0.0,
      1e-15);
  // A genuine (non-constant) difference is NOT removed: adding
  // eps * (x - mean x) gives exactly that error after the gauge shift.
  const ScalarField x = mms::sampleScalar(mesh, [](const Vector2& p) { return p.x; });
  const ScalarField xPrime = validation::removeVolumeWeightedMean(mesh, x);
  ScalarField perturbed = exact;
  for (Index i = 0; i < perturbed.size(); ++i) perturbed[i] += 5.0 + (0.01 * x[i]);
  const auto e = validation::computeGaugeInvariantErrorNorms(mesh, perturbed, exact);
  const auto expected = validation::computeErrorNorms(mesh, xPrime);
  EXPECT_NEAR(e.l2, 0.01 * expected.l2, 1e-14);
  EXPECT_NEAR(e.linf, 0.01 * expected.linf, 1e-14);
  // Without the gauge shift the constant 5 would dominate.
  EXPECT_GT(validation::computeErrorNorms(mesh, perturbed, exact).l1, 4.9);
}

// --- MMS study / report ----------------------------------------------------------

namespace {

validation::MMSStudy syntheticStudy(Real order) {
  validation::MMSStudy study;
  study.name = "synthetic";
  study.description = "E = C h^p";
  study.manufacturedSolution = "none";
  study.coefficients = {{"mu", 0.1}};
  study.configuration = {{"scheme", "upwind"}};
  for (const Index n : std::initializer_list<Index>{8, 16, 32, 64}) {
    validation::MMSLevel level;
    level.name = std::to_string(n) + "x" + std::to_string(n);
    level.nx = n;
    level.ny = n;
    level.cells = n * n;
    level.h = 1.0 / static_cast<Real>(n);
    level.solverStatus = "Converged";
    level.accepted = true;
    level.iterations = 10 * n;
    level.massImbalance = 0.0;
    validation::ErrorNorms norms;
    norms.l1 = 0.5 * std::pow(level.h, order);
    norms.l2 = 0.7 * std::pow(level.h, order);
    norms.linf = 2.0 * std::pow(level.h, order);
    norms.cells = level.cells;
    norms.volume = 1.0;
    level.errors = {{"u", norms}};
    level.runtimeSeconds = 0.1 * static_cast<double>(n);
    study.levels.push_back(level);
  }
  return study;
}

}  // namespace

TEST(MMSStudyTest, OrdersComeFromTheSharedGridConvergenceAnalysis) {
  auto study = syntheticStudy(2.0);
  cfd::validation::GridConvergenceOptions options;
  options.formalOrder = 2.0;
  const auto order = validation::computeMMSOrder(study, "u", validation::NormKind::L2, options);
  ASSERT_EQ(order.triplets.size(), 2u);
  ASSERT_EQ(order.reductionFactors.size(), 3u);
  EXPECT_NEAR(*order.reductionFactors[0], 4.0, 1e-12);
  EXPECT_NEAR(*order.finestOrder(), 2.0, 1e-9);
  EXPECT_EQ(order.triplets.back().status, validation::GridConvergenceStatus::Asymptotic);
  // Exact limit 0: the extrapolated error vanishes for a pure power law.
  EXPECT_NEAR(*order.triplets.back().extrapolated21, 0.0, 1e-15);
  // A first-order sequence against pf = 2 is honestly not asymptotic.
  const auto first =
      validation::computeMMSOrder(syntheticStudy(1.0), "u", validation::NormKind::Linf, options);
  EXPECT_NEAR(*first.finestOrder(), 1.0, 1e-9);
  EXPECT_EQ(first.triplets.back().status,
            validation::GridConvergenceStatus::MonotonicNotAsymptotic);
  // No order from a rejected solve, a missing quantity, or < 3 levels.
  auto rejected = syntheticStudy(2.0);
  rejected.levels[1].accepted = false;
  rejected.levels[1].rejectionReason = "solver status Stagnated";
  EXPECT_THROW((void)validation::computeMMSOrder(rejected, "u", validation::NormKind::L2),
               cfd::InvalidArgumentError);
  EXPECT_THROW((void)validation::computeMMSOrder(study, "p", validation::NormKind::L2),
               cfd::InvalidArgumentError);
  study.levels.resize(2);
  EXPECT_THROW((void)validation::computeMMSOrder(study, "u", validation::NormKind::L2),
               cfd::InvalidArgumentError);
}

TEST(MMSStudyTest, ReportIsDeterministicExceptRuntime) {
  auto a = syntheticStudy(2.0);
  a.orders.push_back(validation::computeMMSOrder(a, "u", validation::NormKind::L2));
  a.gates.push_back({"order", true, "p = 2"});
  auto b = a;
  for (auto& level : b.levels) level.runtimeSeconds *= 3.0;  // only the wall time differs
  const std::string jsonA = validation::mmsReportJson(a);
  const std::string jsonB = validation::mmsReportJson(b);
  EXPECT_NE(jsonA, jsonB);
  auto docA = nlohmann::json::parse(jsonA);
  auto docB = nlohmann::json::parse(jsonB);
  docA.erase("runtime");
  docB.erase("runtime");
  EXPECT_EQ(docA, docB);
  EXPECT_EQ(jsonA, validation::mmsReportJson(a));  // byte-identical on repeat
  EXPECT_EQ(docA["format_version"], 1);
  EXPECT_EQ(docA["levels"].size(), 4u);
  EXPECT_TRUE(docA["all_gates_passed"].get<bool>());
  EXPECT_NEAR(docA["orders"][0]["finest_observed_order"].get<double>(), 2.0, 1e-9);
  EXPECT_TRUE(docA["levels"][0]["errors"]["u"]["l2"].is_number());
  EXPECT_FALSE(docA.contains("runtime"));
  EXPECT_NE(validation::mmsReportMarkdown(a).find("| 64x64 |"), std::string::npos);
  // Report hygiene: no trailing whitespace, exactly one final newline.
  const std::string md = validation::mmsReportMarkdown(a);
  EXPECT_EQ(md.back(), '\n');
  EXPECT_EQ(md.find("\n\n", md.size() - 2), std::string::npos);
  EXPECT_EQ(md.find(" \n"), std::string::npos);
  // A failed gate is reported as such.
  a.gates.push_back({"extra", false, "failed"});
  EXPECT_FALSE(nlohmann::json::parse(validation::mmsReportJson(a))["all_gates_passed"].get<bool>());
}
