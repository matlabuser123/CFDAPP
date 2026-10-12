# CFDApp — Technical Debt

**Diagnostic register.** One row per unresolved deficiency. Detail, derivations, refuted hypotheses
and measurements live in `results/technical_debt/<item>/`; this file says *what is wrong and what
would fix it*, never how it was measured. Live execution state is [TODO.md](TODO.md); direction is
[ROADMAP.md](ROADMAP.md); rules are [CLAUDE.md](CLAUDE.md).

```text
49 items   38 unresolved (P0 6 · P1 11 · P2 12 · P3 9)   11 closed

pre-rewrite register, verbatim   results/doc-archive/TECHNICAL_DEBT-2026-10-12-pre-rewrite.md
historical TD-000 audit record   results/technical_debt/DEBT_REGISTER.md  (2026-10-03 snapshot,
                                 a CLASSIFICATION not a work order; this file is authoritative)
```

## Unresolved

| ID | Priority | Issue | Impact | Required Fix | Status |
|----|----------|-------|--------|--------------|--------|
| TD-003 | P0 | BiCGSTAB breaks down on lower-bidiagonal pure-upwind systems; no repository-wide solver-status audit | The reacting path builds a bare `BiCGSTAB` (`ProjectRunner.cpp:828`) with no escalation, though no real matrix is in the failing regime | Repository-wide status audit; wire the existing `LinearSolverFallback` into the reacting path | OPEN — C1 satisfied, C2 audited with the fix identified but **not applied**. C3 moved to TD-049 |
| TD-006 | P0 | P18 G8 canonical reacting validation not closed | Blocks P18 COMPLETE and TD-007 | Run G8's controls against the pre-frozen 35.0–37.0 cm/s band (Hermanns 2007) | BLOCKED — 8 of 11 declared dependencies open, 3 of them P0 |
| TD-007 | P0 | P18 final qualification not re-run; no current fingerprint | P18 cannot be marked COMPLETE | Re-run P18.9 and record a new fingerprint | BLOCKED on TD-006 |
| TD-027 | P0 | `maximumCellDiffusionNumber = 20.0` is an unvalidated resolution cap | A gated `S_L` sits 3.99 % off against a ±2.8 % band | **C3's instrument must be decoupled from `settings.tolerance` before the cap can be sized at all** — §5.3 amendment derived and NOT authorized | REOPENED — C3 fails by 4.06×, and **AUDITED 2026-10-12: C3 cannot express its own threshold.** Its numerator and denominator are the same knob, so the ratio can only take `m × (q/B)` with `q/B ∈ (1,2]` — the threshold 2 sits in an unreachable gap and the criterion is really the integer test “did the dt ladder move by ≤ 1 terminal bracket?”. Its lowest score, 0, is a flat ladder that resolved nothing. **Also found: the shipped default `tolerance = 1e-4` reads 40.63×, 10× worse than the gated 4.06×, and the comment justifying the cap argues coherence at 1e-3, which is not the default.** `audit_C3.md` |
| TD-029 | P0 | Two PASSING tests pass by error cancellation: `dt` is derived from `dx` | No mesh family is self-similar, so observed-order claims from them are invalid | Refine `dt` and `dx` independently; check each error sign separately | RE-MEASURED — qualitative claim holds, qualified then reinstated |
| TD-049 | P0 | The species RHS assembly's `ΣY` closure: `max_c \|Σ_i Y_i − 1\|` parks at ~1e-6 against a `1e-8` test gate | Blocks TD-004's completion and TD-005's C6 | Explain the magnitude with a measured mechanism; derive a threshold under §5.3 | OPEN — **7 hypotheses refuted**, cause unknown, and the `1e-8` is itself underived |
| TD-004 | P1 | 1D premixed-flame capability; non-conservative recentring fixed, C6a's observed-order clause unresolved | Blocks TD-006 | Close C6a's order clause, then C6b and C7 | OPEN / STOPPED — C5 and C6 pass; C6a order **UNRESOLVED** (2.00 quanta vs a 4-quantum floor) |
| TD-005 | P1 | Flame-speed extraction not deterministic enough to decide G8 near its band edges | Blocks TD-004 and TD-006 | Freeze the extraction method before measuring; close C6 | OPEN / PARTIAL — C3 now passes, 0 of 12 configurations failing; C6 blocked on TD-049's bound |
| TD-008 | P1 | P17 HLLC star-density positivity fails for a characterized class of admissible head-on input | Narrows the qualified compressible envelope (P17 G6) | Positivity-preserving star state / signal speeds | INHERITED — clauses (a)(b)(c)(d)(f) pass, (e) fails |
| TD-009 | P1 | Debug+GUI `DriverSeegmiller` timeout contract unstable | P17 G10; 4 Debug timeouts | Stabilise the contract without raising the per-test cap | INHERITED |
| TD-018 | P1 | CRLF / `git diff --check` warnings in tracked files | **PUSH BLOCKER** for any authorized push | Normalise line endings under a guard | OPEN — measured 2026-10-12: 102 tracked text files, 21 621 CRLF lines |
| TD-030 | P1 | Reaction-integral excess is real but half its recorded size | Feeds TD-005 C3 and TD-006 | Re-measure on the fixed march under its own gate | OPEN / AUDITED |
| TD-031 | P1 | `finalMarchRelaxationTimes = 10` reports a TRANSIENT state | Every quantity read from the final march | Derive the relaxation time needed for a steady read | RE-MEASURED — confirmed and strengthened; the front is still translating |
| TD-035 | P1 | The full sanitizer regression §8 requires is infeasible for the premixed-flame suite (ASan costs 38×) | The heavy G8 cases, 500–1500 cells, run under **no** sanitizer | A stated-scope subset, a cheap fixture, or a §8 amendment — never a raised cap | PARTIALLY RESOLVED — 150-cell fixture implemented and clean; recorded cost limits await a user judgement |
| TD-036 | P1 | `PremixedFlameStatus::NoFlame` is mislabelled and reports no reason | Any search diagnosis reads a wrong cause | Distinguish the states and report the reason | OPEN |
| TD-041 | P1 | CRLF is present in tracked text files; three successive claims about it were wrong | Push-blocker evidence; two probes were broken and one false conclusion was committed | Measure once with a byte-exact probe, then normalise | OPEN — re-opened 2026-10-11; current state measured, growth unmeasured |
| TD-043 | P1 | `S_L` is reported on a multiplicative lattice of quantum 0.0677 % | Bounds the resolution of **every** `S_L` convergence study (TD-029, TD-027 C2, TD-004 C6) | Account for quantisation in any order claim | MECHANISM CLOSED — derived with no solver run; consequences still open |
| TD-010 | P2 | Zero `FixedGradient` transport coverage; turbulence retains the lagged gradient | Boundary robustness unproven | Add coverage; remove the lag | OPEN |
| TD-011 | P2 | `TotalEnergyState` refuses temperature-dependent `cp`; needs a variable-γ EOS | Blocks reacting-shock capability and TD-023 | Implement variable-γ total energy | OPEN |
| TD-012 | P2 | Forward/reverse Arrhenius pairs unchecked for equilibrium consistency | Reversible-chemistry rigour | Check pairs against thermodynamic equilibrium | OPEN |
| TD-019 | P2 | No `∂ω_i/∂Y_j`; the implicit Patankar `Sp` is zero | A genuinely stiff mechanism cannot be integrated implicitly | Implement the chemistry Jacobian | OPEN — carved out of TD-002 |
| TD-020 | P2 | No early-out when a mixture cannot sustain a flame | G8's negative control 1 **cannot run**, so this is a TD-006 dependency | Detect and report an unsustainable mixture | OPEN |
| TD-022 | P2 | Device species transport not implemented | Blocks TD-025 | Implement the GPU species path | OPEN |
| TD-023 | P2 | GPU variable-`cp` parity not established | CPU/GPU physics parity incomplete | Establish parity once TD-011 lands | OPEN — depends on TD-011 |
| TD-026 | P2 | `worstFinalOffsetCells` is capped by the control locator's refusal, so it cannot see the offsets it names | The diagnostic cannot be trusted | Measure offsets independently of the locator | OPEN |
| TD-033 | P2 | A G8 test asserts a precondition its own constants cannot satisfy | G8 test credibility | Fix the constants or the assertion — never by loosening it | OPEN |
| TD-038 | P2 | The two extinction-drift blocks in `runTrial` are duplicated and must stay in step | The duplication already hid a defect once | De-duplicate | OPEN |
| TD-039 | P2 | `c2a1_probe.cpp` hardcodes the pre-fix admissibility pattern and refuses a verdict otherwise | Blocks re-measuring TD-004 C2-A1 | Generalise the probe | OPEN |
| TD-045 | P2 | The repository has **no** line-ending guard; three mechanisms each let CRLF through | Detection gap behind TD-018 and TD-041 | Add one guard that fails closed | OPEN — carved out of TD-041 |
| TD-014 | P3 | Thermochemistry split across two types; registry identity is caller-checked only | API robustness | Consolidate and enforce identity | OPEN |
| TD-015 | P3 | Historical probes are not reproducible from their own evidence directory | Reproducibility of older evidence | Restore or record the probes | OPEN |
| TD-016 | P3 | Separate-process determinism not measured | Determinism evidence is weaker than claimed | Measure across processes | OPEN |
| TD-017 | P3 | Validation-evidence wording and re-measurement gaps | Evidence clarity | Correct forward | OPEN |
| TD-021 | P3 | No elemental composition metadata | Elemental conservation cannot be modelled (out of P18 scope) | Add a thermochemical schema | OPEN |
| TD-024 | P3 | GPU reaction orders limited to integer orders | General GPU kinetics | Support non-integer orders | OPEN |
| TD-025 | P3 | No end-to-end GPU reacting-flow solve | Full GPU reacting flow unproven | Implement once TD-022 and TD-023 land | OPEN |
| TD-042 | P3 | `reactionHeatRelease` is computed every march step and discarded | Wasted work per step, and a trap for anyone instrumenting it | Use the value or stop computing it | OPEN — confirmed present 2026-10-12 |
| TD-044 | P3 | A CLI test writes its working directory into the repository root | Pollutes the work tree and risks being committed | Redirect to a private per-test copy | OPEN — confirmed present 2026-10-12 |

## Closed

Kept for dependency resolution. Evidence under `results/technical_debt/<item>/`.

```text
RESOLVED  TD-001  reacting stack now reachable from a case file      2026-10-03
          TD-002  explicit chemistry ΣY step budget                  2026-10-04  (TD-019 carved out)
          TD-028  period-2 march alternation gone                    2026-10-10
          TD-032  transient term read a time level ONE STEP STALE    2026-10-07  (TD-037 carved out)
          TD-034  clang-format clean                                 2026-10-09  (re-verified 2026-10-12: 742 files, exit 0)
          TD-037  driftWindow unguarded against trial length         2026-10-07
          TD-046  the only λ(T) test never ran its solves            2026-10-11
          TD-047  a reacting run whose every solve FAILED exited 0   2026-10-11
          TD-048  build_identity churn in four tracked case outputs  2026-10-12
N/A       TD-013  Arrhenius A units cannot be validated in code      not a defect
          TD-040  recentre() conservation                            conditional: no gated quantity is affected
```

## Resolution Criteria

- Acceptance gate frozen **before** the fix, dry-run against the unchanged baseline, non-vacuity shown.
- Fix verified by the focused tests for the code changed; no numerical or functional regression.
- Negative control preserved; the original failure preserved verbatim.
- Evidence recorded under `results/technical_debt/<item>/`.
- A threshold is never weakened to obtain PASS; a wrong threshold follows CLAUDE.md §5.3.

**This register authorizes nothing.** Each item needs its own authorization (CLAUDE.md §2).
