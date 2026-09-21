#pragma once

// GPU-DISC-001F -- the shared device implementation of the diffusion face terms.
//
// Extracted from DeviceDiffusionKernel.cu when the momentum assembly needed the
// SAME face terms with two substitutions: the boundary value comes from a
// vector condition instead of a scalar one, and the gradient is a component of
// the velocity gradient instead of the scalar gradient. Everything else -- the
// geometry, the coefficients, the P12-DIFF-002 three-point reconstruction, the
// fallbacks -- is identical, so it lives here once rather than in two copies.
//
// This mirrors cfd::discretization::NonOrthogonalDiffusion, which is itself "the
// ONE implementation of the non-orthogonal correction for every IMPLICIT
// diffusion term in the code". Duplicating it on the device would undo exactly
// the property that header exists to guarantee.
//
// 001C's 528-case bitwise differential is re-run after the extraction to show
// the move changed nothing.
//
// Compile any translation unit including this with -fmad=false: the gate is
// bitwise equality with the CPU.

#include "cfd/core/Types.hpp"

namespace cfd::gpu {

// What one face contributes, matching cfd::discretization::FaceDiffusionTerms.
struct DeviceFaceDiffusionTerms {
  cfd::Real coefficient;
  cfd::Real explicitFlux;
  cfd::Real boundaryValueCoefficient;
  cfd::Real farCellCoefficient;
  cfd::Index farCell;
};

// The immutable per-face geometry both callers precompute, from the same
// production MeshGeometry functions.
struct DeviceDiffusionGeometry {
  const cfd::Real* faceArea;
  const cfd::Real* dPf;
  const cfd::Real* dNf;
  const cfd::Real* dPN;
  // decomposeFaceArea, internal faces.
  const cfd::Index* decompValid;
  const cfd::Real* orthMag;
  const cfd::Real* nonOrthX;
  const cfd::Real* nonOrthY;
  const cfd::Real* nonOrthZ;
  // Boundary faces.
  const cfd::Real* bDistance;
  const cfd::Index* bPrescribed;
  const cfd::Index* bStencilValid;
  const cfd::Index* bFarCell;
  const cfd::Real* bCP;
  const cfd::Real* bCF;
  const cfd::Real* bCB;
  const cfd::Real* bDeltaPX;
  const cfd::Real* bDeltaPY;
  const cfd::Real* bDeltaPZ;
  const cfd::Real* bDeltaFX;
  const cfd::Real* bDeltaFY;
  const cfd::Real* bDeltaFZ;
  // decomposeBoundaryFaceArea, the two-point non-orthogonal fallback.
  const cfd::Index* bDecompValid;
  const cfd::Real* bOrthMag;
  const cfd::Real* bNonOrthX;
  const cfd::Real* bNonOrthY;
  const cfd::Real* bNonOrthZ;
};

#if defined(__CUDACC__)

// cfd::dot (Vector3.hpp:78): the z term is dropped when it is exactly zero.
__device__ inline cfd::Real diffusionDotGuarded(cfd::Real ax, cfd::Real ay, cfd::Real az,
                                                cfd::Real bx, cfd::Real by, cfd::Real bz) {
  const cfd::Real inPlane = (ax * bx) + (ay * by);
  const cfd::Real normal = az * bz;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

// NonOrthogonalDiffusion.cpp:51. `correct` is the CPU's `gradPhi != nullptr`
// for internal faces, i.e. NonOrthogonalCorrectionOptions::enabled.
//
// `gamma` is the per-cell diffusivity/viscosity field; `gx/gy/gz` the per-cell
// gradient of the transported scalar (the scalar gradient for diffusion, the
// velocity-component gradient for momentum).
__device__ inline DeviceFaceDiffusionTerms deviceInternalFaceDiffusionTerms(
    const DeviceDiffusionGeometry& g, const cfd::Index* faceOwner, const cfd::Index* faceNeighbor,
    const cfd::Real* gamma, const cfd::Real* gx, const cfd::Real* gy, const cfd::Real* gz,
    cfd::Index f, bool correct) {
  const cfd::Index o = faceOwner[f];
  const cfd::Index n = faceNeighbor[f];
  const cfd::Real dP = g.dPf[f];
  const cfd::Real dN = g.dNf[f];
  const cfd::Real distance = g.dPN[f];
  // Scalar interpolateInternalFace: ((dNf*phiP) + (dPf*phiN)) / (dPf + dNf)
  const cfd::Real gammaFace = ((dN * gamma[o]) + (dP * gamma[n])) / (dP + dN);

  DeviceFaceDiffusionTerms terms;
  if (correct && g.decompValid[f] != 0) {
    // VECTOR interpolateInternalFace: ((uP*dNf) + (uN*dPf)) * (1.0/(dPf + dNf))
    const cfd::Real inverse = 1.0 / (dP + dN);
    const cfd::Real fx = ((gx[o] * dN) + (gx[n] * dP)) * inverse;
    const cfd::Real fy = ((gy[o] * dN) + (gy[n] * dP)) * inverse;
    const cfd::Real fz = ((gz[o] * dN) + (gz[n] * dP)) * inverse;
    terms.coefficient = gammaFace * g.orthMag[f] / distance;
    terms.explicitFlux =
        gammaFace * diffusionDotGuarded(g.nonOrthX[f], g.nonOrthY[f], g.nonOrthZ[f], fx, fy, fz);
  } else {
    terms.coefficient = gammaFace * g.faceArea[f] / distance;
    terms.explicitFlux = 0.0;
  }
  terms.boundaryValueCoefficient = terms.coefficient;
  terms.farCellCoefficient = 0.0;
  terms.farCell = 0;
  return terms;
}

// NonOrthogonalDiffusion.cpp:64. The gradient is ALWAYS available here
// (P12-DIFF-002 A2), so the only gate is whether the condition prescribes the
// value. `gammaFace` is the OWNER's value, not interpolated.
__device__ inline DeviceFaceDiffusionTerms deviceBoundaryFaceDiffusionTerms(
    const DeviceDiffusionGeometry& g, const cfd::Index* faceOwner, const cfd::Real* gamma,
    const cfd::Real* gx, const cfd::Real* gy, const cfd::Real* gz, cfd::Index f) {
  const cfd::Index o = faceOwner[f];
  const cfd::Real gammaFace = gamma[o];
  const cfd::Real distance = g.bDistance[f];
  DeviceFaceDiffusionTerms terms;

  if (g.bPrescribed[f] != 0) {
    if (g.bStencilValid[f] != 0) {
      const cfd::Real gammaArea = gammaFace * g.faceArea[f];
      const cfd::Real cP = g.bCP[f];
      const cfd::Real cF = g.bCF[f];
      const cfd::Index far = g.bFarCell[f];
      terms.coefficient = gammaArea * cP;
      terms.farCellCoefficient = gammaArea * cF;
      terms.farCell = far;
      terms.boundaryValueCoefficient = gammaArea * g.bCB[f];
      terms.explicitFlux =
          gammaArea * ((cP * diffusionDotGuarded(gx[o], gy[o], gz[o], g.bDeltaPX[f], g.bDeltaPY[f],
                                                 g.bDeltaPZ[f])) -
                       (cF * diffusionDotGuarded(gx[far], gy[far], gz[far], g.bDeltaFX[f],
                                                 g.bDeltaFY[f], g.bDeltaFZ[f])));
      return terms;
    }
    if (g.bDecompValid[f] != 0) {
      terms.coefficient = gammaFace * g.bOrthMag[f] / distance;
      terms.explicitFlux = gammaFace * diffusionDotGuarded(g.bNonOrthX[f], g.bNonOrthY[f],
                                                           g.bNonOrthZ[f], gx[o], gy[o], gz[o]);
      terms.boundaryValueCoefficient = terms.coefficient;
      terms.farCellCoefficient = 0.0;
      terms.farCell = 0;
      return terms;
    }
  }
  terms.coefficient = gammaFace * g.faceArea[f] / distance;
  terms.explicitFlux = 0.0;
  terms.boundaryValueCoefficient = terms.coefficient;
  terms.farCellCoefficient = 0.0;
  terms.farCell = 0;
  return terms;
}

#endif  // __CUDACC__

}  // namespace cfd::gpu
