#pragma once

#include <array>
#include <string_view>

namespace cfd::io {

// The exact turbulence-model vocabulary PhysicsConfigParser.cpp enforces
// at parse time and cfd::turbulence::createTurbulenceModel() actually
// constructs -- hoisted out for the same "GUI dropdown, never a second
// hand-typed copy" reason as BoundaryVocabulary.hpp's own header comment
// (P7-GUI-002).
inline constexpr std::array<std::string_view, 4> kTurbulenceModels{"laminar", "k_epsilon",
                                                                   "k_omega", "sst"};

}  // namespace cfd::io
