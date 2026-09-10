#pragma once

// P5-G -- Post-processing, section 29-30: derived fields and basic field
// statistics computed from already-converged solver results, never a
// second copy of the solver's own physics -- velocity magnitude is a
// pointwise formula, vorticity is a plain post-hoc Green-Gauss gradient
// of the *result* velocity field (not a transport equation, not fed
// back into anything).

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::viz {

// Section 29's own "field min/max, field average" minimum
// post-processing operations. NaN/Inf values are ignored (never
// propagate a non-finite min/max/average from a single bad cell); all
// three are 0 for an empty or all-non-finite field, with hasData==false
// signaling that case (not a silently misleading 0.0).
struct FieldStatistics {
  Real minimum{};
  Real maximum{};
  Real average{};
  bool hasData{false};
};

[[nodiscard]] FieldStatistics computeFieldStatistics(const cfd::fields::ScalarField& field);

// |U| at every cell -- a pointwise formula, no mesh needed.
[[nodiscard]] cfd::fields::ScalarField velocityMagnitude(const cfd::fields::VectorField& velocity);

// 2D vorticity omega_z = dv/dx - du/dy at every cell, via a Green-Gauss
// cell gradient (grad(phi)_c = (1/Vol_c) * sum_faces phi_face *
// areaVector_face) of the *given* (already-converged) velocity field --
// an interior face's phi_face is the simple average of its two
// neighboring cells; a boundary face's phi_face is its owner cell's own
// value (a zero-gradient/Neumann approximation at the boundary -- a
// deliberate, documented post-processing simplification, not this
// codebase's own momentum-equation boundary treatment, since a generic
// derived-field diagnostic has no boundary-condition-type information
// to draw on the way MomentumEquation's own boundary handling does).
// Accuracy is therefore best in the interior and degrades near
// boundaries -- see tests/unit/viz/test_derived_fields.cpp's own
// interior-vs-boundary tolerance split for a directly-measured
// demonstration on a known analytical field (solid-body rotation).
//
// Throws cfd::InvalidArgumentError if velocity.size() != mesh.numberOfCells().
[[nodiscard]] cfd::fields::ScalarField vorticity2D(const cfd::mesh::Mesh& mesh,
                                                   const cfd::fields::VectorField& velocity);

}  // namespace cfd::viz
