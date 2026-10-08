# TD-001 — VERIFICATION UNDER FROZEN AMENDMENT A2 — **ALL CRITERIA PASS**

```text
item        TD-001  Reacting-species production reachability
gate        GATE.md     sha256 0749efbf1120db1fa6689a5b86be2e5045a20722413e6c2770e4605d5c7c319b
amendment   GATE_A1.md  sha256 53aee7afa7b38193fee1d045d43f35720d038fb8b0b6d7851809c0b9969f43b3
amendment   GATE_A2.md  sha256 6b1d97e541b555b94524d9e95b44161646b95c13ebaeacab9da5d540f2da42f2
            (all three recomputed at the start of the authoritative run -- logs/23, section F0)
freeze      logs/22_a2_freeze.log
dry-run     logs/21_a2_dryrun_PRE_FREEZE.log   (BEFORE the freeze, CLAUDE.md section 5.2)
rerun       logs/23_a2_authoritative_rerun.log (AFTER the freeze, reusing NO earlier result)
regression  logs/24_regression.log
verdict     every criterion PASSES; every control FAILS the criterion it guards.
```

`verification.md` (the original C4 failure) and `verification_A1.md` (the C4b failure and N3's
failure to fail C7) are **unchanged and unreclassified**. Their sha256 values are recorded in
`logs/22_a2_freeze.log` and are the same before and after this run.

---

## 1. Criteria, in frozen order

| # | criterion | measured | verdict |
| --- | --- | --- | --- |
| C1 | a case file selects reacting species | `cases/reacting_plug_flow` parses; `G8ReactingCaseTest` **12/12** | **PASS** |
| C2 | the parser reaches the configuration | every species, reaction, explicit order, Arrhenius triple and `time_step` asserted value-for-value | **PASS** |
| C3 | the mechanism is instantiated | `reactionCount` 1, net stoichiometry and `hasExplicitOrders` asserted; G6 mass closure holds on the case-declared mechanism | **PASS** |
| C4a | the CONSTRUCTED state keeps `4*eps*N` | summed in-test over every cell; `<= 3.55e-15` | **PASS** |
| C4b-i | dt SCALING | precondition `rho V/dt / max abs(mdot_f)` = **10.0**; `D(0.1)=5.718198e-08`, `D(0.01)=7.078766e-09`, ratio **8.0780**, `/10 = 0.8078` in `[0.5, 2.0]` | **PASS** |
| C4b-ii | CONTINUITY SCALING | achieved `p` residual `9.998797e-09 -> 9.997238e-11` (**100.0156x**), `D` `5.718198e-08 -> 6.808347e-10` (**83.9880x**) | **PASS** |
| C4b-iii | positivity, tolerance-free | `min_cell min_i Y_i = +1.230741e-03` | **PASS** |
| C4b-iv | the absolute figure, reported | `D = 5.718197704e-08` at `dt = 0.1`, flow `p` residual `9.998797e-09`, `global_mass_imbalance 6.443302e-10`, flow converged in 13 439 iterations | reported |
| C5 | dispatch through the production path | `grep -c 'setup.reacting' src/app/ProjectRunner.cpp` = **3** (was **0**); `cfdapp --case` returns a reacting result | **PASS** |
| C6a | chemistry actually executes | `max S_E = 2.710138e+04 W/m^3 > 0` | **PASS** |
| C6b | the composition change matches prediction | predicted `1.354797e-03` from the case file alone; measured `1.355075e-03`; band `[6.773985e-04, 2.709594e-03]` | **PASS** |
| C7a | the temperature rise matches prediction | predicted `2.709594 K`; measured `2.710194 K`; band `[1.354797, 5.419188]` | **PASS** |
| C7b | the sign is right, and defined | `max S_E > 0` so exothermic; `min_cell (T - T0) = +2.452434 K >= 0` — no cell cooled | **PASS** |
| C8 | outputs expose species / T / reaction quantities | `fields.csv`: `mass_fraction_A/B/N2`, `temperature_reacting`, `heat_release`; `metadata.json` `species` lists A, B, N2, each `Converged` with iterations and final residual | **PASS** |
| C9 | an invalid mechanism fails clearly | 6 malformed variants, **all exit 2**, each naming the offending key/species/reaction, **none** wrote `results/` | **PASS** |
| C10 | a chemistry-off case is BITWISE preserved | 3 cases, 9 artefact hashes, **all identical** to `baseline/c10_prefix_hashes.txt` | **PASS** |
| C11 | patch compositions are valid | a patch summing to 0.9 refused at construction, with the measured sum, before any solve | **PASS** |
| C12 | no unchecked linear solve added | 4 solves added, each `converged()` checked; per-species status/iterations/residual exported (`2.26e-13`, `1.78e-13`, `6.85e-13`, 4/4/2 iterations) | **PASS** |

### 1.1 The two predictions, agreeing to better than 0.1 %

Both were derived from the case file's declared values before the case was ever run, and neither
touches the run's output:

```text
k(T0) = 5.0 * exp(-30000 / (8.314 * 1000))        = 0.13547969 1/s
dY    = k * Y_A,0 * dt                            = 1.354797e-03    measured 1.355075e-03   +0.021 %
dT    = dY * |h_f,B - h_f,A| / cp                 = 2.709594 K      measured 2.710194 K     +0.022 %
```

This is the evidence that the dispatch applies the Arrhenius law the case declared, rather than that
something merely changed.

### 1.2 C4b-ii, stated with its caveat

The tightened run reached `p_res = 9.997238e-11` but reports `converged = false` at its 400 000
iteration cap: it is a **better-converged flow, not a converged one**. `GATE_A2.md` §4.1 keys the
criterion to the residual actually achieved precisely for this reason, and requires the convergence
flag to be reported beside it. It is reported here.

The relationship is close to one-for-one — a 100.0-fold smaller residual gives an 84.0-fold smaller
`D` — which is the quantitative confirmation of `verification_A1.md` §3's mechanism, and the reason
A2 replaced a scaling in the species solver's tolerance with one in the flow's.

## 2. Controls — every one fails what it guards

| id | guards | required | measured | verdict |
| --- | --- | --- | --- | --- |
| N1 | C5, C6, C7, C8 | all fail | `fields.csv` header carries **no** `mass_fraction_*`, no `temperature_reacting`, no `heat_release`; `metadata species: []` | **fails all four** |
| N2 | C6 | fails | `C6a FAIL` (`max abs(S_E) = 0`), `C6b FAIL` (prediction `0`, measured `5.146406e-08`) | **fails** |
| N3 | C7 | fails | `C7a FAIL` (prediction `0`, measured `5.688378e-05`), `C7b FAIL` (thermally neutral yet `dT != 0`) — while **`C6b` still PASSES**, so the control isolates the ENERGY coupling exactly as gate §3 intended | **fails** |
| N4 | C11 | fails to construct | exit 2, message names the patch and the measured sum `0.900000`, no `results/` | **fails to construct** |
| N5 | C4 | refused with the sum | exit 2, message names `reacting.species` and `0.900000`, no `results/` | **refused with the sum** |
| A1-N1 / A2-N1 | C4b-i | fails | `D(0.1) = D(0.01) = 0.30026183572609555` — **identical to 17 digits**, ratio `1.0000`, `/10 = 0.1000` | **fails** |
| A1-N2 | C4b-iii | fails | `min_i Y_i = -0.29742307761030073`, while its sum closure `4.919333e-07` is the best of any variant | **fails** |
| A1-N3 | the original C4 | fails at any tolerance | bound `2.66e-15` against `5.718198e-08` | **fails** |

### 2.1 N3 is the control that did not work before, and now does

Under the frozen C7 (`> 1e-9 K` plus "sign matching `standardReactionEnthalpy`"), N3 **passed** — its
`5.688378e-05 K` of transport noise cleared the threshold by four orders, and with zero reaction
enthalpy there was no sign to match. Gate §5 item 2 was unsatisfied, and gate §3 names N3 as one of
"the two that matter".

Under A2 it fails twice over, and for the right reasons: the predicted rise is `0 K` so any measured
change fails C7a, and a mechanism with `max abs(S_E) = 0` is rejected as a C7 instrument rather than
passed vacuously. Crucially **C6b still passes on N3**, which is what makes it a clean control: it
removes the energy coupling without removing chemistry, and only the energy criteria notice.

### 2.2 A2-N1 is the control that restores C4b's discrimination

The frozen C4b was failed by production *and* by A1-N1, so it separated nothing. Under A2's dt
scaling, production passes at `0.8078` and A1-N1 fails at `0.1000`. The steady variant's composition
equation contains no `dt` at all, so its deviation is exactly dt-independent — `0.30026183572609555`
at both time steps, to every digit — which is why it is the right instrument for this criterion.

### 2.3 Two instrument defects the pre-freeze dry-run caught

Recorded in `GATE_A2.md` §4.1 and both corrected **before** the freeze:

1. The first C6b/C7a probe returned `N/A` when the prediction was zero — which would have excused
   A2-N2 and A2-N3 from the very criteria they exist to break. The band degenerates to `{0}`, so a
   zero prediction now requires a zero measured change.
2. C4b-ii was keyed to the *declared* flow tolerance. The flow does not reach `1e-10` on this case at
   20 000 or at 400 000 iterations, so the criterion was keyed to the residual actually achieved.

Both are the dry-run doing its job, and both make the gate stricter.

## 3. Regression

| suite | gate baseline | measured | note |
| --- | --- | --- | --- |
| `CFDIoTests` | 325 | **328 / 328 pass** | +3, each enumerated in `regression.md`; all 325 baseline tests pass |
| `CFDAppLayerTests` | 32 | **32 / 32 pass** | matches exactly |
| `CFDSpeciesTests` | 339 | **339 / 339 pass** | under the baseline's own `-G8PremixedFlameTest.*` exclusion |
| committed `cases/` | byte-identical | **bitwise identical** | C10's 9 hashes |

`src/species/**` and `include/cfd/species/**` were not modified by TD-001: the 339 count is a
measurement of an untouched library, and the `CFDSpeciesTests` binary hash `faaf7f5e…` is unchanged
across every build in this session.

## 4. Gate §5 — definition of done

| item | state |
| --- | --- |
| 1. every criterion PASSES | **YES** — C1–C12 as amended by A1 and A2 |
| 2. N1–N5 each FAIL what they guard | **YES** — including N3, which did not under the pre-A2 gate |
| 3. the three regression baselines | **YES** — 325 (within 328), 32, 339 |
| 4. `git diff --check` adds no warning beyond the three CRLF files | **YES** — only `cuda/CMakeLists.txt`, `tests/unit/compressible/CMakeLists.txt`, `tests/unit/gpu/CMakeLists.txt` (TD-018) |
| 5. evidence written | **YES** — see `summary.md` for the file list |
| 6. `DEBT_REGISTER.md` OPEN -> RESOLVED, **after** the above | done after this file |

## 5. What is still true, and limits this result

* **One step, not a march.** A second step's chemistry half would need a raw-fields reactor entry
  point; that belongs to **TD-002**. `ProjectRunner.hpp` records why.
* **TD-002's step budget applies** to whatever this ships — the chemistry is P18's explicit
  first-order step.
* **`D = 5.72e-08` is inherited, not eliminated.** It is the flow's residual continuity error, now
  bounded and reported rather than concealed. Driving it lower means converging the flow harder or
  using a divergence-correcting formulation; it is **not** fixed by renormalising, clipping, or
  solving the carrier as `1 - sum`, none of which was done.
* **No flame, no flame speed, no G8 closure** — TD-004 / TD-005 / TD-006. `PremixedFlame.{hpp,cpp}`
  is neither used nor validated here, and `G8PremixedFlameTest` (17 declared tests, most failing by
  its own record, grown from the 13 recorded in `flame_solver_findings.md`) remains TD-004's.
* **No performance claim** is made.
