// P16 BLOCKER -- the resident GPU BiCGSTAB fails on turbulence transport
// systems at production-relevant sizes while the CPU solver succeeds.
//
// Phase A (frozen reproducer) and Phase B (input identity) in one construction.
//
// HOW THE IDENTITY QUESTION IS SETTLED BY CONSTRUCTION
// ----------------------------------------------------
// Phase B asks whether CPU and GPU are solving DIFFERENT systems, or whether an
// identical system enters both and the GPU alone fails. The cheapest airtight
// answer is not to compare two independently assembled matrices -- it is to
// assemble ONCE on the device, then:
//
//     device CSR ──┬─→ solveScalarTransportResident   (GPU, adopts in place)
//                  └─→ download → host LinearSystem → cfd::algebra::BiCGSTAB
//
// Both solvers then receive the same bytes, so "they disagree about the
// system" is excluded by construction rather than by a comparison that could
// itself be wrong. The download exists ONLY for this diagnostic; nothing in
// the production path does it.
//
// The systems are built with the same public device primitives
// `correctKEpsilonDevice` uses, in the same order, so this is the production
// assembly and not a lookalike.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceKEpsilon.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"

using cfd::Index;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

constexpr Real kDensity = 1.225;
constexpr Real kViscosity = 1.8e-5;
constexpr Real kCMu = 0.09;
constexpr Real kSigmaK = 1.0;
constexpr Real kSigmaEpsilon = 1.3;
constexpr Real kC1 = 1.44;
constexpr Real kC2 = 1.92;
constexpr Real kFloor = 1e-10;
constexpr Real kEpsFloor = 1e-10;
constexpr Real kRelax = 0.7;

// One frozen turbulence transport system, device-resident, assembled exactly as
// correctKEpsilonDevice assembles it.
struct FrozenSystem {
  cfd::gpu::DeviceScalarTransportSystem system;
  cfd::gpu::DeviceBuffer<Real> guess;
  Index cells{};
  bool isEpsilon{false};
};

struct Harness {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  cfd::boundary::BoundaryConditionSet velocityBoundaries;
  cfd::boundary::BoundaryConditionSet kBoundaries;
  cfd::boundary::BoundaryConditionSet epsilonBoundaries;
  VectorField velocity;
  cfd::gpu::DeviceDiffusionPlan kDiffusion, epsilonDiffusion;
  cfd::gpu::DeviceConvectionPlan kConvection, epsilonConvection;
  cfd::gpu::DeviceMomentumConvectionPlan momentumPlan;

  explicit Harness(Index nx)
      : mesh(MeshGeometry::createCartesian2D(nx, 8, 1.0, 0.4)),
        fluid(kDensity, kViscosity),
        velocity(mesh.numberOfCells()) {
    velocityBoundaries.set(mesh, "left",
                           std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{1.5, 0.0}));
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
    for (Index i = 0; i < mesh.numberOfCells(); ++i) velocity[i] = {1.5, 0.0, 0.0};
  }

  [[nodiscard]] bool buildPlans() {
    return kDiffusion.build(mesh, kBoundaries) && kConvection.build(mesh, kBoundaries) &&
           epsilonDiffusion.build(mesh, epsilonBoundaries) &&
           epsilonConvection.build(mesh, epsilonBoundaries) &&
           momentumPlan.build(mesh, velocityBoundaries);
  }
  [[nodiscard]] Index cells() const { return mesh.numberOfCells(); }
};

// Assembles the k or epsilon system with the production device primitives, in
// the production order.
void freeze(Harness& h, bool epsilonEquation, FrozenSystem& out) {
  const Index n = h.cells();
  const ScalarField k(n, 0.05);
  const ScalarField epsilon(n, 0.8);
  const ScalarField muT = cfd::turbulence::computeTurbulentViscosityField(
      h.mesh, kDensity, kCMu, k, epsilon, kFloor, kEpsFloor);

  cfd::gpu::DeviceBuffer<Real> dK, dEps, dMuT, dFlux, dVolumes;
  dK.uploadFrom(k.data(), n);
  dEps.uploadFrom(epsilon.data(), n);
  dMuT.uploadFrom(muT.data(), n);
  const auto flux =
      cfd::physics::calculateMassFlux(h.mesh, h.velocity, h.fluid, h.velocityBoundaries);
  dFlux.uploadFrom(flux.data(), h.mesh.numberOfFaces());
  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = h.mesh.cell(i).volume();
  dVolumes.uploadFrom(volumes.data(), n);

  cfd::gpu::DeviceVelocity dVel;
  std::vector<Real> vx(static_cast<std::size_t>(n), 1.5), vy(static_cast<std::size_t>(n), 0.0),
      vz(static_cast<std::size_t>(n), 0.0);
  dVel.x.uploadFrom(vx.data(), n);
  dVel.y.uploadFrom(vy.data(), n);
  dVel.z.uploadFrom(vz.data(), n);

  cfd::gpu::DeviceVelocityGradient gradient;
  computeVelocityGradientDevice(h.momentumPlan, dVel, gradient);
  cfd::gpu::DeviceBuffer<Real> s2, production;
  cfd::gpu::computeStrainRateMagnitudeSquaredDevice(gradient, n, s2);
  cfd::gpu::computeTurbulentProductionDevice(dMuT, s2, production);

  cfd::gpu::DeviceBuffer<Real> gamma, Su, Sp;
  cfd::gpu::computeEffectiveDiffusivityDevice(kViscosity, dMuT,
                                              epsilonEquation ? kSigmaEpsilon : kSigmaK, gamma);
  if (epsilonEquation) {
    cfd::gpu::computeEpsilonSourceTermsDevice(kDensity, kC1, kC2, dK, dEps, production, kFloor, Su,
                                              Sp);
  } else {
    cfd::gpu::computeKSourceTermsDevice(kDensity, dK, dEps, production, kFloor, Su, Sp);
  }

  const cfd::gpu::DeviceTransportTimeState steady;
  const auto& diffusion = epsilonEquation ? h.epsilonDiffusion : h.kDiffusion;
  const auto& convection = epsilonEquation ? h.epsilonConvection : h.kConvection;
  const cfd::gpu::DeviceBuffer<Real>& phi = epsilonEquation ? dEps : dK;
  cfd::gpu::assembleScalarTransportDevice(diffusion, convection, dVolumes, phi, dFlux, gamma, Su,
                                          Sp, false, steady, out.system);
  cfd::gpu::applyImplicitUnderRelaxationDevice(phi, kRelax, out.system);

  out.cells = n;
  out.isEpsilon = epsilonEquation;
  out.guess.uploadFrom(epsilonEquation ? epsilon.data() : k.data(), n);
}

struct SolveReport {
  bool converged{false};
  Index iterations{};
  Index restarts{};
  Real initialResidual{};
  Real finalResidual{};
  cfd::algebra::SolverStatus status{cfd::algebra::SolverStatus::MaxIterations};
  std::string error;
};

const char* statusName(cfd::algebra::SolverStatus s) {
  switch (s) {
    case cfd::algebra::SolverStatus::Converged:
      return "Converged";
    case cfd::algebra::SolverStatus::MaxIterations:
      return "MaxIterations";
    case cfd::algebra::SolverStatus::Breakdown:
      return "Breakdown";
    case cfd::algebra::SolverStatus::NonFiniteInput:
      return "NonFiniteInput";
    case cfd::algebra::SolverStatus::NonFiniteResidual:
      return "NonFiniteResidual";
    case cfd::algebra::SolverStatus::InvalidSystem:
      return "InvalidSystem";
  }
  return "?";
}

// The GPU arm: the device CSR, adopted in place, never downloaded.
SolveReport solveOnGpu(const FrozenSystem& frozen,
                       const cfd::algebra::LinearSolverSettings& settings) {
  SolveReport report;
  try {
    cfd::gpu::GpuKrylovWorkspace workspace;
    cfd::gpu::DeviceBuffer<Real> solution;
    const auto result = cfd::gpu::solveScalarTransportResident(
        settings, frozen.system, frozen.cells, frozen.guess, workspace, solution);
    report.converged = result.converged();
    report.iterations = result.iterations;
    report.restarts = result.restarts;
    report.initialResidual = result.initialResidual;
    report.finalResidual = result.finalResidual;
    report.status = result.status;
  } catch (const std::exception& e) {
    report.error = e.what();
  }
  return report;
}

// The CPU arm: THE SAME BYTES, downloaded for this diagnostic only.
SolveReport solveOnCpu(const FrozenSystem& frozen,
                       const cfd::algebra::LinearSolverSettings& settings) {
  const Index n = frozen.cells;
  std::vector<Index> rowOffsets(static_cast<std::size_t>(n) + 1);
  frozen.system.rowOffsets.downloadTo(rowOffsets.data(), n + 1);
  std::vector<Index> columns(static_cast<std::size_t>(frozen.system.columnIndices.size()));
  frozen.system.columnIndices.downloadTo(columns.data(), frozen.system.columnIndices.size());
  std::vector<Real> values(static_cast<std::size_t>(frozen.system.values.size()));
  frozen.system.values.downloadTo(values.data(), frozen.system.values.size());
  std::vector<Real> rhsValues(static_cast<std::size_t>(n));
  frozen.system.rhs.downloadTo(rhsValues.data(), n);
  std::vector<Real> guessValues(static_cast<std::size_t>(n));
  frozen.guess.downloadTo(guessValues.data(), n);

  cfd::algebra::SparseMatrixBuilder builder(n, n);
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = rowOffsets[static_cast<std::size_t>(row)];
         d < rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      builder.add(row, columns[d], values[d]);
    }
  }
  cfd::algebra::Vector rhs(n, 0.0);
  cfd::algebra::Vector guess(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    rhs[i] = rhsValues[static_cast<std::size_t>(i)];
    guess[i] = guessValues[static_cast<std::size_t>(i)];
  }

  SolveReport report;
  try {
    const cfd::algebra::BiCGSTAB solver(settings);
    const auto result = solver.solve(cfd::algebra::LinearSystem(builder.build(), rhs), guess);
    report.converged = result.converged();
    report.iterations = result.iterations;
    report.restarts = result.restarts;
    report.initialResidual = result.initialResidual;
    report.finalResidual = result.finalResidual;
    report.status = result.status;
  } catch (const std::exception& e) {
    report.error = e.what();
  }
  return report;
}

// The ladder, run once and returned as rows so the qualification gate and its
// non-vacuity control can each read the same measurement.
struct Rung {
  Index nx{};
  Index cells{};
  bool epsilonEquation{false};
  SolveReport cpu;
  SolveReport gpu;
};

std::vector<Rung> runLadder(bool print) {
  cfd::algebra::LinearSolverSettings settings;  // production defaults
  std::vector<Rung> rungs;

  if (print) {
    std::printf("\n  Frozen ladder -- one device-assembled system, both solvers\n");
    std::printf("  %-5s %-6s %-4s  %-28s  %-28s\n", "nx", "cells", "eq", "CPU", "GPU");
  }
  for (const Index nx :
       {Index{16}, Index{24}, Index{28}, Index{32}, Index{40}, Index{48}, Index{64}}) {
    Harness h(nx);
    if (!h.buildPlans()) {
      ADD_FAILURE() << "plans unusable at nx=" << nx;
      return rungs;
    }

    for (const bool epsilonEquation : {false, true}) {
      FrozenSystem frozen;
      freeze(h, epsilonEquation, frozen);

      Rung rung;
      rung.nx = nx;
      rung.cells = frozen.cells;
      rung.epsilonEquation = epsilonEquation;
      rung.cpu = solveOnCpu(frozen, settings);
      rung.gpu = solveOnGpu(frozen, settings);

      if (print) {
        std::printf("  %-4lld %-5lld %-3s CPU %-14s it=%-3lld r0=%.3e rN=%.3e\n",
                    static_cast<long long>(nx), static_cast<long long>(frozen.cells),
                    epsilonEquation ? "eps" : "k", statusName(rung.cpu.status),
                    static_cast<long long>(rung.cpu.iterations), rung.cpu.initialResidual,
                    rung.cpu.finalResidual);
        std::printf("  %-4s %-5s %-3s GPU %-14s it=%-3lld r0=%.3e rN=%.3e restarts=%lld %s\n", "",
                    "", "", statusName(rung.gpu.status),
                    static_cast<long long>(rung.gpu.iterations), rung.gpu.initialResidual,
                    rung.gpu.finalResidual, static_cast<long long>(rung.gpu.restarts),
                    (rung.cpu.converged && !rung.gpu.converged) ? "<-- DIVERGENT" : "");
      }
      rungs.push_back(rung);
    }
  }
  return rungs;
}

// The entries the PRE-FIX ladder recorded as GPU Breakdown while the CPU
// converged, frozen from
// results/p16/turbulence/gpu_equivalence/krylov_blocker/findings.md before any
// solver change. `true` == the epsilon equation.
bool wasBrokenBeforeTheFix(Index cells, bool epsilonEquation) {
  if (cells == 224) return epsilonEquation;   // the FIRST failure
  if (cells == 256) return !epsilonEquation;  // 256-eps passed: not monotonic
  return cells == 320 || cells == 384 || cells == 512;
}

}  // namespace

// =============================================================================
// The qualification gate -- the same ladder, after the restart-parity fix.
// =============================================================================
//
// THIS TEST WAS THE BLOCKER REPRODUCER. Until the fix it asserted that a
// CPU-succeeds/GPU-fails case was still reproduced, and it failed the moment
// that stopped being true -- which is how the fix was confirmed rather than
// assumed. The pre-fix ladder it measured is preserved in
// results/p16/turbulence/gpu_equivalence/krylov_blocker/findings.md and is NOT
// re-derivable from this file any more, which is why that file is the record.
//
// Its assertion is now the opposite one: wherever the CPU converges, the GPU
// must converge too. Everything above it -- the single device-assembled system
// feeding both arms -- is unchanged, so input identity is still settled by
// construction and not by a comparison.

TEST(KrylovRestartParity, TheSameDeviceSystemSolvesOnBothSolvers) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const std::vector<Rung> rungs = runLadder(/*print=*/true);
  ASSERT_FALSE(rungs.empty());

  for (const Rung& rung : rungs) {
    const char* eq = rung.epsilonEquation ? "eps" : "k";
    ASSERT_TRUE(rung.cpu.error.empty()) << rung.cells << "/" << eq << ": " << rung.cpu.error;
    ASSERT_TRUE(rung.gpu.error.empty()) << rung.cells << "/" << eq << ": " << rung.gpu.error;

    // The CPU is the reference. If it ever stops converging on this ladder the
    // gate has lost its own baseline and must not be read as a GPU result.
    ASSERT_TRUE(rung.cpu.converged)
        << "the CPU reference failed at " << rung.cells << "/" << eq << " (status "
        << statusName(rung.cpu.status) << ") -- the gate no longer has a baseline";

    EXPECT_TRUE(rung.gpu.converged)
        << "GPU failed where the CPU converged at " << rung.cells << " cells / " << eq
        << ": status " << statusName(rung.gpu.status) << ", iterations " << rung.gpu.iterations
        << ", restarts " << rung.gpu.restarts << ", final residual " << rung.gpu.finalResidual;
  }
}

// =============================================================================
// Non-vacuity -- the ladder is green BECAUSE of the restart.
// =============================================================================
//
// A green ladder on its own proves nothing about the cause: it would look the
// same if some unrelated change had shifted these systems off the breakdown.
// So this control names the entries the pre-fix ladder recorded as GPU
// Breakdown and requires each of them to report at least one restart now. Those
// are precisely the runs that previously returned Breakdown at the first
// qualifying cancellation, so a restart count of zero on any of them would mean
// its recovery is NOT explained by this fix and must be investigated rather
// than accepted.
//
// It fails if the restart is removed, and it fails if the restart is present
// but never taken. Both are the failures it exists to catch.

TEST(KrylovRestartParity, EveryPreviouslyBrokenRungIsExplainedByARestart) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const std::vector<Rung> rungs = runLadder(/*print=*/false);
  ASSERT_FALSE(rungs.empty());

  Index checked = 0;
  for (const Rung& rung : rungs) {
    if (!wasBrokenBeforeTheFix(rung.cells, rung.epsilonEquation)) {
      // The other half of the control: a rung that converged before the fix
      // never reached a qualifying cancellation, so the restart must be inert
      // there. A restart appearing on one of these would mean the fix changed
      // behaviour it had no business changing.
      EXPECT_EQ(rung.gpu.restarts, Index{0})
          << rung.cells << " cells / " << (rung.epsilonEquation ? "eps" : "k")
          << " converged before the fix and now restarts -- the restart is not inert on systems "
             "that never broke down";
      continue;
    }
    ++checked;
    EXPECT_GE(rung.gpu.restarts, Index{1})
        << rung.cells << " cells / " << (rung.epsilonEquation ? "eps" : "k")
        << " broke down before the fix and now converges without restarting -- its recovery is "
           "not explained by the restart";
  }

  // The frozen list must still match the ladder the gate runs; if the ladder's
  // sizes are ever changed this control would silently check nothing.
  EXPECT_EQ(checked, Index{8}) << "the frozen pre-fix breakdown list no longer matches the ladder";
}
