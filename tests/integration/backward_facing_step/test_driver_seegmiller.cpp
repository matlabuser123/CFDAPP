// P16 -- SST against a NAMED separated-flow reference.
//
// The Re_h = 5000 internal step (test_sst_separation.cpp) discriminates SST
// from k-epsilon and detects every SST mechanism failure -- but it was never
// calibrated to a dataset, so it cannot say whether SST's separation
// prediction is CORRECT, only that it is SST-specific. This file answers the
// other half: Driver & Seegmiller (1985), the case the NASA Turbulence
// Modeling Resource 2D Backward Facing Step is built on, whose reattachment
// length is a published number.
//
// The methodology -- which quantities are gated, which are only recorded, how
// reattachment is detected, and every deviation from the reference -- was
// frozen in
//   results/p16/turbulence/acceptance/separated_flow_methodology.md
// BEFORE this file or DriverSeegmillerCase existed. In particular:
//
//   * x_r/H = 6.26 is the ONLY gated quantity, because it is the only
//     reference value available in this environment;
//   * C_f(x), C_p(x) and the outlet profile are EXPORTED and checked for
//     structure a separated flow must have -- never compared against
//     remembered numbers;
//   * the detector is the pre-existing separationTopology, unchanged, chosen
//     precisely because it predates the question.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BackwardFacingStepCase.hpp"
#include "DriverSeegmillerCase.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

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

constexpr Real kInletTurbulenceIntensity = 0.02;  // a wind-tunnel free stream
constexpr Real kBetaStar = 0.09;

Real inletK() {
  const Real fluct = kInletTurbulenceIntensity * ds::kReferenceVelocity;
  return 1.5 * fluct * fluct;
}
Real inletOmega() {
  // omega = sqrt(k)/(beta*^(1/4) L), with L the inlet boundary-layer scale.
  return std::sqrt(inletK()) /
         (std::pow(kBetaStar, 0.25) * 0.07 * ds::kInletBoundaryLayerThickness);
}

BoundaryConditionSet makeScalarBoundaries(const cfd::mesh::Mesh& mesh,
                                          const BoundaryConditionSet& velocity, Real inletValue) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& bc = velocity.get(patch.name());
    const bool isWall = bc.type() == cfd::boundary::BoundaryConditionType::Wall ||
                        bc.type() == cfd::boundary::BoundaryConditionType::MovingWall;
    const bool isInlet = bc.type() == cfd::boundary::BoundaryConditionType::Inlet;
    if (isWall) {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (isInlet) {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(inletValue));
    } else {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }
  return boundaries;
}

SIMPLESettings makeSettings() {
  SIMPLESettings settings;
  // 12000, not 3000. The diagnostic below established that this case needs
  // ~8000 outer iterations on the medium mesh; at 3000 every mesh hit the cap
  // and none of the results were converged solutions. Raising the budget makes
  // the MEASUREMENT valid -- it is not tuning, and it did not materially change
  // the answer (x_r/H 8.594 unconverged -> 8.502 converged).
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

SSTConfig baseConfig() {
  SSTConfig config;
  config.initialK = inletK();
  config.initialOmega = inletOmega();
  config.wallFunctions = true;
  config.wallFunctionCoefficients.cMu = config.coefficients.betaStar;
  return config;
}

struct Outcome {
  std::string label;
  Index cells{};
  bool converged{};
  Index iterations{};
  Real uResidual{};
  Real pResidual{};
  Real continuityResidual{};
  Real meanWallYPlus{};
  Real minimumWallYPlus{};
  Real maximumWallYPlus{};
  Real minimumCf{};       // most negative C_f -- the bubble
  Real outletCf{};        // C_f at the outlet -- must be positive
  Index cfSignChanges{};  // a clean bubble has one
  Real cpRecovery{};      // C_p(outlet) - C_p(min C_f station)
  Index reversedOutletCells{};

  Index facesOutsideValidBand{};
  Index facesWithFlooredViscosity{};
  Real maximumTurbulentViscosity{};
  std::optional<Real> reattachment;
  Real reattachmentError{};
  ds::WallDistribution wall;
  std::vector<std::pair<Real, Real>> outlet;
};

// `profile` null keeps the ORIGINAL analytic inlet, so the preserved failed
// sequence stays reproducible from this same function. Non-null threads the
// verified precursor. That is the ONLY difference between the two sequences.
Outcome run(const ds::MeshSpec& spec, const SSTConfig& config,
            const ds::PrecursorProfile* profile = nullptr) {
  const auto mesh = ds::makeMesh(spec);
  const auto boundaries = ds::makeBoundariesWithProfile(mesh, profile);
  const FluidProperties fluid(ds::kDensity, ds::kDynamicViscosity);

  auto kBoundaries = makeScalarBoundaries(mesh, boundaries.velocity, inletK());
  auto omegaBoundaries = makeScalarBoundaries(mesh, boundaries.velocity, inletOmega());
  if (profile != nullptr) {
    // The mapped k and omega profiles. Without these the velocity would be
    // developed and the turbulence still flat, which is the half-corrected
    // case nobody wants to measure.
    const auto scalars = ds::mapScalarInlets(mesh, *profile);
    for (const auto& [patch, value] : scalars.k) {
      kBoundaries.replace(mesh, patch, std::make_unique<cfd::boundary::FixedValue>(value));
    }
    for (const auto& [patch, value] : scalars.omega) {
      omegaBoundaries.replace(mesh, patch, std::make_unique<cfd::boundary::FixedValue>(value));
    }
  }
  SSTModel model(mesh, fluid, boundaries.velocity, kBoundaries, omegaBoundaries, config);

  const SIMPLE simple(makeSettings(), /*referenceCell=*/0, &model);
  const VectorField initialVelocity(mesh.numberOfCells(), cfd::Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult result = simple.solve(mesh, fluid, boundaries.velocity, boundaries.pressure,
                                           initialVelocity, initialPressure);

  Outcome outcome;
  outcome.label = spec.label;
  outcome.cells = mesh.numberOfCells();
  outcome.converged = result.converged();
  outcome.iterations = result.iterations;
  outcome.uResidual = result.finalUResidual;
  outcome.pResidual = result.finalPressureResidual;
  outcome.continuityResidual = result.finalContinuityResidual;
  outcome.facesOutsideValidBand = model.wallFacesOutsideValidBand();
  outcome.facesWithFlooredViscosity = model.wallFacesWithFlooredViscosity();

  Real yPlusSum = 0.0;
  Index yPlusCount = 0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    outcome.maximumTurbulentViscosity =
        std::max(outcome.maximumTurbulentViscosity, model.turbulentViscosity()[i]);
    if (model.yPlus()[i] > 0.0) {
      yPlusSum += model.yPlus()[i];
      ++yPlusCount;
    }
  }
  outcome.meanWallYPlus = yPlusCount > 0 ? yPlusSum / static_cast<Real>(yPlusCount) : 0.0;
  outcome.minimumWallYPlus = std::numeric_limits<Real>::infinity();
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (model.yPlus()[i] > 0.0) {
      outcome.minimumWallYPlus = std::min(outcome.minimumWallYPlus, model.yPlus()[i]);
      outcome.maximumWallYPlus = std::max(outcome.maximumWallYPlus, model.yPlus()[i]);
    }
  }
  if (!std::isfinite(outcome.minimumWallYPlus)) outcome.minimumWallYPlus = 0.0;

  outcome.wall = ds::bottomWallDistribution(mesh, spec, result.velocity, result.pressure,
                                            ds::kDynamicViscosity, &model.wallShearStress());
  outcome.outlet = ds::outletProfile(mesh, spec, result.velocity);

  // C_f and C_p structure, per the frozen methodology section 5. Recorded as
  // numbers rather than only asserted, so the sequence table carries them.
  const auto& cf = outcome.wall.cf;
  const auto& cp = outcome.wall.cp;
  outcome.minimumCf = *std::min_element(cf.begin(), cf.end());
  outcome.outletCf = cf.back();
  for (std::size_t i = 1; i < cf.size(); ++i) {
    if ((cf[i - 1] < 0.0) != (cf[i] < 0.0)) ++outcome.cfSignChanges;
  }
  const auto minimumIt = std::min_element(cf.begin(), cf.end());
  const auto minimumIndex = static_cast<std::size_t>(std::distance(cf.begin(), minimumIt));
  outcome.cpRecovery = cp.back() - cp[minimumIndex];
  for (const auto& [y, u] : outcome.outlet) {
    (void)y;
    if (u < -1e-3) ++outcome.reversedOutletCells;
  }

  // The FROZEN detector: the pre-existing separationTopology, fed the bottom
  // wall's signed shear. Reused unchanged and not re-tuned -- see the frozen
  // methodology section 4.
  cfd::validation::step::WallShear shear;
  shear.x = outcome.wall.x;
  shear.bottom = outcome.wall.cf;                // sign is what matters; scaling is monotone
  shear.top.assign(outcome.wall.x.size(), 1.0);  // unused here
  outcome.reattachment = cfd::validation::step::separationTopology(shear).reattachment;
  if (outcome.reattachment) {
    outcome.reattachmentError =
        std::abs(*outcome.reattachment - ds::kReferenceReattachment) / ds::kReferenceReattachment;
  }
  return outcome;
}

std::filesystem::path evidenceRoot(const char* variant) {
  // The failed analytic-inlet sequence is preserved under its own variant --
  // the corrected run must not overwrite the evidence it is measured against.
  return std::filesystem::path("results/p16/turbulence/separated_flow/driver_seegmiller") / variant;
}

void writeEvidence(const Outcome& outcome, const char* variant) {
  const auto directory = evidenceRoot(variant) / outcome.label;
  std::filesystem::create_directories(directory);
  {
    std::ofstream out(directory / "wall.csv");
    out.precision(10);
    out << "x_over_H,Cf,Cp\n";
    for (std::size_t i = 0; i < outcome.wall.x.size(); ++i) {
      out << outcome.wall.x[i] << ',' << outcome.wall.cf[i] << ',' << outcome.wall.cp[i] << '\n';
    }
  }
  {
    std::ofstream out(directory / "outlet_profile.csv");
    out.precision(10);
    out << "y_over_H,u_over_Uref\n";
    for (const auto& [y, u] : outcome.outlet) out << y << ',' << u << '\n';
  }
}

void writeSummary(const std::vector<Outcome>& outcomes, const char* variant) {
  std::filesystem::create_directories(evidenceRoot(variant));
  std::ofstream out(evidenceRoot(variant) / "sweep.csv");
  out.precision(10);
  out << "mesh,cells,x_r_over_H,E_x_r,mean_y_plus,min_y_plus,max_y_plus,faces_outside_band,"
         "floored_faces,min_Cf,outlet_Cf,Cf_sign_changes,Cp_recovery,reversed_outlet_cells,"
         "max_mu_t,iterations,u_residual,p_residual,continuity,converged\n";
  for (const auto& o : outcomes) {
    out << o.label << ',' << o.cells << ','
        << (o.reattachment ? std::to_string(*o.reattachment) : "none") << ',' << o.reattachmentError
        << ',' << o.meanWallYPlus << ',' << o.minimumWallYPlus << ',' << o.maximumWallYPlus << ','
        << o.facesOutsideValidBand << ',' << o.facesWithFlooredViscosity << ',' << o.minimumCf
        << ',' << o.outletCf << ',' << o.cfSignChanges << ',' << o.cpRecovery << ','
        << o.reversedOutletCells << ',' << o.maximumTurbulentViscosity << ',' << o.iterations << ','
        << o.uResidual << ',' << o.pResidual << ',' << o.continuityResidual << ','
        << (o.converged ? 1 : 0) << '\n';
  }
}

void report(const Outcome& o) {
  std::printf(
      "%-8s cells=%6zu  x_r/H=%-9s E=%7.4f  y+=%7.2f  out=%4zu floor=%3zu  "
      "max mu_t=%.4e  iters=%4zu conv=%d\n",
      o.label.c_str(), o.cells,
      o.reattachment ? std::to_string(*o.reattachment).substr(0, 8).c_str() : "none",
      o.reattachmentError, o.meanWallYPlus, o.facesOutsideValidBand, o.facesWithFlooredViscosity,
      o.maximumTurbulentViscosity, o.iterations, o.converged ? 1 : 0);
}

// Coarse / medium / fine. 9H tall x 30H long, so the cell count is
// 270 * n^2 -- which is why this stops at three.
constexpr ds::MeshSpec kCoarse{"coarse", 2};  //  18 x  60 =  1080
constexpr ds::MeshSpec kMedium{"medium", 3};  //  27 x  90 =  2430
constexpr ds::MeshSpec kFine{"fine", 4};      //  36 x 120 =  4320

}  // namespace

TEST(DriverSeegmiller, MeshSequence) {
  std::printf("\nDriver & Seegmiller (1985), Re_H = %.0f, ER = %.3f, reference x_r/H = %.2f\n",
              ds::kReynoldsH, ds::kExpansionRatio, ds::kReferenceReattachment);

  std::vector<Outcome> outcomes;
  for (const auto& spec : {kCoarse, kMedium, kFine}) {
    Outcome outcome = run(spec, baseConfig());
    report(outcome);
    writeEvidence(outcome, "analytic_inlet");
    outcomes.push_back(std::move(outcome));
  }
  writeSummary(outcomes, "analytic_inlet");

  for (const auto& outcome : outcomes) {
    // Stability, not accuracy -- accuracy is judged by the gate below.
    EXPECT_TRUE(std::isfinite(outcome.maximumTurbulentViscosity)) << outcome.label;
    EXPECT_GT(outcome.maximumTurbulentViscosity, 10.0 * ds::kDynamicViscosity)
        << outcome.label << ": the turbulence model is barely active";
    ASSERT_TRUE(outcome.reattachment.has_value())
        << outcome.label << ": no reattachment point was found at all";
  }

  // Mesh stability, per the frozen methodology section 6.
  const Real shift = std::abs(*outcomes[2].reattachment - *outcomes[1].reattachment);
  std::printf("medium -> fine shift in x_r/H: %.4f\n", shift);
  EXPECT_LT(shift, 1.0) << "x_r/H moved by " << shift
                        << " step heights between the two finest meshes -- the result is not "
                           "mesh-stable and no threshold may be derived from it";
}

TEST(DriverSeegmiller, WallDistributionsHaveSeparatedFlowStructure) {
  // The structural checks from the frozen methodology section 5. These do NOT
  // compare against reference tables -- that data is not available here and
  // inventing it would manufacture agreement. They check properties a
  // separated flow must have, each of which a wrong answer could fail.
  const Outcome outcome = run(kMedium, baseConfig());
  ASSERT_TRUE(outcome.reattachment.has_value());

  const auto& cf = outcome.wall.cf;
  const auto& cp = outcome.wall.cp;
  ASSERT_FALSE(cf.empty());

  const Real minimumCf = *std::min_element(cf.begin(), cf.end());
  EXPECT_LT(minimumCf, 0.0) << "C_f is nowhere negative -- the flow never separated";

  // Exactly one downstream sign change: many crossings means the detector is
  // reading noise rather than a reattachment point.
  Index crossings = 0;
  for (std::size_t i = 1; i < cf.size(); ++i) {
    if ((cf[i - 1] < 0.0) != (cf[i] < 0.0)) ++crossings;
  }
  EXPECT_LE(crossings, 2u) << crossings << " sign changes in C_f -- too noisy to read";

  // Reattached flow stays attached to the outlet.
  EXPECT_GT(cf.back(), 0.0) << "C_f is negative at the outlet -- the flow re-separated";

  // Pressure recovers through the bubble: C_p at the outlet exceeds C_p at the
  // minimum-C_f station. This is the defining feature of a reattaching flow.
  const auto minimumIt = std::min_element(cf.begin(), cf.end());
  const auto minimumIndex = static_cast<std::size_t>(std::distance(cf.begin(), minimumIt));
  EXPECT_GT(cp.back(), cp[minimumIndex])
      << "pressure did not recover between the bubble and the outlet";

  // The outlet profile has recovered: no reversed flow left.
  for (const auto& [y, u] : outcome.outlet) {
    EXPECT_GT(u, -1e-3) << "reversed flow at the outlet, y/H = " << y;
  }
}

TEST(DriverSeegmiller, MechanismControlsStillDiscriminate) {
  // The frozen methodology section 5 requires the SST controls to be repeated
  // on this case, not only on the internal one. A non-convergent control is
  // valid evidence provided the unbroken case converges, which MeshSequence
  // establishes.
  const Outcome correct = run(kCoarse, baseConfig());
  ASSERT_TRUE(correct.reattachment.has_value());

  SSTConfig noLimiter = baseConfig();
  noLimiter.coefficients.a1 = 1.0e6;
  const Outcome limiterOff = run(kCoarse, noLimiter);

  SSTConfig noCrossDiffusion = baseConfig();
  noCrossDiffusion.coefficients.sigmaOmega2 = 1e-12;
  const Outcome crossOff = run(kCoarse, noCrossDiffusion);

  SSTConfig flatBlending = baseConfig();
  flatBlending.coefficients.sigmaK2 = flatBlending.coefficients.sigmaK1;
  flatBlending.coefficients.sigmaOmega2 = flatBlending.coefficients.sigmaOmega1;
  flatBlending.coefficients.beta2 = flatBlending.coefficients.beta1;
  const Outcome blendingOff = run(kCoarse, flatBlending);

  std::printf("\ncontrols on %s:\n", kCoarse.label);
  report(correct);
  report(limiterOff);
  report(crossOff);
  report(blendingOff);

  // Each control must produce a DIFFERENT outcome. "Different" includes
  // failing to converge or failing to reattach at all -- a broken model is not
  // required to produce a tidy answer.
  const auto differs = [&](const Outcome& control, const char* what) {
    const bool reattachmentDiffers =
        !control.reattachment.has_value() || *control.reattachment != *correct.reattachment;
    const bool convergenceDiffers = control.converged != correct.converged;
    EXPECT_TRUE(reattachmentDiffers || convergenceDiffers)
        << what << " changed nothing on this case -- it cannot detect that failure";
  };
  differs(limiterOff, "disabling the eddy-viscosity limiter");
  differs(crossOff, "removing cross diffusion");
  differs(blendingOff, "flattening the blending");
}

TEST(DriverSeegmiller, DISABLED_ConvergenceBudgetDiagnostic) {
  // Run with a much larger iteration budget to separate two explanations of
  // the mesh sequence's non-convergence: an insufficient budget, or a setup
  // that does not settle. Disabled by default because it is slow; run
  // explicitly with --gtest_also_run_disabled_tests.
  SIMPLESettings generous = makeSettings();
  generous.maxIterations = 12000;
  const auto mesh = ds::makeMesh(kMedium);
  const auto boundaries = ds::makeBoundaries(mesh);
  const FluidProperties fluid(ds::kDensity, ds::kDynamicViscosity);
  auto kB = makeScalarBoundaries(mesh, boundaries.velocity, inletK());
  auto oB = makeScalarBoundaries(mesh, boundaries.velocity, inletOmega());
  SSTModel model(mesh, fluid, boundaries.velocity, kB, oB, baseConfig());
  const SIMPLE simple(generous, 0, &model);
  const SIMPLEResult result =
      simple.solve(mesh, fluid, boundaries.velocity, boundaries.pressure,
                   VectorField(mesh.numberOfCells(), cfd::Vector2{0.0, 0.0}),
                   ScalarField(mesh.numberOfCells(), 0.0));
  const auto wall = ds::bottomWallDistribution(mesh, kMedium, result.velocity, result.pressure,
                                               ds::kDynamicViscosity, &model.wallShearStress());
  cfd::validation::step::WallShear shear;
  shear.x = wall.x;
  shear.bottom = wall.cf;
  shear.top.assign(wall.x.size(), 1.0);
  const auto reattachment = cfd::validation::step::separationTopology(shear).reattachment;
  std::printf("\nbudget 12000: converged=%d iters=%zu u=%.3e p=%.3e cont=%.3e x_r/H=%s\n",
              result.converged() ? 1 : 0, result.iterations, result.finalUResidual,
              result.finalPressureResidual, result.finalContinuityResidual,
              reattachment ? std::to_string(*reattachment).c_str() : "none");
}

TEST(DriverSeegmiller, IsDeterministic) {
  const Outcome first = run(kCoarse, baseConfig());
  const Outcome second = run(kCoarse, baseConfig());
  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.maximumTurbulentViscosity, second.maximumTurbulentViscosity);
  ASSERT_EQ(first.reattachment.has_value(), second.reattachment.has_value());
  if (first.reattachment) EXPECT_EQ(*first.reattachment, *second.reattachment);
}

// =====================================================================
// The CORRECTED sequence: mapped precursor inlet
// =====================================================================
//
// Identical to MeshSequence above in every respect except the inlet. Same
// meshes, same SST constants, same wall-function formulation, same limiter,
// same cross diffusion, same convection scheme, same outlet, same frozen
// detector, same frozen gate, same reference x_r/H = 6.26.
//
// The previous analytic-inlet sequence is preserved under its own evidence
// variant and is NOT overwritten -- it is the thing this is measured against.
//
// The A/B already showed the medium grid improving from E_x_r = 0.3582 to
// 0.2113. The open question this answers is whether the SEQUENCE is now
// stable: with the old inlet it moved monotonically AWAY from the reference
// (6.51 -> 8.50 -> 9.44), and a single improved mesh says nothing about that.

namespace {

// The verified precursor, computed once. Its own inflow checks live in
// test_driver_seegmiller_inlet.cpp; this reuses the same construction.
const ds::PrecursorProfile& correctedInlet() {
  static const ds::PrecursorProfile profile = ds::runPrecursor(ds::PrecursorSpec{});
  return profile;
}

}  // namespace

TEST(DriverSeegmiller, CorrectedMeshSequence) {
  const auto& profile = correctedInlet();
  ASSERT_TRUE(profile.converged) << "the precursor did not converge; the inlet is not usable";
  std::printf("\nprecursor inlet: delta/H=%.4f  station x/H=%.2f  Cf=%.6f  mass=%.6f\n",
              profile.deltaOverH, profile.extractionStationOverH, profile.skinFriction,
              profile.massFlowPerDepth);

  std::printf("\nDriver & Seegmiller, MAPPED PRECURSOR inlet (reference x_r/H = %.2f)\n",
              ds::kReferenceReattachment);
  std::printf("%-8s %6s %9s %8s %8s %8s %8s %5s %5s %10s %10s %6s %9s %5s %6s\n", "mesh", "cells",
              "x_r/H", "E_x_r", "y+ mean", "y+ min", "y+ max", "out", "flr", "min Cf", "Cp rec",
              "signs", "max mu_t", "iter", "conv");

  std::vector<Outcome> outcomes;
  for (const auto& spec : {kCoarse, kMedium, kFine}) {
    Outcome outcome = run(spec, baseConfig(), &profile);
    std::printf(
        "%-8s %6zu %9.4f %8.4f %8.2f %8.2f %8.2f %5zu %5zu %10.3e %10.4f %6zu %9.3e "
        "%5zu %6d\n",
        outcome.label.c_str(), outcome.cells, outcome.reattachment ? *outcome.reattachment : -1.0,
        outcome.reattachmentError, outcome.meanWallYPlus, outcome.minimumWallYPlus,
        outcome.maximumWallYPlus, outcome.facesOutsideValidBand, outcome.facesWithFlooredViscosity,
        outcome.minimumCf, outcome.cpRecovery, outcome.cfSignChanges,
        outcome.maximumTurbulentViscosity, outcome.iterations, outcome.converged ? 1 : 0);
    writeEvidence(outcome, "precursor_inlet");
    outcomes.push_back(std::move(outcome));
  }
  writeSummary(outcomes, "precursor_inlet");

  for (const auto& outcome : outcomes) {
    ASSERT_TRUE(outcome.reattachment.has_value()) << outcome.label << ": no reattachment found";
    EXPECT_TRUE(outcome.converged) << outcome.label;
    // Structural checks, unchanged from the frozen methodology section 5.
    EXPECT_LT(outcome.minimumCf, 0.0) << outcome.label << ": the flow never separated";
    EXPECT_GT(outcome.outletCf, 0.0) << outcome.label << ": re-separated before the outlet";
    EXPECT_GT(outcome.cpRecovery, 0.0) << outcome.label << ": no pressure recovery";
    EXPECT_EQ(outcome.reversedOutletCells, 0u) << outcome.label << ": reversed flow at the outlet";
  }

  // --- the two questions the sequence exists to answer ---

  const Real coarseToMedium = *outcomes[1].reattachment - *outcomes[0].reattachment;
  const Real mediumToFine = *outcomes[2].reattachment - *outcomes[1].reattachment;
  std::printf("\nshifts: coarse->medium %+.4f, medium->fine %+.4f\n", coarseToMedium, mediumToFine);
  std::printf("E_x_r: coarse %.4f, medium %.4f, fine %.4f\n", outcomes[0].reattachmentError,
              outcomes[1].reattachmentError, outcomes[2].reattachmentError);

  // 1. ACCURACY on the valid resolved meshes.
  //
  // VACUITY GUARD, added after the first corrected run exposed the hole: every
  // mesh failed G1 (60, 3, 3 faces outside the band), so the loop below skipped
  // all three and the accuracy gate was never evaluated -- yet the test
  // reported PASS. An accuracy gate that evaluates on nothing is not a gate.
  //
  // At least one mesh must satisfy the precondition, or the sequence has not
  // tested accuracy at all and must say so.
  const auto validMeshes = std::count_if(outcomes.begin(), outcomes.end(), [](const Outcome& o) {
    return o.facesOutsideValidBand == 0;
  });
  EXPECT_GT(validMeshes, 0)
      << "NO mesh in the sequence satisfies G1, so the accuracy gate was evaluated on nothing. "
         "This is not a pass -- it means the wall function was applied outside its validity on "
         "every mesh, and x_r/H cannot be judged from any of them.";

  for (const auto& outcome : outcomes) {
    if (outcome.facesOutsideValidBand > 0) {
      std::printf("%s: %zu wall faces outside G1 -- excluded from the accuracy gate\n",
                  outcome.label.c_str(), outcome.facesOutsideValidBand);
      continue;
    }
    EXPECT_LE(outcome.reattachmentError, 0.25)
        << outcome.label << ": E_x_r = " << outcome.reattachmentError
        << " exceeds the frozen cap on a G1-valid mesh";
  }

  // 2. MESH STABILITY.
  //
  // Shrinking increments alone are NOT sufficient, and the first corrected run
  // proved it: +1.94 then +1.19 shrank, satisfied this criterion, and the
  // sequence was still marching monotonically AWAY from the reference
  // (E_x_r 0.098 -> 0.211 -> 0.401). A geometric extrapolation of those
  // increments lands near x_r/H = 10.6, not 6.26.
  //
  // So both conditions are required: the increments must shrink AND the
  // sequence must not be moving away from the reference.
  EXPECT_LT(std::abs(mediumToFine), std::abs(coarseToMedium))
      << "the sequence is not converging: medium->fine (" << mediumToFine
      << ") is not smaller than coarse->medium (" << coarseToMedium << ")";

  EXPECT_LE(outcomes[2].reattachmentError, outcomes[1].reattachmentError)
      << "refinement made the answer WORSE: E_x_r went " << outcomes[1].reattachmentError << " -> "
      << outcomes[2].reattachmentError
      << ". A sequence walking away from the reference is not converging to it, however much "
         "the step size shrinks.";
}

TEST(DriverSeegmiller, CorrectedSequenceIsDeterministic) {
  const auto& profile = correctedInlet();
  const Outcome first = run(kMedium, baseConfig(), &profile);
  const Outcome second = run(kMedium, baseConfig(), &profile);
  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.maximumTurbulentViscosity, second.maximumTurbulentViscosity);
  ASSERT_EQ(first.reattachment.has_value(), second.reattachment.has_value());
  if (first.reattachment) EXPECT_EQ(*first.reattachment, *second.reattachment);
}
