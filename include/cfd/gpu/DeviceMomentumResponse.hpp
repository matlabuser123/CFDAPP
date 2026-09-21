#pragma once

// GPU-DISC-001G -- CUDA-only.
//
// CUDA port of cfd::pressure_velocity::computeMomentumResponseCoefficient:
//
//     d_P = V_P / aP_P
//
// where aP is a momentum-system diagonal (the output of the qualified 001F
// assembly) and V_P the cell volume. Deliberately tiny and free-standing, so the
// Rhie-Chow / pressure-correction work can call it directly.
//
// The CPU performs this division UNGUARDED -- its input contract ("already
// positive, finite, and nonzero") is enforced upstream at assembly time, not
// here. That is reproduced exactly; no safeguard is added, because adding one
// would change numerical semantics rather than port them. See
// results/gpu-disc-001/momentum-response/audit.md section 5.

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceMesh.hpp"

namespace cfd::gpu {

// `momentumDiagonal` is DeviceMomentumSystem::diagonal for one component.
// `response` is resized as needed and left device-resident; no transfer occurs.
// Throws InvalidArgumentError on a size mismatch -- the same and only check the
// CPU performs.
void computeMomentumResponseCoefficientDevice(const DeviceMesh& mesh,
                                              const DeviceBuffer<cfd::Real>& momentumDiagonal,
                                              DeviceBuffer<cfd::Real>& response);

}  // namespace cfd::gpu
