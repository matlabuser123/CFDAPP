# P12-DIFF-002 W9 / W9A — result

**Verdicts:**

- **W9 as frozen: FAILED.** The literal "not worse" criterion is tighter than the solve's own
  resolution. That failure stays on record ([logs/02_w9_compare.log](logs/02_w9_compare.log)).
- **W9A: PASS** on the fresh run ([logs/05_02_w9_compare.log](logs/05_02_w9_compare.log),
  "W9A: PASS", exit 0), with one disclosed sensitivity limit on W9A-6.
- **W9A-5 is completed by W10**, which must pass the named reference tests.

This record was written after the run, from the logs listed. The gate is
[acceptance_gate_W9A.md](acceptance_gate_W9A.md), sha256 `dd266d0e…`
([logs/04_w9a_freeze.log](logs/04_w9a_freeze.log)).

| criterion | evidence | result |
|---|---|---|
| **W9A-1**: every case runs in A, C and B, and every solve converged | logs/05_01, logs/05_02: 19 cases × 3 configurations, all status 0 | PASS |
| **W9A-2**: model validity, imbalance ≤ floor in every run | logs/05_02 "model check": 57 of 57 runs within their floor. The tightest is `poiseuille_flow` A at 1.977e-7 against 2.263e-7 (0.87) | PASS |
| **W9A-3**: conservation no worse, `imbalance_B ≤ max(recorded, floor_B)` | logs/05_02: 19 of 19 "W9A PASS". The four literal "WORSE" cases are 3–5 orders below their floors: 1.22e-11 against 3.39e-9; 6.23e-10 against 1.96e-7; 3.97e-11 against 2.26e-9; 2.45e-11 against 4.08e-9 | PASS |
| **W9A-4**: only the DRIFT-001 cases change between C and B | logs/05_02 field table, parsed: max\|B − C\| = 0 for every field of the **17** other cases. Only `poiseuille_distorted` and `curved_channel_multiblock` change | PASS |
| **W9A-5**: changes explained, and the reference tests pass in W10 | explanations in the gate §4. The reference tests (UC-001, W8B, UF-001, MESH-006 A3/G7, the lubrication channel, MMS) passed in W10 Release (1923/1923) and Debug + GUI (1975/1975). The ASan run is recorded in `../w10/` | PASS once W10 passes |
| **W9A-6**: non-vacuity | [logs/06_w9a_mutant.log](logs/06_w9a_mutant.log): a scratch library without the boundary-face flux correction (`883cdf12…`) is **rejected on 2 of the 3 open-channel cases** | see below |

W9A-6 case by case:

| case | outcome | reason |
|---|---|---|
| `channel_transpiration_graded` | rejected | fails W9A-1: MaxIterations, imbalance 5.1e-6 |
| `poiseuille_distorted` | rejected | violates W9A-2: 8.7e-7 against a floor of 2.3e-9 |
| `poiseuille_flow` | **accepted** | 9.8e-8 is below its floor of 2.26e-7 |

**Disclosed limit (W9A-6).** The criterion's wording, "…on the open-channel cases", does not say
whether every case or the set must reject. On `poiseuille_flow` the resolution floor is set by that
case's loose pressure solve, relTol·‖b‖ at the linear-flux stopping point. That floor exceeds the
mutant's effect, so W9A cannot detect this mutant there. W9A's non-vacuity is therefore shown on
the two cases where the floor resolves it. `poiseuille_flow` accuracy is guarded independently by
UC-001's discrete-exact test, which passes in W10.
