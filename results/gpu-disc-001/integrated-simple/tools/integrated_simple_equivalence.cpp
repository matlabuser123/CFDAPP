// GPU-DISC-001M gate -- the PRODUCTION SIMPLE path, GPU discretization on vs off.
//
// Unlike every gate before it, this one drives `SIMPLE::solve` itself. There is
// no ladder and no reimplementation: the only difference between the two runs
// is `SIMPLESettings::enableGpuDiscretization`.
//
// Layers:
//   E  equivalence  one production outer iteration, GPU discretization ON vs
//                   OFF, both with the SAME (CPU) linear solvers. Since every
//                   operator was qualified BITWISE and the solves are
//                   identical, the committed state must be bitwise identical.
//   D  dispatch     the backend actually taken is observable and correct:
//                   CPU stays CPU, GPU reports GPU, an unsupported
//                   configuration falls back and says why.
//   T  transfers    H2D/D2H counts and bytes, kernel launches and
//                   synchronizations for ONE production iteration, measured
//                   with the flag off and on. This is what proves production
//                   now runs CUDA DISCRETIZATION rather than only CUDA linear
//                   algebra.
//   R  determinism  the same configuration repeated gives identical results.
//
// usage: integrated_simple_equivalence [--quick]

#include <algorithm>
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
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

int failures = 0;
int cases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;

struct Coverage {
  int twoD = 0, threeD = 0;
  int equivalenceCases = 0;
  int dispatchChecks = 0;
  int transferMeasurements = 0;
  int determinismChecks = 0;
  int fallbackChecks = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

Mesh warped3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(
      mesh, std::make_shared<cfd::mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                          Vector3{0.05, 0.025, -0.0375},
                                                          cfd::constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  return mesh;
}

struct Case {
  Case(std::string n, Mesh m) : name(std::move(n)), mesh(std::move(m)) {}
  std::string name;
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.2, 1.0e-3};
};

// The canonical closed case: every patch a wall, the last one moving. No
// FixedValue pressure patch, so the reference cell is pinned.
Case lidDrivenCavity(const std::string& name, Mesh mesh) {
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
  return c;
}

// An open case: inlet, outlet with a FixedValue pressure patch, so the pin is
// suppressed and the boundary carries the correction.
Case inletOutlet(const std::string& name, Mesh mesh) {
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
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Symmetry>());
      c.pressureBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    ++i;
  }
  const Index nc = m.numberOfCells();
  c.velocity = VectorField(nc);
  c.pressure = ScalarField(nc);
  const Real pi = cfd::constants::pi;
  for (Index cell = 0; cell < nc; ++cell) {
    const Vector3 x = m.cell(cell).centroid();
    c.velocity[cell] = Vector3{0.8 + (0.2 * std::sin(pi * x.y)), 0.1 * std::sin(pi * x.x),
                               0.05 * std::sin(pi * x.z)};
    c.pressure[cell] = 10.0 * (1.0 - x.x);
  }
  return c;
}

SIMPLESettings settingsFor(bool gpuDiscretization, Index maxIterations = 1) {
  SIMPLESettings s;
  s.maxIterations = maxIterations;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.momentumSolver.absoluteTolerance = 1e-12;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-12;
  s.pressureSolver.relativeTolerance = 1e-12;
  s.pressureSolver.maxIterations = 5000;
  s.enableGpuDiscretization = gpuDiscretization;
  return s;
}

SIMPLEResult run(const Case& c, const SIMPLESettings& settings) {
  SIMPLE simple(settings, /*referenceCell=*/0);
  return simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, c.velocity,
                      c.pressure);
}

struct Difference {
  std::size_t velocity = 0, pressure = 0, flux = 0, residuals = 0;
  Real maxAbs = 0.0;
  // GPU-DISC-001P: where the divergence FIRST appears, not just that it exists.
  // A report that says only "the final result differs" does not localise an
  // integration control, which is the whole point of having one.
  int firstIteration = -1;
  const char* firstStage = "";
  const char* firstMetric = "";
};

// The per-outer-iteration residual histories, attributed to the SIMPLE stage
// that produces each one. The EARLIEST divergence across all five wins, so the
// answer does not depend on comparison order; a genuine tie keeps the earlier
// stage, which is the order they are compared in.
void locate(Difference& d, const std::vector<Real>& a, const std::vector<Real>& b,
            const char* metric, const char* stage) {
  const std::size_t n = std::min(a.size(), b.size());
  int divergent = -1;
  for (std::size_t i = 0; i < n; ++i) {
    if (!sameBits(a[i], b[i]) && divergent < 0) divergent = static_cast<int>(i) + 1;
  }
  if (a.size() != b.size() && divergent < 0) divergent = static_cast<int>(n) + 1;
  if (divergent > 0 && (d.firstIteration < 0 || divergent < d.firstIteration)) {
    d.firstIteration = divergent;
    d.firstStage = stage;
    d.firstMetric = metric;
  }
}

Difference compare(const Mesh& mesh, const SIMPLEResult& a, const SIMPLEResult& b) {
  Difference d;
  const auto note = [&](Real x, Real y, std::size_t& counter) {
    ++valuesCompared;
    if (sameBits(x, y)) ++bitwiseValues; else ++counter;
    d.maxAbs = std::max(d.maxAbs, std::abs(x - y));
  };
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    note(a.velocity[i].x, b.velocity[i].x, d.velocity);
    note(a.velocity[i].y, b.velocity[i].y, d.velocity);
    note(a.velocity[i].z, b.velocity[i].z, d.velocity);
    note(a.pressure[i], b.pressure[i], d.pressure);
  }
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) note(a.massFlux[f], b.massFlux[f], d.flux);
  note(a.finalUResidual, b.finalUResidual, d.residuals);
  note(a.finalVResidual, b.finalVResidual, d.residuals);
  note(a.finalWResidual, b.finalWResidual, d.residuals);
  note(a.finalPressureResidual, b.finalPressureResidual, d.residuals);
  note(a.finalContinuityResidual, b.finalContinuityResidual, d.residuals);
  note(a.globalMassImbalance, b.globalMassImbalance, d.residuals);
  locate(d, a.uResidualHistory, b.uResidualHistory, "u residual", "momentum solve");
  locate(d, a.vResidualHistory, b.vResidualHistory, "v residual", "momentum solve");
  locate(d, a.wResidualHistory, b.wResidualHistory, "w residual", "momentum solve");
  locate(d, a.pressureResidualHistory, b.pressureResidualHistory, "pressure residual",
         "pressure-correction solve");
  locate(d, a.continuityHistory, b.continuityHistory, "continuity",
         "velocity / face-flux correction -> continuity");
  return d;
}

struct Transfers {
  std::uint64_t h2dCalls = 0, h2dBytes = 0, d2hCalls = 0, d2hBytes = 0;
  std::uint64_t kernels = 0, syncs = 0, allocations = 0;
};

Transfers measure(const Case& c, const SIMPLESettings& settings) {
  cfd::gpu::resetGpuExecutionStats();
  (void)run(c, settings);
  const auto& s = cfd::gpu::gpuExecutionStats();
  return Transfers{s.hostToDeviceCalls, s.hostToDeviceBytes, s.deviceToHostCalls,
                   s.deviceToHostBytes, s.kernelLaunches,    s.synchronizations,
                   s.allocations};
}

// ---------------------------------------------------------------------------

void runCase(const Case& c) {
  const bool threeD = c.mesh.dimension() == 3;
  if (threeD) ++coverage.threeD; else ++coverage.twoD;

  const auto cpuSettings = settingsFor(false);
  const auto gpuSettings = settingsFor(true);

  const SIMPLEResult cpu = run(c, cpuSettings);
  const SIMPLEResult gpu = run(c, gpuSettings);

  // --- D: dispatch -------------------------------------------------------
  {
    std::size_t errors = 0;
    // The CPU run must never claim a GPU discretization, and must record no
    // fallback reason -- it never asked.
    if (cpu.gpuDiscretization) ++errors;
    if (!cpu.gpuDiscretizationFallbackReason.empty()) ++errors;
    // The GPU run must report what actually happened, and on a CUDA build with
    // a usable device that must be `true`.
    if (cfd::gpu::cudaAvailable()) {
      if (!gpu.gpuDiscretization) ++errors;
      if (!gpu.gpuDiscretizationFallbackReason.empty()) ++errors;
    } else {
      if (gpu.gpuDiscretization) ++errors;
      if (gpu.gpuDiscretizationFallbackReason.empty()) ++errors;
    }
    ++cases;
    ++coverage.dispatchChecks;
    if (errors != 0) ++failures;
    std::printf("  %s D  %-18s cpu[gpuDisc=%d] gpu[gpuDisc=%d reason='%s'] errors=%zu\n",
                errors == 0 ? "PASS" : "FAIL", c.name.c_str(),
                static_cast<int>(cpu.gpuDiscretization), static_cast<int>(gpu.gpuDiscretization),
                gpu.gpuDiscretizationFallbackReason.c_str(), errors);
  }

  // --- E: equivalence ----------------------------------------------------
  {
    const Difference d = compare(c.mesh, cpu, gpu);
    ++cases;
    ++coverage.equivalenceCases;
    const bool ok = d.velocity == 0 && d.pressure == 0 && d.flux == 0 && d.residuals == 0 &&
                    cpu.status == gpu.status && cpu.iterations == gpu.iterations;
    if (!ok) ++failures;
    std::printf("  %s E  %-18s u[d=%zu] p[d=%zu] flux[d=%zu] residuals[d=%zu] maxAbs=%-10.3g "
                "status=%d/%d iterations=%zu/%zu\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), d.velocity, d.pressure, d.flux, d.residuals,
                d.maxAbs, static_cast<int>(cpu.status), static_cast<int>(gpu.status),
                static_cast<std::size_t>(cpu.iterations),
                static_cast<std::size_t>(gpu.iterations));
    if (!ok) {
      std::printf("       first divergent iteration: %s\n",
                  d.firstIteration < 0 ? "none (histories agree; the divergence is in the final "
                                         "state or the status)"
                                       : std::to_string(d.firstIteration).c_str());
      if (d.firstIteration > 0) {
        std::printf("       first divergent stage: %s   first divergent metric: %s\n",
                    d.firstStage, d.firstMetric);
      }
    }
  }

  // --- T: transfers ------------------------------------------------------
  {
    const Transfers off = measure(c, cpuSettings);
    const Transfers on = measure(c, gpuSettings);
    // One solve's traffic is dominated by prepare()'s one-time plan upload, so
    // a single number would misrepresent the steady state. Measuring five
    // iterations as well makes the MARGINAL per-iteration cost derivable:
    // (five - one) / 4. That is the number GPU-PIPE-001 will later be judged
    // on, and reporting only the total would hide it.
    const Transfers on5 = measure(c, settingsFor(true, 5));
    const auto marginal = [](std::uint64_t five, std::uint64_t one) {
      return five >= one ? (five - one) / 4 : 0ULL;
    };
    ++cases;
    ++coverage.transferMeasurements;
    // The point of the measurement: with the flag off, a default solve issues
    // NO device work at all (the linear solvers are CPU here). With it on, the
    // operators run on the device, so kernels and transfers must appear. If
    // they did not, production would not actually be using CUDA
    // discretization -- which is exactly the claim this gate has to prove.
    const bool expectDevice = cfd::gpu::cudaAvailable();
    const bool ok = !expectDevice || (on.kernels > off.kernels && on.h2dCalls > off.h2dCalls);
    if (!ok) ++failures;
    std::printf("  %s T  %-18s off[h2d=%llu/%lluB d2h=%llu/%lluB kern=%llu sync=%llu alloc=%llu]\n",
                ok ? "PASS" : "FAIL", c.name.c_str(),
                static_cast<unsigned long long>(off.h2dCalls),
                static_cast<unsigned long long>(off.h2dBytes),
                static_cast<unsigned long long>(off.d2hCalls),
                static_cast<unsigned long long>(off.d2hBytes),
                static_cast<unsigned long long>(off.kernels),
                static_cast<unsigned long long>(off.syncs),
                static_cast<unsigned long long>(off.allocations));
    std::printf("       %-18s on [h2d=%llu/%lluB d2h=%llu/%lluB kern=%llu sync=%llu alloc=%llu]\n",
                "", static_cast<unsigned long long>(on.h2dCalls),
                static_cast<unsigned long long>(on.h2dBytes),
                static_cast<unsigned long long>(on.d2hCalls),
                static_cast<unsigned long long>(on.d2hBytes),
                static_cast<unsigned long long>(on.kernels),
                static_cast<unsigned long long>(on.syncs),
                static_cast<unsigned long long>(on.allocations));
    std::printf("       %-18s marginal per extra iteration: h2d=%llu d2h=%llu kern=%llu "
                "alloc=%llu\n",
                "", static_cast<unsigned long long>(marginal(on5.h2dCalls, on.h2dCalls)),
                static_cast<unsigned long long>(marginal(on5.d2hCalls, on.d2hCalls)),
                static_cast<unsigned long long>(marginal(on5.kernels, on.kernels)),
                static_cast<unsigned long long>(marginal(on5.allocations, on.allocations)));
  }

  // --- R: determinism ----------------------------------------------------
  {
    const SIMPLEResult again = run(c, gpuSettings);
    const Difference d = compare(c.mesh, gpu, again);
    ++cases;
    ++coverage.determinismChecks;
    const bool ok = d.velocity == 0 && d.pressure == 0 && d.flux == 0 && d.residuals == 0;
    if (!ok) ++failures;
    std::printf("  %s R  %-18s GPU discretization repeated, identical: %s\n",
                ok ? "PASS" : "FAIL", c.name.c_str(), ok ? "yes" : "NO");
  }
}

// An unsupported configuration must FALL BACK and say why -- never run a mixed
// path, and never claim a GPU discretization it did not perform.
void runFallback(const Case& c) {
  auto settings = settingsFor(true);
  // The corrector passes stay on the CPU by design (their pass loops call
  // CPU-only helpers), so this is the documented decline path.
  settings.nonOrthogonalCorrections = 2;
  const SIMPLEResult result = run(c, settings);
  const SIMPLEResult reference = run(c, [&] {
    auto s = settingsFor(false);
    s.nonOrthogonalCorrections = 2;
    return s;
  }());
  const Difference d = compare(c.mesh, reference, result);
  ++cases;
  ++coverage.fallbackChecks;
  const bool ok = !result.gpuDiscretization && !result.gpuDiscretizationFallbackReason.empty() &&
                  d.velocity == 0 && d.pressure == 0 && d.flux == 0;
  if (!ok) ++failures;
  std::printf("  %s F  %-18s unsupported config -> gpuDisc=%d reason='%s' matches CPU: %s\n",
              ok ? "PASS" : "FAIL", c.name.c_str(), static_cast<int>(result.gpuDiscretization),
              result.gpuDiscretizationFallbackReason.c_str(),
              (d.velocity == 0 && d.pressure == 0 && d.flux == 0) ? "yes" : "NO");
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001M: production SIMPLE, GPU discretization on vs off ===\n");
  std::printf("CUDA available: %s\n\n", cfd::gpu::cudaAvailable() ? "yes" : "no");

  runCase(lidDrivenCavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)));
  runCase(inletOutlet("channel 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)));
  runCase(lidDrivenCavity("cavity 3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)));
  if (!quick) {
    runCase(lidDrivenCavity("cavity 2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)));
    runCase(inletOutlet("channel 3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)));
    runCase(inletOutlet("warped 3d 3", warped3D(3)));
    // Several outer iterations, so the plans are reused rather than rebuilt.
    {
      const Case c = lidDrivenCavity("cavity 2d 16 x5",
                                     MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
      const SIMPLEResult cpu = run(c, settingsFor(false, 5));
      const SIMPLEResult gpu = run(c, settingsFor(true, 5));
      const Difference d = compare(c.mesh, cpu, gpu);
      ++cases;
      const bool ok = d.velocity == 0 && d.pressure == 0 && d.flux == 0 && d.residuals == 0 &&
                      cpu.iterations == gpu.iterations;
      if (!ok) ++failures;
      std::printf("  %s E  %-18s 5 outer iterations u[d=%zu] p[d=%zu] flux[d=%zu] "
                  "iterations=%zu/%zu\n",
                  ok ? "PASS" : "FAIL", c.name.c_str(), d.velocity, d.pressure, d.flux,
                  static_cast<std::size_t>(cpu.iterations),
                  static_cast<std::size_t>(gpu.iterations));
    }
  }
  runFallback(lidDrivenCavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)));

  std::printf("\n=== coverage ===\n");
  std::printf("  2D / 3D cases                     %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  equivalence cases                 %d\n", coverage.equivalenceCases);
  std::printf("  dispatch checks                   %d\n", coverage.dispatchChecks);
  std::printf("  transfer measurements             %d\n", coverage.transferMeasurements);
  std::printf("  determinism checks                %d\n", coverage.determinismChecks);
  std::printf("  documented-fallback checks        %d\n", coverage.fallbackChecks);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical                 %zu\n", bitwiseValues);

  if (coverage.twoD == 0 || coverage.threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (coverage.dispatchChecks == 0) { ++failures; std::printf("  FAIL dispatch was never checked\n"); }
  if (coverage.fallbackChecks == 0) { ++failures; std::printf("  FAIL the documented fallback was never checked\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "INTEGRATED SIMPLE EQUIVALENCE: PASS (bitwise)"
                                    : "INTEGRATED SIMPLE EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
