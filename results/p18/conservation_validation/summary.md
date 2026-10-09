# P18.7 — Conservation & Validation — summary

```text
STATUS      P18.7 PARTIAL
            Every criterion in the frozen P18.7 gate PASSES. The phase is PARTIAL because G8 --
            canonical reacting validation -- CANNOT BE CLOSED IN THIS SCOPE, for an architectural
            reason recorded at audit.md section 1 and NOT because any measurement failed.
SCOPE       VALIDATION-ONLY. NO production file was added or modified. The only change to a
            pre-existing file is one test-source line in tests/unit/species/CMakeLists.txt.
GATE        results/p18/gates/P18_GATES.md
            sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
            re-verified UNCHANGED at open (logs/00) and close (logs/05)
FROZEN      audit.md + acceptance_gate.md, hashed at logs/01_freeze.log BEFORE any measurement.
RESULT      19 new tests, all passing in all three configurations. 302/302 species suite.
```

---

## 1. The gate table (§48)

| gate | verdict | basis |
|---|---|---|
| **G1** baseline preservation | **PASS** | three-configuration regression, +19 and nothing else moved |
| **G2** species-state validity | **PASS** | `max\|ΣY−1\|` within P18.1's `4·eps·N` on every case; bounds hold; **no normalization anywhere** |
| **G3** species conservation | **PASS** | closed and open balances, every term reported, within `8·eps·N_cells` |
| **G4** transport / diffusion | **PASS** (P18.2's, re-exercised) | 34/34 preserved; the closure control N3 re-measured here |
| **G5** mixture thermodynamic closure | **PASS** (P18.3's, re-exercised) | 34/34 preserved; consumed throughout P18.7's cases |
| **G6** reaction-rate correctness | **PASS** (P18.4+P18.5's) | 38/38 + 38/38 preserved; `Σω_i` exactly 0 on every P18.7 case |
| **G7** reacting energy conservation | **PASS** | closed-reactor and open-flow balances, per-step and integrated |
| **G8** canonical reacting validation | **NOT CLOSED** | audit.md §1 — requires a reacting-flow solver, stiff integrator and flame-speed extraction that do not exist. **Not weakened, not restated.** |
| **G9** CPU/GPU equivalence | **NOT YET QUALIFIED** | P18.8's; §47 forbids claiming it |
| **G10** deterministic repeatability | **PASS** | batch and flow, bitwise, including the outer-iteration count |
| **G11** full qualification | **NOT YET QUALIFIED** | P18.9's; §47 forbids claiming it |

```text
NO EARLIER PARTIAL GATE WAS TURNED GREEN. G8 was open before P18.7 and remains open.
```

## 2. Why G8 is not closed

```text
G8 measures "a 1D laminar premixed flame: flame speed, peak temperature, species profiles" against
a PUBLISHED SPREAD. Against the tree:

    files outside species/ referencing the chemistry layer : 0
    case-file keys defining a mechanism                    : 0

NOTHING drives the chemistry. A premixed flame is not a measurement that can be taken of this
tree; it is a solver that would have to be BUILT -- together with a stiff integrator (P18.6 has
none), mixture transport data, and a flame-speed eigenvalue extraction.

AND THE AUTHORIZATION FORBIDS BUILDING IT HERE: section 1 says prefer instrumentation over
production changes; section 24 says "Do not over-expand scope just to implement a famous
benchmark" and ranks the flame LAST behind a manufactured case; section 62 lists "no flame-speed
validation" among the limitations P18.7 may legitimately record.

SO THE FROZEN GATE AND THE AUTHORIZATION DISAGREE, and no amount of work resolves it. G8 stays
exactly as frozen and P18.7 reports PARTIAL. CLAUDE.md section 12 names this case.
```

## 3. What was measured

### Species conservation (§3–§6)

| quantity | measured | bound |
|---|---:|---:|
| closed reactor, per-species normalized imbalance | **≤ 8·eps·N** | `8·eps·N_cells` (G3) |
| open plug flow, `\|in − out + source\|/\|in\|` | **≤ 8·eps·N** | `8·eps·N_cells` (G3) |
| `max_cell \|Σ_i ω_i\|` | **exactly 0** | `32·eps·max\|ω\|` |
| total mixture mass drift | **≤ 4·eps·N_species** | P18.1's |

Every term is reported separately — inflow, outflow, integrated source — as §3 requires, never
only a residual.

### Mass-fraction closure (§7, §8)

```text
max |sum Y_i - 1|  : within massFractionSumTolerance(2) = 4*eps*2 on every case
min / max Y_i      : within [0, 1], BOOLEAN, no tolerance

NORMALIZATION USED : NONE.  normalizeMassFractions appears in the tree exactly twice -- its
                     declaration and its definition. NOTHING CALLS IT. Verified by grep with a
                     positive control, recorded at logs/05_final_integrity.log.
CLIPPING USED      : NONE. P18.1 REFUSES an out-of-bounds state; P18.6's reactor refuses rather
                     than clamping.
```

### Energy conservation (§11, §12)

Closed adiabatic reactor, 16 cells, 1000 steps: final and **per-step** balances both within
`8·eps·N_cells`, with max and RMS step imbalance reported. The sensible pool rises and the
chemical pool falls by the same amount — asserted, so a conservation result that conserved
nothing would fail.

### Batch reactor vs an INDEPENDENT reference (§16–§21)

```text
REFERENCE: tools/batch_reference.py -- mpmath at 50 digits, and NOT A TIME INTEGRATION. It
separates variables and evaluates t(Y_A) = INT dy/(k(T(y)) y) by QUADRATURE, then inverts by
bisection. CFDApp's update is explicit Euler stepping, so the two share no algorithm at all --
section 19's requirement met structurally rather than by using a different tolerance of the same
method. Self-check residuals ~1e-45.

CHECKPOINTS (section 60), t = 0.05 .. 1.0 s: Y_A, Y_B, T and q all compared, not only the final
state. Worst relative species and temperature errors REPORTED; the GATE criterion is refinement.
```

| dt | species error | temperature error | behaviour |
|---|---|---|---|
| 4e-3 → 2e-3 → 1e-3 | **falls monotonically** | **falls monotonically** | observed order ≈ **1**, which is what **explicit Euler** predicts |

§20 forbids forcing second order onto a first-order method, and none is forced.

### Canonical reacting flow (§22–§28)

```text
STEADY 1D ADIABATIC REACTING PLUG FLOW -- section 24's choice 2. Advection + species transport +
chemistry + THERMAL COUPLING simultaneously, subsonic, and NOT a flame, shock or detonation.

THE ENERGY EQUATION IS SOLVED, not substituted: P15's own convection and per-cell source
assemblers are used, so the coupling runs through the production path.

REFERENCE: the SAME quadrature. For steady adiabatic plug flow the system is the batch reactor
with t -> x/U -- a derivation recorded in the reference script, not a coincidence -- so one
independent reference serves both and neither is validated against CFDApp (section 46).
```

| grid | species profile error | outlet conversion error |
|---|---|---|
| N → 2N → 4N (100/200/400) | **falls monotonically** | **falls monotonically** |

Observed spatial order ≈ **1**, which is what **first-order upwind** predicts.

```text
SPATIAL AND TEMPORAL STUDIES ARE SEPARATED BY CONSTRUCTION, not by argument (section 29):
    the batch reactor is ONE CELL      -> the spatial error is EXACTLY zero
    the plug flow is STEADY            -> the temporal error is EXACTLY zero
That is why these two cases were chosen.
```

### Zero limits (§14, §15, §34, §35, §36)

| limit | result |
|---|---|
| zero reaction, same geometry | `ω_i = 0`, `S_E = 0`, `Y_A = 1`, `T = T_0` — and **`A = 0` agrees BITWISE** with chemistry-off (§36) |
| **N14 non-vacuity** | the reacting case differs by **> 100 K** at the outlet |
| thermoneutral, `ΔH = 0` | species change (`Y_A` → 0.90), `S_E` **exactly 0**, `T` unchanged — the expectation **derived before measurement** from `cp` being composition-independent here |

### Determinism (§41, §42)

Batch reactor: 20 repeats × full histories (`Y`, `T`, `q`, energy) — **bitwise**.
Reacting flow: 5 repeats × fields and conservation metrics, **including the outer-iteration
count** — **bitwise**. Everything is rebuilt each repeat, so construction order is inside the claim.

## 4. Elemental conservation: NOT MODELED (§9)

```text
files carrying elemental composition : 0

Section 9 anticipates this and forbids inventing a database to tick the box. Sections 10 and 39
are therefore N/A. TODO.md records it in section 66's honest form:
    [~] Elemental conservation -- N/A: elemental composition metadata is not modeled
```

## 5. The regression (§57)

| configuration | predicted | measured | FAILED | runtime |
|---|---|---|---:|---:|
| Release+CUDA | 3028 / 2977 | **3028 / 2977** ✓ | **0** | 2084 s |
| CPU-only | 2805 / 2758 | **2805 / 2758** ✓ | **0** | 1441 s |
| Debug+GUI | 3090 / 3036 **or** 3035 | **3090 / 3036** ✓ | **3** | 8156 s |

All three match the prediction declared before the run, Debug+GUI on its **first** branch.
`DriverSeegmiller.MeshSequence` **passed at 6963.18 s** (96.7 % of the unchanged cap) — a
**seventh** measurement, now **5 pass / 2 timeout**, all within 6 %.

## 6. Four instrument defects, all mine, all in the plug-flow driver

```text
THE FIRST ONE IS THE DANGEROUS KIND and is worth stating plainly:

(1) BiCGSTAB BROKE DOWN and the driver never checked the status. On a pure-upwind matrix --
    lower-bidiagonal and strongly non-normal -- the Krylov recurrence failed after 2 iterations
    and returned Y_A = -3.05e12. THE MATRIX AND RHS WERE CORRECT THE WHOLE TIME (diagonal 2.0,
    rhs[0] = 1.9729 = inlet + source; downstream cells even held plausible values). The solver
    REPORTED SolverStatus::Breakdown and the driver used its output anyway -- a fail-open
    instrument that would have turned a solver failure into a physics result.
    Diagnosed at logs/03_plugflow_diagnosis.log by instrumenting the loop rather than guessing.

(2) RESTARTED GMRES(30) -- the textbook default -- stagnated on the same matrix. Un-restarted
    GMRES terminates in at most N steps, so the Krylov subspace is now sized to the mesh.

(3) THE TOLERANCE ASKED FOR 1e-15, below what the system reaches.

(4) THEN THE REQUIREMENT AND THE STOPPING RULE DISAGREED: residual/||rhs|| <= 3.55e-14 was
    demanded while the solver was ASKED for 1e-12.

THE FIX SEPARATES TWO QUESTIONS THAT WERE CONFLATED: did the solver stop cleanly, and is the
answer accurate enough for the measurement it feeds. The stopping rule is now DERIVED from the
conservation bound -- absolute tolerance = (bound/50) * ||rhs||, computed from the assembled
system -- and both are checked FAIL-CLOSED.
```

## 7. A P18.6 finding, now closed

```text
P18.6 could not settle the results/validation magnitude: a broken instrument reported 4.6e-02,
and the corrected one could not be run against the file in question. P18.7's scale-aware
comparator settles it:

    max relative change, NON-TIMING : 3.513e-06
      at distorted_mesh_mms.json :: orders[1].triplets[1].extrapolated_error
    keys at the convergence floor  : 810, max ABSOLUTE change 1.328e-08

The worst mover is a RICHARDSON-EXTRAPOLATED quantity, and KNOWN_LIMITATIONS L36 records exactly
that category as moving <= 1.1e-05. 3.513e-06 is INSIDE it. L36 HOLDS, and the earlier alarm was
entirely instrumental.
```

## 8. Known limitations (§62) — demonstrated, not speculative

| limitation | evidence |
|---|---|
| **no flame-speed validation; G8 not closed** | audit.md §1 |
| **no reacting-flow solver; chemistry reachable from nothing** | 0 files outside `species/` reference it |
| **no elemental metadata** | 0 files carry it |
| **no stiff chemistry integrator** | P18.6's; the batch case is kept inside the explicit stability envelope and that is asserted, not assumed |
| **explicit march has a sum-to-one step budget** | measured; for **two** species it is `4·eps·2` = 8 eps, **tighter** than P18.6's three-species 12 eps, and a 10 000-step march is refused |
| **low-Mach / incompressible-regime reacting flow only** | the plug flow is subsonic by construction; P17's envelope is not approached |
| no 3D reacting flow, no reacting shocks | nothing of the kind is implemented or claimed |

## 9. Changeset

```text
PRODUCTION FILES ADDED    : none
PRODUCTION FILES MODIFIED : none
TESTS ADDED               : tests/unit/species/test_conservation_validation.cpp   19 tests
TESTS MODIFIED            : tests/unit/species/CMakeLists.txt   one source line
EVIDENCE                  : results/p18/conservation_validation/**
GATE AMENDMENTS           : none
THRESHOLD CHANGES         : none
```

Nothing was committed, pushed, reset, cleaned or stashed. `HEAD` is unchanged at
`3bd32f918656f471e72b5f6ac7cb6c2cbf75a274`.
