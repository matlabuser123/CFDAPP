#pragma once

#include <array>

#include "cfd/core/Types.hpp"

// Ghia, U., Ghia, K. N., Shin, C. T. (1982). High-Re Solutions for
// Incompressible Flow Using the Navier-Stokes Equations and a Multigrid
// Method. Journal of Computational Physics, 48(3), 387-411.
// DOI: 10.1016/0021-9991(82)90058-4
//
// Table I (u along the vertical centerline x=0.5) and Table II (v along
// the horizontal centerline y=0.5), Reynolds number 100, unit square
// cavity with the moving lid on y=1 carrying tangential velocity (1,0).
// This is the single source of truth used by the validation tests; a
// human-readable copy with full provenance notes lives at
// validation/ghia/ghia_re100_{u,v}.csv -- see validation/ghia/README.md.
namespace cfd::validation::ghia_re100 {

struct Sample {
  cfd::Real coordinate;
  cfd::Real value;
};

// u(0.5, y), ordered from the bottom wall (y=0) to the moving lid (y=1).
inline constexpr std::array<Sample, 17> kCenterlineU = {{
    {0.0000, 0.00000},
    {0.0547, -0.03717},
    {0.0625, -0.04192},
    {0.0703, -0.04775},
    {0.1016, -0.06434},
    {0.1719, -0.10150},
    {0.2813, -0.15662},
    {0.4531, -0.21090},
    {0.5000, -0.20581},
    {0.6172, -0.13641},
    {0.7344, 0.00332},
    {0.8516, 0.23151},
    {0.9531, 0.68717},
    {0.9609, 0.73722},
    {0.9688, 0.78871},
    {0.9766, 0.84123},
    {1.0000, 1.00000},
}};

// v(x, 0.5), ordered from the left wall (x=0) to the right wall (x=1).
inline constexpr std::array<Sample, 17> kCenterlineV = {{
    {0.0000, 0.00000},
    {0.0625, 0.09233},
    {0.0703, 0.10091},
    {0.0781, 0.10890},
    {0.0938, 0.12317},
    {0.1563, 0.16077},
    {0.2266, 0.17507},
    {0.2344, 0.17527},
    {0.5000, 0.05454},
    {0.8047, -0.24533},
    {0.8594, -0.22445},
    {0.9063, -0.16914},
    {0.9453, -0.10313},
    {0.9531, -0.08864},
    {0.9609, -0.07391},
    {0.9688, -0.05906},
    {1.0000, 0.00000},
}};

}  // namespace cfd::validation::ghia_re100
