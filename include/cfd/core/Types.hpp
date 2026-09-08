#pragma once

#include <cstddef>

namespace cfd {

using Real = double;

// Unsigned by default: most topology/container sizes are naturally
// non-negative. Where a signed sentinel (e.g. "no such index") is needed,
// use SignedIndex explicitly at that call site rather than smuggling
// negative values into Index.
using Index = std::size_t;
using SignedIndex = std::ptrdiff_t;

// Vector2/Vector3 and other geometry types are deliberately not defined
// here -- they belong with the mesh layer that first needs them (see
// cfd/core/Vector2.hpp, added in P0 -- Mesh), keeping this header limited
// to scalar/index aliases only.

}  // namespace cfd
