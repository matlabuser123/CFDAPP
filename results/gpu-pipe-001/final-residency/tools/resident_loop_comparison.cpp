// GPU-PIPE-001 Final Residency, Part 3 -- the controlled comparison.
//
// One binary, one build, one mesh, one set of physics. The two arms differ in
// exactly one thing: whether the GPU SIMPLE outer iteration runs device-
// resident. Everything else -- discretization, linear solvers, tolerances,
// relaxation, iteration budget, initial state -- is identical.
//
// THE TOGGLE, and why it is numerically inert.
//
// The resident loop is declined when the turbulence model is not the laminar
// one, because `activeModel->correct(mesh, velocity, pressure)` reads the HOST
// velocity, which a resident loop deliberately leaves stale (audit.md section
// 6). `InertModel` below is LaminarModel with a different name(): mu_t = 0 in
// every cell, correct() a documented no-op, convergenceResidual() nullopt. It
// changes the DECISION and nothing else, so the two arms are the same solve.
//
// This is the same discipline the resident pressure gate used: the toggle is a
// real production condition exercised through its real code path, not a test
// hook bolted on beside it.
//
// NON-VACUITY. A comparison between two identical paths passes for free.
// `SIMPLEResult::residentSimpleLoop` is asserted to be TRUE on one arm and
// FALSE on the other, on every case, or the case fails. Negative control
// `rl8` exists to prove that assertion can fail.
//
// usage: resident_loop_comparison [--quick]
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
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
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

int failures = 0;
int cases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// LaminarModel's numerics with a different name. See the header comment.
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
  bool threeD = false;
};

SIMPLESettings baseSettings(Index outer) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  // The production tolerances of tests/solver/simple/test_simple_gpu_solver.cpp.
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::GPU;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.backend = LinearSolverBackend::GPU;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
  s.enableGpuDiscretization = true;
  return s;
}

Case cavity(std::string name, Mesh mesh, Index outer,
            ConvectionScheme scheme = ConvectionScheme::Upwind) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01},
         baseSettings(outer), false};
  const auto& m = c.mesh;
  c.threeD = m.dimension() == 3;
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
         baseSettings(outer), false};
  const auto& m = c.mesh;
  c.threeD = m.dimension() == 3;
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

struct Run {
  SIMPLEResult result;
  std::uint64_t h2d = 0, h2dBytes = 0, d2h = 0, d2hBytes = 0;
  std::uint64_t allocations = 0, reallocations = 0, syncs = 0, reductions = 0;
};

Run run(const Case& c, bool resident) {
  InertModel inert(c.mesh);
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(c.settings, /*referenceCell=*/0, resident ? nullptr : &inert);
  Run r;
  r.result = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
  const auto& s = cfd::gpu::gpuExecutionStats();
  r.h2d = s.hostToDeviceCalls;
  r.h2dBytes = s.hostToDeviceBytes;
  r.d2h = s.deviceToHostCalls;
  r.d2hBytes = s.deviceToHostBytes;
  r.allocations = s.allocations;
  r.reallocations = s.reallocations;
  r.syncs = s.synchronizations;
  r.reductions = s.reductionGroups;
  return r;
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

// Every comparison is BITWISE. The two arms run the same algorithm on the same
// bits through the same kernels; anything less than bitwise would mean the
// residency change altered the arithmetic, which is the thing being ruled out.
int compareHistory(const std::vector<Real>& a, const std::vector<Real>& b) {
  if (a.size() != b.size()) return 1;
  for (std::size_t i = 0; i < a.size(); ++i) {
    ++valuesCompared;
    if (sameBits(a[i], b[i])) { ++bitwiseValues; continue; }
    return static_cast<int>(i) + 1;
  }
  return 0;
}

void runCase(const Case& c) {
  const Run host = run(c, /*resident=*/false);
  const Run res = run(c, /*resident=*/true);
  ++cases;

  // --- non-vacuity: each arm must have taken the path it was meant to -----
  const bool armsDiffer = res.result.residentSimpleLoop && !host.result.residentSimpleLoop;
  if (!armsDiffer) {
    ++failures;
    std::printf("  FAIL %-24s arms did not differ: resident=%d host=%d -- the comparison would "
                "be vacuous\n",
                c.name.c_str(), static_cast<int>(res.result.residentSimpleLoop),
                static_cast<int>(host.result.residentSimpleLoop));
    return;
  }
  // Both arms must still have had the resident PRESSURE solve, or this
  // comparison is measuring two changes at once.
  if (!res.result.residentPressureSolve || !host.result.residentPressureSolve) {
    ++failures;
    std::printf("  FAIL %-24s the resident pressure solve was not engaged on both arms\n",
                c.name.c_str());
    return;
  }

  int errors = 0;
  const char* firstStage = "none";
  int firstDivergent = 0;
  const auto note = [&](int where, const char* stage) {
    if (where != 0 && (firstDivergent == 0 || where < firstDivergent)) {
      firstDivergent = where;
      firstStage = stage;
    }
    if (where != 0) ++errors;
  };

  if (host.result.status != res.result.status) { ++errors; firstStage = "status"; }
  if (host.result.iterations != res.result.iterations) { ++errors; firstStage = "iteration count"; }
  note(compareHistory(host.result.uResidualHistory, res.result.uResidualHistory),
       "momentum solve (u residual)");
  note(compareHistory(host.result.vResidualHistory, res.result.vResidualHistory),
       "momentum solve (v residual)");
  note(compareHistory(host.result.wResidualHistory, res.result.wResidualHistory),
       "momentum solve (w residual)");
  note(compareHistory(host.result.pressureResidualHistory, res.result.pressureResidualHistory),
       "pressure solve");
  note(compareHistory(host.result.continuityHistory, res.result.continuityHistory),
       "face-flux correction -> continuity");

  // Final fields, bitwise.
  std::size_t fieldMismatch = 0;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    valuesCompared += 4;
    if (sameBits(host.result.pressure[i], res.result.pressure[i])) ++bitwiseValues;
    else ++fieldMismatch;
    if (sameBits(host.result.velocity[i].x, res.result.velocity[i].x)) ++bitwiseValues;
    else ++fieldMismatch;
    if (sameBits(host.result.velocity[i].y, res.result.velocity[i].y)) ++bitwiseValues;
    else ++fieldMismatch;
    if (sameBits(host.result.velocity[i].z, res.result.velocity[i].z)) ++bitwiseValues;
    else ++fieldMismatch;
  }
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    ++valuesCompared;
    if (sameBits(host.result.massFlux[f], res.result.massFlux[f])) ++bitwiseValues;
    else ++fieldMismatch;
  }
  if (fieldMismatch != 0) { ++errors; if (firstDivergent == 0) firstStage = "final fields"; }

  // The same KRYLOV WORK, not merely the same answer. Equal linear-iteration
  // totals and equal reduction-group counts say both arms ran the same number
  // of BiCGSTAB iterations through the same reductions.
  const bool sameWork =
      host.result.momentumLinearIterations == res.result.momentumLinearIterations &&
      host.result.pressureLinearIterations == res.result.pressureLinearIterations &&
      host.reductions == res.reductions;
  if (!sameWork) { ++errors; if (firstDivergent == 0) firstStage = "Krylov work"; }

  if (errors != 0) ++failures;
  std::printf("  %s  %-24s %s it=%lld  mom=%lld p=%lld  red=%llu  fields=%s  first divergence: %s\n",
              errors == 0 ? "PASS" : "FAIL", c.name.c_str(), statusName(res.result.status),
              static_cast<long long>(res.result.iterations),
              static_cast<long long>(res.result.momentumLinearIterations),
              static_cast<long long>(res.result.pressureLinearIterations),
              (unsigned long long)res.reductions,
              fieldMismatch == 0 ? "bitwise" : "DIFFER",
              errors == 0 ? "none"
                          : (firstDivergent ? (std::to_string(firstDivergent) + " (" +
                                               firstStage + ")").c_str()
                                            : firstStage));

  const double n = static_cast<double>(res.result.iterations > 0 ? res.result.iterations : 1);
  std::printf("        transfers/iteration   host-arm H2D %7.2f /%11.0f B  D2H %8.2f /%12.0f B  "
              "sync %8.2f  alloc %llu\n",
              host.h2d / n, host.h2dBytes / n, host.d2h / n, host.d2hBytes / n, host.syncs / n,
              (unsigned long long)host.allocations);
  std::printf("        transfers/iteration   resident H2D %7.2f /%11.0f B  D2H %8.2f /%12.0f B  "
              "sync %8.2f  alloc %llu\n",
              res.h2d / n, res.h2dBytes / n, res.d2h / n, res.d2hBytes / n, res.syncs / n,
              (unsigned long long)res.allocations);
  std::printf("        non-reduction D2H/iteration   host-arm %7.2f   resident %7.2f\n",
              (host.d2h - host.reductions) / n, (res.d2h - res.reductions) / n);
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-PIPE-001 final residency: resident SIMPLE loop, controlled comparison ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }
  std::printf("Arms differ ONLY in whether the GPU outer iteration is device-resident.\n");
  std::printf("Every comparison is BITWISE.\n\n");

  runCase(cavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), 8));
  runCase(cavity("cavity 2d 32", MeshGeometry::createCartesian2D(32, 32, 1.0, 1.0), 6));
  runCase(inletOutlet("channel 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), 8));
  runCase(cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0), 6));
  if (!quick) {
    runCase(cavity("cavity 2d 80", MeshGeometry::createCartesian2D(80, 80, 1.0, 1.0), 6));
    runCase(cavity("cavity 2d 160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0), 4));
    runCase(cavity("cavity 2d 16 quick", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), 8,
                   ConvectionScheme::QUICK));
    runCase(cavity("cavity 2d 16 linup", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), 8,
                   ConvectionScheme::LinearUpwind));
    runCase(inletOutlet("channel 2d 16 central", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                        8, ConvectionScheme::Central));
    runCase(cavity("cavity 3d 8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0), 5));
    // The LONG run. Residency state persists across outer iterations, so a
    // fault that accumulates -- a buffer never reset, a guess quietly inherited
    // -- needs iterations to surface. Bitwise after 60 is a far stronger
    // statement than bitwise after 4.
    runCase(cavity("cavity 2d 16 long", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), 60));
    runCase(inletOutlet("channel 2d 24 long", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0),
                        60));
  }

  std::printf("\ncases=%d failures=%d  values compared=%zu  bitwise=%zu\n", cases, failures,
              valuesCompared, bitwiseValues);
  if (valuesCompared != bitwiseValues) {
    std::printf("NOT every compared value was bitwise identical\n");
  }
  std::printf("%s\n", failures == 0 ? "RESIDENT SIMPLE LOOP COMPARISON: PASS (bitwise)"
                                    : "RESIDENT SIMPLE LOOP COMPARISON: FAIL");
  return failures == 0 ? 0 : 1;
}
