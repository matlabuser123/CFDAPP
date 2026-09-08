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
