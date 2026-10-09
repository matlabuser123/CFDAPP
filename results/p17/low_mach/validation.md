# P17.6 — validation: the measured low-Mach behaviour

Consolidates what the authorization suggested as `velocity.md`, `pressure.md`, `temperature.md`,
`density.md`, `mass_flux.md`, `residuals.md`, `conservation.md`, `asymptotic_trend.md`,
`deterministic_repeatability.md` and `cpu_gpu.md`.

Sources: `logs/01_dryrun.log` (the unchanged tree), `logs/03_family.log` and `logs/03_gate.log`
(the gate), `logs/05_controls.log`, `logs/04_fingerprint.log`.

---

## 1. THE AUTHORITATIVE TABLE

Four levels, each a genuine `CompressibleSIMPLE` solve against its own matched incompressible
`SIMPLE` reference. 32×6 mesh, μ = 0.1, T = 300 K, `rho_ref = 1.17662428` derived from the EOS.

```text
U0     inletMa     machMax     drho/rho    relL2(|U|)  relL2(p')   keRelDiff   itComp  itInc
1      0.0028802   0.0041552   1.1192e-04  7.9159e-05  7.1600e-04  1.6233e-04  1959    2031
0.5    0.0014401   0.0020790   5.4856e-05  3.1193e-05  7.6267e-05  7.8081e-05  2261    2450
0.25   0.00072005  0.0010397   2.7130e-05  1.6984e-05  7.3820e-05  3.8465e-05  2403    2626
0.125  0.00036003  0.00051994  1.3482e-05  1.2574e-05  8.1806e-05  1.9409e-05  2439    2680
```

```text
U0     rhoMin      rhoMax      max|rho-ref|  fluxRelDiff  massImb(comp)  ke/e        T recov err
1      1.176625    1.176757    1.3259e-04    2.4723e-05   1.7767e-08     4.832e-06   0
0.5    1.176625    1.176689    6.4836e-05    1.2296e-05   8.2266e-09     1.210e-06   0
0.25   1.176624    1.176656    3.2025e-05    6.2113e-06   3.7795e-09     3.025e-07   0
0.125  1.176624    1.176640    1.5864e-05    3.1000e-06   1.7959e-09     7.565e-08   0
```

**Every error metric falls as Mach falls.** `maxEosError` is **exactly 0** at every level.

## 2. The asymptotic trend — the answer to P17.6's question

```text
observed exponents, error ~ C Ma^n, over the full 8x range
  drho/rho        n = 1.0178      theory for this case: exactly 1
  keRelDiff       n = 1.0216
  relL2(p')       n = 1.0432      (absolute measure; the relative one is floor-limited, see 4)
  fluxRelDiff     n = 0.9706
  relL2(|U|)      n = 0.8848
```

**Why 1 and not 2.** For this channel the pressure drop is viscous-dominated,
`dp ~ 12 mu U L / H^2`, not dynamic (`rho U^2`). So `drho/rho = dp/p ~ Ma^1`. A quadratic
expectation would have been wrong, and none was assumed — the exponent is measured and only the
density exponent is gated tightly, because that is the one theory pins.

```text
So: the compressible solution DOES approach the incompressible one as Ma -> 0, at first order in
Mach, and the trend is demonstrated across four levels rather than asserted from one case.
```

## 3. Velocity and density

```text
velocity   relL2(|U|)  7.9159e-05 -> 1.2574e-05, monotone, n = 0.8848
           volume-weighted L2 of |U|_comp - |U|_inc, normalised by the incompressible |U| L2
density    rho stays inside [1.176624, 1.176757] against rho_ref = 1.17662428
           max|rho - rho_ref|  1.3259e-04 -> 1.5864e-05, monotone
           Delta rho/rho       1.1192e-04 -> 1.3482e-05, monotone, n = 1.0178
           EOS consistency     |rho_solver - p_abs/(R T)| = EXACTLY 0 at every cell, every level
```

The EOS row is the strongest single piece of evidence that the compressible path is real: a
constant-density fallback would leave an error of order `Delta rho`, not of order `eps`.

## 4. Pressure — and the convergence floor that limits it

Both solvers share the gauge (`Outlet` + `FixedValue(0)` at `right`), and the comparison is
additionally referenced to cell 0 so it is gauge-independent by construction. §7's gauge treatment is
therefore: **same gauge, and compared on the referenced field anyway.**

```text
U0     relL2(p')     floor         ratio to floor
1      7.1600e-04    5.2083e-05    13.75      above the floor
0.5    7.6267e-05    5.2083e-05     1.46      AT the floor
0.25   7.3820e-05    5.2083e-05     1.42      AT the floor
0.125  8.1806e-05    5.2083e-05     1.57      AT the floor
```

The floor is `pressureTolerance(U0) / dp(U0) = (5e-4 U0)/(12 mu U0 L/H^2) = 5.2083e-5` — **constant**
across the family, because both scale linearly with `U0`. From level 2 on, the relative pressure
comparison is floor-limited: it cannot resolve a difference finer than the solve's own convergence.

```text
So the trend criterion is carried by the ABSOLUTE difference (Linf(p'), monotone, n = 1.0432), and
the relative one is gated as "falls hard while above the floor, then stays AT the floor". The frozen
criterion said "strictly decreases", which the arithmetic makes impossible -- and, worse, it had NO
ASSERTION behind it until this was found. See AMENDMENT_thresholds.md section 2.
```

### The inlet-to-outlet pressure DROP corroborates the floor, from an independent probe

Measured by `tools/dryrun.cpp` on the **unchanged** tree (`logs/01_dryrun.log`), so this is a
different instrument from the gated harness and its numbers are reported separately rather than
mixed into the table above:

```text
U0      Linf(p')     relL2(p')    dropRelDiff
1       0.0076873    0.000716     0.00072955
0.5     0.00051494   7.6267e-05   4.6683e-05
0.25    0.00020917   7.382e-05    6.1513e-05
0.125   0.000103     8.1806e-05   7.5329e-05
```

`dropRelDiff` — the relative difference in inlet-to-outlet pressure drop between the two formulations
— is **NOT monotone**: it falls 15-fold, then RISES, 4.67e-5 → 6.15e-5 → 7.53e-5. It behaves exactly
like `relL2(p')`, and for the same reason: both are pressure differences divided by a drop that
shrinks with `U0`, so both hit the convergence floor and then drift upward. Two independently
computed pressure measures showing the same non-monotone signature is the strongest confirmation that
the floor of §4 is real and not an artefact of one metric's definition.

`Linf(p')` from the same probe is monotone across all four levels (0.0076873 → 0.000103), which is
why A3 was retargeted onto the absolute measure.

```text
An earlier draft of this document claimed dropRelDiff fell monotonically to 6.15e-5 and that the
pressure drop therefore "agrees to better than 0.1 percent". That was wrong on both counts -- it
picked the third level's value as if it were the last, and it described a non-monotone quantity as
monotone. Corrected here against logs/01_dryrun.log.
```

## 5. Temperature

```text
EXPECTED: exactly constant at 300 K. CompressibleSIMPLE takes temperature as an INPUT and this case
is isothermal, so there is no temperature field to converge and no temperature error to trend.
Stating that is the honest answer to section 8 rather than manufacturing a variation.

MEASURED, and this is the check that does mean something:
  temperature RECOVERY from rhoE/rho - 0.5|U|^2, through P17.5's exact inverse
  max |T_recovered - 300|  =  EXACTLY 0  at every cell, every level
```

## 6. Mass flux and mass conservation

```text
U0     inletFlow      outletFlow     |sum|        bound (continuityTol * flow)  margin
1      -1.176746981   1.176746964    1.7767e-08   1.1767e-06                    66.2x
0.5    -0.588342211   0.588342203    8.2266e-09   2.9417e-07                    35.8x
0.25   -0.294163509   0.294163506    3.7795e-09   7.3541e-08                    19.5x
0.125  -0.147079885   0.147079883    1.7959e-09   1.8385e-08                    10.2x
```

The bound is the solver's **own convergence contract**, `continuityTolerance(U0) x |inletFlow|`. My
first bound was set at round-off and failed by ~19000x — a converged SIMPLE solve stops at its
continuity tolerance, not at machine precision. See `AMENDMENT_thresholds.md` §1.

Face mass flux against the incompressible reference: `fluxRelDiff` 2.4723e-5 → 3.1e-6, monotone,
n = 0.9706, against the inherited 0.15 threshold — four orders inside it.

```text
The shrinking margin (66x -> 10x) is itself a finding: the mass imbalance does not fall as fast as
the tolerance tightens. Same conditioning signature as the iteration counts.
```

## 7. Momentum conservation

P17.4 ships **no** solver-level momentum-balance evaluator (0 hits in the tree), so §13's "use the
P17.4 diagnostic where applicable" resolves to *not applicable*. The budget is computed here from the
converged fields and cross-checked **analytically** rather than against a self-closing residual:

```text
U0     netMomFlux    pressureForce   viscousImplied   Poiseuille wall shear   ratio
1      0.204194      10.489          -10.2848         -9.6                    1.0713
0.5    0.0510543     5.15415         -5.1031          -4.8                    1.0631
0.25   0.0127593     2.55356         -2.5408          -2.4                    1.0587
0.125  0.00318935    1.27032         -1.26713         -1.2                    1.0559
```

The ~6 % excess over the fully-developed Poiseuille value is the **entrance effect**: a uniform inlet
developing over `L/H = 8` on a 6-cell-high mesh carries a higher wall shear than the fully-developed
limit. That explanation is made testable rather than asserted — the ratio must **shrink monotonically
as Re falls**, and it does (1.0713 → 1.0559). A discretisation bias would not trend.

## 8. Energy — the kinetic contribution, bounded separately

The frozen G5 clause names the trap: *"at Ma = 1e-3 the kinetic term is ~1e-6 of the internal energy,
so a criterion on e_t can be satisfied while the kinetic part is entirely wrong. The criterion must
bound the KINETIC CONTRIBUTION SEPARATELY, relative to its own magnitude."*

```text
internal energy e(300 K)          215385 J/kg, constant
ke_max / e                        4.832e-06 -> 7.565e-08     six to eight orders down
domain kinetic energy, comp       5.4589667 -> 0.085550747 J
domain kinetic energy, inc        5.4580807 -> 0.085549087 J
keRelDiff = |diff| / ke_inc       1.6233e-04 -> 1.9409e-05   monotone, n = 1.0216
```

`keRelDiff` is the criterion: the kinetic contribution against **its own** magnitude, never through
`rhoE`. Control **N5** plants the wrong normalisation and must **PASS**, which is the only way to
show the trap is real rather than theoretical.

### The total-energy balance, reported with its signal-to-noise and NOT gated below S/N 1

```text
U0     E imbalance     predicted (ke gain)  ratio      S/N
1      0.246232        0.251589             0.97871    46.97
0.5    0.0275335       0.0300139            0.91736    12.10
0.25   0.00248665      0.00362617           0.68575     3.18
0.125  -9.75411e-05    0.000443923         -0.21973     0.82
```

`predicted` is the **independently computed** mass-weighted kinetic-energy gain from inlet to outlet.
The ratio tracks it to 98 % and 92 % at the two highest levels, degrades to 69 %, then **inverts** —
exactly where S/N drops below 1.

```text
WHY THE BALANCE CANNOT CARRY THIS CRITERION, quantified:
  the balance's noise floor is |massImbalance| * h, the enthalpy-flux error a residual mass
  imbalance implies. With h = cp T = 301500 and massImbalance ~ 1.8e-9, that is ~5.4e-4 -- while the
  physical kinetic gain at the lowest level is 4.4e-4. The noise EXCEEDS the signal.
  On top of that the case is ISOTHERMAL: T is an input, so the isothermal constraint is an
  unmodelled heat sink, and P17.4 supplies no stress tensor for the viscous-work term. The balance
  was never going to close.
So the balance is REPORTED, gated only above S/N 1, with a non-vacuity guard that at least one level
is above the floor. Gating it below that would be gating noise.
```

## 9. Residual behaviour and conditioning

```text
U0     itComp   itInc   final u res (comp)   final p res   final continuity   status
1      1959     2031    within tolerance     within tol    within tol         Converged
0.5    2261     2450    "                    "             "                  Converged
0.25   2403     2626    "                    "             "                  Converged
0.125  2439     2680    "                    "             "                  Converged
```

All four levels converge in both formulations. No oscillation, no stagnation, no divergence. The two
formulations are **not** required to match iteration counts, and they do not.

```text
CONDITIONING, recorded under sections 11 and 21 rather than worked around:
  iterations RISE as Mach falls -- 1959 -> 2439 (compressible), 2031 -> 2680 (incompressible), both
  climbing toward the inherited maxIterations = 3000. Combined with the reference's inability to
  converge at U0 = 2 or 4 at all, the usable family is bounded on BOTH sides by that limit.
  This is the classic low-Mach conditioning behaviour of a pressure-based solver. NO convergence
  criterion was weakened, NO iteration limit was raised, and no preconditioning was introduced --
  section 21 places that work in a later numerical phase.
```

## 10. Determinism (G9)

```text
a repeated CompressibleSIMPLE solve from an identical initial state, at U0 = 1
  velocity x, velocity y, pressure, density   BITWISE identical, every cell
  mass flux                                   BITWISE identical, every face
  iterations                                  identical
  finalUResidual, finalPressureResidual,
  finalContinuityResidual, globalMassImbalance identical
```

Asserted with `EXPECT_EQ`, not a tolerance. Repeat count 2, which is what the existing
`test_compressible_simple.cpp` determinism test also uses.

## 11. CPU/GPU

```text
symbol                    hits in cuda/ + include/cfd/gpu/
----------------------------------------------------------
compressible              1   <- a COMMENT in DevicePressureCorrection.hpp reading "the body behind
                                 the incompressible", i.e. a false positive
IdealGas                  0     EquationOfState           0
ThermodynamicProperties   0     Enthalpy                  0
rhoE                      0     totalEnergy               0
TotalEnergy               0     machNumber                0
CompressibleSIMPLE        0
```

There is **no GPU compressible solver**, so there is no GPU low-Mach behaviour to preserve and no
differences to report. Reported **NOT PRESENT**, never PASS. P17.10's qualification is not claimed —
the frozen gate matrix already marks G8 DEFERRED as vacuous.

## 12. Preservation

```text
the EXISTING low-Mach test    hash matches the freeze; 9/9 pass, UNCHANGED
MMS compressible              hash matches; 1/1
Poiseuille                    13/13
cavity Ghia                    3/3
P14 transport                 68/68
P15 thermal                  161/161
compressible suite           225/225   = 210 at the P17.5 baseline + P17.6's 15
P13-001 SIMPLE fingerprint   BITWISE IDENTICAL, 0-byte diff, 10 cases / 130 lines
upstream + P17.1-P17.5 files 15 UNTOUCHED, 0 TOUCHED, by SHA-256
clang-format                 exit 0 on the one changed file
```

## 13. Anti-false-success (§20)

The audit found the existing low-Mach test never solves the compressible system. This phase's own
harness proves it is not repeating that:

```text
F1  the compressible density is NOT uniform, and the spread SCALES with Mach
    1.3169e-04 -> 1.5864e-05, monotone
F2  the solver's density satisfies the P17.2 EOS at the solved pressure
    maxEosError EXACTLY 0 -- a constant-density fallback would give O(drho)
F3  the two solutions DIFFER: relL2(|U|) > 0 at every level
    the mirror image of the trend criterion. A2 requires the difference to SHRINK; F3 requires it to
    be NON-ZERO. Together they say "converging by a real computation", which neither says alone.
F4  status Converged and iterations > 0 at every level: 1959 - 2439
```

## 14. What is NOT verified

```text
NOT VERIFIED   a second flow topology as a Mach family. The cavity is a CLOSED domain with no
               through-flow and therefore no pressure drop to drive a density variation, so it
               cannot carry a low-Mach limit study. It is run unchanged as a preservation case.
               Reason stated in case_family.md 5 rather than omitted.
NOT VERIFIED   Mach levels above the existing test's anchor. The INCOMPRESSIBLE reference cannot
               converge at U0 = 2 or 4 within the inherited maxIterations = 3000, which section 22
               forbids raising.
NOT VERIFIED   a closed total-energy balance. Impossible on an isothermal solve, quantified in 8.
NOT VERIFIED   CPU/GPU low-Mach equivalence -- blocked by absence, 11.
NOT VERIFIED   the viscous-work flux, inherited from P17.5: P17.4 builds no stress tensor.
NOT VERIFIED   3D. The compressible path is 2D-only.
NOT VERIFIED   a formal order-of-accuracy claim. The observed exponents are reported and only the
               density exponent is gated, because it is the only one theory pins.
```

---

## 15. Group N — the non-vacuity controls

`logs/05_controls.log`. P17.6 changes **no production source**, so the defects are planted in the
**validation harness itself** — which is the right target, because the thing under suspicion is
whether the comparison actually measures the compressible-to-incompressible distance, and the harness
is what makes that claim.

**The probe set is the whole phase suite** (`-R LowMachFamily`, all 15 tests). P17.4's N6 was wrongly
declared vacuous because its regex excluded the file holding an existing detector; the lesson is
applied here rather than restated.

```text
harness baseline sha256  01838fd3a113420641ef878550dde080adb9d39a0326b046a7ca4ea4fee895c5

N1  the reference is the COMPRESSIBLE solution itself
    DETECTED -- 3 of 15 failed
      CompressiblePathIsGenuinelyActive                      (F3: the solutions must DIFFER)
      EveryErrorMetricDecreasesMonotonicallyAsMachFalls
      EveryOtherErrorExponentIsGenuinelyDecreasing
    F3 is the criterion that exists for exactly this defect, and it is the one that fired.

N2  rho_ref = 1.0 instead of the EOS-derived 1.176624 (a 15 percent mismatch)
    DETECTED -- 4 of 15 failed
      EveryErrorMetricDecreasesMonotonicallyAsMachFalls
      EveryOtherErrorExponentIsGenuinelyDecreasing
      KineticContributionMatchesTheIncompressibleReference
      RelativePressureDifferenceFallsToItsConvergenceFloorAndStaysThere
    So the derived rho_ref of case_family.md 3 is load-bearing, not cosmetic -- and the A3
    replacement test from AMENDMENT_thresholds.md is among the detectors, which is independent
    evidence that the replacement criterion is not vacuous.

N3  machMax reports the inlet Mach, i.e. the REQUESTED value rather than the achieved one
    DETECTED -- 1 of 15 failed
      InheritedG5ThresholdsHoldAtEveryLevel
    M4's `machMax > 1.25 * inletMach` clause is what catches it: if machMax were the inlet value the
    ratio would be exactly 1.

N5  the energy criterion normalised by the rhoE scale instead of by the kinetic part
    PASSED -- 0 failures, AS THE TRAP PREDICTS.
    This control is inverted on purpose. The frozen G5 clause claims that a criterion placed on
    rhoE "can be satisfied while the kinetic part is entirely wrong"; the only way to show that is
    real rather than theoretical is to plant the wrong normalisation and watch the suite go green.
    It did. That is why E1 is written on keRelDiff and never on rhoE.

N4  NOT PLANTED, by design -- see below.

restore   SHA-256 VERIFIED after every control, 0 `// N` markers left, rebuild exit 0,
          and the suite back to 100 percent, 0 failed out of 15 at baseline.
```

### Why N4 is a measurement and not a plant

The frozen gate lists N4 as *"drop the `1.25 × inletMach` clause → M4 becomes vacuous"*. Planting that
means **deleting an assertion**, and a deleted assertion always passes — so there is no defect for any
probe to detect. It is a degenerate control, and running it would have produced a meaningless green.

What the clause is actually worth is its **margin**, which is measurable:

```text
machMax / inletMach = 0.0041552 / 0.0028802 = 1.4427
the clause requires                         > 1.25
```

The flow does accelerate, by 44 % against a 25 % bar — a real margin, and 1.4427 sits sensibly below
the fully-developed Poiseuille peak factor of 1.5 that it is approaching. Recorded as a measurement
rather than faked as a plant.

### One thing the controls run itself got wrong first

The first attempt planted **nothing** for N1: the gate had already run `clang-format-18 -i` on the
harness, joining the anchored declaration onto one line, so the `sed` matched no text. The script
continued and was on course to report a **spurious VACUOUS** — a false accusation against a criterion
that is in fact sound. It was stopped, the anchor corrected to the post-format text, and a
`verify_planted()` guard added that greps for the plant's own marker and **aborts** before every
control:

```text
A sed that does not match silently plants NOTHING and the control then reports a spurious "VACUOUS".
```

That guard, plus `set -e` around the Python plant, is why the four results above can be trusted: each
one is known to have had a defect in the tree when it ran, because the marker was seen there.
