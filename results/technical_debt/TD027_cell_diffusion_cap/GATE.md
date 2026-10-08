# TD-027 — ACCEPTANCE GATE — validating the cell-diffusion resolution cap

```text
item        TD-027   maximumCellDiffusionNumber = 20.0 is an unvalidated resolution cap · P0
authorized  2026-10-05 "authorize TD-027"; cap value chosen 2026-10-05 = 5.0
audit       audit.md (read first: §1 corrects TD-005's D figures, §3 shows D is independent of dx)
status      DRAFT until §6's dry-run is recorded and §7 the freeze hash.
```

---

## 1. The change under test

```text
include/cfd/species/PremixedFlame.hpp
   Real maximumCellDiffusionNumber{20.0};        ->        {5.0}
```

One default. Its effect is not small: `dt = cap * dx^2 / alpha_max` and the diffusion limit binds at
the eigenvalue, so **`D` is identically the cap** (audit §1) and `dt` falls by 4x for every
premixed-flame solve.

**Why 5.0 and not another value.** Measured (TD-005 `logs/18`, the only `dt`-only self-similar
family): the error leaves the asymptotic range between `D = 20` and `D = 10` — it changes sign — and
within the asymptotic family `{10, 5, 2.5}` the error is `-0.422 %`, `-0.084 %`, `-0.017 %`.
`cap = 5` is a factor of 2 inside the measured asymptotic boundary, and §3.2's threshold is the one
it was chosen to meet. `cap = 2.5` would breach the repository's 7200 s per-test cap (audit §5), and
`cap = 1` — the textbook *"`dt` within one cell-diffusion time"* condition — is ~20x cost and
infeasible at this fixture. The choice was put to the user and `5` was selected.

## 2. The converged reference, fixed before any criterion uses it

```text
Richardson limit over {dt/2, dt/4, dt/8}, p = 2.321:   S_L = 4.67589 cm/s
GCI on the finest pair:                                0.021 %
```

From TD-005 `logs/18`. **This is the reference `S_L` for the synthetic fixture**, and it replaces
`4.86230` — which was measured at `D = 20`, outside the asymptotic range, and is 3.99 % high.

## 3. Criteria

| # | criterion | instrument | threshold | derivation |
| --- | --- | --- | --- | --- |
| **C1** | **the cap IS the realized cell diffusion number** | recompute `alpha_max*dt/dx^2` from reported fields | equals the cap to within `1e-12` relative | §3.1. `dt = cap*dx^2/alpha` by construction, so the recomputation is an identity; the tolerance is round-off on one divide and multiply |
| **C2** | **`S_L` is within the temporal-error budget of the converged reference** | one solve at the new default | `|S_L - 4.67589|/4.67589 <= 0.28 %` | §3.2. G8's band is `±2.80 %`; the budget is **one tenth** of it, an explicitly **empirical** margin so the time discretisation is not a material contributor to a gated comparison |
| **C3** | **the reported precision is coherent with the discretisation** | `massFluxResidual` vs C2's measured error | `error / (sqrt(1+tolerance)-1) <= 2` | §3.3. A reported uncertainty that understates the true error by more than 2x is a materially misleading precision claim; within 2x the two are the same order |
| **C4** | **the limit cycle is suppressed: the march converges** | relaxT `{18, 22, 26, 30}` at the new default | `(max - min) <= mean` on `fuelBalanceResidual` | §3.4. The same **constant-free** settling test as TD-004 `GATE_A1.md` C2-A1(c): a quantity varying by more than its own mean has not settled |
| **C5** | **no silent change outside the flame** | `CFDSpeciesTests` minus `G8PremixedFlameTest`; `CFDIoTests`; `CFDAppLayerTests` | **343 / 331 / 32**, unchanged | the default is reachable only from `PremixedFlameSettings`; nothing else constructs one |
| **C6** | **no test breaches the repository's per-test cap** | every `G8PremixedFlameTest` case at `ctest --timeout 7200` | each produces a **verdict**; the longest `<= 7200 s` | §3.5. The cap is `ci.yml:147`, per gtest case. Predicted longest 3640 s. **`WithReactionsDisabledNoFlamePropagates` is a named exception** — see §5.2 |

**Execution stops at the first failure.**

## 4. Derivations

### 4.1 C1 — why an identity is worth gating

The audit's central correction was that `D` is not a loosely-respected bound but *is* the cap, and
that I had computed it with the wrong diffusivity for several sections of TD-005. C1 makes the
recomputation an explicit criterion so the same mistake cannot pass silently: if `alpha_max` is taken
on the wrong side, or `dt` is set by the Courant branch instead, C1 fails and says so. The threshold
is round-off because the relation is exact by construction.

### 4.2 C2 — the budget, and the one empirical factor

G8 judges `S_L` against `35.0–37.0 cm/s`, i.e. `±2.80 %`. The requirement is that the **time
discretisation not be a material contributor** to that comparison, which needs the error to be a
small fraction of the band rather than merely inside it. **One tenth is an engineering margin and is
labelled as such** (CLAUDE.md §5.1 permits a labelled empirical constant). It yields `0.28 %`.

**Non-vacuity, from measured data:** `cap = 5` gives `0.084 %` and **passes** with 3.3x margin;
`cap = 10` gives `0.422 %` and **fails**; `cap = 20` gives `3.99 %` and fails by 14x. So the
criterion discriminates between the candidate caps, which is exactly what it is for.

### 4.3 C3 — the incoherence this item exposed

TD-005's C2 bounds the eigenvalue's reported uncertainty at `sqrt(1+tolerance) - 1`, which is
`5.0e-04` (`0.05 %`) at `tolerance = 1e-3`, and the solver publishes it as `massFluxResidual`. At
`cap = 20` the temporal error is `3.99 %` — **80x** the published precision. A result that reports a
bracket far finer than its discretisation error is making a claim it cannot honour.

```text
cap = 20   error 3.99 %  / 0.05 % =  80x    incoherent
cap = 10   error 0.422 % / 0.05 % = 8.4x    incoherent
cap =  5   error 0.084 % / 0.05 % = 1.7x    PASSES
```

Factor 2 is the boundary because within it the two quantities are the same order and the report is
not misleading. **Labelled empirical.** This criterion is the second reason `cap = 5` was chosen over
`10`, and it is gated rather than merely noted so the choice is held.

### 4.4 C4 — the limit cycle, and reusing a constant-free test

TD-028's limit cycle is governed by this cap (TD-005 §N): at `D = 0.833` in the *old* convention —
`D = 5` in the code's — four march lengths over a 1.67x range agreed to 2 %. C4 requires that
directly, with **the identical settling clause frozen in TD-004's `GATE_A1.md`**: `(max-min) <= mean`.
Using the same form in both places is deliberate — one derivation, no second constant, and the two
items then fail or pass together on the same property.

**The family starts at relaxT 18, not 10.** TD-005 `logs/19` measured `relaxT 10` and `14` at
`D = 5` as a **startup transient** (`8.6e-02` and `4.4e-01`, with `delta` `0.99` and `0.55` mm) that
the march passes through and does not return to. A settling test must be applied to the settled
part; including the transient would test convergence against a state that has not begun to converge.
**This is stated here because it is the one place C4 could be accused of being chosen to pass**, and
the justification is a measured transient, not a convenient window.

### 4.5 C6 — the cost is a criterion, not a footnote

`cap = 5` quadruples the step count. Predicted from the measured `D = 20` timings (audit §5):

```text
suite total   3718 s (62 min)  ->  ~14 872 s (4.1 h)
longest test    910 s          ->  ~3 640 s        against the 7200 s per-test cap
```

C6 gates the **cap**, not the total, because `ctest --timeout 7200` is per gtest case. **A timeout
is a FAILURE of this gate**, and the forbidden repairs are explicit: raising the repository cap,
reducing `maxOuterIterations`, coarsening the fixture, or splitting a test to dodge the cap while
measuring less. Permitted: splitting a test into parts that together measure exactly what the
original did.

## 5. Scope and named exceptions

### 5.1 What this gate does NOT cover

```text
* TD-028 is expected to RESOLVE as a consequence, and C4 measures that -- but TD-028 keeps its own
  register entry and is not closed by this gate
* TD-030 (the chemistry stiffness / first-order split) is untouched. The 8.48e-02 reaction-integral
  residual measured at D = 5 is TD-030's, and C4 deliberately tests SETTLING rather than
  admissibility for that reason: requiring `< 0.05` here would import TD-030's defect
* TD-029's two cancelling tests. The mesh-refinement test will still have a non-self-similar
  family after this fix; the fix removes the temporal error's magnitude, not the coupling
* the spatial discretisation. Only dt moves here
```

### 5.2 NAMED, PERMITTED NON-RUNNER

```text
WithReactionsDisabledNoFlamePropagates   UNMEASURED at 7200 s before this change (TD-020), and it
                                         scales with 1/dt like everything else. It is named as a
                                         permitted non-runner, and if it times out at 7200 s after
                                         the change that is TD-020's cost problem made worse, NOT a
                                         new failure of this gate. It must be REPORTED either way.
```

**No OTHER test may breach the cap** — that is C6.

### 5.3 THREE FROZEN GATE FILES CARRY VALUES THIS FIX INVALIDATES

```text
TD-004 GATE.md      C6's baseline S_L 4.86230; C5's x_front 3.50000 (now a measured quantity)
TD-004 GATE_A1.md   C2-A1's family is sized to a 14-relaxation-time period measured at D = 20
TD-005 GATE.md      C4b/C4c spreads and C5's control values were measured at D = 20
```

**None may be edited.** Each needs its own amendment under CLAUDE.md §5.3, with its own derivation,
dry-run and authorization — exactly as TD-004's C2 required. **This gate does not amend them and
must not be read as doing so.** TD-004 and TD-005 both remain OPEN / STOPPED independently of
TD-027's outcome.

## 6. Dry-run against the unchanged baseline

| # | on the unchanged tree (`cap = 20`) | non-vacuity | source |
| --- | --- | --- | --- |
| C1 | `D = 20.000` recomputed, equals the cap | fails if `alpha_max` is taken on the wrong side, or the Courant branch binds | audit §1, verified arithmetically |
| C2 | **FAILS** — `3.99 %` against `0.28 %` | passes at `cap = 5` (`0.084 %`), fails at `10` (`0.422 %`) and `20` | TD-005 `logs/18` |
| C3 | **FAILS** — `80x` against `<= 2x` | passes at `cap = 5` (`1.7x`), fails at `10` (`8.4x`) | derived from `logs/18` + TD-005 C2 |
| C4 | **FAILS** — at `cap = 20` the relaxT family is `6.751e-01 / 1.045e-02 / 1.093e-02 / 6.022e-01`, so `max-min = 6.647e-01 > mean 2.208e-01` | passes at `cap = 5`: `logs/19` rows 18–30 give `max-min = 1.82e-03` against `mean = 8.51e-02`, by 47x | TD-004 `logs/A1_01`, TD-005 `logs/19` |
| C5 | PASS — 343 / 331 / 32 | — | TD-005 `logs/03`, `logs/08` |
| C6 | PASS at `cap = 20` (longest 910 s) | fails if any case breaches 7200 s | TD-005 `logs/08` |

**Every criterion has a real pass case and a real fail case from measured data.** C2, C3 and C4 all
fail on the unchanged tree — they are the defect — and all three are demonstrated to pass at
`cap = 5` on data measured before this gate was written.

**Verification must be FRESH** (CLAUDE.md §5.3). The table above establishes non-vacuity, which is
what a dry-run is for; the verdicts come from new runs recorded under `logs/`.

## 7. Freeze

```text
NOT YET FROZEN. This file carries no hash until §6 is complete and logs/00_freeze.log is written.
No production file may be modified before that record exists.
```

## 8. Definition of done

1. C1–C6 all PASS, measured fresh, numbers recorded;
2. the regression expectations of C5 met, every delta enumerated;
3. §5.2's named non-runner reported, whichever way it falls;
4. the three frozen files of §5.3 **left unmodified**, and the amendments they now need recorded as
   open work rather than performed here;
5. evidence written: `audit.md`, `GATE.md`, `implementation.md`, `verification.md`, `regression.md`,
   `summary.md`;
6. `TECHNICAL_DEBT.md` and `TODO.md` updated **after** the above, including the consequence for
   every recorded `S_L` figure in the repository.
