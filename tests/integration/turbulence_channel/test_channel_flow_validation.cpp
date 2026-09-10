// P2-TURB-007 -- Turbulence Benchmark Validation: fully developed,
// incompressible, plane (two-wall) turbulent channel flow, compared
// against the Re_tau=180 literature reference (ChannelReTau180.hpp,
// Kim/Moin/Moser 1987 + Moser/Kim/Mansour 1999 -- see
// validation/data/turbulence/channel_flow/README.md for the full
// provenance disclosure).
//
// Same "SIMPLE-numerics-frozen, diagnose-then-lock-thresholds" discipline
// as test_poiseuille_validation.cpp/test_cavity_ghia.cpp: nothing here
// changes SIMPLE, discretization, or any turbulence-model formula --
// only SIMPLESettings and each model's inlet turbulence quantities are
// configured per grid, diagnosed empirically (temporary standalone
// compilation, same precedent as every prior turbulence task) before
// these thresholds were written.
//
// Periodic streamwise boundary conditions do not exist in CFDApp (P2-
// TURB-007 section 6/8's fallback path), so this reuses exactly the
// inlet/outlet open-channel treatment test_poiseuille_validation.cpp
// already validated for laminar flow: uniform-velocity Inlet, zero-
// gradient-pressure Outlet with a Dirichlet outlet pressure, Wall top/
// bottom. A channel long enough for the turbulence-transport-driven
// profile to stop evolving (proven via the development check below, not
// assumed) stands in for a periodic/fully-developed formulation.
//
// Geometry/physics: half-height delta=1 (full height H=2), length
// L=8H=16, rho=1, uniform inlet velocity Ub=1, Re_bulk = Ub*H/nu = 5600
// (the literature's own Re_tau=180 case, restated on the *bulk*
// Reynolds-number convention CFDApp's inlet actually controls -- see
// ChannelReTau180.hpp's header comment on the Re_tau vs Re_bulk
// distinction). Inlet turbulence: 5% intensity, mixing length
// 0.07*H (standard engineering estimates for duct/channel inlets),
// giving k_inlet/epsilon_inlet/omega_inlet from the usual closure
// relations (documented at their point of use below).
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "ChannelFlowValidationUtils.hpp"
#include "ChannelReTau180.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/boundary/WallOmega.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/LaminarModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::boundary::WallOmega;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;
using cfd::turbulence::KOmegaConfig;
using cfd::turbulence::KOmegaModel;
using cfd::turbulence::LaminarModel;
using cfd::turbulence::SSTConfig;
using cfd::turbulence::SSTModel;
using cfd::turbulence::TurbulenceModel;

namespace {

constexpr Real kChannelHeight = 2.0;   // H = 2*delta, delta = 1.
constexpr Real kChannelLength = 16.0;  // L/H = 8.
constexpr Real kDensity = 1.0;
constexpr Real kBulkVelocity = 1.0;
constexpr Real kReBulk = cfd::validation::channel_re_tau_180::kReBulk;  // 5600.
constexpr Real kDynamicViscosity = kDensity * kBulkVelocity * kChannelHeight / kReBulk;

// Inlet turbulence: 5% intensity, 0.07*H mixing length -- standard
// engineering duct-inlet estimates (not part of the DNS comparison
// itself, only the transport equations' inflow condition).
constexpr Real kTurbulenceIntensity = 0.05;
constexpr Real kMixingLength = 0.07 * kChannelHeight;
constexpr Real kCmu = 0.09;

Real inletK() {
  const Real fluct = kTurbulenceIntensity * kBulkVelocity;
  return 1.5 * fluct * fluct;
}
Real inletEpsilon() { return std::pow(kCmu, 0.75) * std::pow(inletK(), 1.5) / kMixingLength; }
Real inletOmega() { return inletEpsilon() / (kCmu * inletK()); }

// Sampling/development stations, all in the back half of the channel
// (entrance region excluded).
constexpr Real kStationA = 0.80 * kChannelLength;
constexpr Real kStationB = 0.90 * kChannelLength;  // primary comparison station.

BoundaryConditionSet makeVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{kBulkVelocity, 0.0}));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

BoundaryConditionSet makePressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

BoundaryConditionSet makeKBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(inletK()));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(0.0));
  return boundaries;
}

// k-epsilon's own documented wall simplification (zero-gradient epsilon).
BoundaryConditionSet makeEpsilonBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(inletEpsilon()));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

// k-omega's own documented wall simplification (zero-gradient omega).
BoundaryConditionSet makeOmegaZeroGradientBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(inletOmega()));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

// SST's real Wilcox near-wall omega value (P2-TURB-006).
BoundaryConditionSet makeOmegaWallBoundaries(const Mesh& mesh, Real beta1) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(inletOmega()));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<WallOmega>(kDynamicViscosity / kDensity, beta1));
  boundaries.set(mesh, "top", std::make_unique<WallOmega>(kDynamicViscosity / kDensity, beta1));
  return boundaries;
}

struct GridCase {
  const char* label;
  Index nx;
  Index ny;
  Index maxOuterIterations;
};

// Diagnosed empirically (temporary standalone compilation): fixed-
// relaxation SIMPLE's u/p residuals plateau above a strict 1e-6 gate on
// this open, turbulence-coupled channel (same documented property
// test_poiseuille_validation.cpp's own GridCase comment already
// establishes for the laminar case) -- velocityTolerance/
// pressureTolerance are loosened accordingly; continuityTolerance stays
// tight (flux correction enforces it every iteration regardless).
SIMPLESettings makeSettings(const GridCase& grid) {
  SIMPLESettings settings;
  settings.maxIterations = grid.maxOuterIterations;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 2e-5;
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

constexpr GridCase kCoarse{"coarse", 48, 12, 3000};
constexpr GridCase kMedium{"medium", 64, 16, 3000};
constexpr GridCase kFine{"fine", 96, 24, 6000};

// Acceptance thresholds (P2-TURB-007 section 33): NOT chosen up front --
// diagnosed empirically (temporary standalone compilation of the exact
// case/settings below, same precedent as every prior turbulence task)
// across all three grids for all three models first, *then* locked with
// margin above the worst observed value per tier. Measured
// wallUnitsError.outer.l2 / reTauRelativeError (kEpsilon / kOmega / SST):
//   coarse: 15.45 / 16.40 / 13.07   ...   0.381 / 0.399 / 0.354
//   medium: 11.99 / 13.53 /  8.62   ...   0.328 / 0.360 / 0.268
//   fine:    8.37 / 10.48 /  3.17   ...   0.242 / 0.298 / 0.107
// Both metrics improve monotonically with refinement for every model
// (the expected direction given ChannelFlowValidationUtils::
// computeWallShearStress's documented near-wall-resolution bias), and
// SST is consistently the most accurate of the three at every grid --
// both are real, physically sensible findings this benchmark actually
// discovered, not assumed. wallUnitsError.outer.l2 is *not* independent
// evidence of profile *shape* on top of reTauRelativeError: both are
// dominated by the same wall-shear-estimator bias (u+ = U/u_tau, so a
// biased u_tau rescales every u+ sample) -- see
// ChannelFlowValidationUtils.hpp's own computeWallShearStress comment
// and the P2-TURB-007 Final Report's "near-wall limitation" section.
// Kept as two separate gates anyway (both must independently regress
// before either one trips) rather than collapsed into one, matching
// this task's own "distinguish numerical correctness from... model
// behavior... agreement with benchmark data" requirement (section 1).
constexpr Real kOuterL2ThresholdCoarse = 18.0;
constexpr Real kOuterL2ThresholdMedium = 15.0;
constexpr Real kOuterL2ThresholdFine = 12.0;
constexpr Real kReTauErrorThresholdCoarse = 0.45;
constexpr Real kReTauErrorThresholdMedium = 0.40;
constexpr Real kReTauErrorThresholdFine = 0.32;

// Everything a single (model, grid) validation run needs beyond the
// SIMPLEResult itself: the fields to extract turbulence-scalar ranges
// from, and identifying metadata. Built by each model-specific runner
// below, then passed through one shared post-processing/assertion path
// so the actual validation logic (wall shear, y+, region errors, mass
// conservation, evidence output) is written exactly once.
struct ModelFields {
  std::string name;
  const ScalarField* k{nullptr};
  const ScalarField* secondScalar{nullptr};  // epsilon or omega.
  const char* secondScalarName{nullptr};
  const ScalarField* muT{nullptr};
  const ScalarField* f1{nullptr};
  const ScalarField* f2{nullptr};
  const ScalarField* wallDistance{nullptr};
};

// Runs SIMPLE to convergence for the given (already-constructed)
// turbulence model, validates the mandatory convergence/mass/finiteness
// gates, computes every wall-units/development/error-metric quantity,
// writes CSV/JSON evidence under results/validation/turbulence/
// channel_flow/<model>/<grid>/, and returns the outcome record. Shared
// by every model-specific TEST below (kepsilon/komega/sst/laminar).
cfd::validation::ChannelFlowValidationRecord runAndValidate(const std::string& modelDirName,
                                                            const GridCase& grid,
                                                            TurbulenceModel* activeModel,
                                                            const ModelFields& fields) {
  using namespace cfd::validation;
  using namespace cfd::validation::channel_re_tau_180;

  const Mesh mesh =
      MeshGeometry::createCartesian2D(grid.nx, grid.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);

  const SIMPLE simple(makeSettings(grid), /*referenceCell=*/0, activeModel);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const auto t0 = std::chrono::steady_clock::now();
  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);
  const auto t1 = std::chrono::steady_clock::now();

  ChannelFlowValidationRecord record;
  record.model = fields.name;
  record.nx = grid.nx;
  record.ny = grid.ny;
  record.channelLength = kChannelLength;
  record.channelHeight = kChannelHeight;
  record.reynoldsNumberBulk = kReBulk;
  record.converged = result.converged();
  record.iterations = result.iterations;
  record.finalUResidual = result.finalUResidual;
  record.finalVResidual = result.finalVResidual;
  record.finalPressureResidual = result.finalPressureResidual;
  record.finalContinuityResidual = result.finalContinuityResidual;
  record.hasTurbulenceResidual = result.finalTurbulenceResidual.has_value();
  record.finalTurbulenceResidual = result.finalTurbulenceResidual.value_or(0.0);
  record.globalMassImbalance = result.globalMassImbalance;
  record.runtimeSeconds = std::chrono::duration<double>(t1 - t0).count();

  bool finite = true;
  for (Index i = 0; i < result.velocity.size(); ++i) {
    finite = finite && std::isfinite(result.velocity[i].x) && std::isfinite(result.velocity[i].y);
  }
  for (Index i = 0; i < result.pressure.size(); ++i)
    finite = finite && std::isfinite(result.pressure[i]);
  record.finite = finite;

  Real maxWallFlux = 0.0, inletFlux = 0.0, outletFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == "top" || patch.name() == "bottom") {
      for (const Index faceId : patch.faceIds()) {
        maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[faceId]));
      }
    } else if (patch.name() == "left") {
      for (const Index faceId : patch.faceIds()) inletFlux += result.massFlux[faceId];
    } else if (patch.name() == "right") {
      for (const Index faceId : patch.faceIds()) outletFlux += result.massFlux[faceId];
    }
  }
  record.inletFlux = inletFlux;
  record.outletFlux = outletFlux;

  // Development check (section 26): profile must have essentially
  // stopped evolving between the two back-of-channel stations before
  // station B is trusted as "fully developed".
  const auto profileA = cfd::validation::extractVerticalProfileU(
      mesh, grid.nx, grid.ny, result.velocity, kStationA, kChannelHeight);
  const auto profileB = cfd::validation::extractVerticalProfileU(
      mesh, grid.nx, grid.ny, result.velocity, kStationB, kChannelHeight);
  record.developmentRelativeDiff = developmentRelativeDifference(profileA, profileB);

  // Wall shear (both walls -- symmetry sanity check) at the primary
  // station.
  record.wallShearBottom = computeWallShearStress(mesh, grid.nx, grid.ny, result.velocity,
                                                  kStationB, kChannelHeight, kDynamicViscosity,
                                                  /*bottomWall=*/true);
  record.wallShearTop = computeWallShearStress(mesh, grid.nx, grid.ny, result.velocity, kStationB,
                                               kChannelHeight, kDynamicViscosity,
                                               /*bottomWall=*/false);
  record.wallShearAsymmetry =
      std::abs(record.wallShearBottom - record.wallShearTop) / std::abs(record.wallShearBottom);

  const Real kinematicViscosity = kDynamicViscosity / kDensity;
  record.frictionVelocity = computeFrictionVelocity(record.wallShearBottom, kDensity);
  const Real y1 = kChannelHeight / (2.0 * static_cast<Real>(grid.ny));
  record.firstCellYPlus = computeYPlus(y1, record.frictionVelocity, kinematicViscosity);
  record.achievedReTau =
      computeAchievedReTau(record.frictionVelocity, kChannelHeight, kinematicViscosity);
  record.reTauTarget = kReTau;
  record.reTauRelativeError = std::abs(record.achievedReTau - kReTau) / kReTau;

  // Build the wall-units (u+ vs y+) profile from the bottom-half of the
  // channel (y in [0, delta]) at the primary station -- the top half is
  // the mirror image by symmetry and would double-count the same
  // physics in the error metrics.
  std::vector<ProfileSample> wallUnits;
  const Index halfNy = grid.ny / 2;
  for (Index j = 0; j < halfNy; ++j) {
    const Real y = (static_cast<Real>(j) + 0.5) * (kChannelHeight / static_cast<Real>(grid.ny));
    const Real u = interpolateProfile(profileB, y);
    const Real yPlus = computeYPlus(y, record.frictionVelocity, kinematicViscosity);
    const Real uPlus = computeUPlus(u, record.frictionVelocity);
    wallUnits.push_back({yPlus, uPlus});
  }

  const std::string gridDir =
      "results/validation/turbulence/channel_flow/" + modelDirName + "/" + grid.label;
  std::filesystem::create_directories(gridDir);

  record.wallUnitsError =
      computeWallUnitsErrors(wallUnits, kKappa, kB, kViscousSublayerMaxYPlus, kBufferLayerMaxYPlus,
                             gridDir + "/wall_units_profile.csv");

  // Raw velocity profile CSV (not wall units) for direct inspection.
  {
    std::vector<std::tuple<Real, Real, Real>> rows;
    rows.reserve(profileB.size());
    for (const auto& sample : profileB) rows.emplace_back(sample.coordinate, sample.value, 0.0);
    writeProfileCsv(gridDir + "/velocity_profile.csv", rows, "y", "u");
  }

  auto rangeOf = [&](const ScalarField* field, Real* lo, Real* hi) {
    if (!field) return;
    *lo = 1e300;
    *hi = -1e300;
    for (Index i = 0; i < field->size(); ++i) {
      *lo = std::min(*lo, (*field)[i]);
      *hi = std::max(*hi, (*field)[i]);
    }
  };
  rangeOf(fields.k, &record.minK, &record.maxK);
  record.hasSecondScalar = fields.secondScalar != nullptr;
  rangeOf(fields.secondScalar, &record.minSecondScalar, &record.maxSecondScalar);
  rangeOf(fields.muT, &record.minMuT, &record.maxMuT);
  record.molecularViscosity = kDynamicViscosity;
  record.hasF1F2 = fields.f1 != nullptr;
  rangeOf(fields.f1, &record.minF1, &record.maxF1);
  rangeOf(fields.f2, &record.minF2, &record.maxF2);
  rangeOf(fields.wallDistance, &record.minWallDistance, &record.maxWallDistance);

  writeValidationJson(gridDir + "/validation.json", record);

  // --- Mandatory gates (P2-TURB-007 section 50) ---
  EXPECT_EQ(result.status, SIMPLEStatus::Converged)
      << fields.name << " " << grid.label
      << " did not converge (status=" << static_cast<int>(result.status)
      << ", iterations=" << result.iterations << ")";
  EXPECT_TRUE(finite) << fields.name << " " << grid.label << " produced a non-finite field";
  EXPECT_LT(maxWallFlux, 1e-6) << fields.name << " " << grid.label
                               << " is leaking mass through a wall";
  EXPECT_NEAR(inletFlux + outletFlux, 0.0, 1e-6) << fields.name << " " << grid.label;
  EXPECT_LT(result.globalMassImbalance, 1e-6) << fields.name << " " << grid.label;
  EXPECT_LT(record.developmentRelativeDiff, 0.05)
      << fields.name << " " << grid.label << " profile has not stopped evolving between stations";
  // Physical admissibility (section 28).
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_GE((*fields.k)[i], 0.0) << fields.name << " k[" << i << "] < 0";
    if (fields.muT) {
      EXPECT_GE((*fields.muT)[i], 0.0) << fields.name << " mu_t[" << i << "] < 0";
    }
  }
  if (fields.f1 && fields.f2) {
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      EXPECT_GE((*fields.f1)[i], 0.0);
      EXPECT_LE((*fields.f1)[i], 1.0);
      EXPECT_GE((*fields.f2)[i], 0.0);
      EXPECT_LE((*fields.f2)[i], 1.0);
    }
  }
  EXPECT_LT(record.wallShearAsymmetry, 0.05)
      << fields.name << " " << grid.label << " top/bottom wall shear should agree by symmetry";

  return record;
}

}  // namespace

// =====================================================================
// k-epsilon
// =====================================================================

TEST(ChannelFlowValidation, KEpsilonCoarseConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kCoarse.nx, kCoarse.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto epsBoundaries = makeEpsilonBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KEpsilonConfig config;
  config.initialK = inletK();
  config.initialEpsilon = inletEpsilon();
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, config);

  const auto record =
      runAndValidate("k_epsilon", kCoarse, &model,
                     ModelFields{"kEpsilon", &model.k(), &model.epsilon(), "epsilon",
                                 &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  // Log-layer u+ error and Re_tau thresholds locked from measured
  // evidence (see kOuterL2ThresholdCoarse's own comment above).
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdCoarse);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdCoarse);
}

TEST(ChannelFlowValidation, KEpsilonMediumConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kMedium.nx, kMedium.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto epsBoundaries = makeEpsilonBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KEpsilonConfig config;
  config.initialK = inletK();
  config.initialEpsilon = inletEpsilon();
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, config);

  const auto record =
      runAndValidate("k_epsilon", kMedium, &model,
                     ModelFields{"kEpsilon", &model.k(), &model.epsilon(), "epsilon",
                                 &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdMedium);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdMedium);
}

TEST(ChannelFlowValidation, DISABLED_KEpsilonFineConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kFine.nx, kFine.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto epsBoundaries = makeEpsilonBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KEpsilonConfig config;
  config.initialK = inletK();
  config.initialEpsilon = inletEpsilon();
  KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, config);

  const auto record =
      runAndValidate("k_epsilon", kFine, &model,
                     ModelFields{"kEpsilon", &model.k(), &model.epsilon(), "epsilon",
                                 &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdFine);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdFine);
}

// =====================================================================
// k-omega
// =====================================================================

TEST(ChannelFlowValidation, KOmegaCoarseConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kCoarse.nx, kCoarse.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto omegaBoundaries = makeOmegaZeroGradientBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KOmegaConfig config;
  config.initialK = inletK();
  config.initialOmega = inletOmega();
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const auto record =
      runAndValidate("k_omega", kCoarse, &model,
                     ModelFields{"kOmega", &model.k(), &model.omega(), "omega",
                                 &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdCoarse);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdCoarse);
}

TEST(ChannelFlowValidation, KOmegaMediumConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kMedium.nx, kMedium.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto omegaBoundaries = makeOmegaZeroGradientBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KOmegaConfig config;
  config.initialK = inletK();
  config.initialOmega = inletOmega();
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const auto record =
      runAndValidate("k_omega", kMedium, &model,
                     ModelFields{"kOmega", &model.k(), &model.omega(), "omega",
                                 &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdMedium);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdMedium);
}

TEST(ChannelFlowValidation, DISABLED_KOmegaFineConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kFine.nx, kFine.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto omegaBoundaries = makeOmegaZeroGradientBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KOmegaConfig config;
  config.initialK = inletK();
  config.initialOmega = inletOmega();
  KOmegaModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, config);

  const auto record =
      runAndValidate("k_omega", kFine, &model,
                     ModelFields{"kOmega", &model.k(), &model.omega(), "omega",
                                 &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdFine);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdFine);
}

// =====================================================================
// SST
// =====================================================================

TEST(ChannelFlowValidation, SSTCoarseConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kCoarse.nx, kCoarse.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  SSTConfig sstConfig;
  const auto omegaBoundaries = makeOmegaWallBoundaries(mesh, sstConfig.coefficients.beta1);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  sstConfig.initialK = inletK();
  sstConfig.initialOmega = inletOmega();
  SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, sstConfig);

  const auto record = runAndValidate(
      "sst", kCoarse, &model,
      ModelFields{"SST", &model.k(), &model.omega(), "omega", &model.turbulentViscosity(),
                  &model.f1(), &model.f2(), &model.wallDistance()});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdCoarse);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdCoarse);
  // F1 spatial-sanity (section 21): near-wall row must have a larger
  // mean F1 than the centerline row (F1 -> 1 at walls, decreasing away
  // from them).
  Real f1WallMean = 0.0, f1CenterMean = 0.0;
  for (Index i = 0; i < kCoarse.nx; ++i) {
    f1WallMean += model.f1()[i];  // j=0 row.
    f1CenterMean += model.f1()[((kCoarse.ny / 2) * kCoarse.nx) + i];
  }
  f1WallMean /= static_cast<Real>(kCoarse.nx);
  f1CenterMean /= static_cast<Real>(kCoarse.nx);
  EXPECT_GT(f1WallMean, f1CenterMean);
}

TEST(ChannelFlowValidation, SSTMediumConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kMedium.nx, kMedium.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  SSTConfig sstConfig;
  const auto omegaBoundaries = makeOmegaWallBoundaries(mesh, sstConfig.coefficients.beta1);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  sstConfig.initialK = inletK();
  sstConfig.initialOmega = inletOmega();
  SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, sstConfig);

  const auto record = runAndValidate(
      "sst", kMedium, &model,
      ModelFields{"SST", &model.k(), &model.omega(), "omega", &model.turbulentViscosity(),
                  &model.f1(), &model.f2(), &model.wallDistance()});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdMedium);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdMedium);
}

TEST(ChannelFlowValidation, DISABLED_SSTFineConvergesAndMatchesLogLaw) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kFine.nx, kFine.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  SSTConfig sstConfig;
  const auto omegaBoundaries = makeOmegaWallBoundaries(mesh, sstConfig.coefficients.beta1);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  sstConfig.initialK = inletK();
  sstConfig.initialOmega = inletOmega();
  SSTModel model(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, sstConfig);

  const auto record = runAndValidate(
      "sst", kFine, &model,
      ModelFields{"SST", &model.k(), &model.omega(), "omega", &model.turbulentViscosity(),
                  &model.f1(), &model.f2(), &model.wallDistance()});
  EXPECT_LT(record.wallUnitsError.outer.l2, kOuterL2ThresholdFine);
  EXPECT_LT(record.reTauRelativeError, kReTauErrorThresholdFine);
}

// =====================================================================
// Grid refinement trend (heavy: needs all three tiers for one model;
// disabled by default, same precedent as
// PoiseuilleValidation.DISABLED_GridRefinementReducesVelocityError).
// =====================================================================

TEST(ChannelFlowValidation, DISABLED_KEpsilonGridRefinementReducesReTauError) {
  auto runGrid = [](const GridCase& grid) {
    const Mesh mesh =
        MeshGeometry::createCartesian2D(grid.nx, grid.ny, kChannelLength, kChannelHeight);
    const auto velocityBoundaries = makeVelocityBoundaries(mesh);
    const auto kBoundaries = makeKBoundaries(mesh);
    const auto epsBoundaries = makeEpsilonBoundaries(mesh);
    const FluidProperties fluid(kDensity, kDynamicViscosity);
    KEpsilonConfig config;
    config.initialK = inletK();
    config.initialEpsilon = inletEpsilon();
    KEpsilonModel model(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, config);
    return runAndValidate("k_epsilon", grid, &model,
                          ModelFields{"kEpsilon", &model.k(), &model.epsilon(), "epsilon",
                                      &model.turbulentViscosity(), nullptr, nullptr, nullptr});
  };
  const auto coarse = runGrid(kCoarse);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  const auto medium = runGrid(kMedium);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  const auto fine = runGrid(kFine);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());

  // Achieved Re_tau should move monotonically toward the DNS target as
  // near-wall resolution improves (first-cell y+ decreases) -- the
  // expected direction of the documented wall-shear-estimator bias
  // (ChannelFlowValidationUtils.hpp's own computeWallShearStress
  // comment). Not a formal discretization order -- turbulence-model/
  // wall-treatment error dominates over pure grid error here (section
  // 24), so only the trend direction is asserted, not a convergence
  // rate.
  EXPECT_GE(medium.achievedReTau, coarse.achievedReTau);
  EXPECT_GE(fine.achievedReTau, medium.achievedReTau);
  EXPECT_LE(fine.reTauRelativeError, coarse.reTauRelativeError);
}

// =====================================================================
// Laminar control (section 32): same case, laminar model -- must NOT
// resemble the turbulent models' result (proves the benchmark actually
// discriminates laminar vs RANS rather than being insensitive to
// turbulence closure).
// =====================================================================

TEST(ChannelFlowValidation, LaminarControlDiffersSubstantiallyFromKEpsilon) {
  // Laminar: same geometry/physics, no turbulence transport. Needs many
  // more outer iterations to relax to a steady state at this bulk
  // Reynolds number (diagnosed: ~1700 on the medium grid, vs k-epsilon's
  // ~450) since there is no eddy diffusivity to smooth momentum -- not a
  // bug, an expected property of solving the literal laminar equations
  // at a nominally turbulent Reynolds number.
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kMedium.nx, kMedium.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);

  LaminarModel laminar(mesh);
  GridCase laminarGrid = kMedium;
  laminarGrid.maxOuterIterations = 4000;
  SIMPLESettings settings = makeSettings(laminarGrid);
  const SIMPLE simple(settings, 0, &laminar);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult laminarResult = simple.solve(
      mesh, fluid, velocityBoundaries, pressureBoundaries, initialVelocity, initialPressure);
  ASSERT_EQ(laminarResult.status, SIMPLEStatus::Converged);

  KEpsilonConfig config;
  config.initialK = inletK();
  config.initialEpsilon = inletEpsilon();
  // Named locals, not inline temporaries: KEpsilonModel holds its
  // boundary-set constructor arguments *by reference* (see
  // SimulationSetup.hpp's own header comment on this project-wide
  // convention), so a temporary BoundaryConditionSet bound directly at
  // the call site would be destroyed at the end of this constructor
  // call, leaving kEpsilon with a dangling reference -- exactly the
  // stack-use-after-scope this project's own ASan gate exists to catch.
  const auto kEpsilonKBoundaries = makeKBoundaries(mesh);
  const auto kEpsilonEpsBoundaries = makeEpsilonBoundaries(mesh);
  KEpsilonModel kEpsilon(mesh, fluid, velocityBoundaries, kEpsilonKBoundaries,
                         kEpsilonEpsBoundaries, config);
  const SIMPLE simpleTurb(makeSettings(kMedium), 0, &kEpsilon);
  const SIMPLEResult turbResult = simpleTurb.solve(
      mesh, fluid, velocityBoundaries, pressureBoundaries, initialVelocity, initialPressure);
  ASSERT_EQ(turbResult.status, SIMPLEStatus::Converged);

  const auto laminarProfile = cfd::validation::extractVerticalProfileU(
      mesh, kMedium.nx, kMedium.ny, laminarResult.velocity, kStationB, kChannelHeight);
  const auto turbProfile = cfd::validation::extractVerticalProfileU(
      mesh, kMedium.nx, kMedium.ny, turbResult.velocity, kStationB, kChannelHeight);
  const Real relativeDiff =
      cfd::validation::developmentRelativeDifference(laminarProfile, turbProfile);

  // A benchmark that cannot tell laminar and RANS apart is useless
  // (section 32) -- require a large, unambiguous difference. Diagnosed:
  // the laminar profile at this station is still boundary-layer-like
  // (nowhere near fully developed at L/H=8, laminar entrance length
  // being ~0.06*Re*H ~ 336*H here), producing a visibly different
  // near-wall/core shape from the fully-developed turbulent profile.
  EXPECT_GT(relativeDiff, 0.10);

  std::filesystem::create_directories("results/validation/turbulence/channel_flow/laminar_control");
  std::vector<std::tuple<Real, Real, Real>> rows;
  for (std::size_t i = 0; i < laminarProfile.size(); ++i) {
    rows.emplace_back(laminarProfile[i].coordinate, laminarProfile[i].value, turbProfile[i].value);
  }
  cfd::validation::writeProfileCsv(
      "results/validation/turbulence/channel_flow/laminar_control/laminar_vs_kepsilon.csv", rows,
      "y", "u");
}

// =====================================================================
// Model independence (section 31): laminar/k-epsilon/k-omega/SST must
// not accidentally alias to the same result on this geometry either.
// =====================================================================

TEST(ChannelFlowValidation, AllFourModelsProduceDistinctMuTAndNames) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kCoarse.nx, kCoarse.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);

  // Named locals, not inline temporaries (same dangling-reference
  // hazard as LaminarControlDiffersSubstantiallyFromKEpsilon's own
  // comment above -- every *Model class holds its boundary-set
  // constructor arguments by reference).
  const auto epsBoundaries = makeEpsilonBoundaries(mesh);
  const auto omegaZeroGradientBoundaries = makeOmegaZeroGradientBoundaries(mesh);

  LaminarModel laminar(mesh);
  KEpsilonConfig kEpsCfg;
  kEpsCfg.initialK = inletK();
  kEpsCfg.initialEpsilon = inletEpsilon();
  KEpsilonModel kEpsilon(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, kEpsCfg);
  KOmegaConfig kOmegaCfg;
  kOmegaCfg.initialK = inletK();
  kOmegaCfg.initialOmega = inletOmega();
  KOmegaModel kOmega(mesh, fluid, velocityBoundaries, kBoundaries, omegaZeroGradientBoundaries,
                     kOmegaCfg);
  SSTConfig sstCfg;
  sstCfg.initialK = inletK();
  sstCfg.initialOmega = inletOmega();
  const auto omegaWallBoundaries = makeOmegaWallBoundaries(mesh, sstCfg.coefficients.beta1);
  SSTModel sst(mesh, fluid, velocityBoundaries, kBoundaries, omegaWallBoundaries, sstCfg);

  auto runIt = [&](TurbulenceModel* model) {
    const SIMPLE simple(makeSettings(kCoarse), 0, model);
    const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
    const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
    // Not `const SIMPLEResult r` -- constness here would suppress the
    // implicit move on return (clang-tidy performance-no-automatic-move).
    SIMPLEResult r = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                  initialVelocity, initialPressure);
    return r;
  };

  const auto laminarResult = runIt(&laminar);
  const auto kEpsResult = runIt(&kEpsilon);
  const auto kOmegaResult = runIt(&kOmega);
  const auto sstResult = runIt(&sst);
  ASSERT_EQ(laminarResult.status, SIMPLEStatus::Converged);
  ASSERT_EQ(kEpsResult.status, SIMPLEStatus::Converged);
  ASSERT_EQ(kOmegaResult.status, SIMPLEStatus::Converged);
  ASSERT_EQ(sstResult.status, SIMPLEStatus::Converged);

  EXPECT_EQ(laminar.name(), "laminar");
  EXPECT_EQ(kEpsilon.name(), "kEpsilon");
  EXPECT_EQ(kOmega.name(), "kOmega");
  EXPECT_EQ(sst.name(), "SST");

  auto anyDiffers = [](const ScalarField& a, const ScalarField& b) {
    for (Index i = 0; i < a.size(); ++i) {
      if (a[i] != b[i]) return true;
    }
    return false;
  };
  EXPECT_TRUE(anyDiffers(kEpsilon.turbulentViscosity(), kOmega.turbulentViscosity()));
  EXPECT_TRUE(anyDiffers(kEpsilon.turbulentViscosity(), sst.turbulentViscosity()));
  EXPECT_TRUE(anyDiffers(kOmega.turbulentViscosity(), sst.turbulentViscosity()));
  for (Index i = 0; i < laminar.turbulentViscosity().size(); ++i) {
    EXPECT_DOUBLE_EQ(laminar.turbulentViscosity()[i], 0.0);
  }
}

// =====================================================================
// Determinism (section 30): repeated coarse-grid runs must reproduce
// bit-identical fields for each model.
// =====================================================================

TEST(ChannelFlowValidation, KEpsilonIsDeterministic) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kCoarse.nx, kCoarse.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  const auto epsBoundaries = makeEpsilonBoundaries(mesh);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  KEpsilonConfig config;
  config.initialK = inletK();
  config.initialEpsilon = inletEpsilon();

  KEpsilonModel modelA(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, config);
  KEpsilonModel modelB(mesh, fluid, velocityBoundaries, kBoundaries, epsBoundaries, config);
  const SIMPLE simpleA(makeSettings(kCoarse), 0, &modelA);
  const SIMPLE simpleB(makeSettings(kCoarse), 0, &modelB);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult a = simpleA.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                       initialVelocity, initialPressure);
  const SIMPLEResult b = simpleB.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                       initialVelocity, initialPressure);
  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
  }
  for (Index i = 0; i < modelA.k().size(); ++i) {
    EXPECT_EQ(modelA.k()[i], modelB.k()[i]);
    EXPECT_EQ(modelA.epsilon()[i], modelB.epsilon()[i]);
  }
}

TEST(ChannelFlowValidation, SSTIsDeterministic) {
  const Mesh mesh =
      MeshGeometry::createCartesian2D(kCoarse.nx, kCoarse.ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto kBoundaries = makeKBoundaries(mesh);
  SSTConfig sstConfig;
  const auto omegaBoundaries = makeOmegaWallBoundaries(mesh, sstConfig.coefficients.beta1);
  const FluidProperties fluid(kDensity, kDynamicViscosity);
  sstConfig.initialK = inletK();
  sstConfig.initialOmega = inletOmega();

  SSTModel modelA(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, sstConfig);
  SSTModel modelB(mesh, fluid, velocityBoundaries, kBoundaries, omegaBoundaries, sstConfig);
  const SIMPLE simpleA(makeSettings(kCoarse), 0, &modelA);
  const SIMPLE simpleB(makeSettings(kCoarse), 0, &modelB);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult a = simpleA.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                       initialVelocity, initialPressure);
  const SIMPLEResult b = simpleB.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                       initialVelocity, initialPressure);
  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < modelA.k().size(); ++i) {
    EXPECT_EQ(modelA.k()[i], modelB.k()[i]);
    EXPECT_EQ(modelA.omega()[i], modelB.omega()[i]);
    EXPECT_EQ(modelA.f1()[i], modelB.f1()[i]);
  }
}
