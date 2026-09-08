#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::io {

// P1 -- Case System scope matches what the numerical core actually
// supports (TODO.md P1 section 7): 2D, rectangular, Cartesian only. type
// is always "rectangle" for now -- kept as a string (rather than an enum
// with one value) so a future geometry type is an additive parser change,
// not a struct-shape break.
struct GeometryConfig {
  std::string type;
  Real length{};
  Real height{};
};

}  // namespace cfd::io
