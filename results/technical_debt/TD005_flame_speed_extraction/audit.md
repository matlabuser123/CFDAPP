# TD-005 — AUDIT — flame-speed extraction method

```text
item        TD-005   Flame-speed extraction method not frozen
priority    P1
authorized  2026-10-04, "do TD-005 next", after TD-004 stopped at C2 and the TD-004 -> TD-005
            dependency inversion was flagged for decision
register    TECHNICAL_DEBT.md section 4
evidence    results/p18/g8_closure/flame_solver_findings.md section 3.1 (the recorded blocker)
tree        the CURRENT tree, which CONTAINS TD-004's locator fix (kept, not reverted)
            src/species/PremixedFlame.cpp         sha256 b52fc24c703bc9df51e0671b735e29c2099cba15012ceef4e4b36cb3c0eae635
            include/cfd/species/PremixedFlame.hpp sha256 765fbee924693d8d49967318b4386f10b548160819aa69585b3fd00d3d9fce61
            build/release/src/libcfdcore.a        sha256 738f7b555b2a22fb19632eb5b742feda86114dcd6afb2f4bb996c77e342e2303
            Release, OpenMP OFF, CUDA OFF, gcc 11.4.0 (recorded in logs/01)
```

**The register's instruction is the hard part of this item:** *"the measurement method must be frozen
before the answer is looked at, and the definition derived from the formulation rather than selected
once numbers are visible."* So this audit's job is to say exactly what the method **is** today, what
is **already established** about it, and what is **not** — before any threshold is written.

---

## 1. What is already true, and must not be re-derived

The resolution protocol says to name this first. The method is not a blank sheet: it is designed,
implemented, and documented at length in `include/cfd/species/PremixedFlame.hpp`, and several of its
hardest parts are already settled **with the failure that motivated them recorded**. None of this is
TD-005's to redo.

| # | established | where |
| --- | --- | --- |
| 1 | `mdot` is an **eigenvalue**, not an input, and `S_L = mdot / rho_u` | `PremixedFlame.hpp:33`; `PremixedFlame.cpp:1515` |
| 2 | the consumption-speed integral is **degenerate** as an eigenvalue condition — `S_c == mdot/rho_u` identically for any `mdot` admitting a steady solution, so a fixed-point iteration on it measures only its own initial guess | `PremixedFlame.hpp:35-50`, with the derivation |
| 3 | what selects the eigenvalue is the **cold boundary**: `mdot(x_f)` plateaus as the front moves away from a Dirichlet inlet, and that plateau is the physical eigenvalue | `PremixedFlame.hpp:52-56` |
| 4 | a **steady** inner solve cannot be used — extinction is an absorbing state and the controller loses the race (peak fell 1800 -> 1501 -> 1276 -> 1088 K while `mdot` moved 4 %/iteration) | `PremixedFlame.hpp:58-73` |
| 5 | a **continuous** drift-driven update cannot be used either — it ran ~360x faster than the structure's own transit time | `PremixedFlame.hpp:79-87` |
| 6 | what is robust is the **sign** of the drift, so the eigenvalue is found by **bracket and halve, then geometric bisection**, with no gain anywhere | `PremixedFlame.hpp:89-99`; `PremixedFlame.cpp:1694-1730` |
| 7 | the reported `mdot` is the **geometric centre of the final bracket**, and the reported `massFluxResidual = (high-low)/low` is therefore a genuine **bound** on the eigenvalue rather than a step size | `PremixedFlame.cpp:1728-1732` |
| 8 | a **final march at the converged `mdot`** is required, so the reported profile belongs to the reported eigenvalue; without it the consumption integral read 2.58 cm/s against a reported 4.50 cm/s | `PremixedFlame.hpp:290-305`; `PremixedFlame.cpp:1735+` |

Items 2 and 3 are the substance of the register's two warnings, and both are already honoured in the
implementation. **TD-005 is therefore not "design the method" — it is "freeze it, and verify the
claims it rests on."**

## 2. The definition, as it exists today

```text
S_L  =  mdot* / rho_u

mdot*  = sqrt(low * high),  the geometric centre of the final bracket [low, high]
uncertainty on mdot*  =  massFluxResidual = (high - low)/low,  reported
bracket exit  =  massFluxResidual <= settings.tolerance   -> Converged
                 otherwise                                 -> MaxIterations
rho_u  =  p * Mbar(Y_u) / (R * T_u),  from the frozen unburnt state
sign(drift) per trial decides which side mdot* falls on; drift is the mean over the
  SECOND HALF of a trial, and a trial ends when consecutive `driftWindow`-step windows
  agree to `driftSettleTolerance * u_in`
```

**This definition is sound and it is derived from the formulation.** Its problem is purely one of
status: **it lives in a source comment.** It has no frozen evidence file, no sha256, and no recorded
statement that it was fixed before the numbers were seen. For a quantity that G8 judges against a
cited published band, that is the whole of what "not frozen" means — and it is the one deliverable
of this item that nothing else blocks.

## 3. The recorded blocker does not reproduce, and that is a finding in itself

`flame_solver_findings.md` §3.1 records the method's reproducibility as **4.3 %**:

```text
drift window 32   mdot = 0.05303   S_L = 4.50 cm/s
drift window 64   mdot = 0.05088   S_L = 4.32 cm/s
spread                             4.3 %
```

and states the requirement `reproducibility << 2.8 %`, because G8's band `35.0-37.0 cm/s` is ±2.8 %
about 36. That is the sole precision blocker the register points at.

**It was measured before `relaxationTimesPerTrial` existed.** The header records the replacement of
the fixed 128-step trial and names this very symptom as one of three it cured:

> *"the old fixed 128 steps was about HALF a relaxation time at the eigenvalue … S_L depended on the
> drift window at the 4.3 % level, because a longer window is simply a longer trial and the structure
> was still relaxing"*

TD-004's own baseline (`TD004_premixed_flame/logs/06`) measures the current drift-window spread as
**4.85901 → 4.86230 cm/s = 0.068 %**, and the bracket residual as `6.77e-04` on every run — which is
exactly `2^(1/1024) - 1`, i.e. ten geometric bisections from a factor-of-two bracket, so the search
is exiting on its `tolerance` and not on an iteration cap.

CLAUDE.md §8: *"a known failure stays known only while it reproduces its recorded values exactly."*
**The 4.3 % does not reproduce** in the drift window.

> **CORRECTED after `logs/01`, 2026-10-05.** This section originally concluded that §3.1's
> precondition "appears to be met with ~40x margin". **That was computed from the drift-window
> spread alone (0.068 %), which is the knob §3.1 happened to name** — and the full method-knob
> reproducibility is dominated by a different parameter:
>
> ```text
> driftWindow spread            0.0677 %   <- what this section used
> relaxationTimesPerTrial       2.1897 %   <- Finding 4's knob, which NO test varies
> the in-tree insensitivity bound  2.00 %  -> EXCEEDED
> G8's band half-width             2.80 %  -> the spread is 78 % of it
> ```
>
> So the `<< 2.8 %` precondition is **not** met with margin; the 4.3 % blocker **moved into the
> parameter that replaced the fixed 128-step trial** rather than disappearing. `dry_run.md` §C.2
> records the one mitigating fact — the eigenvalue is *converging* in that knob (successive
> differences contract 15x), so the 2.19 % is dominated by an unconverged level, and whether such a
> level belongs in an insensitivity sweep is an open gate-design question. The original conclusion
> is preserved above, per CLAUDE.md §4.

**This does not close TD-005**, for the reason that follows.

---

## 4. FINDING 1 — the instruments that certify reproducibility are FAIL-OPEN

**This is the item's real defect.** Both insensitivity tests assert on `flameSpeed` **alone**, and
their own printed output shows one configuration returning a state that is not a steady flame.

From `TD004_premixed_flame/logs/06`, `TheAnswerIsInsensitiveToTheStepsPerTrial` — **PASS, 744 s**:

```text
16 drift window: S_L = 4.85901 cm/s   S_c = 3.04353 cm/s   delta = 0.736 mm
                 fuel balance = 0.373632      <-- 37 % OUT, and the test does not look
32 drift window: S_L = 4.86230 cm/s   S_c = 4.90006 cm/s   delta = 1.229 mm
                 fuel balance = 0.043280
```

The window-16 configuration misses the consumption identity by **37 %** and violates fuel
conservation by **37 %**, with a flame thickness **1.67x** smaller. The test passes because `S_L`
agrees to 0.068 %.

The plateau test has the identical shape:

```text
anchor 0.30: S_L = 4.85572   S_c = 3.03422   delta = 0.734 mm     <-- the same bad mode
anchor 0.40: S_L = 4.86230   S_c = 4.90129   delta = 1.230 mm
```

### 4.1 Why `S_L` survives while the state does not

Not a coincidence, and worth stating because it is the method's design virtue turning into a blind
spot. The trial `mdot` sequence is **identical** between the two drift windows —
`0.696488, 0.348244, 0.174122, 0.0870611, 0.0435305, 0.0615615, …` — because bracketing is driven by
`sign(drift)`, and the signs agree even when the trials are far from relaxed. Only the step counts
differ (`128 vs 160`, `1040 vs 1056`). So the **eigenvalue search** is robust exactly as item 6
claims, while the **final state** it hands back is not. `S_L` is reproducible; the solution is not.

### 4.2 Why this is not a cosmetic complaint

A flame speed is only a measurement of a flame if the thing it was extracted from is one. The
identity `S_c == mdot/rho_u` is exact for a converged conservative discretisation (§1 item 2), so a
37 % mismatch says the state is **not converged** — and the method's own documentation already says
so: *"it detects an unconverged profile, a leaking boundary or a stoichiometry error."* The check
exists. The sweeps simply never compose it with themselves.

This is the [gate-criterion coverage] failure in its purest form: **the criterion exists, the sweep
exists, and nothing joins them.** G8 would otherwise be closed on a number certified by an
instrument that cannot see a 37 % conservation violation.

## 5. FINDING 2 — the plateau, which is the method's entire justification, is UNVERIFIED

§1 item 3 says the plateau **is** the eigenvalue. Its test is
`TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength`, and on the unchanged tree it
**TIMES OUT at 900 s** (`logs/06`). The cause is not a hang: the test performs **four** solves
(anchors 0.30 / 0.40 / 0.50, then a longer domain) at ~250 s each, so it needs ~1000 s against a
900 s cap, and the log shows it completing anchors 0.30 and 0.40 before dying.

```text
PremixedFlame.hpp:109  "THE PLATEAU MUST BE DEMONSTRATED, NOT ASSUMED. `anchorFraction` and
                        `domainLength` are knobs that the answer must be insensitive to, and a
                        flame speed quoted without that demonstration is quoting the anchor
                        position."
```

The header states the requirement, and the test that would discharge it cannot run. TD-004's
upstream probe contributed **two** points post-fix (`S_L = 4.86230` at `anchorFraction` 0.35 and
0.50, identical to six figures), which is evidence but not a plateau.

`TheFlameSpeedConvergesUnderMeshRefinement` times out the same way. Of the method's eight documented
properties, the two that bound a *quoted* flame speed — plateau and mesh convergence — are the two
with no running test.

## 6. FINDING 3 — the consistency test fails on an assertion that is not its own

`TheConsumptionIntegralAgreesWithTheEigenvalue` is recorded **FAIL**, and the reason matters:

```text
mismatch = 0.00776642   against  EXPECT_LT(mismatch, 0.01)        -> PASSES
maxCompositionSumDeviation = 6.29e-07  against  < 1.0e-8          -> FAILS   <- the failure
```

The identity check **passes**. The test fails on a `ΣY` closure bound that is **TD-003's**
tolerance-limited inner solve, bundled into this test as a trailing invariant. So the suite currently
reports the consumption identity as broken when it is not, and the real `ΣY` issue is attributed to
the wrong item. An instrument reporting a true defect under a false name is still a defective
instrument.

## 7. FINDING 4 — the convergence requirement is swept by no test at all

`relaxationTimesPerTrial` is the knob that replaced the fixed 128-step trial and that the header says
*"must be shown insensitive to it"*. It appears in exactly two places in the tree:

```text
src/species/PremixedFlame.cpp:143   requirePositiveFinite(... "relaxationTimesPerTrial")
src/species/PremixedFlame.cpp:1360  relaxationSteps = settings.relaxationTimesPerTrial * ...
```

**No test, probe or gate varies it.** The one knob introduced to fix the recorded 4.3 % blocker has
never had its own insensitivity measured, so the fix's sufficiency rests on the symptom disappearing
rather than on the new parameter being shown not to matter. `logs/01` sweep C measures it for the
first time.

---

## 8. Dependencies: which actually bind TD-005

| item | register claim | this audit's reading |
| --- | --- | --- |
| **TD-003** linear-solver robustness | blocks TD-005 | **Does not bind the precision work.** `worstLinearResidual` is pinned at the `1e-9` acceptance fraction — nine orders below the 2.8 % precision at issue — so it cannot be the reproducibility floor. It **does** bind Finding 3: the `6.29e-07` `ΣY` deviation is TD-003's, and it is why a TD-005 criterion must not assert a `1e-8` closure |
| **TD-004** premixed-flame capability | TD-005 depends on it | **Binds the admissibility criterion, and this is the collision to be honest about.** Finding 1's fix requires each swept configuration to return a conserving state, and TD-004 stopped precisely because the fuel balance degrades with march length (`0.0153 / 0.6022 / 0.7188`). A TD-005 criterion demanding admissibility at every level may fail for TD-004's unresolved reason — see §9 |
| **TD-020** no-flame early-out | P2, a TD-006 dependency | **Binds one of the three required controls.** The register requires the extractor to FAIL with reactions disabled; that control is `WithReactionsDisabledNoFlamePropagates`, which **times out at 900 s**. So that control is currently **unexercisable**, and TD-005 must declare it so rather than claim three working controls |

## 9. The collision with TD-004, stated before any threshold is written

Finding 1's correct fix is to require that every configuration in an insensitivity sweep returns an
**admissible** state, not merely a stable number. That criterion is **expected to be hard**, because
the bad mode (`delta ~ 0.735 mm`, `S_c ~ 3.04`, balance ~ 37 %) is the same kind of non-conserving
long-march state that stopped TD-004 at C2.

Two readings are possible and they are not yet separated:

```text
(i)  the bad mode is an EXTRACTION fault -- the trial read its drift before the structure
     relaxed, and the state handed to the final march was already wrong. Then it is TD-005's,
     and the fix is in the convergence requirement (Finding 4's knob).
(ii) the bad mode is TD-004's flashback surfacing under a different knob. Then TD-005 cannot
     close it, and the criterion fails for a reason that is already preserved and reported.
```

**Which one it is must be decided by measurement, not by choosing the convenient reading.** The
discriminator is available and cheap: the bad mode appears at the SHORTER trial (`driftWindow` 16)
and the CLOSER anchor (0.30), both of which reduce relaxation — so if reading (i) is right,
increasing `relaxationTimesPerTrial` should remove the bad mode at those same settings, and if
reading (ii) is right, it should not. That is sweep C against sweeps A and B in `logs/01`.

**This is recorded before the gate is frozen so that the gate is not written around the answer**, and
so that a failure attributable to TD-004 is reported as such rather than absorbed into TD-005.

## 10. Scope

**TD-005 is:** freezing the extraction definition in evidence; making the insensitivity instruments
fail-closed by composing state admissibility into them; discharging or honestly declaring the
plateau and mesh-convergence properties; unbundling TD-003's assertion from the consistency test;
and establishing the method's precision against §3.1's `<< 2.8 %` requirement on the **synthetic**
flame.

**TD-005 is NOT:** methane, `cp`, `lambda(T)` or any absolute `S_L` claim (TD-006 — and debugging
against methane would mean deciding with G8's gated answer visible); the flashback itself (TD-004,
stopped and preserved); the `1e-9`-limited inner solve (TD-003); the no-flame early-out (TD-020);
route (b)'s drifting-frame reformulation; any performance claim.

**Explicitly forbidden here, from the register:** reviving the consumption integral as the eigenvalue
condition; quoting a flame speed without the plateau demonstration; and weakening any threshold,
tolerance or control to obtain a PASS.
