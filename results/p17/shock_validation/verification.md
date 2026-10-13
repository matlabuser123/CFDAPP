# P17.9 — the measurements

Every number is copied from a preserved log. Nothing is estimated or reused from an earlier run.
The authoritative run is `logs/15_postformat.log` (after clang-format, proven formatting-only in
`logs/14_format.log`); `logs/10_amended.log` is the identical pre-format run.

```text
AUTHORITATIVE
  logs/15_postformat.log        the focused shock suite, 13 / 13
  logs/11_controls_amended.log  V1 and V2, 0 unexpected results
  logs/13_a2_control.log        amendment A2's derived negative control
  logs/14_format.log            formatting-only proof
  logs/50_p17_9_regress.log     the full regression

PRESERVED, superseded but never deleted
  logs/PRESERVED_01_dryrun_detectors_broken.log   3 detector defects, found before the freeze
  logs/PRESERVED_03_sod_first_run.log             the X3 failure, verbatim
  logs/06_waves.log                               the S6 failure, verbatim
  logs/07_controls.log                            V2 passing the ENTIRE gate, verbatim
  logs/04, 08, 09, 12                             the investigations behind the three amendments
```

---

## 1. Sod shock tube — the three structures, identified by measurement

2D mesh 200×1, dx = 5.000e-03, CFL 0.4, t = 0.2, **677 steps**, walls all round.

```text
W1  rarefaction -> contact -> shock, in that order            YES (ordering asserted, not assumed)
W2  the fan spans 60 cells                                    (exact 44.5; floor 3)
W4  the shock jumps in rho, p AND u, all in the right sense   YES
W3  the contact: |dp|/p* 1.6374e-03, |du|/u* 3.6299e-03,
    drho 0.149362 against an exact 0.160674                   a real density jump, no p or u jump
§23 smeared widths: shock 7 cells, contact 16 cells           reported, not gated
```

### Wave positions

```text
wave               numerical       exact           err          err/dx    limit
rarefaction head   0.2044513848    0.2633721125    5.8921e-02   11.7841   A2: scaling, not C*dx
rarefaction tail   0.4995056388    0.4859601022    1.3546e-02    2.7091   4 dx
contact            0.6797523107    0.6855039870    5.7517e-03    1.1503   2 dx
shock              0.8545118193    0.8504185628    4.0933e-03    0.8187   2 dx
```

### Plateau states, sampled 5 cells clear of every DETECTED wave

```text
quantity                 numerical       exact           |err|        limit
p_star (left plateau)    0.302786421     0.303137627     3.5124e-04   2.0314e-02
p_star (right plateau)   0.303137250     0.303137627     3.7700e-07   2.0314e-02
u_star (left plateau)    0.926453715     0.927519935     1.0662e-03   9.2752e-02
u_star (right plateau)   0.927519935     0.927519935     2.2000e-09   9.2752e-02
rho_star_left            0.418562078     0.426279898     7.7178e-03   1.6067e-02
rho_star_right           0.269200117     0.265606118     3.5941e-03   1.4061e-02
pre-shock rho            0.125085890     0.125000000     8.5890e-05   2.5000e-03
```

## 2. Refinement — nx = 200 / 400 / 800

```text
nx     dx          head err     tail err     contact err  shock err
200    5.000e-03   5.8921e-02   1.3546e-02   5.7517e-03   4.0933e-03
400    2.500e-03   3.8596e-02   6.7506e-03   3.2352e-03   2.1143e-03
800    1.250e-03   2.4860e-02   2.5824e-03   1.8011e-03   1.0724e-03
                   ALL FOUR DECREASE IN ABSOLUTE TERMS

nx     err/dx: head    tail     contact  shock
200            11.78   2.71     1.15     0.82
400            15.44   2.70     1.29     0.85
800            19.89   2.07     1.44     0.86
               ^^^^^                     ^^^^^ flat -> the shock is genuinely O(dx),
               grows: the head is diffusive, which is what amendment A2 corrects

nx     L1 rho       L1 u         L1 p         Linf rho     shock w  contact w
200    1.27769e-02  2.20461e-02  1.09023e-02  8.60857e-02  7        16
400    8.09055e-03  1.24045e-02  6.48265e-03  8.47407e-02  7        23
800    5.10495e-03  6.97039e-03  3.81260e-03  8.58707e-02  7        31

observed L1 rates, REPORTED not gated:
  200 -> 400 : rho 0.6592   u 0.8297   p 0.7500
  400 -> 800 : rho 0.6643   u 0.8316   p 0.7658
```

```text
THE L1 RATES ARE WHERE THE GATE PREDICTED THEM, and that prediction was written before the run:
"a contact converges ~dx^1/2, a fan ~dx", so a composite Sod rate sits between 0.5 and 1. Measured
0.66 to 0.83. The Linf error is flat at ~8.5e-02 by construction -- it is the single worst cell
inside the smeared layer and stays O(jump) however good the scheme is, which is why the gate
reports it and does not gate on it.
```

### Amendment A2 — the head's derived scaling

```text
nx 200    err 5.89207e-02   err/dx  11.7841   err/sqrt(dx) 0.83326
nx 400    err 3.85959e-02   err/dx  15.4384   err/sqrt(dx) 0.77192
nx 800    err 2.48602e-02   err/dx  19.8881   err/sqrt(dx) 0.70315
  A2-i   err decreases                PASS
  A2-ii  err/sqrt(dx) does not grow   PASS
  A2-iii err/dx grows                 PASS
observed rate log(5.8921e-2 / 2.4860e-2)/log 4 = 0.623, between the 0.5 of pure diffusion and the
1.0 the frozen bound assumed -- which is the log correction the derivation predicts.
```

### Amendment A3 — the density plateaus

```text
nx     rho*_R fixed-5 (contaminated)  rho*_R isolated (flattest window)  ratio
200    3.59405e-03                    2.53622e-04                        14.2
400    3.74699e-03                    3.95193e-06                       948.1
800    3.42003e-03                    1.50944e-05                       226.6

S6 for p* and u*, UNCHANGED and still strict:
  |dp*|  3.51241e-04 -> 1.71478e-04 -> 7.52235e-05    rate ~1.1   PASS
  |du*|  1.06622e-03 -> 5.28371e-04 -> 2.46821e-04    rate ~1.1   PASS
```

## 3. Normal shock — M1 = 2.0, 2D mesh 400×1, t = 5.0e-04, 1389 steps

```text
quantity     numerical        analytical       rel err
rho2/rho1    2.6667102673     2.6670020510     1.0941e-04
p2/p1        4.4995544351     4.4998113700     5.7099e-05
T2/T1        1.6873053253     1.6872170639     5.2312e-05
u2/u1        0.3749263486     0.3749528425     7.0659e-05
M2           0.5772701741     0.5773260664     9.6812e-05

Rankine-Hugoniot residuals, in the shock frame, from the NUMERICAL plateau states:
  mass             1.8006e-04
  momentum         1.1869e-04
  total enthalpy   4.0216e-05

N9 shock position 0.49995539 vs an initial 0.5000  ->  drift 4.4611e-05 = 0.0178 dx
min rho 1.200000   min p 103338.00   min T 300.0000   retries 0   rejected steps 0
```

```text
THIS IS G7's "SECOND, DISCRIMINATING CASE". The Rankine-Hugoniot residuals are the strongest
evidence in the phase, and the authorization's §14 says why: they test the jump conditions the
NUMERICAL states actually satisfy rather than comparing two profiles. A scheme can smear a shock
badly and still satisfy them, because conservation forces it to -- so these failing would mean
conservation was broken, a different and more serious defect than inaccuracy.
```

## 4. Contacts

```text
STATIONARY, 200x1, t = 0.20, 1339 steps
  position            0.5000000000 vs 0.5   err EXACTLY 0.0000e+00
  pressure variation  EXACTLY 0.0000e+00
  velocity variation  EXACTLY 0.0000e+00
  smeared width       0 cells        <- AMENDMENT A1's criterion; HLL and Rusanov give 21
  mass                relative 0.0000e+00

MOVING at u = 4.0 (Ma_L 3.38, Ma_R 1.20), t = 0.10, 1070 steps
  position            0.6992930944 vs 0.7000000000   err 7.0691e-04 = 0.1414 dx
  pressure variation  4.0856e-14
  velocity variation  2.9976e-15
  smeared width       22 cells   -- and HLL also gives 22, so the MOVING contact does not
                                    discriminate; only the stationary one does
```

## 5. Compression and expansion — reported separately, as §46 requires

```text
COMPRESSION (u_L = +0.4, u_R = -0.4, rho = 1, p = 1), t = 0.12, 332 steps, TWO SHOCKS
  p*    1.5760937179 vs 1.5788857454   rel 1.7684e-03    p RISES
  rho*  1.3764661731 vs 1.3819657312   rel 3.9795e-03    rho RISES
  u*    0.0000000000 vs 0.0000000000   symmetric, exactly 0

EXPANSION (u_L = -0.4, u_R = +0.4, rho = 1, p = 1), t = 0.12, 332 steps, TWO RAREFACTIONS
  p*    0.6150551777 vs 0.6126216501   rel 3.9723e-03    p FALLS
  rho*  0.7026036890 vs 0.7046522885   rel 2.9072e-03    rho FALLS
  u*    0.0000000000 vs 0.0000000000   symmetric, exactly 0
  min rho 0.688108, min p 0.612220 -- an expansion is where positivity is at risk, and it held
```

## 6. Supersonic advection — Ma = 2.02, 200×1, t = 3.0e-04, 419 steps

```text
pulse centroid  0.3000000886 -> 0.5100000886, expected 0.5100000886
position error  2.0151e-13 = 0.0000 dx
pressure variation 9.7165e-15, velocity variation 2.2737e-15   (an entropy wave: both ~0)
mass rel change 9.4027e-15, energy rel change 4.0508e-15       (open domain, flux carries them)
```

## 7. Conservation and positivity

```text
SOD, closed box with walls
  nx = 200, 677 steps    mass 2.3130e-15   energy 6.3052e-15   (bound 3.5527e-13)
  nx = 400, 1368 steps   mass 1.5111e-14   energy 4.2876e-15   (bound 7.1054e-13)
  min rho 0.12500000   min p 0.10000000   min T 0.0024874862
  retries 0, rejected steps 0, non-finite detections 0

POSITIVITY across every benchmark: rho > 0, p > 0, T > 0 at every accepted state.
MOMENTUM: the closed Sod box's momentum balance is P17.8's G1 criterion, re-verified there
  (1.066e-14 against a change of 2.864e+01); the open cases' balance is P17.8's G2 (3.8e-17 mass,
  1.3e-14 momentum, 3.4e-11 energy). Inherited, not renegotiated, exactly as G7 requires.
```

## 8. Determinism and CFL sensitivity

```text
DETERMINISM: 677 steps, 200 cells. dt history, time history, all five residual histories, retry
counts, the full state, AND the detected shock and contact positions -- ALL BITWISE identical
across two runs (EXPECT_EQ, no tolerance).

CFL SENSITIVITY (both values declared in configuration.md before any run):
  CFL    steps    shock err    contact err  head err     L1 rho       mass err
  0.40   677      4.0933e-03   5.7517e-03   5.8921e-02   1.2777e-02   2.3130e-15
  0.20   1353     4.3534e-03   5.9231e-03   6.0176e-02   1.3041e-02   2.6214e-15
Halving the step changes nothing material: the result is a property of the discretisation.
```

## 9. The detectors, cross-checked

```text
D0  the C++ exact sampler against the 60-digit Python profile: worst relative 8.246e-16 over 41
    spot points
D1  the detectors on the EXACT profile, nx = 200:
      rarefaction head 0.2225 dx, tail 0.6180 dx, contact 0.1008 dx, shock 0.0837 dx
      the fan measures 45 cells against an exact 44.5
    V4 a jumpless monotone ramp -> NO SHOCK reported
    V3 the profile shifted 3 dx -> shock error 2.9163 dx, criterion correctly breaks
```

## 10. The negative controls

```text
logs/11_controls_amended.log
  V1  Rusanov / local Lax-Friedrichs -- NO Riemann solver
        ContactDiscontinuityTest   FAIL   detected  (A1: 21 smeared cells)
        SodShockTubeTest.S         FAIL   detected
        ShockConservationTest      PASS   -- still conservative, as it must be
  V2  HLL -- HLLC with the CONTACT WAVE deleted
        ContactDiscontinuityTest   FAIL   detected  (A1: 21 smeared cells)
        NormalShockTest            PASS   -- the shock is still right, as expected
        SodShockTubeTest.S         PASS   -- recorded: no Sod criterion can separate HLLC from HLL
  CONTROLS WITH AN UNEXPECTED RESULT: 0

logs/13_a2_control.log
  HLLC      rarefaction head err 5.8921e-02
  Rusanov   rarefaction head err 6.3477e-02   LARGER, as err ∝ sqrt(D) predicts
```

## 11. §41 — the authoritative benchmark table

```text
Case                  Mesh     CFL   t        rho err      p err       u err       shock pos  contact pos  mass imbal   momentum     energy imbal  min rho   min p      PASS
Sod                   200x1    0.4   0.2      1.278e-02*   1.090e-02*  2.205e-02*  0.82 dx    1.15 dx      2.313e-15    P17.8 G1     6.305e-15     0.125000  0.100000   YES
Sod                   400x1    0.4   0.2      8.091e-03*   6.483e-03*  1.240e-02*  0.85 dx    1.29 dx      1.511e-14    P17.8 G1     4.288e-15     0.125000  0.100000   YES
Sod                   800x1    0.4   0.2      5.105e-03*   3.813e-03*  6.970e-03*  0.86 dx    1.44 dx      N/A          N/A          N/A           N/A       N/A        YES
Sod (CFL 0.2)         200x1    0.2   0.2      1.304e-02*   N/A         N/A         0.87 dx    1.18 dx      2.621e-15    N/A          N/A           N/A       N/A        YES
Normal shock M1=2     400x1    0.4   5e-04    1.094e-04^   5.710e-05^  7.066e-05^  0.018 dx   N/A          1.801e-04#   1.187e-04#   4.022e-05#    1.200000  103338.00  YES
Stationary contact    200x1    0.4   0.2      N/A          0.000e+00   0.000e+00   N/A        0.00 dx      0.000e+00    N/A          N/A           0.125000  1.000000   YES
Moving contact Ma3.4  200x1    0.4   0.1      N/A          4.086e-14   2.998e-15   N/A        0.14 dx      N/A          N/A          N/A           0.125000  1.000000   YES
Supersonic advection  200x1    0.4   3e-04    N/A          9.717e-15   2.274e-15   N/A        0.00 dx      9.403e-15    N/A          4.051e-15     1.200000  103338.00  YES
Compression           200x1    0.4   0.12     3.980e-03~   1.768e-03~  0.000e+00   2 dx       N/A          N/A          N/A          N/A           1.000000  1.000000   YES
Expansion             200x1    0.4   0.12     2.907e-03~   3.972e-03~  0.000e+00   N/A        N/A          N/A          N/A          N/A           0.688108  0.612220   YES

*  L1 field error against the exact profile
^  relative error of the JUMP RATIO against the analytical normal-shock relation
#  Rankine-Hugoniot residual in the shock frame
~  relative error of the star-state value
N/A  the metric does not apply to that case
```
