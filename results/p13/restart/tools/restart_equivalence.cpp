// P13-001, group B -- continuous vs checkpoint/resume equivalence.
//
// The property under test:
//
//     one solve of N iterations
//   ==
//     a solve of N1, its state carried across a SIMPLEContinuation, then a
//     resumed solve to N
//
// BITWISE, not to a tolerance. Both arms run the identical algorithm over the
// identical bits on the identical hardware; a tolerance here would hide
// exactly the defect this gate exists to catch -- the pre-P13 behaviour in
// which a resumed solve regenerates the face mass flux from the cell velocity
// and silently takes a different iteration path.
//
// NON-VACUITY. A comparison of two arms that did the same thing passes for
// free. Every case asserts the DISPATCH: the segmented arm's later segments
// must report resumedFromContinuation == true and the continuous arm must
// report false, or the case fails as vacuous before any value is compared.
// That is the same guard the GPU-PIPE-001 resident-loop comparison needed.
//
// The splits cover the boundaries as well as the middle: N1 = 1, N1 = N-1,
// a mid split, and a four-way split, because an off-by-one in the segment
// budget or the iteration numbering would survive a single mid-run split.
//
// usage: restart_equivalence [--quick] [--control-drop-flux]
//                            [--control-skip-replay] [--control-ignore-count]
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
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLEContinuation.hpp"

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
using cfd::pressure_velocity::makeSIMPLEContinuation;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEContinuation;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

int failures = 0;
int rows = 0;
std::size_t valuesCompared = 0;

// The three deliberate defects the gate's group D names. Each must break a
// named criterion; they exist so the criteria can be shown to be able to fail.
bool controlDropFlux = false;
bool controlSkipReplay = false;
bool controlIgnoreCount = false;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

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

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
  SIMPLESettings settings;
};

SIMPLESettings cpuSettings(Index outer) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::CPU;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = LinearSolverBackend::CPU;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  return s;
}

SIMPLESettings gpuSettings(Index outer) {
  SIMPLESettings s = cpuSettings(outer);
  s.momentumSolver.backend = LinearSolverBackend::GPU;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.backend = LinearSolverBackend::GPU;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  s.enableGpuDiscretization = true;
  return s;
}

Case cavity(std::string name, Mesh mesh, SIMPLESettings settings) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01},
         std::move(settings)};
  const auto& m = c.mesh;
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
  return c;
}

Case channel(std::string name, Mesh mesh, SIMPLESettings settings) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01},
         std::move(settings)};
  const auto& m = c.mesh;
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
  return c;
}

// One uninterrupted solve.
SIMPLEResult solveContinuous(const Case& c) {
  const SIMPLE simple(c.settings, /*referenceCell=*/0);
  return simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
}

// The same solve, run as `splits.size() + 1` segments, carrying a
// SIMPLEContinuation across each boundary -- exactly what writing a checkpoint
// and resuming from it does, minus the file.
SIMPLEResult solveSegmented(const Case& c, const std::vector<Index>& segmentLengths,
                            bool* everyLaterSegmentResumed) {
  *everyLaterSegmentResumed = true;
  SIMPLEResult result;
  VectorField velocity = c.velocity;
  ScalarField pressure = c.pressure;
  SIMPLEContinuation continuation;
  bool first = true;

  for (Index length : segmentLengths) {
    SIMPLE simple(c.settings, /*referenceCell=*/0);
    continuation.iterationBudget = length;

    // --- group D: one isolated defect each, so each control tests one claim.
    //
    // D1 -- the face mass flux is REGENERATED from the cell velocity instead
    // of carried, which is exactly the pre-P13 behaviour audit.md section 2.2
    // identifies. Iteration count and history stay correct, so this isolates
    // the flux. Must break B1/B2/B3.
    if (controlDropFlux && continuation.completedIterations > 0) {
      continuation.massFlux =
          cfd::physics::calculateMassFlux(c.mesh, velocity, c.fluid, c.vb);
    }
    // D2 -- the history is the right LENGTH but the wrong content, so the
    // monitor replay reaches a different state. Isolates the replay: the flux
    // and the count are still correct. Must break B7 everywhere and B4/B5 on
    // the normalized/adaptive row.
    if (controlSkipReplay && !continuation.history.empty()) {
      for (auto& record : continuation.history) record = continuation.history.front();
    }
    // D3 -- resume as if nothing had been completed. The count, the flux and
    // the history all say "fresh start", which is a self-consistent
    // continuation, so validation accepts it and the criterion, not the
    // validator, is what has to catch it. Must break B4.
    if (controlIgnoreCount) {
      continuation.completedIterations = 0;
      continuation.massFlux = SurfaceField();
      continuation.history.clear();
    }

    simple.setContinuation(&continuation);
    result = simple.solve(c.mesh, c.fluid, c.vb, c.pb, velocity, pressure);

    if (!first && !result.resumedFromContinuation) *everyLaterSegmentResumed = false;
    first = false;

    if (result.status != SIMPLEStatus::MaxIterations) break;  // converged or failed: done.
    velocity = result.velocity;
    pressure = result.pressure;
    continuation = makeSIMPLEContinuation(result);
  }
  return result;
}

int compareHistory(const std::vector<Real>& a, const std::vector<Real>& b, const char* name,
                   const std::string& row) {
  if (a.size() != b.size()) {
    std::printf("  FAIL %-34s %s length %zu vs %zu\n", row.c_str(), name, a.size(), b.size());
    return 1;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    ++valuesCompared;
    if (!sameBits(a[i], b[i])) {
      std::printf("  FAIL %-34s %s[%zu] %.17g vs %.17g\n", row.c_str(), name, i, a[i], b[i]);
      return 1;
    }
  }
  return 0;
}

void check(const Case& c, const std::vector<Index>& segments, const std::string& label) {
  ++rows;
  const std::string row = c.name + " " + label;

  const SIMPLEResult continuous = solveContinuous(c);
  bool resumed = false;
  const SIMPLEResult segmented = solveSegmented(c, segments, &resumed);

  // --- non-vacuity (D7): the arms must genuinely have differed in path -----
  if (continuous.resumedFromContinuation) {
    ++failures;
    std::printf("  FAIL %-34s the continuous arm reported resumedFromContinuation -- vacuous\n",
                row.c_str());
    return;
  }
  if (!resumed && !controlIgnoreCount) {
    ++failures;
    std::printf("  FAIL %-34s no later segment reported resumedFromContinuation -- vacuous\n",
                row.c_str());
    return;
  }

  int errors = 0;
  // B4 -- status and total iteration count.
  if (continuous.status != segmented.status) {
    std::printf("  FAIL %-34s status %s vs %s\n", row.c_str(), statusName(continuous.status),
                statusName(segmented.status));
    ++errors;
  }
  if (continuous.iterations != segmented.iterations) {
    std::printf("  FAIL %-34s iterations %zu vs %zu\n", row.c_str(),
                static_cast<std::size_t>(continuous.iterations),
                static_cast<std::size_t>(segmented.iterations));
    ++errors;
  }

  // B1/B2/B3 -- the fields.
  if (continuous.velocity.size() != segmented.velocity.size()) {
    std::printf("  FAIL %-34s velocity size\n", row.c_str());
    ++errors;
  } else {
    for (Index i = 0; i < continuous.velocity.size(); ++i) {
      valuesCompared += 3;
      if (!sameBits(continuous.velocity[i].x, segmented.velocity[i].x) ||
          !sameBits(continuous.velocity[i].y, segmented.velocity[i].y) ||
          !sameBits(continuous.velocity[i].z, segmented.velocity[i].z)) {
        std::printf("  FAIL %-34s velocity[%zu] (%.17g %.17g %.17g) vs (%.17g %.17g %.17g)\n",
                    row.c_str(), static_cast<std::size_t>(i), continuous.velocity[i].x,
                    continuous.velocity[i].y, continuous.velocity[i].z, segmented.velocity[i].x,
                    segmented.velocity[i].y, segmented.velocity[i].z);
        ++errors;
        break;
      }
    }
  }
  for (Index i = 0; i < continuous.pressure.size() && i < segmented.pressure.size(); ++i) {
    ++valuesCompared;
    if (!sameBits(continuous.pressure[i], segmented.pressure[i])) {
      std::printf("  FAIL %-34s pressure[%zu] %.17g vs %.17g\n", row.c_str(),
                  static_cast<std::size_t>(i), continuous.pressure[i], segmented.pressure[i]);
      ++errors;
      break;
    }
  }
  for (Index i = 0; i < continuous.massFlux.size() && i < segmented.massFlux.size(); ++i) {
    ++valuesCompared;
    if (!sameBits(continuous.massFlux[i], segmented.massFlux[i])) {
      std::printf("  FAIL %-34s massFlux[%zu] %.17g vs %.17g\n", row.c_str(),
                  static_cast<std::size_t>(i), continuous.massFlux[i], segmented.massFlux[i]);
      ++errors;
      break;
    }
  }

  // B5 -- the residual histories, full length.
  errors += compareHistory(continuous.uResidualHistory, segmented.uResidualHistory, "u", row);
  errors += compareHistory(continuous.vResidualHistory, segmented.vResidualHistory, "v", row);
  errors += compareHistory(continuous.wResidualHistory, segmented.wResidualHistory, "w", row);
  errors += compareHistory(continuous.pressureResidualHistory, segmented.pressureResidualHistory,
                           "p", row);
  errors += compareHistory(continuous.continuityHistory, segmented.continuityHistory, "cont", row);
  errors += compareHistory(continuous.globalMassImbalanceHistory,
                           segmented.globalMassImbalanceHistory, "imbal", row);

  // B6 -- the reported final scalars.
  if (!sameBits(continuous.finalContinuityResidual, segmented.finalContinuityResidual) ||
      !sameBits(continuous.globalMassImbalance, segmented.globalMassImbalance)) {
    std::printf("  FAIL %-34s final continuity/imbalance differ\n", row.c_str());
    ++errors;
  }

  // B7 -- the robustness diagnostics the monitor replay has to reconstruct.
  const auto& ra = continuous.robustness;
  const auto& rb = segmented.robustness;
  errors += compareHistory(ra.uNormalizedHistory, rb.uNormalizedHistory, "uNorm", row);
  errors += compareHistory(ra.vNormalizedHistory, rb.vNormalizedHistory, "vNorm", row);
  errors += compareHistory(ra.pressureNormalizedHistory, rb.pressureNormalizedHistory, "pNorm",
                           row);
  errors += compareHistory(ra.continuityNormalizedHistory, rb.continuityNormalizedHistory,
                           "cNorm", row);
  errors += compareHistory(ra.convergenceDistanceHistory, rb.convergenceDistanceHistory, "dist",
                           row);
  errors += compareHistory(ra.velocityRelaxationHistory, rb.velocityRelaxationHistory, "relaxU",
                           row);
  errors += compareHistory(ra.pressureRelaxationHistory, rb.pressureRelaxationHistory, "relaxP",
                           row);

  if (errors > 0) {
    ++failures;
  } else {
    std::printf("  PASS %-34s %s  %zu iterations, %zu values bitwise\n", row.c_str(),
                statusName(continuous.status), static_cast<std::size_t>(continuous.iterations),
                valuesCompared);
  }
}

// Every split shape a case is run through. N is the case's own maxIterations.
void checkAllSplits(const Case& c) {
  const Index n = c.settings.maxIterations;
  if (n < 4) return;
  check(c, {1, n - 1}, "split@1");
  check(c, {n / 2, n - n / 2}, "split@mid");
  check(c, {n - 1, 1}, "split@N-1");
  const Index q = n / 4;
  check(c, {q, q, q, n - 3 * q}, "split@quarters");
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) quick = true;
    if (std::strcmp(argv[i], "--control-drop-flux") == 0) controlDropFlux = true;
    if (std::strcmp(argv[i], "--control-skip-replay") == 0) controlSkipReplay = true;
    if (std::strcmp(argv[i], "--control-ignore-count") == 0) controlIgnoreCount = true;
  }
  const bool anyControl = controlDropFlux || controlSkipReplay || controlIgnoreCount;
  if (anyControl) {
    std::printf("CONTROL ACTIVE -- this run is a non-vacuity control and MUST FAIL\n");
    std::printf("  drop-flux=%d skip-replay=%d ignore-count=%d\n\n", controlDropFlux,
                controlSkipReplay, controlIgnoreCount);
  }

  std::printf("=== B-cpu-2d ===\n");
  checkAllSplits(cavity("cpu-cavity-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                        cpuSettings(8)));
  std::printf("=== B-cpu-2d-io ===\n");
  checkAllSplits(channel("cpu-channel-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                         cpuSettings(8)));
  std::printf("=== B-cpu-3d ===\n");
  checkAllSplits(cavity("cpu-cavity-3d-6",
                        MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0), cpuSettings(6)));
  std::printf("=== B-gpu-2d ===\n");
  checkAllSplits(cavity("gpu-cavity-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                        gpuSettings(8)));
  std::printf("=== B-gpu-3d ===\n");
  checkAllSplits(cavity("gpu-cavity-3d-6",
                        MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0), gpuSettings(6)));

  std::printf("=== B-robust ===\n");
  {
    SIMPLESettings s = cpuSettings(40);
    s.robustness.convergenceCriterion = cfd::solver::ConvergenceCriterion::Normalized;
    s.robustness.stagnation.enabled = true;
    s.robustness.stagnation.window = 5;
    s.robustness.stagnation.startIteration = 8;
    s.robustness.divergence.enabled = true;
    s.robustness.divergence.window = 3;
    s.robustness.divergence.startIteration = 4;
    s.robustness.adaptiveRelaxation.enabled = true;
    checkAllSplits(cavity("cpu-cavity-2d-16-robust",
                          MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), s));
  }

  std::printf("=== B-converged ===\n");
  {
    // Budget well above what this case needs, so the solve reaches Converged
    // on its own and the split happens long before convergence. This is the
    // row that decides whether convergence CLASSIFICATION survives a restart,
    // not merely whether the arithmetic does.
    Case c = cavity("cpu-cavity-2d-12-converge",
                    MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0), cpuSettings(3000));
    check(c, {400, 2600}, "split@400");
    if (!quick) check(c, {1, 2999}, "split@1");
    if (!quick) check(c, {700, 700, 700, 900}, "split@quarters");
  }

  if (!quick) {
    std::printf("=== B-schemes ===\n");
    {
      SIMPLESettings s = cpuSettings(8);
      s.convectionScheme = ConvectionScheme::LinearUpwind;
      checkAllSplits(cavity("cpu-cavity-2d-16-linup",
                            MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), s));
    }
    {
      SIMPLESettings s = cpuSettings(6);
      s.nonOrthogonalCorrections = 2;
      checkAllSplits(cavity("cpu-cavity-2d-16-nonorth",
                            MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), s));
    }
  }

  std::printf("\nrows %d   failures %d   values compared %zu\n", rows, failures, valuesCompared);
  if (anyControl) {
    std::printf("CONTROL VERDICT: %s\n", failures > 0 ? "DETECTED (control failed as required)"
                                                      : "UNDETECTED -- the criterion is vacuous");
    return failures > 0 ? 0 : 1;
  }
  std::printf("GROUP B: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
