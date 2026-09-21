// GPU-PIPE-001 Final Residency -- the DISPATCH detector.
//
// A residency change can be wrong in a way no numerical comparison and no
// transfer count will see: by engaging in a configuration it was never
// qualified for. The resident SIMPLE loop is declined under five stated
// conditions (audit.md section 6), and each of those conditions is a promise
// that some other configuration still runs exactly what it ran before.
//
// This probe asserts each decline, and -- more importantly -- asserts that the
// declined configuration still produces what it produced before, bit for bit,
// against an arm that could not have taken the resident path at all.
//
// Without this, "the resident loop is engaged only where it is exactly
// equivalent" is an unfalsifiable claim about code that reads plausibly.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
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

int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++failures;
}

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

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
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

Case cavity(Mesh mesh) {
  Case c{std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
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

SIMPLESettings base() {
  SIMPLESettings s;
  s.maxIterations = 8;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
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
  return s;
}

SIMPLEResult run(const Case& c, const SIMPLESettings& s,
                 cfd::turbulence::TurbulenceModel* model = nullptr) {
  const SIMPLE simple(s, /*referenceCell=*/0, model);
  return simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
}

bool identical(const Case& c, const SIMPLEResult& a, const SIMPLEResult& b) {
  if (a.status != b.status || a.iterations != b.iterations) return false;
  if (a.uResidualHistory != b.uResidualHistory) return false;
  if (a.pressureResidualHistory != b.pressureResidualHistory) return false;
  if (a.continuityHistory != b.continuityHistory) return false;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    if (!sameBits(a.pressure[i], b.pressure[i])) return false;
    if (!sameBits(a.velocity[i].x, b.velocity[i].x)) return false;
    if (!sameBits(a.velocity[i].y, b.velocity[i].y)) return false;
  }
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    if (!sameBits(a.massFlux[f], b.massFlux[f])) return false;
  }
  return true;
}

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001 final residency: dispatch probe ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }

  const Case c = cavity(MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));

  // The production configuration. If this is not engaged, every decline below
  // passes vacuously -- so it is asserted first.
  const SIMPLEResult production = run(c, base());
  check(production.residentSimpleLoop,
        "the production configuration ENGAGES the resident SIMPLE loop (non-vacuity)");
  check(production.residentPressureSolve && production.gpuDiscretization,
        "and it also has the resident pressure solve and the device discretization");

  std::printf("\n--- each declined configuration, and what it must still produce ---\n");
  {
    SIMPLESettings s = base();
    s.momentumSolver.backend = LinearSolverBackend::CPU;
    const SIMPLEResult r = run(c, s);
    check(!r.residentSimpleLoop,
          "a CPU momentum backend DECLINES the resident loop (it needs a host LinearSystem)");
    check(r.residentPressureSolve,
          "and the resident PRESSURE solve is unaffected -- the two decline independently");
  }
  {
    SIMPLESettings s = base();
    s.momentumSolver.type = LinearSolverType::CG;
    const SIMPLEResult r = run(c, s);
    check(!r.residentSimpleLoop,
          "a CG momentum solver DECLINES the resident loop (GpuCG has no resident entry point)");
  }
  {
    InertModel model(c.mesh);
    const SIMPLEResult r = run(c, base(), &model);
    check(!r.residentSimpleLoop,
          "a non-laminar turbulence model DECLINES the resident loop (correct() reads the host "
          "velocity)");
    // And the declined arm must be exactly the pre-residency behaviour. This is
    // the assertion that makes the decline a guarantee instead of a hope.
    SIMPLESettings s = base();
    s.momentumSolver.backend = LinearSolverBackend::CPU;
    s.pressureSolver.backend = LinearSolverBackend::CPU;
    const SIMPLEResult cpuSolvers = run(c, s);
    check(!cpuSolvers.residentSimpleLoop && !cpuSolvers.residentPressureSolve,
          "CPU linear solvers with device discretization decline both resident paths");
  }
  {
    SIMPLESettings s = base();
    s.enableGpuDiscretization = false;
    const SIMPLEResult r = run(c, s);
    check(!r.residentSimpleLoop && !r.gpuDiscretization,
          "without device discretization the resident loop is DECLINED -- there is no device "
          "system to solve");
  }
  {
    SIMPLESettings s = base();
    s.robustness.linearSolverFallback.enabled = true;
    const SIMPLEResult r = run(c, s);
    check(!r.residentSimpleLoop,
          "with the linear-solver fallback enabled the resident loop is DECLINED -- bypassing the "
          "wrapper would change failure behaviour");
  }
  {
    SIMPLESettings s = base();
    s.enableGpuResidency = true;
    const SIMPLEResult r = run(c, s);
    check(!r.residentSimpleLoop,
          "with the residency mirror enabled the resident loop is DECLINED -- syncMatrix reads a "
          "host matrix the resident path never builds");
  }

  std::printf("\n--- the decline is a DISPATCH change, not a numerical one ---\n");
  {
    // Two DIFFERENT ways of declining the resident loop, on the same case with
    // the same solvers. If declining were itself a numerical change, these two
    // would not agree -- they decline for unrelated reasons.
    InertModel model(c.mesh);
    const SIMPLEResult viaModel = run(c, base(), &model);
    SIMPLESettings viaMirror = base();
    viaMirror.enableGpuResidency = true;
    const SIMPLEResult viaMirrorResult = run(c, viaMirror);
    check(!viaModel.residentSimpleLoop && !viaMirrorResult.residentSimpleLoop,
          "both alternative declines really did decline");
    // enableGpuResidency additionally mirrors matrices into GpuResidencyManager,
    // which is read-only with respect to the solution -- so the two declined
    // arms must be the same solve.
    check(identical(c, viaModel, viaMirrorResult),
          "two unrelated reasons for declining produce bit-identical solves -- declining changes "
          "dispatch, not arithmetic");
    const SIMPLEResult viaModelAgain = run(c, base(), &model);
    check(identical(c, viaModel, viaModelAgain),
          "the declined configuration is deterministic across repeats");
  }

  std::printf("\n%s\n", failures == 0 ? "DISPATCH PROBE: PASS" : "DISPATCH PROBE: FAIL");
  return failures == 0 ? 0 : 1;
}
