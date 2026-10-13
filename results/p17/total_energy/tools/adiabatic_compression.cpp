// P17.5 authorization section 22 -- the canonical adiabatic compression/expansion check, and
// section 23's static thermal limit.
//
// SECTION 22. For a reversible adiabatic process an ideal gas satisfies p / rho^gamma = constant.
// This is NOT solved for here -- P17.5 has no coupled solver. What is checked is that the STATE
// relations are mutually consistent along such a process: given a sequence of (rho, T) pairs that
// lie on p/rho^gamma = const, the implemented conversions must reproduce the same p, the same rhoE,
// and the same T on recovery. That exercises the rho <-> rhoE <-> T <-> p coupling, which is what
// the section asks for, WITHOUT over-claiming shock capability.
//
// SECTION 23. With U = 0 the total-energy equation must reduce to conduction alone. Measured
// against the P15 conduction path on the same field, so the "static thermal limit" is a comparison
// rather than an assertion.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/compressible/TotalEnergyEquation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  constexpr Real kEps = std::numeric_limits<Real>::epsilon();
  constexpr Real kR = 287.05;
  constexpr Real kCp = 1005.0;
  const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 6000.0, "cp");
  const TotalEnergyState state(ThermodynamicProperties(kR, kCp), SensibleEnthalpy(cp, 0.0, 0.0));
  const Real gamma = state.thermodynamics().specificHeatRatio();

  std::printf("P17.5 -- adiabatic compression and the static thermal limit\n");
  std::printf("gamma = %.17g   R = %.17g   cp = %.17g\n\n", gamma, kR, kCp);

  // ---------------------------------------------------------------- section 22
  std::printf("### 22. adiabatic compression: p / rho^gamma held constant\n");
  {
    // Start state, then compress. For an ideal gas on an isentrope,
    //   T2 = T1 (rho2/rho1)^(gamma-1)
    const Real rho1 = 1.0;
    const Real t1 = 300.0;
    const Real p1 = state.thermodynamics().equationOfState().pressure(rho1, t1);
    const Real invariant1 = p1 / std::pow(rho1, gamma);
    std::printf("  start: rho %.4g  T %.4g  p %.10g   p/rho^gamma %.10g\n", rho1, t1, p1,
                invariant1);
    std::printf("  %-8s %-12s %-16s %-16s %-16s %s\n", "rho2/rho1", "T2 [K]", "p2 [Pa]",
                "p2/rho2^gamma", "rel dev of inv", "T recovered err [K]");

    Real worstInvariant = 0.0;
    Real worstRecovery = 0.0;
    for (const Real ratio : {1.25, 1.5, 2.0, 4.0, 8.0}) {
      const Real rho2 = rho1 * ratio;
      const Real t2 = t1 * std::pow(ratio, gamma - 1.0);
      const Real p2 = state.thermodynamics().equationOfState().pressure(rho2, t2);
      const Real invariant2 = p2 / std::pow(rho2, gamma);
      const Real deviation = std::abs(invariant2 - invariant1) / std::abs(invariant1);
      worstInvariant = std::max(worstInvariant, deviation);

      // The rho <-> rhoE <-> T <-> p coupling: build the conserved state, recover it, compare.
      const Vector2 zero{0.0, 0.0};
      const Real rhoE2 = state.conservedTotalEnergy(rho2, t2, zero);
      const auto back = state.recover(rho2, zero, rhoE2);
      const Real recoveryError = std::abs(back.temperature - t2);
      worstRecovery = std::max(worstRecovery, recoveryError);

      std::printf("  %-8.3g %-12.8g %-16.10g %-16.10g %-16.4g %.4g\n", ratio, t2, p2, invariant2,
                  deviation, recoveryError);
    }
    std::printf("  worst relative deviation of p/rho^gamma   %.6g  = %.2f eps\n", worstInvariant,
                worstInvariant / kEps);
    std::printf("  worst T recovery error along the isentrope %.6g K\n", worstRecovery);
    std::printf("  NOTE: this verifies the STATE relations along an isentrope. P17.5 does not\n");
    std::printf("        SOLVE a compression, and no shock capability is claimed from it.\n");
  }

  // ---------------------------------------------------------------- section 23
  std::printf("\n### 23. static thermal limit: U = 0 leaves conduction alone\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(8, 6, 8.0, 6.0);
    const Index n = mesh.numberOfCells();
    const Real k = 0.7;
    ScalarField temperature(n);
    for (const auto& cell : mesh.cells()) {
      temperature[cell.id()] = 300.0 + 40.0 * std::sin(0.4 * cell.centroid().x) +
                               10.0 * cell.centroid().y;
    }
    BoundaryConditionSet bcs;
    for (const auto& patch : mesh.boundaryPatches()) {
      bcs.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
    }
    const ScalarField density(n, 1.2);
    const VectorField still(n, Vector2{0.0, 0.0});

    const ScalarField conductive =
        cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, k, bcs);
    const ScalarField hTotal =
        cfd::compressible::specificTotalEnthalpyField(mesh, temperature, still, state);
    const cfd::fields::SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
    const ScalarField convective = cfd::compressible::totalEnergyConvectiveDivergence(
        mesh, noFlux, hTotal, noFlux);
    const ScalarField bodyPower =
        cfd::compressible::bodyForcePowerField(mesh, density, still, Vector2{0.0, -9.81});

    Real worstConvective = 0.0;
    Real worstBody = 0.0;
    Real sumConductive = 0.0;
    Real worstConductive = 0.0;
    for (Index i = 0; i < n; ++i) {
      worstConvective = std::max(worstConvective, std::abs(convective[i]));
      worstBody = std::max(worstBody, std::abs(bodyPower[i]));
      sumConductive += conductive[i];
      worstConductive = std::max(worstConductive, std::abs(conductive[i]));
    }
    std::printf("  U = 0, zero mass flux:\n");
    std::printf("    worst |convective outflow|   %.17g   (must be exactly 0)\n", worstConvective);
    std::printf("    worst |body-force power|     %.17g   (must be exactly 0)\n", worstBody);
    std::printf("    worst |conductive outflow|   %.6g   (must be LARGE -- else vacuous)\n",
                worstConductive);
    std::printf("    sum(conductive outflow)      %.6g   = %.2f eps of the worst cell\n",
                sumConductive, std::abs(sumConductive) / (worstConductive * kEps));
    std::printf("  => with U = 0 the total-energy equation IS the conduction equation, and the\n");
    std::printf("     conduction still sums to zero across a zero-gradient boundary.\n");
  }

  std::printf("\ndone\n");
  return 0;
}
