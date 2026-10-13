# TD-030 AUDIT — the reaction excess is REAL, HALF its recorded size, and no longer the larger term

```text
opened      2026-10-07, authorized in full
status      AUDIT. No production change. No gate frozen.
logs        01_budget_remeasure.log   TD-005's fuel budget probe, unchanged, on the fixed tree
            02_front_inlet_HARNESS_ABORT.log   a launcher defect, no measurement -- see §5
            03_front_inlet.log        front position vs inlet fuel deficit
outcome     TD-030's SCOPE NARROWS to the reaction excess alone. The inlet diffusive term it
            would otherwise have absorbed is a FLAME-ANCHORING effect and moves to TD-004.
```

---

## 1. Every number in TD-030's entry was measured on the alternating field

TD-030 was opened from TD-005 `dry_run.md` §O, before TD-032 was known. Its entry states the
reaction integral is **107.892 %** of `fuelThrough` and is *"the sole oversized term"*. The audit
therefore starts by re-measuring that, not by reasoning from it.

TD-005's `fuel_budget_probe.cpp` is the instrument that produced §O's table, and it **validates
itself first** by recomputing the fuel-balance residual and comparing against the solver's reported
value. It was re-run **unchanged** — its fixture pins `timeStep` explicitly at `9.4243e-06`
(D = 0.833), so §O's conditions are reproduced exactly rather than approximately.

```text
recomputed residual 8.886967e-02   reported 8.886967e-02   agree: YES    (relaxT 22)
recomputed residual 9.215889e-02   reported 9.215889e-02   agree: YES    (relaxT 26)
```

The definition the probe reads is still the one the solver uses, so the table below can be trusted.

## 2. The budget, then and now

```text
term                     §O recorded      relaxT 22       relaxT 26
convection                -100.000 %      -100.000 %      -100.000 %     exact, carries no error
reaction                  -107.892 %      -103.922 %      -103.878 %     excess HALVED
inlet diffusion             -0.572 %        -4.965 %        -5.338 %     9x LARGER
accumulation (omitted)      +0.350 %        -1.113 %                     sign flipped
gap                         +8.464 %        +8.887 %        +9.216 %
UNEXPLAINED REMAINDER       +8.115 %       +10.000 %
```

**The reaction excess is real and about half its recorded size: 7.892 % → 3.9 %.** That was
predictable from the consumption ratio (`S_c/S_L` moved from 0.899 pre-TD-032 to ~1.043 after) and
it is the part of TD-030 that survives.

**What was not predictable: the inlet diffusive term grew ninefold and is now the LARGER of the two.**
TD-030's entry calls the reaction integral the sole oversized term. On the fixed tree it is not even
the biggest contributor to the gap.

## 3. The inlet term is driven entirely by the FIRST CELL, and the first cell by the FRONT

The solver's balance is `|convection − reaction − inletDiffusion| / fuelThrough`, with

```text
inletDiffusion = gamma * (Y_F[0] − Y_F,u) / (dx/2),    gamma = lambda/(cp·Le) = 4.166667e-05
```

so that term is a function of one number: how far the first cell's fuel has fallen below the inlet
value. Backing it out of both eras:

```text
§O era        Y_F[0] = 4.999621924e-02   deficit -3.78e-06   ->  -0.572 %
now relaxT 22 Y_F[0] = 4.996720579e-02   deficit -3.28e-05   ->  -4.965 %     8.7x
```

`logs/03` measured the front and the deficit across the same ladder:

```text
relaxT   front mm   delta mm   deficit        inletDiff%
    10   2.474665   1.184404   -1.1784e-05      -1.784
    14   2.147040   1.181815   -1.9825e-05      -3.002
    18   1.940455   1.179529   -2.7676e-05      -4.190
    22   1.836799   1.177435   -3.2794e-05      -4.965
    26   1.792941   1.176498   -3.5255e-05      -5.338
    30   1.775956   1.176144   -3.6258e-05      -5.490
```

**The front travels 0.699 mm upstream while the deficit grows 3.08x and the inlet term tracks it
monotonically.** The reading registered for this direction before the run was *"the hypothesis holds,
and the inlet term is a domain/anchor effect rather than a chemistry one."*

**The flame MOVED; it did not SPREAD.** `delta` is essentially constant across the ladder —
1.1844 → 1.1761, in fact slightly decreasing. That distinction is why the upstream profile was
printed: a front at the same place with a longer preheat tail would deplete the inlet just as well,
and would mean something entirely different. A constant thickness with a sliding position is
translation.

## 4. Two findings that are NOT TD-030's, recorded here and moved

### 4.1 The front settles ~1.73 mm UPSTREAM of its anchor → TD-004

The anchor is at 3.500 mm; the front converges to ~1.77 mm, about **87 cells** upstream.

The test fixture's own sizing comment reads *"x_anchor = 0.35 * 10 mm = 3.5 mm = 5.9 delta_preheat,
enough for the plateau"*, with the preheat zone at ~0.6 mm. So:

```text
designed upstream room    3.5 mm / 0.6 mm  =  5.8 preheat lengths   (anchor)
actual upstream room      1.78 mm / 0.6 mm =  2.96 preheat lengths  (where the flame sits)
```

**Roughly half what the fixture assumed**, which is precisely why the preheat tail now reaches the
inlet and depletes the first cell. The domain sizing is sound *for a flame at the anchor*; the flame
is not at the anchor. That is flame stabilisation, and it is **TD-004's** blocker — the smaller, real
residue of the "flashback" reading whose larger part was TD-032's alternation artefact.

### 4.2 The front has NOT converged at 51 583 march steps → TD-031

```text
front decrements, relaxT 10->14->18->22->26->30:  0.328  0.207  0.104  0.044  0.017  mm
```

Halving roughly geometrically, extrapolating to ~1.765 mm. **After 51 583 steps the front is still
translating upstream.** This is why the balance residual was still climbing across the C2-A1 ladder:
the inlet term rises because the front is still moving.

**Consequence for TD-031:** "march until the state stops changing" needs a criterion on the **front
position**, not only on the per-step field change that TD-032 made meaningful. A field residual can
look settled while the structure is still sliding.

## 5. A harness defect in this audit's own tooling, recorded

`02_front_inlet_HARNESS_ABORT.log` is preserved and contains no measurement. The launcher ran the
work through `bash -c "$(declare -f run); run"`, which carries the function text into the new shell
but **not the variables it closes over**, so `$BUILD` was empty and it aborted on a missing
`CMakeCache.txt` before a single solve. Re-launched as `03` from a standalone script file.

Third delivery-mechanism failure in this stretch — a heredoc parse error, a job overlapping a
tree-mutating one, and now a lost variable capture. **None touched a number; each cost a cycle.** The
standing pattern in response: work in script FILES, launched detached, with a refuse-if-exists guard
on the output path.

## 6. What TD-030 now has to explain, and what it must not inherit

```text
IN SCOPE      the ~3.9 % reaction-integral excess at D = 0.833
OUT OF SCOPE  the ~5.5 % inlet diffusive term  -> TD-004 (§4.1)
              the unconverged front            -> TD-031 (§4.2)
```

TD-030's recorded diagnosis — a first-order Lie split running at ~500x the chemical time — was
reasoned to explain a **7.9 %** excess. It must now be re-derived against **3.9 %**, and it can no
longer borrow support from the gap's total size, because most of that total is someone else's. The
`chemistrySubSteps` limiter observation stands as an independent fact about cost and stiffness; what
does not stand is the inference from "the gap is 8.5 %" to "the split explains the gap".

**Also still open from TD-030's entry and untouched here:** `S_c` swings 167 % under a knob that
moves `S_L` by 0.135 %, which weakens TD-005's C6 and any G8 validation resting on the consumption
identity.

## 7. Not done

No production change. No gate frozen. The splitting diagnosis is not tested — doing so needs Strang
splitting, a `dt` derived from the chemical timescale (~470x the cost), or `omega` integrated as
applied rather than recomputed, and none is authorized.
