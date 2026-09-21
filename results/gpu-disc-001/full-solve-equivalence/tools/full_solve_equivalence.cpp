// GPU-DISC-001N gate -- complete production solves, CPU vs GPU discretization.
//
// Drives `SIMPLE::solve` to convergence on both arms from identical initial
// state. The ONLY difference between the arms is
// `SIMPLESettings::enableGpuDiscretization`; both use the same CPU linear
// solvers, so the variable under test is where the DISCRETIZATION runs.
//
// Configuration and tolerance are ADOPTED from the repository's own CPU/GPU
// full-solve test (tests/solver/simple/test_simple_gpu_solver.cpp): 3000 outer
// iterations, 1e-6 outer tolerances, 1e-10/1e-8 linear tolerances, both arms
// must report Converged, then maxVelocityError < 1e-6 and
// maxPressureError < 1e-6. Nothing is loosened and no "full-solve tolerance"
// is invented.
//
// A case counts as an acceptance result ONLY when both arms converge. That is
// the correction results/gpu-pipe-001/equivalence/summary.md section 2 records
// against itself: comparing unconverged transients to a bound calibrated on
// converged solutions is meaningless.
//
// Layers:
//   C  convergence   status, outer iterations, final residuals, mass imbalance
//   H  history       EVERY outer iteration's residuals, both arms, with the
//                    first divergent iteration located
//   F  fields        final pressure / U / V / W / face flux: Linf, L2,
//                    relative, bitwise counts
//   S  stages        per-ITERATION field comparison on the small case, by
//                    re-solving with maxIterations = k -- locates the first
//                    divergent iteration exactly
//   N  conservation  global balance, per-cell continuity, interior-face
//                    cancellation, boundary accounting, finiteness
//   D  determinism   each arm repeated
//   T  transfers     H2D/D2H/sync/kernels per solve and per iteration
//   P  performance   wall time per arm and per outer iteration
//
// usage: full_solve_equivalence [--quick]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
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

int failures = 0;
int cases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;
Real globalMaxAbs = 0.0;

struct Coverage {
  int twoD = 0, threeD = 0;
  int converged = 0, notConverged = 0;
  int openBoundary = 0, pinned = 0;
  int nonUpwindSchemes = 0;
  int nonOrthogonal = 0;
  int historyChecks = 0;
  int perIterationFieldChecks = 0;
  int conservationChecks = 0;
  int determinismChecks = 0;
  int longRunCases = 0;
  int maxOuterIterations = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// --- the repository's own configuration, adopted verbatim ------------------
SIMPLESettings baseSettings() {
  SIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  return s;
}
constexpr Real kVelocityBound = 1e-6;   // test_simple_gpu_solver.cpp:146
constexpr Real kPressureBound = 1e-6;   // test_simple_gpu_solver.cpp:147
constexpr Real kBoundaryFluxBound = 1e-8;  // test_simple_gpu_solver.cpp:154

Mesh warped2D(Index n) {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real x = static_cast<Real>(i) / static_cast<Real>(n);
      const Real y = static_cast<Real>(j) / static_cast<Real>(n);
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, v);
}

struct Case {
  Case(std::string n, Mesh m) : name(std::move(n)), mesh(std::move(m)) {}
  std::string name;
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
  SIMPLESettings settings = baseSettings();
  bool openBoundary = false;
  bool nonOrthogonal = false;
};

Case cavity(const std::string& name, Mesh mesh, ConvectionScheme scheme = ConvectionScheme::Upwind,
            bool nonOrthogonal = false) {
  Case c(name, std::move(mesh));
  const auto& m = c.mesh;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    c.velocityBoundaries.set(
        m, patch.name(),
        lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
            : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::Wall>()));
    c.pressureBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  const Index nc = m.numberOfCells();
  c.velocity = VectorField(nc);
  c.pressure = ScalarField(nc);
  for (Index cell = 0; cell < nc; ++cell) {
    c.velocity[cell] = Vector3{0.0, 0.0, 0.0};
    c.pressure[cell] = 0.0;
  }
  c.settings.convectionScheme = scheme;
  c.nonOrthogonal = nonOrthogonal;
  return c;
}

// Open boundaries with a genuine net through-flow: an Inlet at one end, a
// FixedValue pressure Outlet at the other, so production SUPPRESSES the
// reference pin and the open boundary carries the correction.
Case inletOutlet(const std::string& name, Mesh mesh,
                 ConvectionScheme scheme = ConvectionScheme::Upwind) {
  Case c(name, std::move(mesh));
  const auto& m = c.mesh;
  std::size_t i = 0;
  const std::size_t patchCount = m.boundaryPatches().size();
  for (const auto& patch : m.boundaryPatches()) {
    if (i == 0) {
      c.velocityBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::Inlet>(Vector3{1.0, 0.0, 0.0}));
      c.pressureBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (i + 1 == patchCount) {
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Outlet>());
      c.pressureBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Wall>());
      c.pressureBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    ++i;
  }
  const Index nc = m.numberOfCells();
  c.velocity = VectorField(nc);
  c.pressure = ScalarField(nc);
  for (Index cell = 0; cell < nc; ++cell) {
    c.velocity[cell] = Vector3{1.0, 0.0, 0.0};
    c.pressure[cell] = 0.0;
  }
  c.settings.convectionScheme = scheme;
  c.openBoundary = true;
  return c;
}

SIMPLEResult run(const Case& c, bool gpuDiscretization, Index maxIterations = 0) {
  SIMPLESettings s = c.settings;
  s.enableGpuDiscretization = gpuDiscretization;
  if (maxIterations > 0) s.maxIterations = maxIterations;
  SIMPLE simple(s, /*referenceCell=*/0);
  return simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, c.velocity,
                      c.pressure);
}

// --- metrics ---------------------------------------------------------------
struct FieldMetric {
  std::size_t compared = 0, bitwise = 0;
  Real linf = 0.0, l2 = 0.0, relative = 0.0, scale = 0.0;
  void note(Real a, Real b) {
    ++compared;
    ++valuesCompared;
    if (sameBits(a, b)) { ++bitwise; ++bitwiseValues; }
    const Real diff = std::abs(a - b);
    linf = std::max(linf, diff);
    l2 += diff * diff;
    scale = std::max(scale, std::abs(a));
    if (std::abs(a) > 0.0) relative = std::max(relative, diff / std::abs(a));
    globalMaxAbs = std::max(globalMaxAbs, diff);
  }
  void finish() { l2 = compared == 0 ? 0.0 : std::sqrt(l2 / static_cast<Real>(compared)); }
};

std::vector<Real> imbalanceOf(const Mesh& mesh, const SurfaceField& flux) {
  std::vector<Real> out(static_cast<std::size_t>(mesh.numberOfCells()), 0.0);
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    Real sum = 0.0;
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      sum += (face.owner() == c) ? flux[faceId] : -flux[faceId];
    }
    out[c] = sum;
  }
  return out;
}

bool allFinite(const SIMPLEResult& r) {
  for (std::size_t i = 0; i < r.pressure.size(); ++i) {
    if (!std::isfinite(r.pressure[i])) return false;
    if (!std::isfinite(r.velocity[i].x) || !std::isfinite(r.velocity[i].y) ||
        !std::isfinite(r.velocity[i].z))
      return false;
  }
  for (std::size_t f = 0; f < r.massFlux.size(); ++f)
    if (!std::isfinite(r.massFlux[f])) return false;
  return true;
}

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

// ---------------------------------------------------------------------------

void runCase(const Case& c, bool traceEveryIteration) {
  const bool threeD = c.mesh.dimension() == 3;
  if (threeD) ++coverage.threeD; else ++coverage.twoD;
  if (c.openBoundary) ++coverage.openBoundary; else ++coverage.pinned;
  if (c.settings.convectionScheme != ConvectionScheme::Upwind) ++coverage.nonUpwindSchemes;
  if (c.nonOrthogonal) ++coverage.nonOrthogonal;

  cfd::gpu::resetGpuExecutionStats();
  const auto cpuStart = std::chrono::steady_clock::now();
  const SIMPLEResult cpu = run(c, false);
  const auto cpuEnd = std::chrono::steady_clock::now();
  const auto statsAfterCpu = cfd::gpu::gpuExecutionStats();

  cfd::gpu::resetGpuExecutionStats();
  const auto gpuStart = std::chrono::steady_clock::now();
  const SIMPLEResult gpu = run(c, true);
  const auto gpuEnd = std::chrono::steady_clock::now();
  const auto statsAfterGpu = cfd::gpu::gpuExecutionStats();

  const double cpuSeconds = std::chrono::duration<double>(cpuEnd - cpuStart).count();
  const double gpuSeconds = std::chrono::duration<double>(gpuEnd - gpuStart).count();

  const bool bothConverged =
      cpu.status == SIMPLEStatus::Converged && gpu.status == SIMPLEStatus::Converged;
  if (bothConverged) ++coverage.converged; else ++coverage.notConverged;
  coverage.maxOuterIterations =
      std::max(coverage.maxOuterIterations, static_cast<int>(cpu.iterations));
  if (cpu.iterations >= 50) ++coverage.longRunCases;

  // --- C: convergence ----------------------------------------------------
  {
    ++cases;
    // Both arms must obey the SAME production criteria. Iteration counts must
    // match here because the arms are bitwise identical when the operators are
    // -- a differing count would itself be the divergence signal.
    const bool ok = cpu.status == gpu.status && cpu.iterations == gpu.iterations &&
                    gpu.gpuDiscretization == cfd::gpu::cudaAvailable();
    if (!ok) ++failures;
    std::printf("  %s C  %-22s cpu[%s it=%zu] gpu[%s it=%zu gpuDisc=%d] "
                "residuals cpu[u=%.3g v=%.3g p=%.3g cont=%.3g mass=%.3g]\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), statusName(cpu.status),
                static_cast<std::size_t>(cpu.iterations), statusName(gpu.status),
                static_cast<std::size_t>(gpu.iterations),
                static_cast<int>(gpu.gpuDiscretization), cpu.finalUResidual, cpu.finalVResidual,
                cpu.finalPressureResidual, cpu.finalContinuityResidual, cpu.globalMassImbalance);
    std::printf("       %-22s gpu residuals   [u=%.3g v=%.3g p=%.3g cont=%.3g mass=%.3g] "
                "linear iterations cpu[mom=%zu p=%zu] gpu[mom=%zu p=%zu]\n",
                "", gpu.finalUResidual, gpu.finalVResidual, gpu.finalPressureResidual,
                gpu.finalContinuityResidual, gpu.globalMassImbalance,
                static_cast<std::size_t>(cpu.momentumLinearIterations),
                static_cast<std::size_t>(cpu.pressureLinearIterations),
                static_cast<std::size_t>(gpu.momentumLinearIterations),
                static_cast<std::size_t>(gpu.pressureLinearIterations));
  }

  // --- H: the whole residual history, per outer iteration ----------------
  {
    // GPU-DISC-001P: the iteration alone did not localise an integration
    // control -- "iteration 5" does not say which stage of iteration 5 broke.
    // Each history is attributed to the SIMPLE stage that produces it, and the
    // EARLIEST divergence across all five is reported with its stage and
    // metric. Scanning for the earliest rather than the first-found means the
    // answer does not depend on the order these are compared in; a genuine tie
    // keeps the earlier stage in SIMPLE's own order, which is that order.
    int firstDivergent = -1;
    const char* firstStage = "";
    const char* firstMetric = "";
    Real maxDiff = 0.0;
    const auto compareHistory = [&](const std::vector<Real>& a, const std::vector<Real>& b,
                                    const char* metric, const char* stage) {
      const std::size_t n = std::min(a.size(), b.size());
      int divergent = -1;
      for (std::size_t i = 0; i < n; ++i) {
        const Real diff = std::abs(a[i] - b[i]);
        maxDiff = std::max(maxDiff, diff);
        if (!sameBits(a[i], b[i]) && divergent < 0) divergent = static_cast<int>(i) + 1;
      }
      if (a.size() != b.size() && divergent < 0) divergent = static_cast<int>(n) + 1;
      if (divergent > 0 && (firstDivergent < 0 || divergent < firstDivergent)) {
        firstDivergent = divergent;
        firstStage = stage;
        firstMetric = metric;
      }
    };
    compareHistory(cpu.uResidualHistory, gpu.uResidualHistory, "u residual", "momentum solve");
    compareHistory(cpu.vResidualHistory, gpu.vResidualHistory, "v residual", "momentum solve");
    compareHistory(cpu.wResidualHistory, gpu.wResidualHistory, "w residual", "momentum solve");
    compareHistory(cpu.pressureResidualHistory, gpu.pressureResidualHistory, "pressure residual",
                   "pressure-correction solve");
    compareHistory(cpu.continuityHistory, gpu.continuityHistory, "continuity",
                   "velocity / face-flux correction -> continuity");
    globalMaxAbs = std::max(globalMaxAbs, maxDiff);
    ++cases;
    ++coverage.historyChecks;
    const bool ok = firstDivergent < 0;
    if (!ok) ++failures;
    std::printf("  %s H  %-22s %zu outer iterations compared, max history discrepancy %.3g, "
                "first divergent iteration: %s\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), cpu.uResidualHistory.size(), maxDiff,
                ok ? "none" : std::to_string(firstDivergent).c_str());
    if (!ok) {
      std::printf("       first divergent stage: %s   first divergent metric: %s\n", firstStage,
                  firstMetric);
    }
  }

  // --- F: final fields ---------------------------------------------------
  {
    FieldMetric pressure, u, v, w, flux;
    for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
      pressure.note(cpu.pressure[i], gpu.pressure[i]);
      u.note(cpu.velocity[i].x, gpu.velocity[i].x);
      v.note(cpu.velocity[i].y, gpu.velocity[i].y);
      w.note(cpu.velocity[i].z, gpu.velocity[i].z);
    }
    for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) flux.note(cpu.massFlux[f], gpu.massFlux[f]);
    pressure.finish(); u.finish(); v.finish(); w.finish(); flux.finish();
    ++cases;
    // The project's own bounds. They are met with enormous margin because the
    // arms are bitwise, but the ACCEPTANCE criterion is the adopted one.
    const Real maxVelocity = std::max({u.linf, v.linf, w.linf});
    const bool withinAdopted = maxVelocity < kVelocityBound && pressure.linf < kPressureBound;
    const bool ok = bothConverged ? withinAdopted : true;
    if (!ok) ++failures;
    std::printf("  %s F  %-22s p[Linf=%.3g L2=%.3g rel=%.3g bit=%zu/%zu] "
                "u[Linf=%.3g L2=%.3g] v[Linf=%.3g] w[Linf=%.3g] flux[Linf=%.3g L2=%.3g "
                "bit=%zu/%zu]%s\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), pressure.linf, pressure.l2,
                pressure.relative, pressure.bitwise, pressure.compared, u.linf, u.l2, v.linf,
                w.linf, flux.linf, flux.l2, flux.bitwise, flux.compared,
                bothConverged ? "" : "   (TRANSIENT comparison -- not an acceptance result)");
  }

  // --- N: conservation ---------------------------------------------------
  {
    std::size_t errors = 0;
    if (!allFinite(cpu) || !allFinite(gpu)) ++errors;
    const auto cpuImbalance = imbalanceOf(c.mesh, cpu.massFlux);
    const auto gpuImbalance = imbalanceOf(c.mesh, gpu.massFlux);
    Real maxImbalanceDiff = 0.0, maxCpuImbalance = 0.0;
    for (std::size_t i = 0; i < cpuImbalance.size(); ++i) {
      maxImbalanceDiff = std::max(maxImbalanceDiff, std::abs(cpuImbalance[i] - gpuImbalance[i]));
      maxCpuImbalance = std::max(maxCpuImbalance, std::abs(cpuImbalance[i]));
    }
    // Interior-face cancellation: exactly representable, so exact.
    std::size_t pairs = 0;
    for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
      if (c.mesh.face(f).isBoundary()) continue;
      if (!sameBits(gpu.massFlux[f] + (-gpu.massFlux[f]), 0.0)) ++errors;
      ++pairs;
    }
    if (pairs == 0) ++errors;
    // Boundary accounting. A closed cavity must carry ~zero boundary flux --
    // the existing test's own property, with its own bound.
    Real boundaryNet = 0.0, maxBoundaryFlux = 0.0;
    for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
      if (!c.mesh.face(f).isBoundary()) continue;
      boundaryNet += gpu.massFlux[f];
      maxBoundaryFlux = std::max(maxBoundaryFlux, std::abs(gpu.massFlux[f]));
    }
    if (!c.openBoundary && bothConverged && maxBoundaryFlux > kBoundaryFluxBound) ++errors;
    const auto continuity = cfd::physics::evaluateContinuity(c.mesh, gpu.massFlux);
    ++cases;
    ++coverage.conservationChecks;
    if (errors != 0) ++failures;
    std::printf("  %s N  %-22s finite[cpu+gpu] pairs=%zu |dImbalance|=%.3g maxCellImbalance=%.3g "
                "globalNet cpu=%.3g gpu=%.3g maxBoundaryFlux=%.3g errors=%zu\n",
                errors == 0 ? "PASS" : "FAIL", c.name.c_str(), pairs, maxImbalanceDiff,
                maxCpuImbalance, cpu.globalMassImbalance, std::abs(continuity.globalNetFlux),
                maxBoundaryFlux, errors);
  }

  // --- T / P: transfers and timing --------------------------------------
  {
    const Index iterations = std::max<Index>(gpu.iterations, 1);
    std::printf("       T  %-22s cpu-arm[h2d=%llu d2h=%llu kern=%llu sync=%llu] "
                "gpu-arm[h2d=%llu/%lluB d2h=%llu/%lluB kern=%llu sync=%llu alloc=%llu] "
                "per-iteration[h2d=%llu d2h=%llu kern=%llu]\n",
                "", static_cast<unsigned long long>(statsAfterCpu.hostToDeviceCalls),
                static_cast<unsigned long long>(statsAfterCpu.deviceToHostCalls),
                static_cast<unsigned long long>(statsAfterCpu.kernelLaunches),
                static_cast<unsigned long long>(statsAfterCpu.synchronizations),
                static_cast<unsigned long long>(statsAfterGpu.hostToDeviceCalls),
                static_cast<unsigned long long>(statsAfterGpu.hostToDeviceBytes),
                static_cast<unsigned long long>(statsAfterGpu.deviceToHostCalls),
                static_cast<unsigned long long>(statsAfterGpu.deviceToHostBytes),
                static_cast<unsigned long long>(statsAfterGpu.kernelLaunches),
                static_cast<unsigned long long>(statsAfterGpu.synchronizations),
                static_cast<unsigned long long>(statsAfterGpu.allocations),
                static_cast<unsigned long long>(statsAfterGpu.hostToDeviceCalls / iterations),
                static_cast<unsigned long long>(statsAfterGpu.deviceToHostCalls / iterations),
                static_cast<unsigned long long>(statsAfterGpu.kernelLaunches / iterations));
    std::printf("       P  %-22s cpu=%.3fs gpu=%.3fs outer=%zu  cpu/iter=%.4gs gpu/iter=%.4gs "
                "ratio=%.3g\n",
                "", cpuSeconds, gpuSeconds, static_cast<std::size_t>(cpu.iterations),
                cpuSeconds / static_cast<double>(std::max<Index>(cpu.iterations, 1)),
                gpuSeconds / static_cast<double>(std::max<Index>(gpu.iterations, 1)),
                cpuSeconds > 0.0 ? gpuSeconds / cpuSeconds : 0.0);
  }

  // --- S: per-ITERATION field comparison (small case only) ---------------
  if (traceEveryIteration) {
    const Index trace = std::min<Index>(cpu.iterations, 40);
    int firstDivergent = -1;
    Real maxDiff = 0.0;
    for (Index k = 1; k <= trace && firstDivergent < 0; ++k) {
      const SIMPLEResult a = run(c, false, k);
      const SIMPLEResult b = run(c, true, k);
      for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
        const Real dp = std::abs(a.pressure[i] - b.pressure[i]);
        const Real du = std::abs(a.velocity[i].x - b.velocity[i].x);
        const Real dv = std::abs(a.velocity[i].y - b.velocity[i].y);
        const Real dw = std::abs(a.velocity[i].z - b.velocity[i].z);
        maxDiff = std::max({maxDiff, dp, du, dv, dw});
        if ((!sameBits(a.pressure[i], b.pressure[i]) ||
             !sameBits(a.velocity[i].x, b.velocity[i].x) ||
             !sameBits(a.velocity[i].y, b.velocity[i].y) ||
             !sameBits(a.velocity[i].z, b.velocity[i].z)) &&
            firstDivergent < 0)
          firstDivergent = static_cast<int>(k);
      }
      for (Index f = 0; f < c.mesh.numberOfFaces() && firstDivergent < 0; ++f) {
        maxDiff = std::max(maxDiff, std::abs(a.massFlux[f] - b.massFlux[f]));
        if (!sameBits(a.massFlux[f], b.massFlux[f])) firstDivergent = static_cast<int>(k);
      }
    }
    globalMaxAbs = std::max(globalMaxAbs, maxDiff);
    ++cases;
    ++coverage.perIterationFieldChecks;
    const bool ok = firstDivergent < 0;
    if (!ok) ++failures;
    std::printf("  %s S  %-22s per-iteration FIELD comparison over %zu iterations, max %.3g, "
                "first divergent iteration: %s\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), static_cast<std::size_t>(trace), maxDiff,
                ok ? "none" : std::to_string(firstDivergent).c_str());
  }

  // --- D: determinism ----------------------------------------------------
  {
    const SIMPLEResult cpuAgain = run(c, false);
    const SIMPLEResult gpuAgain = run(c, true);
    std::size_t errors = 0;
    for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
      if (!sameBits(cpu.pressure[i], cpuAgain.pressure[i])) ++errors;
      if (!sameBits(gpu.pressure[i], gpuAgain.pressure[i])) ++errors;
      if (!sameBits(cpu.velocity[i].x, cpuAgain.velocity[i].x)) ++errors;
      if (!sameBits(gpu.velocity[i].x, gpuAgain.velocity[i].x)) ++errors;
    }
    for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
      if (!sameBits(cpu.massFlux[f], cpuAgain.massFlux[f])) ++errors;
      if (!sameBits(gpu.massFlux[f], gpuAgain.massFlux[f])) ++errors;
    }
    if (cpu.iterations != cpuAgain.iterations || gpu.iterations != gpuAgain.iterations) ++errors;
    if (cpu.uResidualHistory != cpuAgain.uResidualHistory) ++errors;
    if (gpu.uResidualHistory != gpuAgain.uResidualHistory) ++errors;
    ++cases;
    ++coverage.determinismChecks;
    if (errors != 0) ++failures;
    std::printf("  %s D  %-22s each arm repeated: fields, iteration count and residual history "
                "bitwise identical (errors=%zu)\n",
                errors == 0 ? "PASS" : "FAIL", c.name.c_str(), errors);
  }
}

// The recorded GPU BiCGSTAB restart asymmetry. This is the GPU LINEAR SOLVER,
// a different axis from the GPU discretization this gate varies. Re-run to
// confirm the classification is unchanged; never used as an acceptance case,
// and never fixed here.
void runKnownReproducer() {
  const Case c = cavity("cavity 2d 40 (reproducer)",
                        MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
  SIMPLESettings s = c.settings;
  s.pressureSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
  s.momentumSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
  SIMPLE simple(s, 0);
  const SIMPLEResult gpuSolver =
      simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, c.velocity,
                   c.pressure);
  const SIMPLEResult cpuSolver = run(c, false);
  ++cases;
  // The BASELINE behaviour: the GPU-solver arm exits on PressureCorrectionFailure
  // while the CPU arm runs its budget. Unchanged == still known debt. Anything
  // else on this case is a CHANGE and is reported, not silently accepted.
  const bool unchanged = gpuSolver.status == SIMPLEStatus::PressureCorrectionFailure &&
                         cpuSolver.status != SIMPLEStatus::PressureCorrectionFailure;
  std::printf("  %s K  %-22s gpu-solver[%s it=%zu] cpu-solver[%s it=%zu] -- %s\n",
              unchanged ? "PASS" : "NOTE", c.name.c_str(), statusName(gpuSolver.status),
              static_cast<std::size_t>(gpuSolver.iterations), statusName(cpuSolver.status),
              static_cast<std::size_t>(cpuSolver.iterations),
              unchanged ? "classification UNCHANGED from baseline (known debt, not fixed here)"
                        : "classification CHANGED from baseline -- see summary.md");
  if (!unchanged) {
    std::printf("       the recorded baseline is: gpu PressureCorrectionFailure, cpu runs the "
                "full budget. A DIFFERENT outcome here is reported, not treated as a pass or as "
                "a failure of the discretization gate -- it is the solver axis.\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? std::string(argv[1]) : std::string();
  const bool quick = mode == "--quick";
  // `--controls` is the NEGATIVE-CONTROL mode: one small case with a short
  // outer budget and a full per-iteration field trace. The controls are
  // detected by WHERE they first diverge, and every one of them diverges within
  // a few iterations, so a short budget localises them without spending an hour
  // converging a case whose answer is already known from the full matrix.
  const bool controlsMode = mode == "--controls";
  std::printf("=== GPU-DISC-001N: full production solves, CPU vs GPU discretization ===\n");
  std::printf("CUDA available: %s\n", cfd::gpu::cudaAvailable() ? "yes" : "no");
  std::printf("Configuration and bounds adopted from tests/solver/simple/test_simple_gpu_solver.cpp"
              ": 3000 outer, 1e-6 outer tolerances, 1e-10/1e-8 linear, velocity/pressure bound "
              "1e-6\n\n");

  if (controlsMode) {
    Case c = cavity("cavity 2d 8 (controls)", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
    c.settings.maxIterations = 30;
    runCase(c, true);
    std::printf("\ncases=%d failures=%d\n", cases, failures);
    std::printf("%s\n", failures == 0 ? "FULL SOLVE EQUIVALENCE: PASS"
                                      : "FULL SOLVE EQUIVALENCE: FAIL");
    return failures == 0 ? 0 : 1;
  }

  runCase(cavity("cavity 2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)), true);
  runCase(inletOutlet("channel 2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0)), false);
  // Quick mode carries a 3D case too: U/V/W, the 3D gradients, response
  // coefficients and corrections are a different code path, not a bigger one,
  // so a smoke run that omitted them would not be a smoke run of this gate.
  runCase(cavity("cavity 3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)), false);
  if (!quick) {
    runCase(cavity("cavity 2d 20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0)), false);
    runCase(cavity("cavity 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), false);
    runCase(cavity("cavity 2d 16 quick", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                   ConvectionScheme::QUICK),
            false);
    runCase(cavity("cavity 2d 16 linup", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                   ConvectionScheme::LinearUpwind),
            false);
    runCase(cavity("cavity 2d 16 warped", warped2D(16), ConvectionScheme::Upwind, true), false);
    runCase(inletOutlet("channel 2d 16 central",
                        MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                        ConvectionScheme::Central),
            false);
    runCase(cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0)), true);
    runCase(inletOutlet("channel 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0)),
            false);
  }

  std::printf("\n--- known GPU BiCGSTAB restart asymmetry (solver axis, not this gate's) ---\n");
  runKnownReproducer();

  std::printf("\n=== coverage ===\n");
  std::printf("  2D / 3D cases                     %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  both arms converged / not         %d / %d\n", coverage.converged,
              coverage.notConverged);
  std::printf("  pinned / open-boundary cases      %d / %d\n", coverage.pinned,
              coverage.openBoundary);
  std::printf("  non-Upwind convection cases       %d\n", coverage.nonUpwindSchemes);
  std::printf("  non-orthogonal mesh cases         %d\n", coverage.nonOrthogonal);
  std::printf("  residual-history comparisons      %d\n", coverage.historyChecks);
  std::printf("  per-iteration field comparisons   %d\n", coverage.perIterationFieldChecks);
  std::printf("  conservation checks               %d\n", coverage.conservationChecks);
  std::printf("  determinism checks                %d\n", coverage.determinismChecks);
  std::printf("  long-run cases (>=50 outer)       %d\n", coverage.longRunCases);
  std::printf("  longest solve (outer iterations)  %d\n", coverage.maxOuterIterations);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical                 %zu\n", bitwiseValues);
  std::printf("  max discrepancy over ALL histories and fields  %.3g\n", globalMaxAbs);

  if (coverage.twoD == 0 || coverage.threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (coverage.converged == 0) { ++failures; std::printf("  FAIL no case converged on both arms\n"); }
  if (coverage.openBoundary == 0 || coverage.pinned == 0) { ++failures; std::printf("  FAIL open and pinned pressure cases not both exercised\n"); }
  if (!quick && coverage.nonUpwindSchemes == 0) { ++failures; std::printf("  FAIL no non-Upwind convection scheme was exercised\n"); }
  if (!quick && coverage.nonOrthogonal == 0) { ++failures; std::printf("  FAIL no non-orthogonal mesh was exercised\n"); }
  if (coverage.perIterationFieldChecks == 0) { ++failures; std::printf("  FAIL no per-iteration field comparison ran\n"); }
  if (!quick && coverage.longRunCases == 0) { ++failures; std::printf("  FAIL no long-run case (>=50 outer iterations)\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "FULL SOLVE EQUIVALENCE: PASS"
                                    : "FULL SOLVE EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
