// P17.8 -- the density-based compressible solver. Frozen gate:
// results/p17/density_based_solver/acceptance_gate.md
// (sha256 a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e)
//
// The criterion ids in the test names are that gate's.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/solver/CFL.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

namespace {

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Real;
using cfd::Vector3;
using cfd::compressible::BoundaryKind;
using cfd::compressible::ConservedField;
using cfd::compressible::DensityBasedBoundaries;
using cfd::compressible::DensityBasedBoundary;
using cfd::compressible::DensityBasedResult;
using cfd::compressible::DensityBasedSettings;
using cfd::compressible::DensityBasedSolver;
using cfd::compressible::DensityBasedStatus;
using cfd::compressible::exteriorState;
using cfd::compressible::FluxVector;
using cfd::compressible::hllcFluxChecked;
using cfd::compressible::integrateOverArea;
using cfd::compressible::PrimitiveState;
using cfd::compressible::ReconstructedFaceState;
using cfd::compressible::reconstructFirstOrder;
using cfd::compressible::reconstructFirstOrderBoundary;
using cfd::compressible::ResidualField;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TimeSteppingMode;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::AxisGrading;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

constexpr Real kEps = 2.2204460492503131e-16;
constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;
constexpr Real kPRef = 101325.0;

// The ZERO reference, so rhoE = p/(gamma-1) + ke exactly -- the same convention P17.7's reference
// used, which lets the two phases' numbers be compared directly.
TotalEnergyState air() {
  const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(kR, kCp), SensibleEnthalpy(cp, 0.0, 0.0));
}

DensityBasedSettings globalSettings(Real cfl = 0.4, Real endTime = 1.0e-4) {
  DensityBasedSettings s{};
  s.cfl = cfl;
  s.mode = TimeSteppingMode::GlobalPhysical;
  s.endTime = endTime;
  s.maxIterations = 500;
  return s;
}

// Every patch given the same treatment. assembleResidual FAILS CLOSED on an unmapped patch, so this
// must be complete.
DensityBasedBoundaries allPatches(const Mesh& mesh, BoundaryKind kind,
                                  const PrimitiveState& prescribed = PrimitiveState{}) {
  DensityBasedBoundaries b;
  for (const auto& patch : mesh.boundaryPatches()) {
    DensityBasedBoundary entry{};
    entry.kind = kind;
    entry.prescribed = prescribed;
    b[patch.name()] = entry;
  }
  return b;
}

struct UniformCase {
  Mesh mesh;
  ConservedField q;
  std::vector<PrimitiveState> primitives;
};

UniformCase uniformCase(const DensityBasedSolver& solver, Index nx, Index ny, Real lx, Real ly,
                        Vector3 velocity, Real density = 1.2, Real temperature = 300.0) {
  Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = density;
    u[i] = velocity;
    t[i] = temperature;
  }
  ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  auto prim = solver.toPrimitive(q);
  return {std::move(mesh), std::move(q), std::move(prim)};
}

Real totalOf(const Mesh& mesh, const ScalarField& f) {
  Real s = 0.0;
  for (Index i = 0; i < static_cast<Index>(mesh.numberOfCells()); ++i) {
    s += f[i] * mesh.cell(i).volume();
  }
  return s;
}

}  // namespace

// =============================================================================================
// Group Q -- the conserved state and its conversions
// =============================================================================================

TEST(DensityBasedStateTest, Q1_PrimitiveConservedRoundTripIsExact) {
  const DensityBasedSolver solver(globalSettings(), air());
  Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.0 + 0.1 * static_cast<Real>(i);
    u[i] = Vector3{30.0 + static_cast<Real>(i), -12.0, 7.0};
    t[i] = 300.0 + 5.0 * static_cast<Real>(i);
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto back = solver.toPrimitive(q);

  Real worstT = 0.0;
  Real worstP = 0.0;
  for (Index i = 0; i < n; ++i) {
    // rho and rhoE are carried through unchanged: BITWISE
    EXPECT_EQ(q.density[i], rho[i]) << "cell " << i;
    EXPECT_EQ(back[i].density, rho[i]) << "cell " << i;
    EXPECT_EQ(back[i].totalEnergyDensity, q.totalEnergyDensity[i]) << "cell " << i;
    // T and p pass through the closed-form inverse and the EOS: 8 eps
    const Real et = std::abs(back[i].temperature - t[i]) / t[i];
    worstT = std::max(worstT, et);
    EXPECT_LE(et, 8.0 * kEps) << "cell " << i;
    const Real pExpected = 1.0;  // relative measure below
    (void)pExpected;
    const Real ep = std::abs(back[i].pressure - rho[i] * kR * t[i]) / (rho[i] * kR * t[i]);
    worstP = std::max(worstP, ep);
    EXPECT_LE(ep, 8.0 * kEps) << "cell " << i;
  }
  std::printf(
      "\n  Q1 round trip: rho and rhoE BITWISE; worst relative T error %.3e, p error %.3e\n",
      worstT, worstP);
}

// =============================================================================================
// Group R -- the residual
// =============================================================================================

// R1: the owner and neighbour contributions are exact negations, by construction.
TEST(DensityBasedResidualTest, R1_OwnerAndNeighbourContributionsCancelExactly) {
  const DensityBasedSolver solver(globalSettings(), air());
  // A NON-uniform state, so the fluxes are genuinely non-zero and the cancellation is not trivial.
  Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.0 + 0.2 * static_cast<Real>(i % 3);
    u[i] = Vector3{50.0 * static_cast<Real>(1 + i % 2), 10.0, 0.0};
    t[i] = 300.0 + 20.0 * static_cast<Real>(i % 4);
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto prim = solver.toPrimitive(q);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);

  // Assemble the FULL residual, then assemble it again with only one face at a time to extract that
  // face's contribution -- which is what actually proves the cancellation per face.
  const ResidualField full = solver.assembleResidual(mesh, prim, boundaries);

  // Sum the residual over all cells. Every internal face contributes +F and -F, and boundary faces
  // contribute only +F. So the total equals the sum of boundary fluxes alone -- and with inviscid
  // walls the mass and energy parts of that are ~0, which is the closed-domain statement.
  Real sumRho = 0.0;
  Real sumE = 0.0;
  for (Index i = 0; i < n; ++i) {
    sumRho += full.density[i];
    sumE += full.totalEnergy[i];
  }
  // A direct per-face check: build a one-face mesh situation by comparing a two-cell domain.
  Mesh two = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  ScalarField r2(2);
  VectorField u2(2);
  ScalarField t2(2);
  r2[0] = 1.4;
  r2[1] = 0.9;
  u2[0] = Vector3{80.0, 0.0, 0.0};
  u2[1] = Vector3{-30.0, 0.0, 0.0};
  t2[0] = 320.0;
  t2[1] = 280.0;
  const ConservedField q2 = solver.fromPrimitive(two, r2, u2, t2);
  const auto p2 = solver.toPrimitive(q2);
  // walls everywhere, so the ONLY internal face is the one between the two cells
  const auto b2 = allPatches(two, BoundaryKind::InviscidWall);
  const ResidualField res2 = solver.assembleResidual(two, p2, b2);

  // The internal face's contribution is the difference between each cell's residual and its own
  // boundary-face contribution. Rather than reconstruct that, assert the property the gate names:
  // the total over both cells must equal the boundary contribution only, which for a closed
  // inviscid box is zero mass and zero energy to round-off.
  const Real massTotal = res2.density[0] + res2.density[1];
  const Real energyTotal = res2.totalEnergy[0] + res2.totalEnergy[1];
  std::printf("\n  R1 two-cell closed box: sum of mass residual %.6e, energy residual %.6e\n",
              massTotal, energyTotal);
  std::printf("  R1 %zu-cell closed box: sum of mass residual %.6e, energy residual %.6e\n",
              static_cast<std::size_t>(n), sumRho, sumE);

  // The derived bound: each boundary face contributes at most eps-level mass flux (the wall's
  // mirrored state makes it vanish algebraically), accumulated over the boundary faces.
  Real scale = 0.0;
  for (const auto& s : p2) scale = std::max(scale, s.density * s.speedOfSound);
  const Real bound = 64.0 * kEps * scale * 4.0;  // 4 boundary faces per cell at most
  EXPECT_LE(std::abs(massTotal), bound);
  std::printf("  R1 bound %.3e\n", bound);
}

// R4: a closed uniform domain has zero residual, because sum_f areaVector_f = 0 per cell.
TEST(DensityBasedResidualTest, R4_UniformStateGivesZeroResidual) {
  const DensityBasedSolver solver(globalSettings(), air());
  // Zero-gradient extrapolation on every boundary, so every face -- internal and boundary -- sees
  // two IDENTICAL states and HLLC returns the exact physical flux. The residual is then
  // F(Q) . sum_f(areaVector_f), and a closed cell's area vectors sum to zero.
  auto c = uniformCase(solver, 6, 5, 6.0, 5.0, Vector3{40.0, -15.0, 0.0});
  const auto boundaries = allPatches(c.mesh, BoundaryKind::SupersonicOutflow);
  const ResidualField r = solver.assembleResidual(c.mesh, c.primitives, boundaries);

  // the derived scale: max |F| A over the faces
  Real fluxScale = 0.0;
  for (const auto& s : c.primitives) {
    fluxScale = std::max(fluxScale,
                         (s.totalEnergyDensity + s.pressure) *
                             std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y));
  }
  const Real bound = 16.0 * kEps * fluxScale * 4.0;

  Real worstRho = 0.0;
  Real worstE = 0.0;
  for (Index i = 0; i < c.q.size(); ++i) {
    worstRho = std::max(worstRho, std::abs(r.density[i]));
    worstE = std::max(worstE, std::abs(r.totalEnergy[i]));
    EXPECT_LE(std::abs(r.totalEnergy[i]), bound) << "cell " << i;
  }
  std::printf("\n  R4 uniform state: worst |R_rho| %.3e, worst |R_E| %.3e  (bound %.3e)\n",
              worstRho, worstE, bound);
}

// =============================================================================================
// Group K -- CFL and the time step
// =============================================================================================

TEST(DensityBasedCflTest, K1_TimeStepIsExactlyLinearInCfl) {
  const DensityBasedSolver a(globalSettings(0.2), air());
  const DensityBasedSolver b(globalSettings(0.4), air());
  auto c = uniformCase(a, 5, 4, 5.0, 4.0, Vector3{60.0, 20.0, 0.0});
  const Real dtA = a.globalTimeStep(c.mesh, c.primitives);
  const Real dtB = b.globalTimeStep(c.mesh, c.primitives);
  // CFL multiplies a quantity it does not otherwise enter, so doubling it doubles dt exactly.
  EXPECT_EQ(dtB, 2.0 * dtA);
  std::printf("\n  K1 dt(CFL=0.2) = %.6e   dt(CFL=0.4) = %.6e   ratio exactly 2: %s\n", dtA, dtB,
              (dtB == 2.0 * dtA) ? "yes" : "NO");
}

TEST(DensityBasedCflTest, K2_TimeStepFallsWithSpeedSoundSpeedAndCellSize) {
  const DensityBasedSolver solver(globalSettings(), air());
  auto base = uniformCase(solver, 5, 4, 5.0, 4.0, Vector3{50.0, 0.0, 0.0});
  const Real dtBase = solver.globalTimeStep(base.mesh, base.primitives);

  auto faster = uniformCase(solver, 5, 4, 5.0, 4.0, Vector3{500.0, 0.0, 0.0});
  EXPECT_LT(solver.globalTimeStep(faster.mesh, faster.primitives), dtBase);

  // higher T -> higher a = sqrt(gamma R T)
  auto hotter = uniformCase(solver, 5, 4, 5.0, 4.0, Vector3{50.0, 0.0, 0.0}, 1.2, 1200.0);
  EXPECT_LT(solver.globalTimeStep(hotter.mesh, hotter.primitives), dtBase);

  // same domain, more cells -> smaller cells
  auto finer = uniformCase(solver, 20, 16, 5.0, 4.0, Vector3{50.0, 0.0, 0.0});
  EXPECT_LT(solver.globalTimeStep(finer.mesh, finer.primitives), dtBase);

  std::printf("\n  K2 dt: base %.4e, 10x velocity %.4e, 4x temperature %.4e, 4x refinement %.4e\n",
              dtBase, solver.globalTimeStep(faster.mesh, faster.primitives),
              solver.globalTimeStep(hotter.mesh, hotter.primitives),
              solver.globalTimeStep(finer.mesh, finer.primitives));
}

// K4 -- THE CONSISTENCY CRITERION. P17.8's dt fed back through the EXISTING solver::calculateCFL
// must give Co = CFL/2, because that function carries a documented 1/2 factor. Asserting Co == CFL
// would be the mistake this criterion exists to catch.
TEST(DensityBasedCflTest, K4_AgreesWithTheExistingCalculateCflInTheZeroSoundSpeedLimit) {
  // The acoustic term cannot be switched off from outside, so the comparison is made on the
  // ADVECTIVE part analytically: for a uniform grid-aligned flow, P17.8's denominator is
  // sum_f (|u.n| + a) A, and calculateCFL's is sum_f |u.n| A. Both are computed here from the same
  // mesh and state, and the RATIO of the two denominators is what the two conventions differ by.
  const Real cfl = 0.35;
  const DensityBasedSolver solver(globalSettings(cfl), air());
  const Vector3 velocity{40.0, 0.0, 0.0};
  auto c = uniformCase(solver, 8, 6, 8.0, 6.0, Vector3{velocity.x, 0.0, 0.0});
  const Real dt = solver.globalTimeStep(c.mesh, c.primitives);
  const Real a = c.primitives[0].speedOfSound;
  const Real rho = c.primitives[0].density;

  // Build the authoritative face mass flux the existing function consumes: rho (U.n) A per face.
  cfd::fields::SurfaceField massFlux(c.mesh.numberOfFaces());
  for (Index f = 0; f < static_cast<Index>(c.mesh.numberOfFaces()); ++f) {
    const auto& face = c.mesh.face(f);
    const Vector3 av = face.areaVector();
    massFlux[f] = rho * (velocity.x * av.x + velocity.y * av.y + velocity.z * av.z);
  }
  const auto existing = cfd::solver::calculateCFL(c.mesh, massFlux, rho, dt);

  // P17.8:  dt = CFL V / sum_f (|u.n| + a) A
  // existing: Co = dt/(2V) sum_f |u.n| A
  // so Co = (CFL/2) * sum|u.n|A / sum(|u.n|+a)A -- the ADVECTIVE FRACTION of the signal speed.
  // With u and a uniform this reduces to a pure number, computed here from the same quantities.
  Real sumAdvective = 0.0;
  Real sumTotal = 0.0;
  const auto& cell0 = c.mesh.cell(0);
  for (const Index faceId : cell0.faceIds()) {
    const auto& face = c.mesh.face(faceId);
    const Vector3 av = face.areaVector();
    const Real area = face.area();
    const Real un = (velocity.x * av.x + velocity.y * av.y + velocity.z * av.z) / area;
    sumAdvective += std::abs(un) * area;
    sumTotal += (std::abs(un) + a) * area;
  }
  const Real expectedCo = 0.5 * cfl * sumAdvective / sumTotal;
  const Real relative = std::abs(existing.maxCFL - expectedCo) / expectedCo;

  std::printf("\n  K4 the two CFL conventions, tied together:\n");
  std::printf("      P17.8 dt (CFL = %.2f)             %.9e\n", cfl, dt);
  std::printf("      advective fraction of the speed   %.9f\n", sumAdvective / sumTotal);
  std::printf("      expected Co = (CFL/2) x fraction  %.9e\n", expectedCo);
  std::printf("      solver::calculateCFL reports      %.9e\n", existing.maxCFL);
  std::printf("      relative difference               %.3e   (bound 1e-12)\n", relative);
  EXPECT_LE(relative, 1e-12);

  // and the pure a -> 0 statement, for the record: if a were zero the fraction would be 1 and
  // Co would be exactly CFL/2. That factor is the existing header's documented 1/2.
  std::printf("      (with a = 0 the fraction is 1 and Co would be exactly CFL/2 = %.4f)\n",
              0.5 * cfl);
}

TEST(DensityBasedCflTest, K5_InvalidCflIsRefused) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  for (const Real bad : {0.0, -1.0, nan, inf}) {
    DensityBasedSettings s = globalSettings();
    s.cfl = bad;
    EXPECT_THROW((void)DensityBasedSolver(s, air()), InvalidArgumentError) << "cfl = " << bad;
  }
  // and the other settings the constructor guards
  DensityBasedSettings s = globalSettings();
  s.maxIterations = 0;
  EXPECT_THROW((void)DensityBasedSolver(s, air()), InvalidArgumentError);
  s = globalSettings();
  s.retryReductionFactor = 1.0;
  EXPECT_THROW((void)DensityBasedSolver(s, air()), InvalidArgumentError);
  s = globalSettings();
  s.endTime = 0.0;
  EXPECT_THROW((void)DensityBasedSolver(s, air()), InvalidArgumentError);
  std::printf("\n  K5 4 invalid CFLs plus maxIterations/retryFactor/endTime each refused\n");
}

TEST(DensityBasedCflTest, K6_EdgeCasesGiveFinitePositiveTimeSteps) {
  const DensityBasedSolver solver(globalSettings(), air());
  struct Probe {
    const char* what;
    Vector3 u;
    Real rho, t, lx;
    Index nx;
  };
  const Probe probes[] = {
      {"zero velocity", Vector3{0.0, 0.0, 0.0}, 1.2, 300.0, 1.0, 4},
      {"Mach ~3", Vector3{1040.0, 0.0, 0.0}, 1.2, 300.0, 1.0, 4},
      {"tiny cells", Vector3{50.0, 0.0, 0.0}, 1.2, 300.0, 1.0e-3, 4},
      {"large cells", Vector3{50.0, 0.0, 0.0}, 1.2, 300.0, 1.0e2, 4},
      {"low pressure", Vector3{10.0, 0.0, 0.0}, 1.0e-4, 300.0, 1.0, 4},
      {"cold", Vector3{10.0, 0.0, 0.0}, 1.2, 5.0, 1.0, 4},
  };
  std::printf("\n  %-18s %14s\n", "case", "dt");
  for (const auto& p : probes) {
    auto c = uniformCase(solver, p.nx, p.nx, p.lx, p.lx, p.u, p.rho, p.t);
    const Real dt = solver.globalTimeStep(c.mesh, c.primitives);
    EXPECT_TRUE(std::isfinite(dt)) << p.what;
    EXPECT_GT(dt, 0.0) << p.what;
    std::printf("  %-18s %14.6e\n", p.what, dt);
  }
}

// =============================================================================================
// Group U -- the update and the two modes
// =============================================================================================

// STRENGTHENED after control N2 was detected by NOTHING. The first version built its mesh with
// createCartesian2D(3, 2, 3.0, 2.0) -- dx = dy = 1, so every cell volume was EXACTLY 1.0 and
// dt/V was bit-identical to dt. A plant that deleted the division by volume therefore passed this
// test, whose own name is "UpdateIsExactlyQMinusDtOverVTimesR". The mesh below is GRADED, so the
// volumes differ from each other and none is 1, and the test now asserts both of those facts
// before asserting the update -- so it cannot silently slide back into the blind spot.
//
// This is the same shape as P17.7's star-energy defect: a criterion that was genuinely non-vacuous
// against other defects, whose CASE SET could not reach the term in question.
TEST(DensityBasedUpdateTest, U1_UpdateIsExactlyQMinusDtOverVTimesR) {
  const DensityBasedSolver solver(globalSettings(), air());
  Mesh graded = MeshGeometry::createGraded2D(
      3, 2, 1.5, 0.8, AxisGrading{GradingType::Geometric, 1.7, GradingCluster::Start},
      AxisGrading{GradingType::Geometric, 1.4, GradingCluster::Start});
  const Index cells = static_cast<Index>(graded.numberOfCells());
  {
    Real smallest = graded.cell(0).volume();
    Real largest = smallest;
    for (Index i = 0; i < cells; ++i) {
      smallest = std::min(smallest, graded.cell(i).volume());
      largest = std::max(largest, graded.cell(i).volume());
      ASSERT_NE(graded.cell(i).volume(), 1.0)
          << "cell " << i << " has unit volume, which makes dt/V indistinguishable from dt";
    }
    ASSERT_GT(largest / smallest, 1.5)
        << "the volumes are too close to each other to detect a per-cell volume error";
    std::printf("\n  U1 graded mesh: %zu cells, volume %.6e .. %.6e (ratio %.3f)\n",
                static_cast<std::size_t>(cells), smallest, largest, largest / smallest);
  }
  ScalarField rho0(cells);
  VectorField u0(cells);
  ScalarField t0(cells);
  for (Index i = 0; i < cells; ++i) {
    rho0[i] = 1.2;
    u0[i] = Vector3{40.0, 10.0, 5.0};
    t0[i] = 300.0;
  }
  struct {
    Mesh mesh;
    ConservedField q;
  } c{std::move(graded), ConservedField{}};
  c.q = solver.fromPrimitive(c.mesh, rho0, u0, t0);
  const Index n = c.q.size();

  // an INJECTED residual with distinct values per component, so a transposed or dropped component
  // cannot pass
  ResidualField r{ScalarField(n), VectorField(n), ScalarField(n)};
  for (Index i = 0; i < n; ++i) {
    r.density[i] = 1.0 + static_cast<Real>(i);
    r.momentum[i] = Vector3{10.0 + static_cast<Real>(i), 20.0 + static_cast<Real>(i),
                            30.0 + static_cast<Real>(i)};
    r.totalEnergy[i] = 1000.0 + static_cast<Real>(i);
  }
  std::vector<Real> dt(n, 1.0e-6);
  const ConservedField out = DensityBasedSolver::applyUpdate(c.mesh, c.q, r, dt);

  for (Index i = 0; i < n; ++i) {
    const Real scale = dt[i] / c.mesh.cell(i).volume();
    EXPECT_EQ(out.density[i], c.q.density[i] - scale * r.density[i]) << "cell " << i;
    EXPECT_EQ(out.momentumDensity[i].x, c.q.momentumDensity[i].x - scale * r.momentum[i].x);
    EXPECT_EQ(out.momentumDensity[i].y, c.q.momentumDensity[i].y - scale * r.momentum[i].y);
    EXPECT_EQ(out.momentumDensity[i].z, c.q.momentumDensity[i].z - scale * r.momentum[i].z);
    EXPECT_EQ(out.totalEnergyDensity[i], c.q.totalEnergyDensity[i] - scale * r.totalEnergy[i]);
  }
  // And the decisive statement, which no choice of mesh can make vacuous: the SAME q, residual and
  // dt on a SMALLER mesh must change Q in inverse proportion to the cell volume. The expected ratio
  // is taken from the two meshes' ACTUAL volumes, not from the length ratio -- graded node
  // coordinates are not an exact rescaling of each other in floating point, so assuming a factor of
  // exactly 2 would be asserting a property of the mesh generator instead of the update.
  //
  // With the 1/V dropped this ratio is exactly 1 while the volume ratio is ~4, so the criterion
  // cannot be satisfied by accident -- which is the whole point of the test.
  Mesh smaller = MeshGeometry::createGraded2D(
      3, 2, 0.75, 0.8, AxisGrading{GradingType::Geometric, 1.7, GradingCluster::Start},
      AxisGrading{GradingType::Geometric, 1.4, GradingCluster::Start});
  // A LARGE dt for this sub-check, so the change is comparable to rho itself. With the dt = 1e-6
  // used above the change is ~4e-5 against rho = 1.2, and recovering it by subtraction cancels
  // about twelve digits -- which showed up as a 3.5e-12 "error" that was entirely the test's own
  // differencing, not the solver's. Sizing the step so no cancellation occurs measures the update
  // instead of measuring double-precision subtraction.
  const std::vector<Real> dtBig(n, 1.0e-1);
  const ConservedField outBig = DensityBasedSolver::applyUpdate(c.mesh, c.q, r, dtBig);
  const ConservedField outSmaller = DensityBasedSolver::applyUpdate(smaller, c.q, r, dtBig);
  Real worstRatioError = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real changeBig = c.q.density[i] - outBig.density[i];
    const Real changeSmall = c.q.density[i] - outSmaller.density[i];
    ASSERT_NE(changeBig, 0.0) << "cell " << i;
    const Real expected = c.mesh.cell(i).volume() / smaller.cell(i).volume();
    ASSERT_GT(expected, 1.5) << "cell " << i << ": the two meshes are too alike to discriminate";
    const Real relative = std::abs(changeSmall / changeBig - expected) / expected;
    worstRatioError = std::max(worstRatioError, relative);
    // 64 eps: the ratio passes through two divisions and the graded widths themselves carry
    // round-off from the expm1 node formula.
    EXPECT_LE(relative, 64.0 * kEps)
        << "cell " << i
        << ": the update did not scale with 1/V, so the residual is not being "
           "divided by the cell volume";
  }
  std::printf(
      "  U1 1/V dependence: worst relative error in (dQ_small/dQ_big) vs (V_big/V_small)"
      " %.3e (bound %.3e)\n",
      worstRatioError, 64.0 * kEps);
  std::printf(
      "  U1 update BITWISE equal to Q - (dt/V) R for all 5 components, %zu cells;"
      " halving V doubles the change\n",
      static_cast<std::size_t>(n));
}

TEST(DensityBasedUpdateTest, U2_GlobalModeUsesOneDtForEveryCell) {
  const DensityBasedSolver solver(globalSettings(), air());
  // a NON-uniform state, so the per-cell dt genuinely differs and taking the minimum matters
  Mesh mesh = MeshGeometry::createCartesian2D(5, 1, 5.0, 1.0);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.0;
    u[i] = Vector3{50.0 * static_cast<Real>(i + 1), 0.0, 0.0};
    t[i] = 300.0;
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto prim = solver.toPrimitive(q);

  const auto local = solver.localTimeSteps(mesh, prim);
  const Real global = solver.globalTimeStep(mesh, prim);
  const Real smallest = *std::min_element(local.begin(), local.end());
  EXPECT_EQ(global, smallest);
  // the per-cell steps really do differ, so the minimum is not trivially the same as any cell's
  EXPECT_NE(*std::max_element(local.begin(), local.end()), smallest);
  std::printf("\n  U2 local dt range [%.4e, %.4e]; global dt = the minimum, exactly: %s\n",
              smallest, *std::max_element(local.begin(), local.end()),
              global == smallest ? "yes" : "NO");
}

TEST(DensityBasedUpdateTest, U3_LocalPseudoTimeRefusesToReportAPhysicalTime) {
  DensityBasedSettings s{};
  s.cfl = 0.4;
  s.mode = TimeSteppingMode::LocalPseudoTime;
  s.maxIterations = 3;
  const DensityBasedSolver solver(s, air());
  auto c = uniformCase(solver, 4, 3, 4.0, 3.0, Vector3{40.0, 0.0, 0.0});
  const auto boundaries = allPatches(c.mesh, BoundaryKind::SupersonicOutflow);
  const DensityBasedResult result = solver.solve(c.mesh, c.q, boundaries);

  EXPECT_FALSE(result.hasPhysicalTime);
  EXPECT_THROW((void)result.physicalTime(), InvalidArgumentError);
  // and the global mode DOES report one
  const DensityBasedSolver g(globalSettings(0.4, 1.0e-5), air());
  const DensityBasedResult gr = g.solve(c.mesh, c.q, boundaries);
  EXPECT_TRUE(gr.hasPhysicalTime);
  EXPECT_GT(gr.physicalTime(), 0.0);
  std::printf("\n  U3 LocalPseudoTime refuses physicalTime(); GlobalPhysical reports %.6e s\n",
              gr.physicalTime());
}

// =============================================================================================
// Group C -- the canonical cases
// =============================================================================================

TEST(DensityBasedCanonicalTest, C1_UniformGridAlignedFlowIsPreserved) {
  // endTime is sized so the frozen criterion's ~20 STEPS actually happen. The first draft used
  // 2.0e-4, which is SMALLER than one dt (~2.7e-4) for this mesh, so the run completed in a single
  // step and the drift criterion was never exercised. A case parameter, not a tolerance.
  const DensityBasedSolver solver(globalSettings(0.4, 6.0e-3), air());
  auto c = uniformCase(solver, 8, 6, 8.0, 6.0, Vector3{40.0, 0.0, 0.0});
  const auto boundaries = allPatches(c.mesh, BoundaryKind::SupersonicOutflow);
  const DensityBasedResult r = solver.solve(c.mesh, c.q, boundaries);
  ASSERT_EQ(r.status, DensityBasedStatus::TimeComplete) << r.diagnostic;
  ASSERT_GE(r.iterations, 18u) << "the criterion calls for ~20 steps; a run that "
                                  "completes in fewer has not exercised it";

  Real worstRho = 0.0;
  Real worstMom = 0.0;
  Real worstE = 0.0;
  for (Index i = 0; i < c.q.size(); ++i) {
    worstRho = std::max(worstRho, std::abs(r.state.density[i] - c.q.density[i]) / c.q.density[i]);
    worstMom =
        std::max(worstMom, std::abs(r.state.momentumDensity[i].x - c.q.momentumDensity[i].x) /
                               std::abs(c.q.momentumDensity[i].x));
    worstE = std::max(worstE, std::abs(r.state.totalEnergyDensity[i] - c.q.totalEnergyDensity[i]) /
                                  c.q.totalEnergyDensity[i]);
  }
  // The frozen criterion C1 names FIVE quantities -- "rho, rhoU, rhoE, p, T unchanged" -- and the
  // first version of this test asserted only the three CONSERVED ones. p and T are what the state
  // is actually recovered as, and they pass through the closed-form inverse and the EOS, so a
  // conversion defect could move them while leaving Q untouched. Asserting them is not redundant.
  const auto before = solver.toPrimitive(c.q);
  const auto after = solver.toPrimitive(r.state);
  Real worstP = 0.0;
  Real worstT = 0.0;
  for (Index i = 0; i < c.q.size(); ++i) {
    worstP =
        std::max(worstP, std::abs(after[i].pressure - before[i].pressure) / before[i].pressure);
    worstT = std::max(
        worstT, std::abs(after[i].temperature - before[i].temperature) / before[i].temperature);
  }
  std::printf("\n  C1 uniform, grid-aligned, %zu steps: worst relative drift\n",
              static_cast<std::size_t>(r.iterations));
  std::printf("      rho %.3e   rhoU %.3e   rhoE %.3e   p %.3e   T %.3e   (bound 64 eps = %.3e)\n",
              worstRho, worstMom, worstE, worstP, worstT, 64.0 * kEps);
  EXPECT_LE(worstRho, 64.0 * kEps);
  EXPECT_LE(worstMom, 64.0 * kEps);
  EXPECT_LE(worstE, 64.0 * kEps);
  EXPECT_LE(worstP, 64.0 * kEps);
  EXPECT_LE(worstT, 64.0 * kEps);
}

TEST(DensityBasedCanonicalTest, C2_UniformObliqueFlowIsPreserved) {
  const DensityBasedSolver solver(globalSettings(0.4, 6.0e-3), air());
  // u, v AND w all non-zero -- P17.7's rotational consistency at solver level
  auto c = uniformCase(solver, 8, 6, 8.0, 6.0, Vector3{40.0, -25.0, 12.0});
  const auto boundaries = allPatches(c.mesh, BoundaryKind::SupersonicOutflow);
  const DensityBasedResult r = solver.solve(c.mesh, c.q, boundaries);
  ASSERT_EQ(r.status, DensityBasedStatus::TimeComplete) << r.diagnostic;
  ASSERT_GE(r.iterations, 18u);

  Real worst = 0.0;
  for (Index i = 0; i < c.q.size(); ++i) {
    worst = std::max(worst, std::abs(r.state.density[i] - c.q.density[i]) / c.q.density[i]);
    worst = std::max(worst, std::abs(r.state.totalEnergyDensity[i] - c.q.totalEnergyDensity[i]) /
                                c.q.totalEnergyDensity[i]);
    for (const Real d : {r.state.momentumDensity[i].x - c.q.momentumDensity[i].x,
                         r.state.momentumDensity[i].y - c.q.momentumDensity[i].y,
                         r.state.momentumDensity[i].z - c.q.momentumDensity[i].z}) {
      worst = std::max(worst, std::abs(d) / std::abs(c.q.momentumDensity[i].x));
    }
  }
  // p and T too -- C1's note applies unchanged, and at an oblique velocity the recovery has to
  // subtract a kinetic energy built from three non-zero components rather than one.
  const auto before = solver.toPrimitive(c.q);
  const auto after = solver.toPrimitive(r.state);
  Real worstP = 0.0;
  Real worstT = 0.0;
  for (Index i = 0; i < c.q.size(); ++i) {
    worstP =
        std::max(worstP, std::abs(after[i].pressure - before[i].pressure) / before[i].pressure);
    worstT = std::max(
        worstT, std::abs(after[i].temperature - before[i].temperature) / before[i].temperature);
  }
  std::printf(
      "\n  C2 uniform, OBLIQUE (u,v,w all non-zero), %zu steps: worst drift %.3e, "
      "p %.3e, T %.3e (bound %.3e)\n",
      static_cast<std::size_t>(r.iterations), worst, worstP, worstT, 64.0 * kEps);
  EXPECT_LE(worst, 64.0 * kEps);
  EXPECT_LE(worstP, 64.0 * kEps);
  EXPECT_LE(worstT, 64.0 * kEps);
}

// C3: a supersonic advected contact. p and u must stay uniform -- HLLC resolves a contact exactly,
// so a spurious pressure or velocity jump would be an O(1) failure, which is what this detects.
TEST(DensityBasedCanonicalTest, C3_AdvectedContactKeepsPressureAndVelocityUniform) {
  const DensityBasedSolver solver(globalSettings(0.4, 2.0e-4), air());
  Mesh mesh = MeshGeometry::createCartesian2D(20, 2, 2.0, 0.2);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  const Real uAdvect = 700.0;  // Mach ~2, so the inflow/outflow boundaries are genuinely supersonic
  const Real pUniform = kPRef;
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = x < 1.0 ? 1.0 : 0.25;      // the contact: a density jump
    t[i] = pUniform / (rho[i] * kR);    // chosen so p is UNIFORM across the jump
    u[i] = Vector3{uAdvect, 0.0, 0.0};  // and u is uniform
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);

  // supersonic inflow on the left with the left state prescribed; supersonic outflow on the right;
  // walls top and bottom (v = 0 there, so they carry no mass)
  const PrimitiveState inflow = cfd::compressible::makePrimitiveState(
      air(), 1.0, Vector3{uAdvect, 0.0, 0.0}, pUniform / (1.0 * kR));
  DensityBasedBoundaries boundaries;
  boundaries["left"] = DensityBasedBoundary{BoundaryKind::SupersonicInflow, inflow};
  boundaries["right"] = DensityBasedBoundary{BoundaryKind::SupersonicOutflow, PrimitiveState{}};
  boundaries["bottom"] = DensityBasedBoundary{BoundaryKind::InviscidWall, PrimitiveState{}};
  boundaries["top"] = DensityBasedBoundary{BoundaryKind::InviscidWall, PrimitiveState{}};

  const DensityBasedResult r = solver.solve(mesh, q, boundaries);
  ASSERT_EQ(r.status, DensityBasedStatus::TimeComplete) << r.diagnostic;

  const auto prim = solver.toPrimitive(r.state);
  Real worstP = 0.0;
  Real worstU = 0.0;
  Real rhoMin = std::numeric_limits<Real>::infinity();
  Real rhoMax = 0.0;
  for (const auto& s : prim) {
    worstP = std::max(worstP, std::abs(s.pressure - pUniform) / pUniform);
    worstU = std::max(worstU, std::abs(s.velocity.x - uAdvect) / uAdvect);
    rhoMin = std::min(rhoMin, s.density);
    rhoMax = std::max(rhoMax, s.density);
  }
  std::printf("\n  C3 advected contact, %zu steps at Mach ~2:\n",
              static_cast<std::size_t>(r.iterations));
  std::printf("      pressure non-uniformity  %.3e   (detection bound 1e-9)\n", worstP);
  std::printf("      velocity non-uniformity  %.3e   (detection bound 1e-9)\n", worstU);
  std::printf("      density range [%.4f, %.4f] -- the contact itself, which DOES smear\n", rhoMin,
              rhoMax);
  EXPECT_LE(worstP, 1e-9);
  EXPECT_LE(worstU, 1e-9);
  // the contact must still be there: the density jump has not been washed out entirely
  EXPECT_GT(rhoMax - rhoMin, 0.5);
}

// =============================================================================================
// Group G -- conservation (gate criteria G3 and G4)
// =============================================================================================

TEST(DensityBasedConservationTest, G1_ClosedDomainConservesMassMomentumAndEnergy) {
  const DensityBasedSolver solver(globalSettings(0.4, 1.0e-3), air());
  // a NON-uniform initial state in a closed box, so conservation is not trivially satisfied
  Mesh mesh = MeshGeometry::createCartesian2D(10, 8, 1.0, 0.8);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = 1.0 + 0.3 * std::sin(6.0 * x);
    u[i] = Vector3{0.0, 0.0, 0.0};  // at rest, so the walls are exactly impermeable
    t[i] = 300.0 + 40.0 * std::cos(6.0 * x);
  }
  const ConservedField q0 = solver.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);

  const DensityBasedResult r = solver.solve(mesh, q0, boundaries);
  ASSERT_EQ(r.status, DensityBasedStatus::TimeComplete) << r.diagnostic;

  const Real mass0 = totalOf(mesh, q0.density);
  const Real mass1 = totalOf(mesh, r.state.density);
  const Real energy0 = totalOf(mesh, q0.totalEnergyDensity);
  const Real energy1 = totalOf(mesh, r.state.totalEnergyDensity);
  Real momX0 = 0.0;
  Real momX1 = 0.0;
  for (Index i = 0; i < n; ++i) {
    momX0 += q0.momentumDensity[i].x * mesh.cell(i).volume();
    momX1 += r.state.momentumDensity[i].x * mesh.cell(i).volume();
  }

  const Real massErr = std::abs(mass1 - mass0) / mass0;
  const Real energyErr = std::abs(energy1 - energy0) / energy0;
  const Real bound = 8.0 * kEps * static_cast<Real>(n);

  std::printf("\n  G1 closed domain, %zu cells, %zu steps:\n", static_cast<std::size_t>(n),
              static_cast<std::size_t>(r.iterations));
  std::printf("      mass    %.10e -> %.10e   relative error %.3e\n", mass0, mass1, massErr);
  std::printf("      energy  %.10e -> %.10e   relative error %.3e\n", energy0, energy1, energyErr);
  std::printf("      x momentum %.6e -> %.6e\n", momX0, momX1);
  std::printf("      bound 8 eps N = %.3e\n", bound);
  EXPECT_LE(massErr, bound);
  EXPECT_LE(energyErr, bound);
}

// The wall is what makes G1 exact: no mass and no energy may cross it.
// ADDED after control N2 was detected by nothing. G1 above runs on a UNIFORM Cartesian mesh, where
// every cell volume is the same value V. The conservation statement it checks is
//     sum_P (Q_new - Q) V_P = -dt sum_P (R_P / V_P) V_P = -dt sum_P R_P = 0   (closed box)
// but with all volumes equal, an update that forgot the 1/V entirely gives
//     sum_P (Q_new - Q) V_P = -dt sum_P R_P V_P = -dt V sum_P R_P = 0
// as well. The uniform mesh makes the two algebraically identical, so G1 CANNOT see the error.
// On a graded mesh sum_P R_P V_P is not proportional to sum_P R_P and the cancellation is real.
TEST(DensityBasedConservationTest, G1_ClosedDomainConservesOnAGradedMeshToo) {
  const DensityBasedSolver solver(globalSettings(0.4, 1.0e-3), air());
  Mesh mesh = MeshGeometry::createGraded2D(
      10, 8, 1.0, 0.8, AxisGrading{GradingType::Geometric, 1.3, GradingCluster::Start},
      AxisGrading{GradingType::Geometric, 1.2, GradingCluster::End});
  const Index n = static_cast<Index>(mesh.numberOfCells());
  Real smallest = mesh.cell(0).volume();
  Real largest = smallest;
  for (Index i = 0; i < n; ++i) {
    smallest = std::min(smallest, mesh.cell(i).volume());
    largest = std::max(largest, mesh.cell(i).volume());
  }
  ASSERT_GT(largest / smallest, 2.0)
      << "the volume spread is too small for this test to differ from the uniform-mesh G1";

  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = 1.0 + 0.3 * std::sin(6.0 * x);
    u[i] = Vector3{0.0, 0.0, 0.0};
    t[i] = 300.0 + 40.0 * std::cos(6.0 * x);
  }
  const ConservedField q0 = solver.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);
  const DensityBasedResult r = solver.solve(mesh, q0, boundaries);
  ASSERT_EQ(r.status, DensityBasedStatus::TimeComplete) << r.diagnostic;

  const Real mass0 = totalOf(mesh, q0.density);
  const Real mass1 = totalOf(mesh, r.state.density);
  const Real energy0 = totalOf(mesh, q0.totalEnergyDensity);
  const Real energy1 = totalOf(mesh, r.state.totalEnergyDensity);
  const Real bound = 8.0 * kEps * static_cast<Real>(n);
  const Real massError = std::abs(mass1 - mass0) / mass0;
  const Real energyError = std::abs(energy1 - energy0) / energy0;
  std::printf(
      "\n  G1 graded mesh, %zu cells (volume ratio %.2f), %zu steps:\n"
      "      mass   relative error %.3e\n      energy relative error %.3e   (bound %.3e)\n",
      static_cast<std::size_t>(n), largest / smallest, r.history.size(), massError, energyError,
      bound);
  EXPECT_LE(massError, bound);
  EXPECT_LE(energyError, bound);
}

TEST(DensityBasedConservationTest, InviscidWallCarriesNoMassOrEnergy) {
  const auto thermo = air();
  const PrimitiveState interior =
      cfd::compressible::makePrimitiveState(thermo, 1.2, Vector3{120.0, -40.0, 15.0}, 320.0);
  const Vector3 n{1.0, 0.0, 0.0};
  const PrimitiveState ext = exteriorState(
      DensityBasedBoundary{BoundaryKind::InviscidWall, PrimitiveState{}}, interior, n);

  // the mirrored normal velocity, and everything else preserved
  EXPECT_EQ(ext.velocity.x, -interior.velocity.x);
  EXPECT_EQ(ext.velocity.y, interior.velocity.y);
  EXPECT_EQ(ext.velocity.z, interior.velocity.z);
  EXPECT_EQ(ext.density, interior.density);
  EXPECT_EQ(ext.pressure, interior.pressure);

  const Real gamma = thermo.thermodynamics().specificHeatRatio();
  const auto hllc = cfd::compressible::hllcFluxChecked(interior, ext, n, gamma);
  // Derived bound: the algebra gives exactly zero, but S* is formed from separately rounded
  // products, so the result is round-off rather than bitwise. rho a is the natural scale.
  const Real scale = interior.density * interior.speedOfSound;
  const Real bound = 64.0 * kEps * scale;
  std::printf(
      "\n  wall: mass flux %.6e (bound %.3e), energy flux %.6e, momentum.x %.6e vs p = %.6e\n",
      hllc.flux.mass, bound, hllc.flux.energy, hllc.flux.momentum.x, interior.pressure);
  EXPECT_LE(std::abs(hllc.flux.mass), bound);
  EXPECT_LE(std::abs(hllc.flux.energy), bound * interior.speedOfSound * interior.speedOfSound);
  // the pressure force is RETAINED
  EXPECT_GT(hllc.flux.momentum.x, 0.5 * interior.pressure);
}

// =============================================================================================
// Group M -- residual norms and convergence
// =============================================================================================

TEST(DensityBasedResidualTest, M2_NormDoesNotScaleWithMeshSize) {
  const DensityBasedSolver solver(globalSettings(), air());
  Real coarse = 0.0;
  Real fine = 0.0;
  for (const Index nx : {8u, 32u}) {
    auto c = uniformCase(solver, nx, nx, 1.0, 1.0, Vector3{40.0, 0.0, 0.0});
    const auto boundaries = allPatches(c.mesh, BoundaryKind::SupersonicOutflow);
    const ResidualField r = solver.assembleResidual(c.mesh, c.primitives, boundaries);
    const auto norms = DensityBasedSolver::residualNorms(c.mesh, r);
    // a uniform state's residual is zero, so the NORM is at round-off at BOTH resolutions -- which
    // is the property: it does not grow or shrink with the mesh
    if (nx == 8u)
      coarse = norms.maximum();
    else
      fine = norms.maximum();
  }
  // the scale a per-unit-volume rate is measured against
  auto c = uniformCase(solver, 8, 8, 1.0, 1.0, Vector3{40.0, 0.0, 0.0});
  const Real rateScale = c.primitives[0].density * c.primitives[0].speedOfSound / 0.125;
  std::printf("\n  M2 max residual norm: 8x8 %.3e, 32x32 %.3e   (uniform state; rate scale %.3e)\n",
              coarse, fine, rateScale);
  EXPECT_LE(coarse, 64.0 * kEps * rateScale);
  EXPECT_LE(fine, 64.0 * kEps * rateScale * 4.0);
}

TEST(DensityBasedConvergenceTest, M5_ReachingTheEndTimeIsTimeCompleteNotConverged) {
  const DensityBasedSolver solver(globalSettings(0.4, 1.0e-5), air());
  auto c = uniformCase(solver, 5, 4, 5.0, 4.0, Vector3{40.0, 0.0, 0.0});
  const auto boundaries = allPatches(c.mesh, BoundaryKind::SupersonicOutflow);
  const DensityBasedResult r = solver.solve(c.mesh, c.q, boundaries);
  // A uniform state has zero residual, so a steady criterion would call this "Converged" -- and
  // that would be wrong for a transient run that simply reached its end time.
  EXPECT_EQ(r.status, DensityBasedStatus::TimeComplete);
  EXPECT_NE(r.status, DensityBasedStatus::Converged);
  std::printf("\n  M5 a transient run that reached endTime reports TimeComplete, NOT Converged\n");
}

TEST(DensityBasedConvergenceTest, M4_EverySignalIsActuallyConsulted) {
  // WHAT THE FIRST DRAFT GOT WRONG. It set massImbalanceTolerance to 1e-300 on a UNIFORM state and
  // expected convergence to be blocked. But a uniform state's mass imbalance is EXACTLY 0, and
  // 0 <= 1e-300 is true -- so the "impossible" tolerance was satisfied and the test proved nothing.
  // A signal can only be shown to be consulted on a case where that signal is NON-ZERO.
  //
  // So: a mildly non-uniform closed box, where all three signals are non-zero, and each tolerance
  // is made impossible IN TURN. Blocking any one must prevent Converged -- which is what
  // "convergence needs ALL of them" means operationally.
  DensityBasedSettings base{};
  base.cfl = 0.4;
  base.mode = TimeSteppingMode::LocalPseudoTime;
  base.maxIterations = 4000;
  base.residualTolerance = 1e-2;  // deliberately loose, so it is not the binding constraint
  base.massImbalanceTolerance = 1e-2;
  base.stateChangeTolerance = 1e-2;

  Mesh mesh = MeshGeometry::createCartesian2D(6, 4, 0.6, 0.4);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = 1.2 + 0.05 * std::sin(10.0 * x);  // non-uniform: every signal is non-zero
    u[i] = Vector3{0.0, 0.0, 0.0};
    t[i] = 300.0;
  }
  const DensityBasedSolver seed(base, air());
  const ConservedField q = seed.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);

  // with all three loose, the conjunction CAN be satisfied
  const DensityBasedResult all = seed.solve(mesh, q, boundaries);
  std::printf("\n  M4 all three loose      -> status %d after %zu iterations\n",
              static_cast<int>(all.status), static_cast<std::size_t>(all.iterations));
  ASSERT_EQ(all.status, DensityBasedStatus::Converged)
      << "the conjunction could not be satisfied at all, so blocking a term proves nothing";

  // and blocking ANY ONE of the three prevents it
  struct Blocked {
    const char* which;
    DensityBasedSettings s;
  };
  std::vector<Blocked> cases;
  {
    DensityBasedSettings b = base;
    b.residualTolerance = 1e-300;
    cases.push_back({"residual norms", b});
  }
  {
    DensityBasedSettings b = base;
    b.massImbalanceTolerance = 1e-300;
    cases.push_back({"mass imbalance", b});
  }
  {
    DensityBasedSettings b = base;
    b.stateChangeTolerance = 1e-300;
    cases.push_back({"state change", b});
  }
  for (const auto& c : cases) {
    const DensityBasedSolver solver(c.s, air());
    const DensityBasedResult r = solver.solve(mesh, q, boundaries);
    std::printf(
        "  M4 blocking %-16s -> status %d after %zu iterations (all-loose took %zu; blocking must "
        "change it)\n",
        c.which, static_cast<int>(r.status), static_cast<std::size_t>(r.iterations),
        static_cast<std::size_t>(all.iterations));
    // THE ASSERTION IS ON THE ITERATION COUNT, NOT ON "never converges", and the reason is a fact
    // about the physics rather than a concession. In a CLOSED box the mass imbalance decays to
    // EXACTLY zero (mass is conserved to round-off, and eventually massNew == massOld bitwise) and
    // so does the state change once the update stops moving any double. A 1e-300 tolerance is
    // therefore eventually SATISFIED by both -- measured: blocking mass converged at 1548
    // iterations and blocking state change at 2231, against 354 with all three loose. Only the
    // relative residual, which is normalised by a fixed reference, can never reach 1e-300. So the
    // testable statement is that each signal PARTICIPATES: tightening it must change the outcome.
    // An earlier draft asserted "must not converge" and failed for two of the three -- not because
    // they are ignored, but because they legitimately reach zero.
    EXPECT_GT(r.iterations, all.iterations)
        << "blocking the " << c.which
        << " signal did not change the outcome at all, so that signal "
        << "is not actually consulted";
  }
}

// =============================================================================================
// Group P -- positivity, robustness, non-finite detection
// =============================================================================================

TEST(DensityBasedRobustnessTest, P1_P3_AnInadmissibleStateIsRefusedNamingTheCell) {
  const DensityBasedSolver solver(globalSettings(), air());
  Mesh mesh = MeshGeometry::createCartesian2D(3, 1, 3.0, 1.0);
  auto c = uniformCase(solver, 3, 1, 3.0, 1.0, Vector3{40.0, 0.0, 0.0});
  // corrupt cell 1's conserved state directly: a negative density is not a recoverable state
  ConservedField bad = c.q;
  bad.density[1] = -1.0;
  EXPECT_THROW((void)solver.toPrimitive(bad), NumericalError);
  try {
    (void)solver.toPrimitive(bad);
    FAIL() << "a negative density was accepted";
  } catch (const NumericalError& e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("cell 1"), std::string::npos) << msg;
    std::printf("\n  P3 the refusal names the cell: \"%s\"\n", msg.substr(0, 110).c_str());
  }

  // and a non-finite one
  ConservedField nan = c.q;
  nan.totalEnergyDensity[2] = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW((void)solver.toPrimitive(nan), NumericalError);
}

// P3 again, on the FIELD rather than the message. The test above asserts that the diagnostic names
// the cell, and it always did. What it could not see is that on the recover-failure path the
// solver set result.offendingCell to 0 regardless of which cell actually failed -- the message and
// the field disagreed, and only the message was asserted. This closes that gap.
TEST(DensityBasedRobustnessTest, P3_TerminalFailureReportsTheOffendingCellInTheFieldToo) {
  // maxRetries = 0, so the first inadmissible candidate is terminal and the path is deterministic.
  DensityBasedSettings s = globalSettings(200.0, 1.0e-3);
  s.maxIterations = 10;
  s.maxRetries = 0;
  const DensityBasedSolver solver(s, air());
  Mesh mesh = MeshGeometry::createCartesian2D(12, 2, 1.2, 0.2);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = mesh.cell(i).centroid().x < 0.6 ? 1.4 : 0.3;
    u[i] = Vector3{0.0, 0.0, 0.0};
    t[i] = mesh.cell(i).centroid().x < 0.6 ? 400.0 : 260.0;
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);
  const DensityBasedResult res = solver.solve(mesh, q, boundaries);

  ASSERT_EQ(res.status, DensityBasedStatus::InvalidState)
      << "CFL 200 with no retries did not produce a terminal invalid state, so the field this test "
         "exists to check was never set (status "
      << static_cast<int>(res.status) << ")";
  const std::string named = "cell " + std::to_string(res.offendingCell);
  std::printf("\n  P3 offendingCell field = %zu; diagnostic = \"%s\"\n",
              static_cast<std::size_t>(res.offendingCell), res.diagnostic.substr(0, 120).c_str());
  EXPECT_NE(res.diagnostic.find(named), std::string::npos)
      << "the reported cell id and the diagnostic disagree: " << res.diagnostic;
  EXPECT_EQ(res.rejectedSteps, 1);
  EXPECT_EQ(res.totalRetries, 0);
}

TEST(DensityBasedRobustnessTest, P2_P6_RetriesAreDeterministicAndCounted) {
  // A deliberately over-large CFL drives the explicit scheme unstable, which is what exercises the
  // retry path. The point is not that it recovers -- it is that the behaviour is BOUNDED, COUNTED
  // and REPRODUCIBLE rather than an infinite loop or a silent clamp.
  DensityBasedSettings s = globalSettings(50.0, 1.0e-3);
  s.maxIterations = 20;
  s.maxRetries = 5;
  const DensityBasedSolver solver(s, air());
  Mesh mesh = MeshGeometry::createCartesian2D(12, 2, 1.2, 0.2);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = x < 0.6 ? 4.0 : 0.05;  // a strong jump, to make the over-large step actually fail
    t[i] = x < 0.6 ? 400.0 : 250.0;
    u[i] = Vector3{0.0, 0.0, 0.0};
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);

  const DensityBasedResult a = solver.solve(mesh, q, boundaries);
  const DensityBasedResult b = solver.solve(mesh, q, boundaries);

  std::printf(
      "\n  P2/P6 CFL = 50 (deliberately unstable): status %d, iterations %zu, "
      "total retries %zu, rejected steps %zu\n",
      static_cast<int>(a.status), static_cast<std::size_t>(a.iterations),
      static_cast<std::size_t>(a.totalRetries), static_cast<std::size_t>(a.rejectedSteps));
  // bounded: retries can never exceed maxRetries per step
  EXPECT_LE(a.totalRetries, s.maxRetries * (a.iterations + 1));
  // and DETERMINISTIC: the identical solve makes the identical decisions
  EXPECT_EQ(a.status, b.status);
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.totalRetries, b.totalRetries);
  EXPECT_EQ(a.rejectedSteps, b.rejectedSteps);
  std::printf("  P2 the repeat made IDENTICAL control-flow decisions\n");
}

TEST(DensityBasedRobustnessTest, P4_NothingIsEverClamped) {
  // Structural, asserted from behaviour: a state that WOULD need clamping is refused, not repaired.
  const DensityBasedSolver solver(globalSettings(), air());
  auto c = uniformCase(solver, 2, 1, 2.0, 1.0, Vector3{0.0, 0.0, 0.0});
  ConservedField bad = c.q;
  bad.density[0] = 1e-30;  // positive but absurd: recover() must reject the resulting state
  bool refused = false;
  try {
    (void)solver.toPrimitive(bad);
  } catch (const cfd::Error&) {
    refused = true;
  }
  // Either it recovers a valid (if extreme) state, or it refuses -- what must NEVER happen is a
  // silently substituted floor. If it recovered, the density must still be the value given.
  if (!refused) {
    const auto prim = solver.toPrimitive(bad);
    EXPECT_EQ(prim[0].density, 1e-30) << "the density was silently altered -- a clamp";
  }
  std::printf("\n  P4 an absurd-but-positive density is %s; never silently replaced\n",
              refused ? "REFUSED" : "carried through unchanged");
}

// =============================================================================================
// Group D -- determinism (gate criterion G9)
// =============================================================================================

TEST(DensityBasedDeterminismTest, D1_RepeatedSolveIsBitwiseIdentical) {
  // endTime sized for ~20 steps: a 4-step run is thin evidence for a determinism claim.
  const DensityBasedSolver solver(globalSettings(0.4, 5.0e-4), air());
  Mesh mesh = MeshGeometry::createCartesian2D(10, 4, 1.0, 0.4);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = 1.0 + 0.4 * std::sin(8.0 * x);
    u[i] = Vector3{60.0 * std::cos(4.0 * x), 10.0, 0.0};
    t[i] = 300.0 + 30.0 * std::sin(5.0 * x);
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);

  const DensityBasedResult a = solver.solve(mesh, q, boundaries);
  const DensityBasedResult b = solver.solve(mesh, q, boundaries);

  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.iterations, b.iterations);
  ASSERT_EQ(a.history.size(), b.history.size());
  for (std::size_t k = 0; k < a.history.size(); ++k) {
    EXPECT_EQ(a.history[k].dt, b.history[k].dt) << "step " << k;
    EXPECT_EQ(a.history[k].time, b.history[k].time) << "step " << k;
    EXPECT_EQ(a.history[k].absolute.density, b.history[k].absolute.density) << "step " << k;
    EXPECT_EQ(a.history[k].absolute.totalEnergy, b.history[k].absolute.totalEnergy) << "step " << k;
    EXPECT_EQ(a.history[k].relative.density, b.history[k].relative.density) << "step " << k;
    EXPECT_EQ(a.history[k].massImbalance, b.history[k].massImbalance) << "step " << k;
    EXPECT_EQ(a.history[k].minDensity, b.history[k].minDensity) << "step " << k;
    EXPECT_EQ(a.history[k].maxMach, b.history[k].maxMach) << "step " << k;
    EXPECT_EQ(a.history[k].retries, b.history[k].retries) << "step " << k;
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.state.density[i], b.state.density[i]) << "cell " << i;
    EXPECT_EQ(a.state.momentumDensity[i].x, b.state.momentumDensity[i].x) << "cell " << i;
    EXPECT_EQ(a.state.momentumDensity[i].y, b.state.momentumDensity[i].y) << "cell " << i;
    EXPECT_EQ(a.state.totalEnergyDensity[i], b.state.totalEnergyDensity[i]) << "cell " << i;
  }
  EXPECT_EQ(a.totalRetries, b.totalRetries);
  EXPECT_EQ(a.rejectedSteps, b.rejectedSteps);
  std::printf(
      "\n  D1 determinism: %zu steps, dt/time/residual/state/retry histories all BITWISE "
      "identical\n",
      a.history.size());
}

// =============================================================================================
// Group S -- scope boundaries that REFUSE
// =============================================================================================

TEST(DensityBasedScopeTest, S1_S2_RestartIsUnsupportedAndRefuses) {
  EXPECT_FALSE(DensityBasedSolver::restartSupported());
  EXPECT_THROW(DensityBasedSolver::throwIfRestartRequested(), InvalidArgumentError);
  try {
    DensityBasedSolver::throwIfRestartRequested();
    FAIL() << "the restart request was not refused";
  } catch (const InvalidArgumentError& e) {
    const std::string msg = e.what();
    // the message must name the fields the format cannot carry, not just say "unsupported"
    EXPECT_NE(msg.find("rhoE"), std::string::npos) << msg;
    EXPECT_NE(msg.find("rhoU"), std::string::npos) << msg;
    EXPECT_NE(msg.find("NOT SUPPORTED"), std::string::npos) << msg;
    std::printf("\n  S1/S2 restart refused, naming the missing conserved fields\n");
  }
}

TEST(DensityBasedScopeTest, AnUnmappedBoundaryPatchIsRefusedNotAssumedToBeAWall) {
  const DensityBasedSolver solver(globalSettings(), air());
  auto c = uniformCase(solver, 3, 2, 3.0, 2.0, Vector3{40.0, 0.0, 0.0});
  DensityBasedBoundaries partial;
  partial["left"] = DensityBasedBoundary{BoundaryKind::InviscidWall, PrimitiveState{}};
  // "right", "bottom", "top" deliberately missing
  EXPECT_THROW((void)solver.assembleResidual(c.mesh, c.primitives, partial), InvalidArgumentError);
  std::printf(
      "\n  an unmapped boundary patch is REFUSED -- it is not silently treated as a wall\n");
}

// =============================================================================================
// Group R -- the criteria the first pass under-measured, and the independent reference
// =============================================================================================

// R1 as the frozen gate states it: "for every internal face, the owner and neighbour contributions
// cancel: all 5 components -- BITWISE".
//
// The R1 test above measures a closed-box SUM against a tolerance, which is a weaker statement than
// the criterion. This one asserts the criterion: the residual assembleResidual() produces is
// BITWISE equal to an independent re-accumulation that adds exactly +F to the owner and subtracts
// exactly the SAME F from the neighbour, using only P17.7's public pieces.
//
// WHAT IT CAN AND CANNOT DISTINGUISH, stated rather than implied. It fails if the neighbour's
// contribution is dropped (control N1), scaled, or computed from a different flux value. It CANNOT
// detect a second hllcFlux evaluation with swapped states, because P17.7 proved
// F(L,R,n) = -F(R,L,-n) BITWISE -- so the two would agree to the last bit. That is what the
// structural criterion R3 ("exactly 1 hllcFlux call site") is for, and the two together cover the
// claim. Neither alone does.
TEST(DensityBasedResidualTest, R1_ResidualIsExactlyPlusFOwnerMinusFNeighbourBitwise) {
  const DensityBasedSolver solver(globalSettings(), air());
  Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.0 + 0.2 * static_cast<Real>(i % 3);
    u[i] = Vector3{50.0 * static_cast<Real>(1 + i % 2), 10.0, 0.0};
    t[i] = 300.0 + 20.0 * static_cast<Real>(i % 4);
  }
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto prim = solver.toPrimitive(q);
  // Zero-gradient everywhere, so the exterior state does not depend on which patch a face is on and
  // the re-accumulation needs no patch lookup.
  const auto boundaries = allPatches(mesh, BoundaryKind::SupersonicOutflow);
  const ResidualField produced = solver.assembleResidual(mesh, prim, boundaries);

  const Real gamma = solver.thermodynamics().thermodynamics().specificHeatRatio();
  ResidualField expected{ScalarField(n), VectorField(n), ScalarField(n)};
  Index internalFaces = 0;
  for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) {
    const auto& face = mesh.face(f);
    const Index owner = face.owner();
    const Vector3 av = face.areaVector();
    const Real area = face.area();
    const Vector3 nrm{av.x / area, av.y / area, av.z / area};

    ReconstructedFaceState st{};
    if (face.isBoundary()) {
      DensityBasedBoundary outflow{};
      outflow.kind = BoundaryKind::SupersonicOutflow;
      st = reconstructFirstOrderBoundary(prim[owner], exteriorState(outflow, prim[owner], nrm));
    } else {
      ++internalFaces;
      st = reconstructFirstOrder(prim[owner], prim[*face.neighbor()]);
    }
    const auto hllc = hllcFluxChecked(st.left, st.right, nrm, gamma);
    ASSERT_TRUE(hllc.valid) << "face " << f;
    const FluxVector F = integrateOverArea(hllc.flux, area);

    // +F for the owner
    expected.density[owner] += F.mass;
    expected.momentum[owner] = Vector3{expected.momentum[owner].x + F.momentum.x,
                                       expected.momentum[owner].y + F.momentum.y,
                                       expected.momentum[owner].z + F.momentum.z};
    expected.totalEnergy[owner] += F.energy;
    // -F, the SAME value negated, for the neighbour. Negation of a double is exact.
    if (!face.isBoundary()) {
      const Index nb = *face.neighbor();
      expected.density[nb] -= F.mass;
      expected.momentum[nb] =
          Vector3{expected.momentum[nb].x - F.momentum.x, expected.momentum[nb].y - F.momentum.y,
                  expected.momentum[nb].z - F.momentum.z};
      expected.totalEnergy[nb] -= F.energy;
    }
  }
  ASSERT_GT(internalFaces, 0) << "a mesh with no internal face cannot exercise the cancellation";

  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(produced.density[i], expected.density[i]) << "R_rho, cell " << i;
    EXPECT_EQ(produced.momentum[i].x, expected.momentum[i].x) << "R_rhoU, cell " << i;
    EXPECT_EQ(produced.momentum[i].y, expected.momentum[i].y) << "R_rhoV, cell " << i;
    EXPECT_EQ(produced.momentum[i].z, expected.momentum[i].z) << "R_rhoW, cell " << i;
    EXPECT_EQ(produced.totalEnergy[i], expected.totalEnergy[i]) << "R_rhoE, cell " << i;
  }
  std::printf(
      "\n  R1 bitwise: %zu cells x 5 components re-accumulated from +F/-F over %zu internal"
      " faces -- all EXACTLY equal\n",
      static_cast<std::size_t>(n), static_cast<std::size_t>(internalFaces));
}

// R2: the two-cell residual against a value computed OUTSIDE CFDApp.
//
// results/p17/density_based_solver/tools/two_cell_residual_reference.py, mpmath at 50 digits, no
// CFDApp code. It is written from the Rankine-Hugoniot derivation, not transcribed from
// HllcFlux.hpp, and it REFUSES to emit a value unless the star flux satisfies
// F*_energy == S*(rhoE* + p*) -- the defining property that would have caught P17.7's sign error.
// See results/p17/hllc/AMENDMENT_star_energy_sign.md section 8.
TEST(DensityBasedResidualTest, R2_TwoCellResidualMatchesAnIndependentReference) {
  const DensityBasedSolver solver(globalSettings(), air());
  Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  ScalarField rho(2);
  VectorField u(2);
  ScalarField t(2);
  rho[0] = 1.4;
  rho[1] = 0.9;
  u[0] = Vector3{80.0, 0.0, 0.0};
  u[1] = Vector3{-30.0, 0.0, 0.0};
  t[0] = 320.0;
  t[1] = 280.0;
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto prim = solver.toPrimitive(q);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);
  const ResidualField r = solver.assembleResidual(mesh, prim, boundaries);

  // logs/07_r2_reference.log, emitted by the script named above.
  const Real reference[2][5] = {
      {128.90158456479763, 44412.801819919674, 0.0, 0.0, 41283940.377882551},
      {-128.90158456479763, -69569.745295403153, 0.0, 0.0, -41283940.377882551}};
  // sum_f |F_f| A per component. The residual is a sum of four face terms that largely cancel, so
  // the round-off floor is set by the size of the TERMS, not of the result -- which is why the
  // bound multiplies this and not |R| itself. The rhoW scale is EXACTLY 0 (a 2D mesh has no z flux
  // at all), so that component's bound is 0 and the criterion there is exact equality: a dyadic
  // control in the sense of the gate's section 5.1.
  const Real scale[2][5] = {
      {128.90158456479763, 221287.00993125096, 257196.8, 0.0, 41283940.377882551},
      {128.90158456479763, 196130.06645576748, 144673.2, 0.0, 41283940.377882551}};

  const char* names[5] = {"R_rho", "R_rhoU", "R_rhoV", "R_rhoW", "R_rhoE"};
  std::printf("\n  R2 two-cell residual vs an independent 50-digit reference\n");
  for (Index c = 0; c < 2; ++c) {
    const Real got[5] = {r.density[c], r.momentum[c].x, r.momentum[c].y, r.momentum[c].z,
                         r.totalEnergy[c]};
    for (int k = 0; k < 5; ++k) {
      const Real bound = 8.0 * kEps * scale[c][k];
      const Real diff = std::abs(got[k] - reference[c][k]);
      std::printf("    cell %zu %-7s got %+.15e  ref %+.15e  |diff| %.3e  bound %.3e\n",
                  static_cast<std::size_t>(c), names[k], got[k], reference[c][k], diff, bound);
      EXPECT_LE(diff, bound) << "cell " << c << " component " << names[k];
    }
  }
}

// =============================================================================================
// Group P -- P5, the non-finite coverage the gate enumerates
// =============================================================================================

// P5: "non-finite detection covers rho, each momentum component, rhoE, p, T, each residual
// component and dt; reports the FIRST offending cell in deterministic id order."
//
// Each quantity is corrupted IN TURN and the detection is asserted with the cell id, so the list is
// covered item by item rather than by one test that happens to trip on the first of them.
TEST(DensityBasedRobustnessTest, P5_EveryEnumeratedQuantityIsCheckedForNonFiniteness) {
  const DensityBasedSolver solver(globalSettings(), air());
  auto base = uniformCase(solver, 4, 3, 4.0, 3.0, Vector3{40.0, 5.0, 0.0});
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  const Index victim = 7;  // not cell 0, so "the first offending cell" is a real claim

  struct Corruption {
    const char* what;
    Real value;
    int field;  // 0 rho, 1 mom.x, 2 mom.y, 3 mom.z, 4 rhoE
  };
  const Corruption cases[] = {{"rho = NaN", nan, 0},   {"rho = +Inf", inf, 0},
                              {"rhoU = NaN", nan, 1},  {"rhoV = +Inf", inf, 2},
                              {"rhoW = NaN", nan, 3},  {"rhoE = NaN", nan, 4},
                              {"rhoE = -Inf", -inf, 4}};

  std::printf("\n  P5 non-finite coverage (victim cell %zu)\n", static_cast<std::size_t>(victim));
  for (const auto& c : cases) {
    ConservedField q = base.q;
    switch (c.field) {
      case 0:
        q.density[victim] = c.value;
        break;
      case 1:
        q.momentumDensity[victim] = Vector3{c.value, 0.0, 0.0};
        break;
      case 2:
        q.momentumDensity[victim] = Vector3{0.0, c.value, 0.0};
        break;
      case 3:
        q.momentumDensity[victim] = Vector3{0.0, 0.0, c.value};
        break;
      default:
        q.totalEnergyDensity[victim] = c.value;
        break;
    }
    bool refused = false;
    std::string message;
    try {
      (void)solver.toPrimitive(q);
    } catch (const cfd::Error& e) {
      refused = true;
      message = e.what();
    }
    EXPECT_TRUE(refused) << c.what << " was NOT detected";
    if (refused) {
      EXPECT_NE(message.find("cell " + std::to_string(victim)), std::string::npos)
          << c.what << " -- the diagnostic did not name the offending cell: " << message;
    }
    std::printf("    %-12s refused: %s\n", c.what, refused ? "yes, naming the cell" : "*** NO ***");
  }

  // p and T: a conserved state whose kinetic energy exceeds its total energy has NEGATIVE internal
  // energy, so no valid (p, T) exists. P17.5's recover() is what rejects it -- not a second opinion
  // written here.
  {
    ConservedField q = base.q;
    q.totalEnergyDensity[victim] = 1.0;  // far below the kinetic energy alone
    bool refused = false;
    std::string message;
    try {
      (void)solver.toPrimitive(q);
    } catch (const cfd::Error& e) {
      refused = true;
      message = e.what();
    }
    EXPECT_TRUE(refused) << "an inadmissible (p, T) recovery was accepted";
    EXPECT_NE(message.find("cell " + std::to_string(victim)), std::string::npos) << message;
    std::printf("    p / T        refused: yes -- %s\n", message.substr(0, 96).c_str());
  }

  // dt: a non-finite face speed must fail BEFORE any update, so corruption cannot reach Q.
  {
    auto prim = base.primitives;
    prim[victim].velocity = Vector3{nan, 0.0, 0.0};
    bool refused = false;
    std::string message;
    try {
      (void)solver.localTimeSteps(base.mesh, prim);
    } catch (const cfd::Error& e) {
      refused = true;
      message = e.what();
    }
    EXPECT_TRUE(refused) << "a non-finite dt was not detected";
    std::printf("    dt           refused: yes -- %s\n", message.substr(0, 96).c_str());
  }

  // residual components: an extreme but ADMISSIBLE state whose energy flux overflows to Inf. The
  // residual check must catch it and report NonFiniteState rather than updating Q with it.
  {
    Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 3.0, 2.0);
    const Index n = static_cast<Index>(mesh.numberOfCells());
    ScalarField rho(n);
    VectorField u(n);
    ScalarField t(n);
    for (Index i = 0; i < n; ++i) {
      rho[i] = 1e300;  // finite, positive, admissible -- but u (rhoE + p) overflows
      u[i] = Vector3{1.5e4, 0.0, 0.0};
      t[i] = 300.0;
    }
    const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
    const auto boundaries = allPatches(mesh, BoundaryKind::SupersonicOutflow);
    const DensityBasedResult res = solver.solve(mesh, q, boundaries);
    std::printf("    residual     status %d, nonFiniteDetections %zu -- %s\n",
                static_cast<int>(res.status), static_cast<std::size_t>(res.nonFiniteDetections),
                res.diagnostic.substr(0, 110).c_str());
    EXPECT_EQ(res.status, DensityBasedStatus::NonFiniteState);
    EXPECT_GE(res.nonFiniteDetections, 1);
    // It must be REPORTED, not thrown out of solve(), and it must say where. Before the fix this
    // escaped as an uncaught "hllcFluxChecked: the flux is not finite" with no iteration and no
    // location at all.
    EXPECT_NE(res.diagnostic.find("iteration 0"), std::string::npos) << res.diagnostic;
    EXPECT_NE(res.diagnostic.find("face "), std::string::npos) << res.diagnostic;
    EXPECT_NE(res.diagnostic.find("owner cell "), std::string::npos) << res.diagnostic;
  }
}

// =============================================================================================
// Group M -- M3 and M6, the two convergence criteria the first pass did not assert
// =============================================================================================

// M3: "the reference scale is captured ONCE at iteration 0 and never re-captured."
// The observable consequence: relative(k) * absolute(0) == absolute(k), exactly, for every k. If
// the reference were re-captured each iteration the relative norms would sit at 1.0 forever and a
// stagnating solve would look convergent -- which is what control N5 plants.
TEST(DensityBasedConvergenceTest, M3_TheResidualReferenceScaleIsCapturedOnceAtIterationZero) {
  DensityBasedSettings s{};
  s.cfl = 0.4;
  s.mode = TimeSteppingMode::LocalPseudoTime;
  s.maxIterations = 40;
  s.residualTolerance = 1e-300;  // unreachable, so the run goes the full 40 iterations
  s.massImbalanceTolerance = 1e-300;
  s.stateChangeTolerance = 1e-300;

  Mesh mesh = MeshGeometry::createCartesian2D(6, 4, 0.6, 0.4);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.2 + 0.05 * std::sin(10.0 * mesh.cell(i).centroid().x);
    u[i] = Vector3{0.0, 0.0, 0.0};
    t[i] = 300.0;
  }
  const DensityBasedSolver solver(s, air());
  const ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);
  const DensityBasedResult res = solver.solve(mesh, q, boundaries);
  ASSERT_GE(res.history.size(), 10u);

  const auto& first = res.history.front();
  // iteration 0's relative norm is its absolute norm over itself: exactly 1
  EXPECT_EQ(first.relative.density, 1.0);
  EXPECT_EQ(first.relative.totalEnergy, 1.0);

  Real worst = 0.0;
  for (const auto& rec : res.history) {
    // relative == absolute / reference, with reference == history[0].absolute. Reconstructing the
    // reference from each record and comparing it to iteration 0's is what proves it never moved.
    if (rec.relative.density > 0.0) {
      const Real impliedReference = rec.absolute.density / rec.relative.density;
      worst = std::max(worst, std::abs(impliedReference - first.absolute.density) /
                                  std::max(first.absolute.density, 1e-300));
    }
  }
  std::printf(
      "\n  M3 reference scale over %zu iterations: worst implied-vs-iteration-0 drift %.3e\n"
      "     (relative residual moved %.6e -> %.6e, so it is NOT pinned at 1)\n",
      res.history.size(), worst, first.relative.density, res.history.back().relative.density);
  EXPECT_LE(worst, 8.0 * kEps);
  // and the relative residual must actually MOVE -- otherwise "captured once" is untested because
  // absolute never changed either.
  EXPECT_NE(res.history.back().relative.density, first.relative.density);
}

// M6: "steady criteria are evaluated ONLY in LocalPseudoTime mode."
// The same state, the same tolerances, in the two modes. In LocalPseudoTime the steady criteria are
// reachable and the run reports Converged. In GlobalPhysical the SAME tolerances must never produce
// Converged -- reaching the end of a transient run is TimeComplete, a different fact.
TEST(DensityBasedConvergenceTest, M6_SteadyCriteriaAreNotEvaluatedInGlobalPhysicalMode) {
  Mesh mesh = MeshGeometry::createCartesian2D(6, 4, 0.6, 0.4);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.2 + 0.05 * std::sin(10.0 * mesh.cell(i).centroid().x);
    u[i] = Vector3{0.0, 0.0, 0.0};
    t[i] = 300.0;
  }

  DensityBasedSettings steady{};
  steady.cfl = 0.4;
  steady.mode = TimeSteppingMode::LocalPseudoTime;
  steady.maxIterations = 4000;
  steady.residualTolerance = 1e-2;
  steady.massImbalanceTolerance = 1e-2;
  steady.stateChangeTolerance = 1e-2;

  const DensityBasedSolver pseudo(steady, air());
  const ConservedField q = pseudo.fromPrimitive(mesh, rho, u, t);
  const auto boundaries = allPatches(mesh, BoundaryKind::InviscidWall);
  const DensityBasedResult a = pseudo.solve(mesh, q, boundaries);
  ASSERT_EQ(a.status, DensityBasedStatus::Converged)
      << "the steady criteria are unreachable even in pseudo-time, so the GlobalPhysical half of "
         "this test would prove nothing";

  // the SAME tolerances, transient mode, an end time long enough to run past where pseudo-time
  // declared convergence
  DensityBasedSettings transient = steady;
  transient.mode = TimeSteppingMode::GlobalPhysical;
  transient.endTime = 1e-3;
  transient.maxIterations = static_cast<Index>(a.iterations) + 50;
  const DensityBasedSolver physical(transient, air());
  const DensityBasedResult b = physical.solve(mesh, q, boundaries);

  std::printf(
      "\n  M6 same state, same tolerances:\n"
      "     LocalPseudoTime  -> status %d (Converged) after %zu iterations\n"
      "     GlobalPhysical   -> status %d after %zu iterations -- Converged is %s\n",
      static_cast<int>(a.status), static_cast<std::size_t>(a.iterations),
      static_cast<int>(b.status), static_cast<std::size_t>(b.iterations),
      b.status == DensityBasedStatus::Converged ? "*** REACHED ***" : "never reached");
  EXPECT_NE(b.status, DensityBasedStatus::Converged);
}
