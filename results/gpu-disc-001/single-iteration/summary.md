# GPU-DISC-001L — single-iteration CPU/GPU differential

**Result: PASS.** One production SIMPLE outer iteration produces the same state on CUDA as on the
CPU. With the two paths given the same solver outputs, **every one of the 11 stages is bitwise
identical**; with each path solving independently, they agree to ~1e-10 on fields of order 1e2.

```text
cases                       6   cavity 2d 8/16, channel 2d 16, duct 2d 16, cavity 3d 4,
                                channel 3d 4, warped 3d 3     (2D and 3D, linear and
                                Rhie-Chow predictor, pinned and open pressure)
fidelity checks             6   ladder == SIMPLE::solve(maxIterations = 1), BITWISE
controlled ladders          6   all 11 stages BITWISE
independent ladders         6   within tolerance; stages 1 and 3 still bitwise
invariant cases             6
determinism checks          6   each backend repeated, identical
values compared        51,458
bitwise-identical      43,539   (every controlled-ladder value; the rest are the
                                independent ladder, where the two solves differ)

observable controls       9/9 detected, ALL at the expected stage
documented null             1
compute-sanitizer         4/4 clean, non-vacuous
GPU-DISC gates          13/13 green
full regression    1998/1998 passed, 0 failed, 1321.7 s (45 disabled of 2043 registered)
                   `ninja: no work to do` BEFORE and AFTER ctest; library and
                   source sha256 identical on both sides
```

Full-solve equivalence and GPU-PIPE-001 residency were **not** started.

## 1. Entry point and stage order

`SIMPLE::solve` (`SIMPLE.cpp:125`), outer loop at `:271`. The complete audited order is in
`audit.md` section 2; the load-bearing details:

```text
 pre-loop  massFlux = calculateMassFlux(velocity)          ALWAYS linear, never Rhie-Chow
 1  previousU/V/W from the CURRENT velocity
 2  relaxation fixed for the whole iteration
 3  turbulence correct -> effectiveViscosity
 4  momentum assembly U, V, (W)
 5  momentum solve    U, V, (W)     WARM-STARTED from the previous iterate
 6  velocityStar
 7  [non-orthogonal passes, N > 1]
 8  dU, dV, (dW) from the assembly diagonals
 9  predictorFlux from velocityStar and the START-OF-ITERATION pressure
10  pressure-correction assembly
11  pressure solve                  ZERO initial guess
12  [pressure passes, N > 1]
13  pressureNew = pressure + relaxation.pressure * p'
14  velocityNew = correctVelocity(velocityStar, ...)        velocityStar, not velocity
15  fluxNew     = correctFaceMassFlux(predictorFlux, ...)   predictorFlux, not massFlux
16  allFinite or NonFiniteState
17  residuals = the solves' INITIAL residuals, not their final ones
18  continuity from fluxNew
19  COMMIT velocity, pressure, massFlux
```

`relaxation.pressure` multiplies p' in the **pressure update only** — the velocity and flux
corrections use the unrelaxed p'. There is **no separate BC-enforcement stage**: boundary conditions
enter through the operators, and nothing is re-applied to the committed fields.

## 2. Why there is a ladder, and why it is trustworthy

The brief forbids a simplified SIMPLE-like harness where a production path exists. `SIMPLE::solve`
is that path, but it exposes only the committed end state — not the 11 intermediates this gate must
compare. So the harness walks the stages by calling **the same production functions in the same
order**, and then asserts that its committed state is **bitwise identical** to
`SIMPLE::solve(maxIterations = 1)`'s `velocity`, `pressure`, `massFlux` and all six residuals.

That assertion — layer **F**, 6/6 passing with `fields[d=0] residuals[d=0] iterations=1` — is what
makes the ladder the production iteration rather than a lookalike. A harness that was never checked
against the real solver is exactly what the brief warns against.

## 3. Stage-by-stage results

### Controlled (layer C) — both paths consume the same solver outputs

**Every stage bitwise, on all 6 cases**, with `differing = 0` everywhere. This is the gate's central
claim: the integrated discretization path is exactly equivalent, and no solver variability can be
hiding an operator difference.

### Independent (layer I) — each path runs its own solves

Representative (`warped 3d 3`):

```text
 1 momentum systems        differing=0       maxAbs=0          scale=1.243
 2 momentum solutions      differing=81      maxAbs=1.13e-10   scale=248.7
 3 response coefficients   differing=0       maxAbs=0          scale=7.082
 4 predicted flux          differing=63      maxAbs=6.47e-12   scale=27.24
 5 pressure system         differing=27      maxAbs=1.27e-11   scale=18.45
 6 pressure correction p'  differing=27      maxAbs=3.49e-11   scale=24.72
 7 updated pressure        differing=27      maxAbs=1.05e-11   scale=4.513
 8 corrected U/V/W         differing=81      maxAbs=8.58e-11   scale=164.3
 9 corrected face flux     differing=63      maxAbs=4.09e-12   scale=6.342
10 continuity              differing=27      maxAbs=9.76e-12   scale=4.701e-12
11 residuals               differing=4       maxAbs=9.88e-12   scale=33.39
```

**Stages 1 and 3 stay bitwise even here**, and that is asserted separately rather than folded into a
tolerance: they are the only stages upstream of every solve. Everything from stage 2 onward is
solver-limited by dataflow.

### First divergence

No stage diverges in either layer on any case. Where a mutation is injected, the harness names the
first divergent stage, and §6 shows it names the right one every time.

### A criterion of mine that was wrong

My first version also demanded stage 5 bitwise in the independent layer, on the reasoning that an
"assembly" is upstream of the solves. It is not: the pressure system is built from the predicted
flux, which is built from the momentum **solutions**. Requiring it bitwise demanded that two
different linear solves agree exactly. Corrected to stages 1 and 3, which the dataflow actually puts
upstream. This was a wrong criterion, not a tolerance that needed loosening.

## 4. Final one-iteration state

Compared in full: pressure, U/V/W, face flux, the four momentum/pressure residuals, the continuity
residual and the global mass imbalance, plus solver iteration counts and restart counts. The
committed state is the three fields SIMPLE carries forward — `velocity`, `pressure`, `massFlux` —
and all three are covered, bitwise in the controlled layer.

Every solve converged with **0 restarts** on both backends across all cases, so the recorded GPU
BiCGSTAB restart asymmetry did not fire and nothing had to be classified against it. It was not
fixed here.

## 5. Invariants (layer V) and determinism (layer D)

All 6/6, each stated only where the CPU makes it exactly true:

* **finiteness** — no NaN/Inf in any committed field, on either backend;
* **owner/neighbour cancellation** — every interior face visited once as owner and once as
  neighbour, every boundary face once and never, with `(+F)+(-F) == 0.0` exactly;
* **the 2D W contract** — a 2D solve's corrected w is exactly `+0.0` on both backends;
* **reference/pin behaviour** — with no FixedValue pressure patch, `p'[referenceCell]` is exactly
  `0.0`;
* **boundary conditions still satisfied** — every non-FixedValue boundary face's flux is left equal
  to its predictor, checked against the predictor directly rather than against the CPU;
* **determinism** — each backend run twice gives bitwise identical fields.

## 6. Negative controls — integration level

`negative-control/` — these mutate the ladder's **composition**, not an operator's arithmetic: the
operators are already covered by 001B–001K. What this gate must catch is verified operators wired
together wrongly. Each control declares the stage at which the divergence should **first** appear,
and a control detected at the wrong stage is a failure — otherwise the stage-by-stage report is not
localising, which is the whole reason for building a ladder instead of comparing end states.

| control | mutation | expected first stage | result |
| --- | --- | --- | --- |
| L1 | velocity correction skipped | 8 corrected U/V/W | detected, stage 8 |
| L2 | face-flux correction skipped | 9 corrected face flux | detected, stage 9 |
| L4 | pressure correction assembled from the start-of-iteration flux | 5 pressure system | detected, stage 5 |
| L5 | pressure update skipped | 7 updated pressure | detected, stage 7 |
| L6 | p' negated before the corrections | 8 corrected U/V/W | detected, stage 8 |
| L7 | flux correction consumes the corrected velocity's flux — stage-order swap | 9 corrected face flux | detected, stage 9 |
| L8 | pressure update drops the relaxation factor | 7 updated pressure | detected, stage 7 |
| L9 | momentum fed a pressure that is not the start-of-iteration one | 1 momentum systems | detected, stage 1 |
| L10 | velocity correction applied to the old velocity, not u* | 8 corrected U/V/W | detected, stage 8 |

**9/9 observable controls detected, 9/9 at exactly the expected stage.** All 10 restored to the
baseline sha256 and re-passed.

### The documented null control — and what it revealed

**L3** feeds `dV` where `dU` belongs. It came back UNDETECTED, and the reason is a property of the
codebase worth recording:

> **The momentum matrix is component-independent.** In both the diffusion and the convection
> boundary terms the component enters only the RHS, via `selectComponent(uB, component)`, while the
> diagonal contribution `builder.add(ownerId, ownerId, coefficient)` is added for every boundary
> face regardless of the condition's type (`MomentumEquation.cpp:136`, `:236`, `:298`). Interior
> faces share one mass flux, one geometry and one viscosity. So `dU == dV == dW` **exactly,
> always**, and substituting one for another cannot change any result.

I first assumed this was a coverage gap and added a `symmetryDuct` case, expecting a Symmetry patch
— which removes only the normal component — to separate the diagonals. It does not, for the reason
above. The case is kept anyway, because Symmetry is the one velocity condition the other cases do
not exercise, but its rationale is corrected in the source.

The harness now **asserts the property** (layer `R`, 6/6) instead of requiring its negation. That is
the right shape: if a future change ever made the diagonal component-dependent, the assertion fires
and L3 stops being null the same day. Note the practical consequence — the anisotropic response
`D = diag(d_u, d_v, d_w)` that `pressureCorrectionFaceCoupling` is written for is **isotropic in
practice** in this codebase. That is not a defect; the operator is general. But it means a whole
class of component mis-wiring is undetectable, and claiming otherwise would be claiming coverage
this gate does not have.

### A driver defect, carried forward from 001K and hit again

L3's first run also exposed that the control driver's restore compared SHA-256 after a
`read_text`/`write_text` round-trip, which translates newlines and can change the bytes. Switched to
a binary round-trip; all 10 controls now restore with a matching hash. The 001K lesson — a driver
that can leave a mutation behind, or that cannot prove it did not, is a defect in the instrument —
applies to hashing as much as to build failures.

## 7. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck: **0 errors** each; racecheck: **0 hazards**.
Each log was verified to contain `SINGLE ITERATION EQUIVALENCE: PASS`, so none is vacuous.

## 8. Files

```text
new
  results/gpu-disc-001/single-iteration/tools/single_iteration_equivalence.cpp
  results/gpu-disc-001/single-iteration/tools/{build_and_run,diagnostics,all_gates,regression}.sh
  results/gpu-disc-001/single-iteration/tools/negative_controls.py
```

**No production source was added or modified by this gate.** It is an integration test over
operators that already exist. One build-level note: this is the first harness to link `SIMPLE.cpp`,
which calls back into `cfdcuda`'s residency manager, so the two archives are mutually dependent and
need `-Wl,--start-group`, not an ordering.

## 9. Gate chain and regression

`all_gates.log`, `regression.log`, `regression_freshness.log`

```text
libcfdcuda.a  a6f376e53ad8d13214dd10cd14ccc4be6be92216cb43d50651b2863cee1845f1
libcfdcore.a  eae75242473f6be813fc2926f07b14c877ce28bfe787441aca47119af4e2d16f

mesh 96 checks · gradients 132 · diffusion 528 · convection 1684 · momentum convection 10352
boundary conditions 75 · momentum assembly 27744 · momentum response 260 · face flux 2628
pressure correction 4950 · velocity correction 894 · face-flux correction 1848
single iteration 42
                                                        13/13 gates green

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998     (45 disabled of 2043 registered)
  Total Test time (real) = 1321.71 s
  `ninja: no work to do` BEFORE and AFTER ctest; library and source sha256
  identical on both sides
```

## 10. What is NOT started

Integrated GPU SIMPLE discretization, full-solve equivalence, GPU-PIPE-001 residency. Nothing
committed or pushed.
