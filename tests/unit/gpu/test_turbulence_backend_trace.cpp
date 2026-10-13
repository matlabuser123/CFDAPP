// P16-GPU-TURB Phase C0 -- execution observability and the fallback control.
//
// This file exists BEFORE any turbulence CUDA physics, and that order is the
// point. The Phase A audit established that no turbulence operation currently
// runs on the device, so a CPU-vs-GPU turbulence equivalence test written
// today would have both arms execute the identical host code, agree to the
// last bit, and prove nothing about CUDA.
//
// The gate that makes such a test meaningful is "prove the operation actually
// ran on the device", and the gate that makes THAT claim falsifiable is a
// deliberate fallback injection which the gate must catch. Both are here.
#include <gtest/gtest.h>

#include <vector>

#include "cfd/gpu/TurbulenceBackendTrace.hpp"

using cfd::gpu::ExecutionBackend;
using cfd::gpu::TurbulenceBackendTrace;
using cfd::gpu::TurbulenceOperation;

namespace {

// The operations a P16 k-epsilon GPU equivalence claim must cover. Thermal and
// SST are deliberately absent -- they are outside the frozen P16-GPU-TURB
// scope.
const std::vector<TurbulenceOperation>& requiredOperations() {
  static const std::vector<TurbulenceOperation> ops = {
      TurbulenceOperation::StrainRate,         TurbulenceOperation::Production,
      TurbulenceOperation::KAssembly,          TurbulenceOperation::KSolve,
      TurbulenceOperation::EpsilonAssembly,    TurbulenceOperation::EpsilonSolve,
      TurbulenceOperation::TurbulentViscosity, TurbulenceOperation::WallTreatment};
  return ops;
}

void recordAll(TurbulenceBackendTrace& trace, ExecutionBackend backend) {
  for (const auto op : requiredOperations()) trace.record(op, backend);
}

}  // namespace

TEST(TurbulenceBackendTrace, NotAttemptedIsNotGpu) {
  // THE test that makes the whole facility honest. A fresh trace must not
  // satisfy a GPU requirement, because "we never ran it" is not evidence that
  // it ran on the device. If this were true by default, every equivalence gate
  // built on top would pass vacuously before a single kernel existed.
  TurbulenceBackendTrace trace;
  trace.reset();
  for (const auto op : requiredOperations()) {
    EXPECT_EQ(trace.backendFor(op), ExecutionBackend::NotAttempted);
    EXPECT_EQ(trace.executionCount(op), 0u);
  }
  EXPECT_FALSE(trace.allOnGpu(requiredOperations().data(),
                              static_cast<cfd::Index>(requiredOperations().size())));
}

TEST(TurbulenceBackendTrace, AllOnGpuOnlyWhenEveryOperationRanOnDevice) {
  TurbulenceBackendTrace trace;
  trace.reset();
  recordAll(trace, ExecutionBackend::Gpu);
  EXPECT_TRUE(trace.allOnGpu(requiredOperations().data(),
                             static_cast<cfd::Index>(requiredOperations().size())));

  // Leave ONE operation unattempted and the claim must collapse.
  for (const auto missing : requiredOperations()) {
    TurbulenceBackendTrace partial;
    partial.reset();
    for (const auto op : requiredOperations()) {
      if (op != missing) partial.record(op, ExecutionBackend::Gpu);
    }
    EXPECT_FALSE(partial.allOnGpu(requiredOperations().data(),
                                  static_cast<cfd::Index>(requiredOperations().size())))
        << "a trace missing " << cfd::gpu::turbulenceOperationName(missing)
        << " still claimed full GPU execution";
  }
}

TEST(TurbulenceBackendTrace, ASingleHostExecutionDowngradesTheWholeOperation) {
  // The pessimistic rule. An operation that ran on the device for 99 outer
  // iterations and fell back once has FALLEN BACK, and a gate demanding GPU
  // must see it. Reporting "mostly GPU" would hide exactly the defect this
  // facility exists to catch.
  TurbulenceBackendTrace trace;
  trace.reset();
  for (int i = 0; i < 99; ++i) trace.record(TurbulenceOperation::KAssembly, ExecutionBackend::Gpu);
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::KAssembly), ExecutionBackend::Gpu);

  trace.record(TurbulenceOperation::KAssembly, ExecutionBackend::Cpu);
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::KAssembly), ExecutionBackend::Cpu);
  EXPECT_EQ(trace.executionCount(TurbulenceOperation::KAssembly), 100u);

  // And it must NOT recover afterwards.
  for (int i = 0; i < 50; ++i) trace.record(TurbulenceOperation::KAssembly, ExecutionBackend::Gpu);
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::KAssembly), ExecutionBackend::Cpu)
      << "a later GPU execution erased the record of a host fallback";
}

TEST(TurbulenceBackendTrace, ResetClearsEverything) {
  TurbulenceBackendTrace trace;
  trace.reset();
  recordAll(trace, ExecutionBackend::Gpu);
  trace.reset();
  for (const auto op : requiredOperations()) {
    EXPECT_EQ(trace.backendFor(op), ExecutionBackend::NotAttempted);
    EXPECT_EQ(trace.executionCount(op), 0u);
  }
}

// --- THE NEGATIVE CONTROL -----------------------------------------------------

TEST(TurbulenceBackendTrace, TheFallbackInjectorIsDetectedByTheGate) {
  // Without this, "every operation reports GPU" is an unfalsifiable claim.
  // Here the injector forces one operation onto the host, a caller that
  // honours it records Cpu, and the gate must fail.
  cfd::gpu::clearForcedCpuFallbacksForTesting();
  for (const auto op : requiredOperations()) {
    EXPECT_FALSE(cfd::gpu::cpuFallbackForcedForTesting(op));
  }

  for (const auto forced : requiredOperations()) {
    cfd::gpu::clearForcedCpuFallbacksForTesting();
    cfd::gpu::forceCpuFallbackForTesting(forced, true);

    TurbulenceBackendTrace trace;
    trace.reset();
    // A caller that honours the injector, which is the contract a real device
    // path will implement.
    for (const auto op : requiredOperations()) {
      trace.record(op, cfd::gpu::cpuFallbackForcedForTesting(op) ? ExecutionBackend::Cpu
                                                                 : ExecutionBackend::Gpu);
    }

    EXPECT_FALSE(trace.allOnGpu(requiredOperations().data(),
                                static_cast<cfd::Index>(requiredOperations().size())))
        << "forcing " << cfd::gpu::turbulenceOperationName(forced)
        << " onto the host was NOT detected, so the execution gate cannot catch a fallback:\n"
        << trace.describe();
    EXPECT_EQ(trace.backendFor(forced), ExecutionBackend::Cpu);
  }
  cfd::gpu::clearForcedCpuFallbacksForTesting();
}

TEST(TurbulenceBackendTrace, DescribeNamesEveryOperationAndBackend) {
  // The report a failing gate prints. Checked because a diagnostic nobody can
  // read is a diagnostic nobody uses.
  TurbulenceBackendTrace trace;
  trace.reset();
  trace.record(TurbulenceOperation::KAssembly, ExecutionBackend::Gpu);
  trace.record(TurbulenceOperation::WallTreatment, ExecutionBackend::Cpu);

  const std::string report = trace.describe();
  EXPECT_NE(report.find("k assembly: GPU"), std::string::npos) << report;
  EXPECT_NE(report.find("wall treatment: CPU"), std::string::npos) << report;
  EXPECT_NE(report.find("epsilon assembly: NOT ATTEMPTED"), std::string::npos) << report;
}

TEST(TurbulenceBackendTrace, ProcessWideTraceIsUsableAndResettable) {
  auto& trace = cfd::gpu::turbulenceBackendTrace();
  trace.reset();
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::KSolve), ExecutionBackend::NotAttempted);
  trace.record(TurbulenceOperation::KSolve, ExecutionBackend::Gpu);
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::KSolve), ExecutionBackend::Gpu);
  trace.reset();
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::KSolve), ExecutionBackend::NotAttempted);
}

// --- the P16 scope boundary, asserted ----------------------------------------

TEST(TurbulenceBackendTrace, ScopeCoversKEpsilonAndWallTreatmentOnly) {
  // The frozen P16-GPU-TURB scope, pinned so a later change has to be
  // deliberate. Thermal transport and SST are OUT, and adding an entry for
  // them here would be the first quiet step toward claiming CUDA equivalence
  // for capabilities that have none.
  EXPECT_EQ(static_cast<int>(TurbulenceOperation::Count), 8)
      << "the operation set changed -- if this is intentional, update "
         "results/p16/turbulence/gpu_equivalence/audit.md's frozen scope too";
}
