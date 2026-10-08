# TD-004 — ACCEPTANCE GATE (frozen before implementation)

```text
item        TD-004  Canonical premixed-flame capability · P1
scope       route (a) as authorized: make recentring conservative. NOT the eigenvalue method
            (route c), NOT the drifting-frame reformulation (route b), NOT the no-flame early-out
            (TD-020), NOT the tolerance-limited inner solve (TD-003).
audit       audit.md
baseline    logs/01 (flame state) · logs/03 (march ladder) · logs/04 (front diagnostic)
            logs/06 (per-test, 12/12) · logs/07 (refinement unbounded) · logs/08 (fixture selection)
dry-run     logs/05_c1_dryrun_PRE_FREEZE.log -- C1 and its controls N2/N3, against the UNCHANGED
            operator, BEFORE this freeze (CLAUDE.md section 5.2)
frozen at   git HEAD 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
```

**Frozen before any production change to the recentring operator.** No criterion may be weakened to
obtain a PASS (CLAUDE.md section 1.4, section 5.3). Section 5.1 names the three permitted
non-runners; section 3.3 records why C6a exists and section 3.4 records two anomalies deliberately
left ungated.

**Two test-file changes were made BEFORE this freeze and are part of the baseline, not the fix:**
the refinement triplet was shrunk from `{250, 500, 1000}` to `{125, 250, 500}` so C6 could be
evaluated at all (derivation in logs/08, recorded in the test), and
`DISABLED_BalanceVersusMarchLength`'s knob was corrected from the `finalMarchSteps` ceiling to
`finalMarchRelaxationTimes` with a fail-closed guard. Neither touches the operator under test.

---

## 1. The defect, measured

`logs/04` identified it and excluded every alternative:

```text
relaxT  march   fuel bal      x_front  T_peak   offCells  recentr   recentr/march
  10.0   3968   4.327958e-02  3.50000  1800.00     47.50      920      23 %
  30.0  11905   6.343790e-01  3.50000  1800.00    163.96     8857      74 %
  90.0  35717   6.343790e-01  3.50000  1800.00    163.96    32669      91 %
```

The front is **not** pinned (`x_front` is exactly the 3.50 mm anchor at every length) and the flame is
**not** dying (`T_peak` is exactly `T_ad = 1800 K`). What scales with the balance error is the
**recentring count**.

The cause is in the operator. For a shift of `s` cells:

```cpp
if (source < 0)         shifted[c] = upstreamFill;   // fabricates |s| unburnt cells
else if (source >= nc)  shifted[c] = field[nc - 1];  // fabricates |s| burnt cells
```

Each shift **discards** `|s|` cells of state at one end and **fabricates** `|s|` at the other.
Shifting upstream destroys fuel and creates product; downstream does the reverse. The operator's own
comment — *"Translating the whole profile by a WHOLE NUMBER of cells is an exact operation on this
uniform grid -- no interpolation, so it introduces no error of its own"* — is correct about
**interpolation** and wrong about **conservation**, and that is the gap this item closes.

F14 found the same operator leaking and fixed the *rate* with a dead band. That holds at 23 % of
steps; the residual front drift the bisection leaves (`-4.39864e-06`) keeps pushing the front back
out of the dead band, so by 35717 steps it fires on 91 % and the leak returns.

## 2. What "conservative" can and cannot mean here

**It cannot mean the operator leaves the totals unchanged.** The window is finite and translating
over a physically unbounded domain, so material must enter and leave; a shift that changed nothing
would not be a shift.

**It means the operator must report exactly what it moved**, so the conservation balance accounts
for the frame motion instead of attributing it to the chemistry. The solution is unchanged by this;
only the *measurement* of its conservation becomes correct. That distinction is the whole of route
(a), and criterion C1 is written on it.

The ledger is computed as the integral **before** minus **after** each shift, rather than by
reasoning about which cells moved where — exact by construction, `O(nc)` per shift (the same cost as
the shift itself), and it cannot drift out of step with the operator it measures.

## 3. Criteria

Order is execution order; **execution stops at the first failure** (§5.3).

| # | criterion | metric | acceptance threshold |
| --- | --- | --- | --- |
| C1 | **the ledger is exact** | apply recentring to a known state; compare the reported fuel and energy deltas against the directly recomputed integrals | `<= 8 * eps * nc * max abs(integrand)` — round-off for an `nc`-term sum, derived in §3.1. **No fitted constant.** |
| C2 | **the balance no longer degrades with march length** | fuel balance residual at march `3968` vs `35717`, the `logs/03` ladder rerun | `ratio <= 2.0`; measured **14.6577** on the baseline |
| C3 | the balance closes at the reference configuration | `fuelBalanceResidual`, `relaxT = 10` | `< 0.02` — the **existing** test threshold, neither weakened nor tightened |
| C4 | the balance is insensitive to the recentring RATE | fuel balance at `relaxT` 10 / 30 / 90, where the rate is 23 % / 74 % / 91 % | every point `< 0.02` |
| C5 | **what already works is preserved** | `T_peak`, `x_front`, `min Y_i`, `|sum Y - 1|` | `T_peak == 1800.00 K` exactly; `x_front == 3.50000 mm`; `min Y_i >= 0`; `|sum Y - 1| <= 1.2e-06` (the measured baseline band — see §3.2, this is NOT the `1e-8` test gate and the reason is stated) |
| C6 | the eigenvalue still has its qualified properties | properties 2, 3, 5 of audit.md §5 — independence of the initial guess, plateau in anchor and domain, convergence under refinement | each still holds; `S_L` reported with its change from the baseline `4.86230 cm/s` |
| C6a | **the refinement family is RE-DERIVED on CONSERVING states** | rerun `tools/fixture_probe.cpp` after the fix | the `{125, 250, 500}` triplet still admissible (every level converged, every level `> 4` cells of front, differences shrink, no sign change); observed order `p` in **[1.5, 2.5]**; **and the fuel balance at every level `< 0.02`** — see §3.3 |
| C6b | the consumption integral agrees with the eigenvalue | `S_c` vs `mdot/rho_u`, property 4 | agree to the march's own error. Measured pre-fix: **2.15 vs 4.41** at 125 cells, **2.17 vs 4.77** at 250, **4.90 vs 4.86** at 500 |
| C7 | no silent change elsewhere | `CFDSpeciesTests` excluding `G8PremixedFlameTest` | **343 baseline**, unchanged |

### 3.1 C1's threshold, derived

The ledger compares two `nc`-term sums of the same integrand. Floating-point summation of `n` terms
has a worst-case error of `(n-1) * eps * sum|x_i|`, so the difference of two such sums is bounded by
`2 * (nc-1) * eps * max|integrand| * nc` in the crudest form. Rounding up to a clean multiple and
using the quantity actually summed:

```text
C1 bound = 8 * eps * nc * max_cell |integrand|
```

For `nc = 500` that is `8 * 2.22e-16 * 500 = 8.88e-13` relative to the integrand's scale. The factor
8 covers the two sums plus the subtraction with margin; `eps * nc` is the summation floor and is not
adjustable. **The baseline reports no ledger at all**, so its implicit delta is `0` while the actual
integral change is `O(1)` of a cell's content — it fails C1 by many orders, which §4 measures.

### 3.3 C6a — why a re-derivation is required, and where its band comes from

The refinement fixture was shrunk from `{250, 500, 1000}` to `{125, 250, 500}` because the original
did not finish in 1750 s; the selection is measured in `logs/08_fixture_selection.log` and recorded
in the test itself. That change made C6 evaluable — the test now passes in **313 s**.

**But the order it produces is not yet evidence.** Measured pre-fix:

```text
cells   S_L cm/s   res cells   fuel bal      S_c vs S_L
   63    3.88104        4.09     0.8021
  125    4.40773        7.82     0.4703     2.15 vs 4.41
  250    4.77424       13.42     0.5457     2.17 vs 4.77
  500    4.86230       61.45     0.0433     4.90 vs 4.86

{63, 125, 250}    p = 0.523    NOT asymptotic -- excluded by the p criterion
{125, 250, 500}   p = 2.057    asymptotic, close to the 1.93 on record
```

Every level was measured on a solution whose **fuel balance is 47–80 % broken** at the coarse end.
That is the non-conservative recentring this item exists to fix, and it is worse at coarse
resolution because one shift discards `1/125` of the domain rather than `1/500`. **A convergence
study on non-conserving states can produce a tidy order from a sequence of wrong answers**, so
`p = 2.057` is not yet evidence of a correct second-order method. C6a therefore re-derives the
selection after the fix, and its third clause — fuel balance `< 0.02` at every level — is what
makes it more than a repeat of C6.

**The `[1.5, 2.5]` band, derived.** The discretisation is nominally second order, so `p = 2` is the
target. A 3-point estimate of `p` from a halving family is `log2|d1/d2|`, which inherits the relative
error of both differences; the two independent pre-fix estimates of the true order are the record's
**1.93** and the probe's **2.057**, spanning `0.13`. A band of `±0.5` about 2 admits roughly four
times that spread — generous for measurement noise — while rejecting the **0.523** that the
non-asymptotic `{63, 125, 250}` family produced, which sits `3x` outside the lower edge. The band is
therefore wide enough not to fail a correct implementation and narrow enough to reject a family that
has left the asymptotic range, which is the only discrimination it is asked to make.

### 3.4 Two anomalies REPORTED but deliberately NOT gated

Recording these rather than gating them, because a criterion written on either could fail a correct
implementation.

**The flame thickness is non-monotonic**: `0.650 → 0.625 → 0.537 → 1.229 mm` across `63 → 125 → 250
→ 500` cells, a `2.3x` jump at the last refinement. A thickness that more than doubles under
refinement is not a resolved quantity converging. **F16 established that the flame has two scales** —
a preheat zone and a reaction zone — so the coarse levels may be resolving only the thinner one, in
which case the behaviour is real and a monotonicity criterion would be wrong. It also means the
`> 4` cell resolution guard is computed from a thickness that is itself under-resolved at the coarse
levels, so the guard is partly circular. Both facts are stated so a later reader does not mistake
the guard for a resolution proof.

**`S_c` disagrees with `S_L` by `2x` at the coarse levels.** This is gated, as C6b — the identity
`S_c == mdot/rho_u` is exact for a converged conservative discretisation, so a `2x` mismatch is the
leak appearing in the one quantity designed as a sharp consistency check. It agrees at 500 cells
(`4.90` vs `4.86`) precisely because that is where the balance is least broken.

### 3.2 Why C5's `|sum Y - 1|` band is the measured baseline, not the `1e-8` test gate

The existing test asserts `maxCompositionSumDeviation < 1e-8` and the baseline measures
`6.29e-07 .. 1.10e-06` — so that test is **already failing**, before TD-004 changes anything, and
`logs/03` shows the value creeping with march length while `worstLinearResidual` sits pinned at
exactly the `1e-9` acceptance fraction. That is the **tolerance-limited solve** of audit.md §4.4,
which is TD-003's territory and is **not** route (a).

So C5 gates `|sum Y - 1|` against the **measured baseline band** `1.2e-06`, to catch route (a) making
it worse, and does **not** claim to fix it. Writing `1e-8` here would make TD-004 fail on a defect it
was not authorized to repair; writing nothing would let a regression through. The `1e-8` test gate
stays in the suite, stays failing, and stays attributed to TD-003 — it is **not** relaxed.

## 4. Negative controls

| id | control | must produce |
| --- | --- | --- |
| N1 | the **unchanged** recentring operator (no ledger) | **C1, C2 and C4 FAIL** — this is the measured baseline: `ratio 14.6577`, balance `6.34e-01` at 91 % recentring rate |
| N2 | a ledger with the **sign flipped** | **C1 FAILS** — proves C1 checks the ledger's value and not merely its presence |
| N3 | a ledger that accounts only for the **upstream** fill | **C1 FAILS** on a downstream shift — proves both fill paths are covered, since one of them is the only one a one-sided test would exercise |
| N4 | the dead band set so recentring never fires (`recentreDeadBandCells` huge) | **C2 PASSES VACUOUSLY** — recorded to show that C2 alone could be satisfied by disabling the operator rather than fixing it, which is why **C5 and C6 are mandatory alongside it**: with recentring off the front leaves the domain and `x_front`/`T_peak` fail |
| N5 | recentring forced on **every** step (dead band `1.0`) | **C4 still PASSES** — the strongest statement available: the balance holds at a 100 % recentring rate, not merely at the 91 % the baseline reaches |

| N6 | the **pre-fix** measurement, i.e. the baseline, judged against C6a | **C6a FAILS** — the fuel balance is `0.4703` and `0.5457` at two of the three levels, both above `0.02`, so the order was measured on non-conserving states. This is what makes C6a's third clause load-bearing rather than decorative |
| N7 | the `{63, 125, 250}` family | **C6a FAILS** — `p = 0.523`, `3x` outside the `[1.5, 2.5]` band. Proves the band rejects a family that has left the asymptotic range |

**N2 and N3 are the two that matter.** C1 is a self-consistency check between two computations of the
same quantity, and the way such a check goes vacuous is by comparing a thing to itself. N2 and N3 are
what prove it does not.

**N4 is recorded because it is the cheap wrong fix.** Disabling recentring makes C2 pass and destroys
the solution; it is the reason C2 may never be read alone.

## 5. Regression scope

| suite | reason | expectation |
| --- | --- | --- |
| `CFDSpeciesTests` excluding `G8PremixedFlameTest` | the library TD-004 must not disturb | **343**, unchanged |
| `G8PremixedFlameTest` | the suite being repaired | reported test by test, before and after. **Most currently FAIL** — that is the starting state, and each one's movement is accounted for individually rather than as a count |
| `CFDIoTests`, `CFDAppLayerTests` | the flame reaches neither | **331 / 32**, unchanged |

`G8PremixedFlameTest` is reported per test rather than as a total, because a count would hide one
test improving while another degrades.

### 5.1 A NAMED, PERMITTED NON-RUNNER: `WithReactionsDisabledNoFlamePropagates`

Measured on the unchanged tree (`logs/06`): **TIMEOUT after 900 s**. It is named here because
`CLAUDE.md` §8 permits only the failures a gate names, and an unnamed timeout would become an
unnamed failure at closeout.

```text
MalformedConfigurationsAreRefused                      PASS      0s
TheBurntCompositionConsumesTheFuelExactlyAndConserves  PASS      0s
TheObviousChecksAreVacuousOnTheSeededProfile           PASS      0s
WithReactionsDisabledNoFlamePropagates                 TIMEOUT   900s   <-- property 6 / G8 control 1
ItConvergesAndReachesTheIndependentlyDerivedAdiabatic  FAIL      237s
TheConsumptionIntegralAgreesWithTheEigenvalue          FAIL      236s
TheAnswerIsInsensitiveToTheStepsPerTrial               PASS      744s
TheEigenvalueIsIndependentOfTheInitialMassFluxGuess    PASS      561s   <-- property 2
TenTimesThePreExponentialMovesTheFlameSpeedFar         PASS      804s   <-- G8 control 2
AVariableConductivityChangesTheAnswerAndIsNotFrozen    FAIL        0s   <-- property 8
TheEigenvaluePlateausInTheAnchorPositionAndTheDomain   TIMEOUT   900s   <-- property 3
TheFlameSpeedConvergesUnderMeshRefinement              TIMEOUT   900s   <-- property 5

6 PASS · 3 FAIL · 3 TIMEOUT
```

**THE DRAFT NAMED ONE TIMEOUT; THE COMPLETED BASELINE HAS THREE.** That correction is recorded
rather than quietly applied, because the draft was written after measuring only the first six rows
and it would have treated the other two timeouts as unnamed failures — which §8 forbids.

The three are not interchangeable:

```text
property 6  WithReactionsDisabledNoFlamePropagates   G8 NEGATIVE CONTROL 1   -> TD-020
property 3  TheEigenvaluePlateaus...                 the cold-boundary check
property 5  TheFlameSpeedConvergesUnderMeshRefinement G8's OWN criterion for profiles
```

**Property 5's timeout is the serious one.** It is the study that produced the record's observed
order `p = 1.93`, Richardson `4.894 cm/s` and `GCI 0.21 %` — numbers `audit.md` §2 cites as
"established by measurement". If that study cannot run, **the convergence evidence cannot currently
be regenerated**, and §4's finding that the prior record is not a usable baseline extends to it.

**Consequence for C6, stated plainly: C6 as drafted cannot be evaluated.** It cites properties 2, 3
and 5; only property 2 runs. Three resolutions exist and they are not equivalent — reducing C6 to
property 2 would be weakening it, raising the cap would be untested unless the tests actually
finish, and treating the timeouts as a prerequisite blocks TD-004 on work outside route (a). The
disambiguating measurement (`logs/07`) establishes which is available by testing whether property 5
finishes at all given 1750 s. **That was measured (logs/07, logs/08) and resolved by shrinking the fixture; C6a is the consequence.**

**What it is.** Property 6 of audit.md §5 — *reactions off ⇒ no flame* — and **G8 negative
control 1**. So TD-004 closes with one of G8's two negative controls **unexercised**, and that is
stated as a limitation of what this item ships rather than left for a reader to discover.

**Why it does not run.** The test raises `maxOuterIterations` from the fixture's 40 to **2000**. With
reactions off the seeded tanh front does not vanish at once — it washes out slowly, so early trials
still locate a front and return a sided verdict, the bracket proceeds, and the search grinds through
its raised budget at up to `maximumStepsPerTrial = 20000` steps per trial. The
`!sided -> NoFlame` short-circuit exists but only fires once the front is already gone. **There is no
early-out for "this mixture cannot sustain a flame", so the no-flame case costs MORE than the flame
case rather than less.**

**Why it is not fixed here.** That is a defect in the eigenvalue search's termination, not in the
recentring operator, and route (a) is the recentring operator. Bolting a termination fix onto this
gate would put two unrelated changes behind one set of criteria. Recorded as **TD-020** instead.

**What this gate therefore requires of it:** that it still TIMEOUT, at the same 900 s cap, and that
no other test in the suite acquires a timeout. A test that began timing out would be a regression;
this one timing out is the baseline.

## 6. Definition of done

1. C1–C7 all PASS, measured, with the numbers recorded;
2. N1–N5 each demonstrated to behave as the table requires;
3. the regression expectations met, every delta enumerated;
4. `git diff --check` adds no warning beyond TD-018's three CRLF files;
   4a. `WithReactionsDisabledNoFlamePropagates` still times out at 900 s and no OTHER test in the
       suite has acquired a timeout (§5.1);
5. evidence written: `audit.md`, `GATE.md`, `reproducer.md`, `implementation.md`,
   `negative_controls.md`, `verification.md`, `regression.md`, `summary.md`;
6. `DEBT_REGISTER.md` updated **after** the above.

## 7. Explicitly NOT in this gate

* **the eigenvalue method and its 4.3 % reproducibility** — route (c), and TD-005's to freeze. `S_L`
  may move when the leak is fixed, because the leak was present in the trials too; C6 reports that
  movement rather than forbidding it.
* **the drifting-frame reformulation** — route (b).
* **the tolerance-limited inner solve** (`worstLinearResidual` pinned at `1e-9`, `|sum Y - 1|` at
  `6.29e-07` against a `1e-8` test gate) — **TD-003**, see §3.2.
* **methane, `cp`, `lambda(T)`, and any absolute `S_L` claim** — TD-006. Every criterion here is on
  the **synthetic** fuel, because debugging against methane would mean deciding with G8's gated
  answer visible.
* **the no-flame early-out** — **TD-020**, recorded from this item's own baseline (§5.1). G8
  negative control 1 is left unexercised and that is stated, not hidden.
* the implicit Jacobian — TD-019.
* reacting shocks, detonations, supersonic combustion, 3D combustion — the register's own scope bound.
* any performance claim.
