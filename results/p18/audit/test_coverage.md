# P18.0 — species test coverage matrix (§24, §25)

78 TEST macros across 11 files, 92 registered ctest rows. Every test was read by name **and** its
assertion checked against the name, because §24 says not to trust test names alone.

**Verdict: the existing coverage is genuinely strong for what species IS, and it is exactly as
narrow as the capability. The gaps are not test gaps — they are capability gaps with no test to
write yet.**

---

## 1. Coverage matrix

| Dimension | Coverage | Tests | Assessment |
|---|---|---|---|
| model / API | **STRONG** | `SpeciesPropertiesTest` ×6 — stores name/D, rejects empty name, negative D, non-finite D, accepts D=0, two species keep separate identity | validates every constructor rejection |
| discretization | **STRONG** | `SpeciesEquationDiffusionTest` ×5, `…ConvectionTest` ×3, `…SourceTest` ×2, `…AssemblyTest` ×6 | hand-derived two-cell coefficients, internal-face symmetry, constant-field-gives-zero, upwind direction selection, `S*V` exactness |
| boundary conditions | **PARTIAL** | `SpeciesBoundaryConditionTest` ×2 (Dirichlet inflow source, zero-gradient impermeable wall) | both BC types are covered, but **nothing tests the boundary lag** — see §3 |
| conservation | **STRONG** | `SpeciesConservationTest` ×4 — closed zero-flux domain "for any arbitrary field", with and without convection; open channel in=out; source balances net outflow | the "any arbitrary field" framing is the strongest form |
| analytical validation | **STRONG** | `SpeciesDiffusionValidation` ×4 (grids 20/40/80 + exact flux balance), `SpeciesAdvectionDiffusionValidation` ×2 (Pe 0.1 and Pe 5) | grid refinement plus two Péclet regimes against analytical profiles |
| invalid states | **STRONG** | `SpeciesSolverTest` ×6 — mismatched sizes, zero outer budget, starved inner solver, non-finite initial field, max-iterations | every `SpeciesStatus` enumerator has a test that produces it |
| production dispatch | **STRONG** | `SpeciesProductionCaseTest` ×7, `HeatedSpeciesDiffusionProductionCaseTest` ×5 | end-to-end from case directory, incl. missing-directory error and a non-species case still running |
| case schema / parser | **STRONG** | `SpeciesCaseTest` ×14 — duplicate names, unknown keys, species key when disabled, patch missing a declared species, unknown BC type, missing value | unusually thorough negative coverage of the schema |
| CLI | **PARTIAL** | reached through the production-case tests, which drive the runner | no species-specific CLI test |
| GUI | **NONE** | — | no species GUI test; consistent with there being no species GUI surface |
| GPU | **NONE** | — | correct: there is no GPU species path to test |
| determinism | **STRONG** | `RepeatedAssemblyIsDeterministic`, `RepeatedSolveIsDeterministic`, `RepeatedRunsAreDeterministic` ×2, `CartesianSolveIsBitIdentical` | assembly, solve and full-run levels |
| non-orthogonal meshes | **PARTIAL** | `SpeciesNonOrthogonalTest` ×2 | one correction test plus a bit-identical Cartesian guard |
| coupling / independence | **STRONG** | `SpeciesSolverCouplingTest` ×3 — real SIMPLE-solved Poiseuille, passive species does not alter velocity/thermal, two species show no cross-coupling | directly tests the passivity claim |

## 2. Tests that exist but cannot fail their named criterion

I looked for this specifically. **I found none that are vacuous**, and two worth a caveat:

```text
CHECKED AND SOUND:
  ConstantConcentrationGivesZeroContributionEverywhere
      a real detector: a constant field must give exactly zero diffusive flux, which fails if the
      face coefficients are not antisymmetric.
  ZeroDiffusionCoefficientGivesZeroDiffusiveContribution
      D = 0 is a genuine edge case the constructor deliberately permits.
  PassiveSpeciesDoesNotAlterVelocityOrThermalSolution
      would fail if anyone wired species back into the flow -- a real guard on the one-way policy.
  ClosedZeroFluxDomainConservesSpeciesForAnyArbitraryField
      "for any arbitrary field" means it does not depend on the solution being right, only on the
      operator being conservative. Strong.

CAVEATS, recorded rather than presented as failures:
  1. MaxIterationsWhenOuterBudgetTooSmall
     This test ASSERTS the lagged behaviour -- that a small outer budget yields MaxIterations. It is
     correct as written, but it means the test suite currently ENCODES the lag as expected
     behaviour. If a later phase makes the gradient boundary exact, this test will need updating,
     and it must not be read as evidence that the lag is intended. (See boundary_lag.md §5.1 for
     why the intended semantics are the fixed ones.)
  2. GlobalDiffusiveFluxBalanceIsExact / GlobalDiffusiveFluxBalanceIsConserved
     These validate the DIFFUSIVE balance only. There is no test of the full balance including the
     transient term -- because there is no transient term. Not a test gap; a capability gap.
```

## 3. The real gaps — capability, not coverage

```text
NOTHING TESTS THE BOUNDARY LAG. It is the project's own recorded known limitation
(ROADMAP.md:696), it is measurable (335 vs 2 outer iterations), and no test asserts anything about
it in either direction. The thermal fix shipped WITH a regression test that fails on the pre-fix
solver; species has no equivalent. That asymmetry is the single clearest coverage finding of this
audit.

NOTHING TESTS -- because none of it exists:
  transient species transport            no transient term
  sum(Y_i) = 1                           no mixture closure
  0 <= Y <= 1 enforcement                deliberately not enforced; only reported
  species-dependent mixture diffusivity  one constant D per species
  reaction rates / heat release          nothing
  mixture R, cp, gamma, W                nothing
  species restart                        not in the checkpoint payload
  species on GPU                         no path
```

## 4. Negative-control strategy for later phases (§25)

```text
EXISTING METHODOLOGY, and it is explicitly reversible-plant based: P17.8's
results/p17/density_based_solver/tools/fingerprint.sh plants a one-character change in
SIMPLE.cpp, rebuilds, requires the instrument to DETECT it, then restores and verifies the restore
by sha256. P17.11 re-ran that and it detected the plant in 112 lines. So the project DOES use
reversible source controls, and §25 permits designing to that pattern.

NO PLANT WAS PERFORMED IN P18.0 -- §25 permits it only where the methodology already uses them, and
P18.0 has no gate to validate yet. These are DESIGNED, for the phase that owns each gate:

  control                                    must break            phase
  --------------------------------------------------------------------------------------
  species source sign reversed (S -> -S)     G3 conservation       P18.4
  diffusion disabled (D -> 0 while a
    gradient exists)                         G4 transport          P18.2
  boundary evaluated ONE ITERATION LATE
    -- i.e. re-introduce today's lag after
    it is fixed                              G4 boundary          P18.2   <-- the important one:
                                                                            it is the CURRENT
                                                                            behaviour, so the
                                                                            control is free
  massFlux omitted from convection           G4 transport          P18.2
  negative diffusivity accepted              G2 state validity     P18.1
  Y_i outside [0,1] accepted silently        G2 state validity     P18.1
  sum(Y_i) != 1 accepted silently            G2 state validity     P18.1
  Arrhenius exponent sign flipped            G6 reaction rates     P18.5
  heat release sign flipped (exo <-> endo)   G7 energy             P18.6
  reaction rate not fed to the energy eq.    G7 energy             P18.6

  RULE INHERITED FROM P17: each control must break a NAMED criterion, and a control that instead
  crashes, or breaks a different criterion, is recorded and investigated rather than counted.
```
