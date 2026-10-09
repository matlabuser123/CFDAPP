# P16 eddy-viscosity framework -- closeout

## The causal chain, in the order it actually happened

This sequence is the evidence, not just the final number. Presenting only
"Nu ratio 1.202, PASS" would hide the part that matters: the benchmark
detected missing physics that every other gate had passed.

```
bulk turbulent thermal coupling implemented   k_eff = k_mol + mu_t cp / Pr_t
        |
        v
unit tests, reachability, variable properties, determinism   ALL PASS
        |
        v
energy conservation                           eps_E = 2.8e-16    PASS
        |
        v
frozen Gnielinski benchmark                   Nu ratio 4.295     FAIL
        |
        v
diagnosis: no THERMAL wall function. The wall heat flux was the raw
cell-centre gradient with k_eff across a cell spanning the log layer --
exactly the error the MOMENTUM wall function exists to remove on its own side.
Predicted before the run, with the ln(E y+) derivation.
        |
        v
T+ wall law implemented (Launder-Spalding / Jayatilleke), NO constant tuned
        |
        v
the SAME frozen benchmark, SAME band                Nu ratio 1.202     PASS
```

## The benchmark, before and after

| | Q_wall | T_bulk,out | h | Nu_CFD | Nu/Nu_ref | verdict |
|---|---|---|---|---|---|---|
| no thermal wall function | 4.137e4 W | 333.59 K | 126.02 | 980.73 | **4.295** | FAIL |
| **thermal wall function** | **1.333e4 W** | 310.83 K | 35.27 | **274.48** | **1.202** | **PASS** |
| Gnielinski reference | -- | -- | -- | 228.37 | band 0.5-2.0 | -- |

Frozen before the first run and never moved: the reference correlation, Re, Pr,
geometry, thermal BCs, the Nu convention (log-mean dT), and the acceptance
band. Frozen before implementation and never moved: `Pr_t = 0.85`,
`kappa = 0.41`, `E = 9.793`, and Jayatilleke's 9.24 / 0.75 / 0.28 / 0.007.

## Conservation

```
Q_wall = sum of ThermalResult::wallHeatFlux, the PRODUCTION wall operator
eps_E  = |Q_wall + Q_in - Q_out| / max(|Q_wall|, |Q_in|, |Q_out|)

            Q_wall        Q_in         Q_out        imbalance     eps_E
production  1.33299e4     3.69335e5    3.82665e5    5.82e-11      1.52e-16
C1 k_mol    2.18425e3     3.69337e5    3.82665e5   -1.11e4        2.91e-02
```

Frozen gate `eps_E <= 1e-4`, unchanged. The control misses by 14 orders.

## Two implementation defects this milestone found, both worth keeping

1. **A live wall function disconnected from the matrix.** The conductance was
   computed correctly (maxG = 2.13, scaling correctly with Pr_t) and threaded
   into the assembler's signature, but the boundary loop never read it -- so
   the solve was byte-identical to no wall function at all. Found by
   instrumenting rather than by assuming. `ActiveChangesTheSolveAndPublishes-
   ItsOwnFlux` is the permanent gate for it.

2. **Post-processing with a replaced operator.** Once the wall function
   replaced the boundary treatment, reconstructing Q_wall from `k_eff grad T`
   gave 1.13e5 W against an actual 1.33e4 -- an 8.5x error that made the
   CORRECTED model look dramatically worse (Nu = 2330). Fixed structurally:
   `ThermalResult::wallHeatFlux` publishes the production result and every
   diagnostic reads it.

The second is the same class of error as the earlier conservation slip (the
two-point secant vs the P12-DIFF-002 reconstruction, a 13% miss). Three
occurrences of one lesson: **a diagnostic must read the production operator,
never re-derive it.**

## Scope of the closed checkbox

> CPU eddy-viscosity coupling for momentum and thermal transport, including a
> configurable turbulent Prandtl number, variable-property
> `k_t = mu_t cp / Pr_t`, a production thermal wall treatment, conservative
> wall-flux accounting, and quantitative forced-convection validation.
>
> **Thermal coupling remains ONE-WAY**, from converged flow/turbulence to
> energy. Two-way thermo-fluid coupling and GPU equivalence are outside this
> checkbox.

## Not covered -- see known_limitations.md

* temperature does not feed back into momentum, density, viscosity or
  turbulence;
* no GPU thermal path exists at all;
* the benchmark is a tube correlation applied to a channel through the
  hydraulic diameter -- an engineering reference, not geometry-exact DNS;
* the thermal wall function shares the momentum wall function's validity
  window: it is a log-layer treatment and inherits the y+ constraints
  documented for `k-omega SST` in `../known_limitations.md`.
