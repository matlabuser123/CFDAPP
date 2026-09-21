#!/usr/bin/env bash
# GPU-DISC-001P -- record the sha256 of every production source the negative-control
# campaign is allowed to touch, BEFORE any mutation. Restoration is verified against
# this file, and it is re-run at the end of the campaign to prove the tree is clean.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT"

FILES="
cuda/kernels/DeviceGradientKernel.cu
cuda/kernels/DeviceGradientPlan.cpp
cuda/kernels/DeviceLeastSquaresGradientKernel.cu
cuda/kernels/DeviceLeastSquaresGradientPlan.cpp
cuda/kernels/DeviceDiffusionKernel.cu
cuda/kernels/DeviceDiffusionPlan.cpp
cuda/kernels/DeviceConvectionKernel.cu
cuda/kernels/DeviceConvectionPlan.cpp
cuda/kernels/DeviceMomentumConvectionKernel.cu
cuda/kernels/DeviceMomentumConvectionPlan.cpp
cuda/kernels/DeviceBoundaryConditions.cpp
cuda/kernels/DeviceBoundaryConditionsKernel.cu
cuda/kernels/DeviceMomentumAssemblyKernel.cu
cuda/kernels/DeviceMomentumAssemblyPlan.cpp
cuda/kernels/DeviceMomentumResponseKernel.cu
cuda/kernels/DeviceFaceFluxKernel.cu
cuda/kernels/DeviceFaceFluxPlan.cpp
cuda/kernels/DevicePressureCorrectionKernel.cu
cuda/kernels/DevicePressureCorrectionPlan.cpp
cuda/kernels/DeviceVelocityCorrectionKernel.cu
cuda/kernels/DeviceVelocityCorrectionPlan.cpp
cuda/kernels/DeviceFaceFluxCorrectionKernel.cu
cuda/kernels/GpuSimpleDiscretizationCuda.cpp
include/cfd/gpu/DeviceDiffusionTerms.hpp
include/cfd/gpu/DeviceConvectionTerms.hpp
include/cfd/gpu/DeviceBoundaryConditions.hpp
include/cfd/gpu/BoundaryEncoding.hpp
include/cfd/gpu/VectorBoundaryEncoding.hpp
include/cfd/gpu/ObliqueNeumann.hpp
include/cfd/gpu/GpuSimpleDiscretization.hpp
src/pressure_velocity/SIMPLE.cpp
"

for f in $FILES; do
  if [ ! -f "$f" ]; then
    echo "MISSING  $f"
    continue
  fi
  sha256sum "$f"
done
