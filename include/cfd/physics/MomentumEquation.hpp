#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/FluidProperties.hpp"

namespace cfd::physics {

// u and v are assembled as separate scalar systems (Au u = bu, Av v =
// bv), not one coupled 2N x 2N block system -- see TODO.md P0 --
// Incompressible Physics section 36. This selects which scalar component
// a contribution assembler is building for; velocity boundary conditions
// are inherently vector-valued (a Wall gives one (0,0), not two
// independent scalar conditions), so callers pass the full vector
// BoundaryConditionSet and this says which component to extract.
enum class VelocityComponent { U, V };

// Result of assembling one scalar momentum component. The diagonal
// (aP per cell) is exposed directly rather than requiring callers to
// rescan the matrix -- SIMPLE will need 1/aP-like quantities for
// pressure correction (TODO.md section 39).
struct MomentumAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

struct MomentumSystems {
  MomentumAssembly u;
  MomentumAssembly v;
};

// Each assembleXxxContribution below adds its term directly into a
// shared, caller-owned SparseMatrixBuilder + RHS Vector rather than
// returning its own standalone system -- this keeps convection,
// diffusion, and the pressure source independently testable (TODO.md
// section 9/31-32: contribution-level tests, without needing to disable
// a term by passing invalid physics like mu=0) while still letting
// assembleMomentum combine them into one equation. `velocity` is the
// current/lagged velocity iterate, needed only to evaluate boundary
// conditions whose value depends on the owner cell's state (Outlet,
// Symmetry) -- Wall/MovingWall/Inlet ignore it. This mirrors how the
// discretization layer's interpolateFace/diffusion/convection already
// evaluate boundary conditions against a current field value.

// mu * Af / d diffusion contribution, symmetric internal-face
// contribution (equal/opposite to owner and neighbor rows -- TODO.md
// section 10/45), Dirichlet-style boundary contribution added to the RHS.
// Throws InvalidArgumentError if velocity.size() != mesh.numberOfCells().
void assembleDiffusionContribution(const cfd::mesh::Mesh& mesh, Real dynamicViscosity,
                                   const cfd::fields::VectorField& velocity,
                                   const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component,
                                   cfd::algebra::SparseMatrixBuilder& builder,
                                   cfd::algebra::Vector& rhs);

// P2-TURB-003: same physics as the constant-viscosity overload above, but
// with a per-cell effective viscosity field (mu_eff = mu + mu_t, from
// cfd::turbulence::TurbulenceModel::effectiveViscosity()) instead of one
// scalar -- this is the site that generalizes to support a turbulence
// model without any RANS-specific formula living here. Internal faces use
// the project's already-established distance-weighted linear face
// interpolation (cfd::discretization::interpolateInternalFace) to get
// mu_eff at the face, the same convention every other cell-centered field
// is already interpolated to a face with -- no new averaging scheme is
// invented for this. A boundary face has no neighbor cell to interpolate
// against, so it uses the owner cell's own effective viscosity directly,
// matching how the scalar overload already applies one value uniformly
// including at boundaries.
//
// The scalar overload above is intentionally left as a separate,
// untouched code path (not reimplemented in terms of this one) -- callers
// that only ever pass a single constant viscosity keep their existing,
// exact floating-point behavior. Throws InvalidArgumentError if
// velocity.size() or effectiveViscosity.size() != mesh.numberOfCells().
void assembleDiffusionContribution(const cfd::mesh::Mesh& mesh,
                                   const cfd::fields::ScalarField& effectiveViscosity,
                                   const cfd::fields::VectorField& velocity,
                                   const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component,
                                   cfd::algebra::SparseMatrixBuilder& builder,
                                   cfd::algebra::Vector& rhs);

// First-order upwind convection contribution using the already-computed
// face mass flux (see MassFlux.hpp) -- TODO.md section 11/25/53. Throws
// InvalidArgumentError if velocity.size() != mesh.numberOfCells() or
// massFlux.size() != mesh.numberOfFaces().
void assembleConvectionContribution(const cfd::mesh::Mesh& mesh,
                                    const cfd::fields::SurfaceField& massFlux,
                                    const cfd::fields::VectorField& velocity,
                                    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                    VelocityComponent component,
                                    cfd::algebra::SparseMatrixBuilder& builder,
                                    cfd::algebra::Vector& rhs);

// -V_P * (dp/dx or dp/dy)_P, using the verified Gauss gradient operator
// (TODO.md section 26-29). Purely a source: adds to rhs only, no matrix
// contribution (pressure is not an unknown of this equation). Throws
// InvalidArgumentError if pressure.size() != mesh.numberOfCells().
void assemblePressureSourceContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& pressure,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries, VelocityComponent component,
    cfd::algebra::Vector& rhs);

// P3-PHYS-001: Boussinesq buoyancy body-force contribution -- a pure
// source, same shape as assemblePressureSourceContribution above (adds
// to rhs only, no matrix/diagonal contribution -- temperature is not an
// unknown of this equation). For each cell, `buoyancy.source(T)` (force
// per unit volume -- see BoussinesqBuoyancy.hpp's own header comment for
// the full derivation and sign convention) is integrated over the cell's
// volume, the same "per-volume source times cellVolume" convention
// assemblePressureSourceContribution already establishes for
// -V_P*grad(p) (verified directly from that existing contribution's own
// implementation, not assumed -- P3-PHYS-001 Phase 3's own explicit
// instruction). Throws InvalidArgumentError if temperature.size() !=
// mesh.numberOfCells().
void assembleBuoyancySourceContribution(const cfd::mesh::Mesh& mesh,
                                        const cfd::fields::ScalarField& temperature,
                                        const BoussinesqBuoyancy& buoyancy,
                                        VelocityComponent component, cfd::algebra::Vector& rhs);

// Combines the three contributions above into the full u- and v-momentum
// systems for steady, incompressible, constant-property, laminar flow:
//   rho (U.grad)u - mu grad^2 u = -dp/dx
//   rho (U.grad)v - mu grad^2 v = -dp/dy
// massFlux must already carry rho (see calculateMassFlux) -- fluid is
// used here only for dynamicViscosity. No SIMPLE, no under-relaxation,
// no pressure-correction equation -- see TODO.md section 40-41.
//
// Throws InvalidArgumentError if velocity/pressure size does not match
// mesh.numberOfCells(), or massFlux size does not match
// mesh.numberOfFaces(). Throws NumericalError if the assembled system
// (matrix or RHS) contains a non-finite value.
[[nodiscard]] MomentumSystems assembleMomentum(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const FluidProperties& fluid, const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

}  // namespace cfd::physics
