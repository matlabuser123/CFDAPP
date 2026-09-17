#pragma once

#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::physics {

// Diagnostics for the incompressible mass-conservation constraint
// (div U = 0, or in face-flux form Sum_f F_f = 0 per cell) evaluated
// from an already-computed face mass flux -- see MassFlux.hpp. This
// phase only *evaluates* continuity; it is not yet the SIMPLE
// pressure-correction equation (see TODO.md P0 -- Incompressible
// Physics section 41).
struct ContinuityResult {
  // Per-cell net outward mass flux (owner-oriented: +F_f for the owner,
  // -F_f for the neighbor, +F_f again for a boundary face's owner).
  // Exactly zero for every cell iff the discrete velocity field is
  // divergence-free.
  cfd::fields::ScalarField cellImbalance;

  // Sum of boundary face mass flux only -- the *global* net mass
  // in/outflow of the domain. Can be ~0 (inlet balances outlet) while
  // individual cells are still badly imbalanced locally -- see
  // globalNetFlux vs totalAbsoluteImbalance in TODO.md section 20/64.
  Real globalNetFlux{};

  // Sum of |cellImbalance| over all cells -- a measure of *local*
  // continuity violation that globalNetFlux alone cannot detect.
  Real totalAbsoluteImbalance{};

  // max(|cellImbalance|) over all cells.
  Real maxCellImbalance{};
};

// Throws InvalidArgumentError if massFlux.size() != mesh.numberOfFaces().
[[nodiscard]] ContinuityResult evaluateContinuity(const cfd::mesh::Mesh& mesh,
                                                  const cfd::fields::SurfaceField& massFlux);

// P12-MESH-006: the global mass balance of a face mass flux (owner-oriented,
// so a boundary face's F_f > 0 leaves the domain), for the production
// diagnostics of a converged solve:
//   inflow   = sum over boundary faces with F_f < 0 of -F_f
//   outflow  = sum over boundary faces with F_f > 0 of  F_f
//   net      = sum over boundary faces of F_f (= outflow - inflow; the same
//              sum, in patch order, as ContinuityResult::globalNetFlux)
//   relativeImbalance = |net| / max(inflow, outflow)   (0 if both are 0)
//   maxCellImbalance, rmsCellImbalance: of ContinuityResult::cellImbalance
//   fluxScale = max(inflow, outflow, mean |F_f| over internal faces) -- the
//              scale of the flux field (a closed domain has no inflow)
//   normalizedContinuity = rmsCellImbalance / fluxScale (0 if fluxScale 0).
struct MassBalance {
  Real inflow{};
  Real outflow{};
  Real net{};
  Real relativeImbalance{};
  Real maxCellImbalance{};
  Real rmsCellImbalance{};
  Real fluxScale{};
  Real normalizedContinuity{};
};
// Throws InvalidArgumentError if massFlux.size() != mesh.numberOfFaces().
[[nodiscard]] MassBalance computeMassBalance(const cfd::mesh::Mesh& mesh,
                                             const cfd::fields::SurfaceField& massFlux);

}  // namespace cfd::physics
