#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::solver {

// TODO.md P2 sections 25-27: diagnostic only -- this does not modify dt or
// suggest a value, it only reports what the Courant number *is* for a
// given (mesh, flux, dt). No adaptive time stepping here or implied by
// this API.
struct CFLResult {
  Real maxCFL;
  Real meanCFL;      // volume-weighted mean, matching every other
                     // aggregated per-cell statistic in this codebase
                     // (e.g. ManufacturedFields.hpp's l2CellError) --
                     // equal to the plain arithmetic mean on the uniform
                     // meshes this project generates today, but the
                     // physically meaningful choice once a non-uniform
                     // mesh exists.
  Index maxCFLCell;  // id of the cell attaining maxCFL (first cell found
                     // at a tie, since mesh.cells() iterates in a fixed,
                     // deterministic id order -- see CFL.cpp).
};

// Per-cell Courant number, from the *authoritative* face mass flux (the
// same field SIMPLE's continuity/momentum already use -- TODO.md section
// 25: "a finite-volume CFL can be estimated from face volumetric
// fluxes"), not a separately re-interpolated face velocity.
//
// Convention (documented here, not just in the .cpp, since it is the
// part every caller and every test must agree on): for cell P,
//
//   Co_P = (dt / (2 * V_P)) * sum_over_faces_of_P( |massFlux_f| / rho )
//
// i.e. half the sum of the *absolute* volumetric flux magnitude through
// every one of P's faces (not just outgoing ones), scaled by dt/V_P. The
// 1/2 factor is what keeps this equal to the simple 1D convention
// Co = U*dt/dx for a uniform flow (see CFL.cpp's derivation and
// test_cfl.cpp's U=1/dx=0.1/dt=0.02 -> Co~=0.2 check): for a locally
// mass-conserving cell, the sum of outgoing face fluxes equals the sum of
// incoming ones, so half the *total* absolute flux equals the outgoing
// (or incoming) total alone -- but computing it as half the total,
// rather than classifying each face's flux sign as "outgoing", is
// well-defined even for a not-yet-mass-conserving intermediate field
// (e.g. a SIMPLE outer iteration's predictor flux, or PISO's between
// pressure corrections), where "outgoing vs incoming" would otherwise
// need its own convention.
//
// Throws InvalidArgumentError if:
//   - faceMassFlux.size() != mesh.numberOfFaces()
//   - dt is not finite, or dt <= 0
//   - density is not finite, or density <= 0
[[nodiscard]] CFLResult calculateCFL(const cfd::mesh::Mesh& mesh,
                                     const cfd::fields::SurfaceField& faceMassFlux, Real density,
                                     Real dt);

}  // namespace cfd::solver
