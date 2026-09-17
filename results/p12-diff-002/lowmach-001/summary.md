# P12-DIFF-002-LOWMACH-001 — result

**Verdict: PASS.** All three criteria of the frozen gate ([acceptance_gate.md](acceptance_gate.md),
hash in [logs/00_freeze.log](logs/00_freeze.log)) hold. No production file changed. This record was
written after the authoritative runs.

| criterion | evidence | result |
|---|---|---|
| **L1**: only the test file changes, and it equals the candidate | The repository test equals `data/candidate/` (`e69d3a62…`) as frozen. FORMAT-001 then reformatted it to `593165a1…`, layout only: `../format-001/logs/02_token_proof.log` shows identical code tokens, includes and comment words. `src/` + `include/` are unchanged (tree `42c3d9df…`, `../w10/logs/01_integrity_format.log`) | PASS |
| **L2**: `CFDLowMachRegressionTests` 7/7 from a fresh authoritative build | **7/7** in each W10 clean-first build: Release (`../w10/logs/02_release.log`), Debug + GUI (`03`) and ASan + UBSan (`04`, 0 diagnostics) | PASS |
| **L3**: non-vacuity | Pre-freeze dry-run, recorded in the gate §4. The outflow-density mutant is rejected (9.549e-4 > 1.701e-4). The uniform-field defect is rejected by the Mach check. The original test reproduces the recorded failure (1.0343290470e-4 > 1e-4). The first, vacuous mutant is preserved in `logs/dry_mutant_VACUOUS_first_draft.log` | PASS |

Measured in the W10-era Release runs (the verbose output of the GRAD-002 A2 isolated suites): global
imbalance **4.509e-5** against the derived bound **1.701e-4**, and density variation 1.108e-4.
