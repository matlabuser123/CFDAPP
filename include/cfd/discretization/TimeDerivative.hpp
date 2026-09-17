#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// Per-cell implicit Euler discretization of d(rho*phi)/dt for a cell-
// centered scalar field (TODO.md P2 sections 9-11): rho*V/dt*(phi^{n+1}
// - phi^n), split into a diagonal (matrix) contribution and an old-state
// source (RHS) contribution, the same split every other discretization-
// layer operator's caller expects (cf. MomentumEquation's
// assemble*Contribution family, which this deliberately does not depend
// on -- see TimeDerivative.hpp's own header comment on independence).
struct TimeDerivativeCoefficients {
  // aP,time = rho * V_P / dt, one entry per cell.
  cfd::fields::ScalarField diagonal;
  // b_time = aP,time * phiOld, one entry per cell -- *not* just phiOld
  // itself, so a caller can add this directly into an existing RHS
  // alongside the diagonal into an existing matrix, the same way
  // MomentumEquation's contributions compose (aP*phi_new = ... + b_time
  // once phi_new's own equation is assembled around it).
  cfd::fields::ScalarField source;
};

// Implicit Euler time-derivative coefficients (TODO.md P2 section 9-10).
// Deliberately independent of SIMPLE, PISO, and TransientSolver: knows
// only the mesh (for per-cell volume), the previous-time-level field,
// a density/coefficient, and dt -- not physical time itself (that stays
// TimeController's responsibility, TODO.md P2 section 2) and not how the
// result gets combined with any other equation term (that stays whatever
// assembles the full transient equation, e.g. a future momentum
// assembler). phiOld is read, never mutated.
//
// Throws InvalidArgumentError if:
//   - phiOld.size() != mesh.numberOfCells()
//   - dt is not finite, or dt <= 0
//   - density is not finite, or density <= 0
[[nodiscard]] TimeDerivativeCoefficients implicitEulerTimeDerivative(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phiOld, Real density, Real dt);

// P12-MESH-007 -- implicit Euler on a MOVING control volume (ALE):
// d(rho V phi)/dt ~ (rho V^{n+1} phi^{n+1} - rho V^n phi^n) / dt, with V^{n+1}
// the mesh's current cell volume and V^n = previousVolume (the volume before
// the mesh moved, MeshMotionStep::previousVolumes):
//   diagonal[P] = rho * V_P^{n+1} / dt
//   source[P]   = (rho * V_P^n / dt) * phiOld[P]
// The same operation order as implicitEulerTimeDerivative, so a mesh that did
// not move (previousVolume == the current volumes) gives its coefficients bit
// for bit. Consistent with the discrete geometric conservation law only when
// V^{n+1} - V^n equals the cells' swept volumes (MeshGeometry::sweptVolumes).
//
// Throws InvalidArgumentError as implicitEulerTimeDerivative, and if
// previousVolume.size() != mesh.numberOfCells() or a previous volume is not
// finite and > 0.
[[nodiscard]] TimeDerivativeCoefficients aleImplicitEulerTimeDerivative(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phiOld,
    const cfd::fields::ScalarField& previousVolume, Real density, Real dt);

}  // namespace cfd::discretization
