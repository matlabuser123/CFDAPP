#include "MMSCases.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/algebra/LinearSolverFallback.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/ThermalSolver.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

namespace cfd::test::mmscase {

using cfd::discretization::ConvectionScheme;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::validation::CellMask;
using cfd::validation::ErrorNorms;
using cfd::validation::MMSLevel;
using cfd::validation::MMSStudy;
using cfd::validation::NormKind;

namespace {

double seconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

Mesh makeMesh(Index n, bool distorted) {
  if (distorted) {
    return perFaceBoundaryMesh(
        createDistortedQuad2D(n, n, 1.0, 1.0, kDistortion / static_cast<Real>(n)));
  }
  return perFaceBoundaryMesh(n, n, 1.0, 1.0);
}

MMSLevel newLevel(const Mesh& mesh, Index n) {
  MMSLevel level;
  level.name = std::to_string(n) + "x" + std::to_string(n);
  level.nx = n;
  level.ny = n;
  level.cells = mesh.numberOfCells();
  level.h = cfd::validation::representativeGridSize(1.0, mesh.numberOfCells());
  return level;
}

bool allFinite(const ScalarField& f) {
  for (Index i = 0; i < f.size(); ++i) {
    if (!std::isfinite(f[i])) return false;
  }
  return true;
}
bool allFinite(const VectorField& f) {
  for (Index i = 0; i < f.size(); ++i) {
    if (!std::isfinite(f[i].x) || !std::isfinite(f[i].y)) return false;
  }
  return true;
}

std::string thermalStatusName(cfd::thermal::ThermalStatus status) {
  switch (status) {
    case cfd::thermal::ThermalStatus::Converged:
      return "Converged";
    case cfd::thermal::ThermalStatus::MaxIterations:
      return "MaxIterations";
    case cfd::thermal::ThermalStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case cfd::thermal::ThermalStatus::NonFiniteState:
      return "NonFiniteState";
    case cfd::thermal::ThermalStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

ScalarField component(const VectorField& field, bool x) {
  ScalarField out(field.size());
  for (Index i = 0; i < field.size(); ++i) out[i] = x ? field[i].x : field[i].y;
  return out;
}

// The exact face mass flux of the vortex velocity (divergence-free per cell).
SurfaceField exactVortexMassFlux(const Mesh& mesh) {
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces())
    flux[face.id()] = kDensity * mms::exactVortexVolumeFlux(face);
  return flux;
}

// Per-face value / (rho * area): a mass-flux error expressed as a normal
// velocity.
SurfaceField perUnitArea(const Mesh& mesh, const SurfaceField& massFlux) {
  SurfaceField out(mesh.numberOfFaces());
  for (const auto& face : mesh.faces())
    out[face.id()] = massFlux[face.id()] / (kDensity * face.area());
  return out;
}

void addVelocityErrors(MMSLevel& level, const Mesh& mesh, const VectorField& velocity) {
  const VectorField exact = mms::sampleVector(mesh, mms::velocity);
  const CellMask ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
  const CellMask interior = cfd::validation::invertMask(ring);
  const auto all = cfd::validation::computeVectorErrorNorms(mesh, velocity, exact);
  const auto in = cfd::validation::computeVectorErrorNorms(mesh, velocity, exact, &interior);
  const auto rg = cfd::validation::computeVectorErrorNorms(mesh, velocity, exact, &ring);
  level.errors.emplace_back("u", all.x);
  level.errors.emplace_back("v", all.y);
  level.errors.emplace_back("velocity", all.magnitude);
  level.errors.emplace_back("u_interior", in.x);
  level.errors.emplace_back("v_interior", in.y);
  level.errors.emplace_back("u_boundary_ring", rg.x);
  level.errors.emplace_back("v_boundary_ring", rg.y);
}

}  // namespace

std::string schemeName(ConvectionScheme scheme) {
  switch (scheme) {
    case ConvectionScheme::Upwind:
      return "upwind";
    case ConvectionScheme::Central:
      return "central";
    case ConvectionScheme::LinearUpwind:
      return "linear_upwind";
    case ConvectionScheme::QUICK:
      return "quick";
  }
  return "unknown";
}

std::string format(const char* fmt, Real value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), fmt, value);
  return buffer;
}

// --- Scalar ----------------------------------------------------------------------

MMSLevel runScalarLevel(Index n, const ScalarOptions& options) {
  const auto start = std::chrono::steady_clock::now();
  const Mesh mesh = makeMesh(n, options.distorted);
  MMSLevel level = newLevel(mesh, n);
  const auto boundaries = makeExactBoundaries(mesh, mms::scalar);
  const SurfaceField flux = mms::exactAdvectingMassFlux(mesh, kScalarDensity);
  // Analytical forcing, sampled at the cell centroids (never a discrete
  // operator applied to the exact field).
  const ScalarField source = mms::sampleScalar(mesh, [&](const Vector2& p) {
    return mms::scalarForcing(p, kScalarDensity, kScalarSpecificHeat, options.conductivity);
  });

  cfd::thermal::ThermalSolverSettings settings;
  // The equation is linear; the outer Picard loop only converges the
  // lagged non-orthogonal correction. Inner tolerances: absolute 1e-9 /
  // relative 1e-8 (||b|| ~ 0.4 at 128x128, so ~2.5e-9 relative): iterative
  // error ~3e-7, three orders below the discretization error (>= 4e-4).
  // Tighter absolute targets were measured to drive the warm-started
  // BiCGSTAB re-solves of the corrected distorted case into breakdown at
  // 128x128 (1.3e-9 -> 1.4e-10, then Breakdown) -- the documented
  // P12-NUM-004 limitation (absolute breakdown thresholds; the linear-solver
  // fallback is not wired into the thermal Picard loop).
  settings.linearSolver.absoluteTolerance = 1e-9;
  settings.linearSolver.relativeTolerance = 1e-8;
  settings.linearSolver.maxIterations = 20000;
  settings.tolerance = 1e-10;
  settings.maxIterations = 200;
  if (options.nonOrthogonalCorrection) {
    settings.nonOrthogonal.enabled = true;
    settings.nonOrthogonal.gradientScheme = GradientScheme::LeastSquares;
  }
  const auto result = cfd::thermal::ThermalSolver(settings).solve(
      mesh, ScalarField(mesh.numberOfCells(), 0.0), flux,
      cfd::thermal::ThermalProperties(options.conductivity, kScalarSpecificHeat), boundaries,
      source);

  level.solverStatus = thermalStatusName(result.status);
  level.iterations = result.iterations;
  level.massImbalance = std::abs(cfd::physics::evaluateContinuity(mesh, flux).globalNetFlux);
  level.accepted = result.converged() && allFinite(result.temperature);
  if (!level.accepted) {
    level.rejectionReason =
        "thermal status " + level.solverStatus + " at " + level.name + " after " +
        std::to_string(result.iterations) +
        " outer iterations (last linear solve: " + std::to_string(result.linearIterations) +
        " iterations, residual " + format("%.3e", result.initialResidual) + " -> " +
        format("%.3e", result.finalResidual) + ")";
    level.runtimeSeconds = seconds(start);
    return level;
  }
  const ScalarField exact = mms::sampleScalar(mesh, mms::scalar);
  const CellMask ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
  const CellMask interior = cfd::validation::invertMask(ring);
  level.errors.emplace_back("phi",
                            cfd::validation::computeErrorNorms(mesh, result.temperature, exact));
  level.errors.emplace_back("phi_interior", cfd::validation::computeErrorNorms(
                                                mesh, result.temperature, exact, &interior));
  level.errors.emplace_back("phi_boundary_ring", cfd::validation::computeErrorNorms(
                                                     mesh, result.temperature, exact, &ring));
  level.runtimeSeconds = seconds(start);
  return level;
}

// --- Momentum ----------------------------------------------------------------------

MMSLevel runMomentumLevel(Index n, const MomentumOptions& options) {
  const auto start = std::chrono::steady_clock::now();
  const Mesh mesh = makeMesh(n, options.distorted);
  MMSLevel level = newLevel(mesh, n);
  const auto velocityBoundaries = mms::makeExactVelocityBoundaries(mesh);
  const auto pressureBoundaries = mms::makeExactPressureBoundaries(mesh);
  const ScalarField pressure = mms::sampleScalar(mesh, mms::pressure);
  const VectorField force = mms::sampleVector(
      mesh, [](const Vector2& p) { return mms::momentumForcing(p, kDensity, kViscosity); });
  const ScalarField viscosity(mesh.numberOfCells(), kViscosity);
  const SurfaceField flux = exactVortexMassFlux(mesh);
  const GradientScheme gradientScheme =
      options.nonOrthogonalCorrection ? GradientScheme::LeastSquares : GradientScheme::GreenGauss;

  cfd::algebra::LinearSolverSettings linear;
  linear.absoluteTolerance = 1e-12;
  linear.relativeTolerance = 1e-10;
  linear.maxIterations = 20000;
  cfd::algebra::LinearSolverFallbackSettings fallback;
  fallback.enabled = true;
  const auto solver = cfd::algebra::makeLinearSolverWithFallback(linear, fallback);

  // Picard on the lagged terms (deferred higher-order convection
  // correction, non-orthogonal correction): converged when the max velocity
  // change is below 1e-10 -- iterative error ~1e-10, far below the
  // discretization error.
  VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  std::string status = "MaxIterations";
  Index iteration = 0;
  constexpr Index kMaxPicard = 200;
  for (; iteration < kMaxPicard; ++iteration) {
    const ScalarField previousU = component(velocity, true);
    const ScalarField previousV = component(velocity, false);
    const auto assemble = [&](cfd::physics::VelocityComponent c, const ScalarField& previous) {
      return cfd::pressure_velocity::assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, flux, viscosity, velocityBoundaries, pressureBoundaries, c,
          previous, /*alpha=*/1.0, nullptr, nullptr, options.scheme, gradientScheme,
          options.nonOrthogonalCorrection, nullptr, &force);
    };
    const auto u = assemble(cfd::physics::VelocityComponent::U, previousU);
    const auto v = assemble(cfd::physics::VelocityComponent::V, previousV);
    cfd::algebra::Vector guessU(mesh.numberOfCells());
    cfd::algebra::Vector guessV(mesh.numberOfCells());
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      guessU[i] = previousU[i];
      guessV[i] = previousV[i];
    }
    const auto su = solver->solve(u.system, guessU);
    const auto sv = solver->solve(v.system, guessV);
    if (!su.converged() || !sv.converged()) {
      status = "LinearSolveFailure";
      break;
    }
    Real change = 0.0;
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      change = std::max({change, std::abs(su.solution[i] - velocity[i].x),
                         std::abs(sv.solution[i] - velocity[i].y)});
      velocity[i] = Vector2{su.solution[i], sv.solution[i]};
    }
    if (iteration > 0 && change < 1e-10) {
      status = "Converged";
      ++iteration;
      break;
    }
  }
  level.solverStatus = status;
  level.iterations = iteration;
  level.massImbalance = std::abs(cfd::physics::evaluateContinuity(mesh, flux).globalNetFlux);
  level.accepted = status == "Converged" && allFinite(velocity);
  if (!level.accepted) {
    level.rejectionReason = "momentum Picard status " + status;
    level.runtimeSeconds = seconds(start);
    return level;
  }
  addVelocityErrors(level, mesh, velocity);
  level.runtimeSeconds = seconds(start);
  return level;
}

// --- SIMPLE ------------------------------------------------------------------------

namespace {

cfd::pressure_velocity::SIMPLESettings simpleSettings(const SimpleOptions& options) {
  cfd::pressure_velocity::SIMPLESettings settings;
  settings.maxIterations = 40000;
  // (0.8, 0.4): measured the fastest stable pair of those tried on this
  // problem (0.7/0.3: 1740 outer iterations at 16x16; 0.8/0.4: 1494;
  // 0.9/0.3 diverges). The converged discrete solution does not depend on
  // it (identical errors to 5 digits, results/p12-num-006/summary.md).
  settings.velocityRelaxation = 0.8;
  settings.pressureRelaxation = 0.4;
  // Absolute outer tolerance 1e-8 on every residual: the converged errors
  // agree with a 1e-10 run to 5 significant digits (iterative error
  // << discretization error) -- summary.md.
  settings.velocityTolerance = options.tolerance;
  settings.pressureTolerance = options.tolerance;
  settings.continuityTolerance = options.tolerance;
  settings.convectionScheme = options.scheme;
  // Inner solves only need to make progress (the outer residuals are the
  // initial residuals of freshly assembled systems, independent of inner
  // accuracy): relative 1e-3, absolute 1e-12 (keeps BiCGSTAB off its
  // absolute breakdown threshold near convergence).
  settings.momentumSolver.absoluteTolerance = 1e-12;
  settings.momentumSolver.relativeTolerance = 1e-3;
  settings.momentumSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-12;
  settings.pressureSolver.relativeTolerance = 1e-3;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  settings.robustness.linearSolverFallback.enabled = true;
  if (options.distorted) {
    settings.nonOrthogonalCorrections = 2;
    settings.gradientScheme = GradientScheme::LeastSquares;
  }
  return settings;
}

struct SimpleRun {
  Mesh mesh;
  cfd::pressure_velocity::SIMPLEResult result;
};

SimpleRun solveSimple(Index n, const SimpleOptions& options, Real initialVelocityScale,
                      Real initialPressure) {
  Mesh mesh = makeMesh(n, options.distorted);
  const auto velocityBoundaries = mms::makeExactVelocityBoundaries(mesh);
  const auto pressureBoundaries = mms::makeExactPressureBoundaries(mesh);
  const VectorField force = mms::sampleVector(
      mesh, [](const Vector2& p) { return mms::momentumForcing(p, kDensity, kViscosity); });
  cfd::pressure_velocity::SIMPLE simple(simpleSettings(options), /*referenceCell=*/0);
  simple.setMomentumSource(&force);
  // Initial state: a multiple of the exact velocity (0 = at rest) and a
  // uniform pressure -- never the manufactured solution itself.
  VectorField initialVelocity = mms::sampleVector(mesh, mms::velocity);
  for (Index i = 0; i < initialVelocity.size(); ++i)
    initialVelocity[i] = initialVelocity[i] * initialVelocityScale;
  auto result = simple.solve(mesh, cfd::physics::FluidProperties(kDensity, kViscosity),
                             velocityBoundaries, pressureBoundaries, std::move(initialVelocity),
                             ScalarField(mesh.numberOfCells(), initialPressure));
  return SimpleRun{std::move(mesh), std::move(result)};
}

}  // namespace

MMSLevel runSimpleLevel(Index n, const SimpleOptions& options) {
  const auto start = std::chrono::steady_clock::now();
  const SimpleRun run = solveSimple(n, options, 0.0, 0.0);
  const Mesh& mesh = run.mesh;
  const auto& r = run.result;
  MMSLevel level = newLevel(mesh, n);
  level.solverStatus = std::string(cfd::validation::simpleStatusName(r.status));
  level.iterations = r.iterations;
  level.massImbalance = r.globalMassImbalance;
  // The NUM-005 solver-status gate: Converged, finite fields, global mass
  // imbalance <= 1e-10.
  const auto gate = cfd::validation::assessSimpleSolve(r, 1e-10);
  level.accepted = gate.accepted;
  level.rejectionReason = gate.reason;
  level.diagnostics = {{"global_mass_imbalance", r.globalMassImbalance},
                       {"final_u_residual", r.finalUResidual},
                       {"final_v_residual", r.finalVResidual},
                       {"final_pressure_residual", r.finalPressureResidual},
                       {"final_continuity_residual", r.finalContinuityResidual}};
  if (!level.accepted) {
    level.runtimeSeconds = seconds(start);
    return level;
  }
  addVelocityErrors(level, mesh, r.velocity);
  const ScalarField exactP = mms::sampleScalar(mesh, mms::pressure);
  const CellMask ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
  const CellMask interior = cfd::validation::invertMask(ring);
  level.errors.emplace_back(
      "p", cfd::validation::computeGaugeInvariantErrorNorms(mesh, r.pressure, exactP));
  level.errors.emplace_back("p_interior", cfd::validation::computeGaugeInvariantErrorNorms(
                                              mesh, r.pressure, exactP, &interior));
  level.errors.emplace_back("p_boundary_ring", cfd::validation::computeGaugeInvariantErrorNorms(
                                                   mesh, r.pressure, exactP, &ring));
  // Continuity of the converged face flux, per unit cell volume.
  const auto continuity = cfd::physics::evaluateContinuity(mesh, r.massFlux);
  ScalarField divergence(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    divergence[cell.id()] = continuity.cellImbalance[cell.id()] / (kDensity * cell.volume());
  }
  level.errors.emplace_back("continuity", cfd::validation::computeErrorNorms(mesh, divergence));
  // Face flux against the exact face-integrated flux, as a normal velocity.
  level.errors.emplace_back("face_flux", cfd::validation::computeFaceErrorNorms(
                                             mesh, perUnitArea(mesh, r.massFlux),
                                             perUnitArea(mesh, exactVortexMassFlux(mesh))));
  level.runtimeSeconds = seconds(start);
  return level;
}

SimpleFields runSimpleFields(Index n, const SimpleOptions& options, Real initialVelocityScale,
                             Real initialPressure) {
  const SimpleRun run = solveSimple(n, options, initialVelocityScale, initialPressure);
  SimpleFields fields;
  for (Index i = 0; i < run.mesh.numberOfCells(); ++i) {
    fields.u.push_back(run.result.velocity[i].x);
    fields.v.push_back(run.result.velocity[i].y);
    fields.p.push_back(run.result.pressure[i]);
  }
  fields.status = std::string(cfd::validation::simpleStatusName(run.result.status));
  fields.iterations = run.result.iterations;
  return fields;
}

// --- CompressibleSIMPLE ---------------------------------------------------------------

namespace {

std::string compressibleStatusName(cfd::compressible::CompressibleSIMPLEStatus status) {
  using S = cfd::compressible::CompressibleSIMPLEStatus;
  switch (status) {
    case S::Converged:
      return "Converged";
    case S::MaxIterations:
      return "MaxIterations";
    case S::MomentumFailure:
      return "MomentumFailure";
    case S::PressureCorrectionFailure:
      return "PressureCorrectionFailure";
    case S::NonFiniteState:
      return "NonFiniteState";
    case S::InvalidConfiguration:
      return "InvalidConfiguration";
    case S::Stagnated:
      return "Stagnated";
    case S::Diverging:
      return "Diverging";
  }
  return "Unknown";
}

}  // namespace

MMSLevel runCompressibleLevel(Index n) {
  const auto start = std::chrono::steady_clock::now();
  const Mesh mesh = makeMesh(n, false);
  MMSLevel level = newLevel(mesh, n);
  const Real pRef = kCompressibleReferencePressure;
  const Real rt = kGasConstant * kIsothermalTemperature;
  const cfd::compressible::ThermodynamicProperties thermodynamics(kGasConstant, 1005.0);

  // Exact velocity U = m / rho on every boundary face (zero normal
  // component: m . n = 0 on the walls), exact Neumann gauge-pressure data.
  cfd::boundary::BoundaryConditionSet velocityBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& face = mesh.face(patch.faceIds().front());
    velocityBoundaries.set(mesh, patch.name(),
                           std::make_unique<cfd::boundary::Inlet>(
                               mms::compressibleVelocity(face.centroid(), pRef, rt).value));
  }
  const auto pressureBoundaries = mms::makeExactPressureBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells(), kIsothermalTemperature);
  const VectorField force = mms::sampleVector(mesh, [&](const Vector2& p) {
    return mms::compressibleMomentumForcing(p, pRef, rt, kViscosity);
  });

  cfd::compressible::CompressibleSIMPLESettings settings;
  settings.maxIterations = 40000;
  // The CompressibleSIMPLE defaults (0.7 / 0.3, pseudo time step 1): the
  // incompressible study's faster (0.8, 0.4) pair was measured to diverge
  // here (P_ref = 5, 16x16), while 0.7 / 0.3 converges.
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-8;
  settings.pressureTolerance = 1e-8;
  settings.continuityTolerance = 1e-8;
  settings.momentumSolver.absoluteTolerance = 1e-12;
  settings.momentumSolver.relativeTolerance = 1e-3;
  settings.momentumSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-12;
  settings.pressureSolver.relativeTolerance = 1e-3;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  settings.robustness.linearSolverFallback.enabled = true;
  constexpr Index kReferenceCell = 0;
  cfd::compressible::CompressibleSIMPLE solver(settings, thermodynamics, pRef, kReferenceCell);
  solver.setMomentumSource(&force);

  // The single pressure-level datum (see the header comment).
  const Real pressureDatum = mms::pressure(mesh.cell(kReferenceCell).centroid());
  const Real initialDensity = thermodynamics.density(pRef + pressureDatum, kIsothermalTemperature);
  const auto r = solver.solve(mesh, kViscosity, velocityBoundaries, pressureBoundaries, temperature,
                              nullptr, VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                              ScalarField(mesh.numberOfCells(), pressureDatum),
                              ScalarField(mesh.numberOfCells(), initialDensity));

  level.solverStatus = compressibleStatusName(r.status);
  level.iterations = r.iterations;
  level.massImbalance = r.globalMassImbalance;
  level.accepted = r.converged() && allFinite(r.velocity) && allFinite(r.pressure) &&
                   allFinite(r.density) && std::isfinite(r.globalMassImbalance) &&
                   r.globalMassImbalance <= 1e-10;
  if (!level.accepted) {
    level.rejectionReason = "CompressibleSIMPLE status " + level.solverStatus +
                            format(", global mass imbalance %.3e", r.globalMassImbalance);
    level.runtimeSeconds = seconds(start);
    return level;
  }
  const VectorField exactU = mms::sampleVector(
      mesh, [&](const Vector2& p) { return mms::compressibleVelocity(p, pRef, rt).value; });
  const auto velocityErrors = cfd::validation::computeVectorErrorNorms(mesh, r.velocity, exactU);
  level.errors.emplace_back("u", velocityErrors.x);
  level.errors.emplace_back("v", velocityErrors.y);
  level.errors.emplace_back("velocity", velocityErrors.magnitude);
  const ScalarField exactP = mms::sampleScalar(mesh, mms::pressure);
  level.errors.emplace_back("p", cfd::validation::computeErrorNorms(mesh, r.pressure, exactP));
  level.errors.emplace_back(
      "p_gauge", cfd::validation::computeGaugeInvariantErrorNorms(mesh, r.pressure, exactP));
  const ScalarField exactRho = mms::sampleScalar(
      mesh, [&](const Vector2& p) { return mms::compressibleDensity(p, pRef, rt); });
  level.errors.emplace_back("density",
                            cfd::validation::computeErrorNorms(mesh, r.density, exactRho));
  const auto continuity = cfd::physics::evaluateContinuity(mesh, r.massFlux);
  ScalarField divergence(mesh.numberOfCells());
  Real eosDeviation = 0.0;
  for (const auto& cell : mesh.cells()) {
    divergence[cell.id()] = continuity.cellImbalance[cell.id()] / cell.volume();
    const Real eos = thermodynamics.density(pRef + r.pressure[cell.id()], kIsothermalTemperature);
    eosDeviation = std::max(eosDeviation, std::abs(r.density[cell.id()] - eos) / eos);
  }
  level.errors.emplace_back("continuity", cfd::validation::computeErrorNorms(mesh, divergence));
  SurfaceField exactMassFlux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) exactMassFlux[face.id()] = mms::exactVortexVolumeFlux(face);
  SurfaceField numericPerArea(mesh.numberOfFaces());
  SurfaceField exactPerArea(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    numericPerArea[face.id()] = r.massFlux[face.id()] / face.area();
    exactPerArea[face.id()] = exactMassFlux[face.id()] / face.area();
  }
  level.errors.emplace_back(
      "mass_flux", cfd::validation::computeFaceErrorNorms(mesh, numericPerArea, exactPerArea));
  level.diagnostics = {{"global_mass_imbalance", r.globalMassImbalance},
                       {"eos_max_relative_deviation", eosDeviation},
                       {"pressure_datum", pressureDatum}};
  level.runtimeSeconds = seconds(start);
  return level;
}

// --- Study assembly ------------------------------------------------------------------

const cfd::validation::MMSOrder& addOrder(MMSStudy& study, const std::string& quantity,
                                          NormKind norm, Real formalOrder) {
  cfd::validation::GridConvergenceOptions options;
  options.formalOrder = formalOrder;
  study.orders.push_back(cfd::validation::computeMMSOrder(study, quantity, norm, options));
  return study.orders.back();
}

bool addGate(MMSStudy& study, const std::string& name, bool passed, const std::string& detail) {
  study.gates.push_back({name, passed, detail});
  return passed;
}

bool allLevelsAccepted(const MMSStudy& study) {
  if (study.levels.empty()) return false;
  for (const auto& level : study.levels) {
    if (!level.accepted) return false;
  }
  return true;
}

bool monotonicallyDecreasing(const MMSStudy& study, const std::string& quantity, NormKind norm) {
  for (std::size_t k = 0; k + 1 < study.levels.size(); ++k) {
    const ErrorNorms& coarse = study.error(k, quantity);
    const ErrorNorms& fine = study.error(k + 1, quantity);
    const auto value = [&](const ErrorNorms& e) {
      return norm == NormKind::L1 ? e.l1 : (norm == NormKind::L2 ? e.l2 : e.linf);
    };
    if (!(value(fine) < value(coarse))) return false;
  }
  return true;
}

bool addOrderGate(MMSStudy& study, const std::string& category, const std::string& quantity,
                  NormKind norm, Real formalOrder, Real lo, Real hi) {
  const auto& order = addOrder(study, quantity, norm, formalOrder);
  const std::optional<Real> p = order.finestOrder();
  const bool passed = p.has_value() && *p >= lo && *p <= hi;
  std::string detail =
      "finest-triplet observed order " + (p ? format("%.3f", *p) : std::string("--")) + " (" +
      std::string(cfd::validation::gridConvergenceStatusName(order.triplets.back().status)) +
      " vs formal " + format("%.0f", formalOrder) + "); reduction factors";
  for (const auto& f : order.reductionFactors)
    detail += " " + (f ? format("%.3f", *f) : std::string("--"));
  return addGate(study,
                 category + ": " + quantity + " " +
                     std::string(cfd::validation::normKindName(norm)) + " observed order in [" +
                     format("%.2f", lo) + ", " + format("%.2f", hi) + "]",
                 passed, detail);
}

bool addDecreaseGate(MMSStudy& study, const std::string& category, const std::string& quantity) {
  const bool passed = monotonicallyDecreasing(study, quantity, NormKind::L1) &&
                      monotonicallyDecreasing(study, quantity, NormKind::L2) &&
                      monotonicallyDecreasing(study, quantity, NormKind::Linf);
  return addGate(study, category + ": " + quantity + " L1/L2/Linf decrease on every refinement",
                 passed, passed ? "monotone" : "not monotone");
}

bool categoryPassed(const MMSStudy& study, const std::string& category, std::string* failures) {
  const std::string prefix = category + ":";
  bool any = false;
  bool all = true;
  for (const auto& gate : study.gates) {
    if (gate.name.rfind(prefix, 0) != 0) continue;
    any = true;
    if (!gate.passed) {
      all = false;
      if (failures != nullptr) *failures += gate.name + " -- " + gate.detail + "\n";
    }
  }
  if (!any && failures != nullptr) *failures += "no gate in category " + category + "\n";
  return any && all;
}

void writeStudy(const MMSStudy& study, const std::string& file) {
  const std::string base = "results/validation/mms/" + file;
  cfd::validation::writeMMSReport(base + ".json", study);
  {
    std::ofstream md(base + ".md");
    md << cfd::validation::mmsReportMarkdown(study);
  }
  std::printf("\n%s", cfd::validation::mmsReportMarkdown(study).c_str());
  std::fflush(stdout);
}

}  // namespace cfd::test::mmscase
