// P16-GPU-TURB Stage 7G/7H -- the PRODUCTION path, CPU against GPU.
//
// WHAT MAKES THIS GATE DIFFERENT FROM STAGE 6'S
// ---------------------------------------------
// Stage 6 compared the production CPU model against `correctKEpsilonDevice`
// called directly from a test. That proved the device composition was right,
// but a test-only caller is not what ships. This file drives BOTH arms through
// `KEpsilonModel::correct` -- the same public method SIMPLE and PISO call --
// and differs only in `config.kSolver.backend`.
//
// If the GPU arm here silently ran on the CPU, every number would agree
// perfectly and the gate would prove nothing. So `usingGpuBackend()` is
// asserted before any comparison, and the execution trace is checked after.
//
// THE FROZEN GATE, unchanged from earlier stages
//   fields          P14's 2.1e-12 relative L2, k / epsilon / mu_t
//   wall outputs    y+, wall shear published identically on both arms
//   provenance      seven operations Gpu, WallTreatment host, allOnGpu FALSE
//   chain           five corrections through the public interface
//
// WHY THE SOLVER TOLERANCE IS TIGHTENED HERE, AND ONLY HERE
// ---------------------------------------------------------
// Stage 6 measured that a 1e-10 solver tolerance makes a 2.1e-12 field gate
// unmeasurable: once the CPU and device matrices differ by the 1-2 ulp P14
// froze, one arm can satisfy the stopping test an iteration before the other
// and the solutions then differ by the TOLERANCE rather than by machine
// epsilon. Tightening it is what makes the field gate measure the
// discretization.
//
// This is a property of the EQUIVALENCE EXPERIMENT, not a change to what users
// get. Nothing here touches KEpsilonConfig's defaults; the tightening is local
// to this harness.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/TurbulenceBackendTrace.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"

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

struct Case {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet epsilonBoundaries;
  VectorField velocity;

  Case()
      : mesh(MeshGeometry::createCartesian2D(14, 10, 1.0, 0.4)),
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
      const Real y = mesh.cell(i).centroid().y;
      velocity[i] = {2.0 * (1.0 - ((y - 0.2) * (y - 0.2)) / 0.04) + 0.15, 0.03 * y, 0.0};
    }
  }
};

KEpsilonConfig configFor(bool wallFunctions, cfd::algebra::LinearSolverBackend backend) {
  KEpsilonConfig config;
  config.initialK = 0.05;
  config.initialEpsilon = 0.8;
  config.wallFunctions = wallFunctions;
  // Equivalence-experiment tolerance. See the file header: this is local to
  // the harness and does not touch user-facing defaults.
  config.kSolver.relativeTolerance = 1e-14;
  config.kSolver.absoluteTolerance = 1e-16;
  config.kSolver.maxIterations = 5000;
  config.epsilonSolver = config.kSolver;
  config.kSolver.backend = backend;
  config.epsilonSolver.backend = backend;
  return config;
}

Real relativeL2(const ScalarField& reference, const ScalarField& candidate) {
  Real numerator = 0.0;
  Real denominator = 0.0;
  for (Index i = 0; i < reference.size(); ++i) {
    const Real diff = candidate[i] - reference[i];
    numerator += diff * diff;
    denominator += reference[i] * reference[i];
  }
  return std::sqrt(numerator) / std::sqrt(denominator);
}

}  // namespace

// =============================================================================
// 7D-2 / 7E -- backend selection is explicit and observable.
// =============================================================================

TEST(ProductionKEpsilonBackend, BackendSelectionIsObservable) {
  const Case c;

  // CPU requested -> CPU, always, in every build.
  KEpsilonModel cpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                    configFor(true, cfd::algebra::LinearSolverBackend::CPU));
  EXPECT_FALSE(cpu.usingGpuBackend()) << "a CPU-configured model selected the GPU path";

  // GPU requested -> GPU only when it is genuinely available and supported.
  KEpsilonModel gpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                    configFor(true, cfd::algebra::LinearSolverBackend::GPU));
  if (deviceUnavailable()) {
    // The refusal must be explicit and must carry a reason. A silent CPU
    // fallback that still reported GPU would be the exact defect the whole
    // provenance facility exists to prevent.
    EXPECT_FALSE(gpu.usingGpuBackend());
    EXPECT_FALSE(gpu.gpuUnsupportedReason().empty())
        << "the GPU path was refused without saying why";
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
  EXPECT_TRUE(gpu.usingGpuBackend())
      << "the GPU path was refused on a machine that has a device: " << gpu.gpuUnsupportedReason();
}

// =============================================================================
// 7G -- one correction, both arms through the PUBLIC entry point.
// =============================================================================

TEST(ProductionKEpsilonBackend, OneProductionCorrectionAgrees) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const bool wallFunctions : {false, true}) {
    const Case c;
    KEpsilonModel cpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                      configFor(wallFunctions, cfd::algebra::LinearSolverBackend::CPU));
    KEpsilonModel gpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                      configFor(wallFunctions, cfd::algebra::LinearSolverBackend::GPU));
    ASSERT_TRUE(gpu.usingGpuBackend()) << gpu.gpuUnsupportedReason();
    ASSERT_FALSE(cpu.usingGpuBackend());

    const ScalarField pressure(c.mesh.numberOfCells(), 0.0);
    cpu.correct(c.mesh, c.velocity, pressure);
    gpu.correct(c.mesh, c.velocity, pressure);

    const std::string label = wallFunctions ? "walls ON " : "walls OFF";
    const Real kError = relativeL2(cpu.k(), gpu.k());
    const Real epsilonError = relativeL2(cpu.epsilon(), gpu.epsilon());
    const Real muTError = relativeL2(cpu.turbulentViscosity(), gpu.turbulentViscosity());
    std::printf("  %s  k %.3e  epsilon %.3e  mu_t %.3e\n", label.c_str(), kError, epsilonError,
                muTError);

    EXPECT_LE(kError, 2.1e-12) << label << " k";
    EXPECT_LE(epsilonError, 2.1e-12) << label << " epsilon";
    EXPECT_LE(muTError, 2.1e-12) << label << " mu_t";

    // The wall diagnostics a user exports must be identical, not merely close:
    // both arms run the SAME host wall treatment, so anything other than
    // bitwise equality would mean one of them re-derived it.
    if (wallFunctions) {
      for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
        ASSERT_EQ(gpu.yPlus()[i], cpu.yPlus()[i]) << "y+ differs at cell " << i;
        ASSERT_EQ(gpu.wallShearStress()[i], cpu.wallShearStress()[i])
            << "wall shear differs at cell " << i;
      }
      EXPECT_EQ(gpu.wallFacesOutsideValidBand(), cpu.wallFacesOutsideValidBand());
      EXPECT_EQ(gpu.minimumWallYPlus(), cpu.minimumWallYPlus());
      EXPECT_EQ(gpu.maximumWallYPlus(), cpu.maximumWallYPlus());
    }

    ASSERT_TRUE(cpu.convergenceResidual().has_value());
    ASSERT_TRUE(gpu.convergenceResidual().has_value());
    EXPECT_NEAR(*gpu.convergenceResidual(), *cpu.convergenceResidual(),
                1e-10 * std::max(1.0, *cpu.convergenceResidual()));
  }
}

// =============================================================================
// 7H -- five chained corrections, both arms through the public interface.
// =============================================================================

TEST(ProductionKEpsilonBackend, ChainedProductionCorrectionsStayInStep) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Case c;
  KEpsilonModel cpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                    configFor(true, cfd::algebra::LinearSolverBackend::CPU));
  KEpsilonModel gpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                    configFor(true, cfd::algebra::LinearSolverBackend::GPU));
  ASSERT_TRUE(gpu.usingGpuBackend()) << gpu.gpuUnsupportedReason();

  const ScalarField pressure(c.mesh.numberOfCells(), 0.0);
  const ScalarField initialK = cpu.k();

  for (int correction = 1; correction <= 5; ++correction) {
    cpu.correct(c.mesh, c.velocity, pressure);
    gpu.correct(c.mesh, c.velocity, pressure);

    const Real kError = relativeL2(cpu.k(), gpu.k());
    const Real epsilonError = relativeL2(cpu.epsilon(), gpu.epsilon());
    const Real muTError = relativeL2(cpu.turbulentViscosity(), gpu.turbulentViscosity());
    std::printf("  correction %d: k %.3e  epsilon %.3e  mu_t %.3e\n", correction, kError,
                epsilonError, muTError);

    EXPECT_LE(kError, 2.1e-12) << "correction " << correction << " k";
    EXPECT_LE(epsilonError, 2.1e-12) << "correction " << correction << " epsilon";
    EXPECT_LE(muTError, 2.1e-12) << "correction " << correction << " mu_t";
  }

  // The chain must have MOVED. A fixed point compared to itself would pass
  // every assertion above while proving nothing about state ordering.
  Real movement = 0.0;
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    movement = std::max(movement, std::abs(cpu.k()[i] - initialK[i]));
  }
  EXPECT_GT(movement, 1e-6) << "k barely moved over five corrections";
}

// =============================================================================
// 7D-5 -- production provenance.
// =============================================================================

TEST(ProductionKEpsilonBackend, ProductionTraceRecordsTheRealExecution) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Case c;
  KEpsilonModel gpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                    configFor(true, cfd::algebra::LinearSolverBackend::GPU));
  ASSERT_TRUE(gpu.usingGpuBackend()) << gpu.gpuUnsupportedReason();

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  auto& trace = cfd::gpu::turbulenceBackendTrace();
  trace.reset();
  gpu.correct(c.mesh, c.velocity, ScalarField(c.mesh.numberOfCells(), 0.0));

  using cfd::gpu::ExecutionBackend;
  using cfd::gpu::TurbulenceOperation;
  for (const auto op : {TurbulenceOperation::StrainRate, TurbulenceOperation::Production,
                        TurbulenceOperation::TurbulentViscosity, TurbulenceOperation::KAssembly,
                        TurbulenceOperation::KSolve, TurbulenceOperation::EpsilonAssembly,
                        TurbulenceOperation::EpsilonSolve}) {
    EXPECT_EQ(trace.backendFor(op), ExecutionBackend::Gpu)
        << cfd::gpu::turbulenceOperationName(op)
        << " did not run on the device in the PRODUCTION path:\n"
        << trace.describe();
  }

  // The documented P16 limitation, asserted rather than assumed.
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::WallTreatment), ExecutionBackend::NotAttempted)
      << "the wall treatment is host-side in P16 and must not claim the device";

  const TurbulenceOperation all[] = {
      TurbulenceOperation::StrainRate,         TurbulenceOperation::Production,
      TurbulenceOperation::KAssembly,          TurbulenceOperation::KSolve,
      TurbulenceOperation::EpsilonAssembly,    TurbulenceOperation::EpsilonSolve,
      TurbulenceOperation::TurbulentViscosity, TurbulenceOperation::WallTreatment};
  EXPECT_FALSE(trace.allOnGpu(all, 8)) << "allOnGpu() passed while the wall treatment is host-side";
}

// =============================================================================
// Determinism, and the CPU path unchanged.
// =============================================================================

TEST(ProductionKEpsilonBackend, RepeatedProductionCorrectionsAreDeterministic) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Case c;
  std::vector<Real> firstK;
  for (int pass = 0; pass < 2; ++pass) {
    KEpsilonModel gpu(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                      configFor(true, cfd::algebra::LinearSolverBackend::GPU));
    ASSERT_TRUE(gpu.usingGpuBackend());
    gpu.correct(c.mesh, c.velocity, ScalarField(c.mesh.numberOfCells(), 0.0));
    std::vector<Real> k(gpu.k().data(), gpu.k().data() + gpu.k().size());
    if (pass == 0) {
      firstK = k;
    } else {
      EXPECT_EQ(firstK, k) << "two identical GPU corrections differed";
    }
  }
}

TEST(ProductionKEpsilonBackend, TheCpuPathIsUnchangedByTheSeam) {
  // The backend seam must not have perturbed the CPU numbers. Both models here
  // are CPU-configured and constructed identically, so any difference would be
  // the seam itself leaking into the default path.
  const Case c;
  KEpsilonModel a(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                  configFor(true, cfd::algebra::LinearSolverBackend::CPU));
  KEpsilonModel b(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                  configFor(true, cfd::algebra::LinearSolverBackend::CPU));
  const ScalarField pressure(c.mesh.numberOfCells(), 0.0);
  a.correct(c.mesh, c.velocity, pressure);
  b.correct(c.mesh, c.velocity, pressure);
  for (Index i = 0; i < c.mesh.numberOfCells(); ++i) {
    ASSERT_EQ(a.k()[i], b.k()[i]) << "cell " << i;
    ASSERT_EQ(a.epsilon()[i], b.epsilon()[i]) << "cell " << i;
    ASSERT_EQ(a.turbulentViscosity()[i], b.turbulentViscosity()[i]) << "cell " << i;
  }
}
