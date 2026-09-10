#pragma once

// P5-E -- Vector Plots, section 25: "every Nth cell" subsampling for
// arrow/vector-field rendering, kept as plain data preparation (no Qt,
// no drawing) -- see this header's own VectorSample::displayVector for
// the "visualization scale never overwrites the physical field" rule
// section 25 states explicitly.

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::viz {

struct VectorSample {
  Vector2 position;
  Vector2 vector;  // the real, physical field value -- never rescaled.

  // A caller/renderer computes display_length = visualizationScale *
  // |vector| itself (section 25's own formula) rather than this struct
  // ever storing a second, scaled copy of `vector` -- there is
  // deliberately no "scaledVector" member here to accidentally
  // overwrite or confuse with the physical one.
};

// Every `stride`-th cell (by cell id, stride >= 1 -- stride==1 samples
// every cell) paired with its own velocity. Throws
// cfd::InvalidArgumentError if stride < 1 or velocity.size() !=
// mesh.numberOfCells().
[[nodiscard]] std::vector<VectorSample> sampleVectorField(const cfd::mesh::Mesh& mesh,
                                                          const cfd::fields::VectorField& velocity,
                                                          Index stride);

// Raw-array variant (same rationale as FieldProbe.hpp's own
// probeScalarRaw/sampleLineRaw): every `stride`-th index (by position in
// the arrays, stride >= 1) of a plain parallel (points, velocityX,
// velocityY) triple -- for a caller with no cfd::mesh::Mesh at hand,
// e.g. a completed run reloaded from results/fields.csv. Throws
// cfd::InvalidArgumentError if stride < 1 or the three arrays' sizes
// disagree.
[[nodiscard]] std::vector<VectorSample> sampleVectorFieldRaw(const std::vector<Vector2>& points,
                                                             const std::vector<Real>& velocityX,
                                                             const std::vector<Real>& velocityY,
                                                             Index stride);

}  // namespace cfd::viz
