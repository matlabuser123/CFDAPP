#pragma once

// P5-G -- Post-processing, sections 31-32: point inspection ("probe")
// and line sampling, both built on the same nearest-cell-centroid
// lookup -- a plain brute-force scan over cell centroids (this codebase
// currently has no spatial index; at the grid sizes P0-P4's own
// validation/benchmark suites exercise, a linear scan is fast enough for
// an interactive, infrequent operation like a probe click or a line
// sample, and the algorithm is correct for a mesh of any topology, not
// only the structured-Cartesian one this codebase currently generates --
// see this header's own tests for the (nearest-centroid, never the
// solver's own interpolation) contract). Read-only: neither function
// mutates the mesh or any field (section 31's own "the probe must read
// data; it must not modify solver state").

#include <optional>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::viz {

// The shared core nearestCell()/probeScalar()/sampleLine() below (mesh-
// based) and the *Raw variants further down (plain-array-based -- for a
// caller with no cfd::mesh::Mesh at hand, e.g. a completed run reloaded
// from results/fields.csv, section 9's own "without rerunning the
// solver") both reduce to: the index whose own coordinate is nearest
// `point` (Euclidean distance) -- std::nullopt only if `points` is
// empty. Ties (equidistant points) resolve to the lowest index, a
// fixed, deterministic rule.
[[nodiscard]] std::optional<Index> nearestIndex(const std::vector<Vector2>& points,
                                                const Vector2& point);

// The cell whose centroid is nearest `point` -- see nearestIndex() above
// for the tie-break rule this reduces to.
[[nodiscard]] std::optional<Index> nearestCell(const cfd::mesh::Mesh& mesh, const Vector2& point);

// One scalar field's value at the cell nearest `point` -- throws
// cfd::InvalidArgumentError if field.size() != mesh.numberOfCells(), or
// if mesh has zero cells.
[[nodiscard]] Real probeScalar(const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
                               const Vector2& point);

// One sample of a line-sampling result: the query point actually asked
// for (evenly spaced along the requested line, section 32), the nearest
// cell's own centroid (so a caller/CSV export can show how far the
// sample point is from the cell that answered it), and that cell's
// field value.
struct LineSample {
  Vector2 queryPoint;
  Vector2 cellCentroid;
  Real value{};
};

// `numberOfSamples` points evenly spaced from `start` to `end`
// inclusive (numberOfSamples >= 2; a horizontal/vertical/user-defined
// line is just any two endpoints -- section 32), each resolved via
// nearestCell(). Throws cfd::InvalidArgumentError if numberOfSamples < 2
// or field.size() != mesh.numberOfCells().
[[nodiscard]] std::vector<LineSample> sampleLine(const cfd::mesh::Mesh& mesh,
                                                 const cfd::fields::ScalarField& field,
                                                 const Vector2& start, const Vector2& end,
                                                 Index numberOfSamples);

// --- Raw-array variants (section 9: post-processing a completed run --
// reloaded results/fields.csv, no cfd::mesh::Mesh available) ------------
// Same nearest-point contract as the mesh-based overloads above, over a
// plain parallel (points, values) pair instead of a Mesh/ScalarField --
// e.g. GUI-side, `points`/`values` come straight from a
// cfd::app::VisualizationSnapshot's own per-cell coordinate/field
// arrays, live or reloaded alike, so probe/line-sampling behave
// identically either way. Throws cfd::InvalidArgumentError if
// points.size() != values.size(), or (sampleLineRaw only) if
// numberOfSamples < 2.

[[nodiscard]] Real probeScalarRaw(const std::vector<Vector2>& points,
                                  const std::vector<Real>& values, const Vector2& point);

[[nodiscard]] std::vector<LineSample> sampleLineRaw(const std::vector<Vector2>& points,
                                                    const std::vector<Real>& values,
                                                    const Vector2& start, const Vector2& end,
                                                    Index numberOfSamples);

}  // namespace cfd::viz
