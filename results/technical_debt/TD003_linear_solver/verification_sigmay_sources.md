# TD-003 C3 — the Patankar hypothesis is REFUTED, three more candidates eliminated, and the observed deviation EXCEEDS its own upper bound

```text
STATUS:      **MY PATANKAR HYPOTHESIS IS REFUTED, by inspection, at zero cost.** The flame
             passes an EXPLICIT ZERO source to the transport assembly, so
             `sum_i (Su_i + Sp_i Y_i) = 0` identically -- there is no Patankar source in the
             equation at all.
             All FOUR sites that write composition were then enumerated and each preserves
             `sum Y` to round-off or exactly. **So no composition-write site explains
             8.823e-07.**
             AND A LOGICAL INCONSISTENCY SURFACED: the observed deviation **EXCEEDS the
             transport step's own upper error bound by 5.8x** (2.6e-07 even on the most
             generous ||b||, still 3.4x short). An upper bound cannot be exceeded, so one of
             its premises is false. Which one is NOT settled here.
SCOPE:       step 1 of the agreed plan: dump the raw source sum rather than add another
             derived counter. No production change, no test change, no gate frozen, NO RUN.
INSTRUMENT:  source reading plus exact IEEE arithmetic on the fixture's own literals. Nothing
             here is estimated except ||b||, which is labelled where it is used (§4).
EVIDENCE:    this file. Every number is reproducible from the cited constants.
COMMIT:      carried as evidence.
```

---

## 1. THE HYPOTHESIS, AND WHY IT IS DEAD

`verification_kappa.md` §2.2 proposed that the missing factor came from the Patankar split
failing to preserve `sum_i omega_i = 0`. `PremixedFlame.cpp:1172`-`:1177` settles it:

```cpp
// An EXPLICIT zero source: the reaction is not part of this equation at all. Stated as a
// zero field rather than omitted, so what the transport step solves is unambiguous.
cfd::transport::ScalarSourceFields source;
source.Su = ScalarField(nc);
source.Sp = ScalarField(nc);
```

**There is no Patankar source in the transport equation.** `sum_i (Su_i + Sp_i Y_i) = 0`
identically, for every cell, by construction. The hypothesis had the right question and the wrong
operator: the reaction lives in the *chemistry half* of the operator split, not in the transport
matrix.

`ProjectRunner.cpp:924` does the same thing on the production reacting path — it passes
`zeroSources.perSpecies[i]`. So this is the architecture, not an accident of the flame.

## 2. ALL FOUR COMPOSITION-WRITE SITES, AND WHAT EACH DOES TO `sum Y`

```text
:340   chemistry      composition[i][c] = cellComposition[i][0]       after the reactor
:526   seeding        composition[i][c] = unburnt + ...               initial profile
:973   recentring     shiftField(composition[i], unburntComposition[i])
:1286  transport      composition[i][c] = next                        the linear solve
```

### 2.1 Chemistry — `sum_i omega_i` cancels EXACTLY

```cpp
cellComposition[i][0] += step * sources.perSpecies[i].Su[0] / cellDensity[0];
```

`cellDensity[0]` is **common to every species** in that loop (it is refreshed only after it), so
`sum_i dY_i = (step / rho) * sum_i omega_i`. The fixture's mechanism is
`Reaction("F->P", {{"F",1.0}}, {{"P",1.0}})` — one reaction, 1:1, and `makeSpecies` gives F and P
the **same** molar weight `kWeight = 0.029`. So:

```text
omega_F = -W q = -3.58024430000000038e+01
omega_P = +W q = +3.58024430000000038e+01
sum                 0.00000000000000000e+00      EXACTLY ZERO in IEEE
```

Equal magnitude, opposite sign: the cancellation is exact, not approximate. Per sub-step the only
residue is the rounding of the two separate `step*omega_i/rho` products, which is `~eps` of `dY`.
Over even 85 000 sub-steps a random walk of that gives `~6e-14`.

### 2.2 Recentring — the fill sums to EXACTLY 1.0

The fill value is `conditions.unburntComposition[i]`, and the fixture sets it to
`{kFuelFraction, 1.0 - kFuelFraction}`:

```text
fl(0.05)         = 5.00000000000000027756e-02
fl(1.0 - 0.05)   = 9.49999999999999955591e-01
their sum        = 1.00000000000000000000e+00      |sum - 1| = 0.000e+00
```

**Bit-exactly 1.0.** The two rounding errors cancel. So every cell the shift *fabricates* carries a
sum error of exactly zero, and recentring cannot drift `sum Y` however many times it fires.

This is worth stating plainly because TD-004 established recentring as non-conservative for
**fuel** and **energy** — it discards and fabricates cells, moving `9.97e-07` of fuel per one-cell
shift, which is suspiciously close to `8.823e-07`. That resemblance is a coincidence: the
mechanism that breaks fuel conservation leaves the mixture constraint exact, because the fill is a
valid composition.

### 2.3 Transport — bounded, and the bound is the problem (§4)

## 3. SO THREE CANDIDATES ARE ELIMINATED

```text
ELIMINATED  the Patankar source sum          -- there is no source (§1)
ELIMINATED  the chemistry source sum         -- exactly zero in IEEE (§2.1)
ELIMINATED  the recentring fill              -- sums to exactly 1.0 (§2.2)
REMAINS     the transport solve              -- and §4 shows it does not fit either
```

**This is an elimination, not a diagnosis.** Step 1 was agreed as a cheap test of one hypothesis;
it refuted that hypothesis and, because enumerating the write sites was free, refuted two more.

## 4. THE INCONSISTENCY: the observed value EXCEEDS the transport bound

For `A x = b` and an iterate with residual `r`, `x - x_hat = A^-1 r`, so

```text
||err||_inf <= ||A^-1||_inf * ||r||_inf        with ||A^-1||_inf = 2.400000 (measured)
```

With the accepted relative residual `||r||_2 <= 1e-9 ||b||_2` and `b_i ~ rho V/dt * Y_i^n`
(`rho V/dt = 2.5000`, so `b_cell <~ 2.375`):

```text
||b||_2 (estimated, shape factor 0.6)   31.9
||r||_2 <= 1e-9 ||b||_2                 3.186e-08
||err||_inf <= 2.4 * ||r||_2            7.647e-08
|sum Y - 1| <= 2 x that (two species)   1.529e-07

OBSERVED                                8.823e-07        EXCEEDS THE BOUND BY 5.8x
```

Taking the most generous `||b||_2` the problem admits — no shape factor, every cell at
`b_cell = 2.375`, giving `||b||_2 = 53.1` — the bound becomes `2.6e-07`, **still 3.4x short**.

**An upper bound cannot be exceeded.** So at least one premise is false:

```text
(a) the accepted residual is LARGER than 1e-9 * ||b|| on some solves. TD-003 records
    worstLinearResidual "pinned at exactly its 1e-9 acceptance fraction"; that is carried
    forward here and was never independently verified.
(b) ||A^-1||_inf = 2.400000 does not describe the assembled matrix. It is a RECONSTRUCTION
    (verification_kappa.md §5 says so); the assembled matrix carries the chemistry's Patankar
    Sp in the ENERGY equation and a boundary treatment this reconstruction approximates.
(c) |sum Y - 1| is not a per-step solve error at all -- it accumulates, or has a source
    outside the four write sites, e.g. in `compositionOld` staging or the reported-field path.
```

**Step 1 does not settle which, and I am not guessing.** (c) is attractive because it would also
explain why the deviation tracks neither step count nor tolerance, but "attractive" is how the
last two hypotheses died.

## 5. WHAT THIS MEANS FOR WHERE CRITERION 3 BELONGS

TD-003 is *"reacting-flow linear-solver robustness"*. Its criterion 3 is the `1e-8` `sum Y` gate.
After this measurement:

```text
no composition-write site is the linear solver (§3)
and the transport solve's own bound does not reach the observed value (§4)
```

So **criterion 3's cause is not established as a linear-solver matter**, and TD-003 is plausibly
the wrong owner for it. That is a register-structure question, not a technical one, and it is the
scoping risk flagged before step 1 was run — now with evidence behind it rather than a hunch.

## 6. WHAT IS NOT ESTABLISHED

```text
NOT established  the cause of the 8.823e-07. Three candidates eliminated, the fourth does not
                 fit its own bound. The source is UNKNOWN.
NOT established  which premise in §4 is false. Named, not resolved.
NOT measured     the actual per-solve ||r|| and ||b|| from a run. This is the decisive next
                 measurement and it needs instrumentation: print ||r||/||b|| and ||b|| per
                 species per step from a copy outside the repo. ||b||_2 in §4 is ESTIMATED
                 and labelled as such.
NOT established  that `sum_i omega_i = 0` exactly for a mechanism OTHER than this fixture's.
                 F -> P with equal molar weights cancels exactly; a multi-reaction mechanism
                 with unequal weights (Westbrook & Dryer Set 3, which G8 needs) would sum
                 `sum_r nu_ir W_i q_r` and need its own check. §2.1 covers THIS fixture only.
NOT examined     `compositionOld` staging. TD-028 found a stale-time-level defect in exactly
                 that kind of block, and §4(c) lists it as a candidate; nothing here looked.
NOT re-run       anything. No solver was run for this step.
```
