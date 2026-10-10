# TD-003 C3 — MEASURED: the deviation is 99.97 % the assembled RHS and 1.04 % the linear solve. Criterion 3 does not belong to TD-003.

```text
STATUS:      **CAUSE LOCATED.** An exact Thomas solve of the SAME assembled systems leaves
             `|sum Y - 1| = 2.301527e-06` -- **230x the 1e-8 gate, with zero solver error.**
             The linear solve contributes `2.396274e-08`, **1.04 %** of the total.
             So criterion 3 is NOT a linear-solver defect, and **the "tighten the tolerance"
             remedy cannot work**: driving the residual to exactly zero still leaves 230x.
             My derived requirement of `<= 3.8e-10` is WITHDRAWN.
             Of the three premises in verification_sigmay_sources.md §4: **(a) CONFIRMED**
             (TD-003's 1e-9 record is right), **(b) REFUTED by 50 000x** (my reconstructed
             `||A^-1||_inf` was 2.4; the assembled matrix is 120 000), **(c) CONFIRMED and it
             is the cause.**
SCOPE:       the instrumented measurement. The repository's production source is UNCHANGED and
             hash-gated to prove it. No test change, no gate frozen.
INSTRUMENT:  tools/instrument_residuals.patch applied to a COPY OUTSIDE THE REPOSITORY
             (CLAUDE.md section 2). **A PURE-ADDITION patch: 0 lines removed, 158 added**, so it
             cannot alter control flow or perturb what it measures.
             instrumented source d3e1f95c571a3727..., driver 4d09b029eb2cd5ad...,
             library 0d0dfaa96ac97d5f..., production source fa815c9721658448... VERIFIED
             unchanged before the build (the harness aborts otherwise).
             **THE PATCH WAS TESTED, AND THE FIRST VERSION DID NOT APPLY.** Its `+++` header
             named an absolute scratchpad path, which `patch` refuses as dangerous: 5 of 5
             hunks ignored. Regenerated with repo-relative labels on both sides, it now
             applies with `patch -p0` from the repo root and reproduces the instrumented
             source to the exact hash `d3e1f95c571a3727...` -- see section 1.1.
CONTROL:     the run reproduces the gated values EXACTLY -- see section 1. Without that, none of
             the decomposition would mean anything.
EVIDENCE:    logs/02_instrumented_residuals.log · tools/instrument_residuals.patch
COMMIT:      carried as evidence.
```

---

## 1. THE CONTROL, FIRST

A decomposition of the wrong run is worthless, so the instrumented binary had to reproduce the
quantity the gate measures:

```text
                              instrumented      recorded                    match
S_L                           4.955332 cm/s     4.95533 cm/s                yes
maxCompositionSumDeviation    8.822866e-07      8.822866316426925e-07       EXACT
recentrings                   223               223 (TD-031)                yes
totalTimeSteps                73683             73683 (TD-005 C3, relaxT 3) yes
finalMarchSteps               15283             15283 (TD-005 C3)           yes
status                        0 Converged       Converged                   yes
peakTemperature               1799.998974 K     T_ad 1800.0                 yes
```

**Bit-for-bit on the gated quantity.** The instrument measures the real solve.

`tridiagonal everywhere: yes` — the extracted bands were complete at every step, so the Thomas
solve really was exact and not an approximation of the assembled operator.

### 1.1 The patch is reproducible, which had to be TESTED rather than assumed

```text
patch -p0 < tools/instrument_residuals.patch   applied to a pristine copy
  produced  d3e1f95c571a3727ca021bf6ba26d0ee6db7fa55ab28f86daff8558f18a0f9f6
  target    d3e1f95c571a3727ca021bf6ba26d0ee6db7fa55ab28f86daff8558f18a0f9f6   MATCH
production src/species/PremixedFlame.cpp still fa815c97216584485e1c94842669fbf71976b9f...
```

The FIRST version of this patch silently did nothing — absolute paths in its header, 5 of 5 hunks
ignored, and the "patched" copy came back with the *original* hash. A patch file that is never
applied looks exactly like evidence. It is checked here because this campaign has already been
caught by a probe that printed what success looks like while measuring nothing.

**Note on `git diff --check`:** this patch contributes 13 check lines, all single-space lines.
Those are the unified-diff encoding of EMPTY CONTEXT lines and are required by the format —
stripping them would corrupt the patch and `patch` would reject it. So unlike a raw log, there is a
positive reason to keep them, not merely a precedent.

## 2. THE DECOMPOSITION

```text
max_c |sum_i y_iterative - 1|     2.302203e-06     what the gate's quantity peaks at
max_c |sum_i y_EXACT - 1|         2.301527e-06     99.97 %  <- the assembled RHS
max_c |sum_i (y_iter - y_exact)|  2.396274e-08      1.04 %  <- the linear solve
max |y_iter - y_exact| per cell   2.464101e-08
```

**Solving the same systems exactly removes 1 % of the deviation and leaves 230x the gate.** That is
the whole finding. (The two parts do not add to the total because each is a maximum over cells, and
the maxima fall in different cells.)

### 2.1 Why the gate reports 8.82e-07 and this reports 2.30e-06

Both are correct and they measure different things. `maxCompositionSumDeviation` is computed on the
**final field only** (`PremixedFlame.cpp:1616`-`:1629`); this instrument takes the maximum over
**all 73 683 transport steps**. So the march transiently reaches `2.30e-06` and settles to
`8.82e-07`. The decomposition ratio is what matters and it is a within-run comparison, unaffected.

## 3. THE THREE PREMISES, RESOLVED

`verification_sigmay_sources.md` §4 said one of three premises had to be false. All three are now
settled, and **the one I thought least likely to be checkable turned out to be the cause.**

### (a) "the accepted residual is larger than 1e-9 ||b||" — CONFIRMED AS RECORDED, not the cause

```text
worstLinearResidual        9.999987e-10
max ||r||/||b||            9.999987e-10        mean 7.351215e-10
accepted via converged()   221049 of 221049    via the residual fallback: 0
```

TD-003's note that `worstLinearResidual` is *"pinned at exactly its 1e-9 acceptance fraction"* is
**right**, and now independently verified rather than carried forward. Every one of the 221 049
solves cleared `converged()`; the residual fallback at `:690` never fired once.

**And this explains the `1e-11` puzzle.** `:686` sets `absoluteTolerance = 1e-9 * ||b||`, and
`converged()` is satisfied by the ABSOLUTE criterion, so the `relativeTolerance = 1.0e-11` at
`:625` **never binds**. The solver is doing exactly what it was configured to do.

### (b) "my ||A^-1||_inf does not describe the assembled matrix" — REFUTED BY 50 000x

```text
my reconstruction (verification_kappa.md)      2.400000
the ASSEMBLED matrix, measured               120000.010022
```

My reconstruction described the **trial** matrix, where `rho V/dt = 2.5` dominates and the operator
is strictly diagonally dominant. The **final march** uses a far larger `dt`, so `rho V/dt` stops
dominating and the operator approaches the steady convection-diffusion one, which is nearly
singular — a constant is almost in its null space under a zero-gradient outlet. Hence `1.2e+05`.

```text
and ||b||_2 spans 2.388179e-05 .. 4.025126e+04 -- NINE orders of magnitude.
```

My estimate of `||b||_2 ~ 32` was the right order for a typical cell and wrong by `10^3` for the
worst. **So verification_kappa.md's kappa, and every bound I derived from it, described a matrix
the gated quantity is not measured on.** That is the second time in this item that a reconstruction
stood in for an assembled object and misled me; the instrument exists because of the first time.

### (c) "|sum Y - 1| is not a per-step solve error" — CONFIRMED, and it is the cause

Section 2. The deviation survives an exact solve at 99.97 % of its size.

## 4. CONSEQUENCES

```text
WITHDRAWN   "tighten residualAcceptanceFraction to 3.8e-10" (verification_kappa.md 2.1).
            At zero residual the deviation is still 2.301527e-06, 230x the gate. The remedy
            was derived on the assumption the solve was responsible. It is not.
WITHDRAWN   the claim that criterion 3 "does require a tolerance change". It does not.
STANDS      TD-003's criteria 1 and 2, which are about solver status and the matrix class.
            Nothing here touches them.
RELOCATED   criterion 3. No composition-write site is the linear solver
            (verification_sigmay_sources.md), the solve contributes 1 %, and an exact solve
            fails the gate by 230x. **Criterion 3 belongs with the species RHS assembly, not
            with TD-003.** That is now measured rather than suspected -- it was flagged as a
            scoping RISK two steps ago and is a scoping FACT now.
```

## 5. WHERE THE RHS INCONSISTENCY COULD COME FROM — candidates, NOT a finding

`sum Y = 1` closes only if `sum_i b_i` is exactly the right-hand side of the mixture equation. The
terms, from `PremixedFlame.cpp` and `speciesTransportAssembly`:

```text
transient    rho Y_i^n V/dt  -> sums to rho (sum_i Y_i^n) V/dt. If sum_i Y_i^n is already
             1 + delta, the term CARRIES delta forward, so a deviation once present PERSISTS
             rather than decaying. This is a feedback path, not a source.
inlet        the Dirichlet composition sums to BIT-EXACTLY 1.0 (measured previously), so it
             cannot inject a deviation.
outlet       zero-gradient. Sums to zero by construction if applied identically per species.
diffusive    the `closure` from speciesDiffusiveFaceFluxes (:1132-:1138). For the sum to close,
             sum_i J_i must be exactly zero at every face. With Le = 1 and one gamma for all
             species, sum_i Gamma dY_i/dx = Gamma d(sum Y)/dx -- zero only if sum Y is already
             uniform. **A correction/drift flux that does not sum to exactly zero would be a
             SOURCE, and this is the one term not yet excluded.**
```

**I am naming these, not concluding.** Four hypotheses have died in this item already — the
Patankar source, the chemistry sum, the recentring fill, and the kappa magnitude — and three of
them died because they were plausible. The direct test is cheap and specific: sum the assembled
`b_i` across species per cell and compare against `rho (sum_i Y_i^n) V/dt` plus the boundary
contribution. The same patch already has the RHS in hand; it needs four more lines.

## 6. WHAT IS NOT ESTABLISHED

```text
NOT established  WHICH RHS term is responsible. Section 5 lists four and excludes two. The
                 diffusive closure is the leading candidate and is NOT measured.
NOT established  that the deviation is injected rather than merely carried. The transient term
                 propagates an existing delta; something must introduce it first, and nothing
                 here identifies that.
NOT established  that 2.301527e-06 is reachable-to-zero at all. A formulation that does not
                 enforce sum Y = 1 may simply have a floor, in which case the 1e-8 gate is
                 mis-derived -- which would be a gate question under section 5.3, not a defect.
NOT measured     the same decomposition on the PRODUCTION reacting path. This ran the FLAME
                 fixture. ProjectRunner's reacting march shares the assembler but not the
                 settings, and its own |sum Y - 1| is reported as maxCompositionSumDeviation
                 in ReactingRunResult without a gate.
NOT re-derived   the 1e-8 bound. It is the repository's own existing number, left untouched.
NOT changed      anything in the repository. The production source hash was verified before the
                 build and the patched copy never entered the tree.
```
