# TD-040 §5b — the eigenvalue question is ANSWERED BY BOUNDING. No limit could be extrapolated, and the reason is a new defect.

```text
STATUS:      The dt ladder ran all four levels on the fixed tree. NO Richardson limit is claimed --
             the probe refused, correctly. The eigenvalue question is nonetheless SETTLED in
             substance by a bounding argument (§3), which needs no extrapolation.
RESULT:      4.671937 is EXCLUDED. All four dt levels lie within 0.136 % of each other near 4.96;
             the pre-fix value is 6.0 % away and cannot be the dt -> 0 limit of this solver.
NEW FINDING: S_L is QUANTIZED at ~3.356e-03 cm/s (0.0677 %), and that quantum is >= the entire
             dt-sensitivity. Carved out as TD-043. It retroactively qualifies TD-029's result and
             my own report of it (§4).
SCOPE:       dx fixed at 500 cells; dt halved four times. No production change.
EVIDENCE:    logs/03_dt_ladder_fixed_tree.log
INSTRUMENT:  tools/dt_ladder_probe.cpp
TREE:        PremixedFlame.cpp fa815c97… · libcfdcore.a 852555cf… (gates asserted in the log)
COMMIT:      none — not authorized.
```

---

## 1. The measurement

```text
level   dt              trial cap    S_L cm/s    marchSteps   fuel bal      wall s
    0   9.424300e-06       80 000    4.955332        15 283   6.3028e-03       936
    1   4.712150e-06      160 000    4.958687        30 526   2.0261e-03      2187
    2   2.356075e-06      320 000    4.958687        61 053   6.3963e-03      4442
    3   1.178038e-06      640 000    4.962045       121 940   7.9020e-03      6763

successive differences   +3.355407e-03   +0.000000e+00   +3.357679e-03
```

The trial cap scales as `1/dt` by design, so the trials' physical duration is held fixed across the
ladder. Without that, the fine levels would have been truncated at 80 000 steps and the ladder would
have measured the cap instead of the physics.

## 2. WHY NO LIMIT IS CLAIMED

Levels 1 and 2 returned a **bit-identical** eigenvalue, so the middle difference is exactly zero.
Both order estimates are therefore undefined, the asymptotic range is not demonstrated, and
`CLAUDE.md` §6 forbids an observed-order claim on such a family. The probe stopped there by its own
pre-registered rule:

> *"the two order estimates DISAGREE -> the family is not in the asymptotic range and NO limit may
> be claimed from it, whatever the numbers look like."*

**Reporting a Richardson value here would have been precisely how TD-027's `4.67589` reached the
record** — an extrapolation from a family that did not support one. It is not done.

## 3. THE QUESTION IS STILL ANSWERED — by bounding, not extrapolating

An extrapolation is not needed to exclude a value 6 % away.

```text
observed across a 8x range of dt:   4.955332 .. 4.962045      span 0.136 %
the sequence is MONOTONE INCREASING, so the dt -> 0 limit is >= 4.962045
                                                               and near it, since the
                                                               total movement over 8x in dt
                                                               is only 0.136 %

post-fix  4.955332   the limit sits within ~0.14 % of it
pre-fix   4.671937   6.0 % BELOW the coarsest level, and the sequence moves AWAY from it
TD-027 C2 4.67589    5.9 % below, same conclusion
```

```text
=> THE PRE-FIX EIGENVALUE IS EXCLUDED. TD-027's C2 reference inherited the blind-locator defect.
=> TD-004's registered "S_L moved +6.07 %" was a move TOWARD the truth, not away from it.
```

This is weaker than a Richardson limit and it is sufficient for the question actually asked. What
it does **not** give is a converged value: the limit is "near 4.962, probably slightly above",
with no order and no GCI.

## 4. THE NEW DEFECT, and it changes how TD-029 must be read

Three distinct values appeared, and they are **equally spaced**:

```text
4.955332     ->  +3.355407e-03  ->  4.958687     ->  0  ->  4.958687  ->  +3.357679e-03  ->  4.962045
```

The two nonzero increments agree to three digits, and a 2x change in `dt` produced **no change at
all** between levels 1 and 2. That is the signature of a **quantized output**, not of smooth
convergence: `S_L` is being reported on a lattice of spacing `~3.356e-03 cm/s`, which is `0.0677 %`
of the eigenvalue.

The cause is the bisection that produces `S_L` — it terminates on a bracket, so the reported mass
flux lands on a discrete set. **The exact mechanism is NOT established here** and is carved out:

```text
TD-043  `S_L` is reported on a lattice of spacing ~3.356e-03 cm/s (0.0677 %), so the eigenvalue
        solver's own output resolution is COARSER THAN OR EQUAL TO the dt-sensitivity it is used to
        measure. Any dt or dx convergence study of S_L at this fixture is resolution-limited, and
        an observed order computed from differences at or below one quantum is meaningless. The
        bisection's termination is the suspected cause (`bracket = 0.000677131`, "bisect 10"), and
        the arithmetic from bracket width to lattice spacing is NOT yet closed.
```

### 4.1 This retroactively qualifies TD-029, and my report of it

TD-029's re-measure reported the temporal error as `+0.068 %`. Against this lattice:

```text
one quantum                  0.0677 %
TD-029's "temporal error"    0.068 %
```

**They are the same number.** So TD-029's measured temporal error is **exactly one lattice step** and
is indistinguishable from the solver's reporting resolution. The honest statement is not "the
temporal error is 0.068 %" but **"the temporal error is at or below the smallest change this
instrument can report"**.

What survives of TD-029's re-measure, and what does not:

```text
SURVIVES   the temporal error COLLAPSED. Pre-fix 4.24 % was ~62 quanta and comfortably resolvable;
           post-fix it is <= 1 quantum. A 62x reduction to the resolution floor is a real result.
SURVIVES   the spatial error -0.679 %, which is ~10 quanta and therefore resolvable.
DOES NOT   the precise value "+0.068 %", and with it the ratio "0.10". The ratio's numerator is
           unresolved, so the true asymmetry is 10:1 OR MORE EXTREME, not 10:1 exactly.
DOES NOT   the sign of the temporal error. A one-quantum change cannot establish a direction, so
           "refining dt RAISES S_L" is NOT supported -- and the claim that the two errors have
           OPPOSITE SIGNS therefore rests on an unresolved quantity.
```

> **CORRECTED the same day.** The paragraph below over-withdrew. A one-step change DOES determine a
> direction: the reported value is within ±0.5 step of the true root, so N observed steps bound the
> true difference to `(N-1, N+1)` steps, and one full step is strictly positive. TD-029's
> opposite-signs claim is **reinstated**; only its VALUE and RATIO fall. What IS undetermined is the
> dt ladder's own `L1 -> L2` leg, which moved 0 steps. See `TD043_eigenvalue_lattice/audit.md` §3.

**That last line matters most**, and it works against my own conclusion: TD-029's headline
qualitative claim — opposite signs, hence cancellation — is now **unsupported**, because one of the
two signs is read from a single lattice step. I recorded that claim as surviving. It does not.

## 5. My pre-registered prediction was right, for the wrong reason

I registered: *"the limit lands near 4.96, confirming the post-fix eigenvalue, because TD-029
measured the per-halving dt sensitivity at only +0.068 % -- a geometric tail of that size cannot
reach 4.67."*

The conclusion holds. **The reasoning does not**: the `+0.068 %` I reasoned from was not a
sensitivity at all, it was the instrument's resolution. The tail argument happens to survive —
a quantized sequence spanning 0.136 % still cannot reach 6 % away — but I reached a right answer
through a quantity I had misread, which is luck rather than method.

## 6. What is NOT established

```text
NOT established  the dt -> 0 limit as a VALUE. §3 bounds it; it does not converge it. No order, no
                 GCI, and none may be claimed from this family.
NOT established  the lattice mechanism. §4 infers quantization from three equally spaced values and
                 one exact repeat. The arithmetic from the bisection's bracket to the 3.356e-03
                 spacing is NOT closed, and until it is, "quantized by the bisection" is the
                 leading candidate rather than a finding.
NOT established  that the lattice spacing is fixture-independent. It was observed at 500 cells with
                 this bracket; a different initial bracket or iteration count would plausibly give
                 a different spacing, which is exactly what TD-043 must measure.
NOT established  which eigenvalue is CORRECT in absolute terms. 4.671937 is excluded. That leaves
                 ~4.962 as the limit of THIS solver, which is not the same as physically right --
                 TD-040's own recentring ledger is an uncorrected bias and this ladder does not
                 test it.
NOT re-measured  the fuel balance's behaviour. It is non-monotone across the ladder
                 (6.30e-03, 2.03e-03, 6.40e-03, 7.90e-03) because the four march lengths stop at
                 different phases of TD-031's sawtooth -- consistent with that finding, and not a
                 convergence sequence.
NOT run          any regression. No production change.
```

**§6's fourth item is the boundary of this result.** The ladder settles a dispute between two
recorded numbers. It does not validate the solver against physics, and TD-040's conservation defect
— the reason this item exists — remains unfixed and untested by it.
