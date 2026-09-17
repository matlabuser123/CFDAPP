#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io {

// Uniform initial fields, applied to every cell (TODO.md P1 section 26).
// Explicit in case.json's optional "initial_conditions" object; defaults
// to zero velocity/pressure when absent -- a documented default, not a
// value hidden inside the CLI (section 26's "do not hide initial
// conditions inside the CLI").
struct InitialConditions {
  Vector2 velocity{};
  Real pressure{};
  // P12-MESH-006: how many velocity components case.json gave -- 2 ([u, v])
  // or 3 ([u, v, w]); CaseReader checks it against the geometry's dimension.
  // Absent block = the zero default, valid in both dimensions (0).
  int velocityComponents{0};
};

}  // namespace cfd::io
