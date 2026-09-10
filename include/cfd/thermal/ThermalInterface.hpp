#pragma once

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalRegionMap.hpp"

namespace cfd::thermal {

// P2-THERMAL-005: region-aware conduction assembly -- the minimum
// conjugate heat-transfer (CHT) foundation. Enforces temperature
// continuity and heat-flux continuity at a material interface via a
// single conservative face coefficient (see interfaceConductance below),
// not two separate one-sided conditions: a shared coefficient applied
// symmetrically to both rows (the same face-once assembly convention
// every other diffusion/convection contribution in this codebase already
// uses -- see EnergyEquation.hpp) makes flux continuity structural rather
// than a constraint that could numerically drift. Conduction only (no
// convection/mass flux) -- matches this foundation's own documented
// scope (TODO.md P2 -- Thermal): a solid region has no velocity, and the
// mandatory validation case is conduction-only everywhere, fluid or
// solid.

// Interface thermal conductance between two cells of a possibly-
// different material, via series thermal resistance:
//   R = d1/(k1*A) + d2/(k2*A)   =>   G = 1/R = A / (d1/k1 + d2/k2)
// d1/d2 are the distances from each cell's centroid to the shared face
// (not the full owner-neighbor distance) -- for equal conductivities
// k1==k2==k this reduces exactly to k*A/(d1+d2) = k*A/dPN, the plain
// single-material diffusion coefficient (d1+d2 == dPN exactly for an
// orthogonal structured-Cartesian face sitting on the owner-neighbor
// line -- MeshGeometry::ownerNeighborDistance's own definition), so this
// formula is a strict generalization, not a parallel/competing one.
// Deliberately NOT an arithmetic mean of k1/k2 (harmonic-mean-equivalent
// resistance series is the physically correct combination for conduction
// in series -- an arithmetic mean would not reduce correctly to either
// pure-material limit). Throws InvalidArgumentError if k1/k2/area are not
// finite and > 0, or d1/d2 are not finite and > 0.
[[nodiscard]] Real interfaceConductance(Real k1, Real d1, Real k2, Real d2, Real area);

// Diffusion contribution generalizing
// assembleThermalDiffusionContribution (EnergyEquation.hpp) to a
// per-cell-region conductivity: an interior face between two cells of
// the *same* region uses that region's plain k*Af/d coefficient
// (identical to the single-material path); a face between *different*
// regions uses interfaceConductance() instead. A boundary face always
// uses its owner cell's own region conductivity (there is no "other
// side" to take a resistance from). Throws InvalidArgumentError if
// temperature.size() != mesh.numberOfCells() or
// regions.numberOfCells() != mesh.numberOfCells().
void assembleRegionAwareThermalDiffusionContribution(
    const cfd::mesh::Mesh& mesh, const ThermalRegionMap& regions,
    const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// Combines assembleRegionAwareThermalDiffusionContribution with the
// existing (region-agnostic) assembleThermalSourceContribution into the
// full steady conduction equation for a multi-region domain:
//   -div(k(region) * grad(T)) = Q
// No convection term (see this file's own header comment) -- this is a
// standalone conduction assembly, not a drop-in replacement for
// assembleEnergyEquation (which remains the single-material,
// convection-aware path; this function is CHT-specific). Throws
// InvalidArgumentError if temperature/regions size does not match the
// mesh, or volumetricHeatSource is not finite. Throws NumericalError if
// the assembled system contains a non-finite value.
[[nodiscard]] EnergyAssembly assembleConjugateConductionEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const ThermalRegionMap& regions,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    Real volumetricHeatSource = 0.0);

}  // namespace cfd::thermal
