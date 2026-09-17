# P12-DIFF-002-INV-002 — investigation plan (sub-class F production impact)

Investigation only. No production numerical code, test, threshold, benchmark reference, golden
output, case or acceptance gate was modified. No acceptance gate is frozen — this is investigation,
not acceptance.

## Question

A4's clean-build inventory (1913 tests, 1877 passed, 36 failed, 45 disabled) put four failures in
sub-class F, whose references are **not** the superseded two-point wall discretization:

```text
SpeciesConservationTest.OpenChannelWithVolumetricSourceBalancesNetOutflowAgainstSource  1.587336e-3  (limit 1e-3)
LowMachRegressionTest.GlobalMassImbalanceIsSmall                                        1.034329e-4  (limit 1e-4)
NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3                v_max error 0.130255     (limit 0.12)
NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3        0.130255     (limit 0.12)
```

Are these production defects, or legitimate discretization changes whose validation formulation is
now obsolete? Neither was assumed.

## Method

| step | what | instrument |
| --- | --- | --- |
| INV-F0 | freeze state: production files, library, the four tests, their case inputs | `tools/freeze_state.sh` → `logs/00` |
| INV-F8 | **conservation identity audit** (done first — it is the one check that could directly prove a defect): constant-field exactness and row-sum telescoping against an independently summed boundary flux | `tools/invf_conservation.cpp` → `logs/01` |
| INV-F2 | mandatory pre-DIFF-002 baseline: an **isolated** copy of the tree with only the DIFF-002 reconstruction block removed, configured and built outside the repo. The authoritative tree is never written to (verified by hashing before/after) | `tools/build_baseline.sh` → `logs/02` |
| INV-F1/F3 | species: reproduce (3× for determinism), then compute the same global balance three independent ways, then refine 30×2 → 240×16 | `tools/invf_species.cpp` → `logs/03` |
| INV-F2/F9 | all four tests run in both builds | `tools/compare_builds.sh` → `logs/04` |
| INV-F5 | natural convection: the full metric set (u_max and its location, v_max and its location, Nusselt, θ range, mass and heat imbalance, iterations) from each build's own `validation.json` | `tools/natconv_compare.sh` → `logs/05` |
| INV-F6 | natural-convection refinement 10×10 / 15×15 / 20×20 in both builds against the same independent benchmark | `tools/natconv_refine.sh` → `logs/06` |
| INV-F4/F9 | low-Mach: reproduce, sweep solver stopping tolerances, refine, in both builds | `tools/invf_lowmach.cpp` → `logs/07`, `logs/08` |
| INV-F10 | classify each failure independently | `summary.md` |

## Discipline

- The baseline is an isolated build, not a revert: `rsync` of the source into `$HOME/invf_baseline`,
  one block removed from the **copy**, built there. Each build is run from **its own** tree root so
  neither overwrites the other's generated output.
- Every probe reports the library hash it linked against.
- Determinism is established before any number is interpreted.
- Where a transcription of a test's metric does not reproduce that test's exact value, this is
  disclosed rather than glossed over, and conclusions are restricted accordingly.
