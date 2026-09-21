// GPU-PIPE-001 Final Residency, Part 4 -- CPU production vs GPU production.
//
// The GPU arm is the FULL production path: device discretization, device
// BiCGSTAB for momentum AND pressure, resident SIMPLE loop. The CPU arm is the
// reference: host discretization, host BiCGSTAB. Identical meshes, boundary
// conditions, initial state, material properties, relaxation, convection
// scheme, convergence criteria, solver tolerances and iteration budget. No
// weakened GPU settings -- `settings()` below is built once and only the
// backend flags change.
//
// WHAT THE ACCEPTANCE CRITERIA ARE, AND WHERE THEY COME FROM.
//
// This axis compares two DIFFERENT linear solvers. A GPU block reduction and a
// host sequential dot product sum in different orders, so the residual
// histories are expected to differ -- the repository's own CPU/GPU production
// test says so in as many words:
//
//   tests/solver/simple/test_simple_gpu_solver.cpp
//     "CPU and GPU solve the same discretized system to the same linear-solver
//      tolerances but via a different floating-point summation order -- some
//      divergence is expected and acceptable, not a defect"
//     EXPECT_LT(maxVelocityError, 1e-6)
//     EXPECT_LT(maxPressureError, 1e-6)
//     EXPECT_NEAR(gpuResult.massFlux[boundaryFace], 0.0, 1e-8)
//
// Those are the bounds this gate uses. They are ADOPTED, not invented, and
// nothing here is loosened. Bitwise equality on this axis is neither required
// nor claimed; it IS required on the residency axis, which
// resident_loop_comparison.cpp gates separately.
//
// A case is an ACCEPTANCE result only when BOTH arms converge. Comparing
// unconverged transients against a bound calibrated on converged solutions
// proves nothing.
//
// When the GPU arm does NOT converge, a third arm runs: the GPU production path
// with the resident loop declined. That answers the only question that matters
// there -- "is this the behaviour that was already present, or did residency
// cause it" -- instead of leaving it as an unexplained failure.
//
// usage: production_equivalence [--quick|--full]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// tests/solver/simple/test_simple_gpu_solver.cpp, verbatim.
constexpr Real kVelocityBound = 1e-6;
constexpr Real kPressureBound = 1e-6;
constexpr Real kBoundaryFluxBound = 1e-8;

int failures = 0;
int cases = 0;
int acceptanceCases = 0;
int twoD = 0, threeD = 0, pinned = 0, openBoundary = 0, nonUpwind = 0, longRun = 0;
int determinismChecks = 0;
int classifiedKnown = 0;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// Numerically identical to LaminarModel, named differently, so the resident
// SIMPLE loop declines. Used only to CLASSIFY a non-converging GPU case.
class InertModel final : public cfd::turbulence::TurbulenceModel {
 public:
  explicit InertModel(const Mesh& mesh) : turbulentViscosity_(mesh.numberOfCells(), 0.0) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "inert-not-laminar"; }
  [[nodiscard]] const ScalarField& turbulentViscosity() const override {
    return turbulentViscosity_;
  }
  void correct(const Mesh&, const VectorField&, const ScalarField&) override {}

 private:
  ScalarField turbulentViscosity_;
};

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
  SIMPLESettings settings;
  bool threeDimensional = false;
  bool open = false;
};

// ONE settings object per case. The arms change `enableGpuDiscretization` and
// the two solver backends and nothing else.
SIMPLESettings settings(Index outer) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  return s;
}

Case cavity(std::string name, Mesh mesh, Index outer,
            ConvectionScheme scheme = ConvectionScheme::Upwind) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01},
         settings(outer), false, false};
  const auto& m = c.mesh;
  c.threeDimensional = m.dimension() == 3;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    c.vb.set(m, patch.name(),
             lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
                 : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::Wall>()));
    c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  c.settings.convectionScheme = scheme;
  return c;
}

Case inletOutlet(std::string name, Mesh mesh, Index outer,
                 ConvectionScheme scheme = ConvectionScheme::Upwind) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01},
         settings(outer), false, true};
  const auto& m = c.mesh;
  c.threeDimensional = m.dimension() == 3;
  const std::size_t patchCount = m.boundaryPatches().size();
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    if (i == 0) {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{1.0, 0.0, 0.0}));
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (i + 1 == patchCount) {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Outlet>());
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Wall>());
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  for (Index cell = 0; cell < m.numberOfCells(); ++cell) c.velocity[cell] = Vector3{1.0, 0.0, 0.0};
  c.settings.convectionScheme = scheme;
  return c;
}

enum class Arm { Cpu, GpuResident, GpuNonResident };

// `overrideSettings` exists because a Case cannot be copied -- BoundaryConditionSet
// is move-only by design -- so a variant run has to vary the settings, not the case.
SIMPLEResult run(const Case& c, Arm arm, const SIMPLESettings* overrideSettings = nullptr) {
  SIMPLESettings s = overrideSettings != nullptr ? *overrideSettings : c.settings;
  const bool gpu = arm != Arm::Cpu;
  s.enableGpuDiscretization = gpu;
  s.momentumSolver.backend = gpu ? LinearSolverBackend::GPU : LinearSolverBackend::CPU;
  s.pressureSolver.backend = gpu ? LinearSolverBackend::GPU : LinearSolverBackend::CPU;
  InertModel inert(c.mesh);
  const SIMPLE simple(s, /*referenceCell=*/0,
                      arm == Arm::GpuNonResident ? &inert : nullptr);
  return simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
}

struct Metric {
  std::size_t compared = 0;
  Real linf = 0.0, l2 = 0.0;
  void note(Real a, Real b) {
    ++compared;
    const Real d = std::abs(a - b);
    linf = std::max(linf, d);
    l2 += d * d;
  }
  void finish() { l2 = compared == 0 ? 0.0 : std::sqrt(l2 / static_cast<Real>(compared)); }
};

const char* statusName(SIMPLEStatus s) {
  switch (s) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::Diverging: return "Diverging";
    case SIMPLEStatus::Stagnated: return "Stagnated";
    case SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration: return "InvalidConfiguration";
    case SIMPLEStatus::Cancelled: return "Cancelled";
  }
  return "?";
}

Real maxBoundaryFlux(const Mesh& mesh, const SurfaceField& flux) {
  Real worst = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) worst = std::max(worst, std::abs(flux[faceId]));
  }
  return worst;
}

// Per-cell continuity imbalance, computed here rather than read out of the
// result, so the two arms are measured by one instrument.
Real continuityRms(const Mesh& mesh, const SurfaceField& flux) {
  Real sum = 0.0;
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    Real cell = 0.0;
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      cell += (face.owner() == c) ? flux[faceId] : -flux[faceId];
    }
    sum += cell * cell;
  }
  return std::sqrt(sum / static_cast<Real>(mesh.numberOfCells()));
}

void reportHistory(const char* label, const std::vector<Real>& a, const std::vector<Real>& b) {
  if (a.empty() || b.empty()) return;
  const std::size_t n = std::min(a.size(), b.size());
  Real worst = 0.0;
  std::size_t firstDiffering = 0;
  for (std::size_t i = 0; i < n; ++i) {
    worst = std::max(worst, std::abs(a[i] - b[i]));
    if (firstDiffering == 0 && !sameBits(a[i], b[i])) firstDiffering = i + 1;
  }
  std::printf("        history %-18s compared %3zu   max |cpu-gpu| %9.3g   first differing "
              "iteration %s\n",
              label, n, worst,
              firstDiffering == 0 ? "none (bitwise)" : std::to_string(firstDiffering).c_str());
}

void runCase(const Case& c, bool determinism) {
  if (c.threeDimensional) ++threeD; else ++twoD;
  if (c.open) ++openBoundary; else ++pinned;
  if (c.settings.convectionScheme != ConvectionScheme::Upwind) ++nonUpwind;

  const SIMPLEResult cpu = run(c, Arm::Cpu);
  const SIMPLEResult gpu = run(c, Arm::GpuResident);
  if (cpu.iterations >= 50 || gpu.iterations >= 50) ++longRun;
  ++cases;

  const bool bothConverged =
      cpu.status == SIMPLEStatus::Converged && gpu.status == SIMPLEStatus::Converged;

  std::printf("  %-26s cpu[%s it=%lld mom=%lld p=%lld]  gpu[%s it=%lld mom=%lld p=%lld "
              "disc=%d resident=%d]\n",
              c.name.c_str(), statusName(cpu.status), static_cast<long long>(cpu.iterations),
              static_cast<long long>(cpu.momentumLinearIterations),
              static_cast<long long>(cpu.pressureLinearIterations), statusName(gpu.status),
              static_cast<long long>(gpu.iterations),
              static_cast<long long>(gpu.momentumLinearIterations),
              static_cast<long long>(gpu.pressureLinearIterations),
              static_cast<int>(gpu.gpuDiscretization), static_cast<int>(gpu.residentSimpleLoop));

  // --- the GPU arm must actually have been the production GPU path -------
  if (!gpu.gpuDiscretization || !gpu.residentSimpleLoop || !gpu.residentPressureSolve) {
    ++failures;
    std::printf("  FAIL %-24s the GPU arm did not take the resident production path "
                "(disc=%d loop=%d pressure=%d) -- this comparison would be vacuous\n",
                c.name.c_str(), static_cast<int>(gpu.gpuDiscretization),
                static_cast<int>(gpu.residentSimpleLoop),
                static_cast<int>(gpu.residentPressureSolve));
    return;
  }

  // --- classification when the GPU arm did not converge ------------------
  if (!bothConverged) {
    const SIMPLEResult before = run(c, Arm::GpuNonResident);
    const bool unchanged = before.status == gpu.status && before.iterations == gpu.iterations;
    if (unchanged) ++classifiedKnown; else ++failures;
    std::printf("  %s %-24s NOT an acceptance case (cpu=%s gpu=%s). Pre-residency GPU arm: "
                "%s it=%lld -- %s\n",
                unchanged ? "NOTE" : "FAIL", c.name.c_str(), statusName(cpu.status),
                statusName(gpu.status), statusName(before.status),
                static_cast<long long>(before.iterations),
                unchanged ? "IDENTICAL to the pre-residency path; residency changed nothing"
                          : "DIFFERENT from the pre-residency path -- residency CHANGED this");
    return;
  }

  ++acceptanceCases;
  Metric pressure, u, v, w, flux;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    pressure.note(cpu.pressure[i], gpu.pressure[i]);
    u.note(cpu.velocity[i].x, gpu.velocity[i].x);
    v.note(cpu.velocity[i].y, gpu.velocity[i].y);
    w.note(cpu.velocity[i].z, gpu.velocity[i].z);
  }
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) flux.note(cpu.massFlux[f], gpu.massFlux[f]);
  pressure.finish(); u.finish(); v.finish(); w.finish(); flux.finish();

  const Real maxVelocity = std::max({u.linf, v.linf, w.linf});
  const Real gpuBoundaryFlux = maxBoundaryFlux(c.mesh, gpu.massFlux);
  const Real cpuBoundaryFlux = maxBoundaryFlux(c.mesh, cpu.massFlux);
  const Real gpuContinuity = continuityRms(c.mesh, gpu.massFlux);
  const Real cpuContinuity = continuityRms(c.mesh, cpu.massFlux);

  const bool withinBounds = maxVelocity < kVelocityBound && pressure.linf < kPressureBound;
  // The GPU solution must be physically valid in its own right, not merely
  // close to the CPU one -- the wall-flux bound the repository's test applies.
  const bool physical = c.open || gpuBoundaryFlux < kBoundaryFluxBound;
  // Both arms must satisfy the SAME production convergence criteria.
  const bool sameCriteria = gpu.finalUResidual <= c.settings.velocityTolerance &&
                            gpu.finalVResidual <= c.settings.velocityTolerance &&
                            gpu.finalPressureResidual <= c.settings.pressureTolerance &&
                            gpu.finalContinuityResidual <= c.settings.continuityTolerance &&
                            cpu.finalUResidual <= c.settings.velocityTolerance &&
                            cpu.finalPressureResidual <= c.settings.pressureTolerance;
  const bool ok = withinBounds && physical && sameCriteria;
  if (!ok) ++failures;

  std::printf("  %s  %-24s p[Linf=%.3g L2=%.3g] u[Linf=%.3g L2=%.3g] v[Linf=%.3g L2=%.3g] "
              "w[Linf=%.3g] flux[Linf=%.3g L2=%.3g]\n",
              ok ? "PASS" : "FAIL", c.name.c_str(), pressure.linf, pressure.l2, u.linf, u.l2,
              v.linf, v.l2, w.linf, flux.linf, flux.l2);
  std::printf("        mass imbalance cpu %.3g gpu %.3g | continuity rms cpu %.3g gpu %.3g | "
              "boundary flux cpu %.3g gpu %.3g (bound %.0e)\n",
              cpu.globalMassImbalance, gpu.globalMassImbalance, cpuContinuity, gpuContinuity,
              cpuBoundaryFlux, gpuBoundaryFlux, kBoundaryFluxBound);
  std::printf("        outer iterations cpu %lld gpu %lld   bounds velocity %.0e pressure %.0e\n",
              static_cast<long long>(cpu.iterations), static_cast<long long>(gpu.iterations),
              kVelocityBound, kPressureBound);
  reportHistory("u residual", cpu.uResidualHistory, gpu.uResidualHistory);
  reportHistory("v residual", cpu.vResidualHistory, gpu.vResidualHistory);
  if (c.threeDimensional) reportHistory("w residual", cpu.wResidualHistory, gpu.wResidualHistory);
  reportHistory("pressure residual", cpu.pressureResidualHistory, gpu.pressureResidualHistory);
  reportHistory("continuity", cpu.continuityHistory, gpu.continuityHistory);

  if (determinism) {
    const SIMPLEResult cpu2 = run(c, Arm::Cpu);
    const SIMPLEResult gpu2 = run(c, Arm::GpuResident);
    std::size_t errors = 0;
    if (cpu.iterations != cpu2.iterations || gpu.iterations != gpu2.iterations) ++errors;
    if (cpu.status != cpu2.status || gpu.status != gpu2.status) ++errors;
    if (cpu.uResidualHistory != cpu2.uResidualHistory) ++errors;
    if (gpu.uResidualHistory != gpu2.uResidualHistory) ++errors;
    if (gpu.pressureResidualHistory != gpu2.pressureResidualHistory) ++errors;
    if (gpu.continuityHistory != gpu2.continuityHistory) ++errors;
    for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
      if (!sameBits(cpu.pressure[i], cpu2.pressure[i])) ++errors;
      if (!sameBits(gpu.pressure[i], gpu2.pressure[i])) ++errors;
      if (!sameBits(gpu.velocity[i].x, gpu2.velocity[i].x)) ++errors;
      if (!sameBits(gpu.velocity[i].y, gpu2.velocity[i].y)) ++errors;
      if (!sameBits(gpu.velocity[i].z, gpu2.velocity[i].z)) ++errors;
    }
    for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
      if (!sameBits(gpu.massFlux[f], gpu2.massFlux[f])) ++errors;
    }
    ++cases;
    ++determinismChecks;
    if (errors != 0) ++failures;
    std::printf("  %s  %-24s determinism: each arm repeated -- status, iteration count, residual "
                "histories and final fields bitwise identical (errors=%zu)\n",
                errors == 0 ? "PASS" : "FAIL", c.name.c_str(), errors);
  }
}

// The representative production ladder, 160^2 / 320^2 / 640^2.
//
// These cannot be acceptance cases the way the small ones are, and pretending
// otherwise would be the error this project calls out by name: converging a
// 640^2 cavity to 1e-6 on the CPU takes hours, so the comparison has to be made
// at a FIXED outer budget -- which means comparing two unconverged transients.
// A transient difference measured against a bound calibrated on CONVERGED
// solutions means nothing.
//
// So at these grids the acceptance criterion is the one that IS meaningful:
// the resident GPU arm must be BITWISE identical to the pre-residency GPU arm
// after the same number of outer iterations. That is the property this
// milestone changed and is therefore responsible for. The CPU/GPU transient
// difference is REPORTED alongside it, as a cross-solver observation, never as
// a pass/fail.
void ladderCase(const Case& c, Index budget) {
  SIMPLESettings fixed = c.settings;
  fixed.maxIterations = budget;
  // ADOPTED VERBATIM from performance_benchmark.cpp's benchmarkSettings(), so
  // that the claim "the two instruments describe the same runs" is true rather
  // than merely intended. The first version of this function kept this
  // harness's own Jacobi-preconditioned settings and only matched the BUDGETS;
  // at 640^2 the pressure correction then failed on iteration 0 on BOTH arms,
  // and the comparison passed with "0 differing values" because it was
  // comparing two untouched initial states. That result is preserved at
  // equivalence/production-ladder-VACUOUS-at-640.log. Identical failures are
  // not evidence of identical behaviour.
  fixed.velocityTolerance = 1e-10;
  fixed.pressureTolerance = 1e-10;
  fixed.continuityTolerance = 1e-10;
  fixed.momentumSolver.maxIterations = 1000;
  fixed.momentumSolver.absoluteTolerance = 1e-8;
  fixed.momentumSolver.relativeTolerance = 1e-6;
  fixed.momentumSolver.preconditioner = PreconditionerType::None;
  fixed.pressureSolver.maxIterations = 5000;
  fixed.pressureSolver.absoluteTolerance = 1e-7;
  fixed.pressureSolver.relativeTolerance = 1e-5;
  fixed.pressureSolver.preconditioner = PreconditionerType::None;

  const SIMPLEResult resident = run(c, Arm::GpuResident, &fixed);
  const SIMPLEResult before = run(c, Arm::GpuNonResident, &fixed);
  const SIMPLEResult cpu = run(c, Arm::Cpu, &fixed);
  ++cases;
  if (c.threeDimensional) ++threeD; else ++twoD;

  std::size_t differing = 0;
  Metric cross;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    if (!sameBits(resident.pressure[i], before.pressure[i])) ++differing;
    if (!sameBits(resident.velocity[i].x, before.velocity[i].x)) ++differing;
    if (!sameBits(resident.velocity[i].y, before.velocity[i].y)) ++differing;
    if (!sameBits(resident.velocity[i].z, before.velocity[i].z)) ++differing;
    cross.note(cpu.pressure[i], resident.pressure[i]);
  }
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    if (!sameBits(resident.massFlux[f], before.massFlux[f])) ++differing;
  }
  cross.finish();
  const bool historiesBitwise = resident.uResidualHistory == before.uResidualHistory &&
                                resident.vResidualHistory == before.vResidualHistory &&
                                resident.pressureResidualHistory == before.pressureResidualHistory &&
                                resident.continuityHistory == before.continuityHistory;
  const bool sameWork =
      resident.momentumLinearIterations == before.momentumLinearIterations &&
      resident.pressureLinearIterations == before.pressureLinearIterations;
  const bool engaged = resident.residentSimpleLoop && !before.residentSimpleLoop;
  // NON-VACUITY. Two arms that both fail on iteration 0 agree on everything,
  // because there is nothing yet to disagree about. The comparison is only
  // evidence if the budget actually ran and the histories actually have
  // entries.
  const bool ranTheBudget = resident.iterations == budget && before.iterations == budget &&
                            !resident.uResidualHistory.empty() &&
                            resident.uResidualHistory.size() == static_cast<std::size_t>(budget);
  const bool ok = differing == 0 && historiesBitwise && sameWork && engaged && ranTheBudget &&
                  resident.iterations == before.iterations;
  if (!ok) ++failures;
  if (!ranTheBudget) {
    std::printf("  FAIL %-24s VACUOUS: the arms ran %lld / %lld of the %lld-iteration budget, so "
                "an agreeing comparison compares nothing\n",
                c.name.c_str(), static_cast<long long>(resident.iterations),
                static_cast<long long>(before.iterations), static_cast<long long>(budget));
  }

  std::printf("  %s  %-24s budget=%lld  resident vs pre-residency: %zu differing values, "
              "histories %s, Krylov work %s, arms differed %s\n",
              ok ? "PASS" : "FAIL", c.name.c_str(), static_cast<long long>(budget), differing,
              historiesBitwise ? "bitwise" : "DIFFER", sameWork ? "identical" : "DIFFERS",
              engaged ? "yes" : "NO (vacuous)");
  std::printf("        reported only: cpu vs gpu pressure after %lld outer iterations "
              "Linf=%.3g L2=%.3g -- an unconverged TRANSIENT across two different linear "
              "solvers, not an acceptance measure\n",
              static_cast<long long>(resident.iterations), cross.linf, cross.l2);
  std::printf("        mass imbalance cpu %.3g gpu %.3g   status cpu %s gpu %s\n",
              cpu.globalMassImbalance, resident.globalMassImbalance, statusName(cpu.status),
              statusName(resident.status));
}

// The recorded GPU BiCGSTAB restart asymmetry. Re-run to confirm the
// classification has not broadened; never an acceptance case, never fixed.
//
// AMENDMENT A2. The first version of this function ran the 40x40 case with
// THIS harness's settings, which use Jacobi preconditioning. The recorded
// reproducer uses `PreconditionerType::None`
// (results/gpu-disc-001/full-regression/tools/known_debt_probe.cpp:105,111) and
// differs in nothing else. With Jacobi the breakdown does not occur at all:
// every arm ran its full 3000-iteration budget, so the function asserted a
// baseline shape on a configuration that has never produced it, and FAILED.
//
// That failure is preserved at equivalence/production-FAILED-known-reproducer.log
// and is a defect in this instrument, not a regression: the resident and
// pre-residency arms were identical there too. The settings below are now the
// recorded ones, which makes this STRICTER -- it now actually reproduces the
// breakdown rather than running a configuration that cannot.
void knownReproducer() {
  Case c = cavity("cavity 2d 40 (reproducer)", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0),
                  3000);
  // The recorded reproducer's settings, verbatim: everything as `settings()`
  // builds it except that neither solver is preconditioned.
  SIMPLESettings recorded = c.settings;
  recorded.momentumSolver.preconditioner = PreconditionerType::None;
  recorded.pressureSolver.preconditioner = PreconditionerType::None;
  const SIMPLEResult gpu = run(c, Arm::GpuResident, &recorded);
  const SIMPLEResult before = run(c, Arm::GpuNonResident, &recorded);
  const SIMPLEResult cpu = run(c, Arm::Cpu, &recorded);
  ++cases;
  const bool baselineShape = gpu.status == SIMPLEStatus::PressureCorrectionFailure &&
                             cpu.status != SIMPLEStatus::PressureCorrectionFailure;
  const bool unchangedByResidency =
      before.status == gpu.status && before.iterations == gpu.iterations &&
      before.pressureLinearIterations == gpu.pressureLinearIterations;
  const bool ok = baselineShape && unchangedByResidency;
  if (!ok) ++failures;
  std::printf("  %s  %-24s gpu-resident[%s it=%lld]  gpu-pre-residency[%s it=%lld]  cpu[%s "
              "it=%lld]\n",
              ok ? "PASS" : "FAIL", c.name.c_str(), statusName(gpu.status),
              static_cast<long long>(gpu.iterations), statusName(before.status),
              static_cast<long long>(before.iterations), statusName(cpu.status),
              static_cast<long long>(cpu.iterations));
  std::printf("        recorded baseline: gpu PressureCorrectionFailure, cpu runs its budget. "
              "shape %s; residency changed it: %s\n",
              baselineShape ? "UNCHANGED" : "CHANGED", unchangedByResidency ? "NO" : "YES");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? std::string(argv[1]) : std::string();
  const bool quick = mode == "--quick";
  // `--ladder` runs ONLY the representative 160/320/640 cases and the
  // reproducer. The converged small-case matrix takes about forty minutes and
  // re-running it to reach the ladder would buy nothing; it has its own log,
  // equivalence/production-matrix.log.
  const bool ladderOnly = mode == "--ladder";
  const bool full = mode == "--full" || ladderOnly;
  std::printf("=== GPU-PIPE-001 final residency: CPU production vs GPU production ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }
  std::printf("Bounds adopted from tests/solver/simple/test_simple_gpu_solver.cpp: velocity %.0e, "
              "pressure %.0e, boundary flux %.0e. Nothing is loosened.\n\n",
              kVelocityBound, kPressureBound, kBoundaryFluxBound);

  if (!ladderOnly) {
  runCase(cavity("cavity 2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0), 3000), true);
  runCase(cavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), 3000), true);
  runCase(inletOutlet("channel 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0), 3000),
          true);
  runCase(cavity("cavity 3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0), 3000),
          true);
  if (!quick) {
    runCase(cavity("cavity 2d 20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0), 3000),
            false);
    runCase(cavity("cavity 2d 24 quick", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0), 3000,
                   ConvectionScheme::QUICK),
            false);
    runCase(cavity("cavity 2d 24 linup", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0), 3000,
                   ConvectionScheme::LinearUpwind),
            false);
    runCase(inletOutlet("channel 2d 24 central", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0),
                        3000, ConvectionScheme::Central),
            false);
    runCase(cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0), 3000),
            false);
    runCase(inletOutlet("channel 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0),
                        3000),
            false);
  }
  }  // !ladderOnly
  if (full) {
    std::printf("\n--- representative production ladder, fixed budget (see ladderCase) ---\n");
    // The same budgets the performance benchmark uses at these grids, so the
    // two instruments describe the same runs.
    ladderCase(cavity("cavity 2d 160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0), 60),
               60);
    ladderCase(cavity("cavity 2d 320", MeshGeometry::createCartesian2D(320, 320, 1.0, 1.0), 20),
               20);
    ladderCase(cavity("cavity 2d 640", MeshGeometry::createCartesian2D(640, 640, 1.0, 1.0), 8), 8);
  }

  std::printf("\n--- known GPU BiCGSTAB restart asymmetry (technical debt, not fixed here) ---\n");
  knownReproducer();

  if (ladderOnly) {
    // The coverage assertions below belong to the matrix run, which this mode
    // deliberately skips; asserting them here would fail for the wrong reason.
    std::printf("\n(ladder-only run: the converged case matrix and its coverage assertions are in "
                "equivalence/production-matrix.log)\n");
    std::printf("\ncases=%d failures=%d\n", cases, failures);
    std::printf("%s\n", failures == 0 ? "PRODUCTION EQUIVALENCE (LADDER): PASS"
                                      : "PRODUCTION EQUIVALENCE (LADDER): FAIL");
    return failures == 0 ? 0 : 1;
  }

  std::printf("\n=== coverage ===\n");
  std::printf("  2D / 3D                        %d / %d\n", twoD, threeD);
  std::printf("  pinned / open boundary         %d / %d\n", pinned, openBoundary);
  std::printf("  non-Upwind convection schemes  %d\n", nonUpwind);
  std::printf("  long-running cases (>=50)      %d\n", longRun);
  std::printf("  acceptance cases (both arms converged)   %d\n", acceptanceCases);
  std::printf("  determinism checks             %d\n", determinismChecks);
  std::printf("  non-converging cases classified against the pre-residency path  %d\n",
              classifiedKnown);

  if (twoD == 0 || threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (pinned == 0 || openBoundary == 0) {
    ++failures;
    std::printf("  FAIL pinned and open-boundary cases not both exercised\n");
  }
  if (!quick && nonUpwind == 0) {
    ++failures;
    std::printf("  FAIL no non-Upwind convection scheme exercised\n");
  }
  if (acceptanceCases == 0) {
    ++failures;
    std::printf("  FAIL no case converged on both arms -- nothing was actually accepted\n");
  }
  if (longRun == 0) { ++failures; std::printf("  FAIL no long-running case\n"); }
  if (determinismChecks == 0) { ++failures; std::printf("  FAIL determinism was never checked\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "PRODUCTION EQUIVALENCE: PASS"
                                    : "PRODUCTION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
