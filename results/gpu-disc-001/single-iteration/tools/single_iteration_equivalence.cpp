// GPU-DISC-001L gate -- one production SIMPLE outer iteration, CPU vs CUDA.
//
// This is an INTEGRATION gate. No new numerics: every stage is an operator
// already qualified bitwise by 001B-001K, composed in the order
// SIMPLE::solve's outer loop uses (audit.md section 2).
//
// Layers:
//   F  fidelity    the CPU ladder's committed state must be BITWISE equal to
//                  SIMPLE::solve(maxIterations = 1). This is what makes the
//                  ladder the production iteration rather than a lookalike.
//   C  controlled  both paths consume the SAME solver outputs, so every
//                  discretization stage is compared BITWISE and solver
//                  variability cannot mask an operator difference.
//   I  independent each path runs its own solves; compared with existing
//                  project tolerances. Shows the composition survives real
//                  solver behaviour.
//   V  invariants  conservation, finiteness, BC/reference behaviour, the 2D W
//                  contract, and per-backend determinism.
//
// Every layer reports the FIRST stage at which anything diverges.
//
// usage: single_iteration_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceFaceFluxCorrection.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/gpu/DeviceVelocityCorrection.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/turbulence/LaminarModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverSettings;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::FaceFluxScheme;

namespace {

int failures = 0;
int cases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;

struct Coverage {
  int twoD = 0, threeD = 0;
  int fidelityChecks = 0;
  int controlledLadders = 0;
  int independentLadders = 0;
  int determinismChecks = 0;
  int invariantCases = 0;
  int rhieChowCases = 0, linearCases = 0;
  int pinnedCases = 0, openCases = 0;
  int identicalResponseCases = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

std::vector<Real> pull(const cfd::gpu::DeviceBuffer<Real>& b) {
  std::vector<Real> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
}
std::vector<Index> pullI(const cfd::gpu::DeviceBuffer<Index>& b) {
  std::vector<Index> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
}

// --- the 11 stages the brief names -----------------------------------------
enum StageId {
  kMomentumSystems = 0,
  kMomentumSolutions,
  kResponseCoefficients,
  kPredictedFaceFlux,
  kPressureSystem,
  kPressureCorrection,
  kUpdatedPressure,
  kCorrectedVelocity,
  kCorrectedFaceFlux,
  kContinuityImbalance,
  kResiduals,
  kStageCount
};
const char* const kStageNames[kStageCount] = {
    "1 momentum systems",  "2 momentum solutions", "3 response coefficients",
    "4 predicted flux",    "5 pressure system",    "6 pressure correction p'",
    "7 updated pressure",  "8 corrected U/V/W",    "9 corrected face flux",
    "10 continuity",       "11 residuals"};

struct StageReport {
  std::size_t compared = 0;
  std::size_t differing = 0;
  Real maxAbs = 0.0;
  Real maxRel = 0.0;
  Real scale = 0.0;
};

struct LadderComparison {
  StageReport stage[kStageCount];
  int firstDivergentStage = -1;

  void note(StageId id, Real want, Real got) {
    StageReport& r = stage[id];
    ++r.compared;
    ++valuesCompared;
    const bool same = sameBits(want, got);
    if (same) ++bitwiseValues; else ++r.differing;
    const Real diff = std::abs(want - got);
    r.maxAbs = std::max(r.maxAbs, diff);
    r.scale = std::max(r.scale, std::abs(want));
    if (std::abs(want) > 0.0) r.maxRel = std::max(r.maxRel, diff / std::abs(want));
  }
  void noteAll(StageId id, const std::vector<Real>& want, const std::vector<Real>& got) {
    const std::size_t n = std::min(want.size(), got.size());
    if (want.size() != got.size()) { stage[id].differing += 1; }
    for (std::size_t i = 0; i < n; ++i) note(id, want[i], got[i]);
  }
  // `bitwise` selects the criterion: exact bits, or an absolute/relative
  // tolerance from the project's own scale.
  void finish(bool bitwise, Real absoluteTolerance) {
    for (int s = 0; s < kStageCount; ++s) {
      const bool ok = bitwise ? stage[s].differing == 0
                              : stage[s].maxAbs <= absoluteTolerance * std::max(stage[s].scale, 1.0);
      if (!ok && firstDivergentStage < 0) firstDivergentStage = s;
    }
  }
};

// --- one iteration's full state, host-side ---------------------------------
struct Ladder {
  // stage 1
  std::vector<Real> uValues, uRhs, vValues, vRhs, wValues, wRhs;
  // stage 2
  std::vector<Real> uStar, vStar, wStar;
  // stage 3
  std::vector<Real> dU, dV, dW;
  // stage 4
  std::vector<Real> predictorFlux;
  // stage 5
  std::vector<Real> pValues, pRhs;
  // stage 6
  std::vector<Real> pPrime;
  // stage 7
  std::vector<Real> pressureNew;
  // stage 8
  std::vector<Real> uNew, vNew, wNew;
  // stage 9
  std::vector<Real> fluxNew;
  // stage 10
  std::vector<Real> imbalance;
  // stage 11
  Real uResidual = 0.0, vResidual = 0.0, wResidual = 0.0, pResidual = 0.0;
  Real continuityResidual = 0.0, globalImbalance = 0.0;
  Index uIterations = 0, vIterations = 0, wIterations = 0, pIterations = 0;
  Index pRestarts = 0;
  bool converged = true;
};

void compareLadders(const Ladder& cpu, const Ladder& gpu, LadderComparison& c) {
  c.noteAll(kMomentumSystems, cpu.uValues, gpu.uValues);
  c.noteAll(kMomentumSystems, cpu.uRhs, gpu.uRhs);
  c.noteAll(kMomentumSystems, cpu.vValues, gpu.vValues);
  c.noteAll(kMomentumSystems, cpu.vRhs, gpu.vRhs);
  c.noteAll(kMomentumSystems, cpu.wValues, gpu.wValues);
  c.noteAll(kMomentumSystems, cpu.wRhs, gpu.wRhs);
  c.noteAll(kMomentumSolutions, cpu.uStar, gpu.uStar);
  c.noteAll(kMomentumSolutions, cpu.vStar, gpu.vStar);
  c.noteAll(kMomentumSolutions, cpu.wStar, gpu.wStar);
  c.noteAll(kResponseCoefficients, cpu.dU, gpu.dU);
  c.noteAll(kResponseCoefficients, cpu.dV, gpu.dV);
  c.noteAll(kResponseCoefficients, cpu.dW, gpu.dW);
  c.noteAll(kPredictedFaceFlux, cpu.predictorFlux, gpu.predictorFlux);
  c.noteAll(kPressureSystem, cpu.pValues, gpu.pValues);
  c.noteAll(kPressureSystem, cpu.pRhs, gpu.pRhs);
  c.noteAll(kPressureCorrection, cpu.pPrime, gpu.pPrime);
  c.noteAll(kUpdatedPressure, cpu.pressureNew, gpu.pressureNew);
  c.noteAll(kCorrectedVelocity, cpu.uNew, gpu.uNew);
  c.noteAll(kCorrectedVelocity, cpu.vNew, gpu.vNew);
  c.noteAll(kCorrectedVelocity, cpu.wNew, gpu.wNew);
  c.noteAll(kCorrectedFaceFlux, cpu.fluxNew, gpu.fluxNew);
  c.noteAll(kContinuityImbalance, cpu.imbalance, gpu.imbalance);
  c.note(kResiduals, cpu.uResidual, gpu.uResidual);
  c.note(kResiduals, cpu.vResidual, gpu.vResidual);
  c.note(kResiduals, cpu.wResidual, gpu.wResidual);
  c.note(kResiduals, cpu.pResidual, gpu.pResidual);
  c.note(kResiduals, cpu.continuityResidual, gpu.continuityResidual);
  c.note(kResiduals, cpu.globalImbalance, gpu.globalImbalance);
}

// --- deterministic cases ---------------------------------------------------
struct Case {
  Case(std::string n, Mesh m) : name(std::move(n)), mesh(std::move(m)) {}
  std::string name;
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.2, 1.0e-3};
  Index referenceCell = 0;
  Real velocityRelaxation = 0.7;
  Real pressureRelaxation = 0.3;
};

Mesh warped3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(
      mesh, std::make_shared<cfd::mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                          Vector3{0.05, 0.025, -0.0375},
                                                          cfd::constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  return mesh;
}

// The canonical lid-driven cavity: every patch a wall, the top one moving. No
// FixedValue pressure patch, so the reference cell IS pinned.
Case lidDrivenCavity(const std::string& name, Mesh mesh) {
  Case c(name, std::move(mesh));
  const auto& m = c.mesh;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    // The LAST patch is the lid; every generator orders its patches
    // deterministically, so this is reproducible without naming them.
    const bool lid = i + 1 == m.boundaryPatches().size();
    if (lid) {
      c.velocityBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
    } else {
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Wall>());
    }
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

// An open case: one inlet, one outlet with a FixedValue pressure patch, so the
// reference pin is SUPPRESSED and the open boundary carries the correction.
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
      c.velocityBoundaries.set(m, patch.name(), std::make_unique<cfd::boundary::Wall>());
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
    // Deterministic, non-trivial, and NOT divergence-free, so the pressure
    // correction has real work to do in the very first iteration.
    c.velocity[cell] = Vector3{0.8 + (0.2 * std::sin(pi * x.y)), 0.1 * std::sin(pi * x.x),
                               0.05 * std::sin(pi * x.z)};
    c.pressure[cell] = 10.0 * (1.0 - x.x);
  }
  return c;
}

// A duct with Symmetry side walls, for BOUNDARY-CONDITION coverage at the
// integration level: Symmetry is the one velocity condition the other cases do
// not exercise.
//
// It was originally added on the theory that Symmetry -- which removes only the
// normal component -- would separate the U and V momentum diagonals. It does
// not: in this codebase the component never reaches the MATRIX at all, only the
// RHS, so dU == dV on this case too. See the component-independence assertion
// in runCase and summary.md.
Case symmetryDuct(const std::string& name, Mesh mesh) {
  Case c(name, std::move(mesh));
  const auto& m = c.mesh;
  std::size_t i = 0;
  const std::size_t patchCount = m.boundaryPatches().size();
  for (const auto& patch : m.boundaryPatches()) {
    if (i == 0) {
      c.velocityBoundaries.set(m, patch.name(),
                               std::make_unique<cfd::boundary::Inlet>(Vector3{1.0, 0.2, 0.0}));
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
    c.velocity[cell] = Vector3{0.9 + (0.15 * std::sin(pi * x.y)), 0.2 * std::cos(pi * x.x),
                               0.05 * std::sin(pi * x.z)};
    c.pressure[cell] = 5.0 * (1.0 - x.x);
  }
  return c;
}

cfd::pressure_velocity::SIMPLESettings settingsFor(const Case& c, Index maxIterations) {
  cfd::pressure_velocity::SIMPLESettings s;
  s.maxIterations = maxIterations;
  s.velocityRelaxation = c.velocityRelaxation;
  s.pressureRelaxation = c.pressureRelaxation;
  s.momentumSolver.absoluteTolerance = 1e-12;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-12;
  s.pressureSolver.relativeTolerance = 1e-12;
  s.pressureSolver.maxIterations = 5000;
  return s;
}

cfd::gpu::DeviceVelocity uploadVelocity(const VectorField& v) {
  cfd::gpu::DeviceVelocity d;
  const Index n = static_cast<Index>(v.size());
  std::vector<Real> x(n), y(n), z(n);
  for (Index c = 0; c < n; ++c) { x[c] = v[c].x; y[c] = v[c].y; z[c] = v[c].z; }
  d.x.uploadFrom(x.data(), n);
  d.y.uploadFrom(y.data(), n);
  d.z.uploadFrom(z.data(), n);
  return d;
}

std::vector<Real> component(const VectorField& v, int which) {
  std::vector<Real> out(v.size());
  for (std::size_t i = 0; i < v.size(); ++i)
    out[i] = which == 0 ? v[i].x : (which == 1 ? v[i].y : v[i].z);
  return out;
}

std::vector<Real> imbalanceOf(const Mesh& mesh, const std::vector<Real>& flux) {
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

Real rmsOf(const std::vector<Real>& v) {
  Real sum = 0.0;
  for (const Real x : v) sum += x * x;
  return v.empty() ? 0.0 : std::sqrt(sum / static_cast<Real>(v.size()));
}

// ---------------------------------------------------------------------------
// The CPU ladder -- the audited stage order, production functions only
// ---------------------------------------------------------------------------

struct FedSolutions {
  bool present = false;
  std::vector<Real> uStar, vStar, wStar, pPrime;
  Real uResidual = 0.0, vResidual = 0.0, wResidual = 0.0, pResidual = 0.0;
};

Ladder runCpuLadder(const Case& c, const cfd::pressure_velocity::SIMPLESettings& settings,
                    FedSolutions* capture) {
  const Mesh& mesh = c.mesh;
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  Ladder L;

  // Pre-loop: the initial flux is ALWAYS the linear one (SIMPLE.cpp:212).
  const SurfaceField massFlux =
      cfd::physics::calculateMassFlux(mesh, c.velocity, c.fluid, c.velocityBoundaries);

  // Stage 1 -- previous components, then assembly in U, V, (W) order.
  ScalarField previousU(nc), previousV(nc), previousW(nc);
  for (Index i = 0; i < nc; ++i) {
    previousU[i] = c.velocity[i].x;
    previousV[i] = c.velocity[i].y;
    previousW[i] = c.velocity[i].z;
  }
  cfd::turbulence::LaminarModel laminar(mesh);
  laminar.correct(mesh, c.velocity, c.pressure);
  const ScalarField mu = laminar.effectiveViscosity(c.fluid.dynamicViscosity());

  const auto assemble = [&](cfd::physics::VelocityComponent comp, const ScalarField& previous) {
    return cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, c.velocity, c.pressure, massFlux, mu, c.velocityBoundaries, c.pressureBoundaries,
        comp, previous, settings.velocityRelaxation, nullptr, nullptr, settings.convectionScheme,
        settings.gradientScheme, settings.nonOrthogonalCorrections > 0, nullptr, nullptr);
  };
  const auto uAssembly = assemble(cfd::physics::VelocityComponent::U, previousU);
  const auto vAssembly = assemble(cfd::physics::VelocityComponent::V, previousV);
  std::optional<cfd::physics::MomentumAssembly> wAssembly;
  if (threeD) wAssembly = assemble(cfd::physics::VelocityComponent::W, previousW);

  const auto storeSystem = [&](const cfd::algebra::LinearSystem& system,
                               std::vector<Real>& values, std::vector<Real>& rhs) {
    const auto& m = system.matrix();
    values.assign(m.valuesData(), m.valuesData() + m.rowOffsetsData()[m.rows()]);
    rhs.assign(system.rhs().begin(), system.rhs().end());
  };
  storeSystem(uAssembly.system, L.uValues, L.uRhs);
  storeSystem(vAssembly.system, L.vValues, L.vRhs);
  if (threeD) storeSystem(wAssembly->system, L.wValues, L.wRhs);

  // Stage 2 -- momentum solves, WARM-STARTED from the previous iterate.
  auto momentumSolver = cfd::algebra::makeLinearSolver(settings.momentumSolver);
  const auto toVector = [](const ScalarField& f) {
    cfd::algebra::Vector v(f.size());
    for (std::size_t i = 0; i < f.size(); ++i) v[i] = f[i];
    return v;
  };
  const auto uResult = momentumSolver->solve(uAssembly.system, toVector(previousU));
  const auto vResult = momentumSolver->solve(vAssembly.system, toVector(previousV));
  std::optional<cfd::algebra::SolverResult> wResult;
  if (threeD) wResult = momentumSolver->solve(wAssembly->system, toVector(previousW));
  L.converged = uResult.converged() && vResult.converged() && (!threeD || wResult->converged());
  L.uIterations = uResult.iterations;
  L.vIterations = vResult.iterations;
  if (threeD) L.wIterations = wResult->iterations;

  VectorField velocityStar(nc);
  for (Index i = 0; i < nc; ++i) {
    velocityStar[i] = threeD ? Vector3{uResult.solution[i], vResult.solution[i],
                                       wResult->solution[i]}
                             : Vector3{uResult.solution[i], vResult.solution[i], 0.0};
  }
  L.uStar = component(velocityStar, 0);
  L.vStar = component(velocityStar, 1);
  L.wStar = component(velocityStar, 2);

  // Stage 3 -- response coefficients.
  const ScalarField dU =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, uAssembly.diagonal);
  const ScalarField dV =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, vAssembly.diagonal);
  std::optional<ScalarField> dW;
  if (threeD)
    dW = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, wAssembly->diagonal);
  L.dU.assign(dU.begin(), dU.end());
  L.dV.assign(dV.begin(), dV.end());
  if (threeD) L.dW.assign(dW->begin(), dW->end());

  // Stage 4 -- the predictor flux, by the RESOLVED scheme.
  const FaceFluxScheme scheme =
      cfd::pressure_velocity::resolveFaceFluxScheme(settings.faceFlux, mesh.dimension());
  SurfaceField predictorFlux;
  if (scheme == FaceFluxScheme::RhieChow) {
    const VectorField gradP = cfd::discretization::gradient(mesh, c.pressure, c.pressureBoundaries,
                                                            settings.gradientScheme);
    predictorFlux = cfd::pressure_velocity::rhieChowMassFlux(
        mesh, velocityStar, c.pressure, gradP, dU, dV, threeD ? &*dW : nullptr, c.fluid,
        c.velocityBoundaries, settings.velocityRelaxation);
  } else {
    predictorFlux =
        cfd::physics::calculateMassFlux(mesh, velocityStar, c.fluid, c.velocityBoundaries);
  }
  L.predictorFlux.assign(predictorFlux.begin(), predictorFlux.end());

  // Stage 5 -- the pressure-correction system.
  cfd::pressure_velocity::PressureCorrectionOptions pressureOptions;
  pressureOptions.nonOrthogonal = settings.nonOrthogonalCorrections > 0;
  pressureOptions.gradientScheme = settings.gradientScheme;
  const auto pAssembly = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, predictorFlux, dU, dV, c.fluid.density(), c.referenceCell, c.pressureBoundaries,
      pressureOptions, threeD ? &*dW : nullptr);
  storeSystem(pAssembly.system, L.pValues, L.pRhs);

  // Stage 6 -- the pressure solve, ZERO initial guess.
  auto pressureSolver = cfd::algebra::makeLinearSolver(settings.pressureSolver);
  const auto pResult = pressureSolver->solve(pAssembly.system);
  L.converged = L.converged && pResult.converged();
  L.pIterations = pResult.iterations;
  L.pRestarts = pResult.restarts;
  ScalarField pPrime(nc);
  for (Index i = 0; i < nc; ++i) pPrime[i] = pResult.solution[i];
  L.pPrime.assign(pPrime.begin(), pPrime.end());

  // Stage 7 -- the pressure update (relaxed).
  ScalarField pressureNew(nc);
  for (Index i = 0; i < nc; ++i)
    pressureNew[i] = c.pressure[i] + (settings.pressureRelaxation * pPrime[i]);
  L.pressureNew.assign(pressureNew.begin(), pressureNew.end());

  // Stage 8 -- the velocity correction, applied to velocityStar.
  const VectorField velocityNew = cfd::pressure_velocity::correctVelocity(
      mesh, velocityStar, dU, dV, pPrime, c.pressureBoundaries, settings.gradientScheme,
      threeD ? &*dW : nullptr);
  L.uNew = component(velocityNew, 0);
  L.vNew = component(velocityNew, 1);
  L.wNew = component(velocityNew, 2);

  // Stage 9 -- the face-flux correction, applied to predictorFlux.
  const SurfaceField fluxNew = cfd::pressure_velocity::correctFaceMassFlux(
      mesh, predictorFlux, pAssembly.faceCoefficient, pPrime,
      settings.nonOrthogonalCorrections > 1 ? &pAssembly.explicitFaceFlux : nullptr);
  L.fluxNew.assign(fluxNew.begin(), fluxNew.end());

  // Stage 10 / 11 -- continuity and the residual bookkeeping.
  L.imbalance = imbalanceOf(mesh, L.fluxNew);
  const auto continuity = cfd::physics::evaluateContinuity(mesh, fluxNew);
  L.continuityResidual = rmsOf(std::vector<Real>(continuity.cellImbalance.begin(),
                                                 continuity.cellImbalance.end()));
  L.globalImbalance = std::abs(continuity.globalNetFlux);
  L.uResidual = uResult.initialResidual;
  L.vResidual = vResult.initialResidual;
  L.pResidual = pResult.initialResidual;
  L.wResidual = threeD ? wResult->initialResidual : 0.0;
  (void)nf;

  if (capture != nullptr) {
    capture->present = true;
    capture->uStar = L.uStar;
    capture->vStar = L.vStar;
    capture->wStar = L.wStar;
    capture->pPrime = L.pPrime;
    capture->uResidual = L.uResidual;
    capture->vResidual = L.vResidual;
    capture->wResidual = L.wResidual;
    capture->pResidual = L.pResidual;
  }
  return L;
}


// ---------------------------------------------------------------------------
// The CUDA ladder -- the SAME stage order, verified device operators only
// ---------------------------------------------------------------------------

struct DevicePlans {
  cfd::gpu::DeviceMomentumAssemblyPlan momentum;
  cfd::gpu::DeviceFaceFluxPlan flux;
  cfd::gpu::DevicePressureCorrectionPlan pressure;
  cfd::gpu::DeviceVelocityCorrectionPlan velocity;
  bool usable = false;

  bool build(const Case& c) {
    usable = momentum.build(c.mesh, c.velocityBoundaries, c.pressureBoundaries) &&
             flux.build(c.mesh, c.velocityBoundaries) &&
             pressure.build(c.mesh, c.pressureBoundaries) &&
             velocity.build(c.mesh, c.pressureBoundaries);
    return usable;
  }
};

// `fed` non-null selects CONTROLLED mode: the solves are not run, the supplied
// solutions are used instead, so every stage that is a discretization operator
// is compared against the CPU with nothing else differing.
Ladder runGpuLadder(const Case& c, const cfd::pressure_velocity::SIMPLESettings& settings,
                    DevicePlans& plans, const FedSolutions* fed) {
  const Mesh& mesh = c.mesh;
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  Ladder L;

  auto deviceVelocity = uploadVelocity(c.velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dPrevU, dPrevV, dPrevW;
  {
    std::vector<Real> p(nc), mu(nc), pu(nc), pv(nc), pw(nc);
    for (Index i = 0; i < nc; ++i) {
      p[i] = c.pressure[i];
      mu[i] = c.fluid.dynamicViscosity();  // LaminarModel: mu_t == 0 everywhere
      pu[i] = c.velocity[i].x;
      pv[i] = c.velocity[i].y;
      pw[i] = c.velocity[i].z;
    }
    dPressure.uploadFrom(p.data(), nc);
    dViscosity.uploadFrom(mu.data(), nc);
    dPrevU.uploadFrom(pu.data(), nc);
    dPrevV.uploadFrom(pv.data(), nc);
    dPrevW.uploadFrom(pw.data(), nc);
  }

  // Pre-loop: the initial flux is ALWAYS the linear one.
  cfd::gpu::DeviceBuffer<Real> dMassFlux;
  cfd::gpu::calculateMassFluxDevice(plans.flux, deviceVelocity, c.fluid.density(), dMassFlux);

  // Stage 1 -- momentum assembly, U, V, (W).
  const auto assemble = [&](Index comp, cfd::gpu::DeviceBuffer<Real>& previous,
                            cfd::gpu::DeviceMomentumSystem& system) {
    cfd::gpu::MomentumAssemblyOptions options;
    options.component = comp;
    options.convectionScheme = static_cast<Index>(settings.convectionScheme);
    options.relaxationAlpha = settings.velocityRelaxation;
    options.applyNonOrthogonalCorrection = settings.nonOrthogonalCorrections > 0;
    cfd::gpu::assembleRelaxedMomentumDevice(plans.momentum, deviceVelocity, dPressure, dViscosity,
                                            dMassFlux, previous, nullptr, options, system);
  };
  cfd::gpu::DeviceMomentumSystem uSystem, vSystem, wSystem;
  assemble(cfd::gpu::kVelocityU, dPrevU, uSystem);
  assemble(cfd::gpu::kVelocityV, dPrevV, vSystem);
  if (threeD) assemble(cfd::gpu::kVelocityW, dPrevW, wSystem);

  // Rebuild a host LinearSystem from the device CSR -- the solve itself is the
  // already-qualified linear solver, not part of this gate's numerics.
  const auto toHostSystem = [&](const cfd::gpu::DeviceMomentumSystem& system,
                                std::vector<Real>& valuesOut, std::vector<Real>& rhsOut) {
    const auto rows = pullI(system.rowOffsets);
    const auto cols = pullI(system.columnIndices);
    const auto vals = pull(system.values);
    const auto rhs = pull(system.rhs);
    cfd::algebra::SparseMatrixBuilder builder(nc, nc);
    for (Index r = 0; r < nc; ++r)
      for (Index k = rows[r]; k < rows[r + 1]; ++k)
        if (vals[k] != 0.0) builder.add(r, cols[k], vals[k]);
    auto matrix = builder.build();
    cfd::algebra::Vector b(static_cast<std::size_t>(nc));
    for (Index i = 0; i < nc; ++i) b[i] = rhs[i];
    valuesOut.assign(matrix.valuesData(), matrix.valuesData() + matrix.rowOffsetsData()[nc]);
    rhsOut.assign(b.begin(), b.end());
    return cfd::algebra::LinearSystem(matrix, b);
  };
  const auto uHost = toHostSystem(uSystem, L.uValues, L.uRhs);
  const auto vHost = toHostSystem(vSystem, L.vValues, L.vRhs);
  std::optional<cfd::algebra::LinearSystem> wHost;
  if (threeD) wHost = toHostSystem(wSystem, L.wValues, L.wRhs);

  // Stage 2 -- momentum solutions.
  cfd::gpu::DeviceBuffer<Real> dUStar, dVStar, dWStar;
  if (fed != nullptr && fed->present) {
    L.uStar = fed->uStar;
    L.vStar = fed->vStar;
    L.wStar = fed->wStar;
    L.uResidual = fed->uResidual;
    L.vResidual = fed->vResidual;
    L.wResidual = fed->wResidual;
  } else {
    auto solver = cfd::gpu::makeGpuBiCGSTAB(settings.momentumSolver);
    const auto warm = [&](const std::vector<Real>& previous) {
      cfd::algebra::Vector v(previous.size());
      for (std::size_t i = 0; i < previous.size(); ++i) v[i] = previous[i];
      return v;
    };
    std::vector<Real> pu(nc), pv(nc), pw(nc);
    for (Index i = 0; i < nc; ++i) { pu[i] = c.velocity[i].x; pv[i] = c.velocity[i].y; pw[i] = c.velocity[i].z; }
    const auto uResult = solver->solve(uHost, warm(pu));
    const auto vResult = solver->solve(vHost, warm(pv));
    L.converged = uResult.converged() && vResult.converged();
    L.uIterations = uResult.iterations;
    L.vIterations = vResult.iterations;
    L.uStar.assign(uResult.solution.begin(), uResult.solution.end());
    L.vStar.assign(vResult.solution.begin(), vResult.solution.end());
    L.uResidual = uResult.initialResidual;
    L.vResidual = vResult.initialResidual;
    L.wStar.assign(static_cast<std::size_t>(nc), 0.0);
    if (threeD) {
      const auto wResult = solver->solve(*wHost, warm(pw));
      L.converged = L.converged && wResult.converged();
      L.wIterations = wResult.iterations;
      L.wStar.assign(wResult.solution.begin(), wResult.solution.end());
      L.wResidual = wResult.initialResidual;
    }
  }
  dUStar.uploadFrom(L.uStar.data(), nc);
  dVStar.uploadFrom(L.vStar.data(), nc);
  if (L.wStar.empty()) L.wStar.assign(static_cast<std::size_t>(nc), 0.0);
  dWStar.uploadFrom(L.wStar.data(), nc);
  cfd::gpu::DeviceVelocity deviceVelocityStar;
  deviceVelocityStar.x.uploadFrom(L.uStar.data(), nc);
  deviceVelocityStar.y.uploadFrom(L.vStar.data(), nc);
  deviceVelocityStar.z.uploadFrom(L.wStar.data(), nc);

  // Stage 3 -- response coefficients.
  cfd::gpu::DeviceBuffer<Real> dU, dV, dW;
  const cfd::gpu::DeviceMesh& deviceMesh = plans.momentum.convectionPlan().mesh();
  cfd::gpu::computeMomentumResponseCoefficientDevice(deviceMesh, uSystem.diagonal, dU);
  cfd::gpu::computeMomentumResponseCoefficientDevice(deviceMesh, vSystem.diagonal, dV);
  if (threeD) cfd::gpu::computeMomentumResponseCoefficientDevice(deviceMesh, wSystem.diagonal, dW);
  L.dU = pull(dU);
  L.dV = pull(dV);
  if (threeD) L.dW = pull(dW);

  // Stage 4 -- the predictor flux, by the RESOLVED scheme.
  const FaceFluxScheme scheme =
      cfd::pressure_velocity::resolveFaceFluxScheme(settings.faceFlux, mesh.dimension());
  cfd::gpu::DeviceBuffer<Real> dPredictorFlux;
  if (scheme == FaceFluxScheme::RhieChow) {
    // The gradient of the START-OF-ITERATION pressure, as the CPU uses.
    const VectorField gradP = cfd::discretization::gradient(mesh, c.pressure, c.pressureBoundaries,
                                                            settings.gradientScheme);
    cfd::gpu::DeviceBuffer<Real> gx, gy, gz;
    std::vector<Real> a(nc), b(nc), g(nc);
    for (Index i = 0; i < nc; ++i) { a[i] = gradP[i].x; b[i] = gradP[i].y; g[i] = gradP[i].z; }
    gx.uploadFrom(a.data(), nc);
    gy.uploadFrom(b.data(), nc);
    gz.uploadFrom(g.data(), nc);
    cfd::gpu::rhieChowMassFluxDevice(plans.flux, deviceVelocityStar, dPressure, gx, gy, gz, dU, dV,
                                     threeD ? &dW : nullptr, c.fluid.density(),
                                     settings.velocityRelaxation, dPredictorFlux);
  } else {
    cfd::gpu::calculateMassFluxDevice(plans.flux, deviceVelocityStar, c.fluid.density(),
                                      dPredictorFlux);
  }
  L.predictorFlux = pull(dPredictorFlux);

  // Stage 5 -- the pressure-correction system.
  cfd::gpu::PressureCorrectionOptionsDevice pressureOptions;
  pressureOptions.nonOrthogonal = settings.nonOrthogonalCorrections > 0;
  pressureOptions.referenceCell = c.referenceCell;
  pressureOptions.density = c.fluid.density();
  cfd::gpu::DevicePressureCorrectionSystem pSystem;
  cfd::gpu::assemblePressureCorrectionDevice(plans.pressure, dPredictorFlux, dU, dV,
                                             threeD ? &dW : nullptr, nullptr, pressureOptions,
                                             pSystem);
  cfd::algebra::LinearSystem pHost = [&] {
    const auto rows = pullI(pSystem.rowOffsets);
    const auto cols = pullI(pSystem.columnIndices);
    const auto vals = pull(pSystem.values);
    const auto rhs = pull(pSystem.rhs);
    cfd::algebra::SparseMatrixBuilder builder(nc, nc);
    for (Index r = 0; r < nc; ++r)
      for (Index k = rows[r]; k < rows[r + 1]; ++k)
        if (vals[k] != 0.0) builder.add(r, cols[k], vals[k]);
    auto matrix = builder.build();
    cfd::algebra::Vector b(static_cast<std::size_t>(nc));
    for (Index i = 0; i < nc; ++i) b[i] = rhs[i];
    L.pValues.assign(matrix.valuesData(), matrix.valuesData() + matrix.rowOffsetsData()[nc]);
    L.pRhs.assign(b.begin(), b.end());
    return cfd::algebra::LinearSystem(matrix, b);
  }();

  // Stage 6 -- the pressure correction.
  cfd::gpu::DeviceBuffer<Real> dPPrime;
  if (fed != nullptr && fed->present) {
    L.pPrime = fed->pPrime;
    L.pResidual = fed->pResidual;
  } else {
    auto solver = cfd::gpu::makeGpuBiCGSTAB(settings.pressureSolver);
    const auto pResult = solver->solve(pHost);
    L.converged = L.converged && pResult.converged();
    L.pIterations = pResult.iterations;
    L.pRestarts = pResult.restarts;
    L.pPrime.assign(pResult.solution.begin(), pResult.solution.end());
    L.pResidual = pResult.initialResidual;
  }
  dPPrime.uploadFrom(L.pPrime.data(), nc);

  // Stage 7 -- the pressure update (relaxed).
  L.pressureNew.assign(static_cast<std::size_t>(nc), 0.0);
  for (Index i = 0; i < nc; ++i)
    L.pressureNew[i] = c.pressure[i] + (settings.pressureRelaxation * L.pPrime[i]);

  // Stage 8 -- the velocity correction, applied to velocityStar.
  cfd::gpu::DeviceBuffer<Real> gx, gy, gz;
  cfd::gpu::DeviceVelocity corrected;
  cfd::gpu::correctVelocityDevice(
      plans.velocity, deviceVelocityStar, dU, dV, threeD ? &dW : nullptr, dPPrime,
      settings.gradientScheme == GradientScheme::LeastSquares
          ? cfd::gpu::kGradientSchemeLeastSquares
          : cfd::gpu::kGradientSchemeGreenGauss,
      gx, gy, gz, corrected);
  L.uNew = pull(corrected.x);
  L.vNew = pull(corrected.y);
  L.wNew = pull(corrected.z);

  // Stage 9 -- the face-flux correction, applied to predictorFlux.
  cfd::gpu::DeviceBuffer<Real> dFluxNew;
  cfd::gpu::correctFaceMassFluxDevice(plans.pressure.mesh(), dPredictorFlux,
                                      pSystem.faceCoefficient, dPPrime,
                                      settings.nonOrthogonalCorrections > 1
                                          ? &pSystem.explicitFaceFlux
                                          : nullptr,
                                      dFluxNew);
  L.fluxNew = pull(dFluxNew);

  // Stage 10 / 11.
  L.imbalance = imbalanceOf(mesh, L.fluxNew);
  // The SAME production function as the CPU side, on the GPU's flux --
  // globalNetFlux sums over boundary PATCHES in patch order, so recomputing it
  // here by any other traversal would differ in rounding for reasons that have
  // nothing to do with the port.
  SurfaceField fluxField(nf);
  for (Index f = 0; f < nf; ++f) fluxField[f] = L.fluxNew[f];
  const auto continuity = cfd::physics::evaluateContinuity(mesh, fluxField);
  L.continuityResidual = rmsOf(std::vector<Real>(continuity.cellImbalance.begin(),
                                                 continuity.cellImbalance.end()));
  L.globalImbalance = std::abs(continuity.globalNetFlux);
  return L;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

void printLadder(const char* layer, const std::string& caseName, const LadderComparison& c,
                 bool bitwise, Real tolerance) {
  const bool ok = c.firstDivergentStage < 0;
  std::printf("  %s %s %-22s %s", ok ? "PASS" : "FAIL", layer, caseName.c_str(),
              bitwise ? "bitwise" : "tolerance");
  if (!ok) std::printf("  FIRST DIVERGENCE: %s", kStageNames[c.firstDivergentStage]);
  std::printf("\n");
  for (int s = 0; s < kStageCount; ++s) {
    const StageReport& r = c.stage[s];
    if (r.compared == 0) continue;
    const bool stageOk = bitwise ? r.differing == 0
                                 : r.maxAbs <= tolerance * std::max(r.scale, 1.0);
    std::printf("       %-26s n=%-8zu differing=%-8zu maxAbs=%-11.3g maxRel=%-11.3g scale=%-11.4g %s\n",
                kStageNames[s], r.compared, r.differing, r.maxAbs, r.maxRel, r.scale,
                stageOk ? "" : "  <-- DIVERGES");
  }
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

// F -- the ladder IS the production iteration.
void runFidelity(const Case& c, const Ladder& ladder) {
  const auto settings = settingsFor(c, 1);
  cfd::pressure_velocity::SIMPLE simple(settings, c.referenceCell);
  const auto produced = simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                                     c.velocity, c.pressure);
  const Index nc = c.mesh.numberOfCells();
  const Index nf = c.mesh.numberOfFaces();
  std::size_t differing = 0;
  for (Index i = 0; i < nc; ++i) {
    if (!sameBits(produced.velocity[i].x, ladder.uNew[i])) ++differing;
    if (!sameBits(produced.velocity[i].y, ladder.vNew[i])) ++differing;
    if (!sameBits(produced.velocity[i].z, ladder.wNew[i])) ++differing;
    if (!sameBits(produced.pressure[i], ladder.pressureNew[i])) ++differing;
  }
  for (Index f = 0; f < nf; ++f)
    if (!sameBits(produced.massFlux[f], ladder.fluxNew[f])) ++differing;
  std::size_t residualDiffering = 0;
  if (!sameBits(produced.finalUResidual, ladder.uResidual)) ++residualDiffering;
  if (!sameBits(produced.finalVResidual, ladder.vResidual)) ++residualDiffering;
  if (!sameBits(produced.finalWResidual, ladder.wResidual)) ++residualDiffering;
  if (!sameBits(produced.finalPressureResidual, ladder.pResidual)) ++residualDiffering;
  if (!sameBits(produced.finalContinuityResidual, ladder.continuityResidual)) ++residualDiffering;
  if (!sameBits(produced.globalMassImbalance, ladder.globalImbalance)) ++residualDiffering;

  ++cases;
  ++coverage.fidelityChecks;
  const bool ok = differing == 0 && residualDiffering == 0 && produced.iterations == 1;
  if (!ok) ++failures;
  std::printf("  %s F  %-22s ladder == SIMPLE::solve(maxIterations=1)  fields[d=%zu] "
              "residuals[d=%zu] iterations=%zu status=%d\n",
              ok ? "PASS" : "FAIL", c.name.c_str(), differing, residualDiffering,
              static_cast<std::size_t>(produced.iterations),
              static_cast<int>(produced.status));
}

// V -- invariants that do not go through CPU/GPU equality.
void runInvariants(const Case& c, const Ladder& cpu, const Ladder& gpu, bool pinned) {
  const Mesh& mesh = c.mesh;
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  std::size_t errors = 0;

  // Finiteness, both backends.
  const auto finite = [&](const std::vector<Real>& v) {
    for (const Real x : v) if (!std::isfinite(x)) return false;
    return true;
  };
  for (const auto* L : {&cpu, &gpu}) {
    if (!finite(L->uNew) || !finite(L->vNew) || !finite(L->wNew)) ++errors;
    if (!finite(L->pressureNew) || !finite(L->fluxNew) || !finite(L->pPrime)) ++errors;
  }

  // Owner/neighbour cancellation on the corrected flux -- exactly representable.
  std::vector<int> ownerVisits(static_cast<std::size_t>(nf), 0), neighbourVisits(static_cast<std::size_t>(nf), 0);
  for (Index cell = 0; cell < nc; ++cell)
    for (const Index faceId : mesh.cell(cell).faceIds()) {
      if (mesh.face(faceId).owner() == cell) ++ownerVisits[faceId];
      else ++neighbourVisits[faceId];
    }
  std::size_t pairs = 0;
  for (Index f = 0; f < nf; ++f) {
    const bool boundary = mesh.face(f).isBoundary();
    if (ownerVisits[f] != 1 || neighbourVisits[f] != (boundary ? 0 : 1)) { ++errors; continue; }
    if (boundary) continue;
    if (!sameBits(gpu.fluxNew[f] + (-gpu.fluxNew[f]), 0.0)) ++errors;
    ++pairs;
  }
  if (pairs == 0) ++errors;

  // The 2D W contract: correctVelocity returns Vector2{x,y}, so a 2D solve's
  // corrected w is exactly +0.0 on BOTH backends.
  if (!threeD) {
    for (Index i = 0; i < nc; ++i) {
      if (!sameBits(cpu.wNew[i], 0.0)) ++errors;
      if (!sameBits(gpu.wNew[i], 0.0)) ++errors;
    }
  }

  // Reference/pin behaviour: with no FixedValue pressure patch the reference
  // row is the identity equation, so p'[reference] is exactly 0.
  if (pinned) {
    if (!sameBits(cpu.pPrime[c.referenceCell], 0.0)) ++errors;
    if (!sameBits(gpu.pPrime[c.referenceCell], 0.0)) ++errors;
  }

  // Boundary conditions remain satisfied: an Inlet/Wall face's flux is fixed by
  // its BC, and the correction leaves it untouched, because the assembly gave
  // it a zero coupling. Checked against the PREDICTOR, independently of the CPU.
  std::size_t untouched = 0;
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    if (!face.isBoundary()) continue;
    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, f, c.pressureBoundaries);
    if (bc.type() == cfd::boundary::BoundaryConditionType::FixedValue) continue;
    if (gpu.fluxNew[f] != gpu.predictorFlux[f]) ++errors;
    ++untouched;
  }
  if (untouched == 0 && pinned) ++errors;  // a closed case must have such faces

  ++cases;
  ++coverage.invariantCases;
  if (errors != 0) ++failures;
  std::printf("  %s V  %-22s invariants errors=%zu pairs=%zu untouchedBoundaryFaces=%zu\n",
              errors == 0 ? "PASS" : "FAIL", c.name.c_str(), errors, pairs, untouched);
}

// Determinism: each backend repeated must give identical results.
void runDeterminism(const Case& c, const cfd::pressure_velocity::SIMPLESettings& settings,
                    DevicePlans& plans, const Ladder& cpuFirst, const Ladder& gpuFirst) {
  const Ladder cpuAgain = runCpuLadder(c, settings, nullptr);
  const Ladder gpuAgain = runGpuLadder(c, settings, plans, nullptr);
  const auto identical = [](const std::vector<Real>& a, const std::vector<Real>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!sameBits(a[i], b[i])) return false;
    return true;
  };
  std::size_t errors = 0;
  if (!identical(cpuFirst.uNew, cpuAgain.uNew) || !identical(cpuFirst.vNew, cpuAgain.vNew) ||
      !identical(cpuFirst.pressureNew, cpuAgain.pressureNew) ||
      !identical(cpuFirst.fluxNew, cpuAgain.fluxNew)) ++errors;
  if (!identical(gpuFirst.uNew, gpuAgain.uNew) || !identical(gpuFirst.vNew, gpuAgain.vNew) ||
      !identical(gpuFirst.pressureNew, gpuAgain.pressureNew) ||
      !identical(gpuFirst.fluxNew, gpuAgain.fluxNew)) ++errors;
  ++cases;
  ++coverage.determinismChecks;
  if (errors != 0) ++failures;
  std::printf("  %s D  %-22s determinism (each backend repeated) errors=%zu\n",
              errors == 0 ? "PASS" : "FAIL", c.name.c_str(), errors);
}

// ---------------------------------------------------------------------------
// One case, all layers
// ---------------------------------------------------------------------------

void runCase(Case c, bool quick) {
  const Mesh& mesh = c.mesh;
  const bool threeD = mesh.dimension() == 3;
  const auto settings = settingsFor(c, 1);
  const FaceFluxScheme scheme =
      cfd::pressure_velocity::resolveFaceFluxScheme(settings.faceFlux, mesh.dimension());
  if (scheme == FaceFluxScheme::RhieChow) ++coverage.rhieChowCases; else ++coverage.linearCases;
  if (threeD) ++coverage.threeD; else ++coverage.twoD;

  DevicePlans plans;
  if (!plans.build(c)) {
    ++failures;
    std::printf("  FAIL %-22s a device plan is unusable\n", c.name.c_str());
    return;
  }
  const bool pinned = plans.pressure.pinReferenceCell();
  if (pinned) ++coverage.pinnedCases; else ++coverage.openCases;
  std::printf("       [%s cells=%zu faces=%zu %s pin=%s]\n", c.name.c_str(),
              static_cast<std::size_t>(mesh.numberOfCells()),
              static_cast<std::size_t>(mesh.numberOfFaces()),
              scheme == FaceFluxScheme::RhieChow ? "rhie-chow" : "linear",
              pinned ? "yes" : "no");

  // --- the CPU ladder, capturing its solver outputs ----------------------
  FedSolutions fed;
  const Ladder cpu = runCpuLadder(c, settings, &fed);
  if (!cpu.converged) {
    ++failures;
    std::printf("  FAIL %-22s the CPU reference solve did not converge\n", c.name.c_str());
    return;
  }

  // The momentum MATRIX in this codebase is component-independent: in both the
  // diffusion and the convection boundary terms the component enters only the
  // RHS, via selectComponent(uB, component) -- the diagonal contribution
  // `builder.add(ownerId, ownerId, coefficient)` is added for every boundary
  // face regardless of the condition's type (MomentumEquation.cpp:136, :236,
  // :298). Interior faces share one mass flux, one geometry and one viscosity.
  //
  // So dU, dV and dW are EXACTLY equal, always. That is asserted here rather
  // than assumed, because it is what makes negative control L3 -- feeding dV
  // where dU belongs -- provably null. If a future change ever made the
  // diagonal component-dependent, this fires and L3 stops being null.
  {
    std::size_t separated = 0;
    for (std::size_t i = 0; i < cpu.dU.size(); ++i) {
      if (!sameBits(cpu.dU[i], cpu.dV[i])) ++separated;
      if (threeD && !cpu.dW.empty() && !sameBits(cpu.dU[i], cpu.dW[i])) ++separated;
    }
    ++cases;
    if (separated != 0) ++failures; else ++coverage.identicalResponseCases;
    std::printf("  %s R  %-22s response coefficients component-independent (dU == dV%s) "
                "separated=%zu\n",
                separated == 0 ? "PASS" : "FAIL", c.name.c_str(), threeD ? " == dW" : "",
                separated);
  }

  // --- F: the ladder is the production iteration -------------------------
  runFidelity(c, cpu);

  // --- C: controlled, BITWISE --------------------------------------------
  {
    const Ladder gpu = runGpuLadder(c, settings, plans, &fed);
    LadderComparison comparison;
    compareLadders(cpu, gpu, comparison);
    comparison.finish(/*bitwise=*/true, 0.0);
    ++cases;
    ++coverage.controlledLadders;
    if (comparison.firstDivergentStage >= 0) ++failures;
    printLadder("C ", c.name, comparison, true, 0.0);
    runInvariants(c, cpu, gpu, pinned);
  }

  // --- I: independent solves, existing tolerances ------------------------
  {
    const Ladder gpu = runGpuLadder(c, settings, plans, nullptr);
    LadderComparison comparison;
    compareLadders(cpu, gpu, comparison);
    // The project's own linear-solver tolerance, unchanged.
    //
    // Stages 1 and 3 are the ONLY ones upstream of every solve -- the momentum
    // systems, and the response coefficients, which are a pure function of the
    // assembly diagonal. They must stay bitwise even when the two backends
    // solve independently, and that is asserted separately rather than hidden
    // inside a tolerance.
    //
    // Stage 5 is NOT upstream, despite looking like an assembly: the pressure
    // system is built from the predicted flux (stage 4), which is built from
    // the momentum solutions (stage 2). Requiring it bitwise here was an error
    // in the first version of this criterion -- it demanded that two different
    // linear solves agree exactly. Everything from stage 2 onward is
    // solver-limited by dataflow and is held to the tolerance.
    const Real tolerance = 1e-6;
    comparison.finish(/*bitwise=*/false, tolerance);
    ++cases;
    ++coverage.independentLadders;
    const bool upstreamBitwise = comparison.stage[kMomentumSystems].differing == 0 &&
                                 comparison.stage[kResponseCoefficients].differing == 0;
    if (comparison.firstDivergentStage >= 0 || !upstreamBitwise || !gpu.converged) ++failures;
    printLadder("I ", c.name, comparison, false, tolerance);
    std::printf("       pre-solve stages (1,3) bitwise: %s   gpu solves converged: %s "
                "pressure restarts: cpu=%zu gpu=%zu\n",
                upstreamBitwise ? "yes" : "NO", gpu.converged ? "yes" : "NO",
                static_cast<std::size_t>(cpu.pRestarts), static_cast<std::size_t>(gpu.pRestarts));
    if (!gpu.converged) {
      std::printf("       classify against the recorded GPU BiCGSTAB restart asymmetry before "
                  "treating this as an integration regression\n");
    }
    if (!quick) runDeterminism(c, settings, plans, cpu, gpu);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001L: one production SIMPLE iteration, CPU vs CUDA ===\n");
  std::printf("CPU reference: SIMPLE::solve outer loop (SIMPLE.cpp:271)\n\n");

  runCase(lidDrivenCavity("cavity 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)),
          quick);
  runCase(inletOutlet("channel 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)), quick);
  runCase(symmetryDuct("duct 2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)), quick);
  if (!quick) {
    runCase(lidDrivenCavity("cavity 2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0)), quick);
    runCase(lidDrivenCavity("cavity 3d 4",
                            MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)), quick);
    runCase(inletOutlet("channel 3d 4",
                        MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0)), quick);
    runCase(inletOutlet("warped 3d 3", warped3D(3)), quick);
  } else {
    runCase(lidDrivenCavity("cavity 3d 3",
                            MeshGeometry::createCartesian3D(3, 3, 3, 1.0, 1.0, 1.0)), quick);
  }

  std::printf("\n=== coverage ===\n");
  std::printf("  2D / 3D cases                     %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  linear / rhie-chow predictor      %d / %d\n", coverage.linearCases,
              coverage.rhieChowCases);
  std::printf("  pinned / open pressure cases      %d / %d\n", coverage.pinnedCases,
              coverage.openCases);
  std::printf("  fidelity checks                   %d\n", coverage.fidelityChecks);
  std::printf("  controlled ladders (bitwise)      %d\n", coverage.controlledLadders);
  std::printf("  independent ladders (tolerance)   %d\n", coverage.independentLadders);
  std::printf("  invariant cases                   %d\n", coverage.invariantCases);
  std::printf("  determinism checks                %d\n", coverage.determinismChecks);
  std::printf("  cases with dU == dV (== dW)       %d\n", coverage.identicalResponseCases);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical                 %zu\n", bitwiseValues);

  if (coverage.twoD == 0 || coverage.threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (coverage.linearCases == 0 || coverage.rhieChowCases == 0) { ++failures; std::printf("  FAIL both predictor schemes not exercised\n"); }
  if (coverage.pinnedCases == 0 || coverage.openCases == 0) { ++failures; std::printf("  FAIL both pinned and open pressure cases not exercised\n"); }
  if (coverage.fidelityChecks == 0) { ++failures; std::printf("  FAIL the ladder was never checked against SIMPLE::solve\n"); }
  if (coverage.controlledLadders == 0) { ++failures; std::printf("  FAIL the controlled ladder never ran\n"); }
  if (coverage.identicalResponseCases == 0) { ++failures; std::printf("  FAIL the component-independence of the response coefficients was never checked\n"); }
  if (!quick && coverage.determinismChecks == 0) { ++failures; std::printf("  FAIL determinism was never checked\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "SINGLE ITERATION EQUIVALENCE: PASS"
                                    : "SINGLE ITERATION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
