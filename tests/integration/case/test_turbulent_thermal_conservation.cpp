// P16 -- turbulent thermal energy conservation.
//
// Implements the methodology frozen BEFORE any balance was computed, in
// results/p16/turbulence/eddy_viscosity/conservation.md. Read that file for the
// sign convention and for why the face formulas below must be the assembler's
// own rather than an independent quadrature.
//
// In one line: the discrete energy equation is a per-cell balance, so summing
// it over every cell cancels the internal faces and leaves only boundary ones.
// This file integrates those boundary faces and requires them to sum to zero.
//
// The wall heat flux is evaluated with the SAME
//   k_eff = k_mol + mu_t cp / Pr_t
// the solve used. Verifying conservation with k_mol alone is explicitly
// forbidden by the methodology -- in a turbulent case k_t dominates (measured
// ratio ~209 here), so a balance that ignored it would be measuring the wrong
// equation. Control C1 below runs exactly that mistake on purpose and requires
// it to FAIL.
//
// A CORRECTION MADE AFTER THE FIRST RUN, recorded because it is precisely why
// the methodology insists on the assembler's own formulas: the obvious
// two-point secant  k A / d * (T_b - T_P)  is NOT what this codebase
// assembles. P12-DIFF-002 replaced it with a second-order one-sided
// reconstruction through the boundary value, the owner and the FAR cell,
// because the secant estimates dT/dn at the midpoint of P and the face rather
// than AT the face. Hand-writing the secant under-computed the wall heat flux
// by ~13% and made a provably conservative scheme look non-conservative.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/thermal/TurbulentConductivity.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCaseDirectory = "cases/turbulent_heated_channel";

// From the committed case file. Restated here rather than re-parsed so that a
// change to the case invalidates this test loudly instead of silently shifting
// what it measures.
constexpr Real kMolecularConductivity = 0.0257;
constexpr Real kSpecificHeat = 1005.0;
constexpr Real kInletTemperature = 300.0;
constexpr Real kWallTemperature = 400.0;

struct Balance {
  Real wall{};    // net energy entering through bottom + top
  Real inlet{};   // net entering at the left
  Real outlet{};  // net LEAVING at the right
  Real imbalance{};
  Real normalised{};
};

// Everything the balance needs that ProjectRunResult does not carry, rebuilt
// ONCE from the same case definition with the same functions ProjectRunner
// uses -- identical fields by construction, not approximations to them.
struct BalanceContext {
  cfd::fields::SurfaceField massFlux;
  cfd::fields::VectorField gradT;
};

BalanceContext makeContext(const ProjectRunResult& run, const cfd::io::SimulationSetup& setup) {
  BalanceContext c;
  c.massFlux = cfd::physics::calculateMassFlux(setup.mesh, run.simpleResult->velocity, setup.fluid,
                                               setup.velocityBoundaries);
  // The same gradient the assembler passes in. On this Cartesian mesh the
  // tangential correction is identically zero, but the reconstruction still
  // takes it, so it is supplied rather than assumed away.
  c.gradT = cfd::discretization::gradient(setup.mesh, run.thermalResult->temperature,
                                          *setup.temperatureBoundaries);
  return c;
}

// One boundary face's energy flux INTO the domain, exactly as frozen:
//   q_f = Q_diffusive_in - Q_advective_out
//
// `conductivity` is supplied by the caller so the controls can push a
// deliberately wrong value through this identical routine.
Real faceFluxIntoDomain(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                        const cfd::fields::ScalarField& temperature,
                        const cfd::fields::SurfaceField& massFlux,
                        const cfd::fields::VectorField& gradT, Real conductivity,
                        Real boundaryTemperature, bool prescribed) {
  const Index owner = face.owner();
  const Real massOut = massFlux[face.id()];

  // Advective, upwind -- the convection assembler's own scheme, including its
  // `>= 0` tie-break. massFlux is positive along the OUTWARD normal, so
  // positive means leaving.
  const Real upwindTemperature = massOut >= 0.0 ? temperature[owner] : boundaryTemperature;
  const Real advectiveOut = massOut * kSpecificHeat * upwindTemperature;

  // Diffusive, only where the temperature is prescribed. A zero-gradient
  // (adiabatic) face carries none by definition.
  //
  // Taken from the assembler's own boundaryFaceDiffusionTerms. The assembled
  // row holds -flux_into_owner and is built as
  //     A(P,P)       += coefficient
  //     A(P,farCell) += -farCellCoefficient
  //     b(P)         += boundaryValueCoefficient * T_b + explicitFlux
  // so the net flux INTO P from this face is the RHS minus the LHS.
  Real diffusiveIn = 0.0;
  if (prescribed) {
    const Real distance =
        cfd::mesh::MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
    const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
        mesh, face, conductivity, distance, &gradT, /*prescribedValue=*/true);
    diffusiveIn = (terms.boundaryValueCoefficient * boundaryTemperature) + terms.explicitFlux -
                  (terms.coefficient * temperature[owner]);
    if (terms.farCellCoefficient != 0.0) {
      diffusiveIn += terms.farCellCoefficient * temperature[terms.farCell];
    }
  }
  return diffusiveIn - advectiveOut;
}

// `conductivityOverride < 0` means "use the real k_eff from the run"; a
// non-negative value replaces it everywhere, which is how control C1 forces
// the molecular-only mistake through this same routine.
Balance computeBalance(const ProjectRunResult& run, const cfd::io::SimulationSetup& setup,
                       const BalanceContext& context, Real turbulentPrandtlForFlux,
                       Real conductivityOverride) {
  const auto& mesh = setup.mesh;
  const auto& temperature = run.thermalResult->temperature;
  const auto& muT = run.turbulentThermalResult->turbulentViscosity;

  Balance b;
  // Patch-by-patch, because a mesh exposes faces per patch rather than a patch
  // per face. Every boundary face belongs to exactly one patch.
  for (const auto& patch : mesh.boundaryPatches()) {
    const bool isWall = (patch.name() == "bottom" || patch.name() == "top");
    const bool isInlet = (patch.name() == "left");
    // left and the two walls prescribe T; the outlet is adiabatic.
    const bool prescribed = isWall || isInlet;
    const Real boundaryTemperature = isWall ? kWallTemperature : kInletTemperature;

    for (const Index faceId : patch.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Real kEff = conductivityOverride >= 0.0
                            ? conductivityOverride
                            : kMolecularConductivity +
                                  cfd::thermal::turbulentConductivity(
                                      muT[face.owner()], kSpecificHeat, turbulentPrandtlForFlux);

      // P16-TWF. For a wall face the PRODUCTION result is read, never
      // re-derived: once the wall function replaced the boundary operator, the
      // k_eff cell-centre gradient stopped describing it, and reconstructing
      // that way reported 1.13e5 W against an actual 1.33e4 -- an 8.5x error.
      //
      // The controls below (conductivityOverride >= 0) deliberately keep the
      // reconstruction, because that is the mistake they exist to detect.
      Real q = 0.0;
      if (isWall && conductivityOverride < 0.0 && !run.thermalResult->wallHeatFlux.empty()) {
        // massFlux is zero at a wall, so the advective term vanishes and the
        // published flux IS the whole face contribution.
        q = run.thermalResult->wallHeatFlux[faceId];
      } else {
        q = faceFluxIntoDomain(mesh, face, temperature, context.massFlux, context.gradT, kEff,
                               boundaryTemperature, prescribed);
      }
      if (isWall) {
        b.wall += q;
      } else if (isInlet) {
        b.inlet += q;
      } else {
        b.outlet -= q;  // q is INTO the domain; outlet is what LEAVES
      }
    }
  }
  b.imbalance = b.wall + b.inlet - b.outlet;
  const Real scale = std::max({std::abs(b.wall), std::abs(b.inlet), std::abs(b.outlet)});
  b.normalised = scale > 0.0 ? std::abs(b.imbalance) / scale : std::abs(b.imbalance);
  return b;
}

void report(const char* label, const Balance& b) {
  std::printf("%-28s Q_wall=%12.5e  Q_in=%12.5e  Q_out=%12.5e  imbalance=%12.5e  eps_E=%.3e\n",
              label, b.wall, b.inlet, b.outlet, b.imbalance, b.normalised);
}

}  // namespace

TEST(TurbulentThermalConservation, SteadyEnergyBalanceCloses) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.thermalResult.has_value());
  ASSERT_TRUE(run.thermalResult->converged());
  ASSERT_TRUE(run.turbulentThermalResult.has_value())
      << "turbulent heat transfer was not active, so this test is not measuring what it claims";
  ASSERT_TRUE(run.caseDefinition.has_value());
  ASSERT_TRUE(run.simpleResult.has_value());

  const Real prt = run.turbulentThermalResult->turbulentPrandtl;
  const auto setup = cfd::io::CaseBuilder{}.build(*run.caseDefinition);
  const BalanceContext context = makeContext(run, setup);

  const Balance correct = computeBalance(run, setup, context, prt, -1.0);
  report("k_eff (the solve's own)", correct);

  // The turbulent term must actually dominate here, or the case is not a
  // turbulent-heat-transfer case and C1 below would be weak by accident.
  Real maximumTurbulent = 0.0;
  for (Index i = 0; i < run.turbulentThermalResult->turbulentViscosity.size(); ++i) {
    maximumTurbulent =
        std::max(maximumTurbulent,
                 cfd::thermal::turbulentConductivity(
                     run.turbulentThermalResult->turbulentViscosity[i], kSpecificHeat, prt));
  }
  std::printf("max k_t = %.5e  vs  k_mol = %.5e  (ratio %.1f)\n", maximumTurbulent,
              kMolecularConductivity, maximumTurbulent / kMolecularConductivity);
  EXPECT_GT(maximumTurbulent, kMolecularConductivity)
      << "k_t never exceeds k_mol, so this case does not exercise turbulent heat transfer";

  // --- NON-VACUITY CONTROLS, run through the identical routine ---------------

  // C1: the forbidden mistake -- molecular conductivity only.
  const Balance molecularOnly = computeBalance(run, setup, context, prt, kMolecularConductivity);
  report("C1 k_mol only (must fail)", molecularOnly);

  // C2: the right field, the wrong Pr_t in the flux evaluation.
  //
  // NOTE what this probes NOW. Since the wall faces read the published
  // production flux, a wrong Pr_t here no longer perturbs them -- it only
  // reaches the OTHER prescribed-temperature boundary (the inlet), whose k_eff
  // is still reconstructed. So C2 is a weaker control than it was before the
  // wall function existed, and C1 is the one that exercises the wall term.
  // Recorded rather than quietly left looking stronger than it is.
  const Balance wrongPrandtl = computeBalance(run, setup, context, prt * 4.0, -1.0);
  report("C2 wrong Pr_t at inlet", wrongPrandtl);

  EXPECT_GT(molecularOnly.normalised, 10.0 * correct.normalised)
      << "the balance is no worse with molecular conductivity alone, so it is insensitive to "
         "the turbulent term and proves nothing about it";
  EXPECT_GT(wrongPrandtl.normalised, 10.0 * correct.normalised)
      << "the balance is insensitive to Pr_t even at the inlet face";

  // --- THE GATE --------------------------------------------------------------
  //
  // Threshold derived from the two error sources named in the frozen
  // methodology rather than chosen to fit:
  //
  //   1. the flow solve's mass imbalance enters Q_advective directly. This case
  //      converges to a reported mass imbalance of order 1e-7 against a mass
  //      flow of order 1 kg/s, i.e. a relative contamination of order 1e-7;
  //   2. the thermal solve's outer tolerance is 1e-10 relative.
  //
  // Their sum bounds the achievable residual at order 1e-6. 1e-4 is two orders
  // of margin above that floor and still well below the C1 control, so the gate
  // is loose enough not to be flaky and tight enough to be violated by any real
  // conservation error.
  constexpr Real kConservationTolerance = 1e-4;
  EXPECT_LT(correct.normalised, kConservationTolerance)
      << "steady energy balance does not close: Q_wall + Q_in - Q_out = " << correct.imbalance
      << " (eps_E = " << correct.normalised << ")";
}
