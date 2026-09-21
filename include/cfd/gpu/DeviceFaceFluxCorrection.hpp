#pragma once

// GPU-DISC-001K -- CUDA-only.
//
// CUDA port of cfd::pressure_velocity::correctFaceMassFlux
// (PressureCorrectionEquation.cpp:369): the face mass flux updated by the
// solved pressure correction.
//
//   F'_f = faceCoefficient[f] * (p'_owner - p'_neighbour)   (p'_neighbour = 0 at a boundary)
//   F_f  = F*_f + F'_f  [+ explicitFaceFlux[f]]
//
// There is NO PLAN. The operator needs only the device mesh's owner/neighbour
// arrays; everything else it consumes is already produced and qualified
// upstream:
//
//   F*                 GPU-DISC-001H  rhieChowMassFluxDevice
//   faceCoefficient    GPU-DISC-001I  DevicePressureCorrectionSystem
//   explicitFaceFlux   GPU-DISC-001I  DevicePressureCorrectionSystem
//   p'                 GPU-DISC-001I assembly + GPU-PCORR-001 solve
//
// 001I emits both coefficient fields device-side precisely so this step can
// reuse the EXACT coefficients the pressure-correction matrix was built with.
// Recomputing either here would reintroduce the disagreement that sharing them
// exists to prevent (TODO.md section 30).
//
// Three things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false.
//
// 2. The boundary face is NOT skipped. It takes p'_neighbour = 0.0 and is made
//    a no-op, where it should be one, by faceCoefficient being exactly 0.0 on
//    every non-FixedValue boundary face. The arithmetic is reproduced rather
//    than branched around: `x + (±0.0)` is exactly `x` for every finite x, so
//    the no-op is bitwise, and reproducing the expression is what keeps this
//    faithful to the CPU rather than merely equal to it.
//
// 3. The explicit term is a SEPARATE addition: (F* + F') + E, left to right.
//
// See results/gpu-disc-001/face-flux-correction/audit.md.

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceMesh.hpp"

namespace cfd::gpu {

// `explicitFaceFlux` is null on a first pass, exactly as the CPU's nullable
// parameter is; SIMPLE passes it only when nonOrthogonalCorrections > 1, and
// PISO never does. `corrected` is resized as needed and may alias nothing else.
void correctFaceMassFluxDevice(const DeviceMesh& mesh,
                               const DeviceBuffer<cfd::Real>& predictorMassFlux,
                               const DeviceBuffer<cfd::Real>& faceCoefficient,
                               const DeviceBuffer<cfd::Real>& pressureCorrection,
                               const DeviceBuffer<cfd::Real>* explicitFaceFlux,
                               DeviceBuffer<cfd::Real>& corrected);

}  // namespace cfd::gpu
