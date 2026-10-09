# P17.0 — Baseline & Capability Audit: summary

**P17.0 STATUS: COMPLETE.** 2026-09-26.

All eight checklist items are done and evidenced. No production code, test, threshold
or prior evidence was changed. No P17.1+ implementation was started.

---

## 1. Baseline

```text
HEAD                    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274   (main, DIRTY)
branch                  main
working tree            167 tracked modified, 107 untracked, deliberately dirty:
                        P13, P14, P15, P16 and P17.2 are all uncommitted
submodules              NONE          stash  empty
SOURCE_FINGERPRINT_V2   fbf786656eac2284956a4e29fe2745157a3e59ab339833a9f49ff6e048bd9fd6
P17_CODE_FINGERPRINT    1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
CPU regression          PASS -- 2297 / 2297, 0 failed   (CPU-only, Release, CUDA=OFF)
CUDA regression         PASS -- 2497 / 2497, 0 failed   (Release+CUDA)
Debug+GUI               NOT RUN -- user decision, see baseline/DECISION_debug_gui_scope.md
```

Environment: WSL2 Ubuntu 22.04.5, gcc 11.4.0, CMake 3.22.1, CUDA 12.9 V12.9.86,
RTX 5000 Ada (compute 8.9), driver 580.97. Per `CLAUDE.md` §4 this does **not**
establish native Windows CUDA, which remains unqualified.

### Every frozen prediction matched

The gate was frozen with predicted counts **before** any run
(`baseline/baseline_gate.md`, sha256 `a147f79c…`). That is what makes the regression an
instrument rather than a formality.

```text
                              PREDICTED  MEASURED        PREDICTED  MEASURED
                              Release+CUDA               CPU-only
registered                      2559       2559            2357      2357
matching the exclusions           13         13              13        13
disabled (unique)                 49         49              47        47
executed                        2497       2497            2297      2297
failed                             0          0               0         0
IdealGasEOSTest rows Passed       21         21              21        21

2559 - 13 - 49 = 2497   exact        2357 - 13 - 47 = 2297   exact
```

A run reporting only "PASS" would have been consistent with a stale binary silently
omitting P17.2's nine tests. `build/qual-cpuonly` had **not** been rebuilt since P13's
G10, so that is exactly where staleness would have shown — and 2297, not 2288, is what
came back.

## 2. Capability summary

```text
Variable density                   IMPLEMENTED
Ideal-gas EOS                      IMPLEMENTED
Compressible continuity            PARTIAL
Compressible momentum              PARTIAL
Total-energy equation              MISSING
Low-Mach validation                IMPLEMENTED
HLLC / Riemann flux                MISSING
Density-based solver               MISSING
Shock validation                   MISSING
CPU compressible support           IMPLEMENTED
GPU compressible support           MISSING
CPU/GPU compressible equivalence   MISSING
-------------------------------------------------
IMPLEMENTED 4     PARTIAL 2     MISSING 6
```

The low-Mach steady 2D regime is genuinely built **and physically validated**. The
higher-Mach shock-capturing regime does not exist at all.

## 3. Most important findings

**1. The compressible viscous stress is incomplete.** Momentum uses
`physics::assembleDiffusionContribution` — the *incompressible* Laplacian `mu lap U`.
The dilatational term `-(2/3) mu grad(div U)` is absent: `grad(div` 0 hits,
`dilatation` 0, `bulkViscosity` 0, `twoThirds` 0, `Stokes` 0. The MMS test names it in
its own machine-readable `not_covered` field. Harmless through P16 where `div U = 0`;
compressible flow is exactly the regime where it is not. **This is real
implementation work hiding inside a checklist item marked "verify".**

**2. A symbol census would have reported capability that does not exist.**
`Roe` returns 14 hits — every one a substring of "zeroes"/"zeroed"/"Zero".
`Euler` returns 107 — every one implicit-Euler *time integration*.
`Riemann` returns 1 — a forward-looking comment P17.2 itself wrote.
Concluding from those that a Riemann/Euler-flux foundation exists would have been
wrong, and the raw counts invite it.

**3. Two tested modules have no production caller.**
`evaluateCompressibleContinuity` (6 unit tests) is a *diagnostic* by its own header;
`CompressibleSIMPLE` calls the steady `physics::evaluateContinuity` at line 478
instead. `assembleCompressibleMomentumComponent` (7 tests) calls `.build()` internally
so it has no relaxation hook and cannot be used in a relaxed loop. Both are deliberate
library entry points — neither is production capability.

**4. Compressible continuity in production is the pressure-correction equation.**
Its own header says so: the `dDensityDPressure` compressibility term makes it "the
discretized compressible continuity constraint solved for pressure", which is why no
separate continuity solve exists. The form enforced is **steady** `div(rho U) = 0`;
`pseudoTimeStep` is stabilization, not physical time.

**5. The low-Mach path has genuine literature validation**, which the earlier audit did
not credit. `MatchesArkilicIsothermalLubricationPressureProfile` compares
`pressureAbsolute` against the analytical isothermal compressible lubrication solution
through the real production path, at L2/Linf < 5% — a tolerance derived from the
lubrication approximation's own `O(Re*(H/L)) ~ 0.17` error, bounding a measured ~1.3%
L2 / ~2.6% Linf. Plus a negative control proving the post-hoc mode is unchanged.

**6. `SOURCE_FINGERPRINT_V2` is documentation-contaminated.** During this freeze
`ROADMAP.md` was rewritten by concurrent work (624+/257-) and V2 moved
`fb69dc2a -> fbf78665` with **zero source change** — proved by the untracked component
being byte-identical and `src/compressible/IdealGasEOS.cpp` still hashing to the value
P17.2's controls recorded. V2 was **not** redefined; a separately named, narrower
`P17_CODE_FINGERPRINT` was added alongside it.

**7. A latent dual-`cp` inconsistency.** `compressible.specific_heat_pressure` (a
constant) and `thermal.specific_heat` (optionally a polynomial `cp(T)`) can both be
configured in one case with nothing reconciling them. Not active in any present case;
reachable by construction. HIGH severity for P17.5 — an energy-conservation gate could
close on the wrong thermodynamics and appear to pass.

## 4. Actual implementation gap

```text
P17.1 P17.3 P17.6   VERIFY existing code + freeze gates. Not implementation.
P17.2               DONE (closed, PASS).
P17.4               VERIFY + one real physics addition: the dilatational stress term.
P17.5               BUILD: total energy in full -- e, e_t, E, h_t, conservative
                    assembly, EOS coupling, pressure work, conduction, viscous work,
                    temperature recovery T(e).
P17.7               BUILD: conserved state, inviscid flux, L/R reconstruction, wave
                    speeds, contact wave, positivity.
P17.8              BUILD: conserved update, ACOUSTIC CFL control, primitive<->conserved,
                    positivity protection.
P17.9              BUILD: Sod, contact, supersonic advection, normal shock, expansion,
                    plus an independent analytic Riemann reference containing no
                    CFDApp code.
P17.10             VACUOUS today -- no GPU compressible path exists. Deferred.
```

Five of twelve sub-items are verification work; four are the real build; one is done;
one is vacuous.

### Cross-cutting blockers not in any checklist item

```text
B1  2D ONLY -- requireTwoDimensional at CompressibleSIMPLE.cpp:147. Sod and a normal
    shock are 1D so P17.9 is not blocked, but every claim must say "2D".
B2  UPWIND ONLY -- P17.7's "reconstruction interface for higher order" is new work,
    not an extension of anything.
B3  DUAL cp -- finding 7 above. Decide before P17.5.
B4  pseudoTimeStep has NO case-format key -- 0 hits across src/io, include/cfd/io,
    apps, docs, cases, tests/data. Unreachable capability.
B5  SEGREGATED vs CONSERVED-STATE energy -- a P14-based segregated P17.5 may not be
    reusable by P17.8. Factoring the thermodynamic closure out is the option that
    avoids duplicating the physics.
B6  NO ANALYTIC RIEMANN REFERENCE exists, and CFDApp output may not be adopted as one.
```

## 5. P14 / P15 reuse

**Reuse:**

```text
P14  assembleScalarConvectionContribution   with the existing compressible mass flux
     assembleScalarDiffusionContribution    k(T) as the diffusivity field
     applyTransientContribution             rhoOld/rhoNew IS the conservative
                                            d(rho phi)/dt form already
     Patankar Su/Sp                         pressure work, viscous work, body force
     applyImplicitUnderRelaxation, fixRow, NonOrthogonalCorrectionOptions
P15  SensibleEnthalpy h(T)                  exact closed form, no quadrature error
     SensibleEnthalpy T(h)                  bounded bisection, ERROR-not-clamp,
                                            published tolerance -- the discipline
                                            P17.5's T(e) should replicate
     TemperatureDependentProperty            cp(T), k(T) with a validity range
     ThermodynamicProperties                 R, cv, gamma, a  (but see B3)
     solver::CFL                             a reusable reporter -- ADVECTIVE only
```

**Do not reuse:**

```text
P14's scalar abstraction for P17.7/P17.8. Four independent reasons: an HLLC flux
computes all five components together and PRODUCES the mass flux that
assembleScalarTransportEquation CONSUMES (the data flow is reversed); rho is an input
in P14 but the first conserved unknown in a density-based solver; wave speeds need the
full left/right state a per-scalar assembler never has; and positivity of a conserved
state is a joint property per-scalar clipping cannot express.

P15's EnergyEquation as a total-energy equation. It transports TEMPERATURE.
The cp*T versus h(T) trap its own Enthalpy.hpp documents recurs one level up in
P17.5: conduction is driven by grad(T), not grad(e_t).
```

## 6. Gate state

`gates/P17_GATES.md` records all ten, each with purpose, required case, measured
quantity, reference, acceptance condition, evidence artifact, failure behaviour and a
non-vacuity requirement. **Definitions only — none has been executed.**

```text
G1  baseline preservation           DEFINED
G2  thermodynamic closure           DEFINED
G3  variable-density conservation   DEFINED
G4  total-energy conservation       DEFINED
G5  low-Mach limiting behaviour     DEFINED
G6  Riemann/HLLC flux correctness   DEFINED
G7  shock validation                DEFINED
G8  CPU/GPU numerical equivalence   DEFINED -- and recorded VACUOUS today, DEFERRED
G9  deterministic repeatability     DEFINED
G10 full regression                 DEFINED
```

Thresholds appear only where derivable now or inherited verbatim from an already
validated frozen gate. **G4, G6(f), G7 and G8 carry
`TOLERANCE REQUIRES JUSTIFICATION`** with the derivation each must come from, because
they depend on a mesh family and a scheme order that do not exist yet. Inventing them
now would be fitting a threshold to an unwritten implementation.

## 7. Repository impact

```text
files added        results/p17/** evidence only -- 8 documents, 2 verdicts, 4 tools,
                   raw logs, 2 preserved invalid runs
files modified     TODO.md (P17.0 checkboxes only)
source changed     NONE
tests changed      NONE
thresholds changed NONE
failure evidence
  removed          NONE -- two invalid harness runs preserved with WHY_INVALID.md
ROADMAP.md         NOT TOUCHED -- under concurrent edit elsewhere (CLAUDE.md §2)
```

```text
tracked modified                   167  ==  167 at freeze
untracked                          107  ==  107 at freeze
tracked engineering files changed   116  ==  116 at freeze
newest tracked source file      2026-09-26T03:44:26Z, i.e. BEFORE the 04:03:16Z freeze
P17_CODE_FINGERPRINT            1e307fcd…  unmoved: freeze -> post-regression -> end
SOURCE_FINGERPRINT_V2           fbf78665…  unmoved since the freeze
```

### Files with an mtime newer than the freeze — all solver OUTPUT, none source

```text
tests/data/cases/{does_not_converge, mesh_quality_warning_cli,
                  valid_cavity_cli_smoke}/results/{fields.csv, metadata.json,
                                                   residuals.csv, solution.vtk}
cases/heated_cavity/results/{fields.csv, metadata.json, residuals.csv, solution.vtk}
cli_log_level_valid_cavity_cli_smoke/results/{...}

source / header / test-source / CMake files with a newer mtime:   0
```

This is the known behaviour `CLAUDE.md` §7 documents — a full `ctest` run rewrites
case output. The P13 V2 methodology anticipates it too, hashing two such directories
separately so that a fingerprint move is attributable in one step.

**These are mtime-only changes, and that is proved rather than assumed.**
`P17_CODE_FINGERPRINT` includes `cases/**` and `tests/data/cases/**`, and it is
byte-identical before and after the regression. If any of those files' *contents* had
changed, the hash would have moved. It did not — the deterministic solver rewrote
identical bytes. An incidental determinism signal, recorded because it is free
evidence.

```text
no unrelated source change          confirmed -- 0 source files touched
no formatting sweep                 confirmed -- no .clang-format run, 0 source touched
no binary files added               confirmed -- 0 matches for *.o/.a/.so/.exe/.dll/.pdb
no test threshold changed           confirmed -- 0 test sources touched
no failure evidence removed         confirmed -- 2 invalid harness runs PRESERVED with
                                    their own WHY_INVALID.md
```

## 8. Recommended next step — exactly one

**P17.1 — variable-density verification.** Freeze its acceptance gate and dry-run every
criterion against the unchanged tree before acting on it.

Why this one: it is first in the authorized working order; the code already exists so
it is verification rather than implementation; and it is the item whose gate most
constrains the rest, because P17.3 (continuity), P17.5 (energy) and P17.8 (the
density-based solver) all consume whatever density contract P17.1 pins down.

The specific hazard to design against: **`coupled: false` is the default mode, and in
it density is a post-hoc reinterpretation that does not influence the flow at all.** A
P17.1 criterion run only in that mode would pass while measuring nothing about
variable-density coupling. The gate must exercise `coupled: true`, and must include a
control that fails in the post-hoc mode.

Genuine gaps P17.1 should close, beyond verification: explicit density bounds, and an
invalid-state policy beyond the EOS's own positivity throw.

**Not started.** Implementation authorization for P17.1 has not been given.
