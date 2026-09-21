# GPU-DISC-001L Phase A — production SIMPLE outer-iteration audit

Written before any harness. The CPU implementation is the specification.

## 1. Entry point

```text
SIMPLE::solve                        src/pressure_velocity/SIMPLE.cpp:125
  outer loop                         SIMPLE.cpp:271
```

The whole iteration lives inline in that loop; there is no separate
`oneIteration()` to call. Setting `settings_.maxIterations = 1` executes exactly one outer
iteration and returns its committed state.

## 2. Exact stage order

### Before the loop (SIMPLE.cpp:210-234)

```text
velocity  = initialVelocity
pressure  = initialPressure
massFlux  = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries)   // :212  LINEAR, always
allFinite(velocity, pressure, massFlux) or NonFiniteState
turbulence model: turbulenceModel_, or a call-scoped LaminarModel (mu_t = 0)
```

The initial `massFlux` is **always** the plain linear interpolation, never Rhie–Chow, whatever
`faceFlux` resolves to for the iteration itself.

### One outer iteration

```text
 1  previousU/V/W   = components of the CURRENT velocity                          :282-285
 2  relaxation      = monitor.relaxation()   (fixed for the whole iteration)      :289
 3  turbulence      activeModel->correct(mesh, velocity, pressure)                :307
                    effectiveViscosity = activeModel->effectiveViscosity(mu)      :308
 4  momentum assembly, in order U, V, (W)                                         :319-338
                    assembleRelaxedMomentumComponent(mesh, velocity, pressure,
                        massFlux, mu_eff, ..., previousComponent, relaxation.velocity, ...)
 5  momentum solve, in order U, V, (W)                                            :385,393,404
                    momentumSolver->solve(system, toVector(previousComponent))
                    ^^^ WARM-STARTED from the previous iterate
 6  velocityStar    = combineComponents(uResult, vResult, [wResult])              :415
 7  [non-orthogonal passes 2..N, only when nonOrthogonalCorrections > 1]          :439-468
 8  dU, dV, (dW)    = computeMomentumResponseCoefficient(mesh, assembly->diagonal) :474-477
 9  predictorFlux   RhieChow: gradP = gradient(pressure, pressureBoundaries, scheme)
                             rhieChowMassFlux(velocityStar, pressure, gradP, dU, dV, dW,
                                              fluid, velocityBoundaries, relaxation.velocity)
                    Linear:  calculateMassFlux(velocityStar, fluid, velocityBoundaries)  :484-501
10  pressure assembly assemblePressureCorrection(predictorFlux, dU, dV, density,
                                                 referenceCell_, pressureBoundaries,
                                                 options, dW)                     :531
11  pressure solve   pressureSolver->solve(system)                                :550
                     ^^^ ZERO initial guess (p' resets to 0 every outer iteration)
12  [pressure passes 2..N, each re-assembled with the previous p' and re-solved
     from ZERO, only when nonOrthogonalCorrections > 1]                           :567-610
13  pressureNew     = pressure + relaxation.pressure * p'                         :615-618
14  velocityNew     = correctVelocity(velocityStar, dU, dV, p', pressureBoundaries,
                                      gradientScheme, dW)                         :620
15  fluxNew         = correctFaceMassFlux(predictorFlux, pAssembly->faceCoefficient, p',
                                          N > 1 ? &explicitFaceFlux : nullptr)    :623
16  allFinite(velocityNew, pressureNew, fluxNew) or NonFiniteState                :627
17  residuals       uResidual = uResult.initialResidual                           :638
                    vResidual = vResult.initialResidual
                    pResidual = pResult.initialResidual
                    wResidual = wResult->initialResidual   (3D only)              :650
18  continuity      evaluateContinuity(mesh, fluxNew)                             :641
                    continuityResidual = rms(cellImbalance)
                    globalImbalance    = |globalNetFlux|
19  COMMIT          velocity = velocityNew; pressure = pressureNew; massFlux = fluxNew  :653-655
20  bookkeeping     residual histories, finalXResidual, iterations, progress callback
21  verdict         monitor.record(...) -> Converged / Diverging / Stagnated / continue
```

### Things that are easy to get wrong, and are therefore called out

* **The momentum solve is warm-started from the previous iterate**; the pressure solve is **not**
  (zero guess). Getting either backwards changes `initialResidual`, which *is* the reported
  residual — not the final residual. A warm start is not merely an efficiency choice here.
* **`uResidual` is `initialResidual`, not `finalResidual`.** The source comment is explicit that
  using the final residual would falsely report convergence after one iteration.
* **Stage 9 uses `velocityStar`** (the just-solved predictor) but **`pressure` from the start of the
  iteration** — the pressure gradient is the one the momentum equations used, not the updated one.
* **Stage 14 corrects `velocityStar`**, not `velocity`.
* **Stage 15 corrects `predictorFlux`**, not `massFlux`.
* **`relaxation.pressure` multiplies p' in the pressure update only** — the velocity and flux
  corrections use the unrelaxed p'.
* The explicit face flux is passed to stage 15 **only when `nonOrthogonalCorrections > 1`**, matching
  the assembly that produced it.
* `enableGpuResidency` mirroring (:361, :545) is explicitly **read-only** and never influences the
  result; it is not part of the numerical path.

## 3. BC enforcement and caches

There is **no separate BC-enforcement stage**. Boundary conditions enter through the operators
themselves (momentum assembly, gradient, face flux, pressure-correction assembly). Nothing is
re-applied to the committed fields afterwards — confirmed for the velocity correction in
GPU-DISC-001J and true of the iteration as a whole here.

State carried to the next iteration is exactly three fields — `velocity`, `pressure`, `massFlux` —
plus the monitor's history and the turbulence model's internal state. Nothing else persists:
`velocityStar`, `predictorFlux`, `dU/dV/dW`, `p'` and both assemblies are iteration-local.

## 4. What this gate can and cannot compare bitwise

Stages 1–4, 8–10 and 13–18 are compositions of operators already qualified **bitwise**
(001B–001K). Stages 5, 11 and 12 are **linear solves**, qualified separately under GPU-PCORR-001
and inherently iteration-count dependent.

So the gate is built in two layers:

* **A controlled ladder** in which both paths consume the *same* solver outputs. Every stage that
  is a discretization operator is then compared **bitwise**, and the only inputs that differ are
  none. This is what qualifies the integrated discretization path.
* **An independent-solver ladder** in which each path runs its own solver, compared with existing
  project tolerances. This is what shows the composition survives real solver variability.

The known **GPU BiCGSTAB restart asymmetry** is accounted for by reporting restart counts and
classifying any non-convergence against it, never by relaxing a tolerance. It is not fixed here.

## 5. Fidelity of the ladder

The brief forbids a simplified SIMPLE-like harness where a production path exists. The production
path is `SIMPLE::solve`, but it exposes only the committed end state — not the 11 intermediate
stages this gate must compare.

The harness therefore does both, and ties them together:

1. it walks the stages above by calling the **same production functions in the same order**, which
   gives per-stage visibility; and
2. it asserts that the ladder's committed state is **bitwise identical** to
   `SIMPLE::solve(maxIterations = 1)`'s `velocity`, `pressure`, `massFlux` and residuals.

Step 2 is what makes step 1 legitimate: if the ladder ever stopped being the production iteration,
that assertion fails. A lookalike harness that was never checked against the real solver is exactly
what the brief warns against, and this check is the difference.
