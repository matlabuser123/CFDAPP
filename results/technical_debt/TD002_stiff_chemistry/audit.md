# TD-002 — AUDIT

```text
item        TD-002  Stiff / bounded chemistry integration · P0
origin      P18.4, P18.5, P18.6 (the `Sp` chain) + P18.6, P18.7 (the step-budget chain); KL 1.2
register    results/technical_debt/DEBT_REGISTER.md section 3
dependency  TD-001 (RESOLVED 2026-10-03) -- a production path to integrate in
blocks      TD-004, TD-006
probe       tools/drift_audit.cpp   sha256 60b2fd42419b96a451285d1338e45822f2b46d16c45df61c579f98199b2eb3a9
log         logs/01_drift_audit.log
library     build/release/src/libcfdcore.a sha256 0a29fd9821bc84c0b107e52bf4c50c9ee9947a7046c6efbd529d8fc6c443a7ce
status      INVESTIGATION ONLY -- no production file changed. The gate is not yet frozen.
```

---

## 1. What the register claims, and the contradiction that had to be resolved first

The register's measured fact:

> explicit first-order march; `sum Y = 1` error grows **linearly at ~0.17 eps/step** and crosses
> `4*eps*N` at **~72 steps** for 3 species, sooner for fewer. `d(omega_i)/d(Y_j)` does not exist, so
> the Patankar `Sp` is zero.

Three facts in the tree contradict that as a description of the code **today**:

1. `src/species/HeatRelease.cpp` (P18.6's own update) states that it applies the increment in
   mass-fraction space, `Y_i + dt*omega_i/rho`, rather than forming `rho Y_i`, adding `dt omega_i`
   and dividing back — *"That round trip is not exact, and over 200 steps it drifted sum_i Y_i to
   0.99999999999999722"* — and concludes *"the sum-to-one error does not accumulate"*.
2. `MassFractionState`'s constructor enforces `4*eps*N_species` strictly, per cell
   (`massFractionSumTolerance`, "frozen. Not derived here and not adjustable"), and
   `advanceClosedReactor` returns a `ReactorState` that **contains** one. So every sub-step
   revalidates the invariant.
3. The in-flight `ChemistryIntegrator` marches **284 sub-steps** through that path without throwing.

A ~72-step budget and 284 clean sub-steps cannot both describe the same quantity. The probe decides
it.

## 2. Measured

### 2.1 The reference problem does not exercise the drift at all

P18.7's problem — `A -> B`, two species, equal dyadic weights `M = 0.0625`, `Y = {1, 0}` — through
the real `integrateChemistry`:

```text
     dt    sub-steps   sum(Y)-1        in eps
    1.0            7   -1.110223e-16    -0.50
    5.0           59   +0.000000e+00     0.00
   20.0          284   +4.440892e-16    +2.00
  100.0         1485   +0.000000e+00     0.00
  500.0         7521   +0.000000e+00     0.00
```

**7 521 sub-steps at zero drift.** The reason is structural, not luck: `Y_A` decays exponentially to
`2.47e-323` and `Y_B` rises to ~1, so for almost the whole march one addition is `tiny ± tiny`
(exact) and the other is `1 ± tiny` (exact at that exponent). The reference problem is **degenerate
for round-off accumulation**.

Adding an inert carrier — `A = 0.25, B = 0, N2 = 0.75`, the shape TD-001's production case actually
uses — does not change that: **8 797 sub-steps, 0.00 eps**, with `Y_N2` never moving from `0.75`.

So the register's budget is **not reproducible on the problem the suite integrates**, and that is why
284 sub-steps pass. It is not evidence that the budget is gone.

### 2.2 The budget is real, and the current integrator hits it — by asking for MORE accuracy

Same physical time (`dt = 20`), same problem, varying only `maximumFractionalChange`, which is the
accuracy knob:

```text
   control   sub-steps   sum(Y)-1        in eps
  0.400000          64   +0.000000e+00     0.00
  0.200000         136   +4.440892e-16    +2.00
  0.100000         284   +4.440892e-16    +2.00
  0.050000         584   +1.110223e-15    +5.00
  0.012500   THREW -- sum to 1.000000000000002, deviation 1.9984e-15 > 1.7764e-15
  0.003125   THREW -- same
```

**The failure mode is perverse and that is the finding.** The drift rises with the sub-step count
(0, 2, 2, 5 eps at 64, 136, 284, 584 sub-steps), crosses `4*eps*2 = 1.776e-15`, and
`MassFractionState` then refuses the state — so **requesting a more accurate integration makes the
integrator throw**. An accuracy control that cannot be tightened is not an accuracy control.

This is the reproducer, and it is reached through the real `integrateChemistry` entry point with
nothing but a settings value changed.

### 2.3 The drift in isolation: linear, and removed EXACTLY by compensated summation

With increments that cancel **exactly** (`d[0] = -delta`, `d[1] = +delta`, so `sum(d) = 0` with no
round-off of its own) and a `delta` deliberately not representable at the species' exponent:

```text
                                        72 steps      1 000        10 000       100 000
  2 species, plain                      -15.00 eps   -210.00     -2 095.50    -20 759.00
  2 species, Kahan-compensated            0.00 eps      0.00          0.00          0.00
  3 species + inert carrier, plain        +3.00 eps    +40.00       +405.00     +4 241.00
  3 species + inert carrier, compensated   0.00 eps      0.00          0.00          0.00
```

* The drift is **linear in step count** — `0.21 eps/step` for the 2-species case, `0.042 eps/step`
  with the carrier — which brackets the register's `~0.17 eps/step`. The register's rate is
  confirmed; it simply depends on the magnitudes involved.
* **It is not a closure defect in `omega`.** The increments sum to exactly zero and the drift still
  appears. It is the rounding of the `N` separate additions `fl(Y_i + d_i)`, whose errors do not
  cancel and are sign-biased — which is why it accumulates linearly instead of random-walking.
* **Carrying the low-order bits each addition cannot represent removes it exactly** — `0.00 eps`
  after 100 000 steps, in both configurations.

## 3. What this means for the fix

Compensated summation is **not** on TD-002's forbidden list and is not a disguised member of it:

```text
forbidden            what it does                        what compensation does
clipping             overwrites an out-of-range value    never inspects or alters a value
silent normalisation rescales Y so the sum is 1          never touches the sum; sum(d) = 0 is used, not imposed
loosening the tol    accepts a larger deviation          the deviation becomes exactly 0
```

It discards nothing. The bits that a plain `Y_i + d_i` throws away are carried into the next
increment, so the species' own trajectory is more accurate too — the sum-to-one property follows from
`sum_i d_i = 0` and the absence of loss, rather than being enforced.

## 4. Two things the register bundles that are NOT the same problem

The register's measured fact names the step budget **and** the missing `d(omega_i)/d(Y_j)` in one
breath, and the item is titled "stiff / bounded chemistry integration". The audit separates them:

| | what it is | state |
| --- | --- | --- |
| **the budget** | round-off accumulation in the update, linear in step count | the defect measured above; a round-off fix, not a stiffness fix |
| **stiffness** | needing an implicit method, hence `d(omega)/dY` and a Newton solve | still absent. For the one-step irreversible mechanism the suite freezes, the sub-stepped explicit march is adequate: accuracy against P18.7's mpmath reference improves by refinement, `0.0114 -> 0.00345 -> 0.00119` for controls `0.2 -> 0.05 -> 0.0125` |

The register's own gate requirement is entirely about the budget — *"long-time integration beyond the
~72-step budget, no negative `Y_i`, `sum Y_i` inside the frozen P18.1 invariant, zero-reaction and
zero-heat-release limits, determinism"*. **No implicit integrator is implied by it**, and the audit
finds no measurement that demands one for the mechanisms in scope. Building a Jacobian and a Newton
solve on speculation would be a separate, larger, separately-gateable piece of numerics.

## 5. In-flight work, and what it is NOT credited with

`include/cfd/species/ChemistryIntegrator.hpp`, `src/species/ChemistryIntegrator.cpp` and
`tests/unit/species/test_chemistry_integrator.cpp` (9 tests, all passing) exist from pre-campaign
in-flight work and are recorded in `DEBT_REGISTER.md` §6 as **not credited against any TD item**.
That disposition stands, and §2.2 is why:

* the integrator **throws** on the same problem once the accuracy control is tightened past
  `0.0125`;
* its own multi-sub-step test asserts `EXPECT_NEAR(a + b, 1.0, 1.0e-14)` —
  `test_chemistry_integrator.cpp:203` — which is **5.6x looser** than the `4*eps*2 = 1.776e-15` that
  `MassFractionState` enforces, and loosening the `sum Y` tolerance is on TD-002's forbidden list.
  The test is green because the tolerance was written to the behaviour;
* its accuracy test (`TighteningTheStepControlReducesTheErrorAgainstTheReference`) uses controls
  `0.2 / 0.05 / 0.0125` and passes **only because it marches `dt = 1.0`**. At `dt = 20` the same
  `0.0125` throws. The suite narrowly misses its own defect.

So the sub-stepping machinery is reusable, and the drift fix is what TD-002 has to add to it.

## 6. A reachability question TD-002 does not answer, raised so it is not left silent

TD-001 recorded that a multi-step march is not production-reachable because
`advanceClosedReactor` consumes a `MassFractionState`, whose constructor enforces `4*eps*N`, and a
**transported** composition closes only to the linear solver's tolerance (measured `5.72e-08`).

Fixing the drift does **not** fix that. The two are independent:

* the drift fix makes a long march *valid* once started;
* the raw-fields entry point is what lets a march *start* from a transported composition.

The register's TD-002 gate does not mention production wiring, and `CLAUDE.md` §9 plus
`capability-vs-reachability` both say a library-only fix is not shipped capability. This is therefore
an explicit scoping decision rather than something to assume, and it is put to the user in §7.

## 7. Proposed scope for the gate

**In scope** — the register's own gate requirements:

1. a long march, far beyond the measured budget, with `sum_i Y_i` inside the frozen `4*eps*N` at
   every step and no negative `Y_i`;
2. the accuracy control tightenable without failure — the §2.2 reproducer must stop reproducing;
3. accuracy still improving under refinement against P18.7's independent mpmath reference;
4. the zero-reaction and zero-heat-release limits unchanged;
5. determinism — identical input, identical bits;
6. negative control: the plain (uncompensated) update must FAIL the long-march criterion;
7. a drift-exercising problem, because §2.1 proves the reference problem is degenerate and a gate
   written on it alone would be **vacuous**.

**Out of scope, stated so the item stays closable** — an implicit integrator and `d(omega)/dY` (§4);
the raw-fields reactor entry point and production wiring (§6); TD-003's linear-solver audit;
TD-004/005/006.

## 8. Next required user decision

```text
(a) freeze the gate on section 7 as proposed -- the drift fix, library-level, with the production
    wiring left to a later item; or
(b) widen TD-002 to include the raw-fields reactor entry point and the ProjectRunner wiring, so a
    multi-step reacting march is reachable from a case file when TD-002 closes.
```

(b) is the larger item and the one that makes TD-002 *shipped* rather than *available*; (a) is what
the register's own gate text describes. The audit recommends **(b)** on the strength of
`capability-vs-reachability` — a feature no case file can reach is not shipped, and TD-002's whole
purpose is to let a reacting case be integrated far enough to validate — but the register wrote (a),
so the choice is the user's.
