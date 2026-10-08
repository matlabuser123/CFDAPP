# TD-004 GATE_A3 — DRY-RUN AND NON-VACUITY. All four criteria can fail. Qualified for freezing.

```text
STATUS:    DRY-RUN COMPLETE. Every criterion executed against the baseline, every criterion shown
           capable of failing. GATE_A3.md is qualified for freezing; it is NOT yet frozen when
           this file is written.
PASSES:    pass 1 INVALID (stale library, marked in place) · pass 2 VALID · M5 VALID
EVIDENCE:  logs/A3_01_dryrun_INVALID_stale_library.log  (preserved, banner-marked)
           logs/A3_01_dryrun_COMPILE_ABORT.log          (preserved, operator error)
           logs/A3_02_dryrun.log                        (M1, REF, M2, M3, M4)
           logs/A3_03_m5_acceptance.log                 (M5)
TOOLS:     c5_amended_probe.cpp · a3_patch.py · run_a3_dryrun.sh (v1, defective, retained)
           run_a3_dryrun2.sh (v2) · run_a3_m5.sh
```

---

## 1. The measurements

Every leg proved which library it ran, against the hashes A2_07 recorded. A leg that could not would
have aborted.

```text
leg               library     C5a          C5b          C5c          C5d
                              |T_ad-T_pk|  |min Y|      |x-x_a|      |sumY-1|
                              <= 1.8e-03   <= 5.0e-11   <= bound     <= 1.2e-06
M1  pre-fix       1eb0e2cb    7.4156e-04   8.4530e-12   1.0253e-03   9.3003e-07
                              PASS         PASS         FAIL         PASS
REF post-fix      852555cf    1.0256e-03   1.8898e-11   7.8078e-07   8.8229e-07
                              PASS         PASS         PASS         PASS
M2  deadBand 1    852555cf    9.9564e-04   1.1841e-11   1.9224e-05   1.2355e-06
                              PASS         PASS         PASS         FAIL
M3  heat x0.9999  730e6c0d    1.5105e-01   1.2884e-11   6.7773e-06   1.1916e-06
                              FAIL         PASS         PASS         PASS
M4  species       4614d078    1.5110e-01   1.1848e-11   7.7159e-06   8.4088e-07
    x1.0001                   FAIL         PASS         PASS         PASS
M5  acceptance    4c3a6340    1.7695e+00   1.2311e-08   2.7884e-06   1.2860e-03
    1e-9 -> 1e-6              FAIL         FAIL         PASS         FAIL
```

C5c's bound scales with the dead band in force: `8.0e-05 m` at deadBand 3, `4.0e-05 m` at deadBand 1.

## 2. REF reproduces A2_07 exactly, which is what makes the rest readable

```text
                A2_07 post-fix            REF (this dry-run)
T_peak          1799.998974446609736      1799.998974446609736
min Y          -1.889798074490678e-11    -1.889798074490678e-11
x_front         3.499219221840 mm         3.499219221840 mm
|sum Y - 1|     8.822866316426925e-07     8.822866316426925e-07
```

Identical to the last digit. **The post-fix configuration passes all four amended criteria.**

## 3. NON-VACUITY — each criterion fails on at least one control

```text
C5a   FAILS on M3 by 83.9x   (1.5105e-01 against 1.8e-03 K). GATE_A3.md predicted ~83x BEFORE
                             the run, from T_u + 0.9999*1500 = 1799.85 K. Also fails on M4 and M5.
C5b   FAILS on M5 by 246x    (1.2311e-08 against 5.0e-11).
C5c   FAILS on M1 by 12.8x   (1.0253e-03 against 8.0e-05 m) -- the BASELINE is its control.
C5d   FAILS on M2 by 1.03x   (1.2355e-06 against 1.2e-06) and on M5 by 1072x. C5d is carried over
                             from C5 unchanged and had NEVER been dry-run; it now has a control it
                             was not designed to have.
```

**All four are non-vacuous. GATE_A3.md is qualified for freezing.**

## 4. C5b's derivation is CORROBORATED, not merely given a control

M5 raises `residualAcceptanceFraction` (`PremixedFlame.cpp:662`) from `1e-9` to `1e-6` — the exact
constant C5b's bound is built from. The response:

```text
acceptance   1e-9  ->  1e-6          a 1000x change
|min Y|      1.889798e-11 -> 1.231140e-08    a 651.4x change
```

Sub-linear by a factor of 1.54, which is what a nonlinear iterative solve should give. And the
headroom is stable across three orders of magnitude of the governing constant:

```text
at 1e-9   measured / bound = 1.8898e-11 / 5e-11 = 0.378
at 1e-6   measured / bound = 1.2311e-08 / 5e-08 = 0.246
```

**So `min Y_i` really is linear-acceptance-limited**, the bound's *form* is the right one, and its
coefficient is a genuine upper bound with ~3-4x headroom rather than a number chosen to fit. This is
the part of the dry-run that was not required and is worth more than the non-vacuity verdict: the
alternative reading registered in `run_a3_m5.sh` — "|min Y| unmoved -> THE DERIVATION IS WRONG" — did
not occur.

## 5. What went wrong on the way, and what caught it

### 5.1 Pass 1: two legs ran the wrong library

`cp -p` restored the backup **preserving its mtime**, older than the object already built from the
patched source, so ninja rebuilt nothing and REF and M2 silently ran the pre-fix library. Pass 1
**printed** the library hash and asserted nothing about it — it failed **open**, the class
`CLAUDE.md` §8 exists to prevent. What caught it was the reproduction control against A2_07.

Pass 2 restores with `cp` + `touch` and **asserts the per-leg library hash**, aborting on mismatch.
The defective harness is retained as `run_a3_dryrun.sh` rather than patched, so the defect stays on
the record.

### 5.2 M2 and M4 were chosen from a mechanism the data refutes

`GATE_A3.md` §3.2 registered that M2 would fail C5b *if* `min Y_i` tracks the recentring fill count.
On valid legs it does not:

```text
deadBand 3  |min Y| 1.8898e-11
deadBand 1  |min Y| 1.1841e-11     SMALLER, not larger
```

**The fill-count mechanism is REFUTED** and `verification_A3.md` §4's candidate explanation for the
2.24x post-fix change must be withdrawn. What does track the recentring count is the composition
**sum**: C5d degrades from `8.8229e-07` to `1.2355e-06`. The operator damages conservation through
the sum, not through negativity — a finding that belongs to TD-040.

### 5.3 M4 was never an independent control, and the arithmetic says why

```text
M3  heat x 0.9999      |T_ad - T_peak| = 1.510480e-01 K
M4  species x 1.0001   |T_ad - T_peak| = 1.510976e-01 K     within 5e-05 of M3
```

Consuming fuel `1.0001x` faster while adding heat at `1.0000x` reaches full depletion after
`1/1.0001` of the heat, giving `T_u + 1500/1.0001 = 1799.85 K` — **the same arithmetic as scaling
heat by `0.9999`**. M3 and M4 are two expressions of one corruption. A control chosen for C5b turned
out to be a duplicate of C5a's.

**The lesson, stated against myself:** C5b's control was derivable from C5b's own derivation the
whole time. The bound is `residualAcceptanceFraction * Y_F,u`, so the control is to vary
`residualAcceptanceFraction`. I instead picked two controls from a *mechanism hypothesis* about
recentring fills, and that hypothesis was wrong. **When a threshold is derived from a named
constant, that constant is the control.**

## 6. What this dry-run does NOT establish

```text
NOT established  that M5 is a DISCRIMINATING control. It fails C5a, C5b and C5d together, because
                 loosening the linear solve 1000x degrades everything. It proves C5b CAN fail,
                 which is what §5.2 requires, but it does not isolate C5b.
NOT established  why min Y_i grew 2.24x post-fix (1.8898e-11 from 8.4530e-12). The fill-count
                 explanation is REFUTED by §5.2 and nothing replaces it. Both values are inside
                 C5b, so this does not block the gate, but it is an open question, NOT a closed one.
NOT established  that C5d's 1.2e-06 band is correctly derived. It is carried over UNCHANGED from
                 C5 and is GATE.md §3.2's measured baseline band, attributed to TD-003. M2 fails it
                 by only 1.03x, which is a thin margin for a criterion that was never dry-run
                 before today. Recorded, not amended -- amending it is not authorized.
NOT run          the post-freeze verification. §5.3 step 6 requires a FRESH run that reuses no
                 earlier result, so REF above does NOT serve as the verification even though it
                 passes.
NOT closed       TD-004. C6, C6a and C7 have never been run, and TD-040 carries the S_L +6.07 %
                 stop independently. A passing C5 advances the gate to C6 and nothing further.
```

**§6's second item deserves emphasis.** The dry-run's own best finding is that it destroyed the
explanation `verification_A3.md` offered for the one post-fix change in these quantities. The gate
can be frozen because the criteria are sound and non-vacuous; the *physics* of why `min Y_i` doubled
is unexplained, and saying so is the point.
