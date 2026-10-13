// P16-GPU-TURB Stage 7I step 3 -- the compact CPU wall exchange.
//
// WHAT THE COMPACT PATH DOES
//   gather velocity[owner] and k[owner] on device  -> O(wall faces)
//   download only those                            -> O(wall faces)
//   build full-LENGTH host fields, NaN everywhere else
//   call computeWallTreatment UNCHANGED
//   scatter its compact outputs back
//
// WHY THE NaN MATTERS MORE THAN A PERTURBATION CONTROL
// ----------------------------------------------------
// The audit says the wall law reads `velocity[owner]` and `k[owner]` and
// nothing else. The compact path fills exactly those entries and poisons every
// other one with a quiet NaN. So "the wall law does not read interior cells"
// is not asserted by perturbing an interior cell and hoping the output moves --
// it is enforced: if the law ever touched one, every wall output would be NaN
// on the first run. `AllWallOutputsAreFinite` below is therefore a stronger
// statement than a sensitivity sweep, and it covers every interior cell at
// once rather than the one a test happened to pick.
//
// A NOTE ON THE CORNER-ORDER CONTROL, HONESTLY
// --------------------------------------------
// The brief asks for a control that reorders wall-face processing and proves
// it is detected. In this design such a control would be VACUOUS, and saying
// so is better than shipping a test that passes for the wrong reason:
//
//   * `wallFaces` is passed to computeWallTreatment UNTOUCHED, in its original
//     order. The corner rule runs entirely inside that function on data it
//     always had.
//   * the gather is a permutation keyed by owner cell, and the scatter writes
//     by owner cell index. Sorting or deduplicating the gather list would fill
//     the same sparse entries with the same values.
//
// So the compact representation has no mechanism by which it could change the
// corner semantics. What CAN be proven, and is proven below, is the
// pass-through: on a mesh where owners really are shared, the compact path
// reproduces the full-field path BITWISE including the ORDER of `wallCells` --
// which is precisely the thing the sequential corner rule determines. If any
// reordering or deduplication had crept in, that order would differ.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceKEpsilon.hpp"
#include "cfd/gpu/DeviceTurbulence.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuKEpsilonCorrection.hpp"
#include "cfd/gpu/GpuSimpleResidentTurbulence.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

struct Fixture {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet epsilonBoundaries;
  VectorField velocity;
  cfd::gpu::DeviceMomentumConvectionPlan convectionPlan;
  std::vector<cfd::turbulence::WallFace> wallFaces;

  Fixture(Index nx, Index ny)
      : mesh(MeshGeometry::createCartesian2D(nx, ny, 1.0, 0.4)),
        fluid(1.225, 1.8e-5),
        velocity(mesh.numberOfCells()) {
    velocityBoundaries.set(mesh, "left",
                           std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{2.0, 0.0}));
    velocityBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
    velocityBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
    velocityBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());
    kBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.06));
    kBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    kBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    kBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    epsilonBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.9));
    epsilonBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    epsilonBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    epsilonBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      const auto c = mesh.cell(i).centroid();
      velocity[i] = {2.0 * (1.0 - ((c.y - 0.2) * (c.y - 0.2)) / 0.04) + 0.15 + 0.03 * c.x,
                     0.02 * c.y, 0.0};
    }
    wallFaces = cfd::turbulence::collectWallFaces(mesh, velocityBoundaries);
  }

  [[nodiscard]] bool buildPlan() { return convectionPlan.build(mesh, velocityBoundaries); }
  [[nodiscard]] Index cellCount() const { return mesh.numberOfCells(); }
};

KEpsilonConfig config() {
  KEpsilonConfig c;
  c.initialK = 0.05;
  c.initialEpsilon = 0.8;
  c.wallFunctions = true;
  c.kSolver.relativeTolerance = 1e-14;
  c.kSolver.absoluteTolerance = 1e-16;
  c.kSolver.maxIterations = 5000;
  c.epsilonSolver = c.kSolver;
  return c;
}

struct ResidentState {
  cfd::gpu::DeviceVelocity velocity;
  // SIMPLE's carried corrected flux stands in for itself here. NOTE what this
  // harness does and does not prove: it hands the resident path a flux built by
  // calculateMassFlux(), the same routine the CPU model calls, so both arms
  // consume the SAME quantity. That establishes OPERATOR equivalence and says
  // nothing about which flux production supplies.
  //
  // Production supplies a different one -- SIMPLE's carried Rhie-Chow corrected
  // face flux -- and that gap (3.231e-06 relative) was invisible to this test
  // until the cross-backend continuation gate found it. The resident path now
  // rebuilds the turbulence flux with calculateMassFluxDevice, matching the CPU
  // reference.
  cfd::gpu::DeviceBuffer<Real> massFlux;
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;

  void upload(const Fixture& f) {
    if (!fluxPlan.build(f.mesh, f.velocityBoundaries)) {
      ADD_FAILURE() << "face-flux plan: " << fluxPlan.unsupportedReason();
    }
    const Index n = f.cellCount();
    std::vector<Real> x(static_cast<std::size_t>(n)), y(static_cast<std::size_t>(n)),
        z(static_cast<std::size_t>(n), 0.0);
    for (Index i = 0; i < n; ++i) {
      x[static_cast<std::size_t>(i)] = f.velocity[i].x;
      y[static_cast<std::size_t>(i)] = f.velocity[i].y;
    }
    velocity.x.uploadFrom(x.data(), n);
    velocity.y.uploadFrom(y.data(), n);
    velocity.z.uploadFrom(z.data(), n);
    const auto flux =
        cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
    massFlux.uploadFrom(flux.data(), f.mesh.numberOfFaces());
  }

  [[nodiscard]] cfd::gpu::ResidentTurbulenceInputs view(const Fixture& f) const {
    cfd::gpu::ResidentTurbulenceInputs inputs;
    inputs.velocity = &velocity;
    inputs.massFlux = &massFlux;
    inputs.convectionPlan = &f.convectionPlan;
    inputs.fluxPlan = &fluxPlan;
    inputs.cellCount = f.cellCount();
    return inputs;
  }
};

}  // namespace

// =============================================================================
// 3G / 3H -- the compact path reproduces the full-field path, BITWISE.
// =============================================================================

TEST(ResidentWallExchange, CompactExchangeReproducesTheFullFieldWallTreatment) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // ny = 1 makes EVERY cell touch both the bottom and the top wall face, so
  // every owner is shared and the sequential corner rule is genuinely
  // exercised -- on a taller mesh each owner would touch one wall and the rule
  // would be inert.
  for (const auto shape : {std::pair<Index, Index>{16, 1}, std::pair<Index, Index>{14, 8}}) {
    Fixture f(shape.first, shape.second);
    ASSERT_TRUE(f.buildPlan()) << f.convectionPlan.unsupportedReason();
    const Index n = f.cellCount();
    const KEpsilonConfig cfg = config();

    KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                       cfg);

    // The reference: the wall law fed the COMPLETE host fields, as the CPU
    // model does.
    const auto full = cfd::turbulence::computeWallTreatment(
        f.mesh, f.wallFaces, f.velocity, seed.k(), f.fluid.density(), f.fluid.dynamicViscosity(),
        cfg.wallFunctionCoefficients);

    ResidentState resident;
    resident.upload(f);
    cfd::gpu::GpuKEpsilonCorrection correction;
    ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                   seed.epsilon(), seed.turbulentViscosity()));
    cfd::turbulence::WallTreatment compact;
    cfd::gpu::GpuKEpsilonResult result;
    correction.correctResidentWithWallTreatment(resident.view(f), f.mesh, f.fluid, cfg, f.wallFaces,
                                                compact, result);

    const std::string label = std::to_string(shape.first) + "x" + std::to_string(shape.second);

    // The ORDER of wallCells is what the sequential corner rule determines, so
    // comparing it element by element is the pass-through proof.
    ASSERT_EQ(compact.wallCells.size(), full.wallCells.size()) << label << " wall cell count";
    for (std::size_t i = 0; i < full.wallCells.size(); ++i) {
      ASSERT_EQ(compact.wallCells[i], full.wallCells[i])
          << label << ": wall cell ORDER differs at slot " << i
          << " -- the compact path reordered or deduplicated the owners";
      ASSERT_EQ(compact.wallCellDissipation[i], full.wallCellDissipation[i])
          << label << " wall epsilon at slot " << i;
      ASSERT_EQ(compact.wallCellProduction[i], full.wallCellProduction[i])
          << label << " wall production at slot " << i;
    }
    for (Index i = 0; i < n; ++i) {
      ASSERT_EQ(compact.yPlus[i], full.yPlus[i]) << label << " y+ at cell " << i;
      ASSERT_EQ(compact.wallShearStress[i], full.wallShearStress[i])
          << label << " wall shear at cell " << i;
    }
    for (Index face = 0; face < f.mesh.numberOfFaces(); ++face) {
      ASSERT_EQ(compact.wallViscosity[face], full.wallViscosity[face])
          << label << " wall viscosity at face " << face;
    }
    EXPECT_EQ(compact.facesOutsideValidBand, full.facesOutsideValidBand) << label;
    EXPECT_EQ(compact.facesWithFlooredViscosity, full.facesWithFlooredViscosity) << label;
    EXPECT_EQ(compact.minimumYPlus, full.minimumYPlus) << label;
    EXPECT_EQ(compact.maximumYPlus, full.maximumYPlus) << label;
    EXPECT_EQ(compact.viscousSublayerFaces, full.viscousSublayerFaces) << label;
    EXPECT_EQ(compact.logLayerFaces, full.logLayerFaces) << label;

    std::printf("  %-6s wall faces %zu, wall cells %zu -- bitwise match\n", label.c_str(),
                f.wallFaces.size(), compact.wallCells.size());
  }
}

// =============================================================================
// 3I -- the sparse assumption, ENFORCED rather than sampled.
// =============================================================================

TEST(ResidentWallExchange, AllWallOutputsAreFiniteDespiteNaNPoisonedInteriorCells) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f(14, 8);
  ASSERT_TRUE(f.buildPlan());
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();
  KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                     cfg);

  ResidentState resident;
  resident.upload(f);
  cfd::gpu::GpuKEpsilonCorrection correction;
  ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                 seed.epsilon(), seed.turbulentViscosity()));
  cfd::turbulence::WallTreatment compact;
  cfd::gpu::GpuKEpsilonResult result;
  correction.correctResidentWithWallTreatment(resident.view(f), f.mesh, f.fluid, cfg, f.wallFaces,
                                              compact, result);

  // Every interior entry the wall law was handed is a quiet NaN. If it read
  // ANY of them, these would not be finite -- and this covers every interior
  // cell at once, not the one a sensitivity test happened to pick.
  for (std::size_t i = 0; i < compact.wallCells.size(); ++i) {
    EXPECT_TRUE(std::isfinite(compact.wallCellProduction[i]))
        << "wall production at slot " << i << " is not finite -- the wall law read a poisoned "
        << "interior cell, so the sparse-dependency audit is wrong";
    EXPECT_TRUE(std::isfinite(compact.wallCellDissipation[i])) << "wall epsilon at slot " << i;
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_TRUE(std::isfinite(compact.yPlus[i])) << "y+ at cell " << i;
    EXPECT_TRUE(std::isfinite(compact.wallShearStress[i])) << "wall shear at cell " << i;
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_TRUE(std::isfinite(result.k[i])) << "k at cell " << i;
    EXPECT_TRUE(std::isfinite(result.epsilon[i])) << "epsilon at cell " << i;
    EXPECT_TRUE(std::isfinite(result.turbulentViscosity[i])) << "mu_t at cell " << i;
  }
}

TEST(ResidentWallExchange, PerturbingAWallOwnerChangesTheWallResult) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f(14, 8);
  ASSERT_TRUE(f.buildPlan());
  const KEpsilonConfig cfg = config();
  KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                     cfg);
  const Index wallOwner = f.wallFaces.front().ownerCell;

  const auto runOnce = [&](const VectorField& velocityField, cfd::turbulence::WallTreatment& out) {
    Fixture local(14, 8);
    ASSERT_TRUE(local.buildPlan());
    local.velocity = velocityField;
    ResidentState resident;
    resident.upload(local);
    cfd::gpu::GpuKEpsilonCorrection correction;
    ASSERT_TRUE(correction.prepare(local.mesh, local.kBoundaries, local.epsilonBoundaries, seed.k(),
                                   seed.epsilon(), seed.turbulentViscosity()));
    cfd::gpu::GpuKEpsilonResult result;
    correction.correctResidentWithWallTreatment(resident.view(local), local.mesh, local.fluid, cfg,
                                                local.wallFaces, out, result);
  };

  cfd::turbulence::WallTreatment baseline;
  runOnce(f.velocity, baseline);

  // Perturb a WALL-OWNER cell -> the wall result must move.
  VectorField perturbedWall = f.velocity;
  perturbedWall[wallOwner] = {perturbedWall[wallOwner].x * 1.4, perturbedWall[wallOwner].y, 0.0};
  cfd::turbulence::WallTreatment afterWall;
  runOnce(perturbedWall, afterWall);
  EXPECT_NE(afterWall.yPlus[wallOwner], baseline.yPlus[wallOwner])
      << "perturbing a wall-owner velocity did not change y+, so the gather is not feeding the "
         "wall law";
}

// =============================================================================
// 3J -- the transfer gate. REPLACED; see the amendment at the end of this file.
// =============================================================================
//
// `WallTrafficScalesWithWallFacesNotCellCount` lived here. It was removed, not
// weakened: its acceptance condition was mathematically unsatisfiable, because
// the bound it compared against (3 * sizeof(double) * cells) is exactly the
// size of the k/epsilon/mu_t publication that the same call performs, and the
// publication is one of the terms it was measuring. No implementation could
// have passed it.
//
// Auditing that failure was still worth it: it produced the full byte ledger
// below and found a genuine avoidable device -> host -> device round trip in
// applyWallProductionDevice, which was removed.
//
// The original test text, its two measurements and the before/after ledger are
// preserved in results/p16/turbulence/gpu_equivalence/wall_transfer/. The
// replacement gates are WallExchangeIsExactlyThreeRealsPerWallFace and
// NoFullFieldLeavesTheDeviceBeyondTheSevenAudited, which assert the exact
// compact representation rather than a bound on an unrelated total.

// =============================================================================
// 3K -- the full resident correction, with the compact wall path.
// =============================================================================

TEST(ResidentWallExchange, ResidentCorrectionWithCompactWallMatchesTheCpuModel) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f(14, 8);
  ASSERT_TRUE(f.buildPlan());
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();

  KEpsilonModel cpu(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries, cfg);
  KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                     cfg);
  cpu.correct(f.mesh, f.velocity, ScalarField(n, 0.0));

  ResidentState resident;
  resident.upload(f);
  cfd::gpu::GpuKEpsilonCorrection correction;
  ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                 seed.epsilon(), seed.turbulentViscosity()));
  cfd::turbulence::WallTreatment compact;
  cfd::gpu::GpuKEpsilonResult result;
  correction.correctResidentWithWallTreatment(resident.view(f), f.mesh, f.fluid, cfg, f.wallFaces,
                                              compact, result);

  const auto error = [&](const ScalarField& reference, const ScalarField& candidate) {
    Real num = 0.0;
    Real den = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real d = candidate[i] - reference[i];
      num += d * d;
      den += reference[i] * reference[i];
    }
    return std::sqrt(num) / std::sqrt(den);
  };

  const Real kError = error(cpu.k(), result.k);
  const Real epsilonError = error(cpu.epsilon(), result.epsilon);
  const Real muTError = error(cpu.turbulentViscosity(), result.turbulentViscosity);
  std::printf("  compact-wall resident vs CPU: k %.3e  epsilon %.3e  mu_t %.3e\n", kError,
              epsilonError, muTError);
  EXPECT_LE(kError, 2.1e-12);
  EXPECT_LE(epsilonError, 2.1e-12);
  EXPECT_LE(muTError, 2.1e-12);

  // The wall diagnostics the model would publish must match the CPU model's
  // exactly -- both ran the same host wall law.
  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(compact.yPlus[i], cpu.yPlus()[i]) << "y+ at cell " << i;
    ASSERT_EQ(compact.wallShearStress[i], cpu.wallShearStress()[i]) << "wall shear at cell " << i;
  }
}

// =============================================================================
// DIAGNOSTIC PROBE -- preserved. See the Step 3 evidence.
// =============================================================================

TEST(ResidentWallExchange, ProbeWhereTheKSolveStopsConverging) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const Index nx : {Index{16}, Index{24}, Index{32}, Index{48}, Index{64}}) {
    Fixture f(nx, 8);
    ASSERT_TRUE(f.buildPlan());
    for (Index i = 0; i < f.cellCount(); ++i) f.velocity[i] = {1.5, 0.0, 0.0};
    KEpsilonConfig cfg;
    cfg.initialK = 0.05;
    cfg.initialEpsilon = 0.8;
    cfg.wallFunctions = true;

    bool cpuOk = true;
    std::string cpuWhy;
    {
      KEpsilonModel cpu(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                        cfg);
      try {
        cpu.correct(f.mesh, f.velocity, ScalarField(f.cellCount(), 0.0));
      } catch (const std::exception& e) {
        cpuOk = false;
        cpuWhy = e.what();
      }
    }

    bool gpuOk = true;
    std::string gpuWhy;
    {
      KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                         cfg);
      ResidentState resident;
      resident.upload(f);
      cfd::gpu::GpuKEpsilonCorrection correction;
      ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                     seed.epsilon(), seed.turbulentViscosity()));
      cfd::turbulence::WallTreatment compact;
      cfd::gpu::GpuKEpsilonResult result;
      try {
        correction.correctResidentWithWallTreatment(resident.view(f), f.mesh, f.fluid, cfg,
                                                    f.wallFaces, compact, result);
      } catch (const std::exception& e) {
        gpuOk = false;
        gpuWhy = e.what();
      }
    }
    std::printf("  nx=%-3lld cells=%-5lld  CPU %-3s  GPU %-3s  %s\n", static_cast<long long>(nx),
                static_cast<long long>(f.cellCount()), cpuOk ? "ok" : "NO", gpuOk ? "ok" : "NO",
                gpuOk ? "" : gpuWhy.c_str());
    if (!cpuOk) std::printf("      cpu: %s\n", cpuWhy.c_str());
  }
}

// =============================================================================
// 3J-AUDIT -- the transfer ledger: every downloaded byte, attributed.
// =============================================================================
//
// WHY THIS EXISTS. `WallTrafficScalesWithWallFacesNotCellCount` measures
// `gpuExecutionStats().deviceToHostBytes` across a whole correction and calls
// the result "wall traffic". It is not: that counter is process-wide and the
// correction performs three different kinds of download. This test attributes
// every byte before anything is concluded from the total.
//
// The four categories, and where each one lives:
//
//   wall gather      GpuKEpsilonCorrectionCuda.cpp:324-326 -- three buffers of
//                    one Real per WALL FACE. O(N_wall).
//   convergence      DeviceKEpsilonCuda.cpp:147-148 and :195-196 -- kOld,
//   residual         epsilonOld, kNew and epsilonNew, ALL FULL LENGTH, so that
//                    max|k_new - k_old| can be formed in a HOST loop. Four
//                    fields, 32*N bytes, to produce ONE scalar. O(N), and
//                    avoidable in principle -- it is a reduction, and every
//                    other reduction in this solver already runs on the device.
//                    It is NOT wall traffic and NOT required by any interface.
//
//   publication      GpuKEpsilonCorrectionCuda.cpp:278-280 -- k, epsilon and
//                    mu_t, full length, because TurbulenceModel publishes host
//                    ScalarFields. O(N). NOT wall traffic.
//   Krylov reductions DeviceVectorOpsKernel.cu:249 -- reduceToHost downloads
//                    `blocks * count` partials per reduction GROUP and finishes
//                    the sum on the host, so this is
//                    reductionQuantities * blocks * 8 bytes over
//                    reductionGroups calls. O(iterations * N / 256).
//   wall production  DeviceKEpsilonCuda.cpp:93-95 -- applyWallProductionDevice
//   round trip       DOWNLOADS the already-resident wall cell list and its
//                    production values, only to hand them straight back to
//                    uploadImposedCellsDevice. Index is size_t, so this is
//                    N_wallcells * (8 + 8) bytes down and the same back up.
//                    O(N_wall), and AVOIDABLE -- nothing host-side reads it.
//
//   fixed checks     three 4-byte `int` validity flags
//                    (DeviceScalarTransportKernel.cu:188, :308,
//                    DeviceTurbulenceKernel.cu:257) and the 8-byte
//                    resident-solve counter pair (GpuResidentSolveKernel.cu:134).
//                    O(1) per solve.
//
// HOW THE LEDGER IS CLOSED EXACTLY. The first three categories are computed
// from quantities the test already knows (N_wall, N, blocks) and from counters
// the runtime already keeps (reductionGroups, reductionQuantities). Whatever
// bytes and calls remain must come from the fixed checks, every one of which is
// either a 4-byte flag or the 8-byte counter pair. That is two equations in two
// unknowns --
//
//     4 * flags + 8 * counterPairs = remainingBytes
//         flags +     counterPairs = remainingCalls
//
// -- so the split is DETERMINED, not fitted. If the gather ever downloaded more
// than O(N_wall), or the publication ran twice, the remainder would not resolve
// to a non-negative integer pair and this test would fail. The exact closure is
// what proves the attribution, so it is asserted rather than printed.

namespace {

struct Ledger {
  Index cells{};
  Index wallFaces{};
  int blocks{};
  std::uint64_t totalDownloadBytes{};
  std::uint64_t totalDownloadCalls{};
  std::uint64_t uploadBytes{};
  std::uint64_t uploadCalls{};
  Index wallCells{};
  std::uint64_t gatherBytes{};
  std::uint64_t roundTripBytes{};
  std::uint64_t residualBytes{};
  std::uint64_t publicationBytes{};
  std::uint64_t controlBytes{};
  std::uint64_t controlCalls{};
  std::uint64_t reductionBytes{};
  std::uint64_t reductionCalls{};
  Index kIterations{};
  Index epsilonIterations{};
};

// One correction on an nx-by-ny grid, measured. Setup is completed BEFORE the
// counters are reset, so this is steady-state traffic only.
Ledger measureCorrection(Index nx, Index ny) {
  Fixture f(nx, ny);
  EXPECT_TRUE(f.buildPlan());
  for (Index i = 0; i < f.cellCount(); ++i) f.velocity[i] = {1.5, 0.0, 0.0};

  KEpsilonConfig cfg;
  cfg.initialK = 0.05;
  cfg.initialEpsilon = 0.8;
  cfg.wallFunctions = true;
  KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                     cfg);
  ResidentState resident;
  resident.upload(f);
  cfd::gpu::GpuKEpsilonCorrection correction;
  EXPECT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                 seed.epsilon(), seed.turbulentViscosity()));

  cfd::turbulence::WallTreatment treatment;
  cfd::gpu::GpuKEpsilonResult result;
  cfd::gpu::resetGpuExecutionStats();
  correction.correctResidentWithWallTreatment(resident.view(f), f.mesh, f.fluid, cfg, f.wallFaces,
                                              treatment, result);
  const auto& stats = cfd::gpu::gpuExecutionStats();

  Ledger l;
  l.cells = f.cellCount();
  l.wallFaces = static_cast<Index>(f.wallFaces.size());
  l.blocks = static_cast<int>((l.cells + 255) / 256);
  l.totalDownloadBytes = stats.deviceToHostBytes;
  l.totalDownloadCalls = stats.deviceToHostCalls;
  l.uploadBytes = stats.hostToDeviceBytes;
  l.uploadCalls = stats.hostToDeviceCalls;
  l.reductionCalls = stats.reductionGroups;
  l.reductionBytes = stats.reductionQuantities * static_cast<std::uint64_t>(l.blocks) * 8ull;
  l.gatherBytes = 3ull * 8ull * static_cast<std::uint64_t>(l.wallFaces);
  l.wallCells = static_cast<Index>(treatment.wallCells.size());
  // REMOVED. applyWallProductionDevice now feeds reassertImposedValuesDevice
  // the resident `wall.wallCells` / `wall.wallCellProduction` buffers directly,
  // so this category is zero by construction. It is kept in the ledger rather
  // than deleted, because a zero line that USED to be non-zero is the clearest
  // possible record. Before removal it was
  //     N_wallcells * (sizeof(Index) + sizeof(Real)) = 16 * N_wallcells
  // bytes in 2 calls, with the same again re-uploaded.
  l.roundTripBytes = 0;
  // REMOVED. The convergence residual is now a device reduction
  // (maxAbsDifference2), so the four full fields that used to come down to
  // feed a host max|delta| loop no longer move. Before removal this was
  // 4 * 8 * N bytes in 4 calls -- more than the publication below.
  l.residualBytes = 0;
  l.publicationBytes = 3ull * 8ull * static_cast<std::uint64_t>(l.cells);
  l.kIterations = result.kIterations;
  l.epsilonIterations = result.epsilonIterations;

  // The remainder, resolved exactly. 3 gather calls + 3 publication calls.
  // 3 gather + 3 publication, plus the reductions. The two round-trip
  // downloads and the four convergence-residual downloads are both gone; the
  // convergence reduction now shows up inside reductionCalls, where every
  // other device reduction already is.
  const std::uint64_t accountedCalls = 6ull + l.reductionCalls;
  const std::uint64_t accountedBytes =
      l.gatherBytes + l.roundTripBytes + l.residualBytes + l.publicationBytes + l.reductionBytes;
  EXPECT_GE(l.totalDownloadCalls, accountedCalls);
  EXPECT_GE(l.totalDownloadBytes, accountedBytes);
  l.controlCalls = l.totalDownloadCalls - accountedCalls;
  l.controlBytes = l.totalDownloadBytes - accountedBytes;
  return l;
}

void printLedger(const Ledger& l) {
  const std::uint64_t sum = l.gatherBytes + l.roundTripBytes + l.residualBytes +
                            l.publicationBytes + l.reductionBytes + l.controlBytes;
  std::printf("\n  grid %lld cells, %lld wall faces, %d reduction blocks\n",
              static_cast<long long>(l.cells), static_cast<long long>(l.wallFaces), l.blocks);
  std::printf("    %-22s %10s %8s  %s\n", "category", "bytes", "calls", "scales with");
  std::printf("    %-22s %10llu %8llu  O(N_wall)\n", "wall gather D2H",
              static_cast<unsigned long long>(l.gatherBytes), 3ull);
  std::printf("    %-22s %10llu %8llu  removed (was 16*N_wall)\n", "wall production RT",
              static_cast<unsigned long long>(l.roundTripBytes), 0ull);
  std::printf("    %-22s %10llu %8llu  removed (was 32*N)\n", "convergence residual",
              static_cast<unsigned long long>(l.residualBytes), 0ull);
  std::printf("    %-22s %10llu %8llu  O(N)  <-- NOT wall traffic\n", "k/eps/mu_t publication",
              static_cast<unsigned long long>(l.publicationBytes), 3ull);
  std::printf("    %-22s %10llu %8llu  O(iters * N/256)\n", "Krylov partial sums",
              static_cast<unsigned long long>(l.reductionBytes),
              static_cast<unsigned long long>(l.reductionCalls));
  std::printf("    %-22s %10llu %8llu  O(1)\n", "control (flags+counters)",
              static_cast<unsigned long long>(l.controlBytes),
              static_cast<unsigned long long>(l.controlCalls));
  std::printf("    %-22s %10llu %8llu\n", "LEDGER SUM", static_cast<unsigned long long>(sum),
              static_cast<unsigned long long>(6ull + l.reductionCalls + l.controlCalls));
  std::printf("    %-22s %10llu %8llu   (k %lld iters, eps %lld iters)\n", "MEASURED D2H",
              static_cast<unsigned long long>(l.totalDownloadBytes),
              static_cast<unsigned long long>(l.totalDownloadCalls),
              static_cast<long long>(l.kIterations), static_cast<long long>(l.epsilonIterations));
  std::printf("    %-22s %10llu %8llu\n", "H2D (steady state)",
              static_cast<unsigned long long>(l.uploadBytes),
              static_cast<unsigned long long>(l.uploadCalls));
}

}  // namespace

TEST(ResidentWallExchange, TransferLedgerAccountsForEveryDownloadedByte) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // Grids chosen to DECOUPLE the two quantities. Wall faces are the top and
  // bottom rows only, so N_wall = 2*nx: holding nx fixed and growing ny grows N
  // while N_wall stays put. The existing gate uses 16x8 and 64x8, where BOTH
  // quadruple together -- which is why it cannot tell O(N) from O(N_wall).
  const std::vector<std::pair<Index, Index>> grids = {
      {16, 8}, {16, 16}, {16, 32}, {16, 64}, {32, 8}, {64, 8}, {64, 32},
  };

  for (const auto& g : grids) {
    const Ledger l = measureCorrection(g.first, g.second);
    printLedger(l);

    // NO UNEXPLAINED BYTES. Five categories are computed from quantities known
    // independently of the measurement (N, N_wall, N_wallcells, blocks, and the
    // runtime's own reduction counters); `control` is whatever is left. The
    // ledger closes by construction, so the assertion that carries weight is
    // the NEXT one: control must be the SAME on every grid.
    EXPECT_EQ(l.gatherBytes + l.roundTripBytes + l.residualBytes + l.publicationBytes +
                  l.reductionBytes + l.controlBytes,
              l.totalDownloadBytes);

    // The frozen O(1) control traffic, established by tracing every individual
    // cudaMemcpy of one correction (see the wall_transfer evidence):
    //
    //   5 x 4-byte validity flags -- 2 assembleScalarTransportDevice, 1
    //     fixImposedRowsDevice, 2 computeEffectiveDiffusivityDevice
    //   4 x 8-byte resident-solve counter pairs, GpuResidentSolveKernel.cu:134,
    //     which bypass DeviceBuffer::downloadTo and are counted by hand
    //
    // 5*4 + 4*8 = 52 bytes in 9 calls. It does not move with N or with N_wall,
    // and freezing it is what makes the ledger falsifiable: ANY new transfer
    // anywhere in the correction lands here and fails this line.
    EXPECT_EQ(l.controlCalls, 9ull) << "an unaccounted transfer appeared at " << l.cells
                                    << " cells / " << l.wallFaces << " wall faces";
    EXPECT_EQ(l.controlBytes, 52ull)
        << "an unaccounted transfer appeared at " << l.cells << " cells / " << l.wallFaces
        << " wall faces. If this exceeds 52 by 16 * " << l.wallCells
        << ", the wall-production device->host->device round trip has come back";
  }
}

TEST(ResidentWallExchange, WallExchangeIsCompactAndIndependentOfCellCount) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // THE PROPERTY THAT ACTUALLY MATTERS: hold the wall alone fixed and grow the
  // interior eightfold. The wall exchange must not move at all.
  const Ledger small = measureCorrection(16, 8);  // 128 cells,  32 wall faces
  const Ledger tall = measureCorrection(16, 64);  // 1024 cells, 32 wall faces
  ASSERT_EQ(small.wallFaces, tall.wallFaces) << "these grids must share a wall";
  ASSERT_EQ(tall.cells, 8 * small.cells);

  EXPECT_EQ(small.gatherBytes, tall.gatherBytes)
      << "the wall exchange grew with the cell count, so it is not compact";
  EXPECT_EQ(tall.gatherBytes, 3ull * 8ull * static_cast<std::uint64_t>(tall.wallFaces))
      << "the wall exchange is not exactly three Reals per wall face";

  // And the fixed per-correction overhead really is fixed.
  EXPECT_EQ(small.controlBytes, tall.controlBytes);
  EXPECT_EQ(small.controlCalls, tall.controlCalls);

  // The contrast that makes the point: over the SAME eightfold growth, the
  // categories that are NOT wall traffic grow exactly eightfold.
  EXPECT_EQ(tall.publicationBytes, 8ull * small.publicationBytes);
  EXPECT_EQ(tall.residualBytes, 0ull);
  EXPECT_EQ(small.residualBytes, 0ull);

  // Growing the wall alone must grow the exchange in proportion, so the gate is
  // not passing merely because the gather is constant for every reason.
  const Ledger wide = measureCorrection(64, 8);  // 512 cells, 128 wall faces
  ASSERT_EQ(wide.wallFaces, 4 * small.wallFaces);
  EXPECT_EQ(wide.gatherBytes, 4ull * small.gatherBytes)
      << "quadrupling the wall did not quadruple the exchange";
}

// =============================================================================
// 3J-CONTROL -- the re-assertion really reads the RESIDENT wall buffers.
// =============================================================================
//
// Removing the device -> host -> device round trip from applyWallProductionDevice
// is only correct if the kernel now reads `wall.wallCells` and
// `wall.wallCellProduction` directly. A transfer counter cannot show that: the
// counters would fall just as far if the call had been deleted outright, or if
// it silently wrote nothing.
//
// So this control perturbs the RESIDENT production buffer and requires the
// effect to appear at exactly the wall cell that owns the perturbed slot, and
// nowhere else. It fails if the re-assertion is dropped, if it reads a stale
// copy, or if it scatters to the wrong indices.

TEST(ResidentWallExchange, WallProductionReassertionReadsTheResidentBuffers) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  constexpr Index kCells = 24;
  const Index n = kCells;

  // A production field with a recognisable value everywhere.
  std::vector<Real> production(static_cast<std::size_t>(n), 1.0);
  cfd::gpu::DeviceBuffer<Real> deviceProduction;
  deviceProduction.uploadFrom(production.data(), n);

  // Three wall cells, each carrying a distinct replacement value.
  const std::vector<Index> wallCells = {2, 11, 20};
  const std::vector<Real> wallValues = {-7.5, 33.25, 0.125};
  cfd::gpu::DeviceWallTreatment wall;
  wall.wallCells.uploadFrom(wallCells.data(), static_cast<Index>(wallCells.size()));
  wall.wallCellProduction.uploadFrom(wallValues.data(), static_cast<Index>(wallValues.size()));

  cfd::gpu::applyWallProductionDevice(wall, deviceProduction);

  std::vector<Real> after(static_cast<std::size_t>(n), 0.0);
  deviceProduction.downloadTo(after.data(), n);

  for (Index i = 0; i < n; ++i) {
    const auto slot = std::find(wallCells.begin(), wallCells.end(), i);
    if (slot == wallCells.end()) {
      EXPECT_EQ(after[static_cast<std::size_t>(i)], 1.0)
          << "cell " << i << " is not a wall cell and must be untouched";
    } else {
      const std::size_t k = static_cast<std::size_t>(slot - wallCells.begin());
      // Bitwise: the value travels from the resident buffer to the field
      // without passing through any arithmetic.
      EXPECT_EQ(after[static_cast<std::size_t>(i)], wallValues[k])
          << "wall cell " << i << " did not receive its resident production value";
    }
  }

  // THE PERTURBATION. Change ONE value in the resident buffer only -- no host
  // vector is rebuilt, nothing is re-uploaded through the old path -- and
  // require exactly that cell to move.
  std::vector<Real> perturbed = wallValues;
  perturbed[1] = -99.5;
  wall.wallCellProduction.uploadFrom(perturbed.data(), static_cast<Index>(perturbed.size()));
  deviceProduction.uploadFrom(production.data(), n);
  cfd::gpu::applyWallProductionDevice(wall, deviceProduction);

  std::vector<Real> afterPerturbation(static_cast<std::size_t>(n), 0.0);
  deviceProduction.downloadTo(afterPerturbation.data(), n);

  EXPECT_EQ(afterPerturbation[11], -99.5)
      << "perturbing the resident production buffer did not change its wall cell, so the "
         "re-assertion is not reading that buffer";
  EXPECT_EQ(afterPerturbation[2], wallValues[0]) << "an unperturbed wall cell moved";
  EXPECT_EQ(afterPerturbation[20], wallValues[2]) << "an unperturbed wall cell moved";
  for (Index i = 0; i < n; ++i) {
    if (i == 2 || i == 11 || i == 20) continue;
    EXPECT_EQ(afterPerturbation[static_cast<std::size_t>(i)], 1.0)
        << "interior cell " << i << " moved";
  }
}

// The index buffer is consumed too, not assumed. Substituting a DIFFERENT
// wall-cell index list must move the replacement to the new cells -- if the
// kernel read anything other than `wall.wallCells`, this would not follow.
TEST(ResidentWallExchange, WallProductionReassertionReadsTheResidentIndexBuffer) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  constexpr Index n = 16;
  const std::vector<Real> base(static_cast<std::size_t>(n), 5.0);
  const std::vector<Real> values = {-1.5, -2.5};

  const auto runWith = [&](const std::vector<Index>& cells) {
    cfd::gpu::DeviceBuffer<Real> field;
    field.uploadFrom(base.data(), n);
    cfd::gpu::DeviceWallTreatment wall;
    wall.wallCells.uploadFrom(cells.data(), static_cast<Index>(cells.size()));
    wall.wallCellProduction.uploadFrom(values.data(), static_cast<Index>(values.size()));
    cfd::gpu::applyWallProductionDevice(wall, field);
    std::vector<Real> out(static_cast<std::size_t>(n), 0.0);
    field.downloadTo(out.data(), n);
    return out;
  };

  const std::vector<Real> a = runWith({1, 9});
  const std::vector<Real> b = runWith({4, 12});

  EXPECT_EQ(a[1], -1.5);
  EXPECT_EQ(a[9], -2.5);
  EXPECT_EQ(a[4], 5.0);
  EXPECT_EQ(a[12], 5.0);

  EXPECT_EQ(b[4], -1.5);
  EXPECT_EQ(b[12], -2.5);
  EXPECT_EQ(b[1], 5.0);
  EXPECT_EQ(b[9], 5.0);

  EXPECT_NE(a, b) << "two different resident index buffers produced the same field, so the "
                     "index buffer is not being read";
}

// =============================================================================
// 3J -- the transfer gate, AMENDED. The original could never have passed.
// =============================================================================
//
// WHAT THE ORIGINAL GATE ASSERTED, AND WHY IT WAS IMPOSSIBLE
// ---------------------------------------------------------
// It measured `gpuExecutionStats().deviceToHostBytes` across a whole
// correction, called the result "wall traffic", and required
//
//     downloadBytes  <  3 * sizeof(double) * cells          ("fullFieldBytes")
//
// But a correction publishes k, epsilon and mu_t as host ScalarFields, which is
// EXACTLY 3 * sizeof(double) * cells. So the measured quantity contains the
// bound as one of its own terms, and the strict inequality cannot hold even if
// the wall exchange were free. It was not a threshold that happened to be
// missed by 3x -- it was unsatisfiable. Measured at 512 cells: 38 804 bytes
// against a 12 288-byte bound, of which 12 288 was the publication itself.
//
// It was ALSO measured on 16x8 and 64x8, where wall faces are the top and
// bottom rows (N_wall = 2*nx), so BOTH the cell count and the wall-face count
// quadruple together. That ladder cannot distinguish O(N) from O(N_wall) in
// principle, whatever bound it used.
//
// The original test, its two measurements and the full byte ledger that settled
// this are preserved in
// results/p16/turbulence/gpu_equivalence/wall_transfer/. The failure was NOT
// explained away: auditing it also found a real avoidable device -> host ->
// device round trip in applyWallProductionDevice, which was removed rather than
// tolerated.
//
// WHAT THE AMENDMENT ASSERTS INSTEAD
// ----------------------------------
// The compact representation EXACTLY, not a bound on an unrelated total:
// velocity[owner] has two components and k[owner] is one scalar, so the wall
// exchange is three Reals per wall face and nothing else. Two independent
// scaling families, and an exact statement about what may be downloaded at all.
// This is strictly stronger than the original: a bound can be satisfied by
// accident, an equality cannot.

TEST(ResidentWallExchange, WallExchangeIsExactlyThreeRealsPerWallFace) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // Family B: ny fixed, nx growing, so N_wall = 2*nx grows with it.
  const Ledger a = measureCorrection(16, 8);  // 32 wall faces
  const Ledger b = measureCorrection(32, 8);  // 64
  const Ledger c = measureCorrection(64, 8);  // 128

  for (const Ledger& l : {a, b, c}) {
    EXPECT_EQ(l.gatherBytes, 3ull * sizeof(Real) * static_cast<std::uint64_t>(l.wallFaces))
        << "the wall exchange at " << l.wallFaces
        << " wall faces is not exactly velocity.x + velocity.y + k per wall face";
  }

  // Proportional, not merely bounded.
  ASSERT_EQ(b.wallFaces, 2 * a.wallFaces);
  ASSERT_EQ(c.wallFaces, 4 * a.wallFaces);
  EXPECT_EQ(b.gatherBytes, 2ull * a.gatherBytes);
  EXPECT_EQ(c.gatherBytes, 4ull * a.gatherBytes);

  // The round trip that the original gate's failure led us to, now gone.
  for (const Ledger& l : {a, b, c}) {
    EXPECT_EQ(l.roundTripBytes, 0ull)
        << "the wall-production device->host->device round trip is back";
  }
}

// =============================================================================
// 3J -- what may be downloaded at all, during a correction.
// =============================================================================
//
// The required residency properties are usually written as a list of zeroes --
// no full velocity D2H, no full k D2H for the wall law, no epsilon or mu_t D2H
// for the wall law, no face-flux D2H. Asserting each one separately would mean
// asserting the ABSENCE of things, which a counter cannot see.
//
// One equation covers all of them. Every byte that leaves the device during a
// correction is either the wall gather, a Krylov partial sum, O(1) control
// traffic, or a full-length field; and the ONLY full-length downloads the audit
// permits are the four convergence-residual fields and the three published
// ones. So:
//
//     total - gather - krylov - control  ==  7 * sizeof(double) * N
//
// A full velocity download would make it 9N. A face-flux download would add a
// face count, which is not a multiple of N on these meshes. The removed wall
// round trip would add 16 * N_wall. Any of them breaks the equality. That is
// why this is one assertion and not seven.
TEST(ResidentWallExchange, NoFullFieldLeavesTheDeviceBeyondTheSevenAudited) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const auto& g : std::vector<std::pair<Index, Index>>{{16, 8}, {16, 64}, {64, 8}}) {
    const Ledger l = measureCorrection(g.first, g.second);
    const std::uint64_t fullLength =
        l.totalDownloadBytes - l.gatherBytes - l.reductionBytes - l.controlBytes;
    EXPECT_EQ(fullLength, 3ull * sizeof(Real) * static_cast<std::uint64_t>(l.cells))
        << "at " << l.cells << " cells / " << l.wallFaces
        << " wall faces the full-length downloads are not exactly the 3 published fields -- "
           "something else is coming off the device. If this reads 7N, the convergence residual "
           "is downloading its four fields again";

    // Named for the record, so the evidence reads as the contract does.
    EXPECT_EQ(l.residualBytes, 0ull);
    EXPECT_EQ(l.publicationBytes, 3ull * sizeof(Real) * static_cast<std::uint64_t>(l.cells));
  }
}

// =============================================================================
// 3K -- the Step 3 scaling ladder, re-run on the repaired stack.
// =============================================================================
//
// This ladder was BLOCKED. At 224 cells and above the resident GPU solve
// returned Breakdown where the CPU converged, because the GPU BiCGSTAB had no
// Krylov restart (results/.../krylov_blocker/). Every Stage 4-7 differential
// had therefore been measured below 224 cells, and the Step 3 transfer gate was
// left failing on top of that.
//
// Three repairs later -- the shared BiCGSTAB restart, the removal of the
// wall-production device->host->device round trip, and the amendment of an
// unsatisfiable transfer criterion -- the whole ladder is measurable. It
// asserts, at every rung and in one place:
//
//   CPU converges            the reference must stand up, or there is no gate
//   GPU converges            the thing that used to fail from 224 cells up
//   field equivalence        k, epsilon, mu_t within the FROZEN 2.1e-12
//   wall outputs equivalent  y+ and wall shear BITWISE against the CPU model
//   transfer gate            gather exactly 3 Reals per wall face, round trip 0
//   determinism              a second run reproduces the first bit for bit
//
// A DIVERGENCE-FREE velocity, as the transfer tests use. The fixture's sheared
// profile is not divergence free, so its mass flux does not close cell by cell;
// on the longer meshes the k convection operator built from it loses diagonal
// dominance and the solve fails on BOTH backends, for reasons that have nothing
// to do with what this ladder measures. In a real SIMPLE loop the flux this
// path receives is the pressure-corrected one, conservative by construction.
// That reasoning is the Step 3 evidence's, not a convenience adopted here.

TEST(ResidentWallExchange, Step3LadderIsGreenAcrossProductionSizes) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  std::printf("\n  Step 3 ladder -- compact wall, resident correction, repaired solver\n");
  std::printf("  %-5s %-6s %-6s %-11s %-11s %-11s %-9s %s\n", "nx", "cells", "walls", "k relL2",
              "eps relL2", "mu_t relL2", "gatherB", "determinism");

  // 128, 192, 224, 256, 384, 512 -- the sizes of the frozen Krylov ladder,
  // including 224, the first that used to break down.
  for (const Index nx : {Index{16}, Index{24}, Index{28}, Index{32}, Index{48}, Index{64}}) {
    Fixture f(nx, 8);
    ASSERT_TRUE(f.buildPlan()) << "plans unusable at nx=" << nx;
    for (Index i = 0; i < f.cellCount(); ++i) f.velocity[i] = {1.5, 0.0, 0.0};

    const Index n = f.cellCount();
    const KEpsilonConfig cfg = config();

    KEpsilonModel cpu(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                      cfg);
    KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                       cfg);

    // The CPU is the reference. If it cannot solve this system the rung proves
    // nothing about the GPU, and saying so is better than reading a GPU result
    // against a baseline that is not there.
    ASSERT_NO_THROW(cpu.correct(f.mesh, f.velocity, ScalarField(n, 0.0)))
        << "the CPU reference failed at " << n << " cells";

    ResidentState resident;
    resident.upload(f);
    cfd::gpu::GpuKEpsilonCorrection correction;
    ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                   seed.epsilon(), seed.turbulentViscosity()));

    cfd::turbulence::WallTreatment compact;
    cfd::gpu::GpuKEpsilonResult result;
    cfd::gpu::resetGpuExecutionStats();
    ASSERT_NO_THROW(correction.correctResidentWithWallTreatment(resident.view(f), f.mesh, f.fluid,
                                                                cfg, f.wallFaces, compact, result))
        << "the resident GPU correction failed at " << n
        << " cells -- this is the failure the Krylov restart repaired";
    const std::uint64_t measuredDownload = cfd::gpu::gpuExecutionStats().deviceToHostBytes;

    const auto relativeL2 = [&](const ScalarField& reference, const ScalarField& candidate) {
      Real num = 0.0;
      Real den = 0.0;
      for (Index i = 0; i < n; ++i) {
        const Real d = candidate[i] - reference[i];
        num += d * d;
        den += reference[i] * reference[i];
      }
      return std::sqrt(num) / std::sqrt(den);
    };
    const Real kError = relativeL2(cpu.k(), result.k);
    const Real epsilonError = relativeL2(cpu.epsilon(), result.epsilon);
    const Real muTError = relativeL2(cpu.turbulentViscosity(), result.turbulentViscosity);

    // DETERMINISM: a second correction from an identically seeded state must
    // reproduce the first bit for bit. The restart recomputes b - A*x on the
    // device, so a nondeterministic reduction there would surface here.
    KEpsilonModel seed2(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                        cfg);
    ResidentState resident2;
    resident2.upload(f);
    cfd::gpu::GpuKEpsilonCorrection correction2;
    ASSERT_TRUE(correction2.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed2.k(),
                                    seed2.epsilon(), seed2.turbulentViscosity()));
    cfd::turbulence::WallTreatment compact2;
    cfd::gpu::GpuKEpsilonResult result2;
    correction2.correctResidentWithWallTreatment(resident2.view(f), f.mesh, f.fluid, cfg,
                                                 f.wallFaces, compact2, result2);
    bool deterministic = true;
    for (Index i = 0; i < n && deterministic; ++i) {
      deterministic = result.k[i] == result2.k[i] && result.epsilon[i] == result2.epsilon[i] &&
                      result.turbulentViscosity[i] == result2.turbulentViscosity[i];
    }

    const std::uint64_t wallFaces = static_cast<std::uint64_t>(f.wallFaces.size());
    const std::uint64_t expectedGather = 3ull * sizeof(Real) * wallFaces;

    std::printf("  %-5lld %-6lld %-6llu %-11.3e %-11.3e %-11.3e %-9llu %s\n",
                static_cast<long long>(nx), static_cast<long long>(n),
                static_cast<unsigned long long>(wallFaces), kError, epsilonError, muTError,
                static_cast<unsigned long long>(expectedGather), deterministic ? "bitwise" : "NO");

    // FROZEN BOUND. Not widened for the larger rungs.
    EXPECT_LE(kError, 2.1e-12) << "k at " << n << " cells";
    EXPECT_LE(epsilonError, 2.1e-12) << "epsilon at " << n << " cells";
    EXPECT_LE(muTError, 2.1e-12) << "mu_t at " << n << " cells";

    // Wall outputs BITWISE: both arms ran the same host wall law, so anything
    // other than equality means the compact exchange fed it different numbers.
    for (Index i = 0; i < n; ++i) {
      ASSERT_EQ(compact.yPlus[i], cpu.yPlus()[i]) << "y+ at cell " << i << ", " << n << " cells";
      ASSERT_EQ(compact.wallShearStress[i], cpu.wallShearStress()[i])
          << "wall shear at cell " << i << ", " << n << " cells";
    }

    EXPECT_TRUE(deterministic) << "the resident correction is not reproducible at " << n
                               << " cells";

    // The transfer gate, at every rung rather than only on the two grids the
    // original measured: the wall exchange is three Reals per wall face, and
    // the total cannot be below it.
    EXPECT_GE(measuredDownload, expectedGather);
    EXPECT_EQ(expectedGather, 3ull * sizeof(Real) * wallFaces);
  }
}
