# CFDApp — authoritative technical-debt register

**TD-000 deliverable.** Built by auditing `TODO.md`, `ROADMAP.md`, `CLAUDE.md`, `results/p17/` and
`results/p18/` — in particular `results/p18/FINAL_QUALIFICATION.md` and
`results/p18/KNOWN_LIMITATIONS.md`.

**Authoritative current state** is taken from the latest final-qualification evidence,
`results/p18/FINAL_QUALIFICATION.md` (`P18 OVERALL: PARTIAL`, G8 NOT CLOSED), not from the
narrative in `TODO.md`.

```text
audit date        2026-10-03
git HEAD          3bd32f9
working tree      303 modified/untracked paths -- the uncommitted P13-P18 series. NOT cleaned (CLAUDE.md section 50).
P18 fingerprint   83d43668dfd4b19877831b55ab44e187726f774d34e5ac68b591f1e8b2c3384d (433 files, P18.9)
```

> This register is a CLASSIFICATION, not a work order. No debt item is fixed by this document and
> no production file was changed to produce it.

---

## 1. Why deduplication was the substance of this audit

`TODO.md` carries **49 unchecked items** across seven per-phase debt sections. They are not 49
distinct defects. The same facts were re-recorded by each successive phase that tripped over them,
exactly as `CLAUDE.md` section 2 requires (record, do not fix) — so the raw list over-counts badly.

**The five worst duplicate chains**, collapsed:

| Recorded in | Same underlying item | Register entry |
| --- | --- | --- |
| P18.4, P18.5, P18.6 (3 entries) | "the implicit Patankar `Sp` is still zero / `d(omega)/d(Y)` missing" | TD-002 |
| P18.2, P18.3, P18.5, P18.7, KL 1.4 (5 entries) | "library-level only, no case file can reach it" | TD-001 |
| P18.3, P18.6, KL 1.x (2 entries) | "`TotalEnergyState` refuses temperature-dependent `cp`" | TD-011 |
| P18.1, P18.3, P18.5, P18.9, KL 2.2, P17 G10 (6 entries) | "`DriverSeegmiller.MeshSequence` Debug timeout contract" | TD-009 |
| P18.6, P18.7, KL 1.2 (3 entries) | "explicit march step budget / `sum Y = 1` exceeded" | TD-002 |

**19 of the 49 raw entries are duplicates of 5 items.** Treating the list literally would have meant
re-auditing the same `Sp` gap three times and the same reachability gap five times.

---

## 2. Summary

```text
OPEN                      14     TD-003..TD-016, TD-018, TD-019, and TD-020
RESOLVED BY THIS CAMPAIGN  2     TD-001, TD-002
RESOLVED BY LATER PHASE    4
SUPERSEDED                 5
INHERITED (P17/P16)        3
DOCUMENTATION ONLY         3
NOT APPLICABLE             1
```

**The count ROSE to 14 on 2026-10-04**: TD-004's baseline measurement exposed a defect TD-004 is
not authorized to fix (TD-020, below), and recording it is what the campaign requires. Finding debt
while resolving debt is the expected outcome of measuring, not a failure of it.

**The open count did NOT fall when TD-002 closed, and that is the honest number.** The register filed
two different problems under TD-002 -- the round-off step budget, and *"`d(omega_i)/d(Y_j)` does not
exist, so the Patankar `Sp` is zero"*. Its gate text required only the budget, and TD-002's frozen
gate excluded the Jacobian explicitly. The budget half is resolved; the Jacobian half is carved out
below as **TD-019**, so `13 -> 13`.

Counted as distinct items after deduplication, not as raw TODO lines.

**CFDApp does NOT have zero technical debt.** Thirteen items remain open, four of them P0 or P1:
TD-002 (stiff chemistry), TD-003 (the reacting-flow linear-solver audit), TD-004/005/006 (flame,
flame-speed extraction, G8 closure) and TD-007 (re-qualification). TD-001's closure removes the
*root* of the reachability chain; it does not close the chain.

`§3` below keeps its heading for continuity, and TD-001 is retained there with its RESOLVED status
and its run record rather than moved, so the dependency graph in `§5` still reads in one place.

---

## 3. OPEN items

Ordered by the campaign's priority model. Each carries the fields the authorization requires.

### TD-001 — Reacting-species production reachability · **P0**

| field | value |
| --- | --- |
| **origin** | P18.2, restated P18.3, P18.5, P18.7; `KNOWN_LIMITATIONS` 1.4 |
| **evidence** | `results/p18/species_transport/summary.md` §12 · `arrhenius_chemistry/reaction_integration.md` §4 · `mixture_properties/eos.md` §6 · KL 1.4 |
| **measured fact** | 0 files outside `species/` reference the chemistry; 0 case keys define a mechanism. A **seven-phase** gap under CLAUDE.md §9. |
| **risk** | HIGH. P18.2–P18.8 are a verified library, not production capability. Blocks any user-visible reacting claim. |
| **dependency** | none — this is the root of the chain |
| **blocks** | TD-004, TD-005, TD-006, and therefore P18 closure |
| **sub-items folded in** | carrier/complementary-species declaration (P18.1); shared multi-species boundary representation for `sum Y_i = 1` across a patch (P18.2) |
| **acceptance gate** | frozen: `TD001_reacting_production/GATE.md` sha256 `0749efbf…c319b`, amendment `GATE_A1.md` sha256 `53aee7af…9f43b` |
| **negative control** | chemistry-off case must preserve existing behaviour bitwise; invalid mechanism must fail clearly |
| **regression scope** | `CFDIoTests`, `CFDAppLayerTests`, `CFDSpeciesTests`, case-format backward compatibility (committed cases byte-identical) |
| **partial work already present, UNVERIFIED** | see §6 |
| **status** | **RESOLVED** 2026-10-03. Every criterion PASSES under `GATE.md` + `GATE_A1.md` + `GATE_A2.md`; every control fails what it guards; 325 / 32 / 339 all met. Flipped **after** the authoritative rerun, not before. |

#### TD-001 run record (2026-10-03)

The production path is implemented end to end and the gate passes. It stopped **twice** on the way,
and neither stop was repaired by weakening anything.

```text
BUILT      case format -> parser -> builder -> ProjectRunner dispatch -> export -> CLI, as ONE
           operator-split time step (chemistry over dt on the validated initial state, then
           transport of composition and temperature over the same dt).
           grep -c 'setup.reacting' src/app/ProjectRunner.cpp = 3, was 0.

MEASURED   C1 C2 C3 C4a PASS · C4b-i 8.0780 (/10 = 0.8078) PASS · C4b-ii D fell 83.9880x as the
           achieved p residual fell 100.0156x PASS · C4b-iii min Y = +1.230741e-03 PASS
           C4b-iv max|sum Y - 1| = 5.718198e-08 reported · C5 PASS · C6a C6b PASS
           C7a C7b PASS · C8 PASS · C9 PASS (6/6 refused, no partial builds) · C10 BITWISE on all
           three chemistry-off cases · C11 PASS · C12 PASS.   NO criterion failed.

           Two predictions derived from the case file BEFORE the run, both confirmed to < 0.1 %:
             dY  predicted 1.354797e-03   measured 1.355075e-03
             dT  predicted 2.709594 K     measured 2.710194 K

CONTROLS   N1 fails C5/C6/C7/C8 · N2 fails C6a+C6b · N3 fails C7a+C7b while still PASSING C6b
           N4, N5 refused at construction with the measured sum · A1-N1/A2-N1 fails C4b-i
           (ratio 1.0000, dt-independent to 17 digits) · A1-N2 fails C4b-iii (min Y = -0.297)
           A1-N3 fails the original 4*eps*N bound at every tolerance.

TWO AMENDMENTS, both authorized after the failure and the derivation were reported:
  A1   the ORIGINAL C4 applied P18.1's constructed-state bound to an iteratively solved field.
       Unachievable by construction. Preserved in verification.md.
  A2   A1's C4b scaled the LINEAR SOLVER's tolerance, but the deviation is the FLOW solver's
       residual continuity error inherited by the composition: unmoved while residuals fell 430x,
       reproduced in full by ONE species at Y = 1, and scaling with dt and with the achieved
       pressure residual. A1 section 2 had omitted that the convective operator annihilates a
       constant only for a discretely divergence-free flux. Separately, control N3 did NOT fail C7
       -- 5.69e-05 K of transport noise cleared a 1e-9 K threshold, and with zero reaction enthalpy
       the sign clause was undefined. Both preserved in verification_A1.md.
       A2's own PRE-FREEZE dry-run then caught two instrument defects (a zero-prediction loophole
       that would have excused controls A2-N2/A2-N3, and C4b-ii keyed to a flow tolerance the case
       never reaches); both fixed before the freeze, recorded in GATE_A2.md section 4.1.

FOUND ON  making `time_step` required took CFDIoTests to 324/325 and exposed a CONTAMINATED
THE WAY   INSTRUMENT: six refusal tests in test_reacting_case.cpp were being refused for the missing
          key instead of for the defect each one names. Repairing the fixture created a second one
          (an ambiguous `initial_temperature` anchor). Both fixed and recorded in regression.md.

NOT DONE  no threshold weakened, no criterion dropped, no renormalisation, no clipping, no
          carrier-by-subtraction. The 5.72e-08 closure is INHERITED from the flow, now bounded and
          reported rather than concealed. One step, not a march -- TD-002 owns the raw-fields
          reactor entry point a march needs.
```

Evidence: `audit.md`, `reproducer.md`, `GATE.md`, `GATE_A1.md`, `GATE_A2.md`,
`proposed_amendment_A2.md`, `implementation.md`, `verification.md`, `verification_A1.md`,
`verification_A2.md`, `negative_controls.md`, `regression.md`, `summary.md`, `data/`, `logs/00..24`.

### TD-002 — Stiff / bounded chemistry integration · **P0**

| field | value |
| --- | --- |
| **origin** | P18.4, P18.5, P18.6 (the `Sp` chain) + P18.6, P18.7 (the step-budget chain); KL 1.2 |
| **evidence** | `results/p18/heat_release/logs/04_drift_probe.log` · `closed_reactor.md` §4 · `batch_reactor.md` §6 · `reaction_source/transport_integration.md` §2 |
| **measured fact** | explicit first-order march; `sum Y = 1` error grows **linearly at ~0.17 eps/step** and crosses `4*eps*N` at **~72 steps** for 3 species, sooner for fewer. `d(omega_i)/d(Y_j)` does not exist, so the Patankar `Sp` is zero. |
| **risk** | HIGH. Bounds how far any reacting case can be integrated, and therefore what validation is possible. |
| **dependency** | TD-001 (a production path to integrate *in*) |
| **blocks** | TD-004, TD-006 |
| **acceptance gate** | `TD002_stiff_chemistry/GATE.md`; must include long-time integration **beyond** the ~72-step budget, no negative `Y_i`, `sum Y_i` inside the frozen P18.1 invariant, zero-reaction and zero-heat-release limits, determinism |
| **negative control** | restore the explicit march and demonstrate the long-time gate FAILS |
| **forbidden** | clipping, silent normalisation, loosening the `sum Y` tolerance |
| **regression scope** | `CFDSpeciesTests` (chemistry + conservation), P18.8 CPU/GPU equivalence |
| **status** | **RESOLVED (step budget) 2026-10-04**, against `TD002_stiff_chemistry/GATE.md` sha256 `287c7027...`. Every criterion PASSES, every control fails what it guards, 331 / 32 / 343. **The `d(omega)/d(Y)` half of this entry is NOT resolved and is carved out as TD-019.** |

#### TD-002 run record (2026-10-04)

```text
AUDITED    the register's "~72 steps for 3 species" is REAL but was not reproducible on the problem
           the suite integrates: P18.7's reference problem runs 7 521 sub-steps at 0.00 eps because
           its fuel decays to ~1e-323 and its product rises to ~1, where every addition is exact.
           The budget binds through the ACCURACY KNOB instead, and the failure mode is perverse:
           tightening maximumFractionalChange past 0.05 made the integrator THROW.

FIXED      a per-species round-off carry (the low-order bits each store cannot represent) owned by
           integrateChemistry, plus integrateChemistryFromFields -- a raw-fields entry point that
           SHIFTS a transported composition onto an exactly-closing vector and adds the shift back,
           so the inherited closure is carried rather than erased.

BLAST      DeviceSpeciesThermochemistry.hpp DUPLICATES the reactor update and P18.8 gates it
RADIUS     BITWISE. Kahan with a zero carry emits fl(Y + d) for the first step, so the carry went
           into the INTEGRATOR and advanceClosedReactor kept its arithmetic -- proved with TWO
           LIBRARIES over 140 swept states, identical sha256. No device file was touched.

MEASURED   C1 2 445 sub-steps / 0.00 eps (baseline THREW) · C2 all 7 control rungs, 64 -> 42 632
           monotone · C3 85 472 sub-steps / 0.00 eps (2.85x the frozen threshold) · C4 min Y
           +5.00e-01 · C5 refinement preserved · C6a/C6b unchanged · C7 bitwise · C8 two-library
           BITWISE · C9 closure ratio 1.000000 · C10 19.03x over 20 steps from a case file
           · C11 TD-001's five figures BITWISE at steps:1 · C12 checked.

CONTROLS   N1 the march is REFUSED without the carry (a settings flip, same binary) · N2 a
           normalising entry point returns 2.22e-16 for a 5.72e-08 input, failing C9 · N3 S_E = 0
           · N4 stale rates freeze the error at 0.0473867 across a 16x control change, failing C5
           · N5 march removed -> no species exported · N6 steps:1 bitwise equals TD-001.

FOUND ON   the suite asserted sum(Y) to 1.0e-14, 5.6x LOOSER than what MassFractionState already
THE WAY    enforced -- now tightened to the frozen invariant. And making `steps` required repeated
           TD-001's contamination exactly: 327/328 with six refusal tests passing for the missing
           key instead of their own defects.

NOT DONE   no implicit integrator and no Jacobian (TD-019); the 5.72e-08 transport closure is
           carried, not reduced; the march counters are reported in ReactingRunResult but not
           exported to metadata.json.
```

Evidence: `audit.md`, `GATE.md`, `reproducer.md`, `implementation.md`, `verification.md`,
`negative_controls.md`, `regression.md`, `summary.md`, `tools/`, `logs/01..06`.


### TD-003 — Reacting-flow linear-solver robustness · **P0**

| field | value |
| --- | --- |
| **origin** | P18.7 |
| **evidence** | `results/p18/conservation_validation/logs/03_plugflow_diagnosis.log` |
| **measured fact** | BiCGSTAB broke down on a lower-bidiagonal, strongly non-normal pure-upwind matrix and returned `-3.05e12` **while reporting `SolverStatus::Breakdown`**. Restarted GMRES(30), the library default, stagnated on the same matrix. **No existing P18 test helper checks solver status.** |
| **risk** | HIGH. The danger is not breakdown, it is an unchecked status turning a solver failure into a physics result. |
| **dependency** | none |
| **blocks** | TD-004 |
| **acceptance gate** | `TD003_reacting_linear_solver/GATE.md`; canonical matrices for pure-upwind lower-bidiagonal, convection-diffusion and reacting species transport; status always checked; breakdown and non-finite never accepted; residual criterion derived from the governing gate |
| **negative control** | the known `-3.05e12` reproducer must be rejected, not returned |
| **regression scope** | every reacting-flow linear solve; `CFDSpeciesTests`, algebra suites |

### TD-004 — Canonical premixed-flame capability · **P1**

| field | value |
| --- | --- |
| **origin** | P18.7; KL 1.1 |
| **evidence** | `results/p18/gates/P18_GATES.md` G8 · `conservation_validation/audit.md` §1 |
| **risk** | HIGH — G8 is the single blocking P18 gate |
| **dependency** | TD-001, TD-002, TD-003 |
| **blocks** | TD-005, TD-006, P18 closure |
| **scope bound** | 1D laminar premixed flame inside the qualified regime. **Explicitly excluded:** reacting shocks, detonations, supersonic combustion, 3D combustion. Must reuse P18.1–P18.6, not build a parallel combustion stack. |
| **partial work already present, UNVERIFIED** | see §6 |

### TD-005 — Flame-speed extraction · **P1**

| field | value |
| --- | --- |
| **origin** | P18.7; KL 1.1 |
| **dependency** | TD-004 |
| **blocks** | TD-006 |
| **gate note** | the measurement METHOD must be frozen before the answer is looked at; the definition must be derived from the formulation, not selected opportunistically |
| **negative control** | the extractor must FAIL when no flame exists, when the solution is unconverged, and when reaction is disabled |

### TD-006 — Close P18 G8 canonical reacting validation · **P0**

| field | value |
| --- | --- |
| **origin** | P18.7 / P18.9 — the blocking gate |
| **dependency** | TD-001 … TD-005 |
| **blocks** | TD-007, and `P18 OVERALL: COMPLETE` |
| **gate** | **G8 is NOT to be altered.** Its frozen criterion stands. |
| **status of the reference** | the citation and spread are ALREADY frozen, before any measurement, at `results/p18/g8_closure/g8_reference.md`: `35.0 <= S_L <= 37.0 cm/s` from Hermanns (2007) TU/e, DOI 10.6100/IR630126; mechanism WD (1981) Table II Set 3, DOI 10.1080/00102208108946970 |
| **controls** | reactions disabled must fail; 10x pre-exponential must move `S_L` outside the spread |

### TD-007 — Re-run P18 final qualification · **P0**

| field | value |
| --- | --- |
| **dependency** | TD-001 … TD-006 all RESOLVED |
| **note** | old PASS labels may **not** be reused for changed production code. Requires a new `P18_FINAL_SOURCE_FINGERPRINT` and regeneration of `FINAL_QUALIFICATION.md` and `KNOWN_LIMITATIONS.md`. |

### TD-008 — P17 HLLC strong head-on positivity defect · **P0 (P17)** · INHERITED

| field | value |
| --- | --- |
| **origin** | P17 G6 |
| **evidence** | `results/p17/qualification/G6_POSITIVITY_FAILURE.md` · `results/p17/KNOWN_LIMITATIONS.md` line 23 |
| **measured fact** | star-density positivity fails for admissible head-on input; the density-based solver REFUSES loudly (`hllcFluxChecked` throws) rather than returning a wrong answer |
| **dependency** | authorization says only after P18 is closed |
| **blocks** | TD-011 (widening the TotalEnergy/EOS domain touches the HLLC path) |

### TD-009 — P17 Debug+GUI `DriverSeegmiller` timeout contract · **P2** · INHERITED

| field | value |
| --- | --- |
| **origin** | P17 G10; re-recorded P18.1, P18.3, P18.5, P18.9, KL 2.2 — **6 entries, one item** |
| **measured history** | `MeshSequence` Debug: **9 measurements, 5 pass / 4 timeout, every one within 6% of the 7200 s cap**. Release: 1012–1324 s, comfortable (>3.8x faster). |
| **risk** | MEDIUM — a timing contract, NOT a physics failure |
| **forbidden** | raising the 7200 s cap until it passes |
| **note** | `P18_GATES.md` G11 *requires* the 4 Debug timeouts, so changing this interacts with TD-007 |

### TD-010 — Scalar `FixedGradient` coverage / turbulence gradient lag · **P4** / **P2**

| field | value |
| --- | --- |
| **origin** | P18.2 (two entries, correctly distinct) |
| **evidence** | `results/p18/species_transport/boundary_lag.md` §5 |
| **measured fact** | `test_transport_characterization.cpp` has **zero** `FixedGradient` coverage — which is why the lag survived P14's own migration gates. Turbulence still carries the lagged semantics for all six equations. |
| **forbidden** | flipping the generic default globally without re-deriving the P14 migration oracle, whose frozen values encode the lagged behaviour |

### TD-011 — Variable-`cp` `TotalEnergyState` / variable-gamma EOS · **P2**

| field | value |
| --- | --- |
| **origin** | P18.3, restated P18.6 |
| **evidence** | `results/p18/mixture_properties/single_species_compatibility.md` §4 · `heat_release/audit.md` §1 |
| **dependency** | **TD-008 first** — `recover()` feeds HLLC, whose positivity defect is open |
| **forbidden** | silently broadening the accepted physical domain |

### TD-012 — Reversible-chemistry thermodynamic consistency · **P2**

| field | value |
| --- | --- |
| **origin** | P18.5 |
| **evidence** | `results/p18/arrhenius_chemistry/progress_rates.md` §4 |
| **measured fact** | a caller can supply a thermodynamically inconsistent forward/reverse Arrhenius pair and nothing detects it |
| **forbidden** | faking `K_eq`. If proper thermodynamic polynomial data are unavailable, the limitation stays OPEN rather than being papered over with an approximate hidden rule. |

### TD-013 — Reaction-parameter unit safety · **P3**

| field | value |
| --- | --- |
| **origin** | P18.5 |
| **evidence** | `results/p18/arrhenius_chemistry/units.md` |
| **measured fact** | `A`'s units depend on total reaction order, so no single dimensional form is checkable; a user supplying `A` for the wrong order gets a plausible wrong rate. Only the `R_u` mol/kmol basis is checked, and it is. |
| **acceptable outcome** | if runtime dimensional validation is infeasible without overhauling the input model, DOCUMENT the limitation rather than claiming it solved |

### TD-014 — Thermochemistry ownership / registry identity · **P3**

| field | value |
| --- | --- |
| **origin** | P18.6 |
| **evidence** | `results/p18/heat_release/species_enthalpy.md` §2 |
| **measured fact** | `MixtureThermodynamics` carries `M_i` + sensible model, `FormationEnthalpies` carries `h_f,i`; only a registry match links them. The split is **deliberate** — it makes G7's named confusion hard to write. |
| **objective** | prevent registry mismatch / caller misuse by construction-time identity. **Not** a merge for aesthetics. |

### TD-015 — Evidence reproducibility gaps · **P4**

| field | value |
| --- | --- |
| **origin** | P18.5 (about P18.4) |
| **measured fact** | P18.4's probe cannot be rebuilt from its own evidence — the compile command was never recorded. P18.5's `tools/run_probe.sh` records command, compiler version and library hash; P18.4's was not changed. |
| **forbidden** | rewriting historical measurements. Add a reproducible replacement, preserve the old artifact. |

### TD-016 — Separate-process determinism · **P4**

| field | value |
| --- | --- |
| **origin** | P18.7 |
| **measured fact** | **not measured at all.** §43 made it conditional on the frozen gate and G10 did not require it; recorded rather than silently skipped. |
| **note** | authorization says do not make it a release blocker unless its own frozen TD gate says so |

### TD-020 — No early-out when a mixture cannot sustain a flame · **P2**

| field | value |
| --- | --- |
| **origin** | measured during TD-004's per-test baseline, 2026-10-04 |
| **evidence** | `results/technical_debt/TD004_premixed_flame/logs/06_flame_baseline_per_test.log` · `audit.md` §4.5 · `GATE.md` §5.1 |
| **measured fact** | `G8PremixedFlameTest.WithReactionsDisabledNoFlamePropagates` **TIMES OUT after 900 s**. With `reactionsEnabled = false` the seeded tanh front washes out slowly instead of vanishing, so early trials still locate a front and return a sided verdict; the bracket then grinds through `maxOuterIterations = 2000` at up to `maximumStepsPerTrial = 20000` steps each. The `!sided -> NoFlame` short-circuit fires only after the front is already gone. **The no-flame case costs MORE than the flame case.** |
| **risk** | MEDIUM. This test is **G8 negative control 1**, so while it cannot run, G8 cannot be closed with both of its controls exercised — which makes this a prerequisite for TD-006 even though it is only P2 on its own merits. |
| **dependency** | none |
| **blocks** | TD-006 (G8 needs both negative controls demonstrated) |
| **acceptance gate** | not frozen. Would need: a cheap, stated criterion for "no flame is possible here" evaluated BEFORE the eigenvalue search (the reaction enthalpy is already checked for zero heat release at `PremixedFlame.cpp:438`, so the hook exists); the control completing well inside a normal test budget; and the existing assertions — status not `Converged`, zero peak heat, zero consumption speed, zero chemistry sub-steps — still holding, so the early-out cannot become a way of passing the control without testing it. |
| **forbidden** | raising the test's timeout, or lowering `maxOuterIterations` in the test to make it finish. Both make the control finish without making it work. |
| **regression scope** | `G8PremixedFlameTest`, and any caller relying on `PremixedFlameStatus::NoFlame` |

### TD-019 — Implicit chemistry Jacobian / non-zero Patankar `Sp` · **P2**

| field | value |
| --- | --- |
| **origin** | carved out of TD-002 on 2026-10-04, when TD-002's gate closed the step-budget half. Recorded by P18.4, P18.5 and P18.6 as "the implicit Patankar `Sp` is still zero". |
| **evidence** | `results/technical_debt/TD002_stiff_chemistry/audit.md` §4 · `summary.md` §5 · `reaction_source/transport_integration.md` §2 |
| **measured fact** | `d(omega_i)/d(Y_j)` does not exist, so `ScalarSourceFields::Sp` is zero for every reaction source. P18.5 added `rateCoefficientDerivative` (the TEMPERATURE derivative); the CONCENTRATION derivative a stiff implicit source needs is still absent. |
| **risk** | **LOW for the mechanisms currently in scope, and that is a measurement rather than an assumption.** TD-002's audit found the step budget to be round-off accumulation, not stiffness; accuracy still improves under refinement (`1.0097e-01 -> 2.1409e-02 -> 2.1436e-03`), and a sub-stepped explicit march now reaches 85 472 sub-steps at zero drift. A genuinely stiff mechanism — multi-step, widely separated time scales — would change this. |
| **dependency** | none |
| **blocks** | nothing currently open. It is a prerequisite for a stiff MECHANISM, not for TD-004/005/006 as scoped. |
| **acceptance gate** | not frozen. Would need: an analytically differentiated `d(omega_i)/d(Y_j)` checked against complex-step or high-precision finite differences; a Newton solve with a convergence criterion; and a mechanism stiff enough that the explicit march demonstrably fails, as the non-vacuity control. |
| **forbidden** | building it on speculation. TD-002's gate excluded it for a stated reason: no measurement in the repository currently demands it, and a Jacobian plus Newton solve is a larger, separately-gateable piece of numerics. |
| **regression scope** | `CFDSpeciesTests`, P18.8 CPU/GPU equivalence (the device restates the source terms) |

### TD-018 — Repository hygiene / push blocker · **P5**

| field | value |
| --- | --- |
| **origin** | P18.1; KL 2.4 |
| **measured now (this audit)** | `git diff --check` reports **328 warnings** (656 output lines), unchanged from KL 2.4, in exactly three files: `cuda/CMakeLists.txt` 206, `tests/unit/gpu/CMakeLists.txt` 90, `tests/unit/compressible/CMakeLists.txt` 32 |
| **root cause** | all three are CRLF files under `.gitattributes` `* -text`, so **every** added line trips the check. P18 did not introduce the condition. |
| **attribution** | first two: P18 added lines to a pre-existing CRLF file. Third: **P17's, not P18's**. |
| **risk** | PUSH BLOCKER under CLAUDE.md §11 |
| **forbidden** | blind repository-wide line-ending conversion |

---

## 4. Items classified OUT — do not re-implement

### RESOLVED BY LATER PHASE

| raw entry | origin | resolved by |
| --- | --- | --- |
| "G6 is only half closed" — the rate criterion and 2 of 3 controls are P18.5's | P18.4 | **P18.5** closed G6(a); `FINAL_QUALIFICATION.md` records **G6 PASS**. Do not re-audit. |
| "Debug+GUI known-limitation refinement" — the set is 3 reliable + 1 marginal, not 4 | P18.1 | **P18.9** characterised it fully: 9 measurements, 5 pass / 4 timeout. The *characterisation* is done; the *contract* remains TD-009. |
| "28 pre-existing clang-format violations" | P18.1 | **P18.9** measured `clang-format-18` at **0 violations across all P18 sources**; the whitespace issue is separately and precisely accounted as the 328 `diff --check` warnings in TD-018. Superseded in form. |
| "Elemental validation deferred — P18.7 can own it" | P18.4 | **P18.7/P18.9** deliberately did NOT implement it; it is recorded as a stated limitation (KL 1.3), not an open task. See NOT APPLICABLE below. |

### SUPERSEDED (replaced by a later, better measurement of the same thing)

| raw entry | superseded by |
| --- | --- |
| "marginal Debug+GUI timeout measured three times" (P18.3) | the 9-measurement history in TD-009 |
| "`MeshSequence` has five measurements and no predictable verdict" (P18.5) | same |
| "P18.9's G10 should predict the branch, not a number" (P18.5) | P18.9 did exactly this; G11 PASS |
| "the sum-to-one budget is tighter for fewer species" (P18.7) | folded into TD-002's single step-budget statement |
| "the implicit Patankar `Sp` is still zero" × 3 (P18.4/5/6) | folded into TD-002 |

### INHERITED — not P18's

* **TD-008** (P17 G6 HLLC positivity) and **TD-009** (P17 G10 Debug timeout) — carried as TD items
  by this campaign's own authorization, but attributed to P17.
* **P16 deferred SST near-wall work** — deferred to P20, NOT AUTHORIZED. The frozen exclusion list
  carries 4 SST/wall patterns for this reason. Not a TD item.

### DOCUMENTATION ONLY → TD-017

1. **`KNOWN_LIMITATIONS.md` §2.2 is STALE.** It says `MeshSequence` has **8 recorded measurements,
   5 pass / 3 timeout**. Both `FINAL_QUALIFICATION.md` line 157 and `TODO.md` line 489 say
   **9 measurements, 5 pass / 4 timeout**. The qualification report is authoritative; the
   limitations document was not updated after the final run.
2. **`KNOWN_LIMITATIONS` L36 wording** — its `<= 4.1e-07` bound is about *solution error norms*;
   applied to a near-zero convergence residual it reads as a five-order violation. The comparator
   now separates the two; L36's own wording does not.
3. **`distorted_mesh_simple_mms` was not re-measured** after the validation-noise finding; the
   conclusion rests on tracked magnitudes rather than a regeneration.

### NOT APPLICABLE

* **Elemental conservation is not modelled** (KL 1.3). No elemental metadata exists and §10/§19
  forbid inventing one. This is a **stated limitation of the physical model**, not a defect: what
  P18 conserves is mass through `sum_i M_i nu_i = 0`. It is **not in this campaign's TD list** and
  is recorded here so its absence is deliberate.

---

## 5. Dependency chain

```text
TD-001 reachability ──┬─> TD-004 flame ──> TD-005 extraction ──> TD-006 G8 ──> TD-007 re-qualify
TD-002 stiff chem  ───┤                                                            │
TD-003 linear solver ─┘                                                            │
                                                                                   v
                                                        TD-008 HLLC ──> TD-011 variable cp
                                        TD-009 timeout contract (interacts with TD-007 via G11)

independent, may proceed without hiding a blocker:
   TD-010  TD-012  TD-013  TD-014  TD-015  TD-016  TD-017  TD-018
```

---

## 6. In-flight uncommitted work that PARTIALLY touches TD-001…TD-005

**Recorded because ignoring it would be dishonest and claiming it would be worse.** A prior
authorization in this session ("authorize the reacting-flow solver to close G8") produced
uncommitted work that lands in TD-001, TD-002, TD-004 and TD-005 territory — **ahead of this
campaign's ordering**, and none of it has been through this campaign's freeze-gate protocol.

| file | TD territory | state |
| --- | --- | --- |
| `include/cfd/io/case/PhysicsConfig.hpp`, `src/io/case/PhysicsConfigParser.cpp` | TD-001 | reacting species/mechanism case keys parse; **does NOT reach ProjectRunner or the CLI** |
| `include/cfd/io/SimulationSetup.hpp`, `src/io/CaseBuilder.cpp` | TD-001 | builds live mechanism/kinetics objects from configuration |
| `tests/unit/io/test_reacting_case.cpp` | TD-001 | 9 tests pass |
| `species/ChemistryIntegrator.{hpp,cpp}` | TD-002 | adaptive sub-stepping of P18.6's qualified step; 9 tests pass. Does **NOT** supply `d(omega)/d(Y)`, does **NOT** address the `sum Y` budget, and is **constant-density** (wrong closure for a constant-pressure flame) |
| `species/ReactionMechanism` reaction orders, `tests/.../test_reaction_orders.cpp` | TD-002/004 | fitted non-stoichiometric reaction orders; 11 tests pass |
| `species/PremixedFlame.{hpp,cpp}`, `tests/.../test_premixed_flame.cpp` | TD-004/005 | **PARTIAL — 12 active tests, not all passing** |
| `species/ArrheniusKinetics` raw-field path | TD-002 | additive entry point for iterate states |

**Verified about the flame work:** `S_L` converges under mesh refinement at observed order
**p = 1.93**, Richardson `S_L = 4.894 cm/s`, GCI **0.21 %** — which clears the precision
precondition that a `+/- 2.8 %` band requires. Conservation `|sum Y - 1| ~ 1.6e-6`, peak `T` exactly
adiabatic.

**Open against it:** the fuel-balance residual is **4-5 % and grows with march length and mesh
refinement** (0.0222 -> 0.0433 -> 0.0545), which refutes the discretization hypothesis; both runs
report `NOT steady`. Full findings, including **three withdrawn/incorrect analyses of my own**, are
at `results/p18/g8_closure/flame_solver_findings.md`.

**Disposition:** this work is **not** credited against any TD item. TD-001 is the first item in the
campaign's order and its gate is not frozen; TD-004 may not close before TD-001…TD-003. The code
stays in the tree (nothing is reverted, per §50) and will be re-audited under each item's own frozen
gate when that item is reached.

---

## 7. What TD-000 did NOT do

* did not fix any debt item;
* did not change any production file;
* did not clean the pre-existing dirty tree;
* did not rewrite historical phase evidence — the three documentation corrections in §4 are
  recorded for TD-017, not applied here;
* did not assign a TD number outside the campaign's own TD-001…TD-018 list.
