// P16 -- verify the INFLOW before looking at any separation result, then the
// controlled A/B that can refute the root-cause hypothesis.
//
// The order is the point. The previous Driver & Seegmiller attempt failed
// (E_x_r = 0.36 on the medium mesh) and the failure was attributed to an
// undeveloped inlet. That attribution is a hypothesis, and a hypothesis that
// cannot be refuted is not evidence. So:
//
//   1. the precursor's inflow state is checked against the reference
//      description FIRST -- delta/H, C_f, mass flow, the shape of k and omega.
//      If the inflow is wrong, the separation result cannot mean anything and
//      there is nothing to attribute;
//   2. only then, a medium-grid A/B with everything else held fixed.
//
// The refutation condition was fixed in
// separated_flow/reference_specification.md section 9 BEFORE this file existed:
// if the corrected inlet does NOT move x_r/H materially toward 6.26 while the
// old setup stays near 8.5, the inlet hypothesis is WRONG and the
// investigation moves elsewhere. That is written down so it cannot be quietly
// dropped if the answer is inconvenient.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BackwardFacingStepCase.hpp"
#include "DriverSeegmillerCase.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/SSTModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;

namespace ds = cfd::validation::driver_seegmiller;

namespace {

// Computed once: the precursor is the expensive part and every test below
// needs the same one.
const ds::PrecursorProfile& precursor() {
  static const ds::PrecursorProfile profile = ds::runPrecursor(ds::PrecursorSpec{});
  return profile;
}

SSTConfig baseConfig(Real inletK, Real inletOmega) {
  SSTConfig config;
  config.initialK = inletK;
  config.initialOmega = inletOmega;
  config.wallFunctions = true;
  config.wallFunctionCoefficients.cMu = config.coefficients.betaStar;
  return config;
}

SIMPLESettings makeSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 12000;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-5;
  settings.pressureTolerance = 5e-4;
  settings.continuityTolerance = 1e-6;
  settings.turbulenceTolerance = 1e-5;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-9;
  settings.pressureSolver.relativeTolerance = 1e-7;
  return settings;
}

constexpr ds::MeshSpec kMedium{"medium", 3};

struct StepResult {
  bool converged{};
  Index iterations{};
  std::optional<Real> reattachment;
  Real reattachmentError{};
  Real inletMassFlow{};
  Real maximumTurbulentViscosity{};
};

// One step solve. `profile` null means the OLD analytic inlet; non-null means
// the mapped precursor. Everything else is identical between the two, which is
// what makes this a controlled comparison.
StepResult runStep(const ds::MeshSpec& spec, const ds::PrecursorProfile* profile) {
  const auto mesh = ds::makeMesh(spec);
  const auto boundaries = ds::makeBoundariesWithProfile(mesh, profile);
  const FluidProperties fluid(ds::kDensity, ds::kDynamicViscosity);

  // Free-stream turbulence for the old inlet; the mapped profile for the new.
  const Real freeK = 1.5 * (0.02 * ds::kReferenceVelocity) * (0.02 * ds::kReferenceVelocity);
  const Real freeOmega =
      std::sqrt(freeK) / (std::pow(0.09, 0.25) * 0.07 * ds::kInletBoundaryLayerThickness);

  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet omegaBoundaries;
  const auto scalars =
      profile != nullptr ? ds::mapScalarInlets(mesh, *profile) : ds::ScalarInletProfiles{};
  const auto lookup = [](const std::vector<std::pair<std::string, Real>>& table,
                         const std::string& name, Real fallback) {
    for (const auto& [patch, value] : table) {
      if (patch == name) return value;
    }
    return fallback;
  };

  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& bc = boundaries.velocity.get(patch.name());
    const bool isInlet = bc.type() == cfd::boundary::BoundaryConditionType::Inlet;
    if (isInlet) {
      kBoundaries.set(
          mesh, patch.name(),
          std::make_unique<cfd::boundary::FixedValue>(lookup(scalars.k, patch.name(), freeK)));
      omegaBoundaries.set(mesh, patch.name(),
                          std::make_unique<cfd::boundary::FixedValue>(
                              lookup(scalars.omega, patch.name(), freeOmega)));
    } else {
      kBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
      omegaBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }

  SSTModel model(mesh, fluid, boundaries.velocity, kBoundaries, omegaBoundaries,
                 baseConfig(freeK, freeOmega));
  const SIMPLE simple(makeSettings(), 0, &model);
  const SIMPLEResult result =
      simple.solve(mesh, fluid, boundaries.velocity, boundaries.pressure,
                   VectorField(mesh.numberOfCells(), cfd::Vector2{0.0, 0.0}),
                   ScalarField(mesh.numberOfCells(), 0.0));

  StepResult outcome;
  outcome.converged = result.converged();
  outcome.iterations = result.iterations;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    outcome.maximumTurbulentViscosity =
        std::max(outcome.maximumTurbulentViscosity, model.turbulentViscosity()[i]);
  }

  // Inlet mass flow, so the mapped profile can be shown to carry the same flow
  // rate the precursor produced rather than assumed to.
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name().rfind("inlet_", 0) != 0) continue;
    const auto& face = mesh.face(patch.faceIds().front());
    const auto& bc = boundaries.velocity.get(patch.name());
    const auto* inlet = dynamic_cast<const cfd::boundary::Inlet*>(&bc);
    if (inlet != nullptr) {
      outcome.inletMassFlow += ds::kDensity * inlet->velocity().x * face.area();
    }
  }

  const auto wall = ds::bottomWallDistribution(mesh, spec, result.velocity, result.pressure,
                                               ds::kDynamicViscosity, &model.wallShearStress());
  cfd::validation::step::WallShear shear;
  shear.x = wall.x;
  shear.bottom = wall.cf;
  shear.top.assign(wall.x.size(), 1.0);
  outcome.reattachment = cfd::validation::step::separationTopology(shear).reattachment;
  if (outcome.reattachment) {
    outcome.reattachmentError =
        std::abs(*outcome.reattachment - ds::kReferenceReattachment) / ds::kReferenceReattachment;
  }
  return outcome;
}

}  // namespace

// --- STEP 1: the inflow, checked before any separation result ----------------

TEST(DriverSeegmillerInlet, PrecursorReproducesTheReferenceInflowState) {
  const auto& profile = precursor();

  std::printf(
      "\nprecursor: converged=%d iters=%zu  station x/H=%.2f  delta/H=%.4f  "
      "U_edge=%.4f  Cf=%.6f  mass=%.6f  max mu_t/mu=%.1f\n",
      profile.converged ? 1 : 0, profile.iterations, profile.extractionStationOverH,
      profile.deltaOverH, profile.edgeVelocity, profile.skinFriction, profile.massFlowPerDepth,
      profile.maximumTurbulentViscosityRatio);

  EXPECT_TRUE(profile.converged) << "the precursor itself did not converge";

  // The quantity the whole approach turns on. The reference is delta/H ~ 1.5,
  // and the scan stops at the first station reaching it, so this should be a
  // little above 1.5 rather than far from it.
  EXPECT_GE(profile.deltaOverH, ds::kTargetDeltaOverH);
  EXPECT_LT(profile.deltaOverH, 1.1 * ds::kTargetDeltaOverH)
      << "the extraction overshot the target thickness by more than 10% -- the scan is too "
         "coarse in x to place the station precisely";

  // A turbulent boundary layer, not a laminar one that happened to thicken.
  EXPECT_GT(profile.skinFriction, 0.0) << "attached flow must have positive C_f";
  EXPECT_GT(profile.maximumTurbulentViscosityRatio, 10.0)
      << "mu_t/mu is only " << profile.maximumTurbulentViscosityRatio
      << " -- the incoming layer is barely turbulent, which is the very defect this "
         "precursor exists to remove";

  // k must PEAK inside the BOTTOM layer -- the one that becomes the shear
  // layer -- and not sit flat at the free-stream value. This is the sharpest
  // check that the precursor did its job, because the old inlet was flat by
  // construction.
  //
  // The free-stream reference is the duct CENTRELINE, not profile.k.back().
  // The duct has walls on both sides, so the last entry is the TOP wall's
  // near-wall k, which is comparable to the bottom's -- comparing against it
  // asks whether one boundary layer is more turbulent than the other, which is
  // not the question. (The first version of this test did exactly that and
  // failed for that reason, not because the precursor was wrong.)
  const std::size_t centre = profile.k.size() / 2;
  const Real freeStreamK = profile.k[centre];
  const auto bottomHalfEnd = profile.k.begin() + static_cast<std::ptrdiff_t>(centre);
  const auto peak = std::max_element(profile.k.begin(), bottomHalfEnd);
  const auto peakIndex = static_cast<std::size_t>(std::distance(profile.k.begin(), peak));

  std::printf("k: bottom-layer peak %.6e at y/H %.3f, centreline %.6e, ratio %.1f\n", *peak,
              profile.y[peakIndex] / ds::kStepHeight, freeStreamK, *peak / freeStreamK);

  EXPECT_GT(*peak, 5.0 * freeStreamK)
      << "k does not peak inside the boundary layer (peak " << *peak << " vs centreline "
      << freeStreamK << ") -- the mapped inlet is no more turbulent than the old flat one";
  EXPECT_LT(profile.y[peakIndex], profile.deltaOverH * ds::kStepHeight)
      << "the k peak lies outside the boundary layer";

  // omega must be largest at the wall and fall toward the core -- the near-wall
  // scale. Compared against the CENTRELINE for the same reason as k.
  EXPECT_GT(profile.omega.front(), profile.omega[centre])
      << "omega does not decrease from the wall toward the core";
}

TEST(DriverSeegmillerInlet, MappedInletPreservesTheMassFlow) {
  // The mapping interpolates at face centroids, so the transferred mass flow
  // has to be CHECKED rather than assumed -- a systematic mismatch would change
  // the effective Reynolds number between the A and B cases and invalidate the
  // comparison.
  const auto& profile = precursor();
  const StepResult mapped = runStep(kMedium, &profile);

  const Real relative =
      std::abs(mapped.inletMassFlow - profile.massFlowPerDepth) / profile.massFlowPerDepth;
  std::printf("inlet mass flow: precursor %.6f, mapped %.6f, relative %.4f\n",
              profile.massFlowPerDepth, mapped.inletMassFlow, relative);
  EXPECT_LT(relative, 0.02) << "the mapped inlet carries a different flow rate than the "
                               "precursor produced -- the two cases are not the same problem";
}

// --- STEP 2: the controlled A/B ----------------------------------------------

TEST(DriverSeegmillerInlet, MediumGridAB) {
  // Everything held fixed except the inlet. The refutation condition was fixed
  // in the reference specification before this ran.
  const auto& profile = precursor();
  const StepResult oldInlet = runStep(kMedium, nullptr);
  const StepResult newInlet = runStep(kMedium, &profile);

  const auto show = [](const char* label, const StepResult& r) {
    std::printf("%-22s converged=%d iters=%5zu  x_r/H=%-10s E=%.4f  max mu_t=%.4e\n", label,
                r.converged ? 1 : 0, r.iterations,
                r.reattachment ? std::to_string(*r.reattachment).substr(0, 8).c_str() : "none",
                r.reattachmentError, r.maximumTurbulentViscosity);
  };
  std::printf("\nmedium-grid A/B (reference x_r/H = %.2f)\n", ds::kReferenceReattachment);
  show("old analytic inlet", oldInlet);
  show("mapped precursor", newInlet);

  ASSERT_TRUE(oldInlet.reattachment.has_value());
  ASSERT_TRUE(newInlet.reattachment.has_value());

  // The old setup must still reproduce its recorded failure -- if it does not,
  // something other than the inlet changed and the comparison is void.
  EXPECT_NEAR(*oldInlet.reattachment, 8.502, 0.5)
      << "the old inlet no longer reproduces its recorded x_r/H = 8.502, so this A/B is not "
         "isolating the inlet";

  const Real improvement = oldInlet.reattachmentError - newInlet.reattachmentError;
  std::printf("E_x_r: old %.4f -> new %.4f (improvement %.4f)\n", oldInlet.reattachmentError,
              newInlet.reattachmentError, improvement);

  // Reported, not asserted as a pass. Whether the hypothesis survives is the
  // finding; forcing it to be true here would defeat the purpose of running it.
  if (improvement <= 0.0) {
    std::printf(
        "\n*** the inlet hypothesis is NOT supported: the corrected inflow did not "
        "move x_r/H toward the reference. Per the frozen refutation condition the "
        "investigation moves to wall-function applicability, SST near-wall coupling, "
        "the production limiter, convection discretisation, outlet influence, "
        "step-corner wall distance, and steady-vs-unsteady behaviour. ***\n");
  }
}
