#pragma once

#include "cfd/core/Types.hpp"

// de Vahl Davis, G. (1983). Natural convection of air in a square
// cavity: A bench mark numerical solution. International Journal for
// Numerical Methods in Fluids, 3(3), 249-264. Cross-checked against Wan,
// Patnaik & Wei (2001), Numerical Heat Transfer B 40(3), 199-228,
// Tables 2/3/5 (column "Ref. [3]", which reproduces de Vahl Davis's own
// values).
//
// Single source of truth for the P3-PHYS-002 differentially-heated-
// cavity benchmark, same "hardcoded constexpr, validation/ carries a
// documented human-readable copy" convention as GhiaRe100.hpp/
// ChannelReTau180.hpp -- see
// validation/literature/natural_convection/README.md and
// de_vahl_davis_1983.json (must be kept numerically in sync with this
// file).
namespace cfd::validation::de_vahl_davis_1983 {

inline constexpr cfd::Real kPrandtlNumber = 0.71;

struct BenchmarkCase {
  cfd::Real ra;
  cfd::Real nuAvg;
  cfd::Real nuMax;
  cfd::Real nuMaxY;
  cfd::Real nuMin;
  cfd::Real nuMinY;
  cfd::Real uMax;
  cfd::Real uMaxY;
  cfd::Real vMax;
  cfd::Real vMaxX;
};

inline constexpr BenchmarkCase kRa1e3{1.0e3, 1.12,  1.50,  0.092, 0.692, 1.0,
                                      3.634, 0.813, 3.679, 0.179};
inline constexpr BenchmarkCase kRa1e4{1.0e4, 2.243, 3.53,  0.143, 0.586, 1.0,
                                      16.2,  0.823, 19.51, 0.12};
inline constexpr BenchmarkCase kRa1e5{1.0e5, 4.52,  7.71,  0.08, 0.729, 1.0,
                                      34.81, 0.855, 68.22, 0.066};
inline constexpr BenchmarkCase kRa1e6{1.0e6, 8.8,   17.92, 0.038, 0.989, 1.0,
                                      65.33, 0.851, 216.75, 0.0387};

}  // namespace cfd::validation::de_vahl_davis_1983
