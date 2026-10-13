# P17.3 — implementation

---

## 1. The delta, attributed

```text
file                                                    vs HEAD   earlier phases   THIS session
-----------------------------------------------------------------------------------------------
include/cfd/compressible/CompressibleContinuity.hpp        +20         —              +20
src/compressible/CompressibleContinuity.cpp                +51         —              +51
tests/unit/compressible/test_compressible_continuity.cpp   +342        —             +342
tests/unit/compressible/test_compressible_mass_flux.cpp    +357      +276 (P17.1)     +81
-----------------------------------------------------------------------------------------------
                                                   4 files      770 ins.        494 ins.
                                                                0 deletions     0 deletions
```

**Zero deletions on all four files.** No existing line of production code, test code or
contract was modified or removed — `git diff --numstat` reports `-0` for each.

Of this session's 494 insertions, **71 are production lines** and of those roughly **28 are
executable code**; the rest is the interface contract and the reasoning. 423 are tests.

```text
src/physics/ContinuityEquation.cpp        UNTOUCHED -- empty delta, verified as a gate step
include/cfd/physics/ContinuityEquation.hpp UNTOUCHED
```

## 2. Addition one — `globalNetFlux` on the transient result

```cpp
// CompressibleContinuityResult gains:
Real globalNetFlux{};
```

```cpp
// accumulated over BOUNDARY faces only, in the same order and with the same
// outward-positive convention as physics::evaluateContinuity
for (const auto& patch : mesh.boundaryPatches()) {
  for (const Index faceId : patch.faceIds()) {
    result.globalNetFlux += massFlux[faceId];
  }
}
```

### Why it was needed rather than merely nice

§8's strongest statement of transient mass conservation is

```text
M(t2) - M(t1) + dt * sum_boundary(m_dot_f) == 0
```

`totalMassOld` and `totalMassNew` already gave `M(t1)` and `M(t2)`. The boundary sum did
**not** exist on this result — measured, 0 hits for `globalNetFlux` in the header and the
source — so a caller had to re-loop `mesh.boundaryPatches()` and **re-derive the sign
convention by hand**, outside the function that owns it. The P17.3 dry-run probe
demonstrably had to do exactly that.

`physics::ContinuityResult` has carried this field all along; the compressible transient
result simply did not. The addition closes an asymmetry rather than inventing a quantity.

### Boundary faces only, and why that is the load-bearing detail

Internal faces cancel pairwise and contribute nothing to a domain balance, so a sum over
*all* faces would be a number with no physical meaning. Control **N3** plants exactly that
and three tests reject it — including `InteriorFaceContributionsCancelGlobally`, which
asserts that summing every cell's imbalance reproduces `globalNetFlux`, i.e. that the
interior really does vanish.

## 3. Addition two — value preconditions

```cpp
for (Index i = 0; i < mesh.numberOfCells(); ++i) {
  if (!std::isfinite(densityOld[i]) || !(densityOld[i] > 0.0)) throw ... "cell i: densityOld ...";
  if (!std::isfinite(densityNew[i]) || !(densityNew[i] > 0.0)) throw ... "cell i: densityNew ...";
}
for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
  if (!std::isfinite(massFlux[f])) throw ... "face f: massFlux must be finite";
}
```

### The measurement that justifies it

Before P17.3, an invalid input did not merely propagate — **it produced a reassuring
answer**:

```text
rhoNew[0] = NaN     -> maxCellImbalance 0      "perfectly conserved"
massFlux[0] = NaN   -> maxCellImbalance 0      "perfectly conserved"
rhoNew[0] = -2.0    -> totalMassNew 0          finite, plausible, catastrophic
rhoNew[0] = +Inf    -> maxCellImbalance inf
```

`std::max(a, b)` is `(a < b) ? b : a`. With a running maximum of `0` and `b = NaN`, the
comparison `0 < NaN` is **false**, so the running max is returned unchanged and **the NaN
is silently skipped**. The max reduction therefore reports zero imbalance for a non-finite
field, while `totalAbsoluteImbalance` becomes `NaN` — the two metrics disagree, and a
solver watching the max would declare convergence on a NaN state.

This is the mirror of P17.1's finding. There, a finite-but-wrong density evaded
`allFinite()`. Here, a NaN evades the max-reduction. **Both are cases where the diagnostic
reassures instead of warning**, which is why the check belongs at the input and not
downstream.

### `massFlux` is checked for finiteness only

A face flux is legitimately **negative** (inflow) and legitimately **zero** (a wall), so a
positivity rule would be wrong for it — the same distinction P17.1 drew for gauge pressure,
which is legitimately negative below the reference. `RejectsNonFiniteMassFlux` asserts a
negative flux is **accepted**, so the rule cannot drift into positivity.

### Rejected, never clamped

Consistent with P17.1, P17.2 and `thermal/Enthalpy.hpp`'s recorded policy: a clamped state
yields numbers no physical state corresponds to and lets a diverging solve look plausible.
There is no code path that modifies an input.

## 4. What was deliberately NOT changed

```text
physics::evaluateContinuity
  UNTOUCHED. It is shared with the incompressible path, and modifying it would put P17.3 in
  the way of the P13-001 constant-density fingerprint for no P17.3 benefit. The resulting
  asymmetry -- the compressible evaluator now validates values and the shared one does not
  -- is recorded as debt, not silently equalised.

a production caller for the transient diagnostic
  NOT added. Giving it one would make CFDApp solve transient compressible continuity, i.e.
  a transient compressible SOLVER, which no P17 item has authorized. The diagnostic
  remains a diagnostic; P17.3 verifies it and makes its conservation statement expressible.

the mass-flux construction
  UNTOUCHED (P17.1's). The one edit to CompressibleMassFlux.cpp in this tree is P17.1's
  density precondition; P17.3 added only tests to that file.

the dilatational viscous stress (P17.4), two-way energy (P17.5), HLLC (P17.7),
a density-based solver (P17.8), a GPU continuity path (none exists)
```

## 5. Tests added — 12

```text
test_compressible_continuity.cpp                      6 -> 17   (+11)
  GlobalNetFluxSumsBoundaryFacesOnly                  boundary-only, and asserts the
                                                      all-faces sum DIFFERS so N3 is
                                                      detectable
  UniformDivergenceFreeFluxGivesExactlyZeroImbalance  the operator, directly
  InteriorFaceContributionsCancelGlobally             irregular flux; sum of imbalances
                                                      == globalNetFlux
  ReducesToTheSteadyEvaluatorWhenDensityIsUnchanged   the summation-order question
  TransientIntegralMassConservationHolds              M(t2)-M(t1)+dt*net, two cases
  RejectsNonFiniteOrNonPositiveDensity                5 bad values x both fields
  RejectsNonFiniteMassFlux                            3 bad values, plus a VALID negative
  RejectionMessagesNameTheOffendingIndex              cell N / face N named
  QuiescentClosedBoxConservesExactly                  the canonical closed box
  TotalMassMatchesSumOfRhoTimesVolume                 4 eps, density genuinely changing
  RepeatedEvaluationIsBitwiseIdentical                8 repeats, every field

test_compressible_mass_flux.cpp                      20 -> 21   (+1)
  RhoTimesUConstantConvergesAtSecondOrder             the refinement study
```

`RhoTimesUConstantConvergesAtSecondOrder` is the one worth pointing at: it asserts the
imbalance is **non-zero at every level** before checking the decay rate, because an exactly
zero imbalance would mean the test was not measuring `O(h^2)` at all.

## 6. Formatting

```text
clang-format-18 on all four changed files    CLEAN (gate G6, exit 0)
deletions after formatting                   still 0 on all four
```

The 22 pre-existing tree-wide failures found during P17.1 remain untouched standing debt;
none is a P17.3 file.
