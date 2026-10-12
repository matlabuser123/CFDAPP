# CFDApp — TODO

> **This file owns:** current execution state — what is complete, partial or blocked, the current
> blocker, what comes next, and what is authorized. It answers *where is CFDApp now*.
>
> **It does not own:** direction and phase definitions → [ROADMAP.md](ROADMAP.md) · technical debt →
> [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) · measurements → `results/` · rules →
> [CLAUDE.md](CLAUDE.md).
>
> **This file is authoritative for live status**, and it is a snapshot of the tree, not a diary.
> Stale entries are corrected, not appended to; git and `results/` hold the history.

```text
[x] COMPLETE        implemented AND verified with evidence (CLAUDE.md §1.1)
[~] PARTIAL         implemented but not fully qualified; the reason is always stated
[ ] NOT AUTHORIZED  appearing here is not authorization (CLAUDE.md §2)
```

---

# 1. Project Status

```text
P0–P16   [x] COMPLETE     foundations through turbulence
P17      [~] PARTIAL      implementation complete; qualification partial (G6, G10)
P18      [~] PARTIAL      implementation substantially complete; G8 NOT CLOSED
P19–P28  [ ] NOT AUTHORIZED      definitions in ROADMAP.md §8–§17
```

**No implementation phase is authorized.** The active work is technical-debt resolution under a
separate authorization.

---

# 2. Current Blocker — P18 G8

G8 requires a 1D laminar premixed flame whose flame speed lies inside a cited published spread,
frozen *before* measurement at `results/p18/g8_closure/g8_reference.md`:

```text
35.0 – 37.0 cm/s      Hermanns (2007), DOI 10.6100/IR630126
```

**TD-032 is RESOLVED** (2026-10-07, gate only, TD-037 carved out). It was the root cause beneath
five other items: the march's transient term read a time level one step stale, so the reported field
alternated every step. The immediate blocker is now **TD-037**, which carries the G8 failure TD-032
exposed:

**TD-037 is also RESOLVED** (2026-10-07, gate only). The eigenvalue search now returns the same
answer at `driftWindow` 16, 32, 64 and 128 instead of aborting at two of them, and the G8
method-insensitivity test TD-032 broke **passes with its spread exactly 0**.

```text
TD-032, TD-037  both RESOLVED on their frozen gates, both with REGRESSION PARTIAL — Debug+GUI not
                run, sanitizers infeasible AT FULL RESOLUTION for this suite (TD-035 — but a
                150-cell fixture is MEASURED at 282.6 s and clean). clang-format is CLEAN as of
                2026-10-09 (741 files, exit 0), so TD-034 is no longer a reason.
still open      TD-033, TD-035, TD-036, TD-038, TD-042 — none authorized
TD-004          locator fix APPLIED and RETAINED: the front now sits 0.039 cells off the
                anchor (was 51-86 cells), zero locator rejections, and C2-A1's three clauses are
                SATISFIED. The item STOPS on a pre-registered condition -- S_L moved +6.07 % --
                because the working locator makes a non-conservative operator fire far more
                often. New blocker: TD-040.
                C5 now PASSES (2026-10-09) under the GATE_A3 amendment. Three of its four
                clauses had been transcribed from a PRINTED TABLE and failed on the BASELINE
                too, so none was a regression; they are replaced by bounds derived from named
                constants, each shown able to fail.
                C6 then PASSED (properties 2, 3, 5) and **C6a FAILED** (2026-10-09). Its two
                clauses fail differently: the observed order left the frozen [1.5, 2.5] band
                (2.057 -> 2.693, a REAL regression), while its fuel-balance clause fails on
                the BASELINE at all three levels -- a fourth un-dry-run criterion. The gate
                STOPS at C6a; C6b and C7 were not evaluated. TD-004 now has TWO blockers.
                C6a AMENDED and verified 2026-10-10 (GATE_A6 7bedb728, frozen after a
                dry-run whose non-vacuity used BOTH libraries and BOTH triplets): the
                balance clause now PASSES on an h^2-scaled bound -- the flat 0.02 failed on
                the BASELINE at all three levels -- and the order clause reports UNRESOLVED
                (fine difference 2.00 quanta vs a derived 4-quantum floor). The gate is in
                the SAME PLACE and the record now says why. Also measured: the 1000-cell
                level is KILLED at the real 7200 s cap, so C6a's cheap triplet is FORCED;
                and 343/343 species tests PASS, so nothing else changed silently
re-measured     TD-028's alternation is measured GONE (status is a decision).
                TD-030 — the reaction excess is HALF its recorded size and no longer the larger
                term. The inlet diffusive flux (~5.5 %) is a flame-ANCHORING effect and has been
                MOVED to TD-004, whose blocker is now quantified: the flame settles ~1.73 mm
                (87 cells) upstream of its anchor and is still translating after 51 583 steps.
to re-measure   TD-027's C4, TD-029, TD-031 — still on the alternating field
next action     none authorized
```

**Consequence for everything downstream:** every field-derived premixed-flame number recorded
before 2026-10-06 was measured on a march whose reported field alternated every step, and must be
re-measured under its own item's gate. **TD-027 is REOPENED** on that basis — its C4 was invalidated
and its closure rested on it, though the cap value itself still stands on its other criteria.

**SUPERSEDED:** the earlier "`TD-004 → TD-005` order is inverted because the blocker is flame
stabilisation" question is on hold. That classification rested on the pre-fix front position.

---

# 3. P17 — Compressible Flow · PARTIAL

Implementation complete. Both outstanding gates are **inherited** debt, not new work.

```text
[ ] G6   HLLC star-density positivity fails for a characterized class of admissible head-on
         input. Clauses (a)(b)(c)(d)(f) pass; (e) fails.                → TD-008
[ ] G10  Debug+GUI has 4 timeouts; Release+CUDA and CPU-only both PASS.  → TD-009
```

Evidence: `results/p17/FINAL_QUALIFICATION.md` · limits: `results/p17/KNOWN_LIMITATIONS.md`

---

# 4. P18 — Species / Reactions · PARTIAL

| Sub-phase | Status | Gate |
| --- | --- | --- |
| P18.0 Baseline & audit | [x] COMPLETE | fingerprint and gates frozen |
| P18.1 Multi-species state | [x] COMPLETE | **G2 PASS** |
| P18.2 Species transport & diffusion | [x] COMPLETE | **G4 PASS** |
| P18.3 Mixture properties | [x] COMPLETE | **G5 PASS** |
| P18.4 Reaction source framework | [x] COMPLETE | mass-conserving sources |
| P18.5 Arrhenius chemistry | [x] COMPLETE | **G6 PASS** |
| P18.6 Heat-release coupling | [x] COMPLETE | **G7 PASS** |
| P18.7 Conservation & validation | [~] PARTIAL | **G8 NOT CLOSED** |
| P18.8 CPU / GPU equivalence | [x] COMPLETE | within the frozen P18.8 contract |
| P18.9 Final qualification | EXECUTED | **outcome PARTIAL** |

```text
G1 PASS · G2 PASS · G3 PASS · G4 PASS · G5 PASS · G6 PASS · G7 PASS
G8 NOT CLOSED · G9 PASS (scoped) · G10 PASS · G11 PASS
```

P18.9 ran in full; the **outcome** is partial because G8 is open. Elemental conservation is **N/A** —
elemental metadata is not modelled. The plug-flow benchmark does not replace G8.

Evidence: `results/p18/FINAL_QUALIFICATION.md` · limits: `results/p18/KNOWN_LIMITATIONS.md`

---

# 5. Required Work to Close P18

```text
1  re-measure the premixed-flame items           on the fixed march, each under its own gate
3  qualify the reacting-flow linear solver       TD-003
4  deterministic flame-speed extraction          TD-005
5  run G8's controls and close G8                TD-006
6  re-run P18.9, record a new fingerprint        TD-007
7  mark P18 COMPLETE only if G1–G11 PASS
```

**No P18 gate may be weakened to accomplish this.**

---

# 6. Technical Debt

The register is **[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md)** — one row per item with root cause,
priority, dependencies, acceptance gate and evidence. The dependency model is a **graph**, not a
chain. The currently actionable frontier:

```text
register totals   48 items: 9 RESOLVED (TD-001, TD-002, TD-028, TD-032, TD-034, TD-037,
                  TD-046, TD-047, TD-048), 2 N/A (TD-013, TD-040), 2 PARTIAL (TD-035, TD-043),
                  2 BLOCKED, 2 INHERITED, 31 open. NOT CLOSED: 37. P0s still open: 5 (TD-003, TD-006, TD-007,
                  TD-027, TD-029), down from 6 when TD-040 was reclassified.
                  2026-10-11: TD-041 RE-OPENED (its N/A rested on a false measurement),
                  TD-045, TD-046 and TD-047 added, so the count went 43 -> 47 and N/A 3 -> 2. TD-001/TD-002 closed before this phase
                  and are not repeated below; the frontier block is the CURRENT frontier
                  only. NOTE when counting by grep: TD-004's status contains the word
                  UNRESOLVED, so a /RESOLVED/ match counts it by mistake -- use
                  /\*\*RESOLVED\*\*/.
resolved          TD-032   transient time level one step stale — RESOLVED 2026-10-07 (gate only)
resolved          TD-037   driftWindow unguarded against trial length — RESOLVED 2026-10-07
resolved          TD-034   clang-format clean — RESOLVED 2026-10-09 (741 files, exit 0)
n/a as a defect   TD-040   PREMISE REFUTED 2026-10-10 by its own dead-band ladder: S_L is
                  4.955332 at EVERY dead band INCLUDING recentring OFF, so translation does
                  NOT bias the eigenvalue. The fills are correct for a moving frame and the
                  steady-state balance properly excludes the ledger. All three candidate
                  fixes are wrong or unnecessary; 4.1 would break C3/C4/C6a-bal. CONDITIONAL:
                  a future TIME-INTEGRATED conservation claim reopens it
current blocker   TD-004 C6a-ord is UNRESOLVED and is now its ONLY blocker. Needs TD-043's
                  finer tolerance -- which would likely make the band violation DEFINITE
                  NEW: the dead-band ladder shows the composition SUM tracks the recentring
                  count (8.82e-07 -> 1.24e-06 at deadBand 1) while min Y does NOT. The
                  fill-count explanation for min Y is REFUTED and nothing replaces it
re-measured       TD-005   C3 NOW PASSES (2026-10-11): 0 of 12 configurations fail against a
                  recorded 5 of 12; worst balance 1.0043e-02 vs the untouched 0.05 bound, 37x
                  better. The thin-flame state is GONE -- delta 1.1945-1.1975 mm everywhere.
                  Sweep A confirms TD-037 independently: S_L spread EXACTLY 0.000000 across
                  driftWindow 16/32/64. BUT the consumption identity now EXCEEDS its 0.01 bound
                  at 10 of 12 (0.01027 at the reference vs 0.00776 pre-fix), so **C6's premise is
                  OBSOLETE** -- it existed to unmask a PASSING identity. C6 still FAIL, and its
                  split needs a test change + authorization, in an UNTRACKED file
to re-measure     TD-030 — still on the alternating
resolved          TD-028   period-2 alternation GONE (2026-10-10). T[35] swing fell
                  1.4836e+03 -> 1.4385e-04 K, a factor of 1.03e7, and the shape changed to
                  a monotone drift. Verified on the FIELD, not on S_L. The period-14
                  characterisation and both dt attributions STAY INVALID
re-measured       TD-029   the TEMPORAL error collapsed 4.24 % -> <= ONE LATTICE QUANTUM
                  (0.0677 %). The 62x collapse is real; the VALUE is not, and the
                  'opposite signs' headline is UNSUPPORTED. See TD-043
ANSWERED          TD-040 §5b  the dt ladder on the fixed tree EXCLUDES 4.671937 by 6.0 %.
                  The post-fix eigenvalue stands; TD-027's C2 reference is contaminated;
                  TD-004's +6.07 % move was TOWARD the truth. No Richardson limit claimed
                  -- two dt levels gave a bit-identical S_L, so the family is not asymptotic
CLOSED (mechanism) TD-043  the lattice is MULTIPLICATIVE, ratio 2^(1/1024) = 0.067713 % of
                  S_L, set by settings.tolerance=1e-3 and the bracketing ratio 2. Derived
                  from 3 source lines, matches the solver's own reported bracket to 0.0000 %,
                  and all 10 measured S_L are integer lattice points. It is RELATIVE, so
                  resolution-independent -- no 125/250-cell run was needed.
                  CONSEQUENCES: TD-029's opposite-signs claim REINSTATED (one full step does
                  determine a sign); TD-004 C6a's order is UNDETERMINED, not regressed
                  (true p in (2.00, 3.81) vs a band edge at 2.5); C6 property 2's quoted
                  spread 0.000469 is a lattice ANCHOR offset, not a physical quantity.
                  FIX available: tolerance 1e-4 -> 8x finer for 3 extra bisections. NOT
                  authorized -- it moves every recorded S_L onto a new lattice
re-measured       TD-031   CONFIRMED and strengthened: the front SAWTOOTHS (drift to the
                  3-cell dead band, reset, repeat), so NO relaxT reports a settled front
re-measured       TD-027   C4 PASSES on a parity-CROSSING ladder (2026-10-09) by 24.5x, and
                  C2 is AMENDED and PASSES (2026-10-10): its reference 4.67589 was never
                  derivable -- the Richardson fine difference was ONE lattice quantum --
                  and C2-A2 measures the error instead: 0.2030 % <= 0.28 %, 1.38x margin.
                  **The open criterion MOVED to C3, which now FAILS at 4.06x vs <= 2**,
                  because C3's numerator IS C2's error and the old reference understated it.
                  THE FINDING: the reported precision understates the true temporal error by
                  3-4x. Third time a correct fix has worsened a gate by unmasking
                  field, each needs re-measuring under its own gate
also open         TD-033, TD-035, TD-036, TD-038, TD-039, TD-040, TD-042 — none authorized
                  TD-042   reactionHeatRelease at :1086 is computed every step and DISCARDED;
                  found because patching it as a gate control would have been vacuous
re-opened         TD-041   line endings. THREE successive claims, two broken probes. The
                  measured state, 2026-10-11, by binary byte count: 102 tracked TEXT files
                  contain CRLF, 21 621 lines (91 results/, 11 tests/, 3 .cpp under the format
                  gate). "CRLF is the committed convention" was FALSE and so was its
                  replacement "the repo is pure LF". Whether the count GREW is unmeasured
SEED NARROWED     TD-003   **the initial condition is NOT the seed (2026-10-12):** the seeded
                  profile's closure is 1.110223e-16 (0.5 ulp), yet d = 4.633173e-09 by the end
                  of step 1 -- a 4e+07 jump inside ONE step. A MODEL fits everything:
                  d_new = gain*d_old + A^-1 r_solve, driven by the per-step SOLVE error
                  (2.46e-08 measured) and carried by the transient, settling ~40x the
                  injection. **NOT ESTABLISHED** -- gain ~0.975 is back-solved from the
                  observed value, and the only gain figure available (1.2391) mixes a global
                  norm with a local coefficient and would imply growth the data contradicts.
                  The instrument also failed its main job: the two halves' samples agree to
                  ~7 digits so they do not separate, and the history is from the first TRIAL.
                  NEXT: predict the fixed point from an INDEPENDENTLY measured gain and
                  injection, then check against 8.822866e-07 WITHOUT adjusting either
ATTRIBUTED        TD-003   **C3's residual is ATTRIBUTED 2026-10-12: the TRANSIENT term is
                  98.79 % of it** (2.376e-11 of 2.406e-11), charged by isolation with the
                  decomposition CLOSING (sum/total 1.0263). Closure 3.85 %, diffusion 6.8e-21,
                  convection and zero source EXACTLY zero. The algebra then follows:
                  r^trans = rho V/dt * d_old, so **the transient CARRIES the deviation rather
                  than creating it** -- the correct non-conservative rho dY/dt form, so "fix
                  the transient" is probably the WRONG conclusion. **Corrects my own NV1
                  control**, which disabled the CLOSURE (near-inert at Le=1, rawSum ~ 0) and
                  was read as refuting feedback in general. **ORIGIN STILL UNKNOWN** -- every
                  source site sums to 1 exactly. The 1.2391 "loop gain" is NOT claimed: global
                  norm x local coefficient, the same cross-scope error as the round-off claim.
                  NEXT: hunt the SEED -- track max_c |sum Y - 1| from step 1 and find where it
                  first departs from zero
AMENDMENT FAILED  TD-003   **AMENDMENT A1 WITHDRAWN 2026-10-12: it failed its OWN dry-run,
                  and it took a committed finding with it.** Option A was authorized and §5.3
                  run in order; the derived bound is short by 6.2e+06 (3.72e-13 vs 2.30e-06).
                  T was NOT adjusted -- the amendment pre-registered that. The scale was wrong:
                  the assembly is in FLUX units, ||A||_inf = 2.73e-04, not the ~1e0..1e4 I
                  assumed. The FEEDBACK hypothesis is also REFUTED by a causal control
                  (closureCorrection=false leaves r at 2.73e-11 vs 3.16e-11, S_L bit-identical,
                  because rawSum ~ 0 at Le=1). **AND verification_closure_test.md's committed
                  claim is WRONG both ways**: condition number is 33 (well-conditioned, not
                  near-singular) and ||r|| is 5.6e+08 x eps*||b||_inf (not round-off) -- the
                  original compared ||b||_2 from a DIFFERENT state. That file is marked INVALID
                  in place. **C3's cause is UNKNOWN; six hypotheses dead.** 1e-8 untouched, test
                  still fails. NEXT: attribute r per assembler rather than hypothesise
A vs B SETTLED    TD-003   **OPTION B's PREMISE REFUTED 2026-10-11 -- it was my own
                  counter-argument, and it does not survive.** The 9.54994e-08 minimum came
                  from TheFlameSpeedConvergesUnderMeshRefinement at **125 cells**, a state
                  whose consumption integral is off by **51 %** (S_L 4.40773 vs S_c 2.15465,
                  fuel balance 0.4999, final march 334 NOT steady). Not a better-conditioned
                  flame -- a WORSE SOLUTION on a coarser mesh, so it is no evidence that 1e-8
                  is reachable where the flame is resolved. **Option A now stands unopposed**,
                  with FORM C*eps*||b||*||A^-1||_inf measured per state (a plain C*eps*N does
                  not fit: the quotient rises 3.44e6 -> 5.37e6). I also WITHDREW the "N ~ 23
                  cells" extrapolation -- §6 forbids an order claim when two of three levels
                  are 51-55 % inconsistent. **THE §5.3 AUTHORIZATION IS THE ONLY THING LEFT**
provenance        TD-003   **the 1e-8 was NEVER REACHABLE, checked 2026-10-11.** 0 of 81
                  recorded |sumY-1| measurements fall below it (9.54994e-08 .. 4.00526e-06).
                  It sits in NO frozen gate; it was never derived (every neighbour on this
                  quantity class is relative, and the same constant passes with 529x margin on
                  minMassFraction while failing by 88x here); and TD-004's own frozen GATE.md
                  §3.2 ALREADY recorded it as "already failing" and routed around it -- while
                  attributing it to a "tolerance-limited solve", now refuted at 1.04 %.
                  So a §5.3 amendment would REPLACE AN UNDERIVED CONSTANT, not fix a stale one.
                  **But Option B is not futile:** the 9.55e-08 minimum is 6.8x BELOW the
                  derived floor, so the floor is state-dependent and ~10x less amplification
                  would bring 1e-8 into reach. WHICH configuration produced 9.55e-08 is NOT
                  identified, and that is the measurement that decides A vs B
SOLVED / STOP     TD-003   **C3 SOLVED 2026-10-11, AND ITS GATE IS MIS-DERIVED -- §12 STOP.**
                  The closure test closes the chain to SEVEN DIGITS:
                  max |A^-1 (sum_i b_i - A*1)| = 2.301527e-06 = max_c |sum_i y_EXACT - 1|,
                  the same number. Per-species matrices BIT-IDENTICAL (0.0), so the
                  telescoping holds exactly. The assembled RHS closes to 3.162982e-11 = 3.5x
                  the round-off floor eps*||b||, and the final march's near-singular operator
                  (||A^-1||_inf = 1.2e+05) amplifies it by 7.276e+04. **The 1e-8 gate would
                  need the RHS to close to 1.374e-13 -- 65x BELOW double precision at this
                  ||b||. Unachievable by construction.** Derived floor 6.503e-07 vs observed
                  2.302e-06. NO source site injects anything: inlet, seed, chemistry,
                  recentring fill and burnt composition all sum to 1 EXACTLY.
                  **A §5.3 THRESHOLD AMENDMENT IS DUE AND IS NOT AUTHORIZED.** Options
                  recorded in verification_closure_test.md §5: (A) amend to a derived
                  relative bound, the form SpeciesTransport.hpp:149 already uses for this
                  quantity class -- recommended; (B) reduce the 1.2e+05 amplification, a
                  production change to the march; (C) enforce sum Y = 1 -- FORBIDDEN by the
                  project's own no-clipping policy
located           TD-003   **C3 CAUSE LOCATED 2026-10-11.** Instrumented run (pure-addition
                  patch on a copy outside the repo; reproduces S_L 4.955332 and
                  maxCompositionSumDeviation 8.822866e-07 EXACTLY). An exact Thomas solve of
                  the SAME assembled systems leaves |sum Y - 1| = 2.301527e-06 -- **230x the
                  1e-8 gate with ZERO solver error**. The linear solve contributes
                  2.396274e-08, **1.04 %**. So the tolerance remedy CANNOT work and my
                  <= 3.8e-10 requirement is WITHDRAWN. worstLinearResidual 9.999987e-10
                  confirms TD-003's 1e-9 record; all 221 049 solves cleared converged() and
                  the fallback never fired. The ASSEMBLED ||A^-1||_inf is 120 000 against my
                  reconstructed 2.4 -- wrong by 50 000x. **CRITERION 3 BELONGS WITH THE
                  SPECIES RHS ASSEMBLY, NOT TD-003.** Leading candidate, NOT measured: the
                  diffusive closure's sum_i J_i. A REGISTER-STRUCTURE DECISION IS NOW DUE
eliminated        TD-003   C3 SOURCE HUNT 2026-10-11, no run needed: the transport step
                  passes an EXPLICIT ZERO Patankar source, so my own hypothesis is REFUTED;
                  sum_i omega_i cancels EXACTLY in IEEE (F->P, 1:1, equal molar weights); and
                  the recentring fill {0.05, 1.0-0.05} sums to BIT-EXACTLY 1.0, so recentring
                  cannot drift it despite being non-conservative for fuel and energy. All
                  three eliminated. **And the observed 8.823e-07 EXCEEDS the transport solve's
                  own upper bound (1.53e-07) by 5.8x, which is impossible** -- so a premise is
                  false and which one is not settled. Next: instrument a copy outside the repo
                  to print the ACTUAL per-solve ||r||/||b|| and ||b||. **C3's cause is not
                  established as a linear-solver matter -- TD-003 may be the wrong owner**
measured          TD-003   P0, KAPPA MEASURED 2026-10-11: **26.28, not the 1000 my audit
                  assumed** -- wrong by 38x. kappa x residual = 2.63e-08 against an observed
                  8.823e-07, a 33.6x SHORTFALL, so my mechanism is REFUTED in magnitude. It
                  survives in DIRECTION: 2.63e-08 still exceeds the 1e-8 gate by 2.6x, so a
                  tolerance change IS needed, but to <= 3.8e-10 rather than 1e-11. The
                  dominant source of the 33.6x is UNIDENTIFIED. **And C2 is reframed: neither
                  the flame (dominance 1.0986, Peclet 0.028) nor the production case (10.92,
                  Peclet 250) is in the r > 1 regime the probe characterised** -- both are
                  strictly diagonally dominant. Candidate fix is to wire the EXISTING
                  LinearSolverFallback (it escalates on exactly `Breakdown` to GMRES) into the
                  reacting path, which is the one solve path that does not use it
audited           TD-003   P0, AUDITED 2026-10-11, still OPEN. **C2 ANSWERED**: the
                  pure-upwind (lower-bidiagonal) class is solvable iff r = s/d <= 1. At
                  r = 1.05 -- kappa 7.09e5, still well-posed -- BiCGSTAB BREAKS DOWN and
                  GMRES(30) stagnates at 5000 iterations with true error 0.80, while forward
                  substitution is EXACT. So a direct triangular solve is adequate and the
                  repository HAS none (CG/BiCGSTAB/GMRES only). Jacobi is provably useless on
                  this class: the diagonal is constant, so it is a scalar rescaling. r > 1 is
                  ill-posed (kappa 4.8e60 at n=200) and excluded. C1 SATISFIED; its escalation
                  gap is TD-047. C3: the 1e-8 gate IS reachable and fails at 88x (8.823e-07);
                  mechanism is residual-vs-error, candidate fix is ONE constant that :625
                  already intends -- BLOCKED on measuring the flame's own kappa
resolved          TD-047   **RESOLVED 2026-10-11.** A reacting run whose every linear solve
                  FAILED exited 0, and the CLI printed NO reacting lines at all -- worse than
                  registered, because printReport iterates ProjectRunResult::speciesResults
                  while the reacting path fills ReactingRunResult::speciesResults. All six
                  frozen criteria pass: exit code 0 -> 4 on failure and still 0 on success,
                  10 reacting report lines where there were none, and anyLinearSolveFailed now
                  has 2 readers (was 0). Rule DERIVED from statusFor's existing
                  MomentumFailure -> NumericalFailure mapping, in a PURE function so the 6x2
                  space is exhaustively tested without engineering a failing solve.
                  **Also the first end-to-end reacting run there has ever been.**
                  STILL OPEN as a separate concern: no automated test drives the reacting
                  path, so a regression in it would be caught by nothing -- worth its own item
resolved          TD-046   **RESOLVED 2026-10-11.** The sole test of lambda(T) died in its
                  own setup at 6 ms (0.11 against the 0.15 it demands) and had NEVER run its
                  solves. Slope DERIVED from the untouched assertions and set to 1.0e-4 = 4x,
                  clearing the precondition by 33 %. All five frozen criteria PASS in 2866 s:
                  a 4x conductivity gives a **1.81x** faster flame (a frozen lambda would give
                  exactly 1.00x) and a 1.59x thicker front. 0 assertion lines changed, no
                  production file touched. **G8 drops from 2 failures to 1.** Two unsought
                  confirmations: dt fell exactly 4.0000x (confirming :718) and |sumY-1| rose
                  4.75x where TD-003's floor model predicts ~4x -- its first independent check
also open         TD-045   the repository has NO line-ending guard: `* -text` overrides
                  core.autocrlf, clang-format DERIVES the ending per file (all 3 CRLF .cpp
                  PASS), and git diff --check sees only staged files. Carved out of TD-041
next              determined by TECHNICAL_DEBT.md; nothing is authorized
```

---

# 7. Future — Not Authorized

```text
P19  Multiphase                       P24  HPC & Distributed Scaling
P20  Advanced SST                     P25  Production Workflow & Reliability
P21  Advanced Numerics                P26  Verification & Validation Expansion
P22  3D Physics Parity                P27  Performance & Release Hardening
P23  Moving Mesh Production           P28  v1.0 Final Qualification
```

Definitions, goals, capabilities, dependencies and scope boundaries: **[ROADMAP.md](ROADMAP.md)
§8–§17**. Appearing there is **not** authorization. **Do not begin P19 merely because it appears in
the roadmap.**

---

# 8. Execution Order

```text
P0–P16 COMPLETE  ->  P17 PARTIAL  ->  P18 PARTIAL (G8 not closed)
   ->  the P18 debt work (§5, §6)
   ->  P18 requalification; COMPLETE only if G1–G11 PASS
   ->  P17's remaining qualification debt as priority requires
   ->  P19 … P28, each on explicit authorization
```

---

# 9. Rules

These restate [CLAUDE.md](CLAUDE.md), which is authoritative.

```text
 1  Audit before implementing.
 2  Freeze acceptance criteria before final measurements.
 3  Preserve the original failure before fixing a defect.
 4  Never weaken a valid threshold to obtain PASS.
 5  [x] means implemented AND verified.
 6  Preserve negative controls.
 7  CPU numerics remain the reference unless explicitly superseded.
 8  Conservation before performance.
 9  Numerical equivalence before speed claims.
10  Do not repair unrelated technical debt inside another phase.
11  Reuse existing infrastructure before creating a new subsystem.
12  Avoid duplicate transport, EOS, energy or chemistry implementations.
13  Reject invalid physical states explicitly; do not silently clamp.
14  Do not normalize conserved states merely to hide drift.
15  Implementation and qualification remain separate.
16  Every major phase gets a frozen baseline and a final fingerprint.
17  Every major phase records known limitations.
18  Every CPU/GPU phase records CUDA diagnostic scope.
19  Every performance comparison uses equivalent numerical solves.
20  Full regression follows production-code changes.
21  Detailed evidence belongs under results/.
22  Technical debt belongs in TECHNICAL_DEBT.md.
23  TODO.md holds current state, not historical logs.
24  No commit or push without authorization.
25  No future phase begins without explicit authorization.
```

Evidence layout: one `results/<phase>/` per qualified phase, one
`results/technical_debt/<item>/` per debt item. Future phase directories are created only when that
phase is authorized.

---

# 10. Current Action

```text
PROJECT STATE          P17 PARTIAL · P18 PARTIAL
BLOCKING ISSUE         P18 G8 — not closed. AUDITED 2026-10-11 (TD-006): still BLOCKED on 8
                       of 11 declared dependencies, 3 of them P0. Its PRECISION PRECONDITION is
                       now DISCHARGED -- method reproducibility 4.17 % -> 0.97 %, a 4.3x gain,
                       crossing from "cannot resolve the +/-2.78 % band at all" to 2.86x inside
                       it, with a +/-0.97 % undecidable zone at each edge. Preconditions 2 (cp
                       and lambda(T) cited) and 3 (Westbrook & Dryer Set 3 wired to a case) are
                       NOT met. No solver was run
ROADMAP AUTHORIZATION  NO NEW FUTURE PHASE AUTHORIZED
PUSH BLOCKER           TD-035 — the §8 regression is PARTIAL on this tree: 2 of 4
                       configurations. RELEASE RAN 2026-10-11 (2840 tests, 7 failures, all
                       known, 0 new, 5 G8 tests FIXED, 7144.02 s) and clang-format is clean,
                       so those two now PASS. **DEBUG+GUI AND THE SANITIZERS RAN 2026-10-12:
                       sanitizers 2832 tests / 14 failed / 7.13 h with ZERO sanitizer
                       diagnostics; Debug+GUI 2906 tests / 18 failed / 4.31 h. All 32 failures
                       accounted for, 0 unexplained.** CORRECTION to this block's own wording:
                       there are not "five sanitizer configurations" -- ci.yml has ONE preset
                       in five SHARDS, so one full run covers their union. RELEASE IS
                       RE-RUNNING (TD-046/TD-047 invalidated its 2840/7/7144 s counts), so
                       **RELEASE RE-RAN 2026-10-12: 2844 tests, 6 failed, ALL KNOWN, 6908 s.
                       THE §8 LADDER IS 4 OF 4 ON THIS TREE.** 49 of 53 rewritten tracked
                       files were classified timing-only and RESTORED; the 4 build_identity
                       files are kept and UNSTAGED pending a decision (the embedded SHA moved
                       with HEAD, which §7's timing-only licence does not cover). The ladder
                       carries recorded cost limits: 10 ASan timeouts and 14 Debug timeouts
                       whose Release durations exceed the 7200 s cap once slowed; the cap was
                       NOT raised. Whether those limits are acceptable is the remaining
                       judgement before a push. A sanitizer FIXTURE exists (150 cells, 348.75 s under
                       ASan+UBSan, 20.6x headroom, 0 diagnostics) and the asan preset EXCLUDES
                       the heavy G8 cases, but the full 2860-test preset's cost is UNMEASURED.
                       The blocker STANDS for a PUSH. It does NOT block a commit, and the
                       2026-10-11 production commit was authorized on this partial basis.

STANDING SCOPE LIMIT   (CLAUDE.md §9) The heavy G8PremixedFlameTest cases, 500-1500 cells,
                       are EXCLUDED from `ctest --preset asan` and are exercised under NO
                       sanitizer. The 150-cell fixture covers the same FUNCTIONS, not the
                       same PROBLEM SIZES: a defect manifesting only above some allocation
                       size would be missed. Unavoidable -- the 1000-cell level was KILLED
                       at the real 7200 s cap, and §7 forbids inflating the limit.
                       clang-format is CLEAN (742 files, exit 0) and git diff --check is 0
                       in source after four new files were converted CRLF -> LF; the 14
                       remaining check lines are tab-indented ctest output inside one raw
                       .log, committed verbatim by precedent. TD-041 is RE-OPENED but does
                       not block this commit: it describes CRLF already in HEAD, which §2
                       forbids repairing inside another item's commit
```

## Decisions waiting

```text
1  TD-040 next step   AUDITED: the mechanism is derived exactly -- each translation discards
                      fresh reactant upstream and fabricates burnt product downstream. Next is a
                      recentreDeadBandCells ladder (no production change) to test whether the
                      eigenvalue tracks the ledger rate. Authorize that?
2  the dt ladder      re-measure on the fixed tree to get a Richardson limit not inherited from
                      the blind-locator solver. Neither 4.671937 nor 4.955332 cm/s is verified.
2  TD-035             the full sanitizer regression CLAUDE.md §8 requires is infeasible for the
                      premixed-flame suite. Options: a stated-scope subset, a purpose-built cheap
                      fixture, or an amendment to §8 for suites of this cost. Raising the per-test
                      cap is what TD-009 forbids and is not proposed.
```

## Not authorized, and not to be started implicitly

TD-003 and every later debt item · every phase in §7 · any commit or push.
