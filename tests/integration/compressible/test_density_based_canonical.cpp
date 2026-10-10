// P17.8 -- the density-based solver's canonical integration cases.
//
// Frozen gate: results/p17/density_based_solver/acceptance_gate.md
// (sha256 a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e)
//
// The three cases here are the ones that need a real evolution over a real mesh family rather than
// a single assembled residual, which is why they live beside the other integration suites rather
// than in the unit file:
//
//   C4    a SMOOTH wave with an analytic reference, plus a refinement family and a measured order
//   G2    an OPEN domain: the change in the conserved totals against the integrated boundary flux
//   §27   a low-Mach sanity run -- the density-based path must stay physically valid as Ma -> 0,
//         and its conditioning there is REPORTED rather than claimed to be good
//
// WHAT IS NOT HERE. No Sod tube, no normal shock, no shock-position or jump-ratio measurement.
// Those are P17.9's and the authorization forbids starting them (§49); the frozen gate's §14 says
// the same. A smooth advected pulse is deliberately the opposite of a shock case: it is chosen
// BECAUSE it is smooth, so a convergence order means something.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

namespace {

using cfd::Index;
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
using cfd::compressible::makePrimitiveState;
using cfd::compressible::PrimitiveState;
using cfd::compressible::ReconstructedFaceState;
using cfd::compressible::reconstructFirstOrderBoundary;
using cfd::compressible::ResidualField;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TimeSteppingMode;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

constexpr Real kEps = 2.2204460492503131e-16;
constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;

// The zero-reference convention the P17.7 and P17.8 unit suites use, so numbers compare directly.
TotalEnergyState air() {
  const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(kR, kCp), SensibleEnthalpy(cp, 0.0, 0.0));
}

DensityBasedBoundaries throughFlow(const Mesh& mesh, const PrimitiveState& inflow) {
  DensityBasedBoundaries b;
  for (const auto& patch : mesh.boundaryPatches()) {
    DensityBasedBoundary entry{};
    const std::string name(patch.name());
    if (name == "left") {
      entry.kind = BoundaryKind::SupersonicInflow;
      entry.prescribed = inflow;
    } else if (name == "right") {
      entry.kind = BoundaryKind::SupersonicOutflow;
    } else {
      // top and bottom. With v = 0 the mirrored exterior state equals the interior state exactly,
      // so these faces carry pure pressure and the two sides cancel -- the case stays 1D.
      entry.kind = BoundaryKind::InviscidWall;
    }
    b[name] = entry;
  }
  return b;
}

Real totalOf(const Mesh& mesh, const ScalarField& f) {
  Real s = 0.0;
  for (Index i = 0; i < static_cast<Index>(mesh.numberOfCells()); ++i)
    s += f[i] * mesh.cell(i).volume();
  return s;
}

}  // namespace

// =============================================================================================
// C4 -- a SMOOTH wave against its analytic propagation, with a refinement family
// =============================================================================================
//
// THE CASE, and why this one. A smooth Gaussian density pulse on a UNIFORM, SUPERSONIC, uniform-
// pressure background is an EXACT solution of the Euler equations: with p and U constant the
// momentum and energy equations are satisfied identically and the density profile is simply
// advected,
//
//     rho(x, t) = rho_0(x - u t) ,      p(x, t) = p_0 ,      u(x, t) = u_0
//
// so the reference is analytic and needs no manufactured source term and no external solver.
//
// WHY THE BOUNDARIES ARE EXACT HERE, which is the part that makes the measurement clean. The flow
// is supersonic (Ma ~ 2), so every characteristic travels downstream. The left boundary is a
// SUPERSONIC INFLOW whose prescribed state is the uniform background -- and the background IS the
// exact solution near the inlet for all time, because the pulse only ever moves away from it. The
// right boundary is a SUPERSONIC OUTFLOW, well posed for the same reason. So the error measured
// below is the SCHEME's, not a boundary artefact, and the run stops while the pulse is still well
// inside the domain.
//
// WHAT IS GATED. The frozen criterion C4 says: "reported with its measured order; NOT gated on an
// invented order". So the observed order is REPORTED, and what is asserted is only what can be
// derived without inventing a constant: the error must DECREASE under refinement, and p and u must
// stay uniform to the same detection threshold C3 uses.
TEST(DensityBasedCanonicalIntegration, C4_SmoothAdvectedPulseConvergesToTheAnalyticSolution) {
  const Real rho0 = 1.2;
  const Real t0 = 300.0;
  const Real u0 = 700.0;  // a(300 K) ~ 347 m/s, so Ma ~ 2.0 -- genuinely supersonic
  const Real amplitude = 0.2;
  const Real lx = 1.0;
  const Real x0 = 0.30;
  const Real sigma = 0.06;
  const Real endTime =
      3.0e-4;  // u0 * t = 0.21, so the pulse ends near x = 0.51, far from both ends

  const Real p0 = rho0 * kR * t0;  // the UNIFORM pressure the wave carries
  const auto pulse = [&](Real x) {
    const Real d = (x - x0) / sigma;
    return rho0 * (1.0 + amplitude * std::exp(-0.5 * d * d));
  };
  const auto exactAt = [&](Real x, Real t) { return pulse(x - u0 * t); };
  // An entropy wave has UNIFORM pressure, so the temperature must vary with the density:
  // T = p0 / (rho R). Holding T constant instead makes p vary with rho, which is an ACOUSTIC
  // initial condition, not an entropy one -- it launches real pressure waves and there is then no
  // closed-form reference to compare against. The first draft of this test did exactly that and
  // measured 8.5 % pressure non-uniformity and an L1 error that barely fell under refinement.
  const auto temperatureFor = [&](Real density) { return p0 / (density * kR); };

  const Index family[] = {64, 128, 256};
  std::vector<Real> errors;
  std::vector<Real> spacings;

  std::printf("\n  C4 smooth advected pulse, Ma = %.2f, t_end = %.3e s\n",
              u0 / std::sqrt(1.4 * kR * t0), endTime);
  std::printf("    %6s %10s %14s %14s %14s %10s\n", "nx", "dx", "L1(rho)", "max|dp|/p", "max|du|/u",
              "steps");

  for (const Index nx : family) {
    DensityBasedSettings s{};
    s.cfl = 0.4;
    s.mode = TimeSteppingMode::GlobalPhysical;
    s.endTime = endTime;
    s.maxIterations = 20000;
    const DensityBasedSolver solver(s, air());

    Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, lx, lx / static_cast<Real>(nx));
    const Index n = static_cast<Index>(mesh.numberOfCells());
    ScalarField rho(n);
    VectorField u(n);
    ScalarField t(n);
    for (Index i = 0; i < n; ++i) {
      rho[i] = pulse(mesh.cell(i).centroid().x);
      u[i] = Vector3{u0, 0.0, 0.0};
      t[i] = temperatureFor(rho[i]);
    }
    const ConservedField q0 = solver.fromPrimitive(mesh, rho, u, t);
    const PrimitiveState background =
        makePrimitiveState(air(), rho0, Vector3{u0, 0.0, 0.0}, temperatureFor(rho0));
    const auto boundaries = throughFlow(mesh, background);

    const DensityBasedResult res = solver.solve(mesh, q0, boundaries);
    ASSERT_EQ(res.status, DensityBasedStatus::TimeComplete)
        << "nx = " << nx << " status " << static_cast<int>(res.status) << " -- " << res.diagnostic;
    ASSERT_EQ(res.totalRetries, 0) << "a smooth CFL-0.4 case should never need a retry";

    const auto finalPrim = solver.toPrimitive(res.state);
    Real l1 = 0.0;
    Real volume = 0.0;
    Real worstP = 0.0;
    Real worstU = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real x = mesh.cell(i).centroid().x;
      const Real v = mesh.cell(i).volume();
      l1 += std::abs(finalPrim[i].density - exactAt(x, res.physicalTime())) * v;
      volume += v;
      worstP = std::max(worstP, std::abs(finalPrim[i].pressure - p0) / p0);
      worstU = std::max(worstU, std::abs(finalPrim[i].velocity.x - u0) / u0);
    }
    l1 /= volume;
    errors.push_back(l1);
    spacings.push_back(lx / static_cast<Real>(nx));
    std::printf("    %6zu %10.4e %14.6e %14.6e %14.6e %10zu\n", static_cast<std::size_t>(nx),
                lx / static_cast<Real>(nx), l1, worstP, worstU,
                static_cast<std::size_t>(res.iterations));

    // p and u are analytically uniform for this wave. C3's derivation applies unchanged: this is a
    // DETECTION threshold for a scheme that puts a spurious pressure or velocity signal on a
    // density variation, not an accuracy claim.
    EXPECT_LE(worstP, 1e-9) << "nx = " << nx;
    EXPECT_LE(worstU, 1e-9) << "nx = " << nx;
  }

  std::printf("    observed order between successive refinements:\n");
  for (std::size_t k = 1; k < errors.size(); ++k) {
    const Real order =
        std::log(errors[k - 1] / errors[k]) / std::log(spacings[k - 1] / spacings[k]);
    std::printf("      nx %zu -> %zu : %.4f\n", static_cast<std::size_t>(family[k - 1]),
                static_cast<std::size_t>(family[k]), order);
    // REPORTED, not gated on a value. What IS asserted is the only thing derivable without
    // inventing a constant: refining must reduce the error.
    EXPECT_LT(errors[k], errors[k - 1]) << "refinement from nx=" << family[k - 1] << " to "
                                        << family[k] << " did not reduce the error";
  }
}

// =============================================================================================
// G2 -- an OPEN domain: the conserved totals against the integrated boundary flux
// =============================================================================================
//
// §26: "change in domain conserved quantity + integrated boundary flux - integrated source ~= 0".
// There is no source term in P17.8 (Euler, no body force), so the statement is a two-term balance.
//
// The loop below is driven with the solver's PUBLIC pieces -- assembleResidual, globalTimeStep,
// applyUpdate -- which the header exposes precisely so sections 34/35 can be tested in isolation.
// That is what makes the boundary flux measurable per step: solve() reports history, not states.
//
// TWO separate things are checked, and the order matters:
//   (i)  sum_P R_P equals the sum of the BOUNDARY face fluxes alone, computed independently from
//        the boundary faces. That is the total-level consequence of the internal +F/-F
//        cancellation, and it is measured here rather than assumed from R1.
//   (ii) the balance itself: Delta(sum_P Q_P V_P) + sum_steps dt * (boundary flux) == 0.
TEST(DensityBasedCanonicalIntegration, G2_OpenDomainBalancesAgainstTheIntegratedBoundaryFlux) {
  DensityBasedSettings s{};
  s.cfl = 0.4;
  s.mode = TimeSteppingMode::GlobalPhysical;
  s.endTime = 1.0;  // not reached; the loop below is driven manually for a fixed step count
  s.maxIterations = 1000;
  const DensityBasedSolver solver(s, air());

  const Real rho0 = 1.1;
  const Real t0 = 310.0;
  const Real u0 = 650.0;  // supersonic, so both open boundaries are well posed
  Mesh mesh = MeshGeometry::createCartesian2D(20, 3, 1.0, 0.15);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = rho0 * (1.0 + 0.15 * std::sin(6.0 * x));  // non-uniform, so the fluxes are real
    u[i] = Vector3{u0, 0.0, 0.0};
    t[i] = t0;
  }
  ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  const PrimitiveState inflow = makePrimitiveState(air(), rho0, Vector3{u0, 0.0, 0.0}, t0);
  const auto boundaries = throughFlow(mesh, inflow);
  const Real gamma = solver.thermodynamics().thermodynamics().specificHeatRatio();

  const Real massBefore = totalOf(mesh, q.density);
  const Real energyBefore = totalOf(mesh, q.totalEnergyDensity);
  Real momentumBefore = 0.0;
  for (Index i = 0; i < n; ++i) momentumBefore += q.momentumDensity[i].x * mesh.cell(i).volume();

  Real massIntegral = 0.0;
  Real momentumIntegral = 0.0;
  Real energyIntegral = 0.0;
  Real worstTotalVsBoundary = 0.0;
  Real fluxScale = 0.0;
  Index boundaryFaces = 0;
  const Index steps = 40;

  for (Index step = 0; step < steps; ++step) {
    const auto prim = solver.toPrimitive(q);
    const ResidualField r = solver.assembleResidual(mesh, prim, boundaries);
    const Real dt = solver.globalTimeStep(mesh, prim);

    // the boundary flux, measured directly from the boundary faces
    Real bMass = 0.0;
    Real bMomX = 0.0;
    Real bEnergy = 0.0;
    boundaryFaces = 0;
    for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) {
      const auto& face = mesh.face(f);
      if (!face.isBoundary()) continue;
      ++boundaryFaces;
      const Index owner = face.owner();
      const Vector3 av = face.areaVector();
      const Real area = face.area();
      const Vector3 nrm{av.x / area, av.y / area, av.z / area};
      const auto* treatment = [&]() -> const DensityBasedBoundary* {
        for (const auto& patch : mesh.boundaryPatches()) {
          for (const Index id : patch.faceIds()) {
            if (id == f) return &boundaries.at(std::string(patch.name()));
          }
        }
        return nullptr;
      }();
      ASSERT_NE(treatment, nullptr) << "boundary face " << f << " belongs to no patch";
      const ReconstructedFaceState st =
          reconstructFirstOrderBoundary(prim[owner], exteriorState(*treatment, prim[owner], nrm));
      const auto h = hllcFluxChecked(st.left, st.right, nrm, gamma);
      const FluxVector F = integrateOverArea(h.flux, area);
      bMass += F.mass;
      bMomX += F.momentum.x;
      bEnergy += F.energy;
      fluxScale = std::max(fluxScale, std::abs(F.energy));
    }

    // (i) the total residual IS the boundary flux -- every internal face cancelled
    Real sumRmass = 0.0;
    Real sumRmomX = 0.0;
    Real sumRenergy = 0.0;
    for (Index i = 0; i < n; ++i) {
      sumRmass += r.density[i];
      sumRmomX += r.momentum[i].x;
      sumRenergy += r.totalEnergy[i];
    }
    const Real scale = std::max(std::abs(bEnergy), 1e-300);
    worstTotalVsBoundary = std::max(worstTotalVsBoundary, std::abs(sumRenergy - bEnergy) / scale);

    massIntegral += dt * bMass;
    momentumIntegral += dt * bMomX;
    energyIntegral += dt * bEnergy;
    q = DensityBasedSolver::applyUpdate(mesh, q, r, std::vector<Real>(n, dt));
  }

  const Real massAfter = totalOf(mesh, q.density);
  const Real energyAfter = totalOf(mesh, q.totalEnergyDensity);
  Real momentumAfter = 0.0;
  for (Index i = 0; i < n; ++i) momentumAfter += q.momentumDensity[i].x * mesh.cell(i).volume();

  // Delta(total) + integrated boundary flux == 0
  const Real massResidual = (massAfter - massBefore) + massIntegral;
  const Real momentumResidual = (momentumAfter - momentumBefore) + momentumIntegral;
  const Real energyResidual = (energyAfter - energyBefore) + energyIntegral;

  // The frozen bound: 8 eps x N_faces x the flux scale, accumulated over the steps. The scale for
  // each quantity is its own integrated boundary flux, which is the largest number entering the
  // sum.
  const Real f = 8.0 * kEps * static_cast<Real>(boundaryFaces) * static_cast<Real>(steps);
  const Real massBound = f * std::max(std::abs(massIntegral), std::abs(massAfter));
  const Real momentumBound = f * std::max(std::abs(momentumIntegral), std::abs(momentumAfter));
  const Real energyBound = f * std::max(std::abs(energyIntegral), std::abs(energyAfter));

  std::printf("\n  G2 open domain, %zu cells, %zu boundary faces, %zu steps\n",
              static_cast<std::size_t>(n), static_cast<std::size_t>(boundaryFaces),
              static_cast<std::size_t>(steps));
  std::printf("    (i)  worst |sum_P R_P - boundary flux| / |boundary flux| : %.3e\n",
              worstTotalVsBoundary);
  std::printf("    (ii) mass      d(total) %+.10e  + integral %+.10e  -> %.3e  (bound %.3e)\n",
              massAfter - massBefore, massIntegral, std::abs(massResidual), massBound);
  std::printf("    (ii) momentum  d(total) %+.10e  + integral %+.10e  -> %.3e  (bound %.3e)\n",
              momentumAfter - momentumBefore, momentumIntegral, std::abs(momentumResidual),
              momentumBound);
  std::printf("    (ii) energy    d(total) %+.10e  + integral %+.10e  -> %.3e  (bound %.3e)\n",
              energyAfter - energyBefore, energyIntegral, std::abs(energyResidual), energyBound);

  // (i) is a cancellation of the internal faces inside a sum, so it is round-off, not exact.
  EXPECT_LE(worstTotalVsBoundary, 8.0 * kEps * static_cast<Real>(n));
  EXPECT_LE(std::abs(massResidual), massBound);
  EXPECT_LE(std::abs(momentumResidual), momentumBound);
  EXPECT_LE(std::abs(energyResidual), energyBound);
  // and the case must actually TRANSPORT something, or the balance is 0 == 0
  EXPECT_GT(std::abs(massIntegral), 0.0);
  EXPECT_GT(std::abs(energyIntegral), 0.0);
}

// =============================================================================================
// G1's THIRD quantity -- momentum in a CLOSED domain, which is a balance and not a constant
// =============================================================================================
//
// The frozen Group G criterion names three sums: sum_P rho V, sum_P rhoU V and sum_P rhoE V. The
// unit-test G1 asserts the first and the third and only PRINTS the second, and the reason it could
// not simply assert it is physical rather than an oversight in the assertion:
//
//     IN A CLOSED BOX WITH A NON-UNIFORM PRESSURE FIELD, TOTAL MOMENTUM IS NOT CONSTANT.
//     The walls exert a net force. G1's own log records x-momentum moving 0.000000e+00 ->
//     2.863040e+01, and that is CORRECT physics, not a leak. Asserting "unchanged" there would be
//     asserting something false, and choosing a case where it happened to hold would be choosing
//     the case to fit the criterion.
//
// The correct statement is the same balance §26 asks for on open boundaries, applied to a closed
// one: the change in total momentum must equal the integrated wall force, to round-off. That needs
// the per-step boundary flux, so it uses the public pieces directly, as G2 does.
TEST(DensityBasedCanonicalIntegration, G1_ClosedDomainMomentumMatchesTheIntegratedWallForce) {
  DensityBasedSettings s{};
  s.cfl = 0.4;
  s.mode = TimeSteppingMode::GlobalPhysical;
  s.endTime = 1.0;  // not reached; the loop below is driven manually
  s.maxIterations = 1000;
  const DensityBasedSolver solver(s, air());

  Mesh mesh = MeshGeometry::createCartesian2D(10, 8, 1.0, 0.8);
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField u(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    rho[i] = 1.0 + 0.3 * std::sin(6.0 * x);  // the same state the unit-test G1 uses
    u[i] = Vector3{0.0, 0.0, 0.0};
    t[i] = 300.0 + 40.0 * std::cos(6.0 * x);  // so p varies -> the walls DO exert a net force
  }
  ConservedField q = solver.fromPrimitive(mesh, rho, u, t);
  DensityBasedBoundaries walls;
  for (const auto& patch : mesh.boundaryPatches()) {
    DensityBasedBoundary e{};
    e.kind = BoundaryKind::InviscidWall;
    walls[patch.name()] = e;
  }
  const Real gamma = solver.thermodynamics().thermodynamics().specificHeatRatio();

  const auto totalMomentum = [&](const ConservedField& f) {
    Real mx = 0.0;
    Real my = 0.0;
    for (Index i = 0; i < n; ++i) {
      mx += f.momentumDensity[i].x * mesh.cell(i).volume();
      my += f.momentumDensity[i].y * mesh.cell(i).volume();
    }
    return std::pair<Real, Real>{mx, my};
  };
  const auto [mx0, my0] = totalMomentum(q);
  const Real mass0 = totalOf(mesh, q.density);
  const Real energy0 = totalOf(mesh, q.totalEnergyDensity);

  Real impulseX = 0.0;
  Real impulseY = 0.0;
  Real forceScale = 0.0;
  Index boundaryFaces = 0;
  const Index steps = 37;

  for (Index step = 0; step < steps; ++step) {
    const auto prim = solver.toPrimitive(q);
    const ResidualField r = solver.assembleResidual(mesh, prim, walls);
    const Real dt = solver.globalTimeStep(mesh, prim);

    Real fx = 0.0;
    Real fy = 0.0;
    boundaryFaces = 0;
    for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) {
      const auto& face = mesh.face(f);
      if (!face.isBoundary()) continue;
      ++boundaryFaces;
      const Index owner = face.owner();
      const Vector3 av = face.areaVector();
      const Real area = face.area();
      const Vector3 nrm{av.x / area, av.y / area, av.z / area};
      DensityBasedBoundary wall{};
      wall.kind = BoundaryKind::InviscidWall;
      const ReconstructedFaceState st =
          reconstructFirstOrderBoundary(prim[owner], exteriorState(wall, prim[owner], nrm));
      const auto h = hllcFluxChecked(st.left, st.right, nrm, gamma);
      const FluxVector F = integrateOverArea(h.flux, area);
      fx += F.momentum.x;
      fy += F.momentum.y;
      forceScale = std::max(forceScale, std::abs(F.momentum.x));
    }
    impulseX += dt * fx;
    impulseY += dt * fy;
    q = DensityBasedSolver::applyUpdate(mesh, q, r, std::vector<Real>(n, dt));
  }

  const auto [mx1, my1] = totalMomentum(q);
  const Real mass1 = totalOf(mesh, q.density);
  const Real energy1 = totalOf(mesh, q.totalEnergyDensity);

  // d(momentum) + integrated wall momentum flux == 0, the same convention G2 uses
  const Real residualX = (mx1 - mx0) + impulseX;
  const Real residualY = (my1 - my0) + impulseY;
  const Real bound = 8.0 * kEps * static_cast<Real>(boundaryFaces) * static_cast<Real>(steps) *
                     std::max(std::abs(impulseX), forceScale);

  std::printf("\n  G1 closed box, %zu cells, %zu wall faces, %zu steps\n",
              static_cast<std::size_t>(n), static_cast<std::size_t>(boundaryFaces),
              static_cast<std::size_t>(steps));
  std::printf("    momentum-x  d(total) %+.10e  + wall impulse %+.10e  -> %.3e  (bound %.3e)\n",
              mx1 - mx0, impulseX, std::abs(residualX), bound);
  std::printf("    momentum-y  d(total) %+.10e  + wall impulse %+.10e  -> %.3e\n", my1 - my0,
              impulseY, std::abs(residualY));
  std::printf("    mass    %.10e -> %.10e   relative %.3e\n", mass0, mass1,
              std::abs(mass1 - mass0) / mass0);
  std::printf("    energy  %.10e -> %.10e   relative %.3e\n", energy0, energy1,
              std::abs(energy1 - energy0) / energy0);

  EXPECT_LE(std::abs(residualX), bound);
  EXPECT_LE(std::abs(residualY), bound);
  // and the case must actually accelerate, or the balance is 0 == 0 and proves nothing
  EXPECT_GT(std::abs(mx1 - mx0), 1.0)
      << "the walls exerted no net force, so this test did not exercise the momentum balance";
  // mass and energy ARE constant in a sealed adiabatic box, and stay asserted here too
  EXPECT_LE(std::abs(mass1 - mass0) / mass0, 8.0 * kEps * static_cast<Real>(n));
  EXPECT_LE(std::abs(energy1 - energy0) / energy0, 8.0 * kEps * static_cast<Real>(n));
}

// =============================================================================================
// §27 -- a low-Mach sanity run. P17.8 does NOT replace P17.6.
// =============================================================================================
//
// The authorization is explicit: "Do not require it to outperform CompressibleSIMPLE. The
// pressure-based solver remains the primary low-Mach reference. If density-based conditioning
// deteriorates as Ma -> 0, document that honestly."
//
// So this test asserts only that the density-based path stays PHYSICALLY VALID and
// THERMODYNAMICALLY CONSISTENT at low Mach. The conditioning -- how the acoustic time-step limit
// behaves as Ma -> 0 -- is MEASURED and printed, and the measurement is the deliverable. No claim
// of low-Mach accuracy or efficiency is made anywhere.
TEST(DensityBasedCanonicalIntegration, LowMachRemainsValidAndItsConditioningIsReported) {
  const Real rho0 = 1.2;
  const Real t0 = 300.0;
  const Real a0 = std::sqrt((kCp / (kCp - kR)) * kR * t0);

  std::printf("\n  §27 low-Mach density-based sanity (a = %.3f m/s)\n", a0);
  std::printf("    %8s %10s %12s %12s %12s %12s %12s\n", "Ma", "u [m/s]", "dt_0 [s]", "min rho",
              "min p", "min T", "EOS err");

  // The conditioning measure uses the FIRST step's dt, which is the unclipped CFL-limited step.
  // The LAST step's dt is truncated to land exactly on endTime, so reading conditioning off it
  // measures the end-time arithmetic rather than the stability limit -- the first draft did that
  // and produced a ratio that jumped around with no physical meaning.
  Real firstMachDt = -1.0;
  Real lastMachDt = -1.0;
  for (const Real mach : {0.2, 0.05, 0.01, 0.002}) {
    const Real u0 = mach * a0;
    DensityBasedSettings s{};
    s.cfl = 0.4;
    s.mode = TimeSteppingMode::GlobalPhysical;
    s.maxIterations = 200;
    // an end time long enough for 40-odd acoustic steps at every Mach number
    s.endTime = 40.0 * 0.4 * (0.5 / 20.0) / (a0 * 1.5);
    const DensityBasedSolver solver(s, air());

    Mesh mesh = MeshGeometry::createCartesian2D(20, 2, 0.5, 0.05);
    const Index n = static_cast<Index>(mesh.numberOfCells());
    ScalarField rho(n);
    VectorField u(n);
    ScalarField t(n);
    for (Index i = 0; i < n; ++i) {
      rho[i] = rho0 * (1.0 + 0.01 * std::sin(12.0 * mesh.cell(i).centroid().x));
      u[i] = Vector3{u0, 0.0, 0.0};
      t[i] = t0;
    }
    const ConservedField q0 = solver.fromPrimitive(mesh, rho, u, t);
    const auto boundaries = [&] {
      DensityBasedBoundaries b;
      for (const auto& patch : mesh.boundaryPatches()) {
        DensityBasedBoundary e{};
        e.kind = BoundaryKind::InviscidWall;  // closed box: subsonic open BCs are NOT implemented
        b[patch.name()] = e;
      }
      return b;
    }();

    const DensityBasedResult res = solver.solve(mesh, q0, boundaries);
    ASSERT_TRUE(res.status == DensityBasedStatus::TimeComplete ||
                res.status == DensityBasedStatus::MaxIterations)
        << "Ma = " << mach << " status " << static_cast<int>(res.status) << " -- "
        << res.diagnostic;
    ASSERT_FALSE(res.history.empty());

    const auto& last = res.history.back();
    const Real dt0 = res.history.front().dt;
    const auto prim = solver.toPrimitive(res.state);
    // thermodynamic consistency: p must equal rho R T through the AUTHORITATIVE EOS, per cell
    Real eosError = 0.0;
    for (const auto& st : prim) {
      const Real expected = st.density * kR * st.temperature;
      eosError = std::max(eosError, std::abs(st.pressure - expected) / expected);
    }
    std::printf("    %8.3f %10.3f %12.5e %12.6f %12.1f %12.4f %12.3e\n", mach, u0, dt0,
                last.minDensity, last.minPressure, last.minTemperature, eosError);

    EXPECT_GT(last.minDensity, 0.0) << "Ma = " << mach;
    EXPECT_GT(last.minPressure, 0.0) << "Ma = " << mach;
    EXPECT_GT(last.minTemperature, 0.0) << "Ma = " << mach;
    EXPECT_LE(eosError, 1e-12) << "Ma = " << mach;  // G2's inherited closure tolerance
    EXPECT_EQ(res.totalRetries, 0) << "Ma = " << mach << " needed a retry at CFL 0.4";

    if (firstMachDt < 0.0) firstMachDt = dt0;
    lastMachDt = dt0;
  }

  // THE CONDITIONING, reported rather than claimed. The explicit step is limited by |u| + a, so as
  // Ma -> 0 the step stops shrinking and stops improving: it saturates at the ACOUSTIC limit while
  // the physically interesting convection time scale grows like 1/Ma. That is the well-known
  // low-Mach stiffness of an explicit density-based scheme, and it is why P17.6's pressure-based
  // path remains the low-Mach reference. The assertion only records that dt saturates rather than
  // collapsing -- it is not a quality claim.
  // dt_P = CFL V_P / sum_f (|u.n| + a) A. As Ma -> 0 the |u.n| term vanishes and dt tends to the
  // purely acoustic limit CFL V / (a sum_f A) -- it SATURATES, it does not grow like 1/Ma. So the
  // ratio below is bounded above by (a + |u|_highMach) / a = 1 + 0.2 = 1.2, and must exceed 1: the
  // step gets slightly LONGER as the convective contribution disappears, and then stops improving.
  // Both bounds are derived from the formula, not fitted.
  const Real ratio = lastMachDt / firstMachDt;
  std::printf("    conditioning: dt_0(Ma=0.002) / dt_0(Ma=0.2) = %.6f\n", ratio);
  std::printf(
      "    dt is set by |u| + a, so it SATURATES at the acoustic limit as Ma -> 0 while the\n"
      "    convective time scale grows like 1/Ma: the step stops shrinking but the number of\n"
      "    steps needed to convect one domain length grows without bound. Explicit density-\n"
      "    based stiffness at low Mach, stated plainly. P17.6's CompressibleSIMPLE remains\n"
      "    the low-Mach reference; P17.8 makes no low-Mach accuracy or efficiency claim.\n");
  EXPECT_GT(ratio, 1.0);
  EXPECT_LT(ratio, 1.2 + 1e-9);
}
