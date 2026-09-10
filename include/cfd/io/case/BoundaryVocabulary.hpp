#pragma once

#include <array>
#include <string_view>

namespace cfd::io {

// The exact per-field boundary-condition type vocabulary
// BoundaryConfigParser.cpp enforces at parse time and CaseBuilder.cpp's
// own buildVelocityBoundary/buildPressureBoundary/etc. switch on --
// hoisted out of BoundaryConfigParser.cpp's anonymous namespace (P7-GUI-003)
// so the GUI's boundary-condition editor can populate its type dropdowns
// from this exact same list rather than a second, hand-typed copy that
// could silently drift from what the parser/builder actually accept.
// This is vocabulary only (what strings exist), never validation logic
// (whether a given case is valid) -- see CaseWriter.cpp's own
// "written only when non-empty"-style comments for the parallel
// precedent of one field shared, not reinvented, on the write side.
inline constexpr std::array<std::string_view, 5> kVelocityTypes{"wall", "moving_wall", "inlet",
                                                                "outlet", "symmetry"};
// Only these two velocity types are a prescribed vector, so only they
// take a "value" array.
inline constexpr std::array<std::string_view, 2> kVelocityTypesWithValue{"moving_wall", "inlet"};
inline constexpr std::array<std::string_view, 2> kPressureTypes{"fixed_value", "fixed_gradient"};
// Species concentration (ConcentrationBoundarySpec) and multiphase alpha
// (AlphaBoundarySpec) both reuse kPressureTypes directly -- same
// always-required-value fixed_value/fixed_gradient shape, see their own
// header comments in BoundaryConfig.hpp.
inline constexpr std::array<std::string_view, 3> kTemperatureTypes{"fixed_temperature", "heat_flux",
                                                                   "adiabatic"};
// Only fixed_temperature/heat_flux take a "value" -- adiabatic has none.
inline constexpr std::array<std::string_view, 2> kTemperatureTypesWithValue{"fixed_temperature",
                                                                            "heat_flux"};

}  // namespace cfd::io
