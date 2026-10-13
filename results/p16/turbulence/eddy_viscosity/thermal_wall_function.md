# P16-TWF -- thermal wall function: FROZEN FORMULATION

**Frozen before implementation and before any new CFD result.** No constant
here may be tuned to move `Nu = 980.7` toward `228.4`; the entire point of this
subtask is to find out whether the established physics fixes the fourfold
over-prediction on its own.

---

## Why this is P16 and not P20

P20 is advanced SST **momentum** near-wall treatment. This is **thermal** wall
physics, and it is required by P16's own original completion gate, which
demanded a quantitative turbulent heat-transfer benchmark. The benchmark
exposed that the bulk coupling is right and the wall treatment is missing.
Narrowing P16 to "bulk turbulent diffusion only" after seeing a 4.295x miss
would be weakening the gate after seeing the result.

## The convention -- Launder-Spalding / Jayatilleke

**P is INSIDE the Pr_t bracket.** This was checked deliberately, because the two
readings differ by a factor of Pr_t on the P term:

```
                 [  1                   ]
    T+  =  Pr_t  [ --- ln(E y+)  +  P   ]        (log region)
                 [ kappa                ]

    T+  =  Pr y+                                  (conduction sublayer)
```

NOT `Pr_t ln(E y+)/kappa + P`. The form above is the one used by established
implementations (OpenFOAM's `alphatJayatillekeWallFunction`) and matches the
Launder-Spalding derivation, in which the whole log-layer temperature profile
including the sublayer-resistance correction is scaled by Pr_t.

### Jayatilleke's P-function

```
             [ (       )^0.75     ]  [                    (        ) ]
P = 9.24     [ ( Pr    )      - 1 ]  [ 1 + 0.28 exp( -0.007  Pr     ) ]
             [ ( ---   )          ]  [                    ( ----   ) ]
             [ ( Pr_t  )          ]  [                    ( Pr_t   ) ]
```

Constants 9.24, 0.75, 0.28, 0.007 are Jayatilleke's and are NOT adjustable.

`kappa = 0.41` and `E = 9.793` are taken from the EXISTING
`WallFunctionCoefficients` this repository already uses for momentum, so the
thermal and momentum wall treatments describe the same wall, not two
conventions.

**Sign note, computed at freeze time:** for air, `Pr = 0.704`, `Pr_t = 0.85`,
so `Pr/Pr_t = 0.828 < 1` and `P = -1.55696`, i.e. NEGATIVE. That is correct and
expected whenever `Pr < Pr_t`; a formulation that forced P >= 0 would be wrong.

### The blending point

`y+_T` is where the two branches intersect:

```
Pr y+_T  =  Pr_t [ ln(E y+_T)/kappa + P ]
```

Solved by bisection rather than a correlation, so it is exact for whatever
Pr/Pr_t the case has. Below `y+_T` the conduction branch applies, above it the
log branch. This is a genuine switch, not a blend -- that is the standard
Launder-Spalding construction, and the two branches meet by definition at the
crossing.

## The wall heat flux

```
q_w = rho cp u_tau (T_w - T_P) / T+
```

with `u_tau = sqrt(tau_w / rho)` taken from the turbulence model's own wall
shear, so the thermal wall law is driven by the SAME friction velocity the
momentum wall function produced. Not recomputed, not re-derived.

## How it enters the matrix -- a WALL-FACE contribution

Expressed as a per-face **conductance**, exactly parallel to how the momentum
wall function reaches momentum through a per-face wall viscosity:

```
G = rho cp u_tau / T+          [W / m^2 / K]

flux into owner = G A (T_w - T_P)
   =>   a_P += G A
        b_P += G A T_w
```

This replaces the diffusive boundary treatment at wall faces only. It is NOT a
modified volume-cell energy equation, and it does NOT touch internal faces, the
convection term, or any non-wall boundary.

The codebase already assembles exactly this shape for `ConvectiveHeatTransfer`
(a Robin wall), so the pattern is established rather than invented here.

## What must remain bitwise unchanged

* every laminar case -- no turbulence model, so no wall conductance exists;
* every case without wall functions;
* every non-wall boundary face in every case;
* the conduction/molecular wall path when the thermal wall function is
  inactive.

## Acceptance -- the ALREADY-FROZEN benchmark, unchanged

The Gnielinski definition, the Nu convention and the `0.5 <= Nu_CFD/Nu_ref <=
2.0` band in `benchmark.md` are re-used verbatim. None of them may move.

Negative controls retained unchanged: `k_t = 0`, wrong `Pr_t`, frozen `cp(T)`,
frozen `k(T)`.

## A-PRIORI ESTIMATE -- written before implementing

With `y+ ~ 180`, `Pr = 0.704`, `Pr_t = 0.85`:

```
ln(E y+)/kappa = ln(9.793 * 180)/0.41 = 7.475/0.41 = 18.23
P              = -1.55696
T+             = 0.85 * (18.2318 - 1.55696) = 14.1728
```

Current treatment is equivalent to `T+_effective = Pr_t/kappa = 2.07`, so the
wall flux should fall by roughly `14.17 / 2.07 = 6.8x`. The coupled solve will
damp that, as `T_P` falls until the balance re-closes.

Expected landing zone: `Nu` somewhere around **150-250**, against Gnielinski's
**228.4**. That is inside the frozen band -- but it is an estimate, not a
target, and the band will not be moved if the result lands outside it.
