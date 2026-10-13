// P17.10 -- the density-based solver, CPU against GPU.
//
// Frozen gate: results/p17/cpu_gpu_equivalence/acceptance_gate.md
// (sha256 c5b86211dce2128152f5fe8d5a93b8ba739fc596da562f485af18d3ce6efbb51)
// Frozen policy: tolerance_policy.md, which carries NO fallback tolerance on purpose. The
// requirement is BITWISE at every level, and a failure is a defect to diagnose rather than a number
// to choose.
//
// WHY BITWISE IS DEMANDED RATHER THAN HOPED FOR, in five checkable preconditions:
//   1  one source for the mathematics -- HllcFlux.hpp, CFD_HLLC_HOST_DEVICE, compiled by both
//   2  the per-cell gather visits each cell's faces in ASCENDING order, which is the CPU's own
//      accumulation order (measured before any kernel existed: acceptance_gate.md §0 D1)
//   3  the geometry is UPLOADED, not recomputed -- DeviceMeshCuda.cpp stores face.area() itself
//   4  --fmad=false, the project convention since before P17.7, so nvcc does not contract where the
//      host build cannot
//   5  the only reduction in the loop is a MIN, which is order-independent in IEEE-754
// Every criterion below names which of those it would break, so a failure points somewhere.
//
// THE CONTROLS ARE FLAGS, NOT SOURCE EDITS. DeviceControls re-introduces one named trap each, so a
// control run and the production run are THE SAME BINARY. A control that required rebuilding could
// always be measuring a different build.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "EquivalenceCompare.hpp"
#include "ShockDetectors.hpp"
#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/DeviceDensityBasedSolver.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::compressible::BoundaryKind;
using cfd::compressible::ConservedField;
using cfd::compressible::DensityBasedBoundaries;
using cfd::compressible::DensityBasedBoundary;
using cfd::compressible::DensityBasedSettings;
using cfd::compressible::DensityBasedSolver;
using cfd::compressible::exteriorState;
using cfd::compressible::FluxVector;
using cfd::compressible::hllcFluxChecked;
using cfd::compressible::integrateOverArea;
using cfd::compressible::makePrimitiveState;
using cfd::compressible::PrimitiveState;
using cfd::compressible::ReconstructedFaceState;
using cfd::compressible::reconstructFirstOrder;
using cfd::compressible::reconstructFirstOrderBoundary;
using cfd::compressible::ResidualField;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::gpu::DeviceControls;
using cfd::gpu::DeviceDensityBasedSolver;
using cfd::mesh::AxisGrading;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::p17_10::compareConserved;
using cfd::p17_10::comparePrimitives;
using cfd::p17_10::compareResidual;
using cfd::p17_10::compareVector;
using cfd::p17_10::ComparisonReport;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

std::uint64_t bitsOf(Real v) {
  std::uint64_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

TotalEnergyState air() {
  const TemperatureDependentProperty cp(std::vector<Real>{1005.0}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(287.05, 1005.0), SensibleEnthalpy(cp, 0.0, 0.0));
}

DensityBasedSettings settings(Real cfl = 0.4) {
  DensityBasedSettings s;
  s.cfl = cfl;
  s.mode = cfd::compressible::TimeSteppingMode::GlobalPhysical;
  s.endTime = 1.0;  // never reached in these runs; the step count is what bounds them
  s.maxIterations = 10000;
  return s;
}

// Supersonic in at the low-x patch, out at the high-x patch, walls elsewhere.
//
// IT FAILS CLOSED ON A NAME IT DOES NOT KNOW, and that is not defensive decoration. The first
// version of this helper tested only for "left"/"right" and fell through to InviscidWall for
// anything else. createCartesian3D names its patches xmin/xmax/ymin/ymax/zmin/zmax, so the 3D case
// silently became a Mach-2 stream driven head-on into a solid wall -- which is what exposed the
// wave-speed finding in BLOCKING_FINDINGS.md. The bug was the SILENT DEFAULT, which is exactly the
// hazard trap 6 is about, reappearing in the test harness that was written to check for it.
DensityBasedBoundaries throughFlow(const Mesh& mesh, const PrimitiveState& inflow) {
  DensityBasedBoundaries b;
  for (const auto& patch : mesh.boundaryPatches()) {
    DensityBasedBoundary entry{};
    const std::string name(patch.name());
    if (name == "left" || name == "xmin") {
      entry.kind = BoundaryKind::SupersonicInflow;
      entry.prescribed = inflow;
    } else if (name == "right" || name == "xmax") {
      entry.kind = BoundaryKind::SupersonicOutflow;
    } else if (name == "top" || name == "bottom" || name == "ymin" || name == "ymax" ||
               name == "zmin" || name == "zmax") {
      entry.kind = BoundaryKind::InviscidWall;
    } else {
      throw cfd::InvalidArgumentError("throughFlow: unrecognised boundary patch \"" + name +
                                      "\". Refusing to guess a treatment -- a silent default here "
                                      "is how a test ends up measuring a case nobody chose.");
    }
    b[name] = entry;
  }
  return b;
}

// A NON-UNIFORM initial state. A uniform field would make the residual identically zero, which
// every implementation reproduces -- the classic vacuous comparison.
ConservedField perturbedState(const Mesh& mesh, const TotalEnergyState& thermo,
                              const DensityBasedSolver& solver) {
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField vel(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    const Real y = mesh.cell(i).centroid().y;
    rho[i] = 1.2 * (1.0 + 0.25 * std::sin(7.0 * x) * std::cos(3.0 * y));
    vel[i] = Vector3{700.0 + 40.0 * std::cos(5.0 * x), 12.0 * std::sin(4.0 * y), 0.0};
    t[i] = 300.0 * (1.0 + 0.08 * std::cos(6.0 * x + 1.0));
  }
  return solver.fromPrimitive(mesh, rho, vel, t);
}

// A Sod tube on a 2D mesh, the P17.9 configuration.
ConservedField sodState(const Mesh& mesh, const TotalEnergyState& thermo,
                        const DensityBasedSolver& solver) {
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField vel(n);
  ScalarField t(n);
  const Real R = thermo.thermodynamics().gasConstant();
  for (Index i = 0; i < n; ++i) {
    const bool left = mesh.cell(i).centroid().x < 0.5;
    rho[i] = left ? 1.0 : 0.125;
    vel[i] = Vector3{0.0, 0.0, 0.0};
    const Real p = left ? 1.0e5 : 1.0e4;
    t[i] = p / (rho[i] * R);
  }
  return solver.fromPrimitive(mesh, rho, vel, t);
}

// The whole fixture in one place: mesh, thermodynamics, boundaries, CPU solver and an uploaded
// device solver. Returns false when there is no GPU, so a test SKIPS rather than comparing nothing.
struct Fixture {
  Mesh mesh;
  TotalEnergyState thermo;
  DensityBasedSolver cpu;
  DensityBasedBoundaries boundaries;
  DeviceDensityBasedSolver gpu;
  ConservedField q;
  std::string message;

  Fixture(Mesh m, Real cfl) : mesh(std::move(m)), thermo(air()), cpu(settings(cfl), thermo) {}

  bool prepare(const ConservedField& initial) {
    q = initial;
    if (!gpu.upload(mesh, thermo, boundaries, cpu.settings().cfl, &message)) return false;
    if (!gpu.setState(q, &message)) return false;
    return true;
  }
};

// The CPU's per-face flux, assembled in the test from the SAME production functions the solver's
// own loop calls -- reconstructFirstOrder / exteriorState / hllcFluxChecked / integrateOverArea.
// The residual comparison (B2) is against production directly; this one exists so that when a
// residual differs the evidence can say whether the FLUX or the ACCUMULATION moved.
std::vector<FluxVector> cpuFaceFluxes(const Mesh& mesh,
                                      const std::vector<PrimitiveState>& primitives,
                                      const DensityBasedBoundaries& boundaries, Real gamma) {
  std::vector<FluxVector> out(mesh.numberOfFaces());
  std::vector<const DensityBasedBoundary*> byFace(mesh.numberOfFaces(), nullptr);
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto it = boundaries.find(std::string(patch.name()));
    for (const Index f : patch.faceIds()) byFace[f] = &it->second;
  }
  for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) {
    const auto& face = mesh.face(f);
    const Index owner = face.owner();
    const Vector3 a = face.areaVector();
    const Real area = face.area();
    const Vector3 normal{1.0 * a.x / area, 1.0 * a.y / area, 1.0 * a.z / area};
    ReconstructedFaceState state{};
    if (face.isBoundary()) {
      state = reconstructFirstOrderBoundary(primitives[owner],
                                            exteriorState(*byFace[f], primitives[owner], normal));
    } else {
      state = reconstructFirstOrder(primitives[owner], primitives[*face.neighbor()]);
    }
    out[f] = integrateOverArea(hllcFluxChecked(state.left, state.right, normal, gamma).flux, area);
  }
  return out;
}

#define REQUIRE_GPU()                                               \
  if (!cfd::gpu::densityBasedDeviceAvailable()) {                   \
    GTEST_SKIP() << "no CUDA device present; nothing was compared"; \
  }

}  // namespace

// =================================================================================================
// A5 / A7 -- the recovery and the flux, on DEVICE, against the CPU
// =================================================================================================

TEST(DensityBasedGpuEquivalence, A7_DeviceRecoveryMatchesProductionToPrimitiveBitwise) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(31, 7, 1.0, 0.25), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  const std::vector<PrimitiveState> host = f.cpu.toPrimitive(f.q);
  std::vector<PrimitiveState> device;
  std::vector<int> statuses;
  ASSERT_TRUE(f.gpu.recoverPrimitives(&device, &statuses, &f.message)) << f.message;

  for (const int s : statuses) ASSERT_EQ(s, 0) << "the device refused a state production accepted";
  const ComparisonReport r = comparePrimitives(host, true, device, true);
  EXPECT_TRUE(r.bitwiseEqual()) << r.reason();
  std::printf("  A7 recovery: %d values compared, %d differing\n",
              static_cast<int>(r.comparedValues()), static_cast<int>(r.differingValues()));
  EXPECT_GT(r.comparedValues(), 0);
}

TEST(DensityBasedGpuEquivalence, B1_FaceFluxesAreBitwise) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(31, 7, 1.0, 0.25), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(f.q);
  const std::vector<FluxVector> host = cpuFaceFluxes(f.mesh, primitives, f.boundaries,
                                                     f.thermo.thermodynamics().specificHeatRatio());
  std::vector<FluxVector> device;
  ASSERT_TRUE(f.gpu.faceFluxes(&device, &f.message)) << f.message;
  ASSERT_EQ(host.size(), device.size());

  Index differing = 0;
  for (std::size_t i = 0; i < host.size(); ++i) {
    if (bitsOf(host[i].mass) != bitsOf(device[i].mass)) ++differing;
    if (bitsOf(host[i].momentum.x) != bitsOf(device[i].momentum.x)) ++differing;
    if (bitsOf(host[i].momentum.y) != bitsOf(device[i].momentum.y)) ++differing;
    if (bitsOf(host[i].momentum.z) != bitsOf(device[i].momentum.z)) ++differing;
    if (bitsOf(host[i].energy) != bitsOf(device[i].energy)) ++differing;
  }
  std::printf("  B1 face flux: %zu faces x 5 components, %d differing\n", host.size(),
              static_cast<int>(differing));
  EXPECT_EQ(differing, 0);
  EXPECT_GT(host.size(), 0u);
}

// =================================================================================================
// B2 / B4 -- the residual, on a uniform mesh AND on a graded one
// =================================================================================================

TEST(DensityBasedGpuEquivalence, B2_B4_ResidualIsBitwiseOnUniformAndGradedMeshes) {
  REQUIRE_GPU();
  struct Case {
    const char* name;
    Mesh mesh;
  };
  std::vector<Case> cases;
  cases.push_back({"uniform_2d", MeshGeometry::createCartesian2D(31, 7, 1.0, 0.25)});
  // B4's mesh is graded on BOTH axes, so V_P varies cell to cell. P17.8's N2 control found that a
  // uniform mesh lets a dropped 1/V factor hide completely; a comparison run only there would be
  // measuring less than it appears to.
  cases.push_back(
      {"graded_2d",
       MeshGeometry::createGraded2D(
           17, 6, 1.5, 0.4, AxisGrading{GradingType::Geometric, 1.6, GradingCluster::Start},
           AxisGrading{GradingType::Geometric, 1.3, GradingCluster::Start})});
  cases.push_back({"cartesian_3d", MeshGeometry::createCartesian3D(9, 5, 4, 1.0, 0.5, 0.4)});

  for (auto& c : cases) {
    Fixture f(std::move(c.mesh), 0.4);
    f.boundaries =
        throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
    ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << c.name << ": " << f.message;

    const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(f.q);
    const ResidualField host = f.cpu.assembleResidual(f.mesh, primitives, f.boundaries);
    ResidualField device;
    ASSERT_TRUE(f.gpu.assembleResidual(&device, &f.message)) << c.name << ": " << f.message;

    const ComparisonReport r = compareResidual(host, true, device, true);
    EXPECT_TRUE(r.bitwiseEqual()) << c.name << ": " << r.reason();

    // The volume ratio, reported so "graded" is a measurement rather than a label.
    Real smallest = f.mesh.cell(0).volume();
    Real largest = smallest;
    for (Index i = 1; i < static_cast<Index>(f.mesh.numberOfCells()); ++i) {
      smallest = std::min(smallest, f.mesh.cell(i).volume());
      largest = std::max(largest, f.mesh.cell(i).volume());
    }
    std::printf("  B2 %-14s cells=%zu volumeRatio=%.3f compared=%d differing=%d\n", c.name,
                f.mesh.numberOfCells(), largest / smallest, static_cast<int>(r.comparedValues()),
                static_cast<int>(r.differingValues()));
  }
}

// =================================================================================================
// C1 / C2 -- the time-step control
// =================================================================================================

TEST(DensityBasedGpuEquivalence, C1_C2_LocalAndGlobalTimeStepsAreBitwise) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createGraded2D(
                17, 6, 1.5, 0.4, AxisGrading{GradingType::Geometric, 1.6, GradingCluster::Start},
                AxisGrading{GradingType::Geometric, 1.3, GradingCluster::Start}),
            0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(f.q);
  const std::vector<Real> host = f.cpu.localTimeSteps(f.mesh, primitives);
  std::vector<Real> device;
  ASSERT_TRUE(f.gpu.localTimeSteps(&device, &f.message)) << f.message;

  const ComparisonReport r = compareVector("dt", host, true, device, true);
  EXPECT_TRUE(r.bitwiseEqual()) << r.reason();

  const Real hostGlobal = f.cpu.globalTimeStep(f.mesh, primitives);
  Real deviceGlobal = 0.0;
  ASSERT_TRUE(f.gpu.globalTimeStep(&deviceGlobal, &f.message)) << f.message;
  EXPECT_EQ(bitsOf(hostGlobal), bitsOf(deviceGlobal))
      << "the global dt differs: cpu=" << hostGlobal << " gpu=" << deviceGlobal
      << ". min is order-independent in IEEE-754, so a difference here means the LOCAL dt differs, "
         "not the reduction.";
  std::printf("  C1/C2: %d local dt bitwise; global dt cpu=%.17e gpu=%.17e\n",
              static_cast<int>(r.comparedValues()), hostGlobal, deviceGlobal);
}

// =================================================================================================
// C3 / D1 / D2 -- one step, then a trajectory
// =================================================================================================

TEST(DensityBasedGpuEquivalence, C3_D1_D2_TrajectoryStaysBitwiseStepAfterStep) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(41, 5, 1.0, 0.125), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  ConservedField host = f.q;
  const Index totalSteps = 50;
  Index firstDivergence = 0;  // 0 means "never"

  for (Index s = 1; s <= totalSteps; ++s) {
    // The CPU step, assembled from the production pieces in the solver's own order: primitives ->
    // residual -> dt FROM THOSE SAME primitives -> update.
    const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(host);
    const ResidualField r = f.cpu.assembleResidual(f.mesh, primitives, f.boundaries);
    const Real dt = f.cpu.globalTimeStep(f.mesh, primitives);
    host = DensityBasedSolver::applyUpdate(
        f.mesh, host, r, std::vector<Real>(static_cast<std::size_t>(host.size()), dt));

    Real deviceDt = 0.0;
    bool admissible = false;
    Index offending = 0;
    int status = 0;
    ASSERT_TRUE(f.gpu.step(&deviceDt, &admissible, &offending, &status, &f.message))
        << "step " << s << ": " << f.message;
    ASSERT_TRUE(admissible) << "step " << s << ": the device rejected cell " << offending;
    EXPECT_EQ(bitsOf(dt), bitsOf(deviceDt)) << "dt differs at step " << s;

    if (s == 1 || s == 2 || s == 5 || s == 10 || s == 25 || s == totalSteps) {
      ConservedField device;
      ASSERT_TRUE(f.gpu.getState(&device, &f.message)) << f.message;
      const ComparisonReport cmp = compareConserved(host, true, device, true);
      if (!cmp.bitwiseEqual() && firstDivergence == 0) firstDivergence = s;
      EXPECT_TRUE(cmp.bitwiseEqual()) << "step " << s << ": " << cmp.reason();
      std::printf("  D1 step %3d: compared=%d differing=%d dt=%.17e\n", static_cast<int>(s),
                  static_cast<int>(cmp.comparedValues()), static_cast<int>(cmp.differingValues()),
                  dt);
    }
  }
  EXPECT_EQ(firstDivergence, 0u) << "CPU and GPU first diverged at step " << firstDivergence;
  std::printf("  D2: no divergence through %d steps\n", static_cast<int>(totalSteps));
}

// =================================================================================================
// D3 -- Sod, the benchmark P17.9 validated, run on both backends
// =================================================================================================

TEST(DensityBasedGpuEquivalence, D3_SodShockTubeIsBitwiseThroughTheWholeRun) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(200, 3, 1.0, 0.015), 0.4);
  for (const auto& patch : f.mesh.boundaryPatches()) {
    DensityBasedBoundary entry{};
    entry.kind = BoundaryKind::InviscidWall;  // the waves never reach the ends in this window
    f.boundaries[std::string(patch.name())] = entry;
  }
  ASSERT_TRUE(f.prepare(sodState(f.mesh, f.thermo, f.cpu))) << f.message;

  ConservedField host = f.q;
  // 500 steps, not 120. At 120 the whole wave structure still spans ~20 cells, so a comparison
  // there is largely the undisturbed initial state compared with itself.
  const Index steps = 500;
  for (Index s = 1; s <= steps; ++s) {
    const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(host);
    const ResidualField r = f.cpu.assembleResidual(f.mesh, primitives, f.boundaries);
    const Real dt = f.cpu.globalTimeStep(f.mesh, primitives);
    host = DensityBasedSolver::applyUpdate(
        f.mesh, host, r, std::vector<Real>(static_cast<std::size_t>(host.size()), dt));
    Real deviceDt = 0.0;
    bool admissible = false;
    Index offending = 0;
    int status = 0;
    ASSERT_TRUE(f.gpu.step(&deviceDt, &admissible, &offending, &status, &f.message)) << f.message;
    ASSERT_TRUE(admissible) << "step " << s << ": device rejected cell " << offending;
    ASSERT_EQ(bitsOf(dt), bitsOf(deviceDt)) << "dt differs at Sod step " << s;
  }

  ConservedField device;
  ASSERT_TRUE(f.gpu.getState(&device, &f.message)) << f.message;
  const ComparisonReport cmp = compareConserved(host, true, device, true);
  EXPECT_TRUE(cmp.bitwiseEqual()) << cmp.reason();

  // The wave structure must actually have SPREAD, or the comparison is two nearly-initial states.
  //
  // MEASURED ALONG ONE ROW, and that correction matters. Cell ids run j*nx+i, so a scan over all
  // cells compares the LAST cell of one row with the FIRST of the next -- which sits across the
  // initial discontinuity and reports 0.875 however little the solver did. The first version of
  // this check did exactly that and "passed" while the waves still spanned ~20 cells. The artefact
  // it was meant to rule out was the thing it was measuring.
  std::vector<Real> row;
  for (Index i = 0; i < static_cast<Index>(f.mesh.numberOfCells()); ++i) {
    if (std::abs(f.mesh.cell(i).centroid().y - 0.0075) <= 0.003) row.push_back(host.density[i]);
  }
  ASSERT_GT(row.size(), 100u);
  Real maxJump = 0.0;
  int intermediate = 0;  // cells strictly between the two initial plateaus
  for (std::size_t i = 0; i < row.size(); ++i) {
    if (i > 0) maxJump = std::max(maxJump, std::abs(row[i] - row[i - 1]));
    if (row[i] < 0.99 && row[i] > 0.13) ++intermediate;
  }
  EXPECT_LT(maxJump, 0.3) << "the initial discontinuity has not spread at all";
  EXPECT_GT(intermediate, 40)
      << "only " << intermediate
      << " cells hold an intermediate state, so the wave structure has barely developed and this "
         "comparison is mostly the initial condition compared with itself";
  std::printf(
      "  D3 Sod: %d steps, %d values bitwise, %d cells at an intermediate state, "
      "max centreline jump %.4f\n",
      static_cast<int>(steps), static_cast<int>(cmp.comparedValues()), intermediate, maxJump);
}

// =================================================================================================
// B5 -- the owner/neighbour cancellation, measured rather than assumed
// =================================================================================================
//
// The gather adds +F to the owner and -F to the neighbour from the SAME stored faceFlux[f], so the
// cancellation is exact by construction. "By construction" is an argument, and the gate asked for a
// measurement, so here it is: the device residual is reproduced on the host from the DOWNLOADED
// face fluxes using the same ascending order and sign rule, and must match bitwise. That isolates
// K2b -- if the sign convention or the ordering inside the kernel were wrong, B2 would fail too but
// could not say which half moved.
TEST(DensityBasedGpuEquivalence, B5_InteriorFaceContributionsCancelExactlyInTheGather) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createGraded2D(
                17, 6, 1.5, 0.4, AxisGrading{GradingType::Geometric, 1.6, GradingCluster::Start},
                AxisGrading{GradingType::Geometric, 1.3, GradingCluster::Start}),
            0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  std::vector<FluxVector> faceFlux;
  ASSERT_TRUE(f.gpu.faceFluxes(&faceFlux, &f.message)) << f.message;
  ResidualField device;
  ASSERT_TRUE(f.gpu.assembleResidual(&device, &f.message)) << f.message;

  const Index n = static_cast<Index>(f.mesh.numberOfCells());
  ResidualField rebuilt{ScalarField(n), VectorField(n), ScalarField(n)};
  Index interiorRefs = 0;
  for (Index c = 0; c < n; ++c) {
    Real rho = 0.0;
    Real mx = 0.0;
    Real my = 0.0;
    Real mz = 0.0;
    Real en = 0.0;
    for (const Index fid : f.mesh.cell(c).faceIds()) {
      const auto& face = f.mesh.face(fid);
      const Real sign = (face.owner() == c) ? 1.0 : -1.0;
      if (!face.isBoundary()) ++interiorRefs;
      rho += sign * faceFlux[fid].mass;
      mx += sign * faceFlux[fid].momentum.x;
      my += sign * faceFlux[fid].momentum.y;
      mz += sign * faceFlux[fid].momentum.z;
      en += sign * faceFlux[fid].energy;
    }
    rebuilt.density[c] = rho;
    rebuilt.momentum[c] = Vector3{mx, my, mz};
    rebuilt.totalEnergy[c] = en;
  }

  const ComparisonReport r = compareResidual(rebuilt, true, device, true);
  EXPECT_TRUE(r.bitwiseEqual()) << r.reason();
  // An all-boundary mesh would make "interior cancellation" vacuous.
  EXPECT_GT(interiorRefs, 0u) << "no interior face was visited twice, so nothing cancelled";
  std::printf(
      "  B5: %d values rebuilt from the device face fluxes, %d differing; %d interior "
      "cell-face references\n",
      static_cast<int>(r.comparedValues()), static_cast<int>(r.differingValues()),
      static_cast<int>(interiorRefs));
}

// =================================================================================================
// C4 / C5 -- the positivity DECISIONS, on a state that is actually inadmissible
// =================================================================================================
//
// The trajectory tests assert the device accepts every step, which proves the two agree on ACCEPT
// and says nothing about REJECT. A comparison that has only ever seen the happy path is the kind
// that passes until it matters. This drives deliberately inadmissible states through both and
// requires them to refuse together, naming the SAME first offending cell in ascending id order.
//
// C5's retry loop is host control logic with no device counterpart by design (audit.md §6). What is
// compared is the DECISION that would trigger a retry, which is the thing both backends have.
TEST(DensityBasedGpuEquivalence, C4_InadmissibleStatesAreRefusedByBothNamingTheSameCell) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(21, 4, 1.0, 0.2), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  const ConservedField good = perturbedState(f.mesh, f.thermo, f.cpu);
  ASSERT_TRUE(f.prepare(good)) << f.message;

  // The happy path first, so a later refusal is not simply "this fixture always refuses".
  {
    std::vector<int> statuses;
    ASSERT_TRUE(f.gpu.recoverPrimitives(nullptr, &statuses, &f.message)) << f.message;
    for (const int s : statuses) ASSERT_EQ(s, 0);
    ASSERT_NO_THROW((void)f.cpu.toPrimitive(good));
  }

  // TWO offending cells each time, so a device reduction that returned SOME offender rather than
  // the FIRST would compare equal on a one-offender case and diverge here. Cell 9 is the lower id
  // and is the one both must name.
  struct Case {
    const char* name;
    Index first;
    Index second;
    void (*wreck)(ConservedField&, Index);
  };
  const std::vector<Case> cases{
      {"kinetic_exceeds_total", 9, 30,
       [](ConservedField& q, Index i) { q.momentumDensity[i].x *= 40.0; }},
      {"non_positive_density", 9, 30, [](ConservedField& q, Index i) { q.density[i] = 0.0; }},
      {"non_finite_energy", 9, 30,
       [](ConservedField& q, Index i) {
         q.totalEnergyDensity[i] = std::numeric_limits<Real>::quiet_NaN();
       }},
  };

  for (const auto& c : cases) {
    ConservedField bad = good;
    c.wreck(bad, c.first);
    c.wreck(bad, c.second);

    Index cpuCell = 0;
    bool cpuRefused = false;
    try {
      (void)f.cpu.toPrimitive(bad);
    } catch (const cfd::Error& e) {
      cpuRefused = true;
      // The CPU names the cell in its message: "...: cell N is not a valid thermodynamic state".
      const std::string what(e.what());
      const std::size_t at = what.find("cell ");
      if (at != std::string::npos) {
        cpuCell = static_cast<Index>(std::stoul(what.substr(at + 5)));
      }
    }
    ASSERT_TRUE(cpuRefused) << c.name << ": the case is not actually inadmissible on the CPU";

    ASSERT_TRUE(f.gpu.setState(bad, &f.message)) << f.message;
    std::vector<int> statuses;
    ASSERT_TRUE(f.gpu.recoverPrimitives(nullptr, &statuses, &f.message)) << f.message;

    Index gpuFirst = static_cast<Index>(statuses.size());
    Index gpuCount = 0;
    for (Index i = 0; i < static_cast<Index>(statuses.size()); ++i) {
      if (statuses[i] != 0) {
        ++gpuCount;
        if (i < gpuFirst) gpuFirst = i;
      }
    }
    EXPECT_EQ(gpuCount, 2u) << c.name << ": expected exactly the two cells that were wrecked";
    EXPECT_EQ(gpuFirst, c.first) << c.name << ": the device did not name the FIRST offender";
    EXPECT_EQ(cpuCell, c.first) << c.name << ": the CPU did not name the first offender either";
    std::printf("  C4 %-22s cpu cell=%d  gpu first=%d of %d offenders  status=%d\n", c.name,
                static_cast<int>(cpuCell), static_cast<int>(gpuFirst), static_cast<int>(gpuCount),
                statuses[gpuFirst]);
  }
}

// =================================================================================================
// D4 -- P17.9's own detectors, run on BOTH backends' Sod solutions
// =================================================================================================
//
// D3 shows the two final states are bitwise identical, from which identical wave positions follow.
// That is an ARGUMENT; the frozen gate asked for a measurement, and running the detectors costs
// almost nothing. It also checks something the state comparison cannot: that the wave is actually
// THERE and detectable, so "identical" is not two identical absences.
TEST(DensityBasedGpuEquivalence, D4_DetectedWavePositionsAgreeOnBothBackends) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(200, 3, 1.0, 0.015), 0.4);
  for (const auto& patch : f.mesh.boundaryPatches()) {
    DensityBasedBoundary entry{};
    entry.kind = BoundaryKind::InviscidWall;
    f.boundaries[std::string(patch.name())] = entry;
  }
  ASSERT_TRUE(f.prepare(sodState(f.mesh, f.thermo, f.cpu))) << f.message;

  ConservedField host = f.q;
  for (Index s = 0; s < 500; ++s) {
    const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(host);
    const ResidualField r = f.cpu.assembleResidual(f.mesh, primitives, f.boundaries);
    const Real dt = f.cpu.globalTimeStep(f.mesh, primitives);
    host = DensityBasedSolver::applyUpdate(
        f.mesh, host, r, std::vector<Real>(static_cast<std::size_t>(host.size()), dt));
    Real deviceDt = 0.0;
    bool admissible = false;
    Index offending = 0;
    int status = 0;
    ASSERT_TRUE(f.gpu.step(&deviceDt, &admissible, &offending, &status, &f.message)) << f.message;
    ASSERT_TRUE(admissible);
  }
  ConservedField device;
  ASSERT_TRUE(f.gpu.getState(&device, &f.message)) << f.message;

  // One centreline row, the same sampling P17.9 uses.
  auto centreline = [&](const ConservedField& q, std::vector<double>* xs,
                        std::vector<double>* rho) {
    xs->clear();
    rho->clear();
    const Real yMid = 0.0075;
    for (Index i = 0; i < static_cast<Index>(f.mesh.numberOfCells()); ++i) {
      const auto& centre = f.mesh.cell(i).centroid();
      if (std::abs(centre.y - yMid) > 0.003) continue;
      xs->push_back(centre.x);
      rho->push_back(q.density[i]);
    }
  };
  std::vector<double> xs;
  std::vector<double> xs2;
  std::vector<double> rhoCpu;
  std::vector<double> rhoGpu;
  centreline(host, &xs, &rhoCpu);
  centreline(device, &xs2, &rhoGpu);
  ASSERT_EQ(xs.size(), xs2.size());
  ASSERT_GT(xs.size(), 100u) << "the centreline sample is too small to locate a wave";

  {  // diagnostic: what is actually in the profile, before asking a detector about it
    double lo = rhoCpu[0];
    double hi = rhoCpu[0];
    double maxAdjacent = 0.0;
    for (std::size_t i = 0; i < rhoCpu.size(); ++i) {
      lo = std::min(lo, rhoCpu[i]);
      hi = std::max(hi, rhoCpu[i]);
      if (i > 0) maxAdjacent = std::max(maxAdjacent, std::abs(rhoCpu[i] - rhoCpu[i - 1]));
    }
    std::printf("  D4 profile: n=%zu  rho in [%.5f, %.5f]  max adjacent jump %.5f\n", rhoCpu.size(),
                lo, hi, maxAdjacent);
    std::printf("  D4 sample:");
    for (std::size_t i = 0; i < rhoCpu.size(); i += 20) std::printf(" %.4f", rhoCpu[i]);
    std::printf("\n");
  }
  const auto shockCpu = cfd::shock_detect::crossingPosition(xs, rhoCpu, 0.265, 0.125);
  const auto shockGpu = cfd::shock_detect::crossingPosition(xs2, rhoGpu, 0.265, 0.125);
  const int widthCpu = cfd::shock_detect::transitionWidthCells(rhoCpu, 0.265, 0.125);
  const int widthGpu = cfd::shock_detect::transitionWidthCells(rhoGpu, 0.265, 0.125);

  // The wave must be FOUND, or "they agree" is two identical absences.
  ASSERT_TRUE(shockCpu.has_value()) << "no shock detected on the CPU solution";
  ASSERT_TRUE(shockGpu.has_value()) << "no shock detected on the GPU solution";
  EXPECT_EQ(bitsOf(*shockCpu), bitsOf(*shockGpu))
      << "the detected shock positions differ: cpu=" << *shockCpu << " gpu=" << *shockGpu;
  EXPECT_EQ(widthCpu, widthGpu);
  std::printf(
      "  D4: shock cpu=%.17g gpu=%.17g bitwise=%s, transition width cpu=%d gpu=%d, "
      "%zu centreline cells\n",
      *shockCpu, *shockGpu, bitsOf(*shockCpu) == bitsOf(*shockGpu) ? "YES" : "NO", widthCpu,
      widthGpu, xs.size());
}

// =================================================================================================
// R2 / R3 / R5 -- GPU determinism
// =================================================================================================

TEST(DensityBasedGpuEquivalence, R2_R5_GpuIsBitwiseRepeatableAndBlockSizeIndependent) {
  REQUIRE_GPU();
  const Mesh mesh = MeshGeometry::createCartesian2D(41, 5, 1.0, 0.125);
  const TotalEnergyState thermo = air();
  const DensityBasedSolver cpu(settings(), thermo);
  const DensityBasedBoundaries boundaries =
      throughFlow(mesh, makePrimitiveState(thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  const ConservedField initial = perturbedState(mesh, thermo, cpu);

  auto run = [&](int blockSize, ConservedField* out, std::vector<Real>* dts) {
    DeviceDensityBasedSolver gpu;
    std::string message;
    gpu.setBlockSize(blockSize);
    if (!gpu.upload(mesh, thermo, boundaries, cpu.settings().cfl, &message)) return message;
    if (!gpu.setState(initial, &message)) return message;
    Index taken = 0;
    if (!gpu.advance(30, dts, &taken, &message)) return message;
    if (!gpu.getState(out, &message)) return message;
    return std::string();
  };

  ConservedField a, b, c;
  std::vector<Real> dtA, dtB, dtC;
  ASSERT_EQ(run(128, &a, &dtA), std::string());
  ASSERT_EQ(run(128, &b, &dtB), std::string());
  ASSERT_EQ(run(64, &c, &dtC), std::string());  // R5: a different launch configuration

  const ComparisonReport repeat = compareConserved(a, true, b, true);
  EXPECT_TRUE(repeat.bitwiseEqual()) << "R2: the GPU is not repeatable: " << repeat.reason();

  const ComparisonReport blocks = compareConserved(a, true, c, true);
  EXPECT_TRUE(blocks.bitwiseEqual())
      << "R5: the answer depends on the block size, which means it is not deterministic: "
      << blocks.reason();

  ASSERT_EQ(dtA.size(), dtC.size());
  for (std::size_t i = 0; i < dtA.size(); ++i) {
    EXPECT_EQ(bitsOf(dtA[i]), bitsOf(dtB[i])) << "dt differs between repeats at step " << i;
    EXPECT_EQ(bitsOf(dtA[i]), bitsOf(dtC[i]))
        << "dt differs across block sizes at step " << i
        << " -- the min reduction tree changed the answer, which it must not";
  }
  std::printf("  R2/R5: %zu steps; repeat bitwise=%s; 64 vs 128 threads bitwise=%s\n", dtA.size(),
              repeat.bitwiseEqual() ? "YES" : "NO", blocks.bitwiseEqual() ? "YES" : "NO");
}

// =================================================================================================
// T1 / T2 -- transfer behaviour and residency
// =================================================================================================

TEST(DensityBasedGpuEquivalence, T2_TheUploadDownloadPairIsExactlyInverse) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(23, 4, 1.0, 0.2), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  const ConservedField initial = perturbedState(f.mesh, f.thermo, f.cpu);
  ASSERT_TRUE(f.prepare(initial)) << f.message;

  ConservedField back;
  ASSERT_TRUE(f.gpu.getState(&back, &f.message)) << f.message;
  const ComparisonReport r = compareConserved(initial, true, back, true);
  EXPECT_TRUE(r.bitwiseEqual()) << "the transfer pair is not inverse: " << r.reason();
  std::printf("  T2: %d values round-tripped unchanged\n", static_cast<int>(r.comparedValues()));
}

TEST(DensityBasedGpuEquivalence, T1_AResidentRunDoesNotRoundTripTheStatePerStep) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(23, 4, 1.0, 0.2), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  f.gpu.resetStats();
  const Index steps = 20;
  std::vector<Real> dts;
  Index taken = 0;
  ASSERT_TRUE(f.gpu.advance(steps, &dts, &taken, &f.message)) << f.message;
  ASSERT_EQ(taken, steps);

  const auto& s = f.gpu.stats();
  // The control that this counter is not simply stuck at zero: kernels DID launch, and downloads
  // DID happen (the dt reduction's partials and the validity reduction's). What must be zero is a
  // per-step round trip of the five conserved arrays.
  EXPECT_GT(s.kernelLaunches, steps) << "no kernels launched -- nothing ran";
  EXPECT_EQ(s.hostToDeviceTransfers, 0u)
      << "the resident loop uploaded something per step; only the mesh and the initial state "
         "should ever go up";
  const Index stateArrays = 5 * steps;
  EXPECT_LT(s.deviceToHostTransfers, stateArrays)
      << "the loop brought back at least as much as a full per-step state download, so it is not "
         "resident";
  std::printf(
      "  T1: %d steps, %d kernel launches, H2D=%d, D2H=%d (a per-step state round trip "
      "would be %d D2H), resident bytes=%zu\n",
      static_cast<int>(steps), static_cast<int>(s.kernelLaunches),
      static_cast<int>(s.hostToDeviceTransfers), static_cast<int>(s.deviceToHostTransfers),
      static_cast<int>(stateArrays), f.gpu.residentBytes());
}

// =================================================================================================
// Group V -- the negative controls. Each must break a NAMED criterion.
// =================================================================================================

namespace {

// Runs the residual on device under `controls` and reports how many values differ from the CPU's.
Index residualDifferencesUnder(const DeviceControls& controls, const char* label) {
  Mesh mesh = MeshGeometry::createCartesian2D(31, 7, 1.0, 0.25);
  Fixture f(std::move(mesh), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  if (!f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) {
    ADD_FAILURE() << label << ": " << f.message;
    return 0;
  }
  f.gpu.setControls(controls);

  const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(f.q);
  const ResidualField host = f.cpu.assembleResidual(f.mesh, primitives, f.boundaries);
  ResidualField device;
  if (!f.gpu.assembleResidual(&device, &f.message)) {
    ADD_FAILURE() << label << ": " << f.message;
    return 0;
  }
  const ComparisonReport r = compareResidual(host, true, device, true);
  std::printf("  %-28s %d of %d residual values differ\n", label,
              static_cast<int>(r.differingValues()), static_cast<int>(r.comparedValues()));
  return r.differingValues();
}

}  // namespace

TEST(DensityBasedGpuEquivalence, V_NegativeControls_EachBreaksItsNamedCriterion) {
  REQUIRE_GPU();

  // The positive case first: with every control off, the residual is bitwise. Without this line the
  // numbers below would prove only that the controls change SOMETHING.
  EXPECT_EQ(residualDifferencesUnder(DeviceControls{}, "production (all controls off)"), 0u);

  {  // V1 -- HLLC with the contact wave deleted, on device only
    DeviceControls c{};
    c.useHllInsteadOfHllc = true;
    EXPECT_GT(residualDifferencesUnder(c, "V1 HLL instead of HLLC"), 0u)
        << "a GPU path running a DIFFERENT SCHEME was not detected by B2";
  }
  {  // V3 -- the gather order reversed. Every term identical; only the order changes.
    DeviceControls c{};
    c.reverseGatherOrder = true;
    EXPECT_GT(residualDifferencesUnder(c, "V3 reversed gather order"), 0u)
        << "reversing the accumulation order changed nothing, so the ascending-order argument this "
           "whole phase rests on is not actually load-bearing and B2 is not measuring it";
  }
  {  // V4 -- trap 1: the neighbour re-evaluates with its own outward normal
    DeviceControls c{};
    c.neighbourUsesOwnNormal = true;
    EXPECT_GT(residualDifferencesUnder(c, "V4 neighbour's own normal"), 0u)
        << "re-evaluating the neighbour side was bitwise identical to negating the owner's flux, "
           "which would mean trap 1 is not a trap";
  }
}

TEST(DensityBasedGpuEquivalence, V6_CflSoundSpeedFromTheMeanTemperatureBreaksC1) {
  REQUIRE_GPU();
  Fixture f(MeshGeometry::createCartesian2D(31, 7, 1.0, 0.25), 0.4);
  f.boundaries =
      throughFlow(f.mesh, makePrimitiveState(f.thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  ASSERT_TRUE(f.prepare(perturbedState(f.mesh, f.thermo, f.cpu))) << f.message;

  const std::vector<PrimitiveState> primitives = f.cpu.toPrimitive(f.q);
  const std::vector<Real> host = f.cpu.localTimeSteps(f.mesh, primitives);

  DeviceControls c{};
  c.cflSoundFromMeanTemperature = true;
  f.gpu.setControls(c);
  std::vector<Real> device;
  ASSERT_TRUE(f.gpu.localTimeSteps(&device, &f.message)) << f.message;

  const ComparisonReport r = compareVector("dt", host, true, device, true);
  EXPECT_FALSE(r.bitwiseEqual())
      << "recomputing the face sound speed from the mean temperature made no difference, so C1 is "
         "not sensitive to trap 5";
  std::printf("  V6 sound from mean T: %d of %d dt values differ\n",
              static_cast<int>(r.differingValues()), static_cast<int>(r.comparedValues()));
}

// =================================================================================================
// TRAP 6 -- an unmapped patch must be refused by BOTH, not defaulted to a wall by either
// =================================================================================================

TEST(DensityBasedGpuEquivalence, Trap6_AnUnmappedBoundaryPatchIsRefusedByBothBackends) {
  REQUIRE_GPU();
  const Mesh mesh = MeshGeometry::createCartesian2D(9, 4, 1.0, 0.5);
  const TotalEnergyState thermo = air();
  const DensityBasedSolver cpu(settings(), thermo);

  // Deliberately incomplete: "left" is mapped, the rest are not.
  DensityBasedBoundaries incomplete;
  DensityBasedBoundary wall{};
  wall.kind = BoundaryKind::InviscidWall;
  incomplete["left"] = wall;

  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField vel(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.2;
    vel[i] = Vector3{700.0, 0.0, 0.0};
    t[i] = 300.0;
  }
  const ConservedField q = cpu.fromPrimitive(mesh, rho, vel, t);

  bool cpuRefused = false;
  try {
    (void)cpu.assembleResidual(mesh, cpu.toPrimitive(q), incomplete);
  } catch (const cfd::Error&) {
    cpuRefused = true;
  }
  EXPECT_TRUE(cpuRefused) << "production accepted an unmapped patch";

  DeviceDensityBasedSolver gpu;
  std::string message;
  const bool gpuAccepted = gpu.upload(mesh, thermo, incomplete, 0.4, &message);
  EXPECT_FALSE(gpuAccepted)
      << "the device upload accepted an unmapped boundary patch, which means it silently chose a "
         "treatment production refuses to guess";
  std::printf("  trap6: cpu refused=%s, device refused=%s (%s)\n", cpuRefused ? "YES" : "NO",
              gpuAccepted ? "NO" : "YES", message.c_str());

  // ...and the complete map is accepted, so the refusal is not simply "upload always fails".
  DensityBasedBoundaries complete;
  for (const auto& patch : mesh.boundaryPatches()) complete[std::string(patch.name())] = wall;
  DeviceDensityBasedSolver gpu2;
  EXPECT_TRUE(gpu2.upload(mesh, thermo, complete, 0.4, &message)) << message;
}

// =================================================================================================
// N1 / N2 non-vacuity -- the sanitizer controls
// =================================================================================================
//
// These two tests do nothing useful on their own: OUTSIDE compute-sanitizer they simply run and
// pass. Their purpose is to be present INSIDE a sanitizer run, where each must be REPORTED. A
// sanitizer that stays silent on a deliberate out-of-bounds read in this very library is not
// instrumenting this library, and its silence on the real kernels would then mean nothing at all.
//
// results/p17/cpu_gpu_equivalence/tools/sanitizers.sh runs the real kernels and these controls in
// SEPARATE invocations, so a clean run stays clean and the control run proves the tool works.

// OPT-IN, via CFD_P17_10_SANITIZER_CONTROL=1, which only tools/sanitizers.sh sets.
//
// Not squeamishness: an out-of-bounds DEVICE read can raise a sticky CUDA error that poisons the
// context for the whole PROCESS, and CFDGpuTests runs ~200 GPU tests in one process. Letting a
// deliberate fault run inside the regression would risk failing unrelated tests for a reason that
// has nothing to do with them. The controls therefore SKIP in the regression and run only in the
// dedicated sanitizer invocation, where a poisoned context is the expected outcome and the process
// ends immediately afterwards.
#define REQUIRE_SANITIZER_CONTROL_ENABLED()                                                \
  do {                                                                                     \
    const char* enabled = std::getenv("CFD_P17_10_SANITIZER_CONTROL");                     \
    if (enabled == nullptr || std::string(enabled) != "1") {                               \
      GTEST_SKIP() << "sanitizer control not enabled; set CFD_P17_10_SANITIZER_CONTROL=1 " \
                      "(tools/sanitizers.sh does)";                                        \
    }                                                                                      \
  } while (false)

TEST(DensityBasedGpuSanitizerControl, N1_DeliberateOutOfBoundsRead) {
  REQUIRE_GPU();
  REQUIRE_SANITIZER_CONTROL_ENABLED();
  std::string message;
  EXPECT_TRUE(cfd::gpu::sanitizerControlOutOfBoundsRead(&message)) << message;
  std::printf("  N1 control: launched a deliberate out-of-bounds read; memcheck must name it\n");
}

TEST(DensityBasedGpuSanitizerControl, N2_DeliberateSharedMemoryRace) {
  REQUIRE_GPU();
  REQUIRE_SANITIZER_CONTROL_ENABLED();
  std::string message;
  EXPECT_TRUE(cfd::gpu::sanitizerControlSharedMemoryRace(&message)) << message;
  std::printf("  N2 control: launched a deliberate shared-memory race; racecheck must name it\n");
}
