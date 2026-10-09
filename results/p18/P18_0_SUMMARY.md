# P18.0 — Baseline & Audit — SUMMARY

```text
P18.0 STATUS: COMPLETE

P18_START_SOURCE_FINGERPRINT  60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318
                              UNCHANGED across the whole phase -- P18.0 audited the tree, it did
                              not touch it. All three fingerprint components re-verified at close.
P18 GATES FROZEN              results/p18/gates/P18_GATES.md
                              sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
P17 STATUS                    PARTIAL, preserved. Neither open gate reopened or repaired.
NO P18 PHYSICS IMPLEMENTED    0 production files changed. The one artefact added is an audit-only
                              probe, labelled instrumentation.
```

---

## 1. The three findings that shape P18

```text
1. SPECIES IS A SHIPPED PRODUCTION FEATURE, not a library experiment.
   Full chain: physics.json -> parser (strict key rejection) -> config -> CaseBuilder ->
   ProjectRunner -> SpeciesSolver -> CSV/VTK/JSON export + run metadata. Two case directories,
   92 registered test rows, 14 parser tests covering duplicate names and unknown keys.
   -> P18 must EXTEND it compatibly. Duplicating it would fork a user-visible feature, which is
      exactly what the authorization forbids.

2. THERE ARE ALREADY TWO SCALAR-TRANSPORT STACKS, and species uses the weaker one.
   P14's cfd::transport::ScalarTransport has the rho-weighted transient (rhoOld/rhoNew), per-cell
   Su/Sp sources, and per-cell diffusivity with face interpolation -- precisely the three things
   P18 needs and species lacks. Species references it ZERO times.
   -> P18 should COLLAPSE the two, not add a third.

3. THE BOUNDARY LAG IS A CONFIRMED DEFECT, MEASURED -- and migrating to P14 would NOT fix it.
   Identical pure-Neumann problem: SPECIES 335 outer iterations, THERMAL 2. 167x.
   Thermal was fixed in P12-MESH-003; species was knowingly left. And P14 carries the SAME lag
   (ScalarTransport.cpp:129-133), so two of the three scalar assemblies have it.
   -> the exact gradient treatment must be applied DELIBERATELY by whichever stack P18.2 builds on.
      It cannot be inherited. Gate G4(c) carries it, with the current behaviour as a free control.
```

## 2. Baseline (§3)

| Configuration | Selected | Passed | Failed | Skipped | Disabled | Runtime | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| Release+CUDA | 2763 | 2712 | 0 | 2 | 49 | 2226 s | matches P17.11 |
| CPU-only | 2540 | 2493 | 0 | 0 | 47 | 1675 s | matches P17.11 |
| Debug+GUI | 2825 | 2770 | **4** | 2 | 49 | 8514 s | matches P17.11, incl. its 4 inherited timeouts |

```text
EVERY COUNT MATCHES, across all three. The 4 Debug+GUI failures are the identical DriverSeegmiller
timeout set P17.11 recorded, test id for test id -- no fifth failure, so nothing moved between the
two runs on an unchanged tree. Recorded as INHERITED BASELINE BEHAVIOUR; the 7200 s timeout was not
raised. libcfdcore.a unchanged in both Release configurations.
```

## 3. Existing species architecture (§5–§16)

```text
transported variable      Y itself -- NOT rho*Y. Convection uses massFlux with coefficient 1.
equation assembled        div(rho U Y) - div(rho D grad Y) = S
                          STEADY (no transient term at all), CONSTANT density, conservative-LHS,
                          PASSIVE / non-reacting.
number of species         N, by calling solve() once per species. No collection type, no coupling.
diffusion model           J = -rho*D*grad(Y), D a SINGLE CONSTANT Real per species.
                          No mixture D, no Schmidt model, no D(T,Y), no per-cell D.
source model              one UNIFORM Real added to the RHS. No per-cell source, no implicit Sp.
boundary model            generic FixedValue / FixedGradient, per species per patch. No
                          species-specific BC classes. Gradient faces are LAGGED.
bounds                    0 <= Y <= 1 is CHECKED on request, NEVER enforced, NEVER clamped --
                          deliberately. sum(Y_i)=1 does not exist anywhere.
conservation              the steady balance is expressible and measured; the transient term and
                          any cross-species closure are NOT expressible.
production reachable      YES, end to end, incl. export and run metadata.
CPU path                  complete.
GPU path                  NONE -- 0 GPU references on the species path.
```

## 4. Species boundary lag (§12, §13)

```text
CLASSIFICATION            CONFIRMED DEFECT -- of convergence COST, not of the converged answer.
execution sequence        SIMPLE converges -> massFlux FIXED -> per species: Picard loop of
                          { assemble(concentration) -> BiCGSTAB -> maxChange -> repeat }
                          with the boundary evaluated from the PREVIOUS OUTER ITERATE.
expected state            dY/dn = 0 from ONE assembly -- what thermal achieves.
actual state              Y_P from the previous outer iterate; the +Df diagonal and the
                          -Df*Y_P^lagged RHS cancel only at convergence.
measured difference       335 outer iterations (species) vs 2 (thermal), identical problem. 167x.
                          31 outer iterations on a mixed Dirichlet/Neumann convective case.
why a defect, not a       the project already ruled this pattern a defect and FIXED it in thermal;
scheme choice             an exact assembly exists 5 lines away and is both cheaper and more
                          accurate; and below ~335 iterations the solver returns a NON-CONVERGED
                          field (measured at caps 1, 2, 5, 20).
NOT fixed                 §12 forbids it; the probe is instrumentation only.
```

## 5. Reuse map (§17–§22)

```text
P14 REUSABLE          rho-weighted convection on the authoritative massFlux; per-cell Su explicit
                      source; per-cell Sp implicit source; per-cell diffusivity + face
                      interpolation (Arithmetic reproduces constant-D exactly)
P14 EXTENSIONS NEEDED the transient term means committing to the variable-density conservative
                      form (a re-derivation, not a flag); the gradient boundary must be made exact;
                      the GPU path's reachability outside turbulence is unestablished
P14 NOT COMPATIBLE    bounded mass fractions (nothing exists); multi-species collection and
                      sum(Y_i)=1 (both stacks are strictly per-scalar)

P15 REUSABLE          cp(T) via TemperatureDependentProperty; SensibleEnthalpy's h(T) and its
                      T(h) bisection; k(T); the per-cell heat-source FIELD overload; Robin/flux BCs
P15 EXTENSIONS NEEDED one SensibleEnthalpy per species; h_mix = sum Y_i h_i and its inversion;
                      formation enthalpy h_f,i -- and referenceEnthalpy must NOT be repurposed as
                      one, they are different quantities

P17 REUSABLE          the EOS/thermo architecture and its validation approach; TotalEnergyEquation
                      already accepts a per-cell volumetricSource and carries it in its balance
P17 EXTENSIONS NEEDED IdealGasEOS holds ONE gasConstant_ (rho = p/(RT) in 4 places);
                      ThermodynamicProperties stores one cp and DERIVES cv and gamma;
                      TotalEnergyState REFUSES a T-dependent cp and its own message names the
                      prerequisite -- "requires a variable-gamma EOS"; the closed-form T recovery
                      becomes a root find; DeviceThermodynamics is a 9-scalar POD that a mixture
                      cannot fit. And the one-way ProjectRunner ordering means composition-dependent
                      density is an ARCHITECTURE change, not an algebraic one.
```

## 6. Capability classification (§26)

```text
IMPLEMENTED (6)   passive species transport · species boundary conditions (with the lag caveat)
                  CPU production path · CLI · export · validation (for the passive steady capability)

PARTIAL (8)       multi-species state (N independent scalars, no collection)
                  species-dependent diffusivity (own constant D each, no mixture model)
                  species conservation (steady only; transient and cross-species not expressible)
                  thermal coupling (source plumbing exists; nothing produces a species source)
                  total-energy coupling (source accepted, but that path is P17 library-only)
                  species BCs (Dirichlet/Neumann only; gradient faces lagged)
                  ... see audit/capability_matrix.md for the full 24-row table

MISSING (10)      mass-fraction representation · sum(Y_i)=1 · reaction source framework ·
                  stoichiometry · Arrhenius chemistry · mixture W · mixture R · mixture cp/cv/gamma ·
                  species enthalpy · heat release · GPU production path · restart · GUI
```

## 7. Gates (§28–§31)

| Gate | Dry-run on today's tree |
|---|---|
| G1 baseline / regression preservation | **PASSES** (baseline measured; SIMPLE fingerprint bitwise with its planted-defect control) |
| G2 species-state validity | **NOT IMPLEMENTED** — no collection, no ΣY=1, no bounds enforcement |
| G3 species conservation | **PARTIAL PASS** — steady per-species passes; transient and mixture sum not expressible |
| G4 transport / diffusion | (a)(b)(d) **PASS** · **(c) FAILS: 335 outer iterations against a criterion of ≤2** |
| G5 mixture thermodynamic closure | **NOT IMPLEMENTED** |
| G6 reaction-rate correctness | **NOT IMPLEMENTED** |
| G7 reacting energy conservation | **NOT IMPLEMENTED** |
| G8 canonical reacting validation | **NOT IMPLEMENTED** |
| G9 CPU/GPU equivalence | **NOT IMPLEMENTED** — no GPU species path |
| G10 deterministic repeatability | **PASSES for what exists** — 5 determinism tests at 3 levels |
| G11 full regression / qualification | **baseline established** |

```text
G4(c) IS THE ONLY GATE THAT FAILS ON EXISTING, SHIPPED CODE. Every other failure is "the capability
does not exist yet". That distinction is the most useful output of the dry-run.

THE DRY-RUN ALSO CAUGHT THREE GATES THAT WOULD HAVE BEEN WEAK:
  G3's mixture criterion would have been claimable from tests that already pass -- split.
  G5's single-species limit is the only mixture criterion measurable today, and it is what stops
    P18.3 quietly changing P17's numbers -- frozen as BITWISE.
  G9 could have been claimed from DeviceScalarTransport merely existing -- it now requires an
    end-to-end species solve on device, because every consumer of that API is k-epsilon.
```

## 8. Build-phase reassessment (§27)

```text
The planned P18.1..P18.9 sequence still makes sense. Three corrections to expectations:

  P18.2 Species Transport & Diffusion   MOSTLY MIGRATION + ONE REAL DEFECT FIX, not new physics
  P18.4 Reaction Source Framework       LIGHTER than it sounds -- P14's Su/Sp assembler already
                                        exists; the work is producing values, not assemblers
  P18.8 CPU/GPU Equivalence             AN IMPLEMENTATION PHASE, exactly as P17.10 turned out to be

SEPARABLE, offered not acted on: the boundary-lag fix is P12-era debt independent of everything
else in P18. It could be its own small authorized item and would immediately benefit the shipped
species cases (167x fewer outer iterations on a pure-Neumann problem).
```

## 9. Repository integrity (§35)

```text
working tree            316 entries, ALL classified into 15 buckets, 0 unexplained
production changed      NONE by P18.0 -- proved by all three fingerprint components matching, not
                        by a status count (which cannot distinguish "P18.0 created this" from
                        "already untracked")
added by P18.0          evidence under results/p18/ (8 audit docs, gates, baseline, logs, tools)
                        + ONE audit-only probe: results/p18/tools/boundary_lag_probe.cpp,
                        labelled INSTRUMENTATION in its own header, implementing no physics
threshold changes       none to any existing gate. P18's own gates are new and frozen.
runtime noise           50 results/validation files rewritten by the Debug+GUI run with
                        DEBUG-computed physics values (the known L36 effect). Attribution
                        re-verified for this run: non-timing keys confined to MMS/grid-convergence
                        reports, wall_shear changes exactly 0. Restored; 0 modified after.
history                 nothing committed, pushed, reset, cleaned or stashed.
```

## 10. Evidence index

```text
results/p18/
├── baseline/   source_state.md · source_fingerprint.txt · release_cuda.md · cpu_only.md ·
│               debug_gui.md · summary.md · logs/
├── audit/      species_inventory.md · production_path.md · boundary_lag.md · p14_reuse.md ·
│               thermal_energy_coupling.md · gpu_inventory.md · test_coverage.md ·
│               capability_matrix.md
├── gates/      P18_GATES.md (frozen 92161654...) · freeze.log
├── tools/      boundary_lag_probe.cpp (instrumentation) · freeze_p18_start.sh ·
│               p18_baseline_regress.sh · freeze_gates.sh · tree_integrity.sh ·
│               verify_no_source_change.sh · restore_validation_noise.sh · run_lag_probe.sh ·
│               update_control_docs.py · fix_control_doc_glyphs.py · append_summary_close.py
├── logs/       11_lag_probe.log · 98_tree_integrity.log · 99_no_source_change.log
└── P18_0_SUMMARY.md
```

## 11. Control-document update (§37)

`TODO.md` and `ROADMAP.md` are the only two files outside `results/p18/` that P18.0 changed, and
both are excluded from the source fingerprint by construction, so the integrity proof in §9 still
holds after them. Applied by `results/p18/tools/update_control_docs.py`.

```text
TODO.md     P18.0 block   CURRENT -> COMPLETE, all 8 items [x], with the measured baseline counts,
                          the four audit findings and the frozen-gate dry-run recorded inline
            execution     P18 ACTIVE / P18.0 COMPLETE / P18.1 NOT AUTHORIZED YET; the stale
            order         "P18 ACTIVE + AUTHORIZED" line replaced
            NEXT          P18.1 - Multi-Species State, NOT AUTHORIZED YET

ROADMAP.md  §2           P18 becomes the ACTIVE phase; P17 moves to a new "Prior phase, closed as
                          PARTIAL" heading with its bullets unchanged and marked not reopened
            §2           P18 removed from "Future phases" (it is no longer future)
            §12          heading NOT AUTHORIZED -> ACTIVE (P18.0 COMPLETE); the closing
                          "P18 does not begin until explicitly authorized" replaced by what the
                          audit established, and by the note that each later step still needs its
                          own authorization
            §23          ladder: P17 corrected from "ACTIVE / P17.0 COMPLETE" (stale since
                          P17.11) to PARTIAL; P18 ACTIVE with its two sub-steps
```

Both files are CRLF on disk while Git Bash presents them as LF, so the patch is applied in binary
mode; `file` confirms CRLF is preserved on both, and the edits are localized rather than a
whole-file line-ending rewrite.

## 12. One self-inflicted finding, recorded rather than quietly fixed

```text
WHAT   update_control_docs.py writes its anchors in ASCII and translates " - " to the em dash the
       control documents use. That rule is too broad: it also rewrote the MINUS SIGN in

           div(rho U Y) - div(rho D grad Y) = S

       into an em dash, in 3 places across the two documents.

WHY IT MATTERS ENOUGH TO RECORD   that equation is the central factual claim of this audit -- the
       whole point of section 3 is that the shipped solver assembles THAT equation and not the
       conservative mass-fraction form. A dash that is not a minus misstates it, and it would have
       been read as typography rather than as an error.

CAUGHT BY   reading the applied diff, not by the script, which cannot tell a separator dash from
       an operator. FIXED BY results/p18/tools/fix_control_doc_glyphs.py, which asserts the exact
       occurrence count (1 in TODO.md, 2 in ROADMAP.md) before touching anything.

NO PRODUCTION IMPACT: both files are documentation and both are outside the fingerprint. Recorded
       because the phase rule is to preserve findings, including the ones I caused.
```

## 13. P18.0 status

```text
P18.0                      COMPLETE
production code changed    NONE (fingerprint 60cf951b... identical at start and end)
gates                      FROZEN, sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
                           no amendments, no threshold weakened
baseline                   reproduces P17.11 exactly in all three configurations
open defect found          species boundary lag (G4(c)) -- NOT fixed, fixing it is not P18.0 scope
P17 open items             G6 and G10 untouched, as instructed
next                       P18.1 - Multi-Species State, NOT AUTHORIZED
```
