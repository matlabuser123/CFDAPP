#pragma once

// P5-D -- Contours (TODO.md P5 section 21-23): deterministic marching-
// squares contour extraction, kept as pure math over a plain structured
// grid of samples -- no cfd::mesh::Mesh, no Qt, no rendering (section
// 22's own "separate contour extraction from Qt rendering"). This is
// what makes tests/unit/viz/test_marching_squares.cpp's own "phi(x,y) =
// x, contour at 0.5 should be a vertical line" test (the task's own
// worked example) possible: a synthetic grid with a known analytical
// answer, not a screenshot.
//
// The nx*ny `values`/`coordinates` grid is interpreted as a lattice of
// marching-squares *vertices* -- for this codebase's cell-centered
// fields, callers pass cell-center values/coordinates directly (a
// standard, common treatment for cell-centered contour plotting, the
// same one e.g. matplotlib's contour() applies to a value grid); this
// header does not itself know or care whether its input came from cell
// centers, cell corners, or any other sampling.

#include <vector>

#include "cfd/core/Types.hpp"

namespace cfd::viz {

struct GridPoint {
  Real x{};
  Real y{};
};

struct ContourSegment {
  GridPoint start;
  GridPoint end;
};

// `values[j*nx+i]`/`coordinates[j*nx+i]` (row-major, i fastest -- the
// same "cell id = j*nx+i" convention cfd::mesh::Mesh::createCartesian2D,
// VTKWriter, and every structured-grid test in this codebase already
// use). Throws cfd::InvalidArgumentError if nx<2, ny<2, or either vector
// does not have exactly nx*ny elements.
//
// A marching-squares cell (i,j)-(i+1,j)-(i+1,j+1)-(i,j+1) contributes no
// segment if any of its 4 corner values is non-finite (NaN/Inf never
// silently treated as "below" or "above" level -- section 23). The
// classic saddle-case ambiguity (diagonal corners on opposite sides of
// `level`, the other diagonal also opposite) is resolved by comparing
// the cell's own average corner value to `level` -- a fixed, documented,
// deterministic tie-break (section 23's "avoid unstable topology from
// floating-point equality"), not left to iteration-order chance.
//
// Segments are emitted in row-major cell order (j outer, i inner), a
// fixed order within a cell -- repeated calls on the same input produce
// byte-identical output (tested directly).
[[nodiscard]] std::vector<ContourSegment> extractContourSegments(
    Index nx, Index ny, const std::vector<Real>& values, const std::vector<GridPoint>& coordinates,
    Real level);

// `numberOfLevels` values strictly between the finite min and max of
// `values` (never exactly at either endpoint, where a contour would
// degenerate to the field's own boundary or vanish entirely), evenly
// spaced. Returns an empty vector if `values` has no finite samples, or
// if min==max (a constant field -- section 23 -- has no meaningful
// contour level). Non-finite samples are ignored when computing min/max
// (never propagate a NaN level).
[[nodiscard]] std::vector<Real> automaticContourLevels(const std::vector<Real>& values,
                                                       Index numberOfLevels);

}  // namespace cfd::viz
