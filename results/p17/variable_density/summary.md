# P17.1 — Variable Density: summary

**P17.1 STATUS: COMPLETE**, with one checklist row marked `[~]` partial on purpose.
2026-09-26.

Evidence root: `results/p17/variable_density/`. Gate frozen before any edit,
sha256 `876466e2c97fd8db93e1dd96ac834a18a0c1c99ee8d381ab45b324b6dedc39ff`.

---

## 1. What P17.1 turned out to be

P17.0's audit said eleven of thirteen P17.1 rows already existed. That held up: **the
production change is 34 lines in one function.** The rest was verification, negative
testing, conservation measurement and documentation.

```text
requirement                                  class               outcome
-------------------------------------------------------------------------------------
Define authoritative density update path     ALREADY SATISFIED   documented
Support spatially varying rho                ALREADY SATISFIED   verified
Couple density to pressure / temperature     ALREADY SATISFIED   verified, both signs
Preserve constant-density limit              ALREADY SATISFIED   verified, BITWISE
Density bounds and invalid-state handling    NEEDS EXTENSION     IMPLEMENTED  <- the code
  density use: continuity                    ALREADY SATISFIED   verified
  density use: momentum                      ALREADY SATISFIED   verified
  density use: energy                        CONSTANT BY DESIGN  documented, [~]
  density use: mass flux                     ALREADY SATISFIED   verified
  density use: boundary conditions           ALREADY SATISFIED   verified
Unit tests                                   NEEDS EXTENSION     8 added, 20/20
Conservation tests                           NEEDS EXTENSION     measured, exact
```

## 2. The gap that needed code, and why it was real

`validateFaceDensityInputs` validated array **sizes** and never density **values**, in a
module where the internal-face branch is a plain weighted mean with no validation of its
own. Measured on the unchanged tree before any edit (`logs/00_dryrun.log`):

```text
input            internal rho_f   mDot_f   non-finite?   allFinite() would catch?
------------------------------------------------------------------------------------
rho[0] = -2.0          0             0          NO               NO
rho[0] =  0.0          1             1          NO               NO
rho[0] = NaN         nan           nan         yes              yes
rho[0] = +Inf        inf           inf         yes              yes
rho[0] = -Inf       -inf          -inf         yes              yes
```

All five **returned** instead of throwing. The two that matter are the finite ones: a
negative density averaged against a valid neighbour gives a face density of **exactly
zero** — a face silently carrying no mass — and a zero density gives a plausible non-zero
flux from an impossible state. Neither is non-finite, so `CompressibleSIMPLE`'s
`allFinite()` sweep, the only protection downstream, cannot see either.

Production was never unsafe, because every density reaching these functions is
EOS-evaluated. But nothing in the contract said so, nothing enforced it, and **P17.8's
density-based solver will evolve `rho` as a conserved variable that can go non-positive** —
exactly the caller that would have hit this silently.

## 3. Implementation

```text
src/compressible/CompressibleMassFlux.cpp                +34   -0
include/cfd/compressible/CompressibleMassFlux.hpp        +12   -0
tests/unit/compressible/test_compressible_mass_flux.cpp +276   -0
                                   3 files, 322 insertions, 0 DELETIONS
```

One validator, at the chokepoint both public entry points share:

```cpp
for (Index i = 0; i < mesh.numberOfCells(); ++i) {
  if (!std::isfinite(density[i]) || !(density[i] > 0.0)) {
    throw InvalidArgumentError(std::string(callerName) + ": cell " + std::to_string(i) +
                               ": density must be finite and > 0");
  }
}
```

Rejected, **never clamped** — following `thermal/Enthalpy.hpp`'s existing policy that an
out-of-range value is "an ERROR, not a clamp", because a clamped density yields a mass
flux no thermodynamic state corresponds to. A positivity *limiter* is a conserved-state
concern and was deliberately left to P17.8.

Two scope boundaries decided before implementing:

```text
no temperature/pressureGauge value check here -- the EOS already validates both at every
  boundary face, and an up-front check would make ThrowsOnNonPositiveBoundaryTemperature
  throw earlier: still passing, but no longer exercising the boundary EOS it was written
  for. Also pressureGauge is GAUGE pressure, legitimately negative, so positivity would
  be the WRONG rule for it.
no validators in the momentum / pressure-correction / continuity modules -- they consume
  the same field and the same faceDensity this path validates.
```

## 4. Verification

```text
Unit tests            CompressibleMassFluxTest  20 / 20   (12 existing + 8 new)
Conservation          exact -- see section 5
CPU regression        2305 / 2305, 0 failed     PREDICTED 2305
CUDA regression       2505 / 2505, 0 failed     PREDICTED 2505
G1 preservation       P13-001 SIMPLE fingerprint BITWISE IDENTICAL, 10 cases / 130
                      lines, empty diff preserved; P14 68/68, P15 161/161, P16 315/315
G2 closure            28 / 28; dRho/dp > 0, dRho/dT < 0; 2p doubles rho and 2T halves it
                      EXACTLY
G3 variable-density   imbalance exactly 0 with rho varying 2:1; per-row flux ratio
  conservation        exactly 2
G10 regression        both mandated configurations PASS; Debug+GUI NOT RUN by decision
Controls N1-N5        ALL DETECTED, all restores SHA-256 verified, 0 markers left,
                      20/20 at baseline afterwards
```

Every registry and execution count landed on a number frozen **before** the run:

```text
Release+CUDA   2567 registered - 13 excluded - 49 disabled = 2505 executed    exact
CPU-only       2365 registered - 13 excluded - 47 disabled = 2305 executed    exact
```

## 5. Conservation — measured, not asserted

```text
                            variable rho (2:1)   constant rho
max cell imbalance                 0                  0
total |imbalance|                  0                  0
global net boundary flux           0                  0
mdot_in / mdot_out              -6 / +6            -8 / +8
mdot_in + mdot_out                 0                  0
derived bound            1.2079226507921703e-13  (8 * eps * N_f * max|mdot|)
```

The bound scales with the mesh and the flux magnitude; `kAccum = 8` is labelled
**empirical**. The measured imbalance is exactly zero, thirteen orders inside it.

Throughput differs — **6 against 8** — because the variable case carries `4 + 2` across
its two rows while the constant case carries `4 + 4`. That difference is the evidence the
density field is consumed rather than ignored.

**The discriminator, quantified:** bottom-row face `|mdot| = 4`, top-row `= 2`, ratio
exactly 2. A face density collapsed to one constant would give ratio 1 — and controls
**N4 and N5 both perform that collapse, and this is the only test in the suite that
caught either of them.**

## 6. Equation-consumer matrix

```text
consumer     variable rho verified?      evidence
---------------------------------------------------------------------------------
Continuity   YES                         RowVaryingDensityConservesMassAndScales-
                                         FluxPerRow  (new)
Momentum     YES                         CompressibleMomentumTest (7), Relaxed (5),
                                         system MMS with div(rho U) = 0 exactly
Energy       NO -- CONSTANT BY DESIGN    ProjectRunner.cpp:531-537 then :544
Mass flux    YES                         4 tests incl. 3 new
Boundaries   YES                         5 existing boundary tests, all PASS
```

## 7. The energy row — why it is `[~]`, not `[x]`

Traced by ordering, which is sharper than "one-way coupling":

```text
:531-537  massFlux = physics::calculateMassFlux(..., setup.fluid, ...)   SCALAR density
:544      the thermal solve consumes *massFlux
:756      the compressible block runs AFTERWARDS with its own variable-density flux
```

A compressible run carries **two mass-flux fields**, and energy, species and multiphase
all consume the constant-density one. Making energy consume the compressible flux is
two-way energy coupling — **P17.5** — so changing it here would be unauthorized scope and
a silent physics change. The authorization is explicit that an intentionally
constant-density path is to be documented, not quietly altered.

What P17.5 inherits: P15's library layer is **already variable-density capable** —
`ThermalTimeState` carries `densityOld` and `densityNew` as `ScalarField*` for the
conservative `rho cp dT/dt` term. That transient path simply has no production caller;
`ThermalSolver` runs steady, where its own header records that rho enters only via the
mass flux. So P17.5 extends a foundation rather than rewriting one.

## 8. Findings beyond the checklist

```text
1. InternalFaceUsesArithmeticMeanDensity is NON-DISCRIMINATING for a constant-density
   collapse. Its density pair {1.0, 3.0} has a mean of exactly 2.0, which equals
   kReferenceDensity -- so it cannot tell "the arithmetic mean" from "the reference
   constant". A contaminated instrument; NOT modified (out of scope), recorded as debt.
   This is why control N5 did not break it, and why the new row-varying test matters.

2. P13's final qualification NEVER RAN clang-format. Zero mentions across
   results/p13/final-qualification/. CLAUDE.md section 8 defines the full regression as
   Release, Debug+GUI, sanitizers AND clang-format clean; G10 reported PASS having
   executed three of four. 22 files currently fail the gate, none of them P17.1's.
   Recorded as debt; deliberately not fixed inside P17.1, since reformatting 22 unrelated
   files is exactly the accidental sweep the integrity check forbids.

3. The EOS has no configured validity BAND (no [Tmin, Tmax]) unlike P15's
   TemperatureDependentProperty. For an ideal gas positivity IS the physical domain, so
   this is a difference in kind rather than a defect -- but a caloric or real-gas model
   would need the band and the abstraction has nowhere to put it. Carried from P17.0.

4. An invalid TEMPERATURE in a cell not adjacent to any boundary face is not detected by
   the mass-flux path. Narrow, and not closed here for the instrument reason in section 3.
   It would matter to a solver that writes temperature per cell -- P17.5.
```

## 9. Repository impact

```text
files added        results/p17/variable_density/** -- 12 documents, 6 tools, raw logs,
                   1 preserved invalid run, 1 amendment
files modified     3 source files + TODO.md
source files       3        insertions 322, DELETIONS 0
test files         1 (of the 3)
threshold changes  NONE
evidence removed   NONE -- the invalid regression attempt is preserved with WHY_INVALID.md
```

```text
tracked modified                   167 -> 170   (+3, exactly the P17.1 files)
untracked                          107 -> 107   (+0)
tracked engineering files changed   116 -> 119   (+3)
source files with a post-freeze mtime: exactly the 3 P17.1 files, verified
binaries added                      0
P17_CODE_FINGERPRINT   1e307fcd… -> eba3d702…, then UNMOVED across the regression
SOURCE_FINGERPRINT_V2  fbf78665… -> 8a179e5b…
untracked component    069c034c… -> 069c034c…   BYTE-IDENTICAL
```

## 10. Recommended next step

**P17.3 — compressible continuity verification.** Freeze its gate and dry-run it first.

Why P17.3 and not P17.2: P17.2 is already closed and PASS. Why not P17.4: it contains
real implementation work (the absent dilatational stress term
`-(2/3) mu grad(div U)`), so it deserves its own authorization rather than being reached
by momentum from a continuity item.

What P17.3 inherits from P17.1, already established: one authoritative face mass flux
`m_dot_f = rho_f (U_f . n_f) A_f`, recomputed per call and never cached; exact discrete
conservation on a variable-density field; and a validated density precondition, so
continuity can assume a positive finite density rather than re-checking it.

The hazard P17.3 must design against, carried from P17.0: **the transient form
`d(rho)/dt + div(rho U) = 0` has no production caller.** `evaluateCompressibleContinuity`
is a diagnostic by its own header, reached only from tests;
`CompressibleSIMPLE.cpp:478` calls the **steady** `physics::evaluateContinuity`. A P17.3
gate that exercised only the diagnostic would verify a library function while claiming to
verify the solver.

**Not started.** P17.3 has no implementation authorization.
