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

}  // namespace cfd::physics
