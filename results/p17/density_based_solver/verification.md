# P17.8 — the measurements

Every number here is copied from a preserved log. Nothing is estimated, rounded for presentation, or
carried over from an earlier run. The criterion ids are the frozen gate's.

```text
AUTHORITATIVE -- run against the FINAL source, after every fix in this file:
logs/16_focused.log              the focused unit suite, Release+CUDA   75 / 75
logs/15_canonical.log            the canonical integration cases         4 / 4
logs/17_controls.log             the seven non-vacuity controls          0 unexpected
logs/18_fingerprint.log          the P13-001 SIMPLE fingerprint + its non-vacuity demonstration
logs/19_structural.log           the structural criteria                 0 failures
logs/20_format_verified.log      the formatting proof and the CI format check
logs/07_r2_reference.log         the independent 50-digit two-cell reference
logs/50_p17_8_regress.log        the full regression

SUPERSEDED but PRESERVED -- earlier runs, kept because they are where the defects were found:
logs/08_focused.log, logs/09_canonical.log, logs/10_controls.log, logs/06_structural.log,
logs/11_fingerprint.log, logs/12_format.log, logs/13_format_verified.log,
logs/14_postformat_*.log, logs/PRESERVED_preformat_09_canonical.log,
logs/PRESERVED_aborted_50_regress.log, logs/PRESERVED_attempt1/
```

---

## 1. §45 — the quantitative report the authorization asks for

```text
quantity                         value                            case
---------------------------------------------------------------------------------------------------
minimum rho                      0.972666 kg/m^3                  low-Mach Ma = 0.2, closed box
                                 0.2500   kg/m^3                  advected contact (the contact itself)
minimum p                        77822.7  Pa                      low-Mach Ma = 0.2
minimum T                        277.157  K                       low-Mach Ma = 0.2
maximum Mach                     ~2.02                            C4 smooth pulse
                                 ~2       (by construction)       C3 advected contact
CFL                              0.4 throughout, except the       all canonical cases
                                 deliberate 50 and 200 in P2/P3
minimum dt tested                6.716882e-08 s                   K6 "tiny cells"
maximum dt tested                6.716882e-03 s                   K6 "large cells"
mass conservation error          8.310e-16 relative               G1 closed box, 80 cells, 37 steps
                                 3.816e-17 absolute residual      G2 open domain, 40 steps
momentum conservation error      1.332e-14 absolute residual      G2 open domain
energy conservation error        1.696e-16 relative               G1 closed box
                                 3.365e-11 absolute residual      G2 open domain
uniform-state drift              0.000e+00                        C1 grid-aligned, 23 steps
                                 0.000e+00                        C2 oblique (u, v, w all non-zero)
contact pressure error           7.181e-16                        C3, detection bound 1e-9
contact velocity error           6.496e-16                        C3, detection bound 1e-9
smooth-case L1 error             1.038e-02 / 5.993e-03 / 3.255e-03  C4 at nx = 64 / 128 / 256
smooth-case observed order       0.7928 then 0.8804               C4, REPORTED not gated
residual reduction               see M4 below                     LocalPseudoTime
iteration count                  354 (converged) / 4000 (blocked) M4
fallback activations             0 -- THERE IS NO FALLBACK FLUX   all cases
rejected steps                   0 in every valid case; 1 in P3   deliberate CFL 200, maxRetries 0
retry count                      0 in every valid case; 5 in P2   deliberate CFL 50
non-finite detections            0 in every valid case            P5 exercises the detector directly
CPU/GPU differences              NOT MEASURED -- no GPU solver    see cpu_gpu_equivalence.md
```

## 2. Group Q — state and conversion

```text
Q1  round trip: rho and rhoE BITWISE; worst relative T error 0.000e+00, p error 0.000e+00
Q2  EOS / sound-speed / gamma-arithmetic occurrences in the solver: 0
    sanity controls, which MUST be non-zero or the 0 above is blind:
      HllcFlux.hpp 1     IdealGasEOS.cpp 8     TotalEnergy.cpp 3
Q3  makePrimitiveStateFromConserved 1 site, makePrimitiveState 1 site, toConserved 1 site
Q4  primitive states stored beside Q: 0   (sanity: 4 other members match the pattern)
```

## 3. Group R — the residual

```text
R1  BITWISE re-accumulation, 12 cells x 5 components over 17 internal faces: ALL EXACTLY EQUAL
R1  two-cell closed box    sum of mass residual -5.684342e-14, energy -2.235174e-08
    12-cell closed box     sum of mass residual  9.947598e-14, energy  1.266599e-07
    bound 2.854e-11
R4  uniform state: worst |R_rho| 0.000e+00, worst |R_E| 0.000e+00   (bound 2.203e-07)
```

### R2 — against a reference computed outside CFDApp

`tools/two_cell_residual_reference.py`, mpmath at 50 digits, no CFDApp code. Written from the
Rankine–Hugoniot derivation, **not transcribed from `HllcFlux.hpp`** — and it refuses to emit a value
unless the star flux satisfies `F*_energy == S*(rhoE* + p*)`, the property that would have caught
P17.7's sign error. That check held on every star branch of every face.

```text
cell 0   R_rho   128.90158456479763      R_rhoU  44412.801819919674
         R_rhoV  0.0                     R_rhoW  0.0
         R_rhoE  41283940.377882551
cell 1   R_rho  -128.90158456479763      R_rhoU -69569.745295403153
         R_rhoV  0.0                     R_rhoW  0.0
         R_rhoE -41283940.377882551

the 8 eps bounds, against sum_f |F_f| A per component -- NOT against |R| itself, because a residual
is a sum of four face terms that largely cancel and the round-off floor is set by the TERMS:
  cell 0   2.28975e-13, 3.93085e-10, 4.56873e-10, 0.0, 7.3335e-8
  cell 1   2.28975e-13, 3.48397e-10, 2.56991e-10, 0.0, 7.3335e-8

The rhoW scale is EXACTLY 0 -- a 2D mesh has no z flux at all -- so that component's bound is 0 and
the criterion there is exact equality. A dyadic control in the sense of the gate's section 5.1.
CFDApp met every one of the ten bounds.
```

## 4. Group K — the CFL

```text
K1  dt(CFL=0.2) = 1.291334e-04   dt(CFL=0.4) = 2.582668e-04   ratio EXACTLY 2
K2  base 2.6868e-04  ->  10x velocity 1.6745e-04
                          4x temperature 1.3901e-04
                          4x refinement  6.7169e-05          each strictly lower
K4  P17.8 dt (CFL = 0.35)              2.382920340e-04
    advective fraction of the speed    0.054466751
    expected Co = (CFL/2) x fraction   9.531681361e-03
    solver::calculateCFL reports       9.531681361e-03
    relative difference                0.000e+00   (bound 1e-12)
K5  4 invalid CFLs plus maxIterations / retryFactor / endTime each refused by name
K6  zero velocity 7.200533e-05   Mach ~3 2.882853e-05   tiny cells 6.716882e-08
    large cells   6.716882e-03   low pressure 7.098310e-05   cold 5.017775e-04
```

## 5. Group U — the update

```text
U1  graded mesh, 6 cells, volume 8.944544e-02 .. 3.618962e-01 (ratio 4.046 -- and no cell is 1.0)
    update BITWISE equal to Q - (dt/V) R for all 5 components
    1/V dependence: worst relative error in (dQ_small/dQ_big) vs (V_big/V_small) within 64 eps
U2  local dt range [2.1462e-04, 2.6424e-04]; global dt = the minimum, EXACTLY
U3  LocalPseudoTime refuses physicalTime(); GlobalPhysical reports 1.000000e-05 s
```

## 6. Group C — the canonical cases

```text
C1  uniform, grid-aligned, 23 steps: worst relative drift, ALL FIVE quantities the criterion names
      rho 0.000e+00   rhoU 0.000e+00   rhoE 0.000e+00   p 0.000e+00   T 0.000e+00
      (bound 64 eps = 1.421e-14)
C2  uniform, OBLIQUE (u, v, w all non-zero), 23 steps: worst drift 0.000e+00, p 0.000e+00,
    T 0.000e+00
C3  advected contact, 23 steps at Mach ~2:
      pressure non-uniformity  7.181e-16      (detection bound 1e-9)
      velocity non-uniformity  6.496e-16      (detection bound 1e-9)
      density range [0.2500, 1.0000] -- the contact itself, which DOES smear, as expected
C4  smooth advected pulse, Ma = 2.02, t_end = 3.000e-04 s
      nx    dx           L1(rho)        max|dp|/p      max|du|/u      steps
      64    1.5625e-02   1.038274e-02   2.393917e-15   8.120488e-16   134
      128   7.8125e-03   5.993063e-03   2.675554e-15   9.744586e-16   268
      256   3.9062e-03   3.255498e-03   7.322569e-15   1.948917e-15   536
      observed order   64 -> 128 : 0.7928       128 -> 256 : 0.8804
```

```text
C4 IS NOT A SHOCK RESULT AND IS NOT OFFERED AS ONE. It is a smooth entropy wave -- uniform p,
uniform u, a Gaussian in rho -- which is an EXACT Euler solution that advects rigidly. p and u are
preserved to ROUND-OFF (1e-15), which is the strong half of the result and is what a scheme that
smeared pressure into a density variation would fail. The ~0.9 order is the first-order scheme's
numerical diffusion on the density profile, trending toward 1 under refinement. No formal
order-of-accuracy claim is made: there is no Richardson extrapolation and no GCI here.

THE FIRST DRAFT OF C4 WAS WRONG AND IS PRESERVED IN logs/PRESERVED_preformat_09_canonical.log's
predecessor: it held TEMPERATURE constant while varying density, which makes PRESSURE vary -- an
acoustic initial condition, not an entropy one, with no closed-form reference. It measured 8.5 %
pressure non-uniformity and an L1 error that barely fell under refinement. The defect was in the
test, not the solver.
```

## 7. Group G — conservation

```text
G1  closed domain, 80 cells, 37 steps:
      mass    8.0161733969e-01 -> 8.0161733969e-01   relative error 8.310e-16
      energy  1.7161788150e+05 -> 1.7161788150e+05   relative error 1.696e-16
      bound 8 eps N = 1.421e-13
G1  the THIRD quantity, momentum, as a BALANCE (same box, same 37 steps, 36 wall faces):
      momentum-x  d(total) +2.8641555536e+01  + wall impulse -2.8641555536e+01  -> 1.066e-14
                  (bound 2.610e-08)
      momentum-y  d(total) +1.2764285254e-15  + wall impulse -1.2493348824e-15  -> 2.709e-17
      mass    relative 5.540e-16      energy  relative 3.392e-16
G1  the same on a GRADED mesh (volume ratio > 2), which is what makes the criterion sensitive to a
    missing 1/V at all -- on a uniform mesh the constant volume factors out of both sides.
G2  open domain, 60 cells, 46 boundary faces, 40 steps:
      (i)  worst |sum_P R_P - boundary flux| / |boundary flux|   6.376e-16
      (ii) mass      d(total) +3.3803615524e-03 + integral -3.3803615524e-03 -> 3.816e-17  (bound 5.509e-13)
           momentum  d(total) +2.7193294163e+00 + integral -2.7193294163e+00 -> 1.332e-14  (bound 3.598e-10)
           energy    d(total) +1.8024890687e+03 + integral -1.8024890687e+03 -> 3.365e-11  (bound 2.401e-07)
WALL  mass flux 2.842171e-14 (bound 6.115e-12), energy flux 7.450581e-09,
      momentum.x 1.713575e+05 against p = 1.102272e+05 -- the pressure force is RETAINED
```

```text
WHY G1's MOMENTUM IS A BALANCE AND NOT A CONSTANT. The closed box starts with a non-uniform pressure
field, so the walls exert a net force and total x-momentum genuinely moves, 0 -> 28.64. Asserting
"unchanged" would assert something FALSE, and picking a case where it happened to hold would be
fitting the case to the criterion.

The first pass explained that and stopped, which left the frozen Group G's third quantity measured
by nothing -- the test computed the momentum and only printed it. The balance is now asserted, and
it closes to 1.066e-14 against a change of 28.64: the entire momentum change is accounted for by the
integrated wall force. Controls N1 (drop the neighbour flux) and N6 (stop mirroring the wall normal
velocity) both break it.
```

## 8. Group M — norms and convergence

```text
M2  max residual norm: 8x8 0.000e+00, 32x32 0.000e+00   (uniform state; mesh rate scale 3.333e+03)
M3  reference scale over 40 iterations: worst implied-vs-iteration-0 drift within 8 eps,
    and the relative residual MOVES (so "captured once" is not vacuously true)
M4  all three loose            -> Converged after 354 iterations
    blocking residual norms    -> MaxIterations after 4000
    blocking mass imbalance    -> converged after 1548      (all-loose took 354)
    blocking state change      -> converged after 2231      (all-loose took 354)
M5  a transient run that reached endTime reports TimeComplete, NOT Converged
M6  the SAME state and tolerances: LocalPseudoTime Converged; GlobalPhysical never reports Converged
```

```text
M4's FIRST DRAFT PROVED NOTHING, and the note is kept because the shape recurs: it set
massImbalanceTolerance to 1e-300 on a UNIFORM state and expected convergence to be blocked. But a
uniform state's mass imbalance is EXACTLY 0, and 0 <= 1e-300 is true -- the "impossible" tolerance
was satisfied. A signal can only be shown to be consulted on a case where that signal is NON-ZERO.
```

## 9. Group P — robustness

```text
P1/P3  the refusal names the cell: "DensityBasedSolver::toPrimitive: cell 1 is not a valid
       thermodynamic state: TotalEnergyState::recover: density"
P3     terminal failure, CFL 200 with maxRetries 0: offendingCell field = 5, and the diagnostic
       quotes the same cell. rejectedSteps 1, totalRetries 0.
P2/P6  CFL = 50 (deliberately unstable): status TimeComplete, 5 iterations, 5 retries, 0 rejected
       steps; the repeat made IDENTICAL control-flow decisions
P4     an absurd-but-positive density (1e-30) is REFUSED, never silently replaced;
       0 clamp idioms in the solver (sanity: the pattern matches a synthetic clamp line)
P5     every enumerated quantity detected naming its cell -- see implementation.md section 8
```

## 10. Group D — determinism (gate criterion G9)

```text
D1  20 steps: dt history, time history, all five residual histories, the full state and the retry
    history ALL BITWISE IDENTICAL across two runs (EXPECT_EQ, no tolerance)
D2  the retry path (P2) made the same decisions on the repeat
```

```text
NON-VACUITY, honestly scoped. G9 asks that the determinism check be shown able to fail. The
controls demonstrate that the SUITE detects planted changes in the solver's arithmetic, but no
deliberately order-dependent reduction was injected into THIS solver, because it contains no
reduction whose order could be varied: every loop is a serial, cell-id-ordered accumulation.

  `pragma omp` occurrences in DensityBasedSolver.{hpp,cpp} : 0
  SANITY, so that 0 means absence rather than a blind grep  : src/algebra/SparseMatrix.cpp has 1
  (the first sanity control tried here was SIMPLE.cpp, which ALSO has 0 -- it would have made the
   check vacuous, and is recorded rather than quietly swapped)

So the determinism claim is stated exactly this narrowly: REPRODUCIBLE on a single-threaded host
path that contains no parallel reduction to be non-deterministic about. It is NOT a demonstration
that the D1 instrument would catch a non-deterministic reduction, because this solver has none to
plant one in. A GPU or OpenMP port (P17.10) must supply that demonstration itself.
```

## 11. §43 — the focused suite

```text
Release+CUDA, OMP_NUM_THREADS=1, ctest -R 'Hllc|DensityBased'   (logs/16_focused.log):
  run 75   passed 75   failed 0   skipped 0
    of which P17.8 (DensityBased*)                    36
    of which P17.7 (Hllc*), re-run after the sign fix 39

canonical integration binary, run directly   (logs/15_canonical.log):
  run 4    passed 4    failed 0   skipped 0

The 36 P17.8 rows are 32 in tests/unit/compressible/test_density_based_solver.cpp plus 4 in
tests/integration/compressible/test_density_based_canonical.cpp; the latter are registered under
DensityBasedCanonicalIntegration and so are counted by the same ctest filter.
```

Focused-suite coverage against §43's list:

```text
primitive/conserved conversion   Q1                                    PASS
HLLC                             39 P17.7 rows, re-run                 PASS
residual assembly                R1 (x2), R2, R4                       PASS
owner/neighbour conservation     R1 bitwise                            PASS
CFL                              K1, K2, K4, K5, K6                    PASS
global stepping                  U2, M5                                PASS
local stepping                   U3, M4, M6                            PASS
positivity                       P1, P2, P3 (x2), P4, P6               PASS
nonfinite detection              P5                                    PASS
uniform flow                     C1, C2                                PASS
smooth flow                      C4                                    PASS
moving contact                   C3                                    PASS
boundary conservation            G2, G1 momentum balance               PASS
restart                          S1, S2                                PASS (refusal verified)
determinism                      D1                                    PASS
CPU/GPU solver                   --                                    NOT IMPLEMENTED
```
