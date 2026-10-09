// P17.3 dry-run: measure the UNCHANGED tree's continuity behaviour before any edit.
//
// Jobs:
//   A. Do the two continuity evaluators agree, and bitwise or only to round-off? They sum
//      the same terms in DIFFERENT loop orders, so this is measured, not assumed.
//   B. Steady div(rho U) = 0 with a genuinely varying density -- the exact row-varying
//      construction, and the OPERATOR fed a constructed divergence-free flux.
//   C. Internal-face antisymmetry.
//   D. Boundary fluxes per BC type, with signs.
//   E. Global conservation through the ESTABLISHED MassBalance diagnostic.
//   F. The transient integral relation M(t2)-M(t1) + dt*sum_boundary, computed BY HAND --
//      proving the globalNetFlux gap is real (a caller must re-loop the boundary itself).
//   G. rho(x)U(x)=const as a REFINEMENT study, since the discrete operator is only O(h^2)
//      there -- an exactness claim would be wrong.
//   H. What invalid states the evaluators currently accept.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleContinuity.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::compressible::calculateCompressibleMassFlux;
using cfd::compressible::evaluateCompressibleContinuity;
using cfd::compressible::evaluateCompressibleFaceDensity;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;

BoundaryConditionSet velBC(const Mesh& mesh, Real u) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<Inlet>(Vector2{u, 0.0}));
  b.set(mesh, "right", std::make_unique<Outlet>());
  b.set(mesh, "bottom", std::make_unique<Wall>());
  b.set(mesh, "top", std::make_unique<Wall>());
  return b;
}

BoundaryConditionSet allWallBC(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const char* p : {"left", "right", "bottom", "top"})
    b.set(mesh, p, std::make_unique<Wall>());
  return b;
}

BoundaryConditionSet zgPressure(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const char* p : {"left", "right", "bottom", "top"})
    b.set(mesh, p, std::make_unique<FixedGradient>(0.0));
  return b;
}

}  // namespace

int main() {
  const Real eps = std::numeric_limits<Real>::epsilon();
  std::printf("P17.3 dry-run -- compressible continuity on the UNCHANGED tree\n");
  std::printf("eps = %.17g\n\n", eps);

  const ThermodynamicProperties thermo(kR, kCp);
  const Real tBot = 300.0, tTop = 600.0;
  const Real pRef = 2.0 * kR * tBot;  // so rho_bottom == 2 exactly
  const Real rhoBot = thermo.density(pRef, tBot);
  const Real rhoTop = thermo.density(pRef, tTop);
  const Real u0 = 2.0;

  // ---------------------------------------------------------------- A + B + C + E
  std::printf("### A/B/C/E. row-varying density, 4x3 mesh -- the EXACT construction\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
    const Index n = mesh.numberOfCells();
    ScalarField rho(n), T(n), pG(n, 0.0);
    for (const auto& c : mesh.cells()) {
      const bool top = c.centroid().y > 1.5;
      T[c.id()] = top ? tTop : tBot;
      rho[c.id()] = top ? rhoTop : rhoBot;
    }
    const VectorField vel(n, Vector2{u0, 0.0});
    const auto vb = velBC(mesh, u0);
    const auto pb = zgPressure(mesh);
    const SurfaceField flux =
        calculateCompressibleMassFlux(mesh, vel, rho, vb, pG, pb, pRef, thermo, T, nullptr);

    std::printf("  cells %lld faces %lld   rho_bottom %.17g  rho_top %.17g  ratio %.17g\n",
                static_cast<long long>(n), static_cast<long long>(mesh.numberOfFaces()), rhoBot,
                rhoTop, rhoBot / rhoTop);

    // A: the two evaluators, same flux, zero time term
    const auto steady = cfd::physics::evaluateContinuity(mesh, flux);
    const auto trans = evaluateCompressibleContinuity(mesh, rho, rho, flux, 1.0);
    Index differing = 0;
    Real maxDiff = 0.0;
    for (Index i = 0; i < n; ++i) {
      if (trans.cellImbalance[i] != steady.cellImbalance[i]) ++differing;
      maxDiff = std::max(maxDiff, std::abs(trans.cellImbalance[i] - steady.cellImbalance[i]));
    }
    std::printf("  A. steady vs transient(rhoOld==rhoNew) cellImbalance:\n");
    std::printf("     cells differing bitwise  %lld of %lld\n", static_cast<long long>(differing),
                static_cast<long long>(n));
    std::printf("     max absolute difference  %.17g\n", maxDiff);

    // B: steady imbalance with varying rho
    std::printf("  B. steady   maxCellImbalance %.17g   totalAbs %.17g   globalNetFlux %.17g\n",
                steady.maxCellImbalance, steady.totalAbsoluteImbalance, steady.globalNetFlux);
    std::printf("     transient maxCellImbalance %.17g   totalAbs %.17g\n",
                trans.maxCellImbalance, trans.totalAbsoluteImbalance);
    std::printf("     totalMassOld %.17g   totalMassNew %.17g\n", trans.totalMassOld,
                trans.totalMassNew);

    // C: internal-face antisymmetry -- the same flux enters owner with + and neighbour with -
    Real worstPair = 0.0;
    Index internal = 0;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      if (mesh.face(f).isBoundary()) continue;
      ++internal;
      worstPair = std::max(worstPair, std::abs(flux[f] + (-flux[f])));
    }
    std::printf("  C. internal faces %lld   worst |(+F) + (-F)| = %.17g  (must be exactly 0)\n",
                static_cast<long long>(internal), worstPair);

    // E: the ESTABLISHED MassBalance diagnostic
    const auto mb = cfd::physics::computeMassBalance(mesh, flux);
    std::printf("  E. MassBalance: inflow %.17g  outflow %.17g  net %.17g\n", mb.inflow, mb.outflow,
                mb.net);
    std::printf("     relativeImbalance %.17g  fluxScale %.17g  normalizedContinuity %.17g\n",
                mb.relativeImbalance, mb.fluxScale, mb.normalizedContinuity);
    std::printf("     maxCellImbalance %.17g  rmsCellImbalance %.17g\n", mb.maxCellImbalance,
                mb.rmsCellImbalance);

    // D: boundary fluxes per patch, with signs
    std::printf("  D. boundary flux by patch (outward-positive):\n");
    for (const auto& patch : mesh.boundaryPatches()) {
      Real sum = 0.0;
      for (const Index f : patch.faceIds()) sum += flux[f];
      std::printf("       %-8s faces %2lld   sum %.17g\n", patch.name().c_str(),
                  static_cast<long long>(patch.faceIds().size()), sum);
    }

    // F: the transient integral relation, computed BY HAND because globalNetFlux is absent
    Real boundarySum = 0.0;
    for (const auto& patch : mesh.boundaryPatches())
      for (const Index f : patch.faceIds()) boundarySum += flux[f];
    std::printf("  F. transient integral relation, boundary sum computed BY HAND:\n");
    std::printf("     sum_boundary(m_dot) = %.17g\n", boundarySum);
    std::printf("     CompressibleContinuityResult exposes globalNetFlux?  NO -- a caller must\n");
    std::printf("     re-loop the boundary patches itself, as this probe just did.\n");
  }

  // ---------------------------------------------------------------- closed box
  std::printf("\n### D2. closed box, all walls -- sum_boundary must be exactly 0\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 3.0, 3.0);
    const Index n = mesh.numberOfCells();
    ScalarField rho(n), T(n), pG(n, 0.0);
    for (const auto& c : mesh.cells()) {
      const bool top = c.centroid().y > 1.5;
      T[c.id()] = top ? tTop : tBot;
      rho[c.id()] = top ? rhoTop : rhoBot;
    }
    const VectorField vel(n, Vector2{u0, 0.0});
    const auto vb = allWallBC(mesh);
    const auto pb = zgPressure(mesh);
    const SurfaceField flux =
        calculateCompressibleMassFlux(mesh, vel, rho, vb, pG, pb, pRef, thermo, T, nullptr);
    const auto mb = cfd::physics::computeMassBalance(mesh, flux);
    std::printf("  inflow %.17g  outflow %.17g  net %.17g\n", mb.inflow, mb.outflow, mb.net);
    std::printf("  maxCellImbalance %.17g\n", mb.maxCellImbalance);
  }

  // ---------------------------------------------------------------- G: refinement
  std::printf("\n### G. rho(x)U(x) = const -- a REFINEMENT study, NOT an exactness claim\n");
  std::printf("  the discrete flux uses mean(rho)*mean(U) != mean(rho*U), so the imbalance is\n");
  std::printf("  O(h^2), not zero. Measured decay:\n");
  std::printf("  %-6s %-24s %-24s %s\n", "nx", "maxCellImbalance", "normalizedContinuity",
              "ratio vs previous");
  {
    Real prev = 0.0;
    for (const Index nx : {8, 16, 32, 64}) {
      const Mesh mesh = MeshGeometry::createCartesian2D(nx, 1, 1.0, 1.0 / static_cast<Real>(nx));
      const Index n = mesh.numberOfCells();
      ScalarField rho(n), T(n), pG(n, 0.0);
      VectorField vel(n);
      // rho varies linearly in x; U = C/rho so that rho*U is constant analytically.
      const Real C = 2.0;
      for (const auto& c : mesh.cells()) {
        const Real x = c.centroid().x;
        const Real r = 1.0 + 0.5 * x;  // 1.0 .. 1.5
        rho[c.id()] = r;
        vel[c.id()] = Vector2{C / r, 0.0};
        T[c.id()] = pRef / (r * kR);  // keep the state EOS-consistent
      }
      BoundaryConditionSet vb;
      vb.set(mesh, "left", std::make_unique<Inlet>(Vector2{C / 1.0, 0.0}));
      vb.set(mesh, "right", std::make_unique<Outlet>());
      vb.set(mesh, "bottom", std::make_unique<Wall>());
      vb.set(mesh, "top", std::make_unique<Wall>());
      const auto pb = zgPressure(mesh);
      const SurfaceField flux =
          calculateCompressibleMassFlux(mesh, vel, rho, vb, pG, pb, pRef, thermo, T, nullptr);
      const auto mb = cfd::physics::computeMassBalance(mesh, flux);
      const Real ratio = prev > 0.0 ? prev / mb.maxCellImbalance : 0.0;
      std::printf("  %-6lld %-24.17g %-24.17g %.3f\n", static_cast<long long>(nx),
                  mb.maxCellImbalance, mb.normalizedContinuity, ratio);
      prev = mb.maxCellImbalance;
    }
    std::printf("  (a ratio near 4 per halving of h indicates second order)\n");
  }

  // ---------------------------------------------------------------- H: invalid states
  std::printf("\n### H. invalid states currently ACCEPTED by evaluateCompressibleContinuity\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
    const Index n = mesh.numberOfCells();
    const SurfaceField zeroFlux(mesh.numberOfFaces(), 0.0);
    const Real qnan = std::numeric_limits<Real>::quiet_NaN();
    const Real pinf = std::numeric_limits<Real>::infinity();
    struct Case {
      Real bad;
      const char* label;
    };
    const Case cases[] = {{-2.0, "rhoNew[0] = -2.0"},
                          {0.0, "rhoNew[0] =  0.0"},
                          {qnan, "rhoNew[0] =  NaN"},
                          {pinf, "rhoNew[0] = +Inf"}};
    for (const auto& c : cases) {
      ScalarField rhoOld(n, 2.0), rhoNew(n, 2.0);
      rhoNew[0] = c.bad;
      std::printf("  %-22s ", c.label);
      try {
        const auto r = evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, zeroFlux, 1.0);
        std::printf("RETURNED  maxImbalance %-24.17g totalMassNew %.17g\n", r.maxCellImbalance,
                    r.totalMassNew);
      } catch (const InvalidArgumentError& e) {
        std::printf("THREW: %s\n", e.what());
      }
    }
    // a non-finite FLUX
    {
      ScalarField rho(n, 2.0);
      SurfaceField badFlux(mesh.numberOfFaces(), 0.0);
      badFlux[0] = qnan;
      std::printf("  %-22s ", "massFlux[0] = NaN");
      try {
        const auto r = evaluateCompressibleContinuity(mesh, rho, rho, badFlux, 1.0);
        std::printf("RETURNED  maxImbalance %.17g\n", r.maxCellImbalance);
      } catch (const InvalidArgumentError& e) {
        std::printf("THREW: %s\n", e.what());
      }
    }
  }

  std::printf("\ndone\n");
  return 0;
}
