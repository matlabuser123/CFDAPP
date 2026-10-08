# TD-001 — verification: **STOPPED AT A FROZEN-GATE FAILURE (C4)**

```text
status      BLOCKED -- C4 FAILS. TD-001 is NOT resolved and the register stays OPEN.
gate        GATE.md, sha256 0749efbf1120db1fa6689a5b86be2e5045a20722413e6c2770e4605d5c7c319b
stopped at  C4 (multi-species state validity), measured two ways, both failing
```

Per `CLAUDE.md` §5.3 and the campaign's failure policy: the gate is **not weakened**, the failure is
preserved verbatim, and the item is not marked resolved. No dependent item (TD-002…TD-006) is
started.

---

## 1. What PASSES

Measured on `cases/reacting_plug_flow` through the production CLI
(`./build/final/apps/cli/cfdapp --case cases/reacting_plug_flow`, exit 0, flow converged in 13439
iterations with all residuals < 1e-8).

| # | criterion | measured | verdict |
| --- | --- | --- | --- |
| C1 | case selects reacting species | the case parses and builds its declared mechanism | **PASS** |
| C2 | parser reaches the configuration | 3 species, 1 reaction, orders and Arrhenius triple present | **PASS** |
| C3 | mechanism instantiated | `ReactionMechanism` built by `CaseBuilder` from the file | **PASS** |
| C5 | dispatch through `ProjectRunner` | `grep -c 'setup.reacting'` = **3** (was **0**) | **PASS** |
| C6 | chemistry executes | `max|Y_B - Y_B0|` = **3.97e-01**, `max S_E` = **2.71e+04 W/m^3** | **PASS** |
| C7 | energy coupling executes | `max|T - T0|` = **725.5 K**, T: 1000 -> **1725.5 K**, sign RISES as the exothermic `h_f,B = -2e6` requires | **PASS** |
| C8 | outputs expose species / T / reaction | `mass_fraction_A`, `mass_fraction_B`, `mass_fraction_N2`, `temperature_reacting`, `heat_release` all in `fields.csv` | **PASS** |
| C11 | patch compositions valid | enforced at parse time; a non-unit patch sum is refused | **PASS** (control N4 pending) |
| C12 | no unchecked solve added | every per-species solve checks `SolverResult::converged()`; failure recorded in `anyLinearSolveFailed` | **PASS** |

**The reproducer's three failure modes are all resolved:** V1 and V2 (a reacting case could not even
be read) now read; V3 (the mechanism silently ignored, `species: []`) now produces five reacting
fields and a measured temperature rise.

## 2. What FAILS — C4, and it fails both ways

C4: `max_cell |sum_i Y_i - 1| <= 4*eps*N_species = 2.66e-15`.

Two source treatments were implemented and measured. **Neither passes, and they fail in opposite
directions:**

| treatment | `max|sum Y - 1|` | `Y_A` range | verdict |
| --- | --- | --- | --- |
| per-cell **Patankar split** (consumption implicit) | **3.00e-01** | 0.0028 .. 0.088 — positive | C4 FAIL |
| per-cell **fully explicit** | **4.92e-07** | **-0.297** .. 0.087 — NEGATIVE | C4 FAIL, and physically invalid |
| *(first attempt, cell-averaged source)* | *4.89e-07* | *-0.297 .. 0.087* | *rejected: averaging a per-cell source is a different equation* |

### Why the Patankar split loses conservation

`sum_i omega_i = 0` holds for the SOURCE — P18.4 enforces it. It does **not** hold for the
*effective* source after the split, because the two sides are treated differently: consumption goes
implicit and is self-limiting (A falls only 0.097), while production stays explicit at the rate
evaluated from the **initial** state (B gains 0.397). The split preserves positivity and breaks the
sum.

### Why fully explicit loses positivity — and still fails C4

Explicit treatment keeps `sum_i omega_i = 0` exactly, and every species then shares one matrix, so
the summed equations reproduce the constant: `4.92e-07` rather than `0.300`. But nothing limits
consumption to the reactant available, so `Y_A` reaches **-0.297**.

And `4.92e-07` **still fails C4 by eleven orders**, which is the more important finding.

## 3. C4 IS MIS-DERIVED, and that is my error

`4*eps*N_species` is P18.1's invariant for a `MassFractionState` **constructed from exact data**. I
applied it to a composition produced by an **iterative linear solve**, whose closure cannot be
better than its own convergence tolerance — here `1e-8` relative, giving the observed `~5e-7`
absolute. No iterative solve reaches `2.66e-15` on a transported field, so **C4 as frozen is
unachievable by construction**, independent of the source treatment.

This is the same class of error as the unreachable steadiness tolerance recorded in the G8 findings
(F13): a machine-precision bound demanded of a quantity whose floor is set by something else
entirely. The gate was frozen before the measurement, as required — and freezing it did not make it
correct.

**I am not changing C4.** §5.3 requires preserving the failure, deriving a replacement from first
principles, and requesting authorization for an amendment frozen as a separate file. That is the
decision to be taken, not one to be taken silently.

## 4. The second finding: a steady frozen-source pass cannot be both conservative and positive

This is a design conclusion, measured rather than argued. A single steady solve with the reaction
source frozen at the initial state has no treatment that is simultaneously conservative and
bounded — the table in §2 is the proof, in both directions.

The resolution is not a third source treatment. It is that the production dispatch should be a
**time step**, not a steady pass: one implicit-Euler transport step with no reaction, then one
chemistry step over the same `dt`. That conserves (the chemistry step applies `sum omega_i = 0`
consistently) and stays bounded (the transport operator is an M-matrix with no source at all). It is
the same operator-split structure the G8 flame work arrived at independently, and for the same
reason.

That needs a `time_step` key in the `reacting` block — the precedent being multiphase's
`transportTimeStep`, which exists for exactly this ("the single dt `step()` requires").

## 5. What is NOT yet run, because the item stopped

* C9 (invalid mechanism through the CLI), C10 (chemistry-off bitwise preservation — baseline
  captured at `baseline/c10_prefix_hashes.txt` but not yet compared)
* negative controls N1–N5
* the three regression suites (325 / 32 / 339)

Running them now would be reporting against a gate that has already failed.

## 6. Reproduction

```bash
cmake --build build/final --target cfdapp -j 16
./build/final/apps/cli/cfdapp --case cases/reacting_plug_flow
python3 <measure fields.csv>     # logs/ and the table in section 2
```

Logs: `logs/10_dispatch_build.log`, `logs/11_dispatch_run.log`.
