# CFDApp — Technical Debt

> **This file owns:** debt IDs, priority, root cause, dependencies, what each item blocks, its
> acceptance criteria and its status.
>
> **It does not own:** phase status or execution order (see [TODO.md](TODO.md)) or measurements
> (see `results/`). The same long description must not appear in more than one of the three.

## Legend

```text
Status     OPEN        specified, not started or in progress
           BLOCKED     cannot start until a dependency resolves
           INHERITED   pre-existing from an earlier phase; owned there
           RESOLVED    fixed AND verified against a frozen gate
           SUPERSEDED  overtaken by later work; kept so old notes are not mistaken for active work
           N/A         not a defect; recorded so it is not rediscovered as one

Priority   P0  qualification / correctness blocker
           P1  high
           P2  medium
           P3  low / deferred
```

**Register: 47 items.** Parsed from the table in §2 by the status column's leading token, not
asserted. Re-counted 2026-10-11; the block had read 25 items / OPEN 17, which the table outgrew.

```text
OPEN           31     PARTIAL         2     BLOCKED       2
INHERITED       2     RESOLVED        8     N/A           2     = 47
NOT CLOSED     37     of which   P0 5  ·  P1 11  ·  P2 12  ·  P3 9
all 47 rows                      P0 10 ·  P1 14  ·  P2 13  ·  P3 10
P0 still open  TD-003  TD-006  TD-007  TD-027  TD-029
```

`PARTIAL` is not a status this register declares; it is where the leading token reads
`PARTIALLY RESOLVED` (TD-035) or `MECHANISM CLOSED` (TD-043). **The status column is free text, so
this block is only as reliable as that token** — which is itself a small non-conformance with the
"one home per fact" model, recorded rather than silently normalised.

Separately, and recorded in §7 rather than as register rows: **9 items RESOLVED by a later phase**
and **5 duplicate chains SUPERSEDED** (19 of the original 49 per-phase TODO entries collapsed into
5 root items). Those are not counted above because they are history, not debt.

**CFDApp does not have zero technical debt**, and that is not to be claimed while this register
holds an open item.

---

# 1. Current Dependency Graph

Nothing in P2 or P3 may interrupt this work. **This is a graph, not a chain** — it was
`TD-001 → … → TD-007` until TD-032 was found beneath five items at once, and forcing it back into a
line would misdirect the next session.

```text
TD-001  Reacting-flow production reachability     RESOLVED 2026-10-03
TD-002  Stiff / bounded chemistry integration     RESOLVED 2026-10-04 (budget half; see TD-019)
   │
   ↓
TD-032  transient time level ONE STEP STALE       RESOLVED 2026-10-07 (TD-037 carved out)
   │    was the root cause beneath everything below it; each still needs re-measuring
   │
   ├──> TD-027  cell-diffusion cap                REOPENED — C4 invalidated, to be re-measured
   ├──> TD-028  period-2 alternation              root-caused to TD-032
   ├──> TD-029  error cancellation in 2 tests     OPEN
   ├──> TD-030  reaction-integral excess          OPEN
   └──> TD-031  final march length                BLOCKED on TD-032
   │
   └──> carved out BY TD-032, none authorized:
        TD-033  a G8 test that cannot reach its own body        OPEN
        TD-034  clang-format not clean                          OPEN — blocks EVERY commit
        TD-035  full sanitizer run infeasible for G8            OPEN
        TD-036  NoFlame is mislabelled and reports no reason    OPEN
        TD-037  driftWindow unguarded against trial length      RESOLVED 2026-10-07 — the
                                                                 G8 failure it carried now PASSES
        TD-038  the two extinction-drift blocks are duplicated  OPEN
   │
   ↓
TD-003  Reacting-flow linear-solver robustness    OPEN — next unresolved, NOT AUTHORIZED
   ↓
TD-004  1D premixed-flame capability              OPEN / STOPPED at gate criterion C2-A1
TD-005  Flame-speed extraction                    OPEN / PARTIAL (C4d resolved)
   ↓
TD-006  Close P18 G8                              BLOCKED
   ↓
TD-007  Re-run P18 final qualification            BLOCKED
   ↓
P18 COMPLETE — only if G1–G11 all PASS
```

**Why TD-004 was taken ahead of TD-003.** Its audit established that TD-003's requirement is met
*locally* in the flame path — the solve judges `||r||/||b||` rather than the solver's status,
preconditions, and reports the worst residual — so the flame could be audited and gated. A later
measurement then found `worstLinearResidual` pinned at exactly the acceptance fraction, which puts
TD-003 back in play as a genuine dependency. Recorded in
`results/technical_debt/TD004_premixed_flame/audit.md` §1 and §4.4 rather than smoothed over.

**TD-004's route (a) was executed and the item STOPPED at C2** (§4). The fix is correct and is kept,
but it does not close the item.

> **SUPERSEDED 2026-10-07.** This section previously concluded that TD-004's remaining blocker is
> flame **stabilisation**, which inverted the `TD-004 → TD-005` order and was left as a user
> decision. That classification rested on the pre-fix front position — and TD-032 established that
> the reported front alternated between the anchor and the inlet, with a correctly marched solve
> putting it at neither. **The inversion question is therefore on hold, not answered**, and cannot
> be reopened until TD-004's blocker is re-measured on the fixed march.

---

# 2. Debt Register

| ID | Pri | Debt | Status | Depends on | Blocks | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| TD-001 | P0 | Reacting-species stack not reachable from a case file / `ProjectRunner` | **RESOLVED** | — | was: G8 | `technical_debt/TD001_reacting_production/` |
| TD-002 | P0 | Explicit chemistry march has a round-off `ΣY=1` step budget | **RESOLVED** | TD-001 | was: TD-004 | `technical_debt/TD002_stiff_chemistry/` |
| TD-003 | P0 | BiCGSTAB breaks down on pure-upwind convection systems; no repository-wide status audit | **OPEN / AUDITED + KAPPA MEASURED 2026-10-11.** C1 SATISFIED (escalation gap carved out as TD-047). C2: the lower-bidiagonal class is solvable iff `r = s/d <= 1`; at `r = 1.05` BiCGSTAB BREAKS DOWN and GMRES(30) stagnates while forward substitution is exact. **BUT NEITHER REAL MATRIX IS IN THAT REGIME** — the flame is dominance 1.0986 / cell Peclet 0.028 (diffusion-dominated) and the production case 10.92 / Peclet 250 (transient-dominated), both strictly diagonally dominant. The P18.7 log is a 10-cell setup, NOT the committed case, and its failure was Breakdown at iteration 2 = STRUCTURAL. **Candidate fix: `LinearSolverFallback` already escalates on exactly `Breakdown` (:106) to GMRES (:115), is wired into SIMPLE/CompressibleSIMPLE, and is NOT wired into the reacting path (ProjectRunner.cpp:816 builds a bare BiCGSTAB)** — so no new solver may be needed. C3: the 1e-8 gate is reachable and fails at 88x; **kappa MEASURED at 26.28, not the assumed 1000** — my mechanism is refuted in magnitude (2.63e-08 vs 8.823e-07, a 33.6x shortfall) but survives in direction (tolerance alone cannot reach the gate; needs <= 3.8e-10). The dominant source of the 33.6x is UNIDENTIFIED **C3 SOURCE HUNT 2026-10-11: three candidates ELIMINATED and the fourth does not fit.** The transport step passes an EXPLICIT ZERO Patankar source (`:1172`), so my source-sum hypothesis is refuted; `sum_i omega_i` cancels EXACTLY in IEEE for the fixture's F->P 1:1 equal-weight mechanism; the recentring fill `{0.05, 1.0-0.05}` sums to **bit-exactly 1.0**, so recentring cannot drift it. And the observed 8.823e-07 **EXCEEDS the transport solve's own upper error bound** (1.53e-07, or 2.6e-07 on the most generous ||b||) by 3.4-5.8x — an upper bound cannot be exceeded, so a premise is false and which one is NOT settled. **Criterion 3's cause is therefore NOT established as a linear-solver matter, and TD-003 is plausibly the wrong owner for it.** **C3 CAUSE LOCATED 2026-10-11 by instrumented measurement (pure-addition patch, copy outside the repo, run reproduces the gated values EXACTLY): an exact Thomas solve of the SAME assembled systems leaves `|sum Y - 1| = 2.301527e-06`, 230x the gate, with ZERO solver error. The linear solve contributes `2.396274e-08` — **1.04 %**. So the tolerance remedy CANNOT work and my `<= 3.8e-10` requirement is WITHDRAWN. Also measured: `worstLinearResidual 9.999987e-10` (TD-003's 1e-9 record CONFIRMED; all 221 049 solves cleared `converged()`, the fallback never fired, and `:625`'s 1e-11 never binds because `:686`'s absolute tolerance is what converges), and the ASSEMBLED `||A^-1||_inf` is **120 000** against my reconstructed 2.4 — wrong by 50 000x, because the final march's dt leaves the operator nearly singular. **CRITERION 3 THEREFORE BELONGS WITH THE SPECIES RHS ASSEMBLY, NOT TD-003** — measured, not suspected. Leading remaining candidate: the diffusive closure's `sum_i J_i`, NOT yet measured. **C3 SOLVED 2026-10-11, AND THE GATE IS MIS-DERIVED (§12 STOP).** The closure test closes the chain to SEVEN DIGITS: `max |A^-1 (sum_i b_i - A*1)| = 2.301527e-06` = `max_c |sum_i y_EXACT - 1|`, the same number. Per-species matrices are BIT-IDENTICAL (0.0), so the telescoping holds exactly. The assembled RHS closes to `3.162982e-11` = **3.5x the round-off floor eps*||b||**, and the final march's near-singular operator amplifies it by **7.276e+04**. **The 1e-8 gate would need RHS closure at 1.374e-13, which is 65x BELOW double precision at this ||b|| — unachievable by construction.** Derived floor 6.503e-07 against an observed 2.302e-06. No source site injects anything: inlet, seed, chemistry, recentring fill and burnt composition all sum to 1 EXACTLY. **A §5.3 THRESHOLD AMENDMENT IS DUE AND NOT AUTHORIZED** — the repository already uses the right form for this quantity class at `SpeciesTransport.hpp:149` (a relative `8*eps*N*max|J|` bound). **PROVENANCE CHECKED 2026-10-11: the 1e-8 was NEVER reachable.** 0 of 81 recorded `|sumY-1|` measurements fall below it (range 9.54994e-08 .. 4.00526e-06). It is in NO frozen gate — `P18_GATES.md` has no such bound and the test file carrying it was untracked until 1a70f0a. It was never DERIVED: every neighbouring bound on this quantity class is relative (G2(a) `4*eps*N`=1.78e-15, G3 `8*eps*N_cells`=8.88e-13, §30 `8*eps*N*max|J|`), the 1e-8 is the only absolute constant, and the SAME constant is applied to `minMassFraction` where it passes with 529x margin while failing here by 88x. **TD-004's own frozen GATE.md §3.2 already recorded it as "already failing, before TD-004 changes anything" and gated against a measured band instead** — but attributed it to a "tolerance-limited solve", which is now refuted (the solve is 1.04 %). So a §5.3 amendment would REPLACE AN UNDERIVED CONSTANT, not correct a stale one. Counter-argument for Option B: the 9.55e-08 minimum is 6.8x BELOW the derived floor, so the floor is state-dependent and ~10x less amplification would bring 1e-8 into reach; which configuration produced it is NOT identified. **OPTION B's PREMISE REFUTED 2026-10-11 — it was my own counter-argument.** The `9.54994e-08` minimum came from `TheFlameSpeedConvergesUnderMeshRefinement` at **125 cells**, a state whose consumption integral is off by **51 %** (`S_L 4.40773` vs `S_c 2.15465`, fuel balance 0.4999, final march 334 NOT steady, thickness jumping 0.537→1.229 mm at 500). Not a better-conditioned flame — a WORSE SOLUTION on a coarser mesh, so it is no evidence that 1e-8 is reachable at the resolution the fixture requires (`dx <= delta_reaction/4` fixes 500 cells). I also WITHDRAW the "N ~ 23 cells" extrapolation I derived from that series: §6 forbids an order claim when two of three levels are 51-55 % inconsistent. **Option A now stands unopposed**, and its FORM is `C*eps*||b||*||A^-1||_inf` with both factors measured per state — a plain `C*eps*N` does not fit (the quotient rises 3.44e6→5.37e6). | — | TD-004, TD-005, TD-006 | `technical_debt/TD003_linear_solver/verification_minimum_configuration.md` · verification_gate_provenance.md · verification_closure_test.md · verification_instrumented_residuals.md · verification_sigmay_sources.md · verification_kappa.md · audit.md · logs/01 |
| TD-004 | P1 | 1D premixed-flame capability: recentring is non-conservative — **fixed**; the front then **ALTERNATES** between two interleaved sub-sequences (TD-028, root cause **TD-032**); **the "crossing relocates to the inlet" blocker is reproduced by TD-032 ALONE at the gated fixture (front alternates 3.535 <-> 0.261 mm) and must be re-measured after the fix before any flame-stabilisation claim stands**. C2's monotone premise was false and C2 was **AMENDED to C2-A1** (`GATE_A1.md`, frozen `e57bc758…`); **RE-MEASURED on the fixed tree 2026-10-07: C2-A1 now fails at clause (a) ALONE** — the march-length dependence is substantially repaired (monotone, converging) but the balance sits ~1.9x above the tree's own 0.05 bound, which is **mostly this item's own anchoring effect, moved here from TD-030 2026-10-08**: the flame settles **~1.73 mm (87 cells) upstream of its anchor**, so its preheat tail reaches the inlet and the inlet diffusive flux supplies ~5.5 % of the gap against TD-030's ~3.9 %. The fixture is sized for 5.8 preheat lengths of upstream room; the flame has 2.96. **ROOT CAUSE FOUND 2026-10-08: `recentre()` translates the field but NOT the locator's `previousLocated`, so the first recentring of a march permanently blinds `locateFront`** -- it then refuses the TRUE front once per step for the rest of the march (measured: exactly 1 rejection/step, `worstRejectedJumpPosition == flamePosition`) and the front drifts free. **FIX APPLIED AND RETAINED 2026-10-08**: the front now ends 0.039 cells off the anchor, zero locator rejections, and C2-A1's three clauses are SATISFIED (balance 15x better). **The item STOPS on a pre-registered condition: `S_L` moved +6.07 %**, because the working locator makes `recentre()` fire 3x per march and 220x across the trials, and that operator is not conservation-neutral -> **TD-040** | **OPEN / STOPPED on ONE blocker** (was two; blocker 1 DISCHARGED 2026-10-10). C5 PASSES (GATE_A3 `8a11ad39…`). C6 PASSES. C6a AMENDED (GATE_A6 `7bedb728…`) and verified fresh: **C6a-bal PASS**, **C6a-ord UNRESOLVED** (fine difference 2.00 quanta vs a derived 4-quantum floor — a STOP, not a FAIL). C6b and C7 remain unevaluated. **The TD-040 stop is DISCHARGED**: its chain breaks at the third link (translation does not bias `S_L`) and the dt ladder excluded the pre-fix eigenvalue, so the +6.07 % move was TOWARD the verified limit. REMAINING BLOCKER: C6a-ord needs a finer `settings.tolerance` (TD-043), which would likely make the band violation DEFINITE rather than clear it | TD-003, and now TD-005 — see §1 | TD-006 | `technical_debt/TD004_premixed_flame/` — GATE_A3 `8a11ad39…` · GATE_A6 `7bedb728…` · dry_run_A6.md · **verification_A6.md** · logs/A6_00…A6_03 |
| TD-005 | P1 | Flame-speed extraction — gate FROZEN; **C4d RESOLVED**; C3 diagnosed to a **7.892 % reaction-integral excess**, still FAIL; C6 still FAIL. Carved out **TD-027/028/029/030/031**. The march characterisation its C3 diagnosis relied on was **invalidated by TD-032** and must be re-measured | **OPEN / PARTIAL** — **C3 NOW PASSES** (2026-10-11): 0 of 12 configurations fail against a recorded 5 of 12, worst balance 1.0043e-02 vs the untouched 0.05 bound, 37x better than the recorded worst. The thin-flame state is GONE — `delta` is 1.1945–1.1975 mm at every level, against 0.73–0.85 at the old failures. Sweep A confirms TD-037 independently: S_L spread **exactly 0.000000** across driftWindow 16/32/64 with a bit-identical balance. **But the consumption identity now EXCEEDS its 0.01 bound at 10 of 12** (0.01027 at the reference, against 0.00776 pre-fix), so **C6's premise is obsolete**: it was written to unmask a PASSING identity and the identity no longer passes. C4d RESOLVED. C6 still FAIL | TD-032, then TD-004 | TD-004, TD-006 | `technical_debt/TD005_flame_speed_extraction/` — GATE.md · **verification_c3.md** · logs/21_c3_remeasure.log · logs/20_c5_controls_remeasure_MISLABELLED_AS_C3.log |
| TD-006 | P0 | Close P18 G8 canonical reacting validation | BLOCKED | TD-001…TD-005, **TD-027…TD-031**; TD-020's control is now AVAILABLE | TD-007, P18 | `p18/gates/P18_GATES.md` G8 · `p18/g8_closure/g8_reference.md` |
| TD-007 | P0 | Re-run P18 final qualification | BLOCKED | TD-006 | P18 COMPLETE | `p18/FINAL_QUALIFICATION.md` |
| TD-008 | P1 | P17 HLLC star-state / signal-speed positivity defect | INHERITED | — | wider compressible envelope | `p17/qualification/G6_POSITIVITY_FAILURE.md` |
| TD-009 | P1 | Debug+GUI `DriverSeegmiller` timeout contract unstable | INHERITED | — | P17 clean qualification | `p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md` |
| TD-010 | P2 | Zero `FixedGradient` transport coverage; turbulence retains the lagged gradient | OPEN | — | boundary robustness | `p18/species_transport/boundary_lag.md` §5 |
| TD-011 | P2 | `TotalEnergyState` refuses temperature-dependent `cp`; needs variable-γ EOS | OPEN | — | reacting-shock capability | `p18/mixture_properties/single_species_compatibility.md` §4 |
| TD-012 | P2 | Forward/reverse Arrhenius pairs unchecked for equilibrium consistency | OPEN | thermodynamic polynomials | reversible-chemistry rigour | `p18/arrhenius_chemistry/progress_rates.md` §4 |
| TD-013 | P3 | `A`'s units cannot be validated in code | **N/A as a defect** | — | — | `p18/arrhenius_chemistry/units.md` |
| TD-014 | P3 | Thermochemistry split across two types; registry identity caller-checked only | OPEN | — | API robustness | `p18/heat_release/species_enthalpy.md` §2 |
| TD-015 | P3 | Historical probes not reproducible from their own evidence directory | OPEN | — | reproducibility | `p18/reaction_source/logs/02_reaction_probe.log` |
| TD-016 | P3 | Separate-process determinism not measured | OPEN | — | stronger determinism evidence | `p18/FINAL_QUALIFICATION.md` §10 |
| TD-017 | P3 | Validation-evidence wording and re-measurement gaps | OPEN | — | evidence clarity | `p18/heat_release/logs/10_validation_noise_MAGNITUDE.log` |
| TD-018 | P1 | CRLF / `git diff --check` warnings are a push blocker | OPEN | — | any authorized commit | `p18/multi_species_state/logs/14_final_integrity.log` |
| TD-019 | P2 | No `d(ω_i)/d(Y_j)`; the implicit Patankar `Sp` is zero | OPEN | — | a stiff *mechanism* | `technical_debt/TD002_stiff_chemistry/audit.md` §4 |
| TD-020 | P2 | No early-out when a mixture cannot sustain a flame | OPEN | — | **TD-006** | `technical_debt/TD004_premixed_flame/logs/06_flame_baseline_per_test.log` |
| TD-021 | P3 | No elemental composition metadata | OPEN | thermochemical schema | elemental conservation | `p18/reaction_source/audit.md` §1 |
| TD-022 | P2 | Device species transport not implemented | OPEN | — | TD-025 | `p18/FINAL_QUALIFICATION.md` §10 |
| TD-023 | P2 | GPU variable-`cp` parity not established | OPEN | TD-011 | CPU/GPU physics parity | `p18/cpu_gpu_equivalence/` |
| TD-024 | P3 | GPU reaction orders limited to integer orders | OPEN | — | general GPU kinetics | `p18/cpu_gpu_equivalence/` |
| TD-025 | P3 | No end-to-end GPU reacting-flow solve | OPEN | TD-022, TD-023 | full GPU reacting flow | `p18/FINAL_QUALIFICATION.md` §10 |
| TD-026 | P2 | `worstFinalOffsetCells` is capped by the control locator's refusal, so it cannot see the offsets it names | OPEN | — | diagnostic trust | `technical_debt/TD005_flame_speed_extraction/` |
| TD-027 | P0 | `maximumCellDiffusionNumber = 20.0` was an unvalidated resolution cap, putting a gated `S_L` 3.99 % off against a ±2.80 % band | **REOPENED** 2026-10-06 — **the open criterion has MOVED from C2 to C3** (2026-10-10). C4 re-measured and PASSES on a parity-crossing ladder, by 24.5x. **C2 AMENDED** as C2-A2 (GATE_A3 `532e9bf1…`) because its reference `4.67589` was **never derivable** — the Richardson fine difference was ONE lattice quantum, so the order's interval is (1.00, ∞); replaced by a measured dt→dt/8 difference plus a `p ≥ 1` tail bound, budget unchanged. **C2-A2 PASSES** fresh: 0.2030 % ≤ 0.28 %, 1.38x margin, ladder reproducible to six decimals. **But C3 NOW FAILS at 4.06x against ≤ 2**, because C3's numerator IS C2's error and the old reference understated it. The real finding: **the reported precision understates the true temporal error by 3–4x** | — | was: TD-005, TD-006 | `technical_debt/TD027_cell_diffusion_cap/` — GATE_A3 `532e9bf1…` · amendment_A3_proposal.md · dry_run_A3.md · **verification_A3.md** · verification_A2.md · logs/A3_00, A3_01 |
| TD-028 | P0 | The march executes a **PERIOD-2 alternation**, root-caused to TD-032's one-step-stale time level. The period-14 limit-cycle characterisation and BOTH `dt` attributions are **INVALID AS AUTHORITATIVE** | **RESOLVED** 2026-10-10 — the period-2 alternation is GONE, verified on TD-028's OWN observable (the field, not S_L). `T[35]` at 500 cells / relaxT 18 across five consecutive budgets: swing **1.4836e+03 K → 1.438470e-04 K, a factor of 1.03e+07**, and the shape changed — four monotone same-sign increments of ~3.6e-05 K, a steady drift rather than an oscillation. **The period-14 characterisation and BOTH dt attributions remain INVALID AS AUTHORITATIVE**: clearing a root cause does not re-validate what it corrupted | TD-032 | TD-005, TD-006 | `technical_debt/TD028_period2_alternation/verification.md` · logs/01_cell35_remeasure.log · baseline `TD031…/logs/05_cell35_history.log` |
| TD-029 | P0 | Two PASSING tests passed by error cancellation; `dt` is derived from `dx` so no mesh family is self-similar | **RE-MEASURED 2026-10-09; QUALIFIED then REINSTATED 2026-10-10.** The qualitative claim HOLDS: spatial +10 lattice steps, temporal +1 step, and one FULL step determines a sign (true difference bounded to (N−1, N+1) steps), so **opposite signs stand**. The temporal error collapsed from 4.24 % to the range **(0, +0.135) %** — a real collapse, but a BOUND not a value; the ratio is (0, 0.22), not 0.10. My intermediate withdrawal of the sign claim was WRONG and is corrected in place | — | TD-005, TD-006 | `technical_debt/TD029_error_cancellation/verification.md` · `TD040_recentre_conservation/verification_dt_ladder.md` §4.1 |
| TD-030 | P1 | **RE-MEASURED on the fixed tree 2026-10-08: the reaction excess is REAL but HALF its recorded size, 107.892 % -> 103.9 %, and is no longer the larger term.** The inlet diffusive flux grew 9x to ~5.5 % and is a FLAME-ANCHORING effect -- **moved to TD-004**. Scope narrows to the ~3.9 % reaction excess; the "500x chemical time" diagnosis must be re-derived against that rather than inherited | OPEN / AUDITED | — | TD-005 C3, TD-006 | `TD005_flame_speed_extraction/dry_run.md` §O |
| TD-031 | P1 | `finalMarchRelaxationTimes = 10` reports a TRANSIENT state. **The convergence measurement that found relaxT 18 is INVALIDATED by TD-032** -- its ladder sampled one parity branch. **Measured 2026-10-08: the FRONT is still translating upstream after 51 583 steps** (decrements halving, 0.328 -> 0.017 mm), so a march-length criterion must watch the FRONT and not only the per-step field change | **RE-MEASURED 2026-10-09: CONFIRMED and STRENGTHENED.** The front is still translating upstream on the fixed march, and its character is now known — a **SAWTOOTH**, not a converging sequence: it drifts at ~1.1–1.3e-04 cells/step to the 3-cell dead band, `recentre()` resets it, and the cycle repeats. So **no relaxT reports a settled front** (`steadyStateReached` is false for a structural reason, not an insufficient budget). Explains C4's non-monotone balance spread and confirms C5c's 0.08 mm bound mechanistically. ONE reset observed, so the period is NOT established | -- | TD-005, TD-006 | `technical_debt/TD031_final_march_length/verification.md` — assembled from TD-027 logs/A2_01 and TD-004 logs/A3_04 |
| TD-032 | P0 | **The march's transient term uses a time level ONE STEP STALE** (`PremixedFlame.cpp:1189-1191` copies the old level before the state is updated), splitting the march into two interleaved sub-sequences. Root cause of TD-028. All 10 frozen criteria PASS. The PASS → FAIL it caused on `TheAnswerIsInsensitiveToTheStepsPerTrial` is root-caused and **carved out as TD-037** | **RESOLVED** 2026-10-07 (gate only; REGRESSION PARTIAL — see §TD-032) | — | TD-028, TD-031, TD-005 C3/C6, TD-004 C2, TD-006 | `technical_debt/TD032_lagged_time_level/audit.md` |

| TD-033 | P2 | `AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue` asserts a precondition its own constants cannot satisfy — `0.11 > 0.15` is false — so it fails at **0 ms before any solve**. A test that can never pass tests nothing | OPEN | — | G8 test credibility | `technical_debt/TD032_lagged_time_level/summary.md` §6 |
| TD-034 | P2 | `clang-format` is **not clean** on this tree, in `apps/gui/tests/test_cli_gui_equivalence.cpp` and `include/cfd/app/ProjectRunner.hpp`. CLAUDE.md §8 requires it clean, so this blocks ANY commit from this tree | **RESOLVED** 2026-10-09 for the property it names (clang-format clean: 741 files, exit 0, 0 warnings). A DIFFERENT defect found by the same audit is carved out as TD-041 | — | every commit | `technical_debt/TD034_format_and_eol/verification.md` |
| TD-035 | P1 | **ASan costs 38x on the premixed-flame suite** (303 s → 11612 s, measured), so G8's 4343 s becomes ~46 h and at least three tests would exceed `ctest --timeout 7200` and be recorded as TIMEOUTs. The full sanitizer regression CLAUDE.md §8 requires is therefore impossible as specified for G8 | **PARTIALLY RESOLVED** 2026-10-10. A sanitizer fixture is IMPLEMENTED: `PremixedFlameSanitizerTest` at 150 cells, in its OWN target (`CFDSpeciesSanitizerTests`) because C7 bounds `CFDSpeciesTests` excluding G8 at exactly 343 — **verified still 343**. Under ASan+UBSan at CI settings: **PASSED in 348.75 s against the 7200 s cap (20.6x headroom), with 0 ASan errors, 0 UBSan errors, 0 leaks** — the first sanitizer coverage this code has ever had. The asan testPreset now excludes `G8PremixedFlameTest` (verified: 0 listed, fixture present as Test #1871). **§11.4 is NOT unblocked**: the reason changed from "known infeasible" to "the full 2860-test preset's cost is UNMEASURED". The 38x founding figure remains unexplained against 12-17x measured three times **BOTH MISSING RUNGS RUN 2026-10-12, and this item's own premise is DISCHARGED.** It said the blocker stood because "the full 2860-test asan preset's cost is UNMEASURED". Measured: **25 673.72 s = 7.13 h**, with **ZERO sanitizer diagnostics** (2832 tests, 14 failed). Debug+GUI: 2906 tests, 18 failed, 15 526 s. **All 32 failures accounted for, 0 unexplained** — 4 known SST in each, the rest TIMEOUTS whose Release durations (10–66 min) exceed the 7200 s cap once Debug or ASan slows them; the cap is NOT raised (§7). A pre-registered prediction held: TD-046 made all NINE G8 flame cases time out in Debug. **The P18 Debug baseline is NOT a valid comparator** — it had CUDA ON (3130 discovered, 410 GPU lines) and ZERO G8 mentions against 76 here — but its four required `DriverSeegmiller` timeouts (G11) are all still present. CUDA OFF matches CI, which configures with no CUDA flag on GPU-less runners. **AND A CORRECTION TO THIS ROW'S OWN TEXT: there are not "five sanitizer configurations" — ci.yml has ONE preset in five SHARDS, so one full run covers their union.** Measured cost is within 8 % of ci.yml's own "worst shard 93 min" x5 model. **Release is RE-RUNNING** because TD-046/TD-047 invalidated its counts; §11.4 NOT discharged until it lands **THE §8 LADDER IS NOW 4 OF 4 ON THIS TREE (2026-10-12).** Release re-run: 2844 tests, **6 failed, ALL KNOWN**, 6908.34 s — and `AVariableConductivity...` is GONE from the list, confirming TD-046 in the full suite. Counts predicted EXACTLY (2844/6); the wall-time prediction was WRONG (+2860 s predicted, −236 s measured) because I summed a parallel test's time instead of reasoning about the critical path. **53 tracked files rewritten were CLASSIFIED by a fail-closed probe and 49 restored**; the 4 `build_identity` files are KEPT and left unstaged — the embedded commit SHA moved with HEAD, which §7's timing-only licence does not cover, so §9 makes it the user's call. **Classifying MID-RUN would have produced a FALSE regression report**: three files showed ~294 lines of apparent FP regressions in MMS norms that are timing-only on the completed run. My own classifier had TWO bugs (a regex missing `runtime_seconds`, a wrong markdown column index), both inflating SUSPECTs only, both fixed and recorded | — | TD-032 closure, TD-007 | `technical_debt/TD035_sanitizer_scope/` — audit.md · **verification.md** · logs/01_cost_ladder.log · logs/02_asan_measured.log · logs/03_fixture_under_asan.log |

| TD-036 | P1 | **`PremixedFlameStatus::NoFlame` is mislabelled.** At `PremixedFlame.cpp:1746` and `:1789` it is set when a trial **could not be assigned a side**; the enum documents it as “the anchor temperature is never crossed: nothing is burning”. `diagnostic` is left EMPTY on this path, so the result carries no explanation | OPEN | — | TD-032 closure, any search diagnosis | `technical_debt/TD032_lagged_time_level/attribution.md` §7 |
| TD-037 | P1 | **`driftWindow` has no guard against exceeding trial length.** A drift needs more than `driftWindow` steps of history, so a trial that extinguishes sooner is silently unsidable and aborts the search. Measured: the same trial at 40 steps was sided at window 32 and unsided at window 64; before TD-032 the same trial lived 71 steps and the condition held by 7 steps of margin | **RESOLVED** 2026-10-07 (gate only; REGRESSION PARTIAL) | — | was: TD-032's G8 failure, now PASSING | `technical_debt/TD037_drift_window_guard/verification.md` |

| TD-038 | P2 | **The two extinction-drift blocks in `runTrial` are DUPLICATED** and must stay in step. The duplication made TD-037's first fix incomplete and cost a full verification cycle; the gate caught it. They now cross-reference each other, which is a mitigation, not a fix | OPEN | — | future work in `runTrial` | `technical_debt/TD037_drift_window_guard/verification.md` §4 |

| TD-039 | P2 | **`c2a1_probe.cpp` hardcodes the pre-fix admissibility pattern `GGBGGB`** and refuses to issue a verdict when the measured pattern differs — so on a fixed solver the guard blocks the re-measurement the amendment exists to enable. Its intent is right; it tests the wrong thing, and should compare a run against a REPEAT of itself | OPEN | — | re-measuring TD-004 C2-A1 | `technical_debt/TD004_premixed_flame/remeasurement_A1.md` §6 |

| TD-040 | P0 | ~~`recentre()` is NOT conservation-neutral: -3.53e-06 kg/m^2 fuel and +21.2 J/m^2 per translation, and the mass it moves is never counted in `mdot`~~ **PREMISE REFUTED 2026-10-10 by this item's own §5(a) ladder.** `S_L` = 4.955332 at EVERY dead band, INCLUDING recentring entirely OFF (0 translations, ledger exactly 0), while the ledger rate swung -2.80 % to 0. The eigenvalue is bisected on the SIGN of the trials' drift, which a frame change does not alter, so `mdot` never consults the moved mass. The fills are correct for a moving frame, the ledger measures it accurately, and the steady-state balance properly EXCLUDES it (`:1740` is three instantaneous fluxes; `:1718` says the identities hold for a converged steady flame only). All three candidate fixes are wrong or unnecessary — §4.1 would mix a cumulative term into an instantaneous identity and BREAK C3, C4 and C6a-bal | **N/A as a defect** — reclassified 2026-10-10. CONDITIONAL: no quantity the project currently gates detects it. A future TIME-INTEGRATED conservation claim would see the ledger's 7.9e-04 kg/m^2 across 223 translations and would reopen this | — | nothing — it no longer carries TD-004's stop | `technical_debt/TD040_recentre_conservation/` — audit.md (§2 marked REFUTED in place) · **verification_disposition.md** · logs/01_deadband_ladder.log · logs/03_dt_ladder_fixed_tree.log |
| TD-041 | P1 | ~~CRLF has grown in 21 tracked `CMakeLists.txt`~~ ~~REFUTED: the CR/lines ratio is 1.000 on both sides~~ ~~BOTH WERE ARTIFACTS OF A BROKEN PROBE; the repository has ZERO CRLF anywhere~~ **THAT THIRD CLAIM IS ALSO FALSE, measured 2026-10-11 with a binary byte count: 102 tracked TEXT files contain CRLF, 21 621 lines** (91 under `results/`, 11 under `tests/`, of which 3 are `.cpp` under the format gate). A further 35 flagged files were BINARY and are my probe's artifact -- a PNG signature contains `0D 0A` by spec. Whether the count GREW is still unmeasured | **OPEN** — re-opened 2026-10-11. Three successive claims, two broken probes, one false conclusion committed in 0a9bc42. Severity is narrow: 91 of the 102 are raw `results/` captures this campaign commits verbatim | — | nothing | `technical_debt/TD034_format_and_eol/correction_2_crlf_in_head.md` · correction_lf.md (marked INVALID in place) |
| TD-042 | P3 | **`reactionHeatRelease` is computed every march step and DISCARDED.** `PremixedFlame.cpp:1086` declares `const ScalarField heatRelease = reactionHeatRelease(...)` and the identifier is never read again in that function — the march applies heat through the reactor at `:313` instead. `-Wunused-variable` stays silent because `ScalarField` has a non-trivial constructor, so the compiler cannot flag it. Found while choosing a corruption site for a GATE_A3 negative control: patching it would have produced a control that silently COULD NOT FAIL | OPEN | — | nothing; wasted work per step, and a trap for anyone instrumenting there | `technical_debt/TD004_premixed_flame/tools/run_a3_dryrun2.sh` header |
| TD-043 | P1 | **`S_L` is reported on a LATTICE of spacing ~3.356e-03 cm/s (0.0677 %).** Two dt values a factor of 2 apart returned a BIT-IDENTICAL eigenvalue, and the three distinct values in a four-level ladder are equally spaced to three digits. So the eigenvalue solver's own output resolution is **coarser than or equal to the dt-sensitivity it is used to measure**: any dt or dx convergence study of `S_L` at this fixture is resolution-limited, and an observed order computed from differences at or below one quantum is meaningless. Suspected cause is the bisection's termination (`bracket = 0.000677131`, "bisect 10"); the arithmetic from bracket width to lattice spacing is NOT closed | **MECHANISM CLOSED** 2026-10-10, with NO solver run. The lattice is **MULTIPLICATIVE**, ratio `2^(1/1024)`, spacing **0.067713 %** of S_L — derived from three source lines (bracketing ratio 2 at `:1821`, geometric midpoint at `:1834`/`:1861`, relative termination at `:1832`) and matching the solver's own reported `bracket = 0.000677131` to **0.0000 %**. Verified against 10 measurements across 4 cell counts, 4 dt values and both libraries — every one an integer lattice point, including the pre-fix eigenvalue at k = −87. It is RELATIVE, so resolution-independent. FIX: tighten `tolerance` (1e-4 → 8x finer, 3 extra bisections) — recommended, NOT authorized, and it would move every recorded S_L onto a new lattice | — | every convergence study of S_L: TD-029, TD-027 C2, TD-004 C6a, TD-005, TD-006 | `technical_debt/TD043_eigenvalue_lattice/audit.md` |
| TD-044 | P3 | **A CLI test writes its working directory into the REPOSITORY ROOT.** Running the suite from the repo root leaves `cli_log_level_valid_cavity_cli_smoke/` beside `CMakeLists.txt` — a complete case tree (`case.json`, `mesh.json`, `physics.json`, …) plus `results/solution.vtk`, `fields.csv`, `metadata.json`, `residuals.csv`, 10 files. It is run OUTPUT, not a fixture: the real fixture is `tests/data/cases/valid_cavity_cli_smoke`. It was NOT gitignored, so a blanket `git add` would have committed it — found during step 9's §11.5 audit. Now ignored via `/cli_log_level_*/`; the test should write to a temp directory instead | OPEN | — | nothing; it pollutes the work tree and risks being committed | found in step 9's generated-file audit; `.gitignore` carries the exclusion and the reason |
| TD-045 | P2 | **The repository has NO line-ending guard — not a weak one, none.** Three mechanisms each independently let CRLF through: `.gitattributes:9` `*  -text` disables all normalization and OVERRIDES `core.autocrlf=input`, so bytes are stored verbatim; `.clang-format` sets neither `LineEnding` nor `DeriveLineEnding`, so clang-format-18 DERIVES the ending per file and all 3 fully-CRLF `.cpp` files PASS inside the §7 742-file set; `git diff --check` does flag CRLF but only for files in the diff, and those 3 have not been re-staged since they landed. This is why a false claim about line endings stood through two commits | **OPEN** — carved out of TD-041 2026-10-11, which records the STATE; this records the MACHINERY | — | nothing (it is a detection gap, not a behaviour defect) | `technical_debt/TD034_format_and_eol/correction_2_crlf_in_head.md` §3 |
| TD-046 | P1 | ~~The ONLY test of `lambda(T)` in the flame solver fails in its own setup and has NEVER run its solves~~ — the fixture `LinearProperty(0.05, 300, 4.0e-5)` gave `lambda(1800) = 0.11` (2.2x) against the `> 3x` its own precondition demands, so the test died at **6 ms** and everything after line 627 had never executed | **RESOLVED** 2026-10-11. Slope DERIVED from the untouched assertions (`> 3*lambda_ref` needs `s > 6.667e-5`; the 1.3x flame-speed assertion needs only `4.6e-5`, so the precondition binds) and set to `1.0e-4` = 4x, clearing it by 33 %. **All five frozen criteria PASS in 2866 s**: a 4x conductivity gives a **1.81x** faster flame where a frozen lambda would give exactly 1.00x, and a 1.59x thicker front. **0 assertion lines changed** (audited on the diff); no production file touched. G8 drops from 2 failures to 1. Two unsought confirmations: `dt` fell exactly 4.0000x (confirming `:718`), and `|sumY-1|` rose 4.75x where TD-003's floor model predicts ~4x — its first independent check | — | nothing | `technical_debt/TD046_lambda_t_fixture/verification.md` · GATE.md `0df08809b21ae1c5...` · logs/00, logs/01 |
| TD-047 | P1 | ~~A reacting run whose every linear solve FAILED exits 0~~ — `out.status` was set at `:501` from the FLOW solve and the reacting block at `:785-1040` never revisited it, while `anyLinearSolveFailed` had one writer and ZERO readers. **AND IT WAS WORSE THAN REGISTERED:** the CLI iterates `ProjectRunResult::speciesResults`, a field the reacting path does not fill (it fills `ReactingRunResult::speciesResults`), so a reacting run printed NO species lines either — the failure was invisible in BOTH channels | **RESOLVED** 2026-10-11. All six frozen criteria pass. Rule DERIVED from `statusFor`'s existing treatment of the flow's own `MomentumFailure`/`PressureCorrectionFailure` → `NumericalFailure`; `Cancelled` preserved. Put in a PURE function `statusWithReactingOutcome` so the 6x2 space is exhaustively testable without engineering a failing solve. Exit code **0 → 4** on failure, still 0 on success. CLI now prints **10** reacting lines where it printed none. `anyLinearSolveFailed` has **2 readers**, was 0. 125 insertions, 0 deletions. **Also the first end-to-end reacting run there has ever been** (80 cells, 20 steps, A/B/N2 + temperature all converged) | — | nothing | `technical_debt/TD047_reacting_failure_escalation/verification.md` · GATE.md `fcb95e58700c9cb9...` |

All paths are relative to `results/`.

---

# 3. P0 — Qualification / Correctness Blockers

## TD-003 — Reacting-flow linear-solver robustness · OPEN

**Root cause.** BiCGSTAB broke down on a lower-bidiagonal, strongly non-normal matrix and returned
`-3.05e12` **while reporting `SolverStatus::Breakdown`**. Restarted GMRES(30), the library default,
stagnated on the same matrix. No existing P18 test helper checks the status.

**Partly addressed, and it matters where.** TD-001, TD-002 and TD-004 each check `SolverStatus` on
every solve they add, and the flame path judges the residual against `||b||` rather than the status
— so *new* code is covered. What remains is the repository-wide audit and the GMRES sizing question.
A later measurement found the flame's `worstLinearResidual` pinned at exactly its `1e-9` acceptance
fraction, with `|ΣY−1|` at `6.29e-07` against a `1e-8` test gate, which is consistent with a
**tolerance-limited** solve and is why this item blocks TD-004's completion rather than merely
preceding it.

**Acceptance criteria.** Every reacting-path solve's status checked or its residual judged against
`||b||`; a solver demonstrated adequate on the plug-flow matrix class; the `1e-8` `ΣY` test gate in
`G8PremixedFlameTest` reachable.

## TD-006 — Close P18 G8 · BLOCKED

**The gate is NOT to be altered.** Its criterion is sound (`FINAL_QUALIFICATION.md` §4), and the
reference is frozen before measurement: **35.0–37.0 cm/s**, Hermanns (2007) DOI 10.6100/IR630126;
mechanism Westbrook & Dryer (1981) Table II Set 3, DOI 10.1080/00102208108946970.

**Controls required.** Reactions disabled must fail; `10× A` must move `S_L` outside the spread.
**Negative control 1 currently cannot run** — see TD-020, which is therefore a dependency.

**A precision precondition, established before any measurement.** The extraction method's own
reproducibility is **4.3 %** (drift window 32 → 4.50 cm/s; 64 → 4.32 cm/s) against a band of
**±2.8 %**. A method that cannot resolve the band cannot decide the gate near its edges. So:
method reproducibility ≪ 2.8 % **first**, then freeze `cp` and `λ(T)` with citations, then measure
once. A gross failure is still decisive at 4.3 %; a near-miss is not.

**The order is integrity-critical.** Every current flame test uses a **synthetic** fuel
deliberately — debugging against methane would mean every decision was taken with the gated answer
visible (`g8_reference.md` §2).

## TD-007 — Re-run P18 final qualification · BLOCKED

All eleven gates re-run after G8 closes. Fingerprint
`83d43668dfd4b19877831b55ab44e187726f774d34e5ac68b591f1e8b2c3384d` (433 files) is the pre-change
reference. The 4 inherited Debug+GUI timeouts are **required** by `P18_GATES.md` G11 and must not be
"fixed" by raising the cap.

---

# 4. P1 — High Priority

## TD-004 — 1D premixed-flame capability · **OPEN / STOPPED AT C2**

**Root cause, measured.** Recentring translates the profile by a whole number of cells, which is
exact in *interpolation* and not in *conservation*: each shift **discards** `|s|` cells at one end
and **fabricates** `|s|` at the other from a fill value. A single one-cell shift moves `9.97e-07` of
fuel; at the 164-cell offset the march actually reaches, one shift moves **84 % of the domain's
entire fuel inventory**.

```text
relaxT  march   fuel bal      x_front  T_peak   recentr   recentr/march
  10.0   3968   4.327958e-02  3.50000  1800.00      920      23 %
  30.0  11905   6.343790e-01  3.50000  1800.00     8857      74 %
  90.0  35717   6.343790e-01  3.50000  1800.00    32669      91 %
```

The front is **not** pinned and the flame is **not** dying — `x_front` is exactly the anchor and
`T_peak` exactly `T_ad` at every length. What scales with the error is the recentring count.

**Authorized route (a):** make recentring conservative — the operator must *report* exactly what it
moved, so the balance attributes frame motion to the frame. In a finite window material must enter
and leave, so an operator that changed no totals would not be a shift.

**ROUTE (a) WAS EXECUTED, AND THE GATE STOPPED AT C2.**

```text
gate      technical_debt/TD004_premixed_flame/GATE.md
          sha256 12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73  (unchanged)
C2        balance must not degrade with march length    required <= 2.0
          measured 46.9362      (the pre-fix baseline was 14.6577)
C1 C3 C5  PASS      C4 FAIL      C6 PARTIAL      C6a C6b C7 NOT REACHED
```

The fix itself worked and is **kept, not reverted**: the worst single-step front jump fell
`327.51 -> 5.13` cells, the largest applied shift `164 -> 4`, recentrings at `relaxT 90`
`32 669 -> 241`, the reference-configuration balance `4.33 % -> 1.53 %` (now under C3's `0.02`), and
`S_L` is unchanged at `4.86230 cm/s`. The locator was latching onto a crossing the front could not
have reached; that is gone.

**The remaining blocker, RESTATED 2026-10-05 after TD-005's field dump refuted the flashback
reading.** The original wording is preserved in that item's `summary.md` §4.1, marked in place.

```text
MEASURED (TD005_flame_speed_extraction/logs/02): in the inadmissible state the inlet sits at
300.18 K and the outlet fuel is at round-off -- there is NO flashback and NO breakthrough. The
state is a sensible flame reaching T_ad exactly, whose front sits ~46 cells UPSTREAM of the
anchor. Correcting that needs a ~46-cell frame translation, which route (a)'s admissible-jump
bound CORRECTLY refuses; nothing then corrects it; and result.flamePosition reported the ANCHOR
regardless, so no instrument in the suite could see it.

So TD-004's refusal counters were RIGHT and read wrongly twice (spurious crossings, then
flashback). The reporting half is fixed under TD-005 (C4d) and there is now a test that FAILS
when it happens. WHY the front settles off-anchor is still unknown.
```

**C2 WAS AMENDED, 2026-10-05, on authorization.** Its premise — a monotone degradation with march
length — is false: the signal is a constant-amplitude limit cycle (TD-028), so there was no ratio to
bound and **no fix to the recentring operator could ever have satisfied it.** `GATE_A1.md`
(sha256 `e57bc758ca5610eb7431438537de3e3211a884a9ed8a33adcefe0d3694eed42e`) replaces C2 with a
three-clause criterion — REACHES / STAYS / SETTLES — at the tree's own `0.05` bound, with no fitted
constant in the settling clause. `GATE.md` is **unmodified**. The fresh rerun
(`verification_A1.md`, `logs/A1_01`) also **FAILS**, at (b) and (c), but now for two **named open
defects** (TD-028, TD-030) and it is demonstrably satisfiable. The original `46.9362` is preserved
as a real measurement of a real quantity; the criterion was wrong, not the number.

Five successive diagnoses were refuted along the way, each by the next measurement; the chain is
recorded in `summary.md` §3 because the pattern matters more than any single step, and
`verification.md` §3 is marked **INVALID AS AUTHORITATIVE** in place where a later measurement
(`maxFrontCrossings = 1`) refuted it. The locator fix also carries a **~2x** wall-time cost, which
is likely to fail the gate's definition-of-done clause 4a on its own; that was not measured, because
the run stopped at C2 first.

**Also found and NOT in scope:** the two "independent" conservation balances are **one measurement**
(`e/f = 1.00000`), and `ΣY` is tolerance-limited (TD-003).

## TD-005 — Flame-speed extraction · **OPEN / PARTIAL**

The measurement **method must be frozen before the answer is looked at**, and the definition derived
from the formulation rather than selected once numbers are visible.

**GATE FROZEN** at `technical_debt/TD005_flame_speed_extraction/GATE.md`
sha256 `6a7ccf3cc038a310b99059b75c97062ea7564a25f4c274bbc7e110b4d9de286d`, with the dry-run complete
for all ten criteria before the freeze.

```text
C1  PASS   C2  PASS   C4a PASS   C4b PASS   C4c PASS   C4d PASS (FIXED)   C7  PASS
C3  FAIL   C6  FAIL   C5  PARTIAL (the no-flame control is unmeasured -- TD-020)
```

**RESOLVED within this item — C4d.** `result.flamePosition` was assigned `xAnchor`, i.e. the INPUT
`anchorFraction * domainLength`, and that was its only assignment anywhere. It reported
`3.49999999999999964e-03` identically in a healthy flame (real front 3.5233 mm) and a broken one
(2.5684 mm), and the test guarding it compared that input against the same expression — an identity
that could not fail. Now measured from the reported field, with every computed quantity proved
**bitwise unchanged** by a two-library control whose reconstruction matched the frozen hashes.

**The recorded 4.3 % blocker does not reproduce** (it predates `relaxationTimesPerTrial`); the drift
window now spreads `0.068 %`. But the honest replacement is **2.19 %**, from
`relaxationTimesPerTrial` — a knob no test varies — against G8's `2.8 %` half-width. So §3.1's
`<< 2.8 %` precondition is **still not demonstrated**.

**STILL OPEN, and why the item does not close:**

```text
C3  5 of 12 swept configurations return a state violating fuel conservation by 24-37 %, while
    the in-tree insensitivity tests pass on S_L alone. THE CAUSE IS NOT KNOWN: insufficient
    relaxation (refuted twice -- the longest trial and the most relaxed run are both bad),
    inlet proximity (explains one row only) and bracket width (refuted over a 100x range) are
    all dead, and no fourth hypothesis is offered.
C6  the consumption identity PASSES on its own (0.00777 < 0.01); the test fails on TD-003's
    tolerance-limited SigmaY bound (6.29e-07 vs 1e-8), which is a REAL defect detector and so
    is not migrated, loosened or removed -- only stopped from masking a passing result.
```

**NEW KNOWN FAILURE, created by C4d's fix and reported rather than reclassified:**
`TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength` was PASS (997 s) and is now **FAIL** at
`test_premixed_flame.cpp:409` — the assertion that was an identity. At anchor 0.30 the front sits
**45.8 cells** upstream of the anchor against a 10-cell tolerance. The frozen gate does not name it,
because at freeze time the clause could not fail at all. Its cause is C3. Evidence:
`technical_debt/TD005_flame_speed_extraction/summary.md` §5.

**A degeneracy that must not return.** A fixed point on the consumption integral gives
`S_c ≡ mdot/ρ_u` **identically**, for any `mdot` admitting a steady solution — it reproduces
whatever guess it was given and converges instantly. The integral is still computed, as a sharp
*consistency* check; it is **not** validation. The control is that `S_L` be independent of the
initial guess, which currently **PASSES**.

**Controls.** The extractor must FAIL with no flame, with an unconverged solution, and with reaction
disabled.

## TD-008 — P17 HLLC positivity defect · INHERITED

Star-density positivity fails for a characterized class of admissible head-on input; clauses
(a)(b)(c)(d)(f) pass, **(e) fails**. P17's, recorded here so the reacting work knows the envelope.

## TD-009 — Debug+GUI timeout contract · INHERITED

Six separate recordings across P18.1/P18.3/P18.5/P18.9 and P17 G10, all within ~6 % of the frozen
7200 s cap. **Raising the cap is forbidden.** A future gate must predict the *branch*, not a number.

## TD-018 — Repository hygiene · OPEN · PUSH BLOCKER

259 `git diff --check` warnings in exactly three pre-existing files — `cuda/CMakeLists.txt` (152),
`tests/unit/gpu/CMakeLists.txt` (75), `tests/unit/compressible/CMakeLists.txt` (32) — all CRLF
against an LF HEAD, so every line is flagged. Plus 28 pre-existing `clang-format` violations.
**A repository-wide line-ending conversion is forbidden**; convert per file, with its own diff
reviewed. CLAUDE.md §11 requires `git diff --check` clean before any authorized commit.

---

# 5. P2 — Medium Priority

## TD-010 — `FixedGradient` coverage / turbulence gradient lag

`test_transport_characterization.cpp` has **zero** `FixedGradient` coverage, which is why the lag
survived P14's own migration gates. Flipping turbulence to `Exact` needs its own gate **and** a
re-derivation of the P14 migration oracle, whose frozen values encode the lagged behaviour.

## TD-011 — Variable-`cp` `TotalEnergyState` / variable-γ EOS

Lifting the check touches four `IdealGasEOS` entry points and widens the accepted domain of a type
feeding the HLLC solver — **whose TD-008 positivity defect is still open**. Needs its own gate.

## TD-012 — Reversible-chemistry equilibrium consistency

A caller can supply a thermodynamically inconsistent forward/reverse pair and nothing detects it.
**Faking `K_eq` is forbidden**; it needs the Gibbs route and full thermodynamic polynomials. Until
then this stays a documented limitation rather than a papered-over approximation.

## TD-019 — Implicit chemistry Jacobian / non-zero Patankar `Sp`

Carved out of TD-002 when its budget half closed. `d(ω_i)/d(Y_j)` does not exist, so `Sp` is zero.
P18.5 added the *temperature* derivative; the *concentration* derivative is still absent.

**Risk is LOW for the mechanisms in scope, and that is measured, not assumed:** TD-002's audit found
the budget to be round-off, not stiffness; accuracy still improves under refinement
(`1.01e-01 → 2.14e-02 → 2.14e-03`), and a sub-stepped explicit march now reaches 85 472 sub-steps at
zero drift. **Building a Jacobian on speculation is forbidden.**

## TD-020 — No early-out when a mixture cannot sustain a flame

`WithReactionsDisabledNoFlamePropagates` **TIMES OUT after 900 s**. It is **G8 negative control 1**,
so while it cannot run, G8 cannot close with both controls exercised — which makes a P2 item a
**TD-006 dependency**.

With reactions off the seeded front washes out slowly rather than vanishing, so early trials still
locate a front and return a sided verdict; the bracket then grinds through `maxOuterIterations = 2000`
at up to 20 000 steps each. The `!sided → NoFlame` short-circuit fires only once the front is gone,
so **the no-flame case costs more than the flame case**.

**Forbidden:** raising the test's timeout, or lowering `maxOuterIterations` in the test. Both make
the control finish without making it work.

## TD-027 — `maximumCellDiffusionNumber` validated · **REOPENED** 2026-10-07 (was RESOLVED 2026-10-06)

Found by TD-005 (`TD005_flame_speed_extraction/dry_run.md` §M). The premixed-flame march derives its
time step from `min(cellCourant*dx/u_in, maximumCellDiffusionNumber*dx^2/alpha)`. The header states
these are *resolution* limits rather than stability limits, which is correct for implicit Euler —
**but the value 20.0 was never validated against a measured error.**

```text
dt ladder at FIXED dx (500 cells), r = 2, the asymptotic family {dt/2, dt/4, dt/8}:
   dt/1  D = 3.333   S_L 4.86230   +3.99 %   <- THE DEFAULT, outside the asymptotic range
   dt/2  D = 1.667   S_L 4.65615   -0.42 %
   dt/4  D = 0.833   S_L 4.67194   -0.08 %
   dt/8  D = 0.417   S_L 4.67510   -0.02 %
Richardson limit 4.67589 cm/s, GCI 0.021 %. G8's band is +-2.80 %.
```

**`dt` sits EXACTLY at the cap**, so `D` is identically `maximumCellDiffusionNumber` = 20 — not a
loose bound. (TD-027's audit §1 corrected TD-005's original figures, which used the unburnt
diffusivity rather than the burnt `alpha_max` the code takes at `PremixedFlame.cpp:481`, 6x larger.)
And since `dt = cap*dx^2/alpha`, **`D` is independent of `dx`** — refining the mesh cannot improve
it, which is why TD-029's mesh-refinement test could never have detected this.

**Every `S_L` figure in this repository is about 4 % high**, including the `4.86230` quoted
throughout TD-004 and TD-005. The same cap admits TD-028's limit cycle, which a lower cap suppresses.

**AUTHORIZED 2026-10-05; cap = 5.0 chosen; FIX APPLIED and VERIFIED.** Gate frozen at
`technical_debt/TD027_cell_diffusion_cap/GATE.md` sha256
`56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75`.

```text
C1 the cap IS the realized D   PASS  D = 4.999999999999999, rel 1.78e-16 against 1e-12
C2 S_L within the budget       PASS  0.084 % against 0.28 %   (old default was +3.99 %)
C3 reported precision coherent PASS  1.7x against 2x          (old default was 80x)
C4 the limit cycle suppressed  PASS  max-min 8.07e-04 <= mean 8.48e-02, by 105x
C5 nothing outside the flame   PASS  343 / 331 / 32
C6 no case breaches 7200 s     PASS  longest 1791 s, 0 timeouts, suite 2.45 h
```

**RESOLVED 2026-10-06.** The gate had no criterion for the flame tests' own verdicts, and two
cases moved `PASS -> FAIL`. **Amendment A1** (`GATE_A1.md`, sha256
`f5ac7f98a75378aab67c0337c24638c7f4bd2a713403c8941e3b6ad3a6f4883d`) adds **C7**: every flame-suite
verdict movement must be NAMED with its cause and owning item, and any movement the amendment does
not name FAILS C7. Twelve movements, twelve named. **C7 changes no threshold** — it closes an
accounting gap so that CLAUDE.md §8's *"only the failures a gate names"* can be satisfied truthfully
rather than by omission. **C1–C7 ALL PASS.**

The two `PASS -> FAIL` cases are **real failures owned elsewhere**, and naming them does not make
them acceptable: `ItConverges...` is **TD-030** (the converged balance `0.0847` fails `< 0.02` by
4.2x regardless of march length) and `TheFlameSpeedConverges...` is **TD-029** (the error
cancellation removed). Both stay OPEN.

**Also corrected during the item:** the claim that this cap GOVERNS TD-028's limit cycle is refuted
— the cycle persists at `D = 5` on a 250-cell mesh, and `dt = 3.7699e-05` produces it at both
`(cap 5, 250 cells)` and `(cap 20, 500 cells)`: same `dt`, different `D`, same behaviour. `GATE.md`
and `audit.md` are left bit-identical to their frozen hashes and the correction is recorded forward
in `attribution_correction.md`. **TD-028 is suppressed at the gated fixture, not resolved.**

Evidence: `technical_debt/TD027_cell_diffusion_cap/summary.md`.

**Also established by TD-027's stage-D control** (`logs/08`, a two-library comparison whose
reconstruction matched the frozen hash): `WithReactionsDisabledNoFlamePropagates` passes in **42 s
at the OLD cap**, so TD-027 gets no credit — but **G8's negative control 1 is AVAILABLE**, which
TD-020 recorded as a TD-006 blocker on the strength of a 900 s timeout that is no longer real.

**P0 because it falsifies a gated quantity by more than the gate's own band.** Correcting the
default would invalidate recorded `S_L` figures in two items' frozen evidence, so it needs its own
authorization, its own gate, and a decision about the existing numbers.

### C4 IS INVALID AS AUTHORITATIVE — recorded 2026-10-06, after closure

C4 measured relaxT 18/22/26/30, whose march budgets are 30949, 37827, 44705 and 51583 — **all four odd**, because consecutive levels differ by 6878, an even number, so the ladder preserves step parity by construction. TD-032 shows the march alternates between two interleaved sub-sequences on consecutive steps, so C4's `max-min 8.07e-04 <= mean 8.48e-02` is four samples of ONE branch, not a convergence measurement. **C4 cannot stand as measured.**

The shipped fix (`maximumCellDiffusionNumber 20.0 -> 5.0`) is still justified by C1, C2 and C3, which do not read the march's field and which are the reasons the value 5.0 was chosen. Whether the item's CLOSURE is revisited is a user decision, recorded here and in `TD031_final_march_length/PARITY_FINDING.md` §6 rather than acted on.

## TD-028 — the premixed-flame march executes a numerical limit cycle · **P0**

Found by TD-005 (`dry_run.md` §G–§N). At the default `dt` the front oscillates between the anchor
and the inlet in a nearly **square** limit cycle: period **14 relaxation times** (5558 march steps,
0.2095 s), amplitude ~165 cells, **undiminished at 8 periods** over an 11.6x span of march length.
Fourteen measurements and not one intermediate position; one period parameter predicted all 17
verdicts across three ladders.

> **THE CHARACTERISATION ABOVE AND THE `dt` ATTRIBUTION BELOW ARE INVALID AS AUTHORITATIVE — 2026-10-06.**
> There is no limit cycle of period 14. TD-032 found the root cause: the transient term's old
> time level is one step stale, so the march runs two interleaved sub-sequences and the reported
> field alternates EVERY STEP. The period-14 model was fitting the PARITY of ~397 steps per
> relaxation time, where integer rounding makes it effectively arbitrary along a ladder; the
> "one period parameter predicted 17 verdicts" was 17 parities. The measurements are preserved
> verbatim; the model built on them is withdrawn. See
> `technical_debt/TD032_lagged_time_level/audit.md` and `TD031_final_march_length/PARITY_FINDING.md`.

```text
NOT the recentring operator  cleared twice -- a 10x dead-band range, and recentring DISABLED
                             entirely (0 recentrings, identical pattern)
NUMERICAL                    the period moves with the mesh (14 -> ~18 when dx doubles), and
                             Ze(Le-1) = 0 puts this flame far below any pulsating threshold
GOVERNED BY ABSOLUTE dt      NOT by the cell diffusion number. TD-031 logs/01 measured the cycle
                             at D = 5 on a 250-cell mesh -- the same D at which it is suppressed
                             at 500 cells -- and dt 3.7699e-05 produces it at BOTH (cap 5, 250
                             cells) and (cap 20, 500 cells). Boundary bracketed only between
                             dt 9.42e-06 (no cycle) and 1.88e-05 (cycle); no threshold derived.
SUPPRESSED, NOT RESOLVED     TD-027 suppresses it at 500 cells / cap 5, the gated fixture, and
                             nowhere else that has been measured. This item stays OPEN.
```

**An earlier attribution in this entry and in TD-027's frozen evidence — that the cap governs the
cycle — is refuted.** The frozen files are left bit-identical to their recorded hashes and the
correction is recorded forward at
`technical_debt/TD027_cell_diffusion_cap/attribution_correction.md`.

**Consequences:** no march length converges, so *"march until the state stops changing"* is unsound
as a design — which is why `finalMarchTolerance` was found unreachable by construction (F13). There
was no steady state to reach. The 29–57 % duty cycle predicts TD-005 C3's 5-of-12 failure rate, and
**TD-004's C2 bounds a monotone trend that does not exist.**

## TD-029 — two PASSING tests pass by error cancellation · **P0**

Found by TD-005 (`dry_run.md` §L.3, §O.4). Both currently report success on a solver that is
converged in neither parameter.

```text
TheFlameSpeedConvergesUnderMeshRefinement       refines CELLS; dt is DERIVED from dx. The spatial
   (PASS, 301 s)                                and temporal errors have OPPOSITE SIGNS (+0.81 %
                                                and -4.24 %) and partially cancel. The mesh family
                                                is NOT self-similar, so its observed order is
                                                meaningless -- the defect CLAUDE.md §6 names.
TheConsumptionIntegralAgreesWithTheEigenvalue   asserts mismatch < 0.01, measures 0.00776 at the
   (fails only on TD-003's SigmaY bound)        default dt. At the CONVERGED dt the mismatch is
                                                7.892 %. It passes where two errors cancel.
```

**Neither test is wrong about what it measures; both measure the wrong family.** A valid convergence
study needs `dt` and `dx` refined **independently**, which no test, probe or gate in this repository
had ever done before TD-005's `logs/17`/`logs/18`.

## TD-030 — the chemistry is stiff against a transport-only `dt`; the split is first order · **P1**

Found by TD-005 (`dry_run.md` §P). `result.chemistrySubSteps` shows the limiter binding at
**~500 sub-steps per march step at the default settings** — the chemical timescale is ~500x shorter
than `dt`, and nothing in the `dt` derivation references the chemistry at all.

```text
dt       = 9.424e-06 s    from min(Courant*dx/u_in, D*dx^2/alpha) -- TRANSPORT ONLY
dt/500   = 1.885e-08 s    what the chemistry is actually being resolved at
```

So a **first-order Lie split** runs at ~500x the chemical time, which is the regime where the
splitting error is large. **Leading candidate** for the 7.892 % reaction-integral excess that TD-005
C3 now rests on, and it would explain why that mismatch GREW when `dt` was refined. **Not
established:** testing it needs Strang splitting, a `dt` from the chemical timescale (~470x the
cost), or `omega` integrated as applied rather than recomputed.

**Also found:** `S_c` is not a usable consistency instrument in this regime — it swings **167 %**
under a knob that moves `S_L` by **0.135 %**. That weakens TD-005 C6 and any G8 validation resting
on the consumption identity.

## TD-031 — `finalMarchRelaxationTimes = 10` reports a TRANSIENT state · **P1**

Opened 2026-10-06. Found by TD-027's regression; audit at
`technical_debt/TD031_final_march_length/audit.md`.

The final march exists so that *"the state is marched again at that mass flux until the STATE stops
changing, and it is that state whose profiles and balances are reported."* Measured at the validated
cap (TD-005 `logs/19`, 500 cells, `D = 5`):

```text
relaxT   front offset   fuel balance   delta mm
   10      -34.25 c      8.602e-02      0.9909    <- THE DEFAULT, still transient
   14     -124.70 c      4.395e-01      0.5505
   18       +1.46 c      8.477e-02      1.2781    converged
   22       +2.01 c      8.464e-02      1.2779    converged
   26       +2.56 c      8.454e-02      1.2780    converged
   30       +0.11 c      8.636e-02      1.2778    converged
```

**The march converges by relaxT 18 and the default stops at 10.** At the default the front sits 34
cells off the anchor and `delta` is 22 % short, so the reported state — the one every profile,
balance and consistency check is computed on — is a transient by default.

**Invisible until TD-027** because the march never converged at the old cap at all: it executed
TD-028's limit cycle, so there was no converged state for relaxT 10 to fall short of, and the phase
it stopped in happened to read `1.53e-02` — *better* than the true converged `8.47e-02`. Same shape
as TD-004's shifts masking its C2, and the cycle masking TD-030.

**What it fixes:** `ThePlateau...`'s front offsets (32.4 and 35.6 cells against a 10-cell tolerance,
becoming +1.5 to +2.6) — a FAIL becomes a PASS — and `TheEigenvalueIsIndependent...`'s 20 % `S_c` /
30 % `delta` spread between initial guesses.

**What it does NOT fix, and this corrects an earlier claim of mine:** TD-027's two `PASS -> FAIL`
transitions. `ItConverges...` fails at the CONVERGED balance too (`0.0847` against `< 0.02`, by
4.2x) — that is TD-030; and `TheFlameSpeedConverges...` fails on growing refinement differences,
which is TD-029. The march length explains their MAGNITUDES, not their FAILURES.

**The derivation question before any default is chosen:** the convergence point of 18 was measured
at one mesh, one cap and one fixture. `relaxT` is nominally mesh- and `dt`-independent, but TD-027
established that nominally physical is not the same as measured-invariant. Setting `20.0` because
`18` was seen once would repeat exactly the error TD-027 exists to correct, so the gate must
establish the convergence point across at least a mesh pair and a cap pair first.

## TD-032 — the march's transient term used a time level ONE STEP STALE · **P0**

Opened and fixed 2026-10-06/07. Full evidence:
`technical_debt/TD032_lagged_time_level/` — `audit.md`, `GATE.md` (frozen
`52bc82554574e7b7…`), `dry_run.md`, `verification.md`, `attribution.md`, `summary.md`, 6 logs.

`advance()` copied the transient term's old time level **before** the state was updated, so from
step 2 of any march it trailed by one step. The transient path then connected step `k-1` to step
`k+1`, splitting the march into two interleaved sub-sequences coupled only through the operator
coefficients — and two sub-sequences with no transient coupling need not share a fixed point. The
reported field alternated **every step** between the cold preheat zone and a nearly fully burnt
state 1484 K away, and the reported front between the anchor and the inlet.

```text
WHY IT SURVIVED REVIEW   the first step of every march is seeded old = current, so a one-step
                         check passes; the SAME three-line block is CORRECT for `densityOld`,
                         which is consumed within the step; and the transient tests in
                         tests/unit/thermal and tests/unit/transport hand an assembly an old level
                         and check what it BUILDS -- the assembly was always right, the caller
                         passed the wrong field, and no assembly-level test can see that.
THE FIX                  capture the two fields at the TOP of advance(). Two statements moved, no
                         copy added, so the expected cost change is zero.
GATE                     all ten criteria PASS. R = across/within went 5.609e3 -> 0.50003 at 500
                         cells and 5.192e2 -> 0.50004 at 250, against a derived bound of 1.0 whose
                         smooth-sequence value is 0.5. S_L did not move (0.000 %). The per-step
                         change now FALLS 5.6x over a 44 % longer march where before it did not
                         fall at all. Sanitizers clean, 528 tests, zero diagnostics. A clean
                         rebuild reproduced the library bit for bit.
```

**THE BLOCKER.** `G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial` was recorded PASS
twice pre-fix (744 s, 759 s) and now FAILS: at `driftWindow` 64 the solver returns `NoFlame`. A
two-library comparison, both source forms hash-proved and the pre-fix library byte-identical to the
frozen lagged one, **attributes the transition to this fix**.

**What that does and does not establish.** It establishes causation. It does **not** establish that
a capability was lost, because the pre-fix PASS was computed on the alternating march, whose hot
branch crosses `T_anchor` everywhere — so a flame was found whatever the physical state. A third
reading, which the measurement cannot discriminate, is that the fix turned a *false* pass into a
*truthful* failure. `attribution.md` §4 states both and §5 names the cheap probe that separates
them; it is not authorized.

**The fix is RETAINED** — it is correct about the time level and the gate measures that. Per
CLAUDE.md §12 the item stops with the failure and its attribution preserved.

**Consequence for the register.** Every field-derived premixed-flame quantity recorded before
2026-10-06 was measured on the alternating field: TD-004's C2 and its front-displacement premise,
TD-005's C3 and C6, TD-027's C4, and the whole of TD-028, TD-029, TD-030 and TD-031. Each must be
re-measured under its own gate and its own authorization.

## TD-033 — a G8 test asserts a precondition its own constants cannot satisfy · **P2**

Found by TD-032's C7 cost run. `AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue`
fails at `test_premixed_flame.cpp:624` on
`EXPECT_GT(risingConductivity.value(kExpectedAdiabatic), 3.0 * kConductivity)`, evaluating
`0.11 > 0.15` — false. It fails at **0 ms**, before any solve, so it is provably independent of
TD-032 and of any solver behaviour at all.

Same class as TD-005's C4d and TD-026: an instrument that cannot do the job it names. Here the test
can never reach its own body, so whatever it was written to check has **never been checked**. The
fix is to derive the ramp constants so the precondition holds, not to relax the precondition — and
then to find out whether the property it guards actually holds.

## TD-034 — `clang-format` is not clean on this tree · **P2**

Found by TD-032's scoped regression. Violations in
`apps/gui/tests/test_cli_gui_equivalence.cpp` and `include/cfd/app/ProjectRunner.hpp` (TD-001 /
TD-002 territory). Neither file belongs to TD-032, whose own files are not flagged.

CLAUDE.md §8 requires `clang-format-18 --dry-run --Werror` clean as part of the full regression and
§11 requires it before any commit, so **this blocks every commit from this tree** until fixed. The
violation count recorded is a floor, not a total: the harness piped the output through `head -20`.

## TD-035 — the full sanitizer regression is impossible as specified for G8 · **P1**

Found by TD-032's scoped regression, by measurement rather than estimate.

```text
ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature
   Release    303 s
   ASan     11612 s      = 3.2 h  = 38x

G8 total under ASan        4343 s x 38  ~  46 h
worst single test          1283 s x 38  ~  13.5 h
ctest --timeout 7200                    =  2 h
```

So at least three G8 tests would exceed the per-test cap and be recorded as **TIMEOUTs** — neither
pass nor fail, just truncated. The regression CLAUDE.md §8 defines therefore cannot return a usable
sanitizer result for the one suite that exercises the reacting-flow march.

**Raising the cap is not the fix** — that is exactly what TD-009 forbids as a reflex. The options
needing a decision are a stated-scope sanitizer subset, a cheaper fixture built for sanitizer runs,
or an amendment to §8's definition for suites of this cost. None is chosen here.

## TD-036 — `NoFlame` is mislabelled, and reports no reason · **P1**

Found by TD-032's discriminating probe. `PremixedFlameStatus::NoFlame` is documented in the header
as *“the anchor temperature is never crossed: nothing is burning”*. At `PremixedFlame.cpp:1746`
(bracketing) and `:1789` (bisection) it is set for a different condition entirely: a trial for which
`trialSaysTooLarge()` returned false — **a trial that could not be assigned a side**. Those are not
the same event, and the field may contain a perfectly good flame when the status says otherwise.

`result.diagnostic` is left **empty** on this path, while every other failure return fills it with a
state summary. So the one status that most needs an explanation carries none.

**The cost, measured.** TD-032's first probe asked the reported field whether it contained a
`T_anchor` crossing, because that is what the enum says NoFlame means. The field did contain one,
and the probe's auto-verdict concluded “NoFlame is a FALSE REPORT” — wrong, and wrong *because of
this label*. The real cause took a second probe and a code read to find. Same class as TD-005's C4d
and TD-026: **a reported quantity that does not mean what it says.**

**The fix is to separate the two conditions** — a distinct status for “no trial could be sided” — and
to fill `diagnostic` on both paths. Renaming alone would not do: callers that branch on `NoFlame`
need to keep working, so this needs its own gate and a check of every consumer.

## TD-037 — `driftWindow` is unguarded against trial length · **P1**

Found by TD-032's discriminating probe, and it is the **mechanism behind that item's blocker**.

A trial's drift is read from a sliding window, which needs **more than `driftWindow` steps of
history** before any drift exists. A trial that extinguishes sooner therefore yields no drift, no
side, and — via TD-036 — an unexplained `NoFlame` that aborts the whole search.

```text
measured, logs/08_why_noflame.log, SAME library, SAME mass flux, SAME trial:
  driftWindow 32   trial 3   mdot 0.0870611   steps 40   measured YES   drift 0.207889   sided
  driftWindow 64   trial 3   mdot 0.0870611   steps 40   measured NO    drift 0          UNSIDED
                   -> Converged (15 trials)          -> NoFlame (4 trials, bracketSteps 0)
```

Only the window differs. **Nothing in `validateSettings` or in the search relates `driftWindow` to
the step budget or to observed trial lifetimes**, so the constraint is real, unstated and unchecked.

**Why this surfaced now.** TD-032's time-level fix shortened the life of one trial to 40 steps,
crossing a boundary that was always there. The boundary is not TD-032's doing; being on the wrong
side of it is. Pre-fix the condition happened to hold.

**The fix is not simply “lower the default window.”** The sensitivity test
`TheAnswerIsInsensitiveToTheStepsPerTrial` exists precisely to vary this knob, so the search must
either cope with a window longer than a trial — by reading the drift over whatever history exists,
with its own validity floor — or refuse the configuration explicitly rather than reporting a flame
that is not there. Either way it needs a derived floor and its own gate.

## TD-040 — `recentre()` is not conservation-neutral · **P0**

Carved out of TD-004 on 2026-10-08, by a two-library measurement
(`TD004_premixed_flame/logs/A2_05_ledger.log`).

```text
                           PRE-FIX          POST-FIX        (TD-004's locator fix)
recentrings, total         121              223
   in the final march      1                3
fuel ledger / fuelThrough  -0.793 %         -2.523 %
per recentring, fuel       -3.533634e-06    -3.533419e-06   kg/m^2
per recentring, energy      2.120180e+01     2.120052e+01   J/m^2
S_L                         4.671937         4.955332       cm/s   (+6.07 %)
```

**The per-translation cost is identical across the two libraries, to 6e-05 relative.** That rules
out a mis-reporting ledger — `ledger / per-recentring` is exactly 3.0000 and 1.0000 — and shows the
ledger grew because the COUNT rose, not the cost per event.

**Each translation removes fuel and adds sensible energy.** `shiftField` fills the vacated end with
boundary values, so a shift is a small mass and energy source term that the balance never sees. At
−0.79 % of `fuelThrough` per event it is the same order as the reaction excess TD-030 owns and
larger than the inlet term.

**Why it surfaced now.** TD-004's blind locator let `recentre()` fire ONCE per final march, so the
residue was invisible. With the locator tracking, it fires 3x in the march and 220x across the
trials — and the trials are what set the eigenvalue, via bisection on drift signs. More translations
means more fuel removed, and a flame losing fuel needs more mass flux to sustain.

**TD-004's register entry records recentring non-conservation as “fixed”.** The ledger shows it was
**reduced, not eliminated** — and the ledger exists precisely to measure the residue. This item owns
that residue.

**Not established:** that the ledger accounts for the WHOLE 6.07 %. Same order and same sign is all
that is measured. Closing that needs the `dt` ladder re-measured on the fixed tree, to obtain a
Richardson limit not inherited from the blind-locator solver — at which point it also becomes
possible to say which of 4.671937 and 4.955332 is correct. **Neither is currently verified.**

## TD-026 — `worstFinalOffsetCells` cannot see the offsets it reports

Found by TD-005 while fixing C4d, and **not fixed there** because it is a different field and was
outside the authorized "fix the report" scope.

`result.worstFinalOffsetCells` is updated only on final-march steps where the **control** locator
accepted a crossing. That locator correctly refuses any crossing further than one step could carry
the front (~5.1 cells, TD-004), so a front that has drifted tens of cells off-anchor is exactly the
case it cannot record. TD-004's probes read `offCells 3.03 / 3.04` — just above the 3-cell dead band
— while the front was in fact ~46 cells out.

Same class as TD-005's C4d: a reported diagnostic that cannot report the condition it names. The fix
is the same shape — measure unconditionally for reporting, keep the refusal for control. The header
now warns against using this field; that warning is a stopgap, not a fix.

## TD-022 / TD-023 — GPU species transport; GPU variable-`cp` parity

P18.8's scope is thermochemistry and a closed reactor. Device species *transport* is not implemented
and variable-`cp` parity is not established. **These are debt, not a P18.8 downgrade** — the phase is
complete within its frozen, scoped contract.

---

# 6. P3 — Deferred

- **TD-013 — `A` unit safety · N/A as a defect.** `A`'s units depend on the reaction's total order,
  so there is no single dimensional form to check; **hard-coding one universal unit is forbidden**.
  The checkable part — the `R_u` mol/kmol basis — *is* checked.
- **TD-014 — thermochemistry ownership.** The split is deliberate: it makes G7's named confusion
  hard to write. Only the registry match links the two types.
- **TD-015 — historical probe reproducibility.** P18.4's probe cannot be rebuilt from its own
  evidence; its compile command was never recorded. P18.5 onward record it. **Historical evidence is
  not rewritten** — corrections go forward.
- **TD-016 — separate-process determinism.** Not required by G10; recorded rather than skipped.
- **TD-017 — validation-evidence wording.** `KNOWN_LIMITATIONS` L36 does not say which quantities
  its bound covers; `distorted_mesh_simple_mms` was not re-measured.
- **TD-021 — elemental metadata.** No elemental composition exists and §10 says not to invent one;
  mass conservation is enforced instead.
- **TD-024 / TD-025 — GPU reaction orders; end-to-end GPU reacting solve.**

---

# 7. Resolved / Superseded

Kept so that old unchecked notes are not mistaken for active work. **No historical evidence was
deleted.**

## Resolved by this campaign

| ID | What closed it |
| --- | --- |
| **TD-001** | 2026-10-03. `grep -c 'setup.reacting' src/app/ProjectRunner.cpp` is **3**, was **0**. `cases/reacting_plug_flow` drives parser → builder → dispatch → export → CLI. Took **two authorized gate amendments** (A1, A2), both after a failure was reported and preserved. Folded in and resolved with it: the multi-species shared boundary composition (`ΣY=1` across a patch, enforced at parse time) and the carrier/complementary-species declaration. Evidence: `technical_debt/TD001_reacting_production/summary.md` |
| **TD-002** | 2026-10-04, **the step-budget half**. `cases/reacting_plug_flow_march` marches from a case file: **85 472 sub-steps at 0.00 eps**, where the pre-fix integrator was refused after ~1 333 and *threw* when the accuracy control was tightened. `steps: 1` reproduces TD-001's five figures **bitwise**; P18.6 and P18.8 untouched, proved with **two libraries** over 140 swept states. The `d(ω)/d(Y)` half is **not** closed → TD-019. Evidence: `technical_debt/TD002_stiff_chemistry/summary.md` |
| **TD-032** | 2026-10-07, **gate only, TD-037 carved out**. `advance()` captured the transient term's old time level BEFORE the state was updated, so it trailed by one step from step 2 of any march and the reported field alternated between the cold preheat zone and a nearly fully burnt state 1484 K away. **Thirty historical verdicts in this campaign were predicted by step PARITY.** Fixed by capturing at the top of `advance()`; all ten frozen criteria PASS, `S_L` unmoved, the per-step change now falls 5.6x over a longer march. **REGRESSION PARTIAL** (Debug+GUI not run, sanitizers scoped, clang-format failing) and the G8 PASS → FAIL it caused is OPEN under TD-037. `technical_debt/TD032_lagged_time_level/summary.md` §8 |
| **TD-037** | 2026-10-07, **gate only, REGRESSION PARTIAL**. A trial that extinguished with no more than `driftWindow` positions of history was unsidable, so a legitimate setting silently aborted the eigenvalue search under a status claiming nothing was burning. Fixed by clamping the span to `min(driftWindow, available)` on BOTH extinction paths, floored at 4 — the floor the solver already used. All nine criteria PASS: identical eigenvalue at windows 16/32/64/128, 0 wrongly-sided trials, the window-32 record byte-identical, and the G8 method-insensitivity test passing with spread exactly 0. **The first attempt patched only one of two duplicated blocks and FAILED C1** — preserved in `logs/02`. `technical_debt/TD037_drift_window_guard/verification.md` |

## Resolved by a later phase

| What | Resolved by |
| --- | --- |
| Species boundary lag — the lagged gradient treatment on the species path | **P18.2** (G4(c), ≤ 2 outer iterations) |
| Conservative `ρY` transport | **P18.2** |
| Species-dependent diffusion | **P18.2** |
| `ΣJ` diffusive-closure residual | **P18.2** (G4, 25× margin) |
| Mixture properties / EOS collapse to P17 single-species | **P18.3** (G5, 0.56 eps) |
| Reaction source framework, `Σ_i M_i ν_i` closure | **P18.4** (G3, `Σω` exactly 0) |
| Arrhenius kinetics vs an independent arbitrary-precision reference | **P18.5** (G6, 11.05 of 32 eps) |
| Heat release and temperature feedback | **P18.6** (G7, 6.29 eps) |
| CPU/GPU thermochemistry equivalence, within P18.8's scope | **P18.8** (G9 under amendment A2) |

## Superseded

Five duplicate chains, collapsed. **19 of the 49 original per-phase TODO entries were duplicates of
5 root items** — the same facts re-recorded by each phase that tripped over them, exactly as
CLAUDE.md §2 requires.

```text
P18.4, P18.5, P18.6 (3 entries)                   "Sp is still zero"        → TD-019
P18.2, P18.3, P18.5, P18.7, KL 1.4 (5 entries)    "library-level only"      → TD-001 (RESOLVED)
P18.6, P18.7, KL 1.2 (3 entries)                  "explicit step budget"    → TD-002 (RESOLVED)
P18.3, P18.6, KL 1.x (2 entries)                  "variable cp refused"     → TD-011
P18.1, P18.3, P18.5, P18.9, KL 2.2, P17 G10 (6)   "Debug timeout contract"  → TD-009
```

The deduplication derivation is preserved at `results/technical_debt/DEBT_REGISTER.md`, which is now
the **historical TD-000 audit record**. This file is the authoritative current register.

---

# 8. Resolution Protocol

One protocol, stated once. Every substantial item follows it; it is **not** repeated per item.

```text
AUDIT        root cause from evidence, not assumption. Name what is already true.
   ↓
FREEZE GATE  criteria and thresholds DERIVED and frozen, with sha256, BEFORE any change.
             Dry-run every criterion against the unchanged baseline first; prove non-vacuity
             in BOTH directions — the control must fail AND the correct implementation must pass.
   ↓
REPRODUCE    the defect, through a real entry point, preserved verbatim.
   ↓
FIX          the smallest correct change. Root cause, not symptom.
   ↓
NEG. CONTROL each control demonstrated to FAIL the criterion it guards.
   ↓
VERIFY       criteria in frozen order. STOP at the first failure; preserve and report it.
   ↓
REGRESSION   exact counts, per configuration, on demonstrably rebuilt binaries.
   ↓
INTEGRITY    git status / diff --check; runtime noise restored; frozen hashes recomputed.
   ↓
EVIDENCE     audit · GATE · reproducer · implementation · negative_controls · verification
             · regression · summary, under results/technical_debt/<ID>/.
   ↓
RESOLVE      status updated here ONLY after all of the above.
```

**When a frozen threshold proves wrong:** preserve the failure verbatim, investigate without
changing production, derive the replacement from first principles (never from the observed output),
**request authorization**, freeze the amendment as a separate file with its own dry-run, and rerun
fresh reusing no earlier result. TD-001 required two such amendments; both are recorded.

**Never:** weaken a threshold, clip, silently normalise, loosen a conservation tolerance, raise a
timeout, or disable a control to obtain a PASS.

---

# 9. Current Target

```text
RESOLVED 2026-10-07 (gate only):        TD-032  the transient term used a time level ONE STEP
                                                STALE. All ten frozen criteria PASS; the march no
                                                longer alternates and the per-step change now FALLS
                                                with march length. Closed on its GATE, with
                                                REGRESSION PARTIAL: Debug+GUI not run, sanitizers
                                                scoped (TD-035), clang-format failing (TD-034).
                                                The G8 failure it caused is OPEN under TD-037.
                                                EVERY field-derived flame number predating it must
                                                be re-measured.
RESOLVED 2026-10-07 (gate only):        TD-037  driftWindow was unguarded against trial length.
                                                All nine frozen criteria PASS; the search now
                                                returns the SAME eigenvalue at driftWindow 16, 32,
                                                64 and 128, and the G8 method-insensitivity test
                                                TD-032 broke PASSES with spread exactly 0.
                                                REGRESSION PARTIAL, as TD-032's was.
STILL OPEN, CARVED OUT BY TD-032/037:   TD-033  a G8 test that can never reach its own body
                                        TD-034  clang-format not clean -> blocks every commit
                                        TD-035  ASan at 38x makes the full sanitizer run
                                                impossible as specified for G8
                                        TD-036  NoFlame is mislabelled, and empty-diagnosed
                                        TD-038  the two extinction-drift blocks are duplicated
NEXT UNRESOLVED ON THE CRITICAL PATH:   TD-003  Reacting-flow linear-solver robustness
STOPPED, NOT RESOLVED:                  TD-004  1D premixed-flame capability (route (a) executed,
                                                gate stopped at C2, findings preserved)
OPEN / PARTIAL:                         TD-005  Flame-speed extraction (gate FROZEN, C4d fixed
                                                and verified bitwise; C3 and C6 still FAIL)
REOPENED 2026-10-06 (was RESOLVED):     TD-027  the cell-diffusion cap, validated at 5.0 -- but C4
                                                is INVALIDATED by TD-032 and the closure rested
                                                on it. A user decision.
                                                The cap value itself still stands on C2/C3.
                                                EVERY S_L recorded before it is ~4 % HIGH; the
                                                corrected synthetic value is 4.67589 cm/s.
                                                Removing the mask exposed TD-029/030/031 and
                                                resolved TD-005's F1 outright.
```

TD-004's route (a) fix is complete and correct but does not close the item; its blocker is flame
stabilisation, so **the user must decide whether TD-005 now precedes TD-004** (§1). `TD-020` was
found by TD-004's baseline and is a `TD-006` dependency. TD-003 still blocks TD-004's completion
because the flame's `ΣY` closure is tolerance-limited.

**This register authorizes nothing.** Each item needs its own authorization before work starts
(CLAUDE.md §2), and no roadmap phase is authorized at all.
