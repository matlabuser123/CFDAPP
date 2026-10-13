# TD-032 — the march's transient term uses a time level that is ONE STEP STALE

```text
opened      2026-10-06
found by    reading advance() after TD-031's logs/05 measured a period-2 alternation
status      AUDIT ONLY. No production change. No gate frozen. NOT AUTHORIZED to fix.
severity    P0. It is the root cause of TD-028, and it is upstream of every field-derived
            quantity in the premixed-flame evidence base.
reproducer  results/technical_debt/TD031_final_march_length/logs/05_cell35_history.log
            and PARITY_FINDING.md -- both already recorded, no new run needed.
```

---

## 1. The defect, in three lines of source

`src/species/PremixedFlame.cpp`, inside `advance()`:

```text
1111    speciesTime.fieldsOld     = &compositionOld;     <- the transient term's OLD level
1157    thermalTime.temperatureOld = &temperatureOld;     <- the transient term's OLD level
        ...
1189    temperatureOld = temperature;                     <- assigned HERE, and at this point
1190    compositionOld = composition;                        `temperature` STILL HOLDS THE
1191    densityOld     = density;                            START-OF-STEP VALUE
        ...
~1210   temperature[c] = next;                            <- the state is updated AFTER
~1245   composition[i][c] = next;
~1235   advanceChemistryAtConstantPressure(... temperature ...)   <- and again here
```

The old level is copied **before** the state it is supposed to trail is written. Writing
`S_k` for the state at the top of step `k`:

```text
step k   assembles its transient term from  temperatureOld = S_{k-1}
         builds its operator coefficients from  temperature = S_k
         line 1189 then sets  temperatureOld = S_k
         the state is updated to S_{k+1}
```

So from step 2 onward the transient term is permanently one step behind the state. The first
step is correct, because `temperatureOld` is seeded equal to `temperature` immediately before
each march begins (line 1692 for the search, 1803 for the final march, 1314 per trial) — which
is why the defect is invisible in any one-step check.

## 2. Why this produces a period-2 alternation specifically

With the lag, the transport solve at step `k` satisfies

```text
(rho V / dt) (X - S_{k-1})  +  L_{S_k}(X)  =  0
```

and `S_{k+1} = chem(S_k + relax * (X - S_k))`. **The transient path therefore connects step
`k-1` to step `k+1`.** The even-indexed and odd-indexed steps form two interleaved
sub-sequences, coupled to each other only through the operator coefficients (`lambda(T)`, the
upwind stencil, `rho`) — not through the transient term, which is the dominant term at these
step sizes. Two sub-sequences with no transient coupling **need not share a fixed point.**

Three predictions follow, and all three were already measured before the mechanism was known:

```text
the period is exactly 2                      logs/05: COLD, HOT, COLD, HOT, COLD
the two branches can differ ARBITRARILY,     logs/05: ~310 K against ~1790 K across cells
  not by a per-step physical increment                25-45 -- 1480 K apart
each branch is individually SMOOTH           logs/05 cold branch: 308.0932 -> 308.0917
                                                     -> 308.0901, monotone
```

This is not a flame instability, a recentring artefact or a chemistry-split error. It is a
time-level staging error in the transport half.

## 3. Why the same three-line block is CORRECT for `densityOld`

`densityOld` is consumed **inside** the same step, by the `dt` controller at lines 1254-1260,
where start-of-step density is exactly what is wanted — and the comment there says so. The
transient term does not use it at all: `thermalTime.densityOld = &density` (line 1158), the
same level at both ends, deliberately, per the non-conservative-form note.

`temperatureOld` and `compositionOld` are consumed at the **top of the next step**, where
start-of-next-step is what is wanted. One assignment site, two different requirements, and the
block satisfies the one belonging to the field that does not need it.

## 4. Why no test caught it

```text
tests/unit/thermal/test_thermal_transient.cpp     tests assembleEnergyEquation GIVEN an old
tests/unit/transport/test_scalar_transport.cpp    level, and the assembly is CORRECT
```

Both suites hand the assembly an old level and check what it builds. Neither can see which
field the *caller* passes. The defect is entirely in the caller's staging, and there is **no
march-level transient test** for the premixed flame — no manufactured unsteady solution, no
check that the march reproduces a known time-dependent answer. A one-step lag is invisible to
every test that exists.

## 5. Scope — and a correction to PARITY_FINDING.md section 4

`runTrial` calls the same `advance()` (line 1374), so **the trials carry the same lag.** The
survival argument recorded for TD-027's C2 — "S_L comes from the trials, not the final march" —
is correct only about the final march's *parity*. It does **not** make `S_L` independent of
this defect.

What keeps `S_L` credible is narrower and should be stated as such: it is taken from the
**sign** of the front drift, it has been bitwise reproducible across 30+ solves, and it
converges in `dt` to a Richardson limit of 4.67589 cm/s with the gated value 3.99 % off. That
is evidence the drift *sign* is robust to the lag. It is not evidence the march is correct, and
no number measured on this march should be called validated until TD-032 is fixed.

## 6. What is NOT done here

No production file is changed. No gate is frozen. The fix is a staging move — the old level
must be captured at the top of `advance()`, or after the chemistry half, so that it equals the
state the next step starts from — but `recentre()` also shifts `temperatureOld` and
`compositionOld` (lines 971-976), so the two candidate placements are not interchangeable and
the choice needs its own derivation.

**Required before any fix:** authorization, a frozen gate whose central criterion is that the
reported state at budget `N` and `N+1` agree to within the per-step change bound, a dry-run
proving that criterion FAILS on the current tree (logs/05 already shows ~1480 K against a bound
of order 1 K), and a negative control that re-introduces the lag and must fail again.
