# P12-DIFF-002 Amendment A5 — M-A reclassification and validation migration — report

**Gate:** `results/p12-diff-002/validation-migration/acceptance_gate_A5.md`, sha256
`fbc927a22cf474b97d7a1cd3aa69f4646ca9c1a7691299fd9fd2f7fca66f40cb`, frozen before any authoritative
test was changed (`a5/logs/00_A5_freeze.log`).

```text
A5-1  T11 failure preserved                PASS   gate, report and logs untouched
A5-2  production freeze                    PASS   0 of 23 production files changed
A5-3  all 18 M-A entries re-audited        PASS   1 KEEP, 17 MIGRATE, 0 DEFECT, 0 UNCERTAIN
A5-4  independent derivation per MIGRATE    PASS   derived == measured, everywhere
A5-5  non-vacuity per amended test         PASS   17/17 fail on the historical operator; M-B by mutation
A5-6  inventory formally corrected         PASS   gate frozen and hashed before any test edit
A5-7  only MIGRATE entries modified        PASS   KEEP file byte-identical
A5-8  fresh focused verification           PASS   22 tests individually + full suites
A5-9  T11 re-run exactly as frozen         PASS   467 run, 464 pass, 3 fail -- all 3 are U-H
A5-10 migration resumed (M-B)              PASS   M-E deferred: values are not derivable
```

**Verdict: the ThermalInterface fix is accepted at T0–T12, and the validation migration has
resumed.** The initial `BLOCKED AT T11` result is preserved unchanged as historical evidence.

---

## 1. A5-1 — the T11 failure is preserved

`a5/T11_preserved_failure.md`. Nothing in `thermal-interface-fix/` was rewritten: its gate
(`9944d066…`), its report and logs `00`–`13` are byte-identical, re-verified in
`a5/logs/07_scope_and_coverage.log`. The record states, and the evidence still shows:

```text
T11 correctly prevented automatic closeout;
0 new failures were introduced by the ThermalInterface fix;
the remaining failure set contains previously identified migration/policy items;
one frozen M-A classification was subsequently proven incorrect.
```

## 2. A5-2 — production frozen, and unchanged

`a5/production_freeze_A5.txt`. The three files the ThermalInterface fix changed:

```text
7d58766dfde9ba7fc28971d8a79172fc59e8268865760efdc8b9a4a46cf1dbe1  src/thermal/ThermalInterface.cpp
fbd47a072fa221f071082ac17a0d40dc8f547909b0f874af007b199a6dba953a  src/thermal/EnergyEquation.cpp
0393ac602e4f715f51ace0aac05693a430361e552e8a01ae815e353b024bafbd  include/cfd/thermal/EnergyEquation.hpp
212141137733bffa7429d09067fefd20c9e760618d9aa0569d7ba990f7b7fa16  build/release/src/libcfdcore.a
```

A5 changed **no production file**: all 23 in the frozen set are byte-identical at the end of A5
(`a5/logs/07_scope_and_coverage.log`, `logs/17_final_scope.log`). A5 is validation work only.

## 3. A5-3 — the re-audit, and a count correction

The authorization refers to "all 12 original M-A entries"; the frozen inventory lists **18**. Twelve
sit in the two suites T11 covers (11 failing + the control); the other six (3 momentum, 3 species)
live in `CFDPhysicsTests` and `CFDSpeciesTests`, which T11 does not run — which is why they were not
visible in the T11 report. **All 18 were audited**, and all 17 failing ones were in scope, since A5-9
requires no unresolved M-A failure.

Per-entry evidence: `a5/logs/02_MA_assertion_detail.log` carries every failing assertion verbatim
with its expected and measured values; `a5/logs/03_independent_derivation.log` carries the
derivation. The classification table is §2 of the gate. Summary:

| classification | count | entries |
| --- | --- | --- |
| **KEEP** | 1 | `RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly` |
| **MIGRATE** | 17 | 11 in class M-A/1 (obsolete wall constant), 6 in class M-A/2 (contaminated instrument) |
| **DEFECT** | 0 | every measured value is reproduced exactly by the independent derivation |
| **UNCERTAIN** | 0 | — |

### The KEEP control

Entry 8 was listed under M-A as a hand-derived constant. It encoded no constant: it asserted that
equal conductivity must reproduce the single-material path exactly — a genuine production invariant —
and it **passes untouched** now that the defect is fixed. `test_thermal_region.cpp` is byte-identical
to its pre-A5 hash `0f74dd66…`. Migrating it would have rewritten a correct defect detector to bless
the defect. That is why the inventory could not be treated as authoritative.

### The two migration classes, and why the distinction matters

**M-A/1 — obsolete wall constant** (entries 1, 2, 4, 5, 7, 9, 10, 11, 12, 16, 18). The expected
number literally encoded `Γ|S|/d`. Amended to the independently derived three-point value, with the
derivation carried in the comment and the identity `cP − cF = cB` asserted at run time.

**M-A/2 — contaminated instrument** (entries 3, 6, 13, 14, 15, 17). These assert that an internal
face contributes equally and oppositely — a property that is **still exactly true**. What broke is
the instrument: the test reads two matrix entries and one of them now also carries the **one-sided
far-cell coupling** DIFF-002 introduces by design (documented in `architecture.md` §2, deliberately
asserted by `test_boundary_reconstruction.cpp`'s `EXPECT_NE`, and the reason these systems use
BiCGSTAB and never CG). The derivation confirms the interpolated internal coefficient is unchanged in
every one of these cases — 4.0, 51.5, 0.011, 0.00865.

So the obsolete expectation here is specifically `A(P,N) == A(N,P)` at entry level. Each amendment
therefore **keeps testing the property**, on entries the far-cell term cannot reach:

* the entry that is still a pure internal coupling is asserted to equal `−cInt` exactly;
* the contaminated entry is asserted to equal `−(cInt + cF)` with `cF` independently derived;
* the difference of the two is asserted to be exactly the far-cell term — a *stronger* statement than
  the old `a01 == a10`, because it pins what broke symmetry rather than merely tolerating it;
* and the equal/opposite property is additionally tested where nothing can contaminate it — between
  two interior cells of a 5×5 mesh (neither has a boundary face), and over **every** internal face at
  once with gradient-type boundaries, where no wall is reconstructed and no far-cell entry exists.

## 4. A5-4 — independent derivation

`a5/tools/derive_expected.py` is a from-scratch reference implementation **in Python**: it links
nothing and contains no CFDApp code, so its values cannot be production's output fed back. Everything
is computed analytically from the mesh **generator arguments** plus the closed-form stencil

```text
cP = Γ|S| h2/(h1(h2−h1))   cF = Γ|S| h1/(h2(h2−h1))   cB = Γ|S|(1/h1 + 1/h2)
uniform mesh (h1 = h/2, h2 = 3h/2):  cP = 3Γ|S|/h   cF = Γ|S|/(3h)   cB = 8Γ|S|/(3h)
historical two-point:                cP = cB = 2Γ|S|/h              cF = 0
```

with `cP − cF = cB` asserted for every case, and the historical value printed beside every new one.
Reconstruction applies only where the wall's own axis has ≥ 2 cells — an integer test on the
generator arguments, no floating-point predicate.

**Derived == measured, in every case.** Examples: the two-cell block gives diagonal 24,
off-diagonal −4, rhs 200/280; `TwoCellsInX` gives 24, −16/3, `21.166666666666664`; `TwoByTwoByTwo`
gives 12, −4/3, 8.625; the variable-property blocks give A(1,0) = −5.666666666666667 and
A(0,1) = −5.0 for k = {3,5}, and −0.015 / −0.014333333333333333 for mu = {0.01,0.012}. Had any value
disagreed, that entry would have been **DEFECT** and A5 would have stopped.

## 5. A5-5 — non-vacuity

**N2, frozen historical library** (`a5/logs/05_N2_historical_control.log`). An isolated copy of the
tree with only the DIFF-002 reconstruction block removed from `boundaryFaceDiffusionTerms`
(historical library `90473bfb622c63e5`; the authoritative tree never written to — its
`NonOrthogonalDiffusion.cpp` verified still at `20a02b16499fa7b0`). All **17** amended tests, compiled
unchanged against it, **FAIL**. No amendment passes both operators.

**A5-f, the natural controls.** `SparseAssembly3DTest.OneCell` — a 1×1×1 mesh whose six walls are all
one-cell-thick — **passes on the historical library too**, because its two-point values are correct
under both operators. That is the required outcome, and it is what proves the amended values were
derived per-face from the stencil rather than applied as a blanket factor. Entry 10 carries the same
control internally: its `FixedGradient` outlet row (A(1,1) = 15, A(1,0) = −7, rhs = 10.5) is
**unchanged**, and the derivation says so before the test is run.

**The M-B migration needs a different control, and this is stated rather than glossed.** The M-A
amendments pin the DIFF-002 coefficients, so they are operator-specific and must fail on the
historical operator. The M-B amendment does the opposite: it replaces a *hard-coded* operator with
production's actual one, so it is deliberately operator-agnostic and would — correctly — pass on
either. Requiring it to fail on the historical library would be requiring it to re-hard-code an
operator, which is the defect being removed. Its control is therefore **mutation**
(`a5/logs/12_MB_mutation_control.log`):

| injected error in the diffusive boundary flux | result |
| --- | --- |
| × 1.02 | passes (inside the unchanged 1e-3 bound) |
| × 1.5 | **FAILS** at 1.32e-02 |
| × 2.0 | **FAILS** at 2.63e-02 |
| × 0 (term dropped) | **FAILS** at 2.63e-02 |

The assertion therefore detects any diffusive-flux error above roughly 4 %, and the two-point/
three-point mismatch it was migrated for was ≈ 6 % (1.587e-03 imbalance against the 1e-3 bound;
3.48e-08 after migration). Its 1e-3 threshold is unchanged. Measured honestly: this balance is
dominated by convection, so it has no power against a *uniform* scaling of the convective flux — that
is a pre-existing property of the statement, not something the migration introduced, and the
statement and threshold are exactly as they were.

## 6. A5-7 — what was modified

```text
tests/unit/discretization/test_boundary_reconstruction.cpp   entry 1
tests/unit/thermal/test_energy_equation.cpp                  entries 2, 3, 4, 5
tests/unit/thermal/test_energy_equation_variable_properties.cpp  entries 6, 7
tests/unit/thermal/test_sparse_assembly3d.cpp                entries 9, 10, 11, 12
tests/unit/physics/test_momentum_diffusion.cpp               entry 13
tests/unit/physics/test_momentum_variable_viscosity.cpp      entries 14, 15
tests/unit/species/test_species_equation.cpp                 entries 16, 17, 18
tests/integration/species/test_species_conservation.cpp      M-B (A5-10)
tests/unit/thermal/test_thermal_region.cpp                   UNCHANGED -- entry 8 (KEEP)
```

No threshold weakened, no test disabled (`grep` for `DISABLED_`/`GTEST_SKIP`: none), no assertion
deleted, and coverage strictly increased (`a5/logs/08_coverage_before_after.log`, with each
pre-migration file verified by hash against the pre-A5 freeze):

| file | TESTs before → after | assertions before → after |
| --- | --- | --- |
| `test_boundary_reconstruction.cpp` | 13 → 13 | 63 → **70** |
| `test_energy_equation.cpp` | 24 → 24 | 57 → **71** |
| `test_energy_equation_variable_properties.cpp` | 13 → 13 | 26 → **32** |
| `test_sparse_assembly3d.cpp` | 5 → 5 | 13 → 13 |
| `test_momentum_diffusion.cpp` | 9 → 9 | 20 → **25** |
| `test_momentum_variable_viscosity.cpp` | 4 → 4 | 13 → **17** |
| `test_species_equation.cpp` | 21 → 21 | 47 → **54** |
| `test_thermal_region.cpp` (KEEP) | 16 → 16 | 30 → 30 |

Three amendments also strengthen the property their test is named for, which the old constants had
never actually checked: entry 5 now compares the `FixedTemperature` and `FixedValue` assemblies
**bitwise** (an operator-independent property that would have survived A2 untouched); entry 7 now
asserts directly that a boundary face's contribution is unchanged when the *neighbour's* conductivity
changes by an order of magnitude; entry 1 keeps its non-vacuity power by asserting the assembled row
differs from the hand-derived two-point system by exactly the reconstruction's coefficient changes.

## 7. A5-8 / A5-9 — verification, and T11 re-run unaltered

`a5/logs/14_A5_8_focused.log`: all 17 migrated tests, the M-B migration, the 4 KEEP/controls
(including `ConjugateConductionPathIsConsistentToo` and
`EqualConductivityMatchesSingleMaterialPathExactly`) and the 7-case conjugate boundary-equivalence
suite, each run individually — **all PASS**, with library and binary hashes recorded.

T11 re-run with the A4 harness, criterion untouched (`a5/logs/06_T11_rerun.log`,
`a5/logs/10_T11_three_way_comparison.log`):

```text
CFDThermalTests           99 run,  99 pass, 0 fail       (was 89/10)
CFDDiscretizationTests   169 run, 168 pass, 1 fail       (was 167/2)
CFDCaseIntegrationTests   63 run,  61 pass, 2 fail, 18 disabled
CFDTurbulenceTests       136 run, 136 pass, 0 fail
TOTAL                    467 run, 464 pass, 3 fail, 18 disabled       (was 453/14)
```

The three remaining failures are **exactly** the three **U-H** tests, which T11 explicitly permits:
`GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` (GRAD-002
history), `MultiBlockProductionCase.CurvedChannelGridConvergence` and
`StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` (W8 history). All 11 M-A failures
resolved; **0 new failures** between the pre-migration state and now. Also green:
`CFDPhysicsTests` 98/98, `CFDSpeciesTests` 37/37, `CFDSpeciesConservationValidationTests` 4/4.

```text
ThermalInterface fix acceptance: PASS T0-T12
```

## 8. A5-10 — migration resumed, and what remains

**M-B done.** The frozen inventory's three M-B entries are now all resolved:
`ConvergedFieldIsConsistentWithItsBoundaryValues` (migrated in validation-migration Step 3),
`ConjugateConductionPathIsConsistentToo` (**not** migrated — it was detecting the production defect
and now passes untouched), and `OpenChannelWithVolumetricSourceBalancesNetOutflowAgainstSource`
(migrated here to `tests/support/BoundaryFluxProbe.hpp`, the same shared production-operator
diagnostic, threshold unchanged).

**There is no M-C** in the frozen inventory (the classes are M-A, M-B, M-E); recorded here rather
than silently absorbed.

**M-E deferred — it is outside what A5 authorizes.** `SIMPLERobustnessTest.DefaultRobustnessPreserves`
`Baseline` and `.DivergenceStatus` both fail (`a5/logs/09_MB_ME_status.log`, `CFDSimpleTests` 123 run,
121 pass, 2 fail). They are **recorded iteration/status baselines**: their expected values are
recorded outputs of the superseded operator, not quantities that can be derived. A5-10 authorizes
only "migration classes whose expected values are mathematically derivable", and A5-4 forbids running
production and adopting its output as the new expectation — which is the only way these two could be
"migrated". They therefore need a separate, explicit decision and are left untouched.

**Untouched, as required:** the 13 **U** tests (verified by hash), the MMS upper-order-band policy
set, W8, the GRAD-002 gradient-order assertion, MESH-007 G6.3, and the MESH-004 ASan defect.

## 9. The strongly-sheared-mesh finding, preserved

Recorded in `thermal-interface-fix/summary.md` §7 and `logs/07_T9_sheared_attribution.log`, unchanged
by A5:

```text
old conjugate apparent error   ~ machine precision (9.095e-15)
new conjugate error            = 1.228e-03
single-material pre-existing   = 1.228e-03      (identical, to the last digit)
```

The old conjugate result is **not** superior for having the smaller scalar error. It came from the
superseded two-point wall treatment, and it violated the corrected production-path consistency that
`ConjugateConductionPathIsConsistentToo` and the T2–T4 bitwise criteria require: the conjugate path
was silently disagreeing with the single-material path it is documented to match, by up to 3.02 K.
The 1.228e-03 is the single-material path's own long-standing value, now shared.

Preserved alongside it, the improvements on that same mesh:

```text
per-cell conservation      1.77e-03  ->  4.8e-14
Neumann global imbalance   2.43e-02  ->  2.2e-14
```

The remaining corrected-wall / uncorrected-internal-face asymmetry on non-orthogonal conjugate meshes
is **outside A5** and was not touched.

## 10. Remaining blockers

1. **M-E** (2 tests) — recorded iteration/status baselines; values not derivable, needs a separate
   decision.
2. **U-D MMS upper-order bands** (4 tests) — the known numerical-**policy** blocker: DIFF-002's W7
   cannot pass while they stand, and they must not be amended without separate authorization.
3. **U-C, U-F, U-H** (9 tests) — superseded Poiseuille references, the natural-convection and
   low-Mach bounds, and the W8/GRAD-002 history; each recorded in the frozen inventory.

No commit. No push. No other P12 phase.
