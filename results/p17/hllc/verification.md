# P17.7 — verification: the measured results

Consolidates what §36 suggested as `analytic_tests.md`, `star_states.md`, `positivity.md`,
`contact_wave.md`, `discontinuity_tests.md`, `rotational_consistency.md` and `determinism.md`.

Sources: `logs/03_hllc_measured.log` (33 CPU tests), `logs/04_gpu_attempt1.log` (4 GPU tests),
`logs/01_reference.log` (the independent 60-digit reference), `logs/06_controls.log` (Group V).

`eps = 2.2204460492503131e-16`. `kTol = 32 eps = 7.105e-15`, derived in `acceptance_gate.md` §3.

---

## 1. THE HEADLINE TABLE — every derived threshold, and what was measured

```text
criterion                                       bound          measured        margin
---------------------------------------------------------------------------------------------
F3  physical flux vs the reference              8 eps          1.114e-17         160x
C1  uniform supersonic == F_L / F_R             BITWISE        BITWISE            --
C2  uniform subsonic consistency                32 eps         0.000e+00       exact
W1  wave speeds vs the reference                8 eps          2.172e-16         8.2x
W2  S_L < S* < S_R                              boolean        15 of 15           --
W3  |denom| / (rho_L a_L + rho_R a_R)           >= 1           1.000000000000000  attained
W4  the estimator BOUNDS the true signal speeds boolean        14 of 14           --
B1  S_L >= 0 returns F_L                        BITWISE        BITWISE            --
B2  S_R <= 0 returns F_R                        BITWISE        BITWISE            --
B3/B4 star branches vs the reference            32 eps         1.833e-16         39x
S1  rho* > 0                                    boolean        min 1.314130e-05   --
S3  star internal energy > 0                    boolean        min 1.977800e+00   --
S4  rho* vs the reference                       32 eps         2.220e-16         32x
T1  stationary contact mass/energy flux          BITWISE 0      exactly 0          --
T3  tangential shear mass / tang. momentum       BITWISE 0      exactly 0          --
R1  dyadic rotation                              BITWISE        BITWISE            --
R2  oblique normal under rotation                32 eps         1.421e-14      179x (scaled bound)
R3  area scaling / wave speeds                   BITWISE        BITWISE            --
N1  F(L,R,n) = -F(R,L,-n)                        BITWISE        BITWISE, 15 cases  --
X1  == the exact Godunov flux where HLLC is exact 32 eps        3.254e-16         22x
X2  == the 60-digit HLLC everywhere              32 eps         3.254e-16         22x
G2  CPU vs GPU                                   BITWISE        17/17 rows, diff 0 --
G4  determinism, CPU and GPU                     BITWISE        BITWISE            --
```

Every bitwise claim was **derived before measurement** (see the gate's derivations for C1, T1 and N1)
and then held. The one derivation that proved *conservative* is C2: a 32 eps bound was derived and the
measurement came out exactly 0. A bound the measurement beats is fine; a bound it violates would have
been a failure.

## 2. §16 — the analytic tests, case by case

```text
A  equal-state consistency          F_HLLC(Q,Q,n) == F(Q,n)
   6 thermodynamic states x 4 normals (x, y, z, oblique (2,-3,6)/7) = 24 evaluations
   supersonic:  BITWISE (2 selections)      subsonic: 0.000e+00
B  zero velocity / uniform pressure  the stationary contact IS this case: u = 0, p_L = p_R
   mass flux EXACTLY 0, energy flux EXACTLY 0, momentum flux EXACTLY p n
C  pure x-direction                  F1, BITWISE against rho u, rho u^2 + p, rho u v, rho u w,
                                     u(rhoE + p)
D  pure y-direction                  F2, BITWISE permutation; plus sod_y in the reference table
E  pure z-direction                  F2, BITWISE; plus sod_z. The state carries three velocity
                                     components because Vector2 is an ALIAS of Vector3, so the flux
                                     is verified in full 3D even though the MESH is 2D
F  supersonic right-going            branch F_L, flux BITWISE == eulerFlux(left)
G  supersonic left-going             branch F_R, flux BITWISE == eulerFlux(right)
H  stationary contact                see section 4
```

## 3. §13 — all four branches, entered deterministically

```text
case                     branch      reference agrees
uniform_x                F*_L        yes
uniform_oblique          F*_L        yes
sod_literature           F*_L        yes
sod_atmospheric          F*_L        yes
sod_y / sod_z            F*_L        yes
stationary_contact       F*_L        yes
moving_contact           F*_L        yes
tangential_shear         F*_L        yes
supersonic_right         F_L         yes
supersonic_left          F_R         yes
strong_pressure_ratio    F*_L        yes
strong_density_ratio     F*_L        yes
near_sonic_left          F_L         yes
low_pressure             F*_L        yes
subsonic_left_moving     F*_R        constructed in the test
---------------------------------------------------------
coverage:  F_L = 2   F*_L = 12   F*_R = 1   F_R = 1
```

`F*_R` does not occur anywhere in the reference table — every reference case is left-going or
symmetric — so it is **constructed explicitly** (both states moving left at 200 m/s with a density
jump, putting `S* < 0 < S_R`). §13 says *"Do not leave branch coverage accidental"*, and each of the
four has an `EXPECT_GT(count, 0)` behind it so a branch silently ceasing to be exercised fails the
test rather than passing quietly.

## 4. §12 and §21 — the contact wave

This is the reason HLLC exists rather than HLL, and it is the criterion with the strongest result.

```text
STATIONARY CONTACT   rho_L = 1.0, rho_R = 0.125, u = 0, p_L = p_R = 101325
  S*           = -0            (exactly zero; the sign is IEEE's, see wave_speeds.md section 5)
  mass flux    = 0             EXACTLY. Not "small" -- bitwise zero.
  energy flux  = 0             EXACTLY
  momentum flux= p n           EXACTLY -- no spurious pressure jump
  rho*_L       = rho_L, rho*_R = rho_R, both exactly

DERIVED IN ADVANCE: p_L = p_R makes the numerator's pressure term exactly 0; u_n = 0 makes both
velocity terms exactly 0; so S* = 0/denom = 0 exactly, rho*_L = rho_L (S_L-0)/(S_L-0) = rho_L exactly,
and the mass flux is 0 + S_L*0 = 0 exactly. BITWISE is therefore the CORRECT requirement here, and any
tolerance would have been a concession.

MOVING CONTACT       the same with u_nL = u_nR = 50 m/s
  S*           = 50            exactly the common normal velocity
  flux vs the 60-digit reference: worst |diff|/scale = 0.000e+00

TANGENTIAL SHEAR     u_n = 0, p equal, tangential velocity 20 -> -35 m/s (and z: -5 -> 11)
  mass flux              = 0   EXACTLY
  TANGENTIAL momentum flux= 0  EXACTLY -- no mass crosses, so no tangential momentum is advected
  normal momentum flux   = p n EXACTLY
  energy flux            = 0   EXACTLY
```

§21's decomposition `U = u_n n + U_t` is what the star state expresses as
`U*_K = U_K + (S* - u_nK) n`: the normal component is replaced by `S*` and everything orthogonal to
`n` is preserved exactly. Control **V3** takes the tangential part from the opposite side and is
detected by T3 — the test written for precisely that defect — plus B3/B4 and X2.

### The non-vacuity evidence, quantified in physical units

```text
with the contact wave DELETED (i.e. HLL), on the stationary contact:
  spurious mass flux  =  243.4595971727 kg/(m^2 s)     where the true flux is EXACTLY 0
  the 60-digit reference predicts                       243.4595971727
  HLLC itself gives                                     0
and on the 1e6 density-ratio contact:
  spurious mass flux  =  11897.6099070769 kg/(m^2 s)
while the UNIFORM-state consistency test still passes PERFECTLY (0.000e+00).
```

That is exactly the structure the frozen G6 criterion names as its non-vacuity backbone: *"a
deliberately dropped contact-wave term must break (f) while still passing (a)"*. Measured on CPU and,
via `V2_ContactWaveDeletionBreaksTheContactOnDeviceToo`, on GPU as well — so a device path cannot
silently be a different scheme.

## 5. §14 — positivity, with the three levels kept apart

§14 warns against the phrase "preserve positivity" and demands the distinction be documented.

```text
CLAIMED, and verified
  (1) ADMISSIBLE LEFT/RIGHT STATES. Every input is validated: rho > 0, p > 0, finite rho/p/U/rhoE,
      finite and positive a, and a genuine UNIT normal. 11 inadmissible states + 5 invalid normals
      + 2 invalid gammas each refused with a typed reason and a named message.
  (2) ADMISSIBLE STAR STATES. PROVEN via the denominator bound (wave_speeds.md section 4) and TESTED
      over every case:
         minimum rho*        1.314130e-05   (the 1e6 density-ratio case)
         minimum e*          1.977800e+00
         all star components finite, every case
      including the extreme set: pressure ratio 1e5, density ratio 1e6, Mach ~2.4, p down to 1e-3.

NOT CLAIMED
  (3) A POSITIVITY-PRESERVING TIME UPDATE. A single flux evaluation cannot guarantee positivity of
      the full update: that depends on the CFL number and on the reconstruction, neither of which
      exists in P17.7. It belongs partly to P17.8. No wording anywhere in this phase's evidence
      claims it, and the TODO entry uses [~] for the positivity item for this reason.
```

```text
NOTHING IS CLAMPED AND NO FLOOR IS APPLIED. There is no fallback flux, because the denominator is
provably non-degenerate, so there are also no fallback-activation counters -- criterion S5 asks for
them to read zero and is reported NOT APPLICABLE with that reason rather than as satisfied.
hllcFluxChecked() still checks the star states and throws NumericalError describing a violation as a
DEFECT IN THIS LAYER, because under the proof it could only be that. A proof that is never tested is
an assumption.
```

## 6. §19 and §20 — rotational and directional consistency

```text
R1  DYADIC rotations -- exactly representable, so G6(d) requires BITWISE and gets it:
      z90  (x,y,z)->(-y,x,z),  normal (1,0,0)->(0,1,0)    mass, energy, S_L, S*, S_R all BITWISE
      z180 (x,y,z)->(-x,-y,z), normal (1,0,0)->(-1,0,0)   mass, energy BITWISE; momentum rotated
      y90  (x,y,z)->(z,y,-x),  normal (1,0,0)->(0,0,-1)   mass, energy BITWISE
    and the momentum flux equals the ROTATED x-flux bitwise, not merely a permutation of magnitudes.
R2  an OBLIQUE normal (2,-3,6)/7 -- |n| = 1 exactly, since 2^2+3^2+6^2 = 49 -- under a general axis
    cycle: mass diff 1.421e-14 against the scaled bound 2.548e-12. Not bitwise, because the rotation
    itself is inexact; the derived 32 eps applies and holds.
R3  AREA: 0.25, 1, 4, 1024 scale the integrated flux EXACTLY, and S_L/S*/S_R are BITWISE unchanged.
    Wave speeds depend on the unit-normal velocity, never on area magnitude -- which is the confusion
    §20 warns about, and control V6 plants it.
```

## 7. §33 — conservation

```text
N1  F(Q_L, Q_R, n)  ==  -F(Q_R, Q_L, -n)      BITWISE, all 5 components, 15 cases
    and on the wave speeds too: S_L' = -S_R and S_R' = -S_L, bitwise.

DERIVED IN ADVANCE: negating a double is exact and min/max of negated arguments maps exactly onto
max/min, so every term carries a single exact sign flip in the SAME operation order. BITWISE is the
correct requirement, not round-off.

The synthetic two-cell harness §44 permits (one flux evaluation, owner gains -F*A and neighbour +F*A):
    mass sum   = 0    exactly
    energy sum = 0    exactly
    momentum sum = 0  exactly
so P17.8 can update both cells from one evaluation with no conservation error at all. It is NOT a
solver and does not advance in time.
```

## 8. §17 and §18 — the discontinuity states, REPORTED

```text
Sod's initial state: rho 1.0/0.125, p 1.0/0.1, u = 0, gamma = 1.399818928894769831

  S_L    = -1.1831394376381720
  S*     =  0.67616713174314473
  S_R    =  1.7520928142199601        <- the pressure-based estimate encloses the true shock
  rho*_L =  0.63633370479181441
  rho*_R =  0.29171697937952814
  F      = [0.43026793600057014, 0.49093303616654848, 0, 0, 0.30167470754041015]
  the exact solution's leftmost / rightmost waves: -1.1831394376381720 / 1.7520928142199601
  S_L < S* < S_R            yes
  both star densities > 0   yes
  energy flux finite        yes
```

### The scheme's own error against the exact solution — characterised, NOT gated

```text
                        |diff| / scale
case                    mass      x-momentum   energy
sod_literature          0.0061    0.0609       0.2961
sod_atmospheric         0.0061    0.0609       0.2961   <- IDENTICAL to the dimensionless case
strong_pressure_ratio   0.0768    0.1026       0.3177
low_pressure            0.0301    0.0617       0.2565
```

The two Sod rows agreeing to every digit is a self-check on the reference: the relative error must be
invariant under pressure scaling, and it is.

```text
WHY NO ACCURACY TOLERANCE IS PLACED ON THESE.
G6(f) requires the tolerance to be "derived from the wave-speed estimator's own truncation behaviour,
not from the observed error". Measured at 60 digits, that truncation behaviour for a strong
discontinuity is O(1) -- a single HLLC interface flux is simply not close to the exact Godunov flux
when the jump is strong, and no tight bound is derivable. Fitting a tolerance to the observed 0.0061
would be exactly the "derive from the observed error" the criterion forbids.
So the DERIVABLE parts of (f) are gated:
    X1  HLLC == the exact flux where it is ALGEBRAICALLY exact       3.254e-16 over 8 cases
    X2  HLLC == the 60-digit HLLC everywhere (implementation error)  3.254e-16
    W4  the estimator BOUNDS the true signal speeds                  14 of 14
and the scheme's approximation error is reported above.
§18 also says so in as many words: "Do not claim the full Sod solution passes until P17.9." It is not
claimed. There is no PDE solve in P17.7 at all.
```

### Where HLLC IS exact — and it is not a trivial set

```text
uniform, x-normal            0.0            uniform, oblique normal   0.0
stationary contact           0.0            moving contact            0.0
supersonic right             0.0            supersonic left           0.0
near-sonic                   0.0            strong density ratio (1e6) 7.97e-65
```

Eight cases where the approximate solver reproduces the exact Godunov flux to 60-digit round-off. The
two contact rows are the ones HLL cannot match — that difference is the whole content of the C in
HLLC, and section 4 quantifies it as 243.46 kg/(m²s).

## 9. §32 — extreme but valid states

```text
case                      min rho*     min e*       max |F|
strong_pressure_ratio     5.8355e+00   2.5385e+07   3.7291e+11
strong_density_ratio      1.0000e-03   2.5385e+05   1.0133e+05
low_pressure              2.4067e-05   1.9778e+00   1.3221e-01
supersonic_right          6.5628e-01   1.7695e+05   6.8378e+08
near_sonic_left           1.0000e+00   2.5385e+05   1.6031e+08
```

All finite, all admissible. §32's caution against physically impossible values is respected: every
state here satisfies `rho > 0` and `p > 0` and is a genuine ideal-gas state.

## 10. §30 — determinism

```text
CPU   15 cases evaluated twice: flux (5 components), S_L, S*, S_R, branch and rho*_L
      BITWISE identical. EXPECT_EQ, not a tolerance.
GPU   17 rows evaluated twice through two separate kernel launches:
      BITWISE identical, including the branch.
```

Branch selection cannot vary between repeats, which §30 specifically asks about: it is a pure function
of the computed wave speeds, and there is no uninitialised state and no reduction anywhere in the
layer.

## 11. Group V — the controls

```text
id  planted defect                                        detected by               tests
V1  Davis wave speeds (the authorization's own suggestion) W4 non-vacuity            8 of 14 fail
    -- a PERMANENT test, because the defect is real code worth keeping
V2  the contact wave deleted (HLLC -> HLL)                T4 + the GPU V2 test       243.46 spurious
    -- also PERMANENT, and named by the frozen G6 criterion itself
V3  star tangential velocity from the OPPOSITE side       T3, B3/B4, X2              3 tests
V4  S* = 0.5(u_nL + u_nR)                                 W1, S1-S4, B3/B4, N1, X2   5 tests
V5  the p*n term dropped from eulerFlux                   F1, F3, T1, T2, T3, X1,
                                                          X2, B3/B4                  8 tests
V6  the unit-normal check removed                         P2 + the GPU refusal test  2 tests
V7  star density divides by (S_K - u_nK)                  see logs/06_controls.log
```

Every plant was verified present by its own marker before the control ran (`verify_planted()` aborts
otherwise), and every restore was confirmed by SHA-256 — because in P17.6 a control whose anchor
missed was on course to report a **spurious VACUOUS**, which is a false accusation against a sound
criterion.

```text
WHY V1 AND V2 ARE TESTS RATHER THAN PLANTS. Both defects exist as real, callable code that is worth
keeping -- WaveSpeedEstimator::Davis and hllFluxForControlOnly(). A permanent test is re-checked by
every future regression; a control script can rot unnoticed. Where the alternative implementation has
independent value, the permanent test is the stronger choice, and it is what makes W4 and T1
self-defending rather than defended once.
```
