#pragma once

#include "cfd/core/Vector3.hpp"

namespace cfd {

// P12-MESH-005: the former two-component geometry vector is now the
// three-component Vector3 (see Vector3.hpp). The name is kept as an alias
// so that every existing 2D caller compiles and behaves unchanged: a
// `Vector2{x, y}` is a Vector3 with z = 0, and every operation on such
// values returns exactly the two-component result it returned before.
using Vector2 = Vector3;

}  // namespace cfd
