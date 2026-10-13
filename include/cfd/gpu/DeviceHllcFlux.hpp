#pragma once

// P17.7 -- CUDA-only. See DeviceBuffer.hpp's "never include from a CPU-only-compiled file" rule.
//
// This is the thinnest GPU port in the project, deliberately. Every other device kernel here
// re-expresses its CPU algorithm in a .cu; this one does NOT. The HLLC numerics live entirely in
// include/cfd/compressible/HllcFlux.hpp, marked CFD_HLLC_HOST_DEVICE, and both gcc and nvcc compile
// THAT SAME SOURCE. The kernel below only marshals memory and calls it.
//
// WHY THAT MATTERS FOR THE GATE. P17_GATES.md G8 says "where the operation order is identical,
// BITWISE is the correct requirement and anything looser is a concession that must be argued for".
// With one source there is no reformulation and no reordering, so bitwise is demanded rather than
// hoped for. The single remaining difference is FMA CONTRACTION -- nvcc fuses a*b+c by default and
// the host x86-64 build does not -- so this translation unit is compiled with `--fmad=false`
// (set in cuda/CMakeLists.txt). The test measures BOTH settings so the cost of contraction is
// reported rather than hidden behind a tolerance.
//
// Evidence: results/p17/hllc/cpu_gpu_equivalence.md

#include <string>
#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/core/Types.hpp"

#include "cfd/core/Vector3.hpp"

namespace cfd::gpu {

// One Riemann problem. A flat POD so it can be memcpy'd to the device unchanged.
struct HllcDeviceInput {
  cfd::compressible::PrimitiveState left{};
  cfd::compressible::PrimitiveState right{};
  cfd::Vector3 normal{};
  cfd::Real gamma{};
  int estimator{0};  // 0 = ToroPressureBased (production), 1 = Davis (control V1)
  int pad{0};
};

// Everything the section 29 comparison needs. Comparing only the final flux would hide a case where
// CPU and GPU selected DIFFERENT BRANCHES and happened to land close, so the wave speeds, the
// branch and the star states are all carried out.
struct HllcDeviceOutput {
  cfd::Real flux[5]{};  // mass, x/y/z momentum, energy
  cfd::Real sLeft{};
  cfd::Real sContact{};
  cfd::Real sRight{};
  cfd::Real denominator{};
  cfd::Real starDensityLeft{};
  cfd::Real starDensityRight{};
  cfd::Real starInternalEnergyLeft{};
  cfd::Real starInternalEnergyRight{};
  int branch{};
  int validity{};
  int valid{};
  int pad{0};
};

// true when a usable CUDA device is present. Every entry point below is a no-op returning false if
// not, so a CUDA-enabled build on a machine without a GPU degrades to a skipped test rather than a
// crash -- the same policy the rest of the GPU layer follows.
[[nodiscard]] bool hllcDeviceAvailable();

// Evaluates cfd::compressible::hllcFlux() on device, one thread per entry, and copies the results
// back. Returns false if no device is available or a CUDA call failed; `message` then says why.
bool evaluateHllcOnDevice(const HllcDeviceInput* inputs, HllcDeviceOutput* outputs, int count,
                          std::string* message);

// The same, but calling hllFluxForControlOnly() -- the contact wave deleted. Present so control V2
// can be exercised on device too, which is the only way to show the GPU path is not silently a
// different scheme.
bool evaluateHllOnDeviceForControlOnly(const HllcDeviceInput* inputs, HllcDeviceOutput* outputs,
                                       int count, std::string* message);

}  // namespace cfd::gpu
