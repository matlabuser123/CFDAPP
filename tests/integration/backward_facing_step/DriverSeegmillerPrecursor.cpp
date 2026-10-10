// P16 -- the precursor inflow for the Driver & Seegmiller step (Approach A).
//
// The reference inflow profiles cannot be obtained in this environment
// (results/p16/turbulence/separated_flow/reference_specification.md section 5),
// so the incoming boundary layer is DEVELOPED by CFDApp in an upstream duct and
// mapped into the step inlet.
//
// THE THING THE FIRST ATTEMPT GOT WRONG, and the reason this file exists
// separately from the case geometry: the reference inflow is NOT fully
// developed channel flow. delta/H is about 1.5 in an 8H duct, where fully
// developed would be delta = 4H. A precursor run "until developed" produces a
// confidently wrong inlet. This one is run long and SAMPLED at the station
// where delta/H first reaches 1.5.
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "DriverSeegmillerCase.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/SSTModel.hpp"

namespace cfd::validation::driver_seegmiller {

namespace {

constexpr const char* kInletPatch = "left";

// 99% boundary-layer thickness from the bottom wall of one column.
Real boundaryLayerThickness(const std::vector<Real>& y, const std::vector<Real>& u,
                            Real edgeVelocity) {
  const Real target = 0.99 * edgeVelocity;
  for (std::size_t j = 1; j < u.size(); ++j) {
    if (u[j] >= target) {
      const Real denominator = u[j] - u[j - 1];
      if (std::abs(denominator) < 1e-30) return y[j];
      const Real t = (target - u[j - 1]) / denominator;
      return y[j - 1] + (t * (y[j] - y[j - 1]));
    }
  }
  return y.back();
}

Real freeStreamK() {
  const Real fluct = 0.02 * kReferenceVelocity;
  return 1.5 * fluct * fluct;
}

Real freeStreamOmega() {
  return std::sqrt(freeStreamK()) / (std::pow(0.09, 0.25) * 0.07 * kUpstreamChannelHeight);
}

}  // namespace

PrecursorProfile runPrecursor(const PrecursorSpec& spec) {
  const Index nx = spec.cellsStreamwise;
  const Index ny = spec.cellsWallNormal;
  const Real length = spec.lengthOverH * kStepHeight;
  const mesh::Mesh mesh =
      mesh::MeshGeometry::createCartesian2D(nx, ny, length, kUpstreamChannelHeight);

  boundary::BoundaryConditionSet velocity;
  boundary::BoundaryConditionSet pressure;
  boundary::BoundaryConditionSet kBoundaries;
  boundary::BoundaryConditionSet omegaBoundaries;

  velocity.set(mesh, "left", std::make_unique<boundary::Inlet>(Vector2{kReferenceVelocity, 0.0}));
  velocity.set(mesh, "right", std::make_unique<boundary::Outlet>());
  velocity.set(mesh, "bottom", std::make_unique<boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<boundary::Wall>());

  pressure.set(mesh, "left", std::make_unique<boundary::FixedGradient>(0.0));
  pressure.set(mesh, "right", std::make_unique<boundary::FixedValue>(0.0));
  pressure.set(mesh, "bottom", std::make_unique<boundary::FixedGradient>(0.0));
  pressure.set(mesh, "top", std::make_unique<boundary::FixedGradient>(0.0));

  // Free-stream turbulence at the duct inlet only. The boundary layer generates
  // its own turbulence downstream -- that generation is the entire reason for
  // running a precursor rather than prescribing a profile.
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    if (std::string(patch) == kInletPatch) {
      kBoundaries.set(mesh, patch, std::make_unique<boundary::FixedValue>(freeStreamK()));
      omegaBoundaries.set(mesh, patch, std::make_unique<boundary::FixedValue>(freeStreamOmega()));
    } else {
      kBoundaries.set(mesh, patch, std::make_unique<boundary::FixedGradient>(0.0));
      omegaBoundaries.set(mesh, patch, std::make_unique<boundary::FixedGradient>(0.0));
    }
  }

  turbulence::SSTConfig config;
  config.initialK = freeStreamK();
  config.initialOmega = freeStreamOmega();
  config.wallFunctions = true;
  config.wallFunctionCoefficients.cMu = config.coefficients.betaStar;

  const physics::FluidProperties fluid(kDensity, kDynamicViscosity);
  turbulence::SSTModel model(mesh, fluid, velocity, kBoundaries, omegaBoundaries, config);

  pressure_velocity::SIMPLESettings settings;
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
  // The configuration BackwardFacingStepCase already uses for this geometry
  // family, adopted rather than re-derived: a Jacobi-preconditioned pressure
  // solve with the P12-NUM-004 fallback enabled.
  //
  // The first version omitted both and the pressure correction failed on
  // iteration ZERO (SIMPLEStatus::PressureCorrectionFailure). The duct's cells
  // are 10:1 in aspect ratio, which makes the pressure Poisson system far worse
  // conditioned than the near-square step meshes, and an unpreconditioned
  // BiCGSTAB with a 2000-iteration budget could not solve it.
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-12;
  settings.pressureSolver.relativeTolerance = 1e-6;
  settings.pressureSolver.preconditioner = algebra::PreconditionerType::Jacobi;
  settings.momentumSolver.maxIterations = 2000;
  settings.momentumSolver.absoluteTolerance = 1e-12;
  settings.robustness.linearSolverFallback.enabled = true;

  const pressure_velocity::SIMPLE simple(settings, 0, &model);
  // From REST, not from the uniform inflow.
  //
  // Initialising to Vector2{U_ref, 0} looks like a helpful head start and is a
  // trap: a uniform field satisfies continuity exactly and momentum almost
  // exactly away from the walls, so the initial residuals fall below tolerance
  // and SIMPLE returns after ZERO iterations. The first version of this file
  // did exactly that -- the "developed" profile it produced was the untouched
  // initial condition, which the delta/H guard caught only because it refuses
  // to fall back to the outlet profile.
  const auto result = simple.solve(mesh, fluid, velocity, pressure,
                                   fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                                   fields::ScalarField(mesh.numberOfCells(), 0.0));

  std::vector<Real> columnY(ny);
  for (Index j = 0; j < ny; ++j) columnY[j] = mesh.cell(j * nx).centroid().y;

  // The station is FOUND by scanning, not assumed from the flat-plate estimate
  // that sized the domain.
  Index station = nx;
  Real stationDelta = 0.0;
  Real stationEdge = 0.0;
  Real maximumDelta = 0.0;
  Real outletEdge = 0.0;
  for (Index i = 0; i < nx; ++i) {
    std::vector<Real> columnU(ny);
    for (Index j = 0; j < ny; ++j) columnU[j] = result.velocity[(j * nx) + i].x;
    const Real edge = columnU[ny / 2];  // duct centreline
    const Real delta = boundaryLayerThickness(columnY, columnU, edge);
    maximumDelta = std::max(maximumDelta, delta);
    outletEdge = edge;
    if (delta >= kTargetDeltaOverH * kStepHeight) {
      station = i;
      stationDelta = delta;
      stationEdge = edge;
      break;
    }
  }
  if (station == nx) {
    // The message carries the state, not just the verdict: "never reached" is
    // consistent with a domain that is too short AND with a solve that did not
    // run, and those need different fixes.
    throw std::runtime_error(
        "runPrecursor: the boundary layer never reached delta/H = " +
        std::to_string(kTargetDeltaOverH) + " within " + std::to_string(spec.lengthOverH) +
        " step heights. Diagnostic state: converged=" + std::to_string(result.converged() ? 1 : 0) +
        " status=" + std::to_string(static_cast<int>(result.status)) +
        " iterations=" + std::to_string(result.iterations) +
        " max delta/H=" + std::to_string(maximumDelta / kStepHeight) + " outlet centreline u=" +
        std::to_string(outletEdge) + " u_residual=" + std::to_string(result.finalUResidual) +
        ". Falling back to the outlet profile would substitute a different inflow state for "
        "the one the reference specifies; fix the cause rather than accepting what it produced.");
  }

  PrecursorProfile profile;
  profile.converged = result.converged();
  profile.iterations = result.iterations;
  profile.extractionStationOverH = mesh.cell(station).centroid().x / kStepHeight;
  profile.deltaOverH = stationDelta / kStepHeight;
  profile.edgeVelocity = stationEdge;
  profile.y = columnY;
  profile.u.resize(ny);
  profile.v.resize(ny);
  profile.k.resize(ny);
  profile.omega.resize(ny);

  const Real dy = kUpstreamChannelHeight / static_cast<Real>(ny);
  Real massFlow = 0.0;
  Real maximumRatio = 0.0;
  for (Index j = 0; j < ny; ++j) {
    const Index cell = (j * nx) + station;
    profile.u[j] = result.velocity[cell].x;
    profile.v[j] = result.velocity[cell].y;
    profile.k[j] = model.k()[cell];
    profile.omega[j] = model.omega()[cell];
    massFlow += kDensity * profile.u[j] * dy;
    maximumRatio = std::max(maximumRatio, model.turbulentViscosity()[cell] / kDynamicViscosity);
  }
  profile.massFlowPerDepth = massFlow;
  profile.maximumTurbulentViscosityRatio = maximumRatio;

  const Real dynamicHead = 0.5 * kDensity * kReferenceVelocity * kReferenceVelocity;
  profile.skinFriction = model.wallShearStress()[station] / dynamicHead;
  return profile;
}

Real sampleProfile(const std::vector<Real>& y, const std::vector<Real>& value, Real yStepCase) {
  // The duct's bottom wall becomes the STEP LIP, so the duct coordinate is the
  // step coordinate minus one step height.
  const Real yDuct = yStepCase - kStepHeight;
  if (yDuct <= y.front()) return value.front();
  if (yDuct >= y.back()) return value.back();
  for (std::size_t j = 1; j < y.size(); ++j) {
    if (yDuct <= y[j]) {
      const Real t = (yDuct - y[j - 1]) / (y[j] - y[j - 1]);
      return value[j - 1] + (t * (value[j] - value[j - 1]));
    }
  }
  return value.back();
}

Boundaries makeBoundariesWithProfile(const mesh::Mesh& mesh, const PrecursorProfile* profile) {
  // nullptr reproduces makeBoundaries EXACTLY. That is what makes the A/B
  // comparison a controlled one rather than two differently-built cases.
  if (profile == nullptr) return makeBoundaries(mesh);

  Boundaries boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const std::string& name = patch.name();
    if (name.rfind("inlet_", 0) == 0) {
      const Real y = mesh.face(patch.faceIds().front()).centroid().y;
      // Interpolated at the face centroid. The precursor is finer in y than the
      // step inlet on every mesh used here, so this and a face average differ
      // negligibly -- and the resulting mass flow is COMPARED against the
      // precursor's own rather than assumed equal.
      const Real u = sampleProfile(profile->y, profile->u, y);
      const Real v = sampleProfile(profile->y, profile->v, y);
      boundaries.velocity.set(mesh, name, std::make_unique<boundary::Inlet>(Vector2{u, v}));
      boundaries.pressure.set(mesh, name, std::make_unique<boundary::FixedGradient>(0.0));
    } else if (name == "right") {
      boundaries.velocity.set(mesh, name, std::make_unique<boundary::Outlet>());
      boundaries.pressure.set(mesh, name, std::make_unique<boundary::FixedValue>(0.0));
    } else {
      boundaries.velocity.set(mesh, name, std::make_unique<boundary::Wall>());
      boundaries.pressure.set(mesh, name, std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return boundaries;
}

ScalarInletProfiles mapScalarInlets(const mesh::Mesh& mesh, const PrecursorProfile& profile) {
  ScalarInletProfiles mapped;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name().rfind("inlet_", 0) != 0) continue;
    const Real y = mesh.face(patch.faceIds().front()).centroid().y;
    mapped.k.emplace_back(patch.name(), sampleProfile(profile.y, profile.k, y));
    mapped.omega.emplace_back(patch.name(), sampleProfile(profile.y, profile.omega, y));
  }
  return mapped;
}

}  // namespace cfd::validation::driver_seegmiller
