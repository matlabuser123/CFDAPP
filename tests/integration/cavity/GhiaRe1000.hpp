#pragma once

#include <array>

#include "GhiaRe100.hpp"

// Ghia, U., Ghia, K. N., Shin, C. T. (1982). High-Re Solutions for
// Incompressible Flow Using the Navier-Stokes Equations and a Multigrid
// Method. Journal of Computational Physics, 48(3), 387-411.
// DOI: 10.1016/0021-9991(82)90058-4
//
// Table I (u along x=0.5) and Table II (v along y=0.5), Reynolds number
// 1000, same cavity / sample locations as GhiaRe100.hpp. Provenance and
// the cross-check of three independent transcriptions (including the one
// resolved discrepancy, v(0.9063) = -0.51550) are documented in
// validation/ghia/README.md; the citable copy is
// validation/ghia/ghia_re1000_{u,v}.csv.
namespace cfd::validation::ghia_re1000 {

using Sample = ghia_re100::Sample;

// u(0.5, y), ordered from the bottom wall (y=0) to the moving lid (y=1).
inline constexpr std::array<Sample, 17> kCenterlineU = {{
    {0.0000, 0.00000},
    {0.0547, -0.18109},
    {0.0625, -0.20196},
    {0.0703, -0.22220},
    {0.1016, -0.29730},
    {0.1719, -0.38289},
    {0.2813, -0.27805},
    {0.4531, -0.10648},
    {0.5000, -0.06080},
    {0.6172, 0.05702},
    {0.7344, 0.18719},
    {0.8516, 0.33304},
    {0.9531, 0.46604},
    {0.9609, 0.51117},
    {0.9688, 0.57492},
    {0.9766, 0.65928},
    {1.0000, 1.00000},
}};

// v(x, 0.5), ordered from the left wall (x=0) to the right wall (x=1).
inline constexpr std::array<Sample, 17> kCenterlineV = {{
    {0.0000, 0.00000},
    {0.0625, 0.27485},
    {0.0703, 0.29012},
    {0.0781, 0.30353},
    {0.0938, 0.32627},
    {0.1563, 0.37095},
    {0.2266, 0.33075},
    {0.2344, 0.32235},
    {0.5000, 0.02526},
    {0.8047, -0.31966},
    {0.8594, -0.42665},
    {0.9063, -0.51550},
    {0.9453, -0.39188},
    {0.9531, -0.33714},
    {0.9609, -0.27669},
    {0.9688, -0.21388},
    {1.0000, 0.00000},
}};

}  // namespace cfd::validation::ghia_re1000
