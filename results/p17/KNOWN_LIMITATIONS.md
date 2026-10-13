# P17 — known limitations

Every entry is supported by evidence in `results/p17/`. Nothing speculative is listed, and nothing
known is omitted because P17 otherwise passes.

**A limitation is not a future phase.** §33 asks these to be kept apart, so §9 lists deferred scope
separately — that section is about work nobody has done, this one is about what the shipped code
does not do.

---

## 1. The one open defect

```text
L1  HLLC's wave-speed estimate does not bound a strong head-on collision.
    Above a normal Mach number of ~1.76 for a symmetric collision -- which an inviscid wall produces
    under supersonic normal flow -- the ordering S_L <= S* <= S_R inverts, the star densities go
    NEGATIVE, HLLC selects the supersonic branch and returns the UNDISTURBED UPSTREAM FLUX, and
    hllcFlux() still reports valid = true.
      Ma 2.02: estimated S_L = +58.0 against an exact -264.9; exact p*/p1 = 8.84.
    STATUS   OPEN. Not fixed: a wave-speed estimator is a numerical-scheme change needing its own
             frozen gate, and P17.11 is a qualification phase.
    IMPACT   G6 = PARTIAL. The density-based solver REFUSES loudly (hllcFluxChecked throws and
             solve() returns NonFiniteState naming the cell), so it does not answer wrongly. A
             direct caller of hllcFlux() gets no warning at all.
    REACH    any case with a supersonic flow normal to an inviscid wall: a blunt body, a closed end,
             a forward-facing step. No existing qualified case is affected -- their walls are
             tangential.
    EVIDENCE results/p17/qualification/G6_POSITIVITY_FAILURE.md
             results/p17/cpu_gpu_equivalence/BLOCKING_FINDINGS.md
             reproducer: P17_10_Finding.DOCUMENTS_A_DEFECT_ToroWaveSpeedsDoNotBoundAHeadOnCollision
```

## 2. Numerical scope of the density-based solver

```text
L2  FIRST-ORDER RECONSTRUCTION ONLY. Piecewise-constant. The interface for MUSCL/WENO is defined and
    hllcFlux() need not change to accept one, but no higher-order reconstruction exists. Shock
    smearing is correspondingly wide: 7 cells for the Sod shock, 16 for the contact.
L3  EULER ONLY. No viscous stress and no heat conduction in the density-based path, and deliberately
    NO ZERO STUB -- a zero stub is indistinguishable from a wired-up term that happens to vanish.
    Compressible Navier-Stokes is not claimed.
L4  BOUNDARY CONDITIONS: three only -- inviscid wall, supersonic inflow, supersonic outflow. There
    is NO subsonic open boundary; that needs incoming-characteristic reconstruction to be well
    posed, which is its own verification surface. Every qualified case is built to need only these.
L5  AN UNMAPPED BOUNDARY PATCH IS REFUSED, not defaulted. This is a deliberate design property, not
    a gap, but callers must map every patch explicitly.
L6  RESTART IS UNSUPPORTED AND REFUSES. The existing restart format carries pressure, velocity and
    mass flux with no rho, rhoU or rhoE. throwIfRestartRequested() always throws.
L7  NO CASE-FILE REACHABILITY. The density-based solver is library-level: no dispatch change, no
    case-format key. It cannot be selected from a case file, so it is not reachable by a user.
L8  1D PROBLEMS ON 2D MESHES for all shock validation. 3D residual equivalence is measured
    (Cartesian 3D, 900 values bitwise), but no 3D shock structure is validated.
```

## 3. Thermodynamic and property scope

```text
L9   CALORICALLY PERFECT GAS ONLY. A temperature-dependent cp is REFUSED at construction with a
     named error, because ThermodynamicProperties carries a single cp from which it derives cv and
     gamma. A variable-cp energy state would need a variable-gamma EOS.
L10  k(T) CONDUCTION NOT IMPLEMENTED in the compressible energy path -- a variable-k flux needs a
     face-interpolated k, which is new discretisation.
L11  THE VISCOUS-WORK FLUX IS NOT IMPLEMENTED. P17.4 builds no stress tensor, so (tau.U).n cannot be
     formed exactly; substituting tau : grad(U) would silently switch to the internal-energy form of
     the equation. Reported by viscousWorkLimitation(); null terms are exactly 0.
L12  NO IMPLICITLY COUPLED rhoE SOLVE inside CompressibleSIMPLE. hTotal is nonlinear in rhoE through
     T, so an implicit form needs a Newton linearisation. What exists is the conservative flux form
     plus an explicit conservative advance.
L13  P15's SensibleEnthalpy MODEL IS NOT REACHABLE FROM A CASE FILE. Pre-existing, recorded in
     P17.5; rhoE inherits it.
```

## 4. Low-Mach scope

```text
L14  LOW-MACH CONDITIONING DEGRADES AS Ma FALLS, and it is measured rather than hidden:
       U0 1.0    inletMa 2.88e-03   iterations 1959 (compressible) vs 2031 (incompressible)
       U0 0.5    inletMa 1.44e-03              2261                     2450
       U0 0.25   inletMa 7.20e-04              2403                     2626
       U0 0.125  inletMa 3.60e-04              2439                     2680
     Iteration count rises as Mach falls. No preconditioner is implemented.
L15  MACH LEVELS ABOVE THE EXISTING ANCHOR ARE NOT COVERED -- blocked by an inherited iteration
     limit that the P17.6 authorization forbade raising.
L16  NO CLOSED TOTAL-ENERGY BALANCE on an isothermal low-Mach solve; impossible there, quantified.
L17  THE CAVITY IS NOT A MACH FAMILY. A closed domain has no through-flow and therefore no pressure
     drop to drive a density variation; it runs unchanged as a preservation case only.
```

## 5. GPU scope

```text
L18  GPU COVERS THE DENSITY-BASED SOLVER ONLY. There is no GPU CompressibleSIMPLE, no GPU
     total-energy equation, no GPU low-Mach path. G8 is claimed for the density-based solver and the
     HLLC flux, and for nothing else.
L19  RESIDUAL NORMS AND CONSERVATION SUMS ARE HOST-SIDE BY DESIGN. They are order-dependent sums; a
     parallel tree reduction cannot be bitwise equal to the CPU's sequential sum, and forcing the
     device reduction to be sequential would be engineering the answer. Recorded as a decision
     before implementation, not discovered later.
L20  THE RETRY / REJECTION CONTROL LOOP IS HOST-SIDE. The device reports the decision; the loop runs
     on the host.
L21  NO GPU RESTART. The CPU refuses; the refusal is preserved rather than worked around.
L22  --fmad=false IS REQUIRED for bitwise equivalence. With contraction enabled, 7 of 15 equivalence
     tests fail and trajectory divergence grows to 704 of 1025 values by step 50. This is a build
     requirement, not an optional flag.
L23  MESH RE-UPLOAD ALLOCATION BEHAVIOUR IS NOT MEASURED for the density-based solver (gate T4).
     DeviceBuffer's never-shrink contract is qualified elsewhere; this phase did not re-verify it.
```

## 6. Coverage gaps in the CPU/GPU qualification itself

```text
L24  C5 -- no run in which the CPU ACTUALLY RETRIES, with retry counts compared. The reject DECISION
     is compared (C4, three kinds, two offending cells each); a retrying run is not.
L25  D5 -- the normal-shock M=2 jump ratios were NOT measured on the GPU. The backends agree bitwise
     on Sod and the CPU's ratios are validated, but the GPU normal shock was not run.
L26  D6 -- the benchmark matrix ran 5 configurations, not the labelled A-G set.
L27  B3 -- boundary exterior states are covered through the face-flux comparison rather than
     compared per boundary face directly.
```

## 7. Diagnostic scope

```text
L28  NO CUDA KERNEL-LAUNCH TRACER on this machine (nsys and ncu both absent), so P17 CUDA kernel
     coverage is ARGUED FROM CALL SITES plus a launch-count cross-check (180 launches / 20 steps =
     9 per step, matching the nine production launches a step performs). It is not a traced
     measurement and is not presented as one.
L29  initcheck AND synccheck HAVE NO DEDICATED NON-VACUITY CONTROL. memcheck and racecheck each have
     one -- a deliberately faulty kernel they must name -- and both fire. initcheck and synccheck
     share compute-sanitizer's instrumentation, which is demonstrated active on this library, so
     their zeros are not baseless; they are weaker.
L30  SANITIZER SCOPE IS THE P17 GPU SUITES, not the whole program. Whole-program sanitizer coverage
     is not claimed.
```

## 8. Harness and evidence limitations

```text
L31  THE REGRESSION HARNESS REPORTED "Not Run : 0" IN EVERY P17 PHASE while 49 DISABLED_ tests were
     not running. Fixed in P17.11's harness, which counts them. No executed count was ever wrong.
     None of the 49 is a P17 test.
L32  THE qual-* BUILD TREES' CONFIGURATION EXISTS ONLY AS CMAKE CACHE STATE -- in no preset and,
     before P17.11, in no harness. Deleting a tree silently reconfigures it Debug/no-OpenMP, which
     cost P17.10 two regression re-runs and presented as three test failures. P17.11's harness now
     ASSERTS the four options, but the options are still not stored in the repository.
L33  THE GLOBAL STALENESS RULE fires on any CUDA-only source when the CPU-only configuration is
     checked, because it compares against the newest production source anywhere. It fails in the
     safe direction and costs a full rebuild.
L36  A DEBUG REGRESSION REWRITES TRACKED VALIDATION REPORTS WITH DEBUG-COMPUTED PHYSICS VALUES.
     results/validation/** is Release-generated. A Debug build writes l1/l2/linf and derived orders
     that differ by up to 4.1e-07 (raw norms) and 1.1e-05 (Richardson-extrapolated), because an
     iterative solve converges to a slightly different point under different optimization. Anyone
     running the Debug regression and committing the tree would silently replace validated Release
     numbers with Debug ones, and no harness warns about it. Attributed by experiment in
     results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md §7.
L37  FOUR PRE-P17 BACKWARD-FACING-STEP TESTS CANNOT COMPLETE UNDER DEBUG within the frozen 7200 s
     timeout: DriverSeegmiller.MeshSequence, .CorrectedSequenceIsDeterministic,
     DriverSeegmillerInlet.MappedInletPreservesTheMassFlow and .MediumGridAB. They pass in both
     Release configurations (1199-1883 s); Debug is >3.8x to >6.0x slower. This is why G10 is
     PARTIAL. The timeout was NOT raised.
L34  NATIVE WINDOWS IS NOT QUALIFIED. Everything is WSL2 Ubuntu-22.04. A Windows MSVC build tree
     exists but is not part of this qualification.
```

## 9. Deferred scope — NOT limitations of the shipped code (§33)

```text
These are things nobody has built, distinct from things the built code cannot do:

  higher-order shock reconstruction (MUSCL/WENO)     a future phase
  low-Mach preconditioning                           a future phase
  full 3D compressible parity                        a future phase
  multi-species / reacting flow                      P18, NOT AUTHORIZED
  multiphase compressible                            P19, NOT AUTHORIZED
  advanced SST                                       P20, NOT AUTHORIZED, deliberately not reopened

None of these blocks P17: the original P17 contract does not require them.
```

## 10. Performance

```text
L35  NO PERFORMANCE CLAIM IS MADE, and none of the gates requires one. Measurements, where taken,
     are recorded under results/p17/performance/ and are kept out of every correctness gate.
     GPU speedup is NOT claimed.
```
