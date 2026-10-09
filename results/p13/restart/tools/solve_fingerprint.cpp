// P13-001 -- the PRESERVATION instrument.
//
// P13-001 changes SIMPLE::solve(). CLAUDE.md section 6's rule is "do not
// silently change existing behaviour ... when shared code changes, prove
// existing results are unchanged (bitwise where claimed)". A full CTest run
// proves tests still pass; it does not prove the numbers are the same bits.
// This binary does.
//
// It runs a fixed suite of SIMPLE solves with NO resume state -- i.e. exactly
// the call shape every existing caller uses -- and prints, for each case, the
// status, the iteration count, every final residual as a raw bit pattern, and
// a 64-bit FNV-1a hash over the raw bits of every value in the final velocity,
// pressure and face-flux fields and of every residual history entry.
//
// Run it on the unchanged tree, freeze the output, run it again after the
// change. The two must be IDENTICAL. Anything else means P13-001 moved a
// number it had no business moving.
//
// The suite deliberately covers the paths P13-001 touches or could touch:
// the default absolute criterion, the normalized criterion with every
// robustness feature on (the monitor state the resume has to reconstruct),
// 2D and 3D, CPU and GPU, and more than one convection scheme.
//
// NON-VACUITY (control D-G1). A fingerprint that is insensitive to the fields
// would pass for free. `--perturb-control` nudges ONE component of ONE cell of
// the FIRST case by one ULP after the solve and before hashing. That run must
// print a different fingerprint for that case and identical fingerprints for
// every other case. Run it once; never run it to produce evidence.
//
// usage: solve_fingerprint [--cpu-only] [--perturb-control]
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
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

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

// FNV-1a over the raw object representation, so the hash is a function of the
// BITS, not of any decimal rendering.
struct Hasher {
  std::uint64_t h = 1469598103934665603ULL;
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ULL;
    }
  }
  void real(Real v) { bytes(&v, sizeof(v)); }
  void count(std::size_t n) {
    const std::uint64_t v = n;
    bytes(&v, sizeof(v));
  }
};

std::uint64_t bits(Real v) {
  std::uint64_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
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

void hashHistory(Hasher& hasher, const std::vector<Real>& history) {
  hasher.count(history.size());
  for (Real v : history) hasher.real(v);
}

bool perturbControl = false;
int caseIndex = 0;

void report(const Case& c) {
  const SIMPLE simple(c.settings, /*referenceCell=*/0);
  SIMPLEResult r = simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);

  // Control D-G1 -- see the header comment. One ULP, one component, one cell,
  // first case only.
  if (perturbControl && caseIndex == 0 && r.velocity.size() > 0) {
    r.velocity[0].x = std::nextafter(r.velocity[0].x, 1e300);
  }
  ++caseIndex;

  Hasher hasher;
  hasher.count(static_cast<std::size_t>(r.velocity.size()));
  for (Index i = 0; i < r.velocity.size(); ++i) {
    hasher.real(r.velocity[i].x);
    hasher.real(r.velocity[i].y);
    hasher.real(r.velocity[i].z);
  }
  hasher.count(static_cast<std::size_t>(r.pressure.size()));
  for (Index i = 0; i < r.pressure.size(); ++i) hasher.real(r.pressure[i]);
  hasher.count(static_cast<std::size_t>(r.massFlux.size()));
  for (Index i = 0; i < r.massFlux.size(); ++i) hasher.real(r.massFlux[i]);
  hashHistory(hasher, r.uResidualHistory);
  hashHistory(hasher, r.vResidualHistory);
  hashHistory(hasher, r.wResidualHistory);
  hashHistory(hasher, r.pressureResidualHistory);
  hashHistory(hasher, r.continuityHistory);
  hashHistory(hasher, r.robustness.uNormalizedHistory);
  hashHistory(hasher, r.robustness.vNormalizedHistory);
  hashHistory(hasher, r.robustness.pressureNormalizedHistory);
  hashHistory(hasher, r.robustness.continuityNormalizedHistory);
  hashHistory(hasher, r.robustness.convergenceDistanceHistory);
  hashHistory(hasher, r.robustness.velocityRelaxationHistory);
  hashHistory(hasher, r.robustness.pressureRelaxationHistory);

  std::printf("case          %s\n", c.name.c_str());
  std::printf("  status      %s\n", statusName(r.status));
  std::printf("  iterations  %zu\n", static_cast<std::size_t>(r.iterations));
  std::printf("  cells/faces %zu %zu\n", static_cast<std::size_t>(r.pressure.size()),
              static_cast<std::size_t>(r.massFlux.size()));
  std::printf("  residual_u  %016llx\n", static_cast<unsigned long long>(bits(r.finalUResidual)));
  std::printf("  residual_v  %016llx\n", static_cast<unsigned long long>(bits(r.finalVResidual)));
  std::printf("  residual_w  %016llx\n", static_cast<unsigned long long>(bits(r.finalWResidual)));
  std::printf("  residual_p  %016llx\n",
              static_cast<unsigned long long>(bits(r.finalPressureResidual)));
  std::printf("  continuity  %016llx\n",
              static_cast<unsigned long long>(bits(r.finalContinuityResidual)));
  std::printf("  imbalance   %016llx\n",
              static_cast<unsigned long long>(bits(r.globalMassImbalance)));
  std::printf("  passes      %zu %zu\n", static_cast<std::size_t>(r.momentumPredictorPasses),
              static_cast<std::size_t>(r.pressureCorrectionPasses));
  std::printf("  gpu         disc=%d residentP=%d residentLoop=%d\n",
              static_cast<int>(r.gpuDiscretization), static_cast<int>(r.residentPressureSolve),
              static_cast<int>(r.residentSimpleLoop));
  std::printf("  FINGERPRINT %016llx\n\n", static_cast<unsigned long long>(hasher.h));
}

}  // namespace

int main(int argc, char** argv) {
  bool cpuOnly = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--cpu-only") == 0) cpuOnly = true;
    if (std::strcmp(argv[i], "--perturb-control") == 0) perturbControl = true;
  }
  if (perturbControl) std::printf("PERTURB CONTROL ACTIVE -- this output is not evidence\n\n");

  // --- default absolute criterion, every feature off -----------------------
  report(cavity("cpu-cavity-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                cpuSettings(8)));
  report(channel("cpu-channel-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                 cpuSettings(8)));
  report(cavity("cpu-cavity-3d-6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0),
                cpuSettings(6)));

  // --- a different convection scheme ---------------------------------------
  {
    SIMPLESettings s = cpuSettings(8);
    s.convectionScheme = ConvectionScheme::LinearUpwind;
    report(cavity("cpu-cavity-2d-16-linup", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), s));
  }

  // --- non-orthogonal correction passes ------------------------------------
  {
    SIMPLESettings s = cpuSettings(6);
    s.nonOrthogonalCorrections = 2;
    report(cavity("cpu-cavity-2d-16-nonorth", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                  s));
  }

  // --- the monitor state a resume has to reconstruct: every feature on -----
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
    report(cavity("cpu-cavity-2d-16-robust", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), s));
  }

  // --- a solve that actually converges -------------------------------------
  report(cavity("cpu-cavity-2d-12-converge", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0),
                cpuSettings(3000)));

  if (!cpuOnly) {
    report(cavity("gpu-cavity-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                  gpuSettings(8)));
    report(channel("gpu-channel-2d-16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                   gpuSettings(8)));
    report(cavity("gpu-cavity-3d-6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0),
                  gpuSettings(6)));
  }
  return 0;
}
