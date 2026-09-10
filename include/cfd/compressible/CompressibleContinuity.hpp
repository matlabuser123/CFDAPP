#pragma once

#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::compressible {

// P3-PHYS-006: the transient compressible continuity diagnostic (section
// 10) --
//   (rho_P^{n+1} - rho_P^n) * V_P / dt + sum_faces(mDot_f) = 0
// evaluated *per cell* from an already-computed compressible mass flux
// (calculateCompressibleMassFlux) plus the old/new density fields --
// this is a genuine generalization of
// physics::ContinuityEquation.hpp's own steady-only
// evaluateContinuity(mesh, massFlux) (cellImbalance = sum_faces(F_f)),
// not a replacement for it: the *steady* compressible mass-balance check
// (section 10's own "For steady flow: Sum_faces mDot_f = 0") is exactly
// physics::evaluateContinuity fed this module's own
// calculateCompressibleMassFlux output directly -- no new function is
// needed for that case (this is the "one authoritative mass-flux path"
// section 17 requires: continuity, mass-conservation reporting, and any
// future pressure-correction step must all consume the *same*
// calculateCompressibleMassFlux result, never a second, independently
// computed one).
struct CompressibleContinuityResult {
  // Per-cell residual of the transient equation above -- exactly zero
  // (up to floating-point roundoff) iff densityNew is *exactly* the
  // density this compressible continuity equation predicts from
  // densityOld and massFlux; nonzero if densityNew came from an
  // independent source (e.g. a fresh EOS evaluation at a newly solved
  // pressure/temperature) not yet reconciled with the flux field --
  // exactly the diagnostic section 43's own "density consistency" gate
  // asks for.
  cfd::fields::ScalarField cellImbalance;
  Real totalAbsoluteImbalance{};
  Real maxCellImbalance{};

  // M(t) = Sum_cells(rho_P * V_P) at the old and new time levels
  // (section 28's own "for transient closed domains, M(t) must change
  // only consistently with boundary mass flux").
  Real totalMassOld{};
  Real totalMassNew{};
};

// Throws InvalidArgumentError if densityOld/densityNew.size() !=
// mesh.numberOfCells(), massFlux.size() != mesh.numberOfFaces(), or dt
// is not finite or <= 0.
[[nodiscard]] CompressibleContinuityResult evaluateCompressibleContinuity(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& densityOld,
    const cfd::fields::ScalarField& densityNew, const cfd::fields::SurfaceField& massFlux, Real dt);

}  // namespace cfd::compressible
