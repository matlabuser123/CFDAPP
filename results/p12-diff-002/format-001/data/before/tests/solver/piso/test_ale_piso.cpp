// P12-MESH-007: ALE PISO on prescribed moving meshes
// (results/p12-mesh-007/acceptance_gate.md G1.2, G5 (primary), G6.3/G6.4,
// G7, G8, with Amendment A2 for G5.4).
//
// Kinds of check:
//   G1.2 -- regression: with a stationary motion AlePISO IS PISO, bit for bit;
//   G5   -- verification of an exact discrete property: a uniform flow is an
//           exact solution of the GCL-consistent ALE scheme on any moving mesh;
//           two GCL-inconsistent variants (N1, N2; test-only, through the
//           library-internal PISO step) must violate it (discrimination);
//   G6.3 -- verification by Galilean invariance: a cavity translating with the
//           mesh equals the fixed cavity plus the translation velocity;
//   G7   -- verification against exact solutions (the piston-driven channel,
//           Couette flow in a translating mesh) and refusal of inconsistent
//           wall motion;
//   G8   -- local and global ALE mass conservation of every run.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "MeshMotionCases.hpp"
#include "PisoStep.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/VTKWriter.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/AlePISO.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"
#include "cfd/solver/TimeController.hpp"
#include "cfd/solver/TransientSolver.hpp"

namespace {

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometryState;
using cfd::mesh::MeshMotion;
using cfd::mesh::MeshMotionStep;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::AlePISO;
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::solver::TransientState;
using cfd::solver::TransientStepResult;
using cfd::solver::TransientStepStatus;

constexpr Real kRho = 1.0;
constexpr Real kMu = 0.01;
const FluidProperties kFluid(kRho, kMu);

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

PISOSettings gateSettings() {
  PISOSettings s;
  s.momentumSolver.absoluteTolerance = 1e-15;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-15;
  s.pressureSolver.relativeTolerance = 1e-12;
  s.pressureSolver.maxIterations = 20000;
  return s;
}

using BcSpec = std::vector<std::pair<std::string, std::function<std::unique_ptr<cfd::boundary::BoundaryCondition>()>>>;

BoundaryConditionSet makeSet(const Mesh& mesh, const BcSpec& spec) {
  BoundaryConditionSet set;
  for (const auto& [patch, make] : spec) set.set(mesh, patch, make());
  return set;
}

BoundaryConditionSet everywhere(const Mesh& mesh,
                                const std::function<std::unique_ptr<cfd::boundary::BoundaryCondition>()>& make) {
  BoundaryConditionSet set;
  for (const auto& patch : mesh.boundaryPatches()) set.set(mesh, patch.name(), make());
  return set;
}

std::unique_ptr<cfd::boundary::BoundaryCondition> inlet(Vector3 u) {
  return std::make_unique<cfd::boundary::Inlet>(u);
}
std::unique_ptr<cfd::boundary::BoundaryCondition> movingWall(Vector3 u) {
  return std::make_unique<cfd::boundary::MovingWall>(u);
}
std::unique_ptr<cfd::boundary::BoundaryCondition> wall() { return std::make_unique<cfd::boundary::Wall>(); }
std::unique_ptr<cfd::boundary::BoundaryCondition> outlet() { return std::make_unique<cfd::boundary::Outlet>(); }
std::unique_ptr<cfd::boundary::BoundaryCondition> symmetry() {
  return std::make_unique<cfd::boundary::Symmetry>();
}
std::unique_ptr<cfd::boundary::BoundaryCondition> zeroGradient() {
  return std::make_unique<cfd::boundary::FixedGradient>(0.0);
}
std::unique_ptr<cfd::boundary::BoundaryCondition> fixedPressure(Real p) {
  return std::make_unique<cfd::boundary::FixedValue>(p);
}

TransientState initialState(const Mesh& mesh, const VectorField& u, Real p,
                            const BoundaryConditionSet& velocityBcs) {
  return TransientState{u, ScalarField(mesh.numberOfCells(), p),
                        cfd::physics::calculateMassFlux(mesh, u, kFluid, velocityBcs)};
}

ScalarField toScalar(const std::vector<Real>& v) {
  ScalarField f(v.size());
  for (std::size_t i = 0; i < v.size(); ++i) f[i] = v[i];
  return f;
}
SurfaceField toSurface(const std::vector<Real>& v) {
  SurfaceField f(v.size());
  for (std::size_t i = 0; i < v.size(); ++i) f[i] = v[i];
  return f;
}

// --- G1.2 -----------------------------------------------------------------------------------

struct Flow {
  std::string name;
  Mesh (*make)();
  BcSpec velocity;
  BcSpec pressure;
};

TEST(AlePisoStaticLimit, StationaryMotionIsPisoBitForBit) {
  const BcSpec zeroGradientAll = {{"left", zeroGradient}, {"right", zeroGradient},
                                  {"bottom", zeroGradient}, {"top", zeroGradient}};
  const BcSpec channelPressure = {{"left", zeroGradient}, {"right", [] { return fixedPressure(0.0); }},
                                  {"bottom", zeroGradient}, {"top", zeroGradient}};
  const std::vector<Flow> flows = {
      {"C16 cavity", &m7::c16,
       {{"left", wall}, {"right", wall}, {"bottom", wall}, {"top", [] { return movingWall({1.0, 0.0, 0.0}); }}},
       zeroGradientAll},
      {"Q16 channel", &m7::q16,
       {{"left", [] { return inlet({1.0, 0.0, 0.0}); }}, {"right", outlet}, {"bottom", wall}, {"top", wall}},
       channelPressure},
      {"MB2 channel", &m7::mb2,
       {{"left", [] { return inlet({1.0, 0.0, 0.0}); }}, {"right", outlet}, {"bottom", symmetry}, {"top", symmetry}},
       channelPressure}};
  for (const Flow& flow : flows) {
    const Mesh staticMesh = flow.make();
    Mesh movingMesh = flow.make();
    const BoundaryConditionSet vStatic = makeSet(staticMesh, flow.velocity);
    const BoundaryConditionSet pStatic = makeSet(staticMesh, flow.pressure);
    const BoundaryConditionSet vAle = makeSet(movingMesh, flow.velocity);
    const BoundaryConditionSet pAle = makeSet(movingMesh, flow.pressure);
    const PISO piso(staticMesh, kFluid, vStatic, pStatic, gateSettings(), 0);
    MeshMotion motion(movingMesh, m7::stationary());
    const AlePISO ale(motion, kFluid, vAle, pAle, gateSettings(), 0);
    const VectorField u0(staticMesh.numberOfCells(), Vector3{});
    TransientState a = initialState(staticMesh, u0, 0.0, vStatic);
    TransientState b = initialState(movingMesh, u0, 0.0, vAle);
    for (int n = 1; n <= 10; ++n) {
      const TransientStepResult ra = piso.solveTimeStep(a, 0.01);
      const TransientStepResult rb = ale.solveTimeStep(b, 0.01);
      ASSERT_EQ(ra.status, TransientStepStatus::Converged) << flow.name;
      ASSERT_EQ(rb.status, TransientStepStatus::Converged) << flow.name;
      bool same = sameBits(ra.maxCFL, rb.maxCFL) && sameBits(ra.continuityResidual, rb.continuityResidual) &&
                  sameBits(ra.massImbalance, rb.massImbalance);
      for (Index c = 0; c < staticMesh.numberOfCells(); ++c) {
        same = same && sameBits(ra.state.velocity[c].x, rb.state.velocity[c].x) &&
               sameBits(ra.state.velocity[c].y, rb.state.velocity[c].y) &&
               sameBits(ra.state.pressure[c], rb.state.pressure[c]);
      }
      for (Index f = 0; f < staticMesh.numberOfFaces(); ++f) {
        same = same && sameBits(ra.state.massFlux[f], rb.state.massFlux[f]);
      }
      EXPECT_TRUE(same) << flow.name << " step " << n;
      a = ra.state;
      b = rb.state;
    }
    std::printf("G1.2 %s: AlePISO(STAT) vs PISO, 10 steps -- velocity, pressure, flux, CFL, continuity, "
                "mass imbalance BITWISE identical\n",
                flow.name.c_str());
  }
}

// --- G5 (primary) + G4 + G8 for the uniform-flow runs ------------------------------------------

enum class Formulation { Ale, NoMeshFlux /* N1 */, NoVolumeChange /* N2 */, Static };

const char* formulationName(Formulation f) {
  switch (f) {
    case Formulation::Ale: return "ALE";
    case Formulation::NoMeshFlux: return "N1 no-mesh-flux";
    case Formulation::NoVolumeChange: return "N2 V^{n+1}-both-levels";
    case Formulation::Static: return "static";
  }
  return "?";
}

struct UniformRun {
  bool allConverged{true};
  int steps{0};
  Real maxU{0.0};  // max |u - u0| / |u0|
  Real maxV{0.0};
  Real maxP{0.0};  // max |p - p0| / (rho |u0|^2)
  Real maxContinuity{0.0};
  Real maxGcl{0.0};
  Real maxGclRatio{0.0};  // |r_P| / (256 eps X^2)
  Real maxAleMass{0.0};
  Real maxGlobalAleMass{0.0};
  Real maxDomainVolumeError{0.0};  // |sum V - analytic| / (N 16 eps X^2)
  Real minVolume{1e300};
  Real maxDisplacement{0.0};
  Real maxRelativeVolumeChange{0.0};
  Real maxCornerAngleChange{0.0};  // degrees
  Real maxCfl{0.0};
};

Real cornerAngle(const Vector3& prev, const Vector3& at, const Vector3& next) {
  const Vector3 a = prev - at;
  const Vector3 b = next - at;
  return std::acos(std::clamp(dot(a, b) / (magnitude(a) * magnitude(b)), -1.0, 1.0)) * 180.0 /
         cfd::constants::pi;
}

// The 20-step uniform-flow run of acceptance_gate.md G5 on `mesh` moved by
// `prescribed`, with the formulation under test. domainVolume(tau) is the
// analytical total volume. Prints one line per step (G5.5).
UniformRun runUniform(const std::string& name, Mesh mesh, const m7::Motion& prescribed,
                      Formulation formulation, const std::function<Real(Real, Real)>& domainVolume) {
  const Vector3 u0{1.0, 0.5, 0.0};
  const Real p0 = 1.0;
  const Real speed = magnitude(u0);
  const Real dt = 0.02;
  const BoundaryConditionSet velocityBcs = everywhere(mesh, [&] { return inlet(u0); });
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  Real totalReference = 0.0;
  for (const auto& cell : mesh.cells()) totalReference += cell.volume();
  MeshMotion motion(mesh, prescribed);
  const auto& topology = motion.topology();
  const std::vector<Vector3> reference = motion.referenceVertices();
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  const PISO piso(mesh, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  TransientState state = initialState(mesh, VectorField(mesh.numberOfCells(), u0), p0, velocityBcs);
  const Real fluxRef = kRho * speed * (1.0 / 16.0);
  UniformRun run;
  for (int n = 1; n <= m7::kSteps; ++n) {
    TransientStepResult result;
    if (formulation == Formulation::Ale) {
      result = ale.solveTimeStep(state, dt);
    } else {
      (void)motion.advance(motion.time() + dt);
      const MeshMotionStep& s = motion.lastStep();
      if (formulation == Formulation::Static) {
        result = piso.solveTimeStep(state, dt);
      } else {
        ScalarField previousVolume = toScalar(s.previousVolumes);
        if (formulation == Formulation::NoVolumeChange) {
          for (Index c = 0; c < mesh.numberOfCells(); ++c) previousVolume[c] = mesh.cell(c).volume();
        }
        const SurfaceField convecting =
            formulation == Formulation::NoMeshFlux
                ? state.massFlux
                : cfd::physics::relativeMassFlux(state.massFlux, toSurface(s.meshVolumeFlux), kRho);
        const cfd::pressure_velocity::detail::AleStepTerms terms{&previousVolume, &convecting};
        result = cfd::pressure_velocity::detail::solvePisoStep(mesh, kFluid, velocityBcs, pressureBcs,
                                                                gateSettings(), 0, nullptr, state, dt, &terms);
      }
    }
    if (result.status != TransientStepStatus::Converged) {
      run.allConverged = false;
      std::printf("  %s %s step %d: status %d\n", name.c_str(), formulationName(formulation), n,
                  static_cast<int>(result.status));
      break;
    }
    const MeshMotionStep& step = motion.lastStep();
    state = result.state;
    run.steps = n;
    Real du = 0.0, dv = 0.0, dp = 0.0;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      du = std::max(du, std::abs(state.velocity[c].x - u0.x) / speed);
      dv = std::max(dv, std::abs(state.velocity[c].y - u0.y) / speed);
      dp = std::max(dp, std::abs(state.pressure[c] - p0) / (kRho * speed * speed));
    }
    run.maxU = std::max(run.maxU, du);
    run.maxV = std::max(run.maxV, dv);
    run.maxP = std::max(run.maxP, dp);
    run.maxContinuity = std::max(run.maxContinuity, result.continuityResidual);
    run.maxCfl = std::max(run.maxCfl, result.maxCFL);
    const Real x = step.maxCoordinate;
    run.maxGcl = std::max(run.maxGcl, step.maxAbsGclResidual);
    run.maxGclRatio = std::max(run.maxGclRatio, step.maxAbsGclResidual / (256 * m7::kEps * x * x));
    const auto conservation = cfd::pressure_velocity::evaluateAleConservation(mesh, step, state.massFlux, kRho);
    run.maxAleMass = std::max(run.maxAleMass, conservation.maxCellMassResidual / fluxRef);
    run.maxGlobalAleMass = std::max(run.maxGlobalAleMass, std::abs(conservation.globalMassResidual) / fluxRef);
    const Real analytic = domainVolume(step.time, totalReference);
    run.maxDomainVolumeError =
        std::max(run.maxDomainVolumeError, std::abs(step.totalVolume - analytic) /
                                               (static_cast<Real>(mesh.numberOfCells()) * 16 * m7::kEps * x * x));
    run.minVolume = std::min(run.minVolume, step.minVolume);
    run.maxDisplacement = std::max(run.maxDisplacement, step.maxDisplacement);
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      run.maxRelativeVolumeChange = std::max(
          run.maxRelativeVolumeChange, std::abs(mesh.cell(c).volume() - step.previousVolumes[c]) / step.previousVolumes[c]);
      const auto& corner = topology.cells[c].corner;
      const std::vector<Vector3>& now = motion.currentVertices();
      for (int k = 0; k < 4; ++k) {
        const Index a = corner[static_cast<std::size_t>((k + 3) % 4)];
        const Index b = corner[static_cast<std::size_t>(k)];
        const Index d = corner[static_cast<std::size_t>((k + 1) % 4)];
        run.maxCornerAngleChange = std::max(
            run.maxCornerAngleChange, std::abs(cornerAngle(now[a], now[b], now[d]) -
                                               cornerAngle(reference[a], reference[b], reference[d])));
      }
    }
    std::printf("  %s %s step %2d t %.2f: |u-u0| %.3e |v-v0| %.3e |p-p0| %.3e continuity %.3e mass %.3e "
                "GCL %.3e (global %.3e) R_P/F %.3e minV %.4e disp %.4f CFL %.3f\n",
                name.c_str(), formulationName(formulation), n, step.time, du, dv, dp,
                result.continuityResidual, result.massImbalance, step.maxAbsGclResidual,
                step.globalGclResidual, conservation.maxCellMassResidual / fluxRef, step.minVolume,
                step.maxDisplacement, result.maxCFL);
  }
  return run;
}

Real constantVolume(Real, Real reference) { return reference; }
Real expandingVolume(Real t, Real reference) { return (1.0 + (0.5 * t)) * (1.0 + (0.5 * t)) * reference; }

struct UniformCase {
  std::string name;
  Mesh (*make)();
  m7::Motion motion;
  Real (*volume)(Real, Real);
  bool volumeChanging;
  bool shear;
};

std::vector<UniformCase> uniformCases() {
  return {{"U1 C16/SN2", &m7::c16, m7::sn2(), &constantVolume, true, false},
          {"U2 C16/TR2", &m7::c16, m7::affine(m7::tr2Spec()), &constantVolume, false, false},
          {"U3 C16/EX2", &m7::c16, m7::affine(m7::ex2Spec()), &expandingVolume, true, false},
          {"U4 C16/SH2", &m7::c16, m7::affine(m7::sh2Spec()), &constantVolume, false, true},
          {"U5 Q16/SN2", &m7::q16, m7::sn2(), &constantVolume, true, false},
          {"U6 MB2/SN2", &m7::mb2, m7::sn2(), &constantVolume, true, false},
          {"U7 G16/EX2", &m7::g16, m7::affine(m7::ex2Spec()), &expandingVolume, true, false}};
}

void expectUniformGate(const std::string& name, const UniformRun& r, const UniformCase& c) {
  EXPECT_TRUE(r.allConverged) << name;
  EXPECT_EQ(r.steps, m7::kSteps) << name;
  EXPECT_LE(r.maxU, 1e-9) << name;  // G5.1
  EXPECT_LE(r.maxV, 1e-9) << name;
  EXPECT_LE(r.maxP, 1e-8) << name;  // G5.2
  EXPECT_GT(r.minVolume, 0.0) << name;  // G5.3
  EXPECT_GE(r.maxDisplacement, 0.25 / 16.0) << name;
  if (c.volumeChanging) {
    EXPECT_GE(r.maxRelativeVolumeChange, 0.01) << name;
  }
  if (c.shear) {
    EXPECT_GE(r.maxCornerAngleChange, 5.0) << name;
  }
  EXPECT_LE(r.maxGclRatio, 1.0) << name;  // G4.1
  EXPECT_LE(r.maxAleMass, 1e-9) << name;  // G8.1
  EXPECT_LE(r.maxGlobalAleMass, 256 * 1e-9) << name;  // G8.2 (N = 256)
  EXPECT_LE(r.maxDomainVolumeError, 1.0) << name;  // G8.4
}

TEST(AlePisoUniformFlow, UniformFlowStaysUniformUnderPrescribedMotion) {
  for (const UniformCase& c : uniformCases()) {
    const UniformRun r = runUniform(c.name, c.make(), c.motion, Formulation::Ale, c.volume);
    expectUniformGate(c.name, r, c);
    std::printf("G5 %s ALE: %d steps, max |u-u0|/|u0| %.3e |v-v0|/|u0| %.3e (<= 1e-9), |p-p0|/(rho|u0|^2) "
                "%.3e (<= 1e-8); continuity %.3e; GCL/bound %.3e; R_P/F_ref %.3e global %.3e; domain "
                "volume/bound %.3e; min V %.4e; max displacement %.4f; max |dV|/V %.3e; max corner-angle "
                "change %.2f deg; max CFL %.3f\n",
                c.name.c_str(), r.steps, r.maxU, r.maxV, r.maxP, r.maxContinuity, r.maxGclRatio,
                r.maxAleMass, r.maxGlobalAleMass, r.maxDomainVolumeError, r.minVolume,
                r.maxDisplacement, r.maxRelativeVolumeChange, r.maxCornerAngleChange, r.maxCfl);
  }
}

// G5.4 (Amendment A2): GCL-inconsistent variants must break uniform flow; the
// static formulation is recorded (it preserves uniform flow by construction).
TEST(AlePisoUniformFlow, GclInconsistentVariantsBreakUniformFlow) {
  for (const UniformCase& c : uniformCases()) {
    if (c.name.rfind("U1", 0) != 0 && c.name.rfind("U3", 0) != 0) continue;
    const UniformRun n1 = runUniform(c.name, c.make(), c.motion, Formulation::NoMeshFlux, c.volume);
    const UniformRun n2 = runUniform(c.name, c.make(), c.motion, Formulation::NoVolumeChange, c.volume);
    const UniformRun st = runUniform(c.name, c.make(), c.motion, Formulation::Static, c.volume);
    EXPECT_GE(std::max(n1.maxU, n1.maxV), 1e-5) << c.name;
    EXPECT_GE(std::max(n2.maxU, n2.maxV), 1e-5) << c.name;
    std::printf("G5.4 %s: max velocity deviation -- N1 (no mesh flux) %.3e, N2 (V^{n+1} both levels) "
                "%.3e (each >= 1e-5); static formulation %.3e (recorded; uniform by construction)\n",
                c.name.c_str(), std::max(n1.maxU, n1.maxV), std::max(n2.maxU, n2.maxV),
                std::max(st.maxU, st.maxV));
  }
}

// --- G6.3 / G6.4 / G7.3: Galilean invariance of a translating cavity ----------------------------

struct CavityComparison {
  Real maxVelocity{0.0};
  Real maxPressure{0.0};
  Real maxFlux{0.0};
  bool converged{true};
};

CavityComparison translatingCavity(bool aleTerms) {
  const Vector3 b{0.5, 0.25, 0.0};
  const Real dt = 0.01;
  // A: the fixed cavity.
  const Mesh meshA = m7::c16();
  const BoundaryConditionSet vA = makeSet(meshA, {{"left", wall}, {"right", wall}, {"bottom", wall},
                                                  {"top", [] { return movingWall({1.0, 0.0, 0.0}); }}});
  const BoundaryConditionSet pA = everywhere(meshA, zeroGradient);
  const PISO pisoA(meshA, kFluid, vA, pA, gateSettings(), 0);
  TransientState a = initialState(meshA, VectorField(meshA.numberOfCells(), Vector3{}), 0.0, vA);
  // B: the same cavity translating with the mesh at b; walls MovingWall(b), lid b + (1, 0).
  Mesh meshB = m7::c16();
  const BoundaryConditionSet vB =
      makeSet(meshB, {{"left", [&] { return movingWall(b); }}, {"right", [&] { return movingWall(b); }},
                      {"bottom", [&] { return movingWall(b); }},
                      {"top", [&] { return movingWall(b + Vector3{1.0, 0.0, 0.0}); }}});
  const BoundaryConditionSet pB = everywhere(meshB, zeroGradient);
  MeshMotion motion(meshB, m7::affine(m7::tc2Spec()));
  const AlePISO aleB(motion, kFluid, vB, pB, gateSettings(), 0);
  const PISO staticB(meshB, kFluid, vB, pB, gateSettings(), 0);
  TransientState bState = initialState(meshB, VectorField(meshB.numberOfCells(), b), 0.0, vB);
  CavityComparison cmp;
  for (int n = 1; n <= m7::kSteps; ++n) {
    const TransientStepResult ra = pisoA.solveTimeStep(a, dt);
    TransientStepResult rb;
    if (aleTerms) {
      rb = aleB.solveTimeStep(bState, dt);
    } else {
      (void)motion.advance(motion.time() + dt);
      rb = staticB.solveTimeStep(bState, dt);
    }
    if (ra.status != TransientStepStatus::Converged || rb.status != TransientStepStatus::Converged) {
      cmp.converged = false;
      return cmp;
    }
    a = ra.state;
    bState = rb.state;
    Real dvel = 0.0, dpre = 0.0, dflux = 0.0;
    for (Index c = 0; c < meshA.numberOfCells(); ++c) {
      dvel = std::max(dvel, magnitude((bState.velocity[c] - b) - a.velocity[c]));
      dpre = std::max(dpre, std::abs((bState.pressure[c] - bState.pressure[0]) - (a.pressure[c] - a.pressure[0])));
    }
    const SurfaceField relative =
        cfd::physics::relativeMassFlux(bState.massFlux, toSurface(motion.lastStep().meshVolumeFlux), kRho);
    for (Index f = 0; f < meshA.numberOfFaces(); ++f) dflux = std::max(dflux, std::abs(relative[f] - a.massFlux[f]));
    cmp.maxVelocity = std::max(cmp.maxVelocity, dvel);
    cmp.maxPressure = std::max(cmp.maxPressure, dpre);
    cmp.maxFlux = std::max(cmp.maxFlux, dflux);
    if (aleTerms) {
      const auto conservation =
          cfd::pressure_velocity::evaluateAleConservation(meshB, motion.lastStep(), bState.massFlux, kRho);
      EXPECT_LE(conservation.maxCellMassResidual, 1e-9 * kRho * 1.0 / 16.0) << "G8.1 step " << n;
      EXPECT_LE(std::abs(conservation.globalMassResidual), 256 * 1e-9 * kRho * 1.0 / 16.0) << "G8.2 step " << n;
      std::printf("  G6.3 step %2d: max |u_B - b - u_A| %.3e, |dp| %.3e, |F_rel,B - F_A| %.3e, R_P %.3e, "
                  "global %.3e\n",
                  n, dvel, dpre, dflux, conservation.maxCellMassResidual,
                  conservation.globalMassResidual);
    }
  }
  return cmp;
}

TEST(AlePisoGalilean, TranslatingCavityIsTheFixedCavityPlusTheTranslation) {
  const CavityComparison ale = translatingCavity(true);
  ASSERT_TRUE(ale.converged);
  EXPECT_LE(ale.maxVelocity, 1e-8);
  EXPECT_LE(ale.maxPressure, 1e-8);
  EXPECT_LE(ale.maxFlux, 1e-8 / 16.0);
  std::printf("G6.3 translating cavity (ALE): max |u_B - b - u_A| %.3e (<= 1e-8), gauge-aligned |p| %.3e "
              "(<= 1e-8), |F_rel,B - F_A| %.3e (<= 6.25e-10)\n",
              ale.maxVelocity, ale.maxPressure, ale.maxFlux);
}

TEST(AlePisoGalilean, StaticFormulationOnTheTranslatingMeshIsNotGalileanInvariant) {
  const CavityComparison st = translatingCavity(false);
  ASSERT_TRUE(st.converged);
  EXPECT_GE(st.maxVelocity, 1e-4);
  std::printf("G6.4 translating cavity, static formulation: max |u_B - b - u_A| %.3e (>= 1e-4)\n",
              st.maxVelocity);
}

// --- G7.1: the piston-driven channel ---------------------------------------------------------

TEST(AlePisoMovingWall, PistonPushesUniformFlowOutOfTheChannel) {
  Mesh mesh = m7::p32();
  const Real vp = 0.5;
  const BoundaryConditionSet velocityBcs =
      makeSet(mesh, {{"right", [&] { return movingWall({-vp, 0.0, 0.0}); }}, {"left", outlet},
                     {"bottom", symmetry}, {"top", symmetry}});
  const BoundaryConditionSet pressureBcs = makeSet(
      mesh, {{"left", [] { return fixedPressure(0.0); }}, {"right", zeroGradient}, {"bottom", zeroGradient},
             {"top", zeroGradient}});
  MeshMotion motion(mesh, m7::affine(m7::ps2Spec()));
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  TransientState state = initialState(mesh, VectorField(mesh.numberOfCells(), Vector3{-vp, 0.0, 0.0}), 0.0,
                                      velocityBcs);
  Real maxU = 0.0, maxV = 0.0, maxP = 0.0, maxOutlet = 0.0, maxMass = 0.0, maxVolume = 0.0;
  const Real dt = 0.05;
  for (int n = 1; n <= m7::kSteps; ++n) {
    const TransientStepResult r = ale.solveTimeStep(state, dt);
    ASSERT_EQ(r.status, TransientStepStatus::Converged) << n;
    state = r.state;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      maxU = std::max(maxU, std::abs(state.velocity[c].x + vp) / vp);
      maxV = std::max(maxV, std::abs(state.velocity[c].y) / vp);
      maxP = std::max(maxP, std::abs(state.pressure[c]) / (kRho * vp * vp));
    }
    Real outletFlux = 0.0;
    for (const Index f : mesh.boundaryPatch("left").faceIds()) outletFlux += state.massFlux[f];
    maxOutlet = std::max(maxOutlet, std::abs(outletFlux - (kRho * vp * 1.0)) / (kRho * vp * 1.0));
    const auto& step = motion.lastStep();
    const auto conservation = cfd::pressure_velocity::evaluateAleConservation(mesh, step, state.massFlux, kRho);
    maxMass = std::max(maxMass, conservation.maxCellMassResidual / (kRho * vp / 16.0));
    const Real x = step.maxCoordinate;
    maxVolume = std::max(maxVolume, std::abs(step.totalVolume - ((2.0 - (vp * step.time)) * 1.0)) /
                                        (static_cast<Real>(mesh.numberOfCells()) * 16 * m7::kEps * x * x));
    EXPECT_LE(std::abs(conservation.globalMassResidual), 256 * 1e-9 * kRho * vp / 16.0) << n;
  }
  EXPECT_LE(maxU, 1e-9);
  EXPECT_LE(maxV, 1e-9);
  EXPECT_LE(maxP, 1e-8);
  EXPECT_LE(maxOutlet, 1e-9);
  EXPECT_LE(maxMass, 1e-9);
  EXPECT_LE(maxVolume, 1.0);
  std::printf("G7.1 piston: 20 steps, channel length now %.4f; max |u+Vp|/Vp %.3e, |v|/Vp %.3e (<= 1e-9), "
              "|p|/(rho Vp^2) %.3e (<= 1e-8), outlet flux error %.3e (<= 1e-9), R_P/F_ref %.3e, domain "
              "volume/bound %.3e\n",
              2.0 - (vp * motion.time()), maxU, maxV, maxP, maxOutlet, maxMass, maxVolume);
}

// --- G7.2: Couette flow in a translating mesh ----------------------------------------------

Real couetteDeviation(bool wrongWall) {
  Mesh mesh = m7::c16();
  const Vector3 meshVelocity{0.4, 0.0, 0.0};
  const BoundaryConditionSet velocityBcs = makeSet(
      mesh, {{"bottom", [&] { return wrongWall ? movingWall(meshVelocity) : wall(); }},
             {"top", [] { return movingWall({1.0, 0.0, 0.0}); }}, {"left", outlet}, {"right", outlet}});
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  MeshMotion motion(mesh, m7::affine(m7::tccSpec()));
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  VectorField u(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) u[cell.id()] = Vector3{cell.centroid().y, 0.0, 0.0};
  TransientState state = initialState(mesh, u, 0.0, velocityBcs);
  Real worst = 0.0;
  for (int n = 1; n <= m7::kSteps; ++n) {
    const TransientStepResult r = ale.solveTimeStep(state, 0.02);
    EXPECT_EQ(r.status, TransientStepStatus::Converged) << n;
    if (r.status != TransientStepStatus::Converged) return 1e300;
    state = r.state;
    for (const auto& cell : mesh.cells()) {
      worst = std::max(worst, std::abs(state.velocity[cell.id()].x - cell.centroid().y));
      worst = std::max(worst, std::abs(state.velocity[cell.id()].y));
    }
    if (!wrongWall) {
      const auto conservation =
          cfd::pressure_velocity::evaluateAleConservation(mesh, motion.lastStep(), state.massFlux, kRho);
      EXPECT_LE(conservation.maxCellMassResidual, 1e-9 * kRho * 1.0 / 16.0) << "G8.1 step " << n;
      EXPECT_LE(std::abs(conservation.globalMassResidual), 256 * 1e-9 * kRho / 16.0) << "G8.2 step " << n;
    }
  }
  return worst;
}

TEST(AlePisoMovingWall, CouetteFlowIsUnchangedByTangentialMeshTranslation) {
  const Real exact = couetteDeviation(false);
  EXPECT_LE(exact, 1e-9);
  const Real wrong = couetteDeviation(true);
  EXPECT_GE(wrong, 1e-3);
  std::printf("G7.2 Couette in a translating mesh: max |u - y_c|, |v| %.3e (<= 1e-9); with the bottom wall "
              "given the mesh velocity %.3e (>= 1e-3)\n",
              exact, wrong);
}

// --- G7.4: inconsistent wall motion is refused ----------------------------------------------

void expectRefusal(const std::string& name, Mesh mesh, const m7::Motion& prescribed,
                   const BoundaryConditionSet& velocityBcs, const std::string& patch) {
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  MeshMotion motion(mesh, prescribed);
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  const MeshGeometryState before = mesh.geometry();
  const TransientState state = initialState(mesh, VectorField(mesh.numberOfCells(), Vector3{}), 0.0, velocityBcs);
  try {
    (void)ale.solveTimeStep(state, 0.02);
    ADD_FAILURE() << name << ": expected InvalidArgumentError";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("patch '" + patch + "'"), std::string::npos) << message;
    std::printf("G7.4 %s refused: %s\n", name.c_str(), message.c_str());
  }
  const MeshGeometryState after = mesh.geometry();
  bool same = true;
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    same = same && sameBits(before.cellVolumes[c], after.cellVolumes[c]) &&
           sameBits(before.cellCentroids[c].x, after.cellCentroids[c].x) &&
           sameBits(before.cellCentroids[c].y, after.cellCentroids[c].y);
  }
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    same = same && sameBits(before.faceAreaVectors[f].x, after.faceAreaVectors[f].x) &&
           sameBits(before.faceAreaVectors[f].y, after.faceAreaVectors[f].y) &&
           sameBits(before.faceCentroids[f].x, after.faceCentroids[f].x) &&
           sameBits(before.faceCentroids[f].y, after.faceCentroids[f].y);
  }
  EXPECT_TRUE(same) << name << ": mesh not reverted";
  EXPECT_TRUE(sameBits(motion.time(), 0.0)) << name;
}

TEST(AlePisoMovingWall, InconsistentWallMotionIsRefusedAndTheMeshReverted) {
  {
    Mesh mesh = m7::c16();
    const BoundaryConditionSet walls = everywhere(mesh, wall);
    expectRefusal("(a) EX2 with stationary walls", m7::c16(), m7::affine(m7::ex2Spec()), walls, "bottom");
  }
  {
    Mesh mesh = m7::c16();
    const BoundaryConditionSet planes = everywhere(mesh, symmetry);
    expectRefusal("(b) EX2 with symmetry planes", m7::c16(), m7::affine(m7::ex2Spec()), planes, "bottom");
  }
  {
    Mesh mesh = m7::p32();
    const BoundaryConditionSet bcs =
        makeSet(mesh, {{"right", [] { return movingWall({-0.25, 0.0, 0.0}); }}, {"left", outlet},
                       {"bottom", symmetry}, {"top", symmetry}});
    expectRefusal("(c) piston with a mismatched wall velocity", m7::p32(), m7::affine(m7::ps2Spec()), bcs,
                  "right");
  }
}

// --- TransientSolver integration: a rejected step reverts the mesh ------------------------------

TEST(AlePisoTransient, TransientSolverRunsAleAndRevertsTheMeshOfARejectedStep) {
  const Vector3 u0{1.0, 0.5, 0.0};
  const auto run = [&](Real cflLimit, MeshGeometryState& geometry, Real& finalTime) {
    Mesh mesh = m7::c16();
    const BoundaryConditionSet velocityBcs = everywhere(mesh, [&] { return inlet(u0); });
    const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
    MeshMotion motion(mesh, m7::sn2());
    const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
    const cfd::solver::TransientSolver solver(ale, cflLimit);
    const auto result = solver.solve(initialState(mesh, VectorField(mesh.numberOfCells(), u0), 1.0, velocityBcs),
                                     cfd::solver::TimeController(0.0, 0.4, 0.02, 1000));
    geometry = mesh.geometry();
    finalTime = motion.time();
    return result;
  };
  MeshGeometryState completedGeometry;
  Real completedTime = 0.0;
  const auto completed = run(10.0, completedGeometry, completedTime);
  EXPECT_EQ(completed.status, cfd::solver::TransientStatus::Completed);
  EXPECT_EQ(completed.history.size(), 20U);
  // Replay the motion with the run's own time steps: the run's final mesh is it.
  Mesh replayMesh = m7::c16();
  MeshMotion replay(replayMesh, m7::sn2());
  for (const auto& record : completed.history) (void)replay.advance(replay.time() + record.deltaT);
  EXPECT_TRUE(sameBits(replay.time(), completedTime));
  const MeshGeometryState expected = replayMesh.geometry();
  for (Index c = 0; c < replayMesh.numberOfCells(); ++c) {
    EXPECT_TRUE(sameBits(expected.cellVolumes[c], completedGeometry.cellVolumes[c]));
  }
  // A CFL limit below the first step's Courant number: the step is rejected
  // by TransientSolver after AlePISO converged -- the mesh must be back at t0.
  MeshGeometryState rejectedGeometry;
  Real rejectedTime = 1.0;
  const auto rejected = run(1e-3, rejectedGeometry, rejectedTime);
  EXPECT_EQ(rejected.status, cfd::solver::TransientStatus::CFLViolation);
  EXPECT_TRUE(rejected.history.empty());
  EXPECT_TRUE(sameBits(rejectedTime, 0.0));
  const Mesh reference = m7::c16();
  for (Index c = 0; c < reference.numberOfCells(); ++c) {
    EXPECT_TRUE(sameBits(reference.cell(c).volume(), rejectedGeometry.cellVolumes[c]));
    EXPECT_TRUE(sameBits(reference.cell(c).centroid().x, rejectedGeometry.cellCentroids[c].x));
  }
  std::printf("AlePISO + TransientSolver: 20 steps Completed, final mesh = motion replay (bitwise); a "
              "CFL-rejected first step leaves the mesh at t0 (bitwise)\n");
}

TEST(AlePisoTransient, NumericalFailureRevertsTheMesh) {
  Mesh mesh = m7::c16();
  const Vector3 u0{1.0, 0.5, 0.0};
  const BoundaryConditionSet velocityBcs = everywhere(mesh, [&] { return inlet(u0); });
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  MeshMotion motion(mesh, m7::sn2());
  PISOSettings starved = gateSettings();
  starved.pressureSolver.maxIterations = 1;  // cannot converge a non-trivial system
  starved.pressureSolver.absoluteTolerance = 1e-300;
  starved.pressureSolver.relativeTolerance = 1e-300;
  starved.momentumSolver = starved.pressureSolver;
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, starved, 0);
  const MeshGeometryState before = mesh.geometry();
  // A non-uniform state so the linear systems are not solved at iteration 0.
  VectorField u(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) u[cell.id()] = Vector3{std::sin(cell.centroid().y), 0.0, 0.0};
  const TransientStepResult r = ale.solveTimeStep(initialState(mesh, u, 0.0, velocityBcs), 0.02);
  EXPECT_NE(r.status, TransientStepStatus::Converged);
  const MeshGeometryState after = mesh.geometry();
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    EXPECT_TRUE(sameBits(before.cellVolumes[c], after.cellVolumes[c]));
  }
  EXPECT_TRUE(sameBits(motion.time(), 0.0));
}

TEST(AlePisoTransient, RefusesA3DMeshLikePiso) {
  Mesh mesh = m7::h8();
  const BoundaryConditionSet velocityBcs = everywhere(mesh, [] { return inlet({1.0, 0.0, 0.0}); });
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  MeshMotion motion(mesh, m7::sn3());
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  const TransientState state =
      initialState(mesh, VectorField(mesh.numberOfCells(), Vector3{1.0, 0.0, 0.0}), 0.0, velocityBcs);
  EXPECT_THROW((void)ale.solveTimeStep(state, 0.02), InvalidArgumentError);
  EXPECT_TRUE(sameBits(motion.time(), 0.0));
}

// --- G8.3: a static step's ALE mass residual IS the continuity residual -------------------------

TEST(AlePisoConservation, StationaryStepMassResidualIsTheContinuityResidualBitForBit) {
  Mesh mesh = m7::c16();
  const BoundaryConditionSet velocityBcs = makeSet(
      mesh, {{"left", wall}, {"right", wall}, {"bottom", wall}, {"top", [] { return movingWall({1.0, 0.0, 0.0}); }}});
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  MeshMotion motion(mesh, m7::stationary());
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  TransientState state = initialState(mesh, VectorField(mesh.numberOfCells(), Vector3{}), 0.0, velocityBcs);
  for (int n = 1; n <= 5; ++n) {
    const TransientStepResult r = ale.solveTimeStep(state, 0.01);
    ASSERT_EQ(r.status, TransientStepStatus::Converged);
    state = r.state;
    const auto conservation =
        cfd::pressure_velocity::evaluateAleConservation(mesh, motion.lastStep(), state.massFlux, kRho);
    const auto continuity = cfd::physics::evaluateContinuity(mesh, state.massFlux);
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      EXPECT_TRUE(sameBits(conservation.cellMassResidual[c], continuity.cellImbalance[c])) << c;
    }
    EXPECT_TRUE(sameBits(conservation.globalMassResidual, continuity.globalNetFlux));
  }
  std::printf("G8.3 C16 cavity, stationary motion: R_P bitwise = continuity imbalance, 5 steps\n");
}

// --- VTK writes the moved geometry ------------------------------------------------------------

std::vector<Vector3> readVtkPoints(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::string token;
  std::vector<Vector3> points;
  while (in >> token) {
    if (token == "POINTS") {
      std::size_t count = 0;
      std::string type;
      in >> count >> type;
      points.resize(count);
      for (auto& p : points) in >> p.x >> p.y >> p.z;
      break;
    }
  }
  return points;
}

TEST(AlePisoOutput, VtkWritesTheCurrentMovedGeometry) {
  Mesh mesh = m7::q16();
  const Vector3 u0{1.0, 0.5, 0.0};
  const BoundaryConditionSet velocityBcs = everywhere(mesh, [&] { return inlet(u0); });
  const BoundaryConditionSet pressureBcs = everywhere(mesh, zeroGradient);
  MeshMotion motion(mesh, m7::sn2());
  const AlePISO ale(motion, kFluid, velocityBcs, pressureBcs, gateSettings(), 0);
  TransientState state = initialState(mesh, VectorField(mesh.numberOfCells(), u0), 1.0, velocityBcs);
  for (int n = 1; n <= 5; ++n) state = ale.solveTimeStep(state, 0.02).state;
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = state.velocity;
  result.pressure = state.pressure;
  result.massFlux = state.massFlux;
  const auto path = std::filesystem::temp_directory_path() / "m7_moved_q16.vtk";
  cfd::io::VTKWriter::writeSolution(path, mesh, result);
  const std::vector<Vector3> points = readVtkPoints(path);
  const auto& grid = mesh.structuredBlocks().front().vertices;
  ASSERT_EQ(points.size(), grid.size());
  Real movedMax = 0.0;
  for (std::size_t v = 0; v < grid.size(); ++v) {
    EXPECT_TRUE(sameBits(points[v].x, grid[v].x) && sameBits(points[v].y, grid[v].y)) << v;
    movedMax = std::max(movedMax, magnitude(points[v] - motion.referenceVertices()[v]));
  }
  EXPECT_GT(movedMax, 1e-3);  // the written points are the MOVED ones
  std::filesystem::remove(path);
  // 3D: writeCellFields of the moved H8.
  Mesh cube = m7::h8();
  MeshMotion motion3(cube, m7::sn3());
  for (int n = 1; n <= 5; ++n) (void)motion3.advance(n * 0.02);
  const auto path3 = std::filesystem::temp_directory_path() / "m7_moved_h8.vtk";
  cfd::io::VTKWriter::writeCellFields(path3, cube, {{"volume", [&] {
                                                       ScalarField v(cube.numberOfCells());
                                                       for (const auto& cell : cube.cells()) v[cell.id()] = cell.volume();
                                                       return v;
                                                     }()}});
  const std::vector<Vector3> points3 = readVtkPoints(path3);
  const auto& grid3 = cube.structuredBlocks().front().vertices;
  ASSERT_EQ(points3.size(), grid3.size());
  for (std::size_t v = 0; v < grid3.size(); ++v) {
    EXPECT_TRUE(sameBits(points3[v].x, grid3[v].x) && sameBits(points3[v].y, grid3[v].y) &&
                sameBits(points3[v].z, grid3[v].z))
        << v;
  }
  std::filesystem::remove(path3);
  std::printf("VTK: 2D (Q16, 5 AlePISO steps, max displacement %.4f) and 3D (H8, 5 advances) POINTS = the "
              "moved vertices, bitwise\n",
              movedMax);
}

}  // namespace
