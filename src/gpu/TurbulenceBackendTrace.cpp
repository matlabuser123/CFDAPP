#include "cfd/gpu/TurbulenceBackendTrace.hpp"

#include <array>
#include <string>

namespace cfd::gpu {

namespace {

constexpr std::size_t kOperationCount = static_cast<std::size_t>(TurbulenceOperation::Count);

std::array<bool, kOperationCount>& forcedFallbacks() noexcept {
  static std::array<bool, kOperationCount> forced{};
  return forced;
}

}  // namespace

std::string_view turbulenceOperationName(TurbulenceOperation operation) {
  switch (operation) {
    case TurbulenceOperation::StrainRate:
      return "strain rate";
    case TurbulenceOperation::Production:
      return "production P_k";
    case TurbulenceOperation::KAssembly:
      return "k assembly";
    case TurbulenceOperation::KSolve:
      return "k solve";
    case TurbulenceOperation::EpsilonAssembly:
      return "epsilon assembly";
    case TurbulenceOperation::EpsilonSolve:
      return "epsilon solve";
    case TurbulenceOperation::TurbulentViscosity:
      return "mu_t";
    case TurbulenceOperation::WallTreatment:
      return "wall treatment";
    case TurbulenceOperation::Count:
      break;
  }
  return "unknown";
}

std::string_view executionBackendName(ExecutionBackend backend) {
  switch (backend) {
    case ExecutionBackend::NotAttempted:
      return "NOT ATTEMPTED";
    case ExecutionBackend::Cpu:
      return "CPU";
    case ExecutionBackend::Gpu:
      return "GPU";
  }
  return "unknown";
}

void TurbulenceBackendTrace::reset() noexcept {
  backends_.fill(ExecutionBackend::NotAttempted);
  counts_.fill(0);
}

void TurbulenceBackendTrace::record(TurbulenceOperation operation,
                                    ExecutionBackend backend) noexcept {
  const auto index = static_cast<std::size_t>(operation);
  if (index >= kOperationCount) return;
  ++counts_[index];

  // PESSIMISTIC by design. Once an operation has executed on the host during a
  // solve, this record says Cpu for the rest of it, even if later iterations
  // run on the device. A single host execution IS a fallback, and a gate that
  // demands GPU has to see it; reporting "mostly GPU" would make this file
  // useless for the one job it exists to do.
  if (backends_[index] == ExecutionBackend::Cpu) return;
  if (backends_[index] == ExecutionBackend::NotAttempted || backend == ExecutionBackend::Cpu) {
    backends_[index] = backend;
  }
}

ExecutionBackend TurbulenceBackendTrace::backendFor(TurbulenceOperation operation) const noexcept {
  const auto index = static_cast<std::size_t>(operation);
  if (index >= kOperationCount) return ExecutionBackend::NotAttempted;
  return backends_[index];
}

std::uint64_t TurbulenceBackendTrace::executionCount(TurbulenceOperation operation) const noexcept {
  const auto index = static_cast<std::size_t>(operation);
  if (index >= kOperationCount) return 0;
  return counts_[index];
}

bool TurbulenceBackendTrace::allOnGpu(const TurbulenceOperation* required,
                                      Index count) const noexcept {
  if (required == nullptr) return false;
  for (Index i = 0; i < count; ++i) {
    // NotAttempted fails here exactly as Cpu does. "We never ran it" is not
    // evidence that it ran on the device.
    if (backendFor(required[i]) != ExecutionBackend::Gpu) return false;
  }
  return true;
}

std::string TurbulenceBackendTrace::describe() const {
  std::string out;
  for (std::size_t i = 0; i < kOperationCount; ++i) {
    const auto operation = static_cast<TurbulenceOperation>(i);
    out += std::string(turbulenceOperationName(operation));
    out += ": ";
    out += std::string(executionBackendName(backends_[i]));
    out += " (" + std::to_string(counts_[i]) + " executions)\n";
  }
  return out;
}

TurbulenceBackendTrace& turbulenceBackendTrace() noexcept {
  static TurbulenceBackendTrace trace;
  return trace;
}

void forceCpuFallbackForTesting(TurbulenceOperation operation, bool forced) noexcept {
  const auto index = static_cast<std::size_t>(operation);
  if (index >= kOperationCount) return;
  forcedFallbacks()[index] = forced;
}

bool cpuFallbackForcedForTesting(TurbulenceOperation operation) noexcept {
  const auto index = static_cast<std::size_t>(operation);
  if (index >= kOperationCount) return false;
  return forcedFallbacks()[index];
}

void clearForcedCpuFallbacksForTesting() noexcept { forcedFallbacks().fill(false); }

}  // namespace cfd::gpu
