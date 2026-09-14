# Ghia, Ghia & Shin (1982) Re=100 Lid-Driven Cavity Reference Data

Source:

```
Ghia, U., Ghia, K. N., Shin, C. T. (1982)
High-Re Solutions for Incompressible Flow Using the Navier-Stokes
Equations and a Multigrid Method
Journal of Computational Physics, 48(3), 387-411
DOI: 10.1016/0021-9991(82)90058-4
```

Table I of the paper (u along the vertical centerline x=0.5) and Table II
(v along the horizontal centerline y=0.5), Reynolds number 100, extracted
from the paper's 129x129 fine-grid solution.

Coordinate convention: unit square cavity, x and y in [0, 1], moving lid on
the top boundary (y=1) with tangential velocity (1, 0), all other walls
stationary. This matches this project's cavity configuration exactly
(TODO.md "P0 -- Physical Validation" section 20).

`ghia_re100_u.csv`: columns `y,u` -- horizontal velocity component u(0.5, y)
sampled at 17 y-locations from y=0 (bottom wall) to y=1 (moving lid).

`ghia_re100_v.csv`: columns `x,v` -- vertical velocity component v(x, 0.5)
sampled at 17 x-locations from x=0 (left wall) to x=1 (right wall).

These values are a widely-reproduced machine-readable transcription of the
original tables (cross-checked against multiple independent public
transcriptions before being committed here). The original journal tables
remain the authoritative source; this transcription is provided for
convenience and automated comparison only.

The same 34 (17+17) values are embedded directly in
`tests/integration/cavity/GhiaRe100.hpp` as the source of truth actually
used by the validation tests -- these CSV files are a human-readable,
citable copy for documentation and provenance, not something the test
code parses at run time.

## Reynolds number 1000 (P12-NUM-007)

`ghia_re1000_u.csv` (columns `y,u`) and `ghia_re1000_v.csv` (columns `x,v`)
hold the Re = 1000 columns of the same Table I (u(0.5, y)) and Table II
(v(x, 0.5)) of Ghia, Ghia & Shin (1982), at the same 17 sample locations
as Re = 100 (129x129 fine-grid solution). Only tabulated values are
stored -- nothing is interpolated or digitised from a figure.

Provenance and cross-check (done 2026-09-14, before the values were
committed):

1. Machine-readable transcription of Tables I and II (all seven Reynolds
   numbers), public GitHub gists by ivan-pi:
   `gist.github.com/ivan-pi/3e9326d18a366ffe6a8e5bfda6353219` (Table I, u)
   and `gist.github.com/ivan-pi/caa6c6737d36a9140fbcf2ea59c78b3c`
   (Table II, v).
2. Independent re-typeset of Tables I/II for Re = 100/400/1000 in
   H. Juujarvi, I. Kinnunen, "Lid driven cavity flow using stencil-based
   numerical methods", Uppsala University thesis (2022),
   DiVA `diva2:1668016`, Tables 1 and 2.
3. Independent re-typeset of the Re = 1000 columns in S. Mehmood,
   M. Nawaz, A. Ali, "Finite Volume Solution of Non-Newtonian Casson
   Fluid Flow in A Square Cavity", Communications in Mathematics and
   Applications 9(3), 459-474 (2018), DOI 10.26713/cma.v9i3.795,
   Table 2 ("Ghia et al." column).

The Re = 100 columns of source 1 reproduce this directory's existing
`ghia_re100_{u,v}.csv` exactly (34/34 values). For Re = 1000, 33 of the
34 values agree across all sources. The single discrepancy is
v(x = 0.9063): source 1 gives -0.51500, while sources 2 and 3 both give
-0.51550. The value stored here is **-0.51550** (two independent
transcriptions against one). This is a transcription-level discrepancy of
5e-4, far below the solver-vs-Ghia differences the validation tests
measure, so the choice does not affect any pass/fail decision.

As for Re = 100, the values actually used by the tests are embedded in
`tests/integration/cavity/GhiaRe1000.hpp`; these CSV files are the
citable, human-readable copy.
