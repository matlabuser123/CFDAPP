#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/Field.hpp"

namespace cfd::fields {

// One Real value per mesh face: mass flux, face velocity, interpolated
// pressure, face diffusivity, ... -- as opposed to ScalarField/
// VectorField, which store one value per cell. Scalar-only for now: this
// is enough for SIMPLE's mass flux. A templated SurfaceFieldT<T> (with a
// VectorSurfaceField alias) can be introduced later if a face-centered
// vector quantity is actually needed.
class SurfaceField : public Field<Real> {
 public:
  using Field<Real>::Field;
};

}  // namespace cfd::fields
