# Differentially Heated Square Cavity — Reference Data (P3-PHYS-002)

## Source

* **Primary**: de Vahl Davis, G. (1983). *Natural convection of air in a
  square cavity: A bench mark numerical solution.* International Journal
  for Numerical Methods in Fluids, 3(3), 249-264.
  DOI: 10.1002/fld.1650030305. The canonical, extremely widely cited
  (thousands of citations) benchmark for this exact problem.
* **Independent cross-check**: Wan, D. C., Patnaik, B. S. V., & Wei,
  G. W. (2001). *A new benchmark quality solution for the
  buoyancy-driven cavity by discrete singular convolution.* Numerical
  Heat Transfer, Part B: Fundamentals, 40(3), 199-228. Tables 2, 3, and
  5 of this paper reproduce de Vahl Davis's (1983) own values verbatim
  (labeled "Ref. [3]" in that paper's tables) alongside several other
  independent investigations (Ramaswamy et al., Massarotti et al.,
  Manzari, Mayne et al.) and this paper's own two independent high-
  accuracy methods (FEM, discrete singular convolution) -- all in close
  agreement, giving genuine multi-source confidence in the numbers used
  here.

## Provenance disclosure

Retrieved in this development sandbox via `WebSearch`/`WebFetch` against
`https://users.math.msu.edu/users/wei/paper/p59.pdf` (the Wan/Patnaik/Wei
paper above, openly hosted by the author's institution), converted to
text with `pdftotext -layout`, and the exact table rows below transcribed
directly from that extracted text (Tables 2, 3, and 5 of that paper).
This is a live-fetched, independently-published, peer-reviewed source --
not a value recalled from training data or copied from an uncited web
page (P3-PHYS-002 section 6's own explicit requirement).

## Problem definition

Two-dimensional square cavity, `Lx = Ly = L = 1` (nondimensional).
Left wall: `T = T_hot` (fixed), no-slip (`u=v=0`). Right wall:
`T = T_cold` (fixed), no-slip. Top/bottom: adiabatic (zero heat flux),
no-slip. Gravity acts in `-y` (downward), driving flow via the
Boussinesq approximation. Working fluid: air, `Pr = 0.71`.

## Nondimensionalization

* Length: `x* = x/L`, `y* = y/L`.
* Velocity: `U = u*L/alpha`, `V = v*L/alpha` (`alpha` = thermal
  diffusivity) -- the classical de Vahl Davis convention, *not* a
  viscous (`nu`) velocity scale.
* Temperature: `theta = (T - T_cold) / (T_hot - T_cold)`, so
  `theta=1` at the hot wall, `theta=0` at the cold wall.
* `Ra = g*beta*(T_hot-T_cold)*L^3 / (nu*alpha)`, `Pr = nu/alpha`.

CFDApp's own case setup (`tests/integration/thermal/
NaturalConvectionValidationUtils.hpp`) chooses dimensional inputs
(`rho=1`, `cp=1`, `k=1` so `alpha=1`; `nu = Pr` so `mu = Pr`; `L=1`;
`T_hot=1`, `T_cold=0`; `g=1` (magnitude); `beta = Ra*Pr` -- so that
`Ra`/`Pr` land exactly on the requested target) such that the raw
dimensional solver output is *already* expressed in the nondimensional
convention above (`alpha=L=1` makes `U=u`, `theta=T` directly) --
documented in full in that file's own header comment.

## Reference data table

`Nu_max`/`Nu_min` are the local hot-wall Nusselt-number extrema, with
their `y`-location in parentheses (`y=0` bottom, `y=1` top).
`u_max`/`v_max` are the maximum horizontal/vertical nondimensional
velocity magnitudes on the vertical/horizontal mid-lines
(`x=0.5`/`y=0.5` respectively), with their own coordinate in
parentheses.

| Ra | Nu_avg | Nu_max (y) | Nu_min (y) | u_max (y) | v_max (x) |
|---|---|---|---|---|---|
| 1e3 | 1.12 | 1.50 (0.092) | 0.692 (1.0) | 3.634 (0.813) | 3.679 (0.179) |
| 1e4 | 2.243 | 3.53 (0.143) | 0.586 (1.0) | 16.2 (0.823) | 19.51 (0.12) |
| 1e5 | 4.52 | 7.71 (0.08) | 0.729 (1.0) | 34.81 (0.855) | 68.22 (0.066) |
| 1e6 | 8.8 | 17.92 (0.038) | 0.989 (1.0) | 65.33 (0.851) | 216.75 (0.0387) |

This is the exact source-of-truth table
(`tests/integration/thermal/DeVahlDavis1983.hpp` mirrors it as hardcoded
`constexpr` values, same "hardcoded constexpr is the source of truth,
`validation/` carries a documented human-readable copy" convention as
`GhiaRe100.hpp`/`ChannelReTau180.hpp`).

## Scope for P3-PHYS-002

`Ra=1e3` is this task's primary, fully-validated benchmark (three grids:
20x20/40x40/80x80). `Ra=1e4` is run as a secondary check at one grid
(see the task's own guidance: "Start with a moderate benchmark... Do
not jump immediately to extremely high Rayleigh numbers" and "if stable
and practical, add Ra=1e4, Ra=1e5" -- time-boxed to one additional
Rayleigh number for this pass, not the full grid triplet, an explicit,
disclosed scope decision).
