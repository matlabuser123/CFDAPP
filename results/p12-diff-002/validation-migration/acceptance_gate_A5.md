# P12-DIFF-002 Amendment A5 — M-A reclassification and validation migration — acceptance gate

Authorized 2026-09-17. Frozen **before any authoritative test is changed**.

Entry state: the ThermalInterface production defect is FIXED; `thermal-interface-fix` T0–T10 and T12
PASS; T11 FAILED because previously-authorized migration tests remain unresolved; 0 new failures.

**Stop rule.** Stop at the first failed criterion. Never weaken a threshold, never disable a test,
never delete an assertion, never reduce coverage. No test may be migrated merely because it fails.

This gate does **not** amend `thermal-interface-fix/acceptance_gate.md` (sha256
`9944d066659d4127cbfdba5246f67b023461738fb9e9f5cd96d0d289258e8870`) and does **not** rewrite or
delete the original frozen inventory in `acceptance_gate.md` §2/§3 (sha256
`833ad5608a265ecc75b8df32e5a37242a68773f04155ba7f8101cf92150ba219`). Both stay as historical record,
as does the `BLOCKED AT T11` verdict (`a5/T11_preserved_failure.md`).

---

## 1. Why the original inventory is not authoritative

`acceptance_gate.md` §2 froze 23 tests as **M** — "each evaluates the superseded two-point wall
operator". That inventory was written **before** the `ThermalInterface.cpp` defect was known, and it
misclassified at least one entry:

`RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly` was listed under
**M-A** (hand-derived constants). It encoded no constant at all: it asserted a genuine production
invariant — equal conductivity must reproduce the single-material path exactly — and it **passes
untouched** now that the defect is fixed (`thermal-interface-fix/logs/11_…`). Migrating it would have
rewritten a correct defect detector to bless the defect.

It is therefore carried through A5 as an explicit **control**, and every other entry is re-audited
from evidence rather than inherited from the list.

### Count correction, recorded rather than silently absorbed

The authorization refers to "all 12 original M-A entries". The frozen inventory actually lists
**18**. Twelve of them (11 failing + the control) live in the two suites `thermal-interface-fix` T11
covers; the remaining six (3 momentum, 3 species) live in `CFDPhysicsTests` and `CFDSpeciesTests`,
which T11 does not run, and were therefore not visible in the T11 report. **All 18 are audited here**
— the superset — and all 17 failing ones are in scope, because A5-9 requires no unresolved M-A
failure. Measured status: `CFDPhysicsTests` 98 run / 95 pass / 3 fail; `CFDSpeciesTests` 37 run /
34 pass / 3 fail (`a5/logs/01_MA_other_suites.log`).

## 2. Classification of all 18 M-A entries

Evidence: `a5/logs/02_MA_assertion_detail.log` (every failing assertion verbatim, with expected and
measured values) and `a5/logs/03_independent_derivation.log` (the independent derivation, §3).

| # | test | file:line | classification |
| --- | --- | --- | --- |
| 1 | `BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne` | `tests/unit/discretization/test_boundary_reconstruction.cpp:273` | **MIGRATE** |
| 2 | `EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients` | `tests/unit/thermal/test_energy_equation.cpp:90` | **MIGRATE** |
| 3 | `EnergyEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric` | `tests/unit/thermal/test_energy_equation.cpp:135` | **MIGRATE** |
| 4 | `EnergyEquationAssemblyTest.CombinedAssemblyMatchesHandDerivedCoefficients` | `tests/unit/thermal/test_energy_equation.cpp:385` | **MIGRATE** |
| 5 | `EnergyEquationThermalBCIntegrationTest.FixedTemperatureGivesIdenticalResultToEquivalentFixedValue` | `tests/unit/thermal/test_energy_equation.cpp:563` | **MIGRATE** |
| 6 | `EnergyEquationVariablePropertiesTest.DiffusionInternalFaceMatchesHandDerivedValue` | `tests/unit/thermal/test_energy_equation_variable_properties.cpp:108` | **MIGRATE** |
| 7 | `EnergyEquationVariablePropertiesTest.DiffusionBoundaryFaceUsesOwnerConductivityDirectly` | `tests/unit/thermal/test_energy_equation_variable_properties.cpp:138` | **MIGRATE** |
| 8 | `RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly` | `tests/unit/thermal/test_thermal_region.cpp:190` | **KEEP** |
| 9 | `SparseAssembly3DTest.TwoCellsInX` | `tests/unit/thermal/test_sparse_assembly3d.cpp:121` | **MIGRATE** |
| 10 | `SparseAssembly3DTest.TwoCellsWithUpwindConvectionAndAZeroGradientOutlet` | `tests/unit/thermal/test_sparse_assembly3d.cpp:140` | **MIGRATE** |
| 11 | `SparseAssembly3DTest.TwoByTwoByOne` | `tests/unit/thermal/test_sparse_assembly3d.cpp:166` | **MIGRATE** |
| 12 | `SparseAssembly3DTest.TwoByTwoByTwo` | `tests/unit/thermal/test_sparse_assembly3d.cpp:183` | **MIGRATE** |
| 13 | `MomentumDiffusionTest.InternalFaceCoefficientsAreSymmetric` | `tests/unit/physics/test_momentum_diffusion.cpp:129` | **MIGRATE** |
| 14 | `MomentumVariableViscosityTest.InternalFaceMatchesHandDerivedLinearMuValue` | `tests/unit/physics/test_momentum_variable_viscosity.cpp:100` | **MIGRATE** |
| 15 | `MomentumVariableViscosityTest.TabulatedMuGivesTheExpectedFaceValue` | `tests/unit/physics/test_momentum_variable_viscosity.cpp:152` | **MIGRATE** |
| 16 | `SpeciesEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients` | `tests/unit/species/test_species_equation.cpp:100` | **MIGRATE** |
| 17 | `SpeciesEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric` | `tests/unit/species/test_species_equation.cpp:137` | **MIGRATE** |
| 18 | `SpeciesEquationAssemblyTest.CombinedAssemblyUsesDensityTimesDiffusivityAsCoefficient` | `tests/unit/species/test_species_equation.cpp:351` | **MIGRATE** |

**No entry is DEFECT** and **no entry is UNCERTAIN**: every failing value is reproduced *exactly* by
the independent derivation of §3, so nothing is unexplained. Had any measured value disagreed with
the derivation, that entry would have been DEFECT and A5 would have stopped.

### The two migration classes

The 17 MIGRATE entries are not all the same kind of obsolescence, and the distinction governs how
each may be amended.

**Class M-A/1 — obsolete wall constant (11 entries: 1, 2, 4, 5, 7, 9, 10, 11, 12, 16, 18).** The
expected number literally encodes `Γ|S|/d`. Amendment: replace it with the independently derived
three-point value, carrying the derivation in the comment.

**Class M-A/2 — contaminated instrument (6 entries: 3, 6, 13, 14, 15, 17).** These assert a
*property* — that an internal face contributes equally and oppositely — and that property is **still
exactly true**. What broke is the instrument: the test reads two matrix entries, and one of them now
also carries the **one-sided far-cell coupling** that DIFF-002 introduces by design (documented in
`architecture.md` §2, and asserted deliberately by `test_boundary_reconstruction.cpp:334`
`EXPECT_NE(entryOf(matrix, 1, 4), entryOf(matrix, 4, 1))`, which is why these systems are solved with
BiCGSTAB and never CG). The derivation confirms the interpolated internal coefficient is unchanged in
every one of these cases (4.0, 51.5, 0.011, 0.00865).

For M-A/2 the obsolete expectation is specifically `A(P,N) == A(N,P)` at entry level. Amendment
**must keep testing the equal/opposite internal-face property**, on entries that are not
contaminated, and must additionally pin the far-cell term that explains the asymmetry. Simply
loosening or deleting the symmetry assertion is forbidden.

## 3. A5-4 — independent derivation (required for every MIGRATE)

`a5/tools/derive_expected.py` is a from-scratch reference implementation in Python. It links nothing,
contains no CFDApp code, and computes every expected value analytically from the mesh **generator
arguments** (nx, ny, nz, Lx, Ly, Lz) plus the closed-form stencil:

```text
cP = Γ|S| h2 / (h1 (h2 − h1))      cF = Γ|S| h1 / (h2 (h2 − h1))      cB = Γ|S| (1/h1 + 1/h2)
uniform mesh, h1 = h/2, h2 = 3h/2:  cP = 3Γ|S|/h    cF = Γ|S|/(3h)    cB = 8Γ|S|/(3h)
historical two-point:               cP = cB = 2Γ|S|/h                 cF = 0
```

It asserts the identity `cP − cF = cB` for every case it emits, and prints the historical value
beside each new one. A wall is reconstructed only where its own axis has ≥ 2 cells — an integer test
on the generator arguments, no floating-point predicate (the Oracle-T rule of
`acceptance_gate_A1.md` §5).

**Every derived value must equal the measured one.** Any mismatch ⇒ DEFECT ⇒ stop.

## 4. A5-5 — non-vacuity, per amended test

Each amended test must **fail** against an implementation still using the historical two-point wall
flux. Two independent controls, both required:

| id | control |
| --- | --- |
| **N1** | **Independent calculation.** The derivation prints the historical value next to every new one; each amended assertion must sit on a row marked `CHANGED`. An assertion whose value is identical under both operators may remain (it still protects its property) but cannot by itself satisfy N1 for that test. |
| **N2** | **Frozen historical library.** A library rebuilt with the DIFF-002 reconstruction block removed from `boundaryFaceDiffusionTerms` (the isolated-baseline method of `investigation-f/tools/build_baseline.sh`, in a scratch tree; the authoritative tree is never reverted). Every amended test, compiled unchanged against it, must **FAIL**. |

An amendment that passes both the historical and the corrected operator is rejected.

Two natural controls already exist and must keep passing unchanged, proving the amended values are
not blanket-applied: `SparseAssembly3DTest.OneCell` (a 1×1×1 mesh — every axis one cell thick, so
every wall legitimately keeps the two-point form) and the fallback rows of entry 10 (cell 1's
`FixedGradient` outlet, whose three values the derivation shows are **unchanged**).

## 5. Criteria

| id | criterion | threshold |
| --- | --- | --- |
| **A5-a** | Production byte-identical to `a5/production_freeze_A5.txt` throughout | by hash; any change ⇒ `A5 BLOCKED — ADDITIONAL PRODUCTION DEFECT FOUND` |
| **A5-b** | Every one of the 18 M-A entries classified with evidence; every MIGRATE independently derived; derived == measured | exact |
| **A5-c** | Only MIGRATE entries modified. Entry 8 (KEEP) byte-identical | by hash |
| **A5-d** | No threshold weakened, no test disabled, no assertion deleted, no coverage reduced. M-A/2 amendments still test the equal/opposite internal-face property on uncontaminated entries | reviewed per test, recorded |
| **A5-e** | N1 and N2 satisfied for **every** amended test | each must FAIL on the historical library |
| **A5-f** | `SparseAssembly3DTest.OneCell` and entry 10's fallback rows still pass unchanged | pass |
| **A5-g** | Focused verification from a forced rebuild: all migrated tests, all KEEP controls, `ConjugateConductionPathIsConsistentToo`, `EqualConductivityMatchesSingleMaterialPathExactly`, the conjugate boundary-equivalence suite, `CFDThermalTests` — with exact counts and binary/library hashes | 0 fail among these |
| **A5-h** | `thermal-interface-fix` T11 re-run **exactly as frozen**, unaltered: no M-A failure remains, and only the explicitly preserved **U**/policy failures do | per T11's own wording |
| **A5-i** | The 13 **U** tests, W8, GRAD-002, MESH-007 G6.3 and the MESH-004 ASan defect untouched | by hash / not run |

## 6. Verdicts

```text
production hash changes                        -> P12-DIFF-002 A5 BLOCKED — ADDITIONAL PRODUCTION DEFECT FOUND
any entry UNCERTAIN                            -> P12-DIFF-002 A5 BLOCKED — M-A CLASSIFICATION UNCERTAIN
any A5 criterion fails                         -> P12-DIFF-002 BLOCKED / FAILED A5 GATE
T11 passes as frozen, migration resumed        -> THERMAL-INTERFACE FIX COMPLETE — VALIDATION MIGRATION RESUMED
next blocker is the U-D MMS policy set         -> P12-DIFF-002 BLOCKED — MMS NUMERICAL POLICY DECISION REQUIRED
```

No commit. No push. No other P12 phase.
