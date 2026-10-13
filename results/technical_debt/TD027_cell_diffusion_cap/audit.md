# TD-027 — AUDIT — `maximumCellDiffusionNumber = 20.0` is an unvalidated resolution cap

```text
item        TD-027   P0 · an unvalidated resolution cap that falsifies a gated quantity
authorized  2026-10-05, "authorize TD-027"
found by    TD-005 (`TD005_flame_speed_extraction/dry_run.md` §M)
tree        src/species/PremixedFlame.cpp  sha256 aead57e6c39aa7c893ae1d9c2bca8928c31321b4192a4eb7015a14c3797b6278
            include/cfd/species/PremixedFlame.hpp sha256 c962020a2254e721037840a70a0d2ab30f48c6e4f24ae131313b71e266743fdf
status      AUDIT ONLY. No gate frozen, no production change. §6 names a decision that is the
            user's and not mine to make, because it trades gate accuracy against CI runtime.
```

---

## 1. A CORRECTION TO TD-005's OWN FIGURES, first

TD-005 `dry_run.md` §M.4 and TD-027's register entry both state that the cap *"permits 20"* while
*"the default realizes `D = 3.33`"*, i.e. that the cap is a loose upper bound. **That is wrong.** It
used the **unburnt** diffusivity. The code uses the **burnt** one:

```text
src/species/PremixedFlame.cpp:477-481
  // alpha_max is taken at the BURNT side, where the density is lowest and the diffusivity largest.
  const Real alphaMax = conditions.conductivity->value(tAdiabatic) / (rhoBurnt * cp);
```

```text
rho_burnt      0.19635 kg/m3        rho_unburnt   1.17810      ratio 6.00
alpha_burnt    2.1221e-04 m2/s      <- alpha_max, what the code uses
alpha_unburnt  3.5368e-05 m2/s      <- what §M.4 wrongly used
```

With the correct diffusivity:

```text
diffusion limit = 20 * dx^2 / alpha_burnt = 3.769930e-05 s
measured dt in every converged trial row  = 3.76972e-05 s      MATCHES
```

**So `dt` sits EXACTLY at the cap, and `D` is identically `maximumCellDiffusionNumber`.** The cap is
not a loose bound that happens to be respected — it *is* the realized cell diffusion number. That
makes the item simpler and more serious than §M.4 described, and §M.4 is corrected in place.

### 1.1 Which limit binds, and when

```text
dt = min( cellCourant * dx / u_in ,  maximumCellDiffusionNumber * dx^2 / alpha_max )

at the Zeldovich SEED (u_in = 0.591 m/s):   Courant   0.3*dx/u_in = 1.0152e-05 s   <- binds
at the EIGENVALUE     (u_in = 0.0486 m/s):  Courant                 1.2346e-04 s
                                            diffusion              3.7699e-05 s   <- binds
```

The trial logs show exactly this progression — `dt = 1.01484e-05`, `2.02967e-05`, then
`3.76972e-05` held for every subsequent trial. **At the eigenvalue, where every reported result is
produced, the diffusion cap binds and saturates.**

## 2. The measured consequence

From TD-005 `logs/18`, a `dt` ladder at fixed `dx` (the only self-similar family in this
parameter), with `D` restated in the code's own convention:

```text
divisor   dt s          D       S_L cm/s    error vs the converged value
 dt/1     3.770e-05   20.00      4.86230        +3.987 %     <- THE DEFAULT
 dt/2     1.885e-05   10.00      4.65615        -0.422 %
 dt/4     9.424e-06    5.00      4.67194        -0.084 %
 dt/8     4.712e-06    2.50      4.67510        -0.017 %

asymptotic family {dt/2, dt/4, dt/8}: successive differences shrink 5.00x
Richardson limit 4.67589 cm/s     GCI on the finest pair 0.021 %
G8's acceptance band 35.0-37.0 cm/s is +-2.80 %
```

**The default cap puts `S_L` 3.99 % off, which is 1.4x G8's entire band, and outside the asymptotic
range** — the error changes sign between `D = 20` and `D = 10`, so at the default no convergence
statement can be made at all.

## 3. `D` CANNOT BE IMPROVED BY REFINING THE MESH

This is the structural point, and it decides what a fix can even look like.

```text
dt = cap * dx^2 / alpha_max        =>        D = alpha_max * dt / dx^2 = cap,  identically
```

`D` is **independent of `dx`**. Refining the mesh at a fixed cap leaves `D = 20` exactly where it
was, while costing `2x` the cells and `4x` the steps — **8x for no accuracy in this parameter.**
Confirmed by measurement: TD-005 `logs/16` found ~103 march steps per relaxation time at 250 cells
against ~397 at 500, a ratio of 3.85 ≈ `(dx ratio)^2`.

**So the cap is the only lever.** That also explains why the mesh-refinement test (TD-029) could
never have detected this: refining cells does not change `D`, so the temporal error it carries is
constant across that test's whole family, and only the spatial error moves — which is precisely how
the two errors came to cancel.

## 4. Blast radius

```text
EVERY recorded S_L in the repository    computed at D = 20, so ~4 % high. Includes the 4.86230
                                        quoted throughout TD-004 and TD-005, and P18's G8 work.
TD-004 GATE.md                          C6 reports S_L against a 4.86230 baseline; C5 expects
                                        T_peak 1800.00 and x_front 3.50000 exactly. FROZEN.
TD-004 GATE_A1.md                        C2-A1's family is sized to a 14-relaxation-time period
                                        measured AT D = 20. FROZEN 2026-10-05.
TD-005 GATE.md                           C2's bracket bounds are relative and survive; C4b/C4c
                                        spreads and C5's control values were measured at D = 20.
                                        FROZEN.
TD-028                                   the limit cycle is suppressed at a lower cap, so fixing
                                        TD-027 is expected to resolve TD-028 as a consequence.
```

**A change to this default invalidates measured values inside three frozen gate files.** None may be
edited; each needs its own amendment under CLAUDE.md §5.3, exactly as TD-004's C2 just did.

## 5. The cost, measured

`G8PremixedFlameTest` per-test timings from TD-005 `logs/08` (post-C4d, at `D = 20`), and the
step count scales as `1/dt`, i.e. linearly in `1/cap`:

```text
test                                        D=20     D=10     D=5      D=2.5
ItConverges...                               256s     512s    1024s    2048s
TheConsumptionIntegralAgrees...              256s     512s    1024s    2048s
TheAnswerIsInsensitiveToTheStepsPerTrial     759s    1518s    3036s    6072s
TheEigenvalueIsIndependentOfTheGuess         471s     942s    1884s    3768s
TenTimesThePreExponential...                 798s    1596s    3192s    6384s
TheEigenvaluePlateaus...                     910s    1820s    3640s    7280s  <- EXCEEDS 7200 s
TheFlameSpeedConvergesUnderRefinement        268s     536s    1072s    2144s
---------------------------------------------------------------------------
suite total (measured tests)                3718s    7436s   14872s   29744s
                                             62 min   2.1 h    4.1 h     8.3 h
longest single test                          910s    1820s    3640s    7280s
```

```text
the repository's per-test cap is ctest --timeout 7200 (ci.yml:147, per gtest case)
WithReactionsDisabledNoFlamePropagates is ALREADY unmeasured at 7200 s (TD-020), and scales too
the full CI matrix already runs ~4.5 hours with no path filters
```

**`D = 2.5` breaks the per-test cap outright.** `D = 5` puts the suite at 4.1 hours on top of a CI
matrix that already takes 4.5. `D = 10` costs an extra hour.

## 6. THE DECISION THIS NEEDS, which is not mine

Three candidate caps, each defensible, with materially different consequences:

| cap | `S_L` error | suite | longest test | the case for it |
| --- | --- | --- | --- | --- |
| **10** | 0.422 % | 2.1 h | 1 820 s | the largest value demonstrably **inside** the asymptotic range; error is 15 % of G8's band; affordable |
| **5** | 0.084 % | 4.1 h | 3 640 s | a 2x margin inside the asymptotic boundary; error 33x smaller than G8's band |
| **1** | `<0.02 %` | ~20 h | breaks the cap | the textbook condition — `dt` does not exceed one cell-diffusion time — but infeasible here |

**An internal incoherence that bears on the choice, and that I would not have found without the
correction in §1.** TD-005's C2 bounds the eigenvalue's reported uncertainty at
`sqrt(1+tolerance) - 1 = 5.0e-04`, i.e. **0.05 %**, and the solver reports that as
`massFluxResidual`. At `cap = 10` the time-discretisation error is **0.42 %** — eight times the
precision the solver claims for the same quantity. **A reported bracket finer than the
discretisation supports is a precision claim the solver cannot honour**, whichever cap is chosen,
and `cap = 5` (0.084 %) is the first that comes within a factor of two of it.

So the choice is between:

```text
(A) cap = 10   affordable, inside the asymptotic range, but the reported bracket overstates the
               achieved precision by ~8x, and TD-005 C2 would need its own amendment to say so
(B) cap = 5    the reported bracket is then coherent to within 1.7x, but the suite goes to 4.1 h
               and CI to ~7 h total
(C) cap = 10 AND loosen TD-005's bracket tolerance to match the discretisation, so the two agree
               -- cheapest, but it weakens a precision the solver currently claims, which needs
               its own authorization and is not something I would do without it
```

**I recommend (B), `cap = 5`**, on the grounds that this item exists because an unvalidated
resolution constant falsified a gated quantity by more than the gate's own band, and (A) leaves a
second incoherent precision claim in place to be discovered later. But (B) costs three hours of CI
per run, and that is a project-level tradeoff rather than a numerical one.

**No gate is frozen and no production file is touched until this is decided**, because the cap's
value is the criterion's subject: a gate frozen against the wrong target would have to be amended
immediately.
