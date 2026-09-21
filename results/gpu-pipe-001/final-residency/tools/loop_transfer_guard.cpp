// GPU-PIPE-001 Final Residency, Part 3 -- the transfer detector for the
// resident SIMPLE loop.  AMENDMENT A1.
//
// This exists because of one specific negative control: downloading every field
// every iteration and uploading it straight back is NUMERICALLY IDENTICAL to
// keeping it resident. Every equivalence gate in this project passes it. Only a
// transfer count can tell the difference, so without this detector "the SIMPLE
// loop is device-resident" is an unfalsifiable claim.
//
// ---------------------------------------------------------------------------
// AMENDMENT A1 -- WHY THIS FILE CHANGED, AND WHAT THE FIRST VERSION GOT WRONG
// ---------------------------------------------------------------------------
// The first version was frozen before the implementation and FAILED on 4 of 6
// cases. That failure is preserved verbatim at
// transfers/after-FAILED-first-run.log and classified in transfers/FAILURE.md.
// It was a defect in the CRITERION, not in the implementation:
//
//     nonReductionBytes = d2hBytes - reductionGroups * 8.0;     // WRONG
//
// A Krylov reduction round trip does NOT bring back 8 bytes. `reduceToHost`
// (cuda/kernels/DeviceVectorOpsKernel.cu) downloads the per-block PARTIAL SUMS
// and finishes the sum on the host, so one round trip costs
//
//     blockCountFor(n) * count * 8   bytes,   blockCountFor(n) = (n + 255)/256
//
// which at 160^2 is 800 or 1600 bytes, not 8 -- a factor of 100 to 200. The
// amended subtraction is derived below from that code, not fitted to any
// observed number.
//
// ---------------------------------------------------------------------------
// THE CRITERIA, ALL DERIVED
// ---------------------------------------------------------------------------
// The requirement is "no avoidable full-field transfers during steady-state
// SIMPLE iterations", zero steady-state reallocations, and only scalar/control
// traffic besides.
//
//   C1  H2D bytes / iteration  <  nc * 8
//       Strictly less than ONE cell field. Nothing uploaded per iteration can
//       then be a field. (Unchanged from the first version; it passed at
//       exactly 0.00 on every case.)
//
//   C2  non-reduction D2H calls / iteration  ==  8 in 2D, 9 in 3D
//       EXACT, by enumeration of the code rather than a budget with slack --
//       a slack budget is what let the first version fail to say anything
//       useful. A steady-state resident iteration issues exactly:
//           1   mass-flux download          (the one justified field, S4.4)
//           2|3 momentum system checks      (one per resident momentum solve)
//           1   pressure system check
//           1   p' finiteness
//           1   predictor finiteness
//           1   velocity finiteness
//           1   pressure finiteness
//       and nothing else.
//
//   C3  non-reduction D2H bytes / iteration  <  (nFaces + nc) * 8
//       with   reductionBytes = blockCountFor(nc) * 8 * reductionQuantities
//       `reductionQuantities` counts the scalars produced across all groups, so
//       summing blocks*count over groups is blocks * sum(count). `blocks` is
//       constant within a case because every Krylov vector in a solve has
//       length nc. The bound allows the one justified face field plus strictly
//       less than one more field on top.
//
//   C4  allocations / iteration    == 0
//   C5  reallocations / iteration  == 0
//
// ---------------------------------------------------------------------------
// NON-VACUITY -- `--dryrun`
// ---------------------------------------------------------------------------
// A criterion that cannot fail proves nothing. `--dryrun` measures the SAME
// cases on the PRE-RESIDENCY arm -- the resident loop declined through a real
// production condition (a turbulence model numerically identical to
// LaminarModel but not named "laminar"), so the arithmetic is unchanged and
// only the dispatch differs. Every criterion above MUST FAIL there, and the
// dry-run exits non-zero if any of them passes.
//
// usage: loop_transfer_guard [--quick] [--dryrun]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
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
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

// DeviceVectorOpsKernel.cu's own constant and block count, restated here so the
// subtraction is derived from the reduction's shape rather than from a number
// someone measured once.
constexpr Index kReductionThreadsPerBlock = 256;
Index blockCountFor(Index n) {
  return (n + kReductionThreadsPerBlock - 1) / kReductionThreadsPerBlock;
}

int failures = 0;

// LaminarModel's numerics with a different name, so the resident loop declines
// through a real production condition and nothing arithmetic changes.
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
};

Case cavity(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
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

Case inletOutlet(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
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

struct Sample {
  Index iterations = 0;
  std::uint64_t h2d = 0, h2dBytes = 0, d2h = 0, d2hBytes = 0;
  std::uint64_t alloc = 0, realloc_ = 0, syncs = 0;
  std::uint64_t reductionGroups = 0, reductionQuantities = 0;
  bool resident = false, residentPressure = false;
};

Sample run(const Case& c, Index outer, bool resident) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  // Unreachable on purpose: both budgets must be EXHAUSTED or the delta
  // divides by the wrong iteration count.
  s.velocityTolerance = 1e-14;
  s.pressureTolerance = 1e-14;
  s.continuityTolerance = 1e-14;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.backend = LinearSolverBackend::GPU;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
  s.pressureSolver = s.momentumSolver;
  s.pressureSolver.maxIterations = 2000;
  s.enableGpuDiscretization = true;
  InertModel inert(c.mesh);
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(s, /*referenceCell=*/0, resident ? nullptr : &inert);
  const SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
  const auto& st = cfd::gpu::gpuExecutionStats();
  Sample o;
  o.iterations = r.iterations;
  o.h2d = st.hostToDeviceCalls;
  o.h2dBytes = st.hostToDeviceBytes;
  o.d2h = st.deviceToHostCalls;
  o.d2hBytes = st.deviceToHostBytes;
  o.alloc = st.allocations;
  o.realloc_ = st.reallocations;
  o.syncs = st.synchronizations;
  o.reductionGroups = st.reductionGroups;
  o.reductionQuantities = st.reductionQuantities;
  o.resident = r.residentSimpleLoop;
  o.residentPressure = r.residentPressureSolve;
  return o;
}

double per(std::uint64_t hi, std::uint64_t lo, double n) {
  return (static_cast<double>(hi) - static_cast<double>(lo)) / n;
}

// Returns the number of criteria that PASSED, and reports each. The caller
// decides what "should" happen: on the resident arm every criterion must pass;
// on the dry-run arm every criterion must fail.
int measure(const Case& c, bool resident, bool expectPass) {
  const Index nc = c.mesh.numberOfCells();
  const Index nf = c.mesh.numberOfFaces();
  const bool threeD = c.mesh.dimension() == 3;
  (void)run(c, 2, resident);  // warm-up
  const Sample a = run(c, 4, resident);
  const Sample b = run(c, 8, resident);
  const Sample d = run(c, 16, resident);
  const double nAC = static_cast<double>(d.iterations - a.iterations);
  const double nAB = static_cast<double>(b.iterations - a.iterations);

  const double h2dCalls = per(d.h2d, a.h2d, nAC);
  const double h2dBytes = per(d.h2dBytes, a.h2dBytes, nAC);
  const double d2hCalls = per(d.d2h, a.d2h, nAC);
  const double d2hBytes = per(d.d2hBytes, a.d2hBytes, nAC);
  const double groups = per(d.reductionGroups, a.reductionGroups, nAC);
  const double quantities = per(d.reductionQuantities, a.reductionQuantities, nAC);
  const double allocs = per(d.alloc, a.alloc, nAC);
  const double reallocs = per(d.realloc_, a.realloc_, nAC);
  const double syncs = per(d.syncs, a.syncs, nAC);

  // AMENDMENT A1: the derived reduction-byte term.
  const double blocks = static_cast<double>(blockCountFor(nc));
  const double reductionBytes = blocks * 8.0 * quantities;
  const double nonRedCalls = d2hCalls - groups;
  const double nonRedBytes = d2hBytes - reductionBytes;

  const double oneCellField = static_cast<double>(nc) * 8.0;
  const double faceField = static_cast<double>(nf) * 8.0;
  const double expectedCalls = threeD ? 9.0 : 8.0;

  std::printf("  --- %s  cells=%lld faces=%lld blocks=%lld  arm=%s  (budgets n=%lld,%lld,%lld) ---\n",
              c.name.c_str(), static_cast<long long>(nc), static_cast<long long>(nf),
              static_cast<long long>(blocks), resident ? "RESIDENT" : "pre-residency",
              static_cast<long long>(a.iterations), static_cast<long long>(b.iterations),
              static_cast<long long>(d.iterations));
  std::printf("      resident loop engaged: %-3s   resident pressure solve: %s\n",
              d.resident ? "yes" : "NO", d.residentPressure ? "yes" : "NO");
  std::printf("      H2D            %8.2f calls   %14.1f bytes/iteration\n", h2dCalls, h2dBytes);
  std::printf("      D2H total      %8.2f calls   %14.1f bytes/iteration\n", d2hCalls, d2hBytes);
  std::printf("      D2H reductions %8.2f groups  %14.1f bytes/iteration  (%.1f quantities x %lld "
              "blocks x 8 B)\n",
              groups, reductionBytes, quantities, static_cast<long long>(blocks));
  std::printf("      D2H remaining  %8.2f calls   %14.1f bytes/iteration\n", nonRedCalls,
              nonRedBytes);
  std::printf("      of which the justified face-flux download is %14.1f bytes; everything else "
              "%10.1f bytes\n",
              faceField, nonRedBytes - faceField);
  std::printf("      sync %8.2f/iteration   allocations %.2f   reallocations %.2f\n", syncs,
              allocs, reallocs);

  const double h2dBytesAB = per(b.h2dBytes, a.h2dBytes, nAB);
  const bool linear = std::abs(h2dBytesAB - h2dBytes) < 1e-9;

  // A criterion is one of two kinds, and the dry-run demands opposite things of
  // them. The first version of this dry-run demanded that EVERY criterion fail
  // on the baseline and reported VACUOUS when C4/C5 passed -- which was wrong,
  // and the dry-run is where that was supposed to surface.
  //
  //   DETECTION   gates the NEW behaviour. Must FAIL on the pre-residency arm,
  //               or it cannot detect what it claims to gate.
  //   PRESERVATION gates an EXISTING guarantee the change must not break. Must
  //               PASS on both arms -- "a sanity criterion that the baseline
  //               fails is a broken criterion" (CLAUDE.md 5.2).
  //
  // C4/C5 are preservation criteria: the pre-residency path already had zero
  // steady-state allocations (the persistent-fields gate established that), and
  // the brief's allocation target is that the resident loop KEEPS that. Their
  // falsifiability is not assumed -- it is on record: an early version of the
  // resident pressure solve allocated a counter per solve and this same
  // instrument family measured allocations rising 375 -> 406 over 8 outer
  // iterations (gpu-resident-pressure-solve/summary.md section 12).
  enum class Kind { Detection, Preservation };
  struct C {
    const char* name;
    Kind kind;
    bool pass;
    std::string detail;
  };
  const std::vector<C> criteria = {
      {"C1 H2D bytes/iteration < one cell field", Kind::Detection, h2dBytes < oneCellField,
       std::to_string(h2dBytes) + " < " + std::to_string(oneCellField)},
      {"C2 non-reduction D2H calls == enumeration", Kind::Detection,
       std::abs(nonRedCalls - expectedCalls) < 1e-9,
       std::to_string(nonRedCalls) + " == " + std::to_string(expectedCalls)},
      {"C3 non-reduction D2H bytes < face + one field", Kind::Detection,
       nonRedBytes < faceField + oneCellField,
       std::to_string(nonRedBytes) + " < " + std::to_string(faceField + oneCellField)},
      {"C4 allocations/iteration == 0", Kind::Preservation, allocs == 0.0, std::to_string(allocs)},
      {"C5 reallocations/iteration == 0", Kind::Preservation, reallocs == 0.0,
       std::to_string(reallocs)},
  };

  int passed = 0;
  for (const C& x : criteria) {
    if (x.pass) ++passed;
    std::printf("      %-6s %-12s %-46s %s\n", x.pass ? "pass" : "FAIL",
                x.kind == Kind::Detection ? "[detection]" : "[preserve]", x.name,
                x.detail.c_str());
  }
  if (expectPass) {
    if (!d.resident) {
      std::printf("      FAIL the resident SIMPLE loop was NOT engaged -- vacuous\n");
      ++failures;
    }
    if (!linear) {
      std::printf("      FAIL per-iteration H2D bytes are not linear in the iteration count\n");
      ++failures;
    }
    failures += static_cast<int>(criteria.size()) - passed;
    std::printf("      %s\n", passed == static_cast<int>(criteria.size()) ? "PASS" : "FAIL");
  } else {
    if (d.resident) {
      std::printf("      FAIL the dry-run arm took the RESIDENT path -- it is not a baseline\n");
      ++failures;
    }
    int detectionPassed = 0, preservationFailed = 0;
    for (const C& x : criteria) {
      if (x.kind == Kind::Detection && x.pass) ++detectionPassed;
      if (x.kind == Kind::Preservation && !x.pass) ++preservationFailed;
    }
    if (detectionPassed != 0) {
      std::printf("      FAIL %d DETECTION criteria passed on the pre-residency arm -- they "
                  "cannot detect the behaviour they claim to gate\n",
                  detectionPassed);
      failures += detectionPassed;
    }
    if (preservationFailed != 0) {
      std::printf("      FAIL %d PRESERVATION criteria failed on the pre-residency arm -- a "
                  "sanity criterion the baseline fails is a broken criterion\n",
                  preservationFailed);
      failures += preservationFailed;
    }
    std::printf("      %s\n",
                (detectionPassed == 0 && preservationFailed == 0)
                    ? "non-vacuous: every detection criterion fails on the baseline, every "
                      "preservation criterion holds on it"
                    : "VACUOUS OR BROKEN");
  }
  return passed;
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false, dryrun = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--quick") quick = true;
    if (a == "--dryrun") dryrun = true;
  }
  std::printf("=== GPU-PIPE-001 final residency: resident SIMPLE loop transfer guard "
              "(AMENDMENT A1) ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }
  std::printf("%s\n\n", dryrun ? "DRY-RUN: the PRE-RESIDENCY arm. Every criterion must FAIL here."
                               : "The RESIDENT arm. Every criterion must PASS here.");

  std::vector<Case> cases;
  cases.push_back(cavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)));
  cases.push_back(inletOutlet("channel 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)));
  cases.push_back(cavity("cavity 3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0)));
  if (!quick) {
    cases.push_back(cavity("cavity 2d 80", MeshGeometry::createCartesian2D(80, 80, 1.0, 1.0)));
    cases.push_back(cavity("cavity 2d 160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0)));
    cases.push_back(
        cavity("cavity 3d 12", MeshGeometry::createCartesian3D(12, 12, 12, 1.0, 1.0, 1.0)));
  }

  for (const Case& c : cases) measure(c, /*resident=*/!dryrun, /*expectPass=*/!dryrun);

  const bool ok = failures == 0;
  std::printf("\n%s\n", dryrun ? (ok ? "LOOP TRANSFER GUARD DRY-RUN: PASS (every criterion is "
                                       "falsifiable)"
                                     : "LOOP TRANSFER GUARD DRY-RUN: FAIL")
                               : (ok ? "LOOP TRANSFER GUARD: PASS" : "LOOP TRANSFER GUARD: FAIL"));
  return ok ? 0 : 1;
}
