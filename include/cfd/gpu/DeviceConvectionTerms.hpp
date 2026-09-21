#pragma once

// GPU-DISC-001F -- the shared device implementation of the momentum convection
// face terms.
//
// Extracted from DeviceMomentumConvectionKernel.cu when the momentum assembly
// needed the same terms. The reason it had to be shared rather than reused by
// calling 001D's kernel is worth recording, because it is not obvious:
//
//   SparseMatrixBuilder sums repeated (row, column) entries in INSERTION order,
//   and the CPU inserts every diffusion triplet before every convection one.
//   So each entry accumulates d1+d2+...+dk+c1+c2+...+ck strictly left to right.
//   Calling 001D's kernel to produce a convection subtotal and adding it to a
//   diffusion subtotal computes (d1+..+dk) + (c1+..+ck) instead -- algebraically
//   identical, and NOT bitwise identical.
//
// So the momentum assembly walks the faces itself, in face-id order, adding the
// convection contribution into the same running value. It calls these shared
// functions to do it, so there is still exactly one implementation of the
// scheme machinery.
//
// 001D's 10352-case bitwise differential is re-run after the extraction.
//
// Compile any translation unit including this with -fmad=false.

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/gpu/VectorBoundaryEncoding.hpp"

namespace cfd::gpu {

// Per-face immutable convection geometry. The upwind side depends on the sign
// of the mass flux -- a field -- so the far-upstream data is stored for BOTH
// orientations and selected at evaluation time.
struct DeviceConvectionGeometry {
  const cfd::Real* dPf;
  const cfd::Real* dNf;
  const cfd::Real* offOX;
  const cfd::Real* offOY;
  const cfd::Real* offOZ;
  const cfd::Real* offNX;
  const cfd::Real* offNY;
  const cfd::Real* offNZ;
  const cfd::Index* farOValid;
  const cfd::Index* farOCell;
  const cfd::Real* farOHCU;
  const cfd::Index* farNValid;
  const cfd::Index* farNCell;
  const cfd::Real* farNHCU;
};

// The encoded vector boundary conditions, as the convection plan stores them.
struct DeviceVectorBoundaryView {
  const cfd::Index* kind;
  const cfd::Real* constX;
  const cfd::Real* constY;
  const cfd::Real* constZ;
  const cfd::Real* normalX;
  const cfd::Real* normalY;
  const cfd::Real* normalZ;
};

#if defined(__CUDACC__)

__device__ inline cfd::Real convectionDotGuarded(cfd::Real ax, cfd::Real ay, cfd::Real az,
                                                 cfd::Real bx, cfd::Real by, cfd::Real bz) {
  const cfd::Real inPlane = (ax * bx) + (ay * by);
  const cfd::Real normal = az * bz;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

__device__ inline cfd::Real deviceQuickFaceValue(cfd::Real phiC, cfd::Real hCU, cfd::Real phiU,
                                                 cfd::Real hUf, cfd::Real phiD, cfd::Real hfD) {
  const cfd::Real weightC = (-hUf * hfD) / (hCU * (hUf + hCU + hfD));
  const cfd::Real weightU = ((hUf + hCU) * hfD) / (hCU * (hUf + hfD));
  const cfd::Real weightD = (hUf * (hUf + hCU)) / ((hUf + hCU + hfD) * (hUf + hfD));
  return (weightC * phiC) + (weightU * phiU) + (weightD * phiD);
}

// std::optional<Real> as a NaN sentinel.
__device__ inline cfd::Real deviceSmoothnessRatio(cfd::Real phiC, cfd::Real phiU, cfd::Real phiD) {
  const cfd::Real localGradient = phiD - phiU;
  if (localGradient == 0.0) return nan("");
  return (phiU - phiC) / localGradient;
}

__device__ inline cfd::Real deviceVanLeerLimiter(cfd::Real r, bool hasValue) {
  // has_value() is tested FIRST on the CPU: `r <= 0.0` is false for NaN, so
  // collapsing the two tests would let a nullopt through as a full blend.
  if (!hasValue || isnan(r) || r <= 0.0) return 0.0;
  return (r + fabs(r)) / (1.0 + fabs(r));
}

__device__ inline cfd::Real deviceClamp(cfd::Real v, cfd::Real lo, cfd::Real hi) {
  return v < lo ? lo : (hi < v ? hi : v);
}

// boundaryVelocity() followed by selectComponent(), in one step.
__device__ inline cfd::Real deviceBoundaryVelocityComponent(const DeviceVectorBoundaryView& bc,
                                                            cfd::Index f, cfd::Real ux,
                                                            cfd::Real uy, cfd::Real uz,
                                                            cfd::Index component) {
  const cfd::Index kind = bc.kind[f];
  if (kind == kVectorBoundaryConstant) {
    return component == kVelocityU ? bc.constX[f]
                                   : (component == kVelocityV ? bc.constY[f] : bc.constZ[f]);
  }
  if (kind == kVectorBoundaryIdentity) {
    return component == kVelocityU ? ux : (component == kVelocityV ? uy : uz);
  }
  const cfd::Real nx = bc.normalX[f];
  const cfd::Real ny = bc.normalY[f];
  const cfd::Real nz = bc.normalZ[f];
  const cfd::Real normalComponent = convectionDotGuarded(ux, uy, uz, nx, ny, nz);
  if (component == kVelocityU) return ux - (nx * normalComponent);
  if (component == kVelocityV) return uy - (ny * normalComponent);
  return uz - (nz * normalComponent);
}

// The deferred-correction face value for one INTERNAL face, and the upwind
// value it is measured against. MomentumEquation.cpp:326-408.
__device__ inline cfd::Real deviceDeferredFaceValue(
    const DeviceConvectionGeometry& g, const cfd::Index* faceOwner, const cfd::Index* faceNeighbor,
    cfd::Index f, cfd::Real ownerFlux, cfd::Index scheme, cfd::Index component, const cfd::Real* ux,
    const cfd::Real* uy, const cfd::Real* uz, const cfd::Real* gx, const cfd::Real* gy,
    const cfd::Real* gz, cfd::Real& phiUpwindOut) {
  const cfd::Index owner = faceOwner[f];
  const cfd::Index neighbor = faceNeighbor[f];
  const cfd::Real* phi = component == kVelocityU ? ux : (component == kVelocityV ? uy : uz);
  const bool ownerIsUpwind = ownerFlux >= 0.0;
  const cfd::Index upwind = ownerIsUpwind ? owner : neighbor;
  const cfd::Index downwind = ownerIsUpwind ? neighbor : owner;
  const cfd::Real phiUpwind = phi[upwind];
  const cfd::Real phiDownwind = phi[downwind];
  phiUpwindOut = phiUpwind;
  if (scheme == kConvectionUpwind) return phiUpwind;

  const bool farValid = (ownerIsUpwind ? g.farOValid[f] : g.farNValid[f]) != 0;
  const cfd::Index farCell = ownerIsUpwind ? g.farOCell[f] : g.farNCell[f];
  const cfd::Real hCU = ownerIsUpwind ? g.farOHCU[f] : g.farNHCU[f];

  cfd::Real phiHighOrder = phiUpwind;
  if (scheme == kConvectionCentral) {
    // VECTOR interpolateInternalFace, then select the component.
    const cfd::Real dP = g.dPf[f];
    const cfd::Real dN = g.dNf[f];
    const cfd::Real inverse = 1.0 / (dP + dN);
    const cfd::Real fxv = ((ux[owner] * dN) + (ux[neighbor] * dP)) * inverse;
    const cfd::Real fyv = ((uy[owner] * dN) + (uy[neighbor] * dP)) * inverse;
    const cfd::Real fzv = ((uz[owner] * dN) + (uz[neighbor] * dP)) * inverse;
    phiHighOrder = component == kVelocityU ? fxv : (component == kVelocityV ? fyv : fzv);
  } else if (scheme == kConvectionLinearUpwind) {
    const cfd::Real ox = ownerIsUpwind ? g.offOX[f] : g.offNX[f];
    const cfd::Real oy = ownerIsUpwind ? g.offOY[f] : g.offNY[f];
    const cfd::Real oz = ownerIsUpwind ? g.offOZ[f] : g.offNZ[f];
    phiHighOrder = phiUpwind + convectionDotGuarded(gx[upwind], gy[upwind], gz[upwind], ox, oy, oz);
  } else if (scheme == kConvectionQUICK) {
    if (farValid) {
      const cfd::Real hUf = ownerIsUpwind ? g.dPf[f] : g.dNf[f];
      const cfd::Real hfD = ownerIsUpwind ? g.dNf[f] : g.dPf[f];
      phiHighOrder = deviceQuickFaceValue(phi[farCell], hCU, phiUpwind, hUf, phiDownwind, hfD);
    }
  }

  const cfd::Real r =
      farValid ? deviceSmoothnessRatio(phi[farCell], phiUpwind, phiDownwind) : nan("");
  const cfd::Real psi = deviceVanLeerLimiter(r, farValid);
  const cfd::Real blended = phiUpwind + (psi * (phiHighOrder - phiUpwind));
  const cfd::Real lo = phiUpwind < phiDownwind ? phiUpwind : phiDownwind;
  const cfd::Real hi = phiUpwind < phiDownwind ? phiDownwind : phiUpwind;
  return deviceClamp(blended, lo, hi);
}

#endif  // __CUDACC__

}  // namespace cfd::gpu
