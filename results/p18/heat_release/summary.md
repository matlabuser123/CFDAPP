# P18.6 — Heat-Release Coupling — summary

```text
STATUS      P18.6 COMPLETE
SCOPE       LIBRARY/API ONLY. Formation enthalpy, reaction enthalpy, the chemical energy source,
            temperature recovery and the explicit chemistry-energy loop, coupled to the P15
            THERMAL equation. No reacting-shock claim. No case-file reachability.
GATE        results/p18/gates/P18_GATES.md
            sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
            re-verified UNCHANGED at open (logs/00) and close (logs/07)
FROZEN      audit.md + energy_accounting.md, hashed at logs/01_freeze.log BEFORE any production
            code was written. NO AMENDMENT WAS NEEDED.
RESULT      G7 PASS. 42 new tests, all passing in all three configurations.
```

---

## 1. G7 — the primary gate

| G7 clause | measured | allowance |
|---|---:|---:|
| closed-reactor total-energy drift | **6.29 eps** (worst over the march 6.82) | 8·eps·N = **128 eps** |
| adiabatic final T vs an **independent** balance | **exactly 2000 K and 3000 K** — bitwise | — |
| `q = -Σ h_f,i ω_i` vs the molar route | **0 eps** | — |
| `Σ_i ω_i` (the invariant it rests on) | **exactly 0** | — |

**Margin ≈ 19×.** Worth stating next to P18.5's G6 margin of 3×: these are different criteria with
differently derived floors, but tightening G7 later would have room where tightening G6 would not.

G7's three named controls: sign flip **2× + inversion**; rate fed to species but not energy
**150 % gap**; `h_f` replaced by the sensible reference **> 1.8e4×**.

## 2. The design gate (§8), resolved from the code

```text
EnergyEquation.hpp:347 states the transported quantity is rho * cp(T,Y) * T, with cp carried at
two time levels. It contains NO formation energy -- nothing in CFDApp did.

=> ARCHITECTURAL POSSIBILITY B: an explicit chemical source is REQUIRED, and double counting is
   STRUCTURALLY IMPOSSIBLE because the source adds a quantity the transported variable does not
   hold.

The source was then DERIVED from dE/dt = 0 with E = rho cp T + rho sum_i Y_i h_f,i, giving
S_E = -sum_i h_f,i omega_i -- which is G7's own formula, verbatim. The gate and the architecture
agree, and the reason they agree is specific rather than lucky.

THE TRAP §45 WARNS ABOUT, QUANTIFIED: using the TOTAL species enthalpy instead would add
-sum_i dh_i(T) omega_i, measured at 9.6% of the real reaction enthalpy at 1500 K -- large enough
to matter, small enough to look like discretization error. Control N5.
```

## 3. The one quantity CFDApp lacked

```text
h_f,i, the standard enthalpy of formation. SensibleEnthalpy::referenceEnthalpy is NOT it -- that is
a sensible OFFSET which cancels out of every physical result, and G7 anticipated the confusion two
phases ago by making it a named control.

ADDED AS A SEPARATE REGISTRY-BOUND TYPE rather than a field on P18.3's SpeciesThermodynamics, so
the confusion is hard to WRITE rather than merely detectable, and P18.3's files stay bitwise
untouched. The cost -- species thermochemistry now split across two types -- is recorded as debt.
```

## 4. Everything measured (`logs/06_probe.log`)

| quantity | measured |
|---|---:|
| species-enthalpy reference error | **0** |
| reaction enthalpy, `Delta H^0` and `Delta H(T)` at 4 temperatures | **0** |
| Kirchhoff `d(Delta H)/dT` vs `Delta Cp` | **0.0026 × eps^(2/3)**, bound 8 |
| energy source, 5 rates | **0** |
| molar vs mass route | **0** |
| source linearity, 5 factors | **0 — all bitwise** |
| `max \|Σ ω_i\|` | **0** |
| one-step balance `Δu = dt·S_E` | **0.51 eps** |
| temperature recovery, constant `cp` and `cp(T)` | **0** |
| non-reacting limit, 10 steps × 32 cells | **0 bitwise differences** |
| determinism, 200 repeats × 64 cells | **0 differences** |

## 5. Four failures, three of them real production defects

All preserved at `logs/02_` and `logs/03_four_failures_PRESERVED.log`.

```text
PRODUCTION DEFECT 1 -- temperature recovery threw for ANY ranged cp. The bracketing started at
    T = 1 K, outside the polynomial's validity range, and TemperatureDependentProperty correctly
    refuses to extrapolate. The recovery only ever worked for a constant cp -- invisible until a
    cp(T) test existed. FIXED: bracket the species' COMMON validity range; refuse, never extrapolate.

PRODUCTION DEFECT 2 -- sum(Y) drifted past P18.1's tolerance. The (rho Y + dt omega)/rho round trip
    is not exact. FIXED: additive in mass-fraction space, Y_i + dt omega_i / rho.

PRODUCTION DEFECT 3 -- the non-reacting limit was NOT bitwise, drifting ~1 ulp per step. Two
    causes: the same round trip, and re-running the bisection when nothing had changed. FIXED: a
    zero increment is an exact no-op and the recovery is skipped when nothing moved -- the same
    principle P15's own transient term states.

TEST ERROR -- an endothermic dt that would consume 1.8x the available species, and (earlier) a
    negative rate fed to an IRREVERSIBLE reaction. IN BOTH CASES THE PRODUCTION GUARD WAS RIGHT AND
    THE TEST WAS WRONG, which is worth recording as clearly as the reverse. dt is now DERIVED from
    the rate rather than guessed.
```

## 6. A measured limitation, not a fix

```text
THE EXPLICIT MARCH ACCUMULATES sum-to-one ERROR LINEARLY at ~0.17 eps/step, crossing P18.1's
4 eps N tolerance at step 72. MEASURED BEFORE ANYTHING WAS CHANGED (tools/drift_probe.cpp), because
the response depends entirely on whether the growth is systematic or random:

    100 steps   17.00 eps      400 steps   69.00 eps
   1600 steps  284.50 eps     6400 steps 1034.50 eps      -- LINEAR, not sqrt

AND THE CAUSE IS VISIBLE: increment_A + increment_B = 0 EXACTLY. The bias is entirely in
Y_i + increment_i rounding against Y ~ 1 differently than against Y ~ 0. Not a stoichiometry,
source or conservation defect -- the irreducible cost of an explicit per-component march.

SECTION 41 FORBIDS THE RENORMALISATION THAT WOULD MASK IT, so P18.1 correctly REFUSES the state.
What was done: bound the march to 50 steps, RAISE THE RATE so it still converts 40% of A and
raises T by 800 K, and PIN the budget in a dedicated test asserting the march is eventually
refused. What was NOT done: renormalise, widen P18.1's tolerance, or quietly shorten the march --
when the count alone came down, the test's OWN non-vacuity guards caught that it had become
meaningless.

G7's ENERGY CRITERION IS UNTOUCHED: the energy drift is EXACTLY ZERO for 1000+ steps.
```

## 7. Regression

| configuration | predicted | measured | FAILED | runtime |
|---|---|---|---:|---:|
| Release+CUDA | 3009 / 2958 | **3009 / 2958** ✓ | **0** | 2417 s |
| CPU-only | 2786 / 2739 | **2786 / 2739** ✓ | **0** | 1510 s |
| Debug+GUI | 3071 / 3016 **or** 3017 | **3071 / 3017** ✓ | **3** | 8206 s |

All three match the prediction declared before the run; Debug+GUI on its second branch.
`DriverSeegmiller.MeshSequence` **passed at 7168.06 s** (99.6 % of the unchanged cap) — a **sixth**
measurement, now 4 pass / 2 timeout, all within 6 %.

**A staleness concern was raised and resolved by measurement**, not by assertion: a comment-only
header edit landed after Release+CUDA had built, so `build/qual-final` was rebuilt and
`libcfdcore.a`'s sha256 compared — **identical**, proving no object code changed. See
`regression.md` §4.1.

## 8. Three instrument defects, all mine, all recorded

```text
(1) THE INTEGRITY PROBE REPORTED ITSELF BLIND -- it grepped for the P15 assembler name in a header
    that never mentioned it. FIXED by making the header name the assembler it feeds, which is
    useful documentation AND makes the control read something real.

(2) A "cp * T SHORTCUT" GREP FIRED ON CORRECT CODE -- it matched rho * cp * T, which IS the
    transported quantity this phase is built on. REMOVED as mis-specified, with the reason
    recorded: a probe that fires on correct code trains a reader to ignore its output.

(3) THE VALIDATION-NOISE MAGNITUDE LOOKED LIKE A FIVE-ORDER L36 VIOLATION (5.609e-02 against
    4.1e-07). The key was a CONTINUITY RESIDUAL of 5.741112e-10, six orders below the solution
    norms in the same file -- an absolute move of 3.2e-11. L36 IS NOT CONTRADICTED. The comparator
    now judges each key against its own file's scale, excluding metadata leaves, and reports
    near-floor keys by ABSOLUTE change. logs/10_validation_noise_MAGNITUDE.log.
```

Every "must be 0" check in `final_integrity.sh` is paired with a positive control that must be
non-zero on the same file.

## 9. Limitations — stated, not deferred

| limitation | why |
|---|---|
| **No case file can reach any P18.6 code** | library/API scope. Per CLAUDE.md §9 this is **not production capability** — the same limit now spans **P18.2–P18.6**, five phases |
| **Thermal path only** | P17's `TotalEnergyState` refuses a temperature-dependent `cp`; lifting it needs a variable-gamma EOS, outside scope, and §46 forbids the reacting-shock claims it would enable. **Not lifted**, as P18.3 also declined |
| explicit, operator-split coupling | no sub-stepping, no iteration, no implicit treatment — and none is claimed (§18) |
| the march is bounded at ~72 steps | §6 above |
| Patankar `Sp` still zero | needs `d(omega)/d(Y)`; carried forward from P18.4 and P18.5 |
| no equilibrium constant | P18.5's limitation, carried forward: an inconsistent forward/reverse pair is not detected |
| §37 EOS closure, §39 constant-pressure reactor | no pressure field in the reactor; §39 is explicitly optional and faking it is forbidden |
| sanitizers NOT RUN | deferred to G11 at P18.9, as in P18.1–P18.5. Recorded as NOT RUN, not as a pass |
| 259 whitespace + 28 clang-format violations | all pre-existing, in files no P18 phase authored. A push blocker in `TODO.md` |

## 10. Changeset

```text
PRODUCTION (new)
  include/cfd/species/HeatRelease.hpp
  src/species/HeatRelease.cpp
PRODUCTION (modified) -- a build-list entry only
  src/CMakeLists.txt
TESTS (new)
  tests/unit/species/test_heat_release.cpp          42 tests
TESTS (modified)
  tests/unit/species/CMakeLists.txt                 one source line
DOCS
  TODO.md, ROADMAP.md, results/p18/heat_release/**

NO PRE-EXISTING PRODUCTION SOURCE WAS MODIFIED, and NO ENERGY EQUATION WAS ADDED -- the W/m^3
source is consumed by the P15 per-cell assembler that has existed since P12-NUM-006.
```

Nothing was committed, pushed, reset, cleaned or stashed. `HEAD` is unchanged at
`3bd32f918656f471e72b5f6ac7cb6c2cbf75a274`.
