#pragma once

#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/Preconditioner.hpp"

namespace cfd::algebra {

// P6-GPU-002 -- Performance: the one place that turns data
// (LinearSolverSettings::type/backend -- from a case file, GUI, or a
// direct C++ call site) into a concrete cfd::algebra::LinearSolver,
// mirroring the architecture this task's own design calls for:
//
//   SIMPLE -> LinearSolver interface -> backend selection -> {CPU CG,
//   CPU BiCGSTAB, GPU CG, GPU BiCGSTAB}
//
// `settings.backend == GPU` is a *request*, not a guarantee: if this
// binary has no CUDA support, or a CUDA build finds no usable device at
// runtime, this function transparently falls back to the CPU-equivalent
// solver of the same `type` -- deterministic (the same runtime check
// cfd::gpu::cudaAvailable() always gives the same answer for a given
// process/device state), logged once via cfd::Logger at Warning level so
// the fallback is never silent, and counted in
// cfd::gpu::gpuExecutionStats().gpuBackendFallbacks so a caller can
// assert on it in a test without parsing log output. The returned
// solver's own SolverResult::backendUsed always reports which backend
// actually ran (CPU here, even though the caller asked for GPU) -- see
// LinearSolver.hpp's own header comment on that field.
//
// Never throws for an unavailable GPU (that is the documented fallback
// path, not an error); still throws InvalidArgumentError for a
// structurally invalid `settings` (non-positive tolerances/
// maxIterations), exactly as constructing CG/BiCGSTAB directly already
// does (LinearSolver's own constructor validates this).
[[nodiscard]] std::unique_ptr<LinearSolver> makeLinearSolver(
    LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner = nullptr);

}  // namespace cfd::algebra
