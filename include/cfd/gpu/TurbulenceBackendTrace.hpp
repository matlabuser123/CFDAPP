#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "cfd/core/Types.hpp"

// P16-GPU-TURB Phase C0 -- per-OPERATION execution evidence.
//
// WHY THIS EXISTS, AND WHY IT COMES FIRST
// ---------------------------------------
// The Phase A audit established that NO turbulence operation currently runs on
// the device: the GPU-resident SIMPLE loop requires
// `activeModel->name() == "laminar"` (SIMPLE.cpp:458) and
// `activeModel->correct()` is unconditionally a host call (SIMPLE.cpp:547).
//
// That makes a naive CPU-vs-GPU turbulence equivalence test WORSE than
// useless: both arms would execute the identical host code and agree to the
// last bit, "proving" an equivalence that says nothing about CUDA. So the
// evidence that an operation actually ran on the device has to exist BEFORE
// any turbulence CUDA physics is written, not after.
//
// WHAT THIS IS NOT
// `GPUExecutionStats` already counts VOLUME -- bytes, calls, allocations,
// kernel launches -- process-wide. That answers "did the device do work". It
// cannot answer "did the EPSILON ASSEMBLY specifically run on the device",
// which is the question a per-operation equivalence gate has to ask. This is
// that second question, and it is deliberately a separate, tiny facility
// rather than more fields on the volume counters.
//
// DESIGN CONSTRAINTS
//   * no CUDA dependency -- compiles and works in a CPU-only build, where
//     every operation simply records Cpu;
//   * no permanent stderr noise -- it is a queryable record, not logging;
//   * an operation that was never attempted is DISTINCT from one that ran on
//     the host. "Not attempted" must never read as "ran on GPU".
namespace cfd::gpu {

// The turbulence operations whose backend a P16 equivalence gate must be able
// to interrogate individually. Deliberately narrow: exactly the flow-side
// k-epsilon + high-Re wall treatment scope frozen for P16-GPU-TURB. Thermal
// and SST are explicitly out of scope and have no entries here.
enum class TurbulenceOperation : std::uint8_t {
  StrainRate,
  Production,
  KAssembly,
  KSolve,
  EpsilonAssembly,
  EpsilonSolve,
  TurbulentViscosity,
  WallTreatment,
  Count,
};

[[nodiscard]] std::string_view turbulenceOperationName(TurbulenceOperation operation);

enum class ExecutionBackend : std::uint8_t {
  // The operation did not run at all this solve. The default, and NOT a
  // synonym for Cpu: a gate that requires GPU must fail on NotAttempted just
  // as loudly as on Cpu, because "we never ran it" is not evidence of
  // anything.
  NotAttempted,
  Cpu,
  Gpu,
};

[[nodiscard]] std::string_view executionBackendName(ExecutionBackend backend);

// A record of which backend each turbulence operation last executed on.
//
// MIXED EXECUTION IS RECORDED, NOT COLLAPSED. If an operation runs on the GPU
// for some outer iterations and falls back to the host for others, the record
// reports Cpu -- the pessimistic answer. A single host execution is a fallback
// and a gate demanding GPU must see it. Claiming Gpu because "mostly GPU"
// would defeat the entire purpose of this file.
class TurbulenceBackendTrace {
 public:
  void reset() noexcept;

  // Records one execution. Calling this with Cpu after a Gpu is what makes the
  // record pessimistic: it downgrades and never upgrades back within a solve.
  void record(TurbulenceOperation operation, ExecutionBackend backend) noexcept;

  [[nodiscard]] ExecutionBackend backendFor(TurbulenceOperation operation) const noexcept;
  [[nodiscard]] std::uint64_t executionCount(TurbulenceOperation operation) const noexcept;

  // True only when EVERY operation in `required` executed on the device. An
  // operation that was NotAttempted makes this false.
  [[nodiscard]] bool allOnGpu(const TurbulenceOperation* required, Index count) const noexcept;

  // A one-line-per-operation human-readable report, for a failing test's
  // message. Not printed anywhere by default.
  [[nodiscard]] std::string describe() const;

 private:
  std::array<ExecutionBackend, static_cast<std::size_t>(TurbulenceOperation::Count)> backends_{};
  std::array<std::uint64_t, static_cast<std::size_t>(TurbulenceOperation::Count)> counts_{};
};

// Process-wide, same convention as gpuExecutionStats(), and ACCUMULATING: it
// is reset only by an explicit reset(), which no production path calls. So a
// reader sees every operation recorded since the last reset, not the ones from
// the most recent solve.
//
// That matters more than it sounds. The host k-epsilon path records NOTHING --
// only the device implementations call record() -- so a CPU solve leaves the
// trace exactly as the previous GPU solve left it. A test that reads the trace
// after a CPU solve, without resetting first, will be told the GPU path ran.
// Every caller that means "what did THIS solve do" must reset() immediately
// before it.
//
// (An earlier version of this comment claimed a solve resets it at entry. It
// does not, and never did; the claim was corrected when a P16 non-vacuity
// control caught a CPU arm reporting the GPU path as qualified.)
[[nodiscard]] TurbulenceBackendTrace& turbulenceBackendTrace() noexcept;

// P16-GPU-TURB: the fallback-injection switch that makes the negative control
// possible.
//
// When set, the named operation is FORCED onto the host path even where a
// device implementation exists. It exists so a test can prove the execution
// gate actually detects a fallback -- without it, "all operations report GPU"
// is an unfalsifiable claim.
//
// Test-only in intent; it is a plain global with no case-file key and nothing
// in production sets it.
void forceCpuFallbackForTesting(TurbulenceOperation operation, bool forced) noexcept;
[[nodiscard]] bool cpuFallbackForcedForTesting(TurbulenceOperation operation) noexcept;
void clearForcedCpuFallbacksForTesting() noexcept;

}  // namespace cfd::gpu
