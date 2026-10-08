# TD-027 — ACCEPTANCE GATE, AMENDMENT A1 — naming the flame suite's verdict movements

```text
item        TD-027   maximumCellDiffusionNumber = 20.0 is an unvalidated resolution cap · P0
amends      GATE.md sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75
            by ADDING criterion C7. Every existing criterion stands unchanged.
authorized  2026-10-06, "close TD-027 with an amendment naming the two failures"
status      DRAFT until §5's dry-run is recorded and §6 the freeze hash.
```

**What this amendment does and does not do.** It adds the accounting criterion the original gate
lacked. It **changes no threshold, in this gate or in any test**, and it does **not** make the two
failures acceptable — they are real failures owned by TD-029 and TD-030, both of which stay open.
What it does is make the gate's accounting complete, so that CLAUDE.md §8's rule — *"the only
permitted failures are those the gate names"* — can be satisfied truthfully rather than by omission.

---

## 1. The gap being closed

`GATE.md` gated the counts **outside** the flame (C5) and the per-test **cap** (C6), and wrote no
criterion for the flame suite's own verdicts. That omission was not neutral: the whole point of the
fix is that several flame tests were passing for the wrong reasons, so requiring their verdicts
unchanged would have been **wrong**. But TD-005's gate faced the same situation and handled it —
*"reported test by test, before and after. Most currently FAIL — that is the starting state, and
each one's movement is accounted for individually rather than as a count"* — and I did not carry
that clause over.

The consequence, found at verification: two cases moved `PASS -> FAIL` with nothing in the gate
naming them.

## 2. Criterion C7-A1

| # | criterion | instrument | threshold | derivation |
| --- | --- | --- | --- | --- |
| **C7** | **every flame-suite verdict movement is NAMED, with its cause and the item that owns it** | `G8PremixedFlameTest`, per case, against the cap-20 baseline | each case's verdict is either **unchanged**, or appears in §3's table with a cause and an owning register item. **Any movement not in that table fails C7** | §2.1 |

### 2.1 Why this is a criterion and not a note

A list of named failures is only worth anything if something checks that the list is **complete**.
C7's threshold is exactly that: the set of movements must equal the named set. It fails if a verdict
moves that §3 does not name, which is the realistic failure mode — a later change quietly adding a
failure to a suite that already has several. It carries **no numerical threshold at all**, so there
is nothing in it to weaken.

**What C7 explicitly does NOT assert:** that the named failures are acceptable, harmless, or
resolved. Each is attributed to an open item, and §4 states what closing them requires.

## 3. The named movements, complete

Measured in `logs/03`–`logs/06` at cap 5, against TD-005 `logs/08` at cap 20.

| case | movement | cause | owned by |
| --- | --- | --- | --- |
| `ItConvergesAndReachesTheIndependentlyDerived…` | **PASS 256 s -> FAIL 812 s** | `fuelBalanceResidual 0.093590` against `< 0.02`. The **converged** value is `0.0847`, which still fails by 4.2x — so this is not the march length. At cap 20 the march stopped in a good phase of the limit cycle and read `1.53e-02`; suppressing the cycle exposed the true steady residual | **TD-030** |
| `TheFlameSpeedConvergesUnderMeshRefinement` | **PASS 301 s -> FAIL 968 s** | refinement differences **grow**, `0.00653 -> 0.151`, so `fine < coarse` fails. The test passed only while the temporal error cancelled the spatial one; with the temporal error 48x smaller the spatial convergence is exposed and is not converging | **TD-029** |
| `TheEigenvaluePlateausInTheAnchorPositionAnd…` | FAIL 910 s -> FAIL 1734 s, **now 2 anchors** | front off by `32.4` and `35.6` cells against a 10-cell tolerance. `logs/19` measures `-34.25` cells at relaxT 10 at this resolution: the reported state is a startup transient | **TD-031** |
| `TheConsumptionIntegralAgreesWithTheEigenvalue` | FAIL -> FAIL, **clause changed** | was failing on TD-003's `ΣY` bound while the identity passed at `0.00776`; now fails on `fuelBalanceResidual 0.093590` against `< 0.05`, and the converged `0.0847` still fails by 1.7x. The `0.00776` was error cancellation | **TD-030**, with TD-003 |
| `AVariableConductivityChangesTheAnswer…` | FAIL 0 s -> FAIL 1 s | `risingConductivity.value(T_ad) > 3*kConductivity` reads `0.11` vs `0.15`. Fixture arithmetic, independent of `dt` | pre-existing, named in TD-005 `GATE.md` §5.2 |
| `WithReactionsDisabledNoFlamePropagates` | **UNMEASURED -> PASS 51 s** | the stage-D control (`logs/08`) measured **PASS in 42 s at cap 20**, so **TD-027 gets no credit**. `brackets = 13` instead of grinding through `maxOuterIterations`, which matches TD-004's locator fix | TD-004's locator fix (**not** isolated from TD-005's C4d) |
| `TheAnswerIsInsensitiveToTheStepsPerTrial` | PASS 759 s -> PASS 1321 s, **now for the right reason** | spread **exactly 0** with all three windows returning the identical state, against `6.8e-04` at cap 20 while two of three configurations were 24x outside the tree's admissibility bound | TD-005 F1, **resolved** |
| `TheEigenvalueIsIndependentOfTheInitialMassFlux…` | PASS 471 s -> PASS 1293 s | `S_L` spread `2.08e-04`. But `S_c` differs 20 % and `delta` 30 % between guesses — both states are transients and retain path memory | **TD-031** |
| `MalformedConfigurationsAreRefused` | PASS 0 s -> PASS 0 s | — | — |
| `TheBurntCompositionConsumesTheFuelExactly…` | PASS 0 s -> PASS 0 s | — | — |
| `TheObviousChecksAreVacuousOnTheSeededProfile` | PASS 0 s -> PASS 0 s | — | — |
| `TenTimesThePreExponentialMovesTheFlameSpeedFar` | PASS 798 s -> PASS 1791 s | `S_L` `4.86230 -> 4.67194` and `15.3551 -> 14.814`, both down ~4 % as C2 requires | — |

**Twelve cases, all accounted for.** Two moved `PASS -> FAIL`, one moved `UNMEASURED -> PASS`, one
changed its failing clause, one changed the number of failing sub-cases, one now passes for the
right reason, and six are materially unchanged.

## 4. What closing the named failures requires — NOT this gate's work

```text
TD-030   the converged fuel balance is 8.47e-02. It fails ItConverges (< 0.02) by 4.2x and
         TheConsumptionIntegral (< 0.05) by 1.7x. The reaction integral is 107.892 % of
         fuelThrough while convection is exactly -100.000 %, so the excess is in the reaction
         term. A spatial lead exists: S_c reaches 41 % -> 68 % -> 90 % of S_L as dx falls.
TD-029   the mesh family is not self-similar: dt is DERIVED from dx, so a cell-count ladder
         refines both. A valid study refines each independently, which TD-005 logs/17-18 did
         for the first time in this repository.
TD-031   the reported state is a transient at the default march length, and TD-031's own
         measurement shows the fix cannot be a default VALUE -- two of three configurations have
         no convergence point at all, because the cycle still runs there.
```

**None of these is TD-027's defect and none is made worse by its fix.** All three were present
before and masked by the limit cycle, which is why they surface now.

## 5. Dry-run

| # | on the measured data | non-vacuity |
| --- | --- | --- |
| C7 | **PASS** — the twelve movements in `logs/03`–`logs/06` are exactly the twelve named in §3 | fails if any case moves that §3 omits. Demonstrated by construction: removing any row from §3 makes C7 fail against the same logs, since the movement set would no longer equal the named set |

**C7 is evaluated against data measured BEFORE this amendment was written**, which is the opposite
of the usual order and has to be stated plainly: the verification came first and the accounting
criterion second. That is legitimate only because C7 adds **no threshold** — it asserts that the
observed set of movements is fully enumerated, and the enumeration is checkable against the same
logs by anyone. It would **not** be legitimate for a numerical criterion, where fitting the
threshold to the measurement is exactly the failure CLAUDE.md §5.3 forbids.

## 6. Freeze

```text
NOT YET FROZEN. This file carries no hash until logs/A1_00_freeze.log is written.
GATE.md and audit.md are NOT modified -- their hashes are the freeze record, and the attribution
correction they also need is recorded forward in attribution_correction.md for the same reason.
```

## 7. Definition of done, amended

1. C1–C6 PASS as recorded in `verification.md` and `regression.md` — **done**;
2. C7 PASS: every movement named — **done**, §3;
3. the attribution correction recorded forward — **done**, `attribution_correction.md`;
4. the three named open items (TD-029, TD-030, TD-031) carry the failures they own — **done**,
   register updated;
5. `GATE.md` and `audit.md` left bit-identical to their frozen hashes — **verified**;
6. `TECHNICAL_DEBT.md` and `TODO.md` updated after the above.
