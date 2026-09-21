// GPU-DISC-001H -- the CUDA predicted face-mass-flux path.
//
// Transcribed from:
//   physics::calculateMassFlux                     MassFlux.cpp:15
//   pressure_velocity::rhieChowFaceCorrection      RhieChow.cpp:17
//   pressure_velocity::pressureCorrectionFaceCoupling / coupling
//                                                  PressureCorrectionEquation.cpp:74, :146
//
// The gate is BITWISE equality. Four things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false.
//
// 2. Both interpolateInternalFace overloads appear here and must not be
//    confused. The face VELOCITY and the face PRESSURE GRADIENT use the VECTOR
//    overload -- `(uP*dNf + uN*dPf) * (1/(dPf+dNf))`, a multiply by the
//    reciprocal. The RESPONSE COEFFICIENTS use the SCALAR overload --
//    `(dNf*aP + dPf*aN) / (dPf+dNf)`, a divide. Swapping them is the exact
//    defect NC1 caught in diffusion.
//
// 3. The two geometric predicates (isAxisAligned, exactlyParallel) are EXACT
//    and mesh-only, so the host decided them and stored a flag. The CPU
//    evaluates the test twice -- once to choose which response field to
//    interpolate, once inside coupling() -- and in the axis-aligned branch
//    du == dv == dw, so one stored flag reproduces both.
//
// 4. Boundary faces get correction exactly 0.0: rhieChowFaceCorrection skips
//    them, and the SurfaceField was value-initialised to 0.0.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceConvectionTerms.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__device__ inline Real fluxDotGuarded(Real ax, Real ay, Real az, Real bx, Real by, Real bz) {
  const Real inPlane = (ax * bx) + (ay * by);
  const Real normal = az * bz;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

__device__ inline Real magnitudeOf(Real x, Real y, Real z) {
  return sqrt(fluxDotGuarded(x, y, z, x, y, z));
}

}  // namespace

struct FaceFluxPlanView {
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* areaX;
  const Real* areaY;
  const Real* areaZ;
  const Real* faceArea;
  const Real* dPf;
  const Real* dNf;
  const Real* dX;
  const Real* dY;
  const Real* dZ;
  const Real* distance;
  const Index* axisAligned;
  const Index* axisIndex;
  const Index* areaZIsZero;
  DeviceVectorBoundaryView boundary;
};

namespace {

using View = FaceFluxPlanView;

// interpolateFace's vector overload, then density * dot(u_f, Sf).
__global__ void massFluxKernel(Index faceCount, View p, const Real* __restrict__ ux,
                               const Real* __restrict__ uy, const Real* __restrict__ uz,
                               Real density, Real* __restrict__ massFlux) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  const Index owner = p.faceOwner[f];
  const Index neighbor = p.faceNeighbor[f];

  Real vx, vy, vz;
  if (neighbor == DeviceMesh::kNoNeighbor) {
    vx = deviceBoundaryVelocityComponent(p.boundary, f, ux[owner], uy[owner], uz[owner],
                                         kVelocityU);
    vy = deviceBoundaryVelocityComponent(p.boundary, f, ux[owner], uy[owner], uz[owner],
                                         kVelocityV);
    vz = deviceBoundaryVelocityComponent(p.boundary, f, ux[owner], uy[owner], uz[owner],
                                         kVelocityW);
  } else {
    // VECTOR interpolateInternalFace: multiply by the reciprocal.
    const Real dP = p.dPf[f];
    const Real dN = p.dNf[f];
    const Real inverse = 1.0 / (dP + dN);
    vx = ((ux[owner] * dN) + (ux[neighbor] * dP)) * inverse;
    vy = ((uy[owner] * dN) + (uy[neighbor] * dP)) * inverse;
    vz = ((uz[owner] * dN) + (uz[neighbor] * dP)) * inverse;
  }
  massFlux[f] = density * fluxDotGuarded(vx, vy, vz, p.areaX[f], p.areaY[f], p.areaZ[f]);
}

// coupling(face, d, density, du, dv, dw, nonOrthogonal = false).
__device__ inline Real couplingCoefficient(const View& p, Index f, Real du, Real dv, Real dw,
                                           Real density) {
  const Real distance = p.distance[f];
  if (p.axisAligned[f] != 0) {
    const Index axis = p.axisIndex[f];
    const Real dComponent = axis == 0 ? du : (axis == 1 ? dv : dw);
    // density * face.area() * dComponent / distance, left to right.
    return density * p.faceArea[f] * dComponent / distance;
  }
  // responseVector = (sf.z == 0.0) ? {du*sx, dv*sy} : {du*sx, dv*sy, dw*sz}
  const Real rx = du * p.areaX[f];
  const Real ry = dv * p.areaY[f];
  const Real rz = p.areaZIsZero[f] != 0 ? 0.0 : dw * p.areaZ[f];
  return density * magnitudeOf(rx, ry, rz) / distance;
}

__global__ void rhieChowKernel(Index faceCount, View p, const Real* __restrict__ pressure,
                               const Real* __restrict__ gpx, const Real* __restrict__ gpy,
                               const Real* __restrict__ gpz, const Real* __restrict__ dU,
                               const Real* __restrict__ dV, const Real* __restrict__ dW,
                               Real density, Real alpha, Real* __restrict__ correction) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  const Index owner = p.faceOwner[f];
  const Index neighbor = p.faceNeighbor[f];
  if (neighbor == DeviceMesh::kNoNeighbor) {
    correction[f] = 0.0;  // boundary faces are skipped; the field starts at 0.0
    return;
  }

  const Real dP = p.dPf[f];
  const Real dN = p.dNf[f];

  // pressureCorrectionFaceCoupling: which response fields get interpolated
  // depends on the axis-aligned test -- one SCALAR interpolation in that
  // branch, three in the general one.
  Real du, dv, dw;
  if (p.axisAligned[f] != 0) {
    const Index axis = p.axisIndex[f];
    const Real* response = axis == 0 ? dU : (axis == 1 ? dV : dW);
    // SCALAR interpolateInternalFace: divide.
    const Real dFace = ((dN * response[owner]) + (dP * response[neighbor])) / (dP + dN);
    du = dFace;
    dv = dFace;
    dw = dFace;
  } else {
    du = ((dN * dU[owner]) + (dP * dU[neighbor])) / (dP + dN);
    dv = ((dN * dV[owner]) + (dP * dV[neighbor])) / (dP + dN);
    dw = dW == nullptr ? 0.0 : ((dN * dW[owner]) + (dP * dW[neighbor])) / (dP + dN);
  }
  const Real coupling = couplingCoefficient(p, f, du, dv, dw, density);

  // gradFace: the VECTOR overload again.
  const Real inverse = 1.0 / (dP + dN);
  const Real gx = ((gpx[owner] * dN) + (gpx[neighbor] * dP)) * inverse;
  const Real gy = ((gpy[owner] * dN) + (gpy[neighbor] * dP)) * inverse;
  const Real gz = ((gpz[owner] * dN) + (gpz[neighbor] * dP)) * inverse;

  const Real compactMinusInterpolated =
      (pressure[neighbor] - pressure[owner]) -
      fluxDotGuarded(gx, gy, gz, p.dX[f], p.dY[f], p.dZ[f]);
  correction[f] = -(coupling / alpha) * compactMinusInterpolated;
}

__global__ void addKernel(Index faceCount, const Real* __restrict__ correction,
                          Real* __restrict__ flux) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  flux[f] = flux[f] + correction[f];
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void fillFaceFluxPlanView(const DeviceFaceFluxPlan& plan, FaceFluxPlanView& p) {
  const DeviceMesh& mesh = plan.convection_.mesh();
  p.faceOwner = mesh.faceOwner();
  p.faceNeighbor = mesh.faceNeighbor();
  p.areaX = mesh.faceAreaX();
  p.areaY = mesh.faceAreaY();
  p.areaZ = mesh.faceAreaZ();
  p.faceArea = mesh.faceArea();
  const DeviceConvectionGeometry g = plan.convection_.geometry();
  p.dPf = g.dPf;
  p.dNf = g.dNf;
  p.dX = plan.dX_.data();
  p.dY = plan.dY_.data();
  p.dZ = plan.dZ_.data();
  p.distance = plan.distance_.data();
  p.axisAligned = plan.axisAligned_.data();
  p.axisIndex = plan.axisIndex_.data();
  p.areaZIsZero = plan.areaZIsZero_.data();
  p.boundary = plan.convection_.boundaryView();
}

void calculateMassFluxDevice(const DeviceFaceFluxPlan& plan, const DeviceVelocity& velocity,
                             Real density, DeviceBuffer<cfd::Real>& massFlux) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("calculateMassFluxDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const Index nf = plan.faceCount_;
  massFlux.resize(nf);
  if (nf == 0) return;
  FaceFluxPlanView p{};
  fillFaceFluxPlanView(plan, p);
  cfd::Timer timer;
  massFluxKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(
      nf, p, velocity.x.data(), velocity.y.data(), velocity.z.data(), density, massFlux.data());
  recordLaunch("massFluxKernel launch", timer);
}

void rhieChowFaceCorrectionDevice(const DeviceFaceFluxPlan& plan,
                                  const DeviceBuffer<cfd::Real>& pressure,
                                  const DeviceBuffer<cfd::Real>& pressureGradX,
                                  const DeviceBuffer<cfd::Real>& pressureGradY,
                                  const DeviceBuffer<cfd::Real>& pressureGradZ,
                                  const DeviceBuffer<cfd::Real>& dU,
                                  const DeviceBuffer<cfd::Real>& dV,
                                  const DeviceBuffer<cfd::Real>* dW, Real density, Real alpha,
                                  DeviceBuffer<cfd::Real>& correction) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("rhieChowFaceCorrectionDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  if (plan.threeDimensional_ && dW == nullptr) {
    throw cfd::InvalidArgumentError(
        "rhieChowFaceCorrectionDevice: a 3D mesh needs the w response dW");
  }
  if (!(alpha > 0.0) || alpha > 1.0 || !isfinite(alpha)) {
    throw cfd::InvalidArgumentError(
        "rhieChowFaceCorrectionDevice: alpha must be finite and in (0, 1]");
  }
  const Index nf = plan.faceCount_;
  correction.resize(nf);
  if (nf == 0) return;
  FaceFluxPlanView p{};
  fillFaceFluxPlanView(plan, p);
  cfd::Timer timer;
  rhieChowKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(
      nf, p, pressure.data(), pressureGradX.data(), pressureGradY.data(), pressureGradZ.data(),
      dU.data(), dV.data(), dW == nullptr ? nullptr : dW->data(), density, alpha,
      correction.data());
  recordLaunch("rhieChowKernel launch", timer);
}

void rhieChowMassFluxDevice(const DeviceFaceFluxPlan& plan, const DeviceVelocity& velocity,
                            const DeviceBuffer<cfd::Real>& pressure,
                            const DeviceBuffer<cfd::Real>& pressureGradX,
                            const DeviceBuffer<cfd::Real>& pressureGradY,
                            const DeviceBuffer<cfd::Real>& pressureGradZ,
                            const DeviceBuffer<cfd::Real>& dU, const DeviceBuffer<cfd::Real>& dV,
                            const DeviceBuffer<cfd::Real>* dW, Real density, Real alpha,
                            DeviceBuffer<cfd::Real>& massFlux) {
  calculateMassFluxDevice(plan, velocity, density, massFlux);
  static thread_local DeviceBuffer<cfd::Real> correction;
  rhieChowFaceCorrectionDevice(plan, pressure, pressureGradX, pressureGradY, pressureGradZ, dU, dV,
                               dW, density, alpha, correction);
  const Index nf = massFlux.size();
  if (nf == 0) return;
  cfd::Timer timer;
  addKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(nf, correction.data(), massFlux.data());
  recordLaunch("flux addKernel launch", timer);
}

}  // namespace cfd::gpu
