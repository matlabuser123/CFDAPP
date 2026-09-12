# CFDApp — TODO

**Released:** v0.2.0 (commit `1e960c7`, tag `v0.2.0`) — see `results/release/v0.2.0/`.
**Evidence/workflow rules:** see `CLAUDE.md`. `[x]` = implemented **and**
verified with real evidence; `[ ]` = not yet.

---

## Current

None — P12-COMP-002 (below) just closed. Awaiting next scope decision
before starting any further P12 direction.

## Next

1. P11-GUI-005 manual GUI verification (see Blocked/Manual below) — the
   one remaining open item from P10/P11 closeout.
2. No further P12 direction (P12-NUM/TURB/SPECIES/MULTI/MESH, or
   compressible energy/higher-Mach capability) without an explicit new
   scope decision.

## Blocked / Manual

- **P11-GUI-005 — case-creation-from-scratch GUI acceptance.** Backend
  logic is verified (`CaseEditingTest.FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli`).
  Still open: one human-executed pass through the GUI itself (New → Save
  As → close → reopen → round-trip → Validate → Run → inspect results),
  recorded as a dated addendum to `results/release/p8-hardening/gui_acceptance.md`,
  plus confirming whether template selection exists. This has been claimed
  "done" twice without an actual filled evidence record (commit SHA +
  PASS/FAIL per step); it stays `[ ]` until that evidence is provided.
- Native Windows + CUDA is untested (only WSL2/Linux+CUDA verified) —
  needs that specific hardware/OS combination to close.

## Recently Completed

- **P12-COMP-002 — coupled compressible pressure-velocity solver.** `[x]`
  A dedicated `CompressibleSIMPLE` path (new
  `assembleRelaxedCompressibleMomentumComponent`/
  `assembleCompressiblePressureCorrection` modules, reusing existing
  `CompressibleMomentum`/`IdealGasEOS::dDensityDPressure`), density
  genuinely iterated via the EOS each outer iteration, gated behind
  `physics.json`'s `compressible.coupled` (default `false`, existing
  post-hoc behavior byte-identical when absent — confirmed by the
  existing low-Mach/production-case suites passing unchanged). Reduces
  to plain incompressible `SIMPLE` in the low-Mach limit (regression
  test). Independent physical validation against Arkilic et al. (1997)'s
  isothermal compressible-channel analytical solution: L2/Linf pressure
  error 1.28%/2.55% (`cases/compressible_channel_coupled`). Root cause of
  an initial convergence failure (unpreconditioned BiCGSTAB breakdown on
  the case's own symmetric pressure-correction matrix) found and fixed
  via a pure case-configuration change (CG instead of BiCGSTAB for that
  case's own pressure solve) — no shared solver code was modified to fix
  it. Fresh evidence: `CFDCompressibleSimpleTests` 19/19,
  `CFDCompressibleTests` 50/50, `CFDLowMachRegressionTests` 7/7,
  `CFDIoTests` 200/200 (up from 194/194), new
  `CompressibleCoupledProductionCaseTest` 6/6, full regression **1344/1344**
  (up from 1313/1313, zero regressions), `ctest -j32` clean. Evidence:
  `results/p12-comp-002/summary.md`.
- **P12-COMP-001 — EOS-based boundary-density model.** `[x]` Verified
  commit `2dae0d78d14cc45475845d3476131eb48ee5f342`. `CFDCompressibleTests`
  50/50, `CFDLowMachRegressionTests` 7/7, `CompressibleProductionCaseTest`
  8/8, `CFDIoTests` 194/194, full regression 1313/1313. Evidence:
  `results/p12-comp-001/summary.md`.
- **P10-APP-004 — physics compatibility matrix.** `[x]` One authoritative
  `validatePhysicsCompatibility` (`src/io/case/PhysicsConfigParser.cpp`),
  10/10 new tests, full regression 1300/1300. Evidence:
  `results/p10-app-004/summary.md`.
- **Combined-physics example case** (`cases/heated_species_diffusion`,
  thermal+species). `[x]` 5/5 tests, full regression 1305/1305.
- **Case-format documentation** (`docs/user_guide/case_format.md`): every
  physics block, per-patch BC requirements, and the compatibility matrix
  documented with real command-verified error messages. `[x]`
- **P10/P11 reconciliation.** Species/multiphase/compressible production
  integration and GUI case-authoring editors were already implemented and
  shipped in v0.2.0 (pre-existing roadmap/doc bug had them unchecked) —
  see `ROADMAP.md`'s P10/P11 sections for the corrected record.
- **v0.2.0 release.** `[x]` Tagged/published, CI green on the release
  commit, GUI acceptance 17/17, packaged smoke tests PASS, checksums
  verified. Evidence: `results/release/v0.2.0/`.
