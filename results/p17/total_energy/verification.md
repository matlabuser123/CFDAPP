# P17.5 — verification

Measured values, not verdicts. Consolidates what the authorization suggested as
`primitive_conserved_conversion.md`, `temperature_recovery.md`, `thermodynamic_closure.md`,
`conservation.md`, `canonical_tests.md`, `determinism.md` and `cpu_gpu_equivalence.md`, following the
P17.1–P17.4 convention.

Sources: `logs/01_dryrun.log` (unchanged tree), `logs/03_gate.log` and `logs/03_total_energy.log`
(gate attempt 2), `logs/04_diagnose_g2.log`, `logs/05_controls.log`, `logs/06_fingerprint.log`,
`logs/07_adiabatic.log`.

```text
R = 287.05   cp = 1005   cv = 717.95000000000005   gamma = 1.3998189288947698
eps = 2.2204460492503131e-16
```

---

## 1. Thermodynamic closure, as ONE coherent state

Each row is evaluated on the same `(rho, U, T, p)` state, not with unrelated values.

```text
identity                       zero reference        shifted reference (T_ref = 298.15)
------------------------------------------------------------------------------------------
p = rho R T                    exact, via the EOS    exact, via the EOS
cp - cv = R                    P17.2's own invariant  (cv is derived as cp - R)
gamma = cp / cv                1.3998189288947698
ke = 0.5 |U|^2                 EXACT                 EXACT
eTotal = e + ke                EXACT                 EXACT
rhoE = rho eTotal              EXACT                 EXACT
h = e + p/rho                  <= 4 eps              <= 4 eps
hTotal = h + ke                EXACT                 EXACT
h - e = R T                    EXACTLY 0             EXACTLY 0
rho hTotal = rhoE + p          0                     5.9117155615240335e-12
                                                     against a 9.2e-11 bound
e = cv T                       0.52 eps              299640.75 = cp T_ref -- and ASSERTED wrong
h = cp T                       EXACTLY 0             299640.75 -- and ASSERTED wrong
p = (gamma-1)(rhoE - ke)       0.76 eps              83 PERCENT WRONG -- never used as a path
```

The two right-hand entries that say "asserted wrong" are criteria T9 and C7's second half. They
exist so that the shortcuts cannot be silently applied in a regime where they do not hold: the test
*requires* the discrepancy to exceed 1e3, so it cannot pass by accident.

`h - e = R T` being **exactly 0 under both references** is the payoff of defining `e := h - R T`.
Nothing had to be tuned for it.

## 2. Primitive ↔ conserved conversion

```text
primitive -> conserved, vs hand-computed rho (cv T + 0.5|U|^2)   EXACT
                                                                 (rho 1.25, T 400, |U| = 100)
conserved -> primitive
  velocity                                                       <= 8 eps
  specific internal energy                                       <= 8 eps
  temperature                                                    within 1e-12 K
  pressure                                                       BITWISE equal to
                                                                 IdealGasEOS::pressure
round trip prim -> cons -> prim, 4 states x 2 references         T within 1e-12 K
round trip cons -> prim -> cons                                  2.19 eps relative
```

Pressure being **bitwise** equal to the EOS is criterion C4, and it is the structural guarantee that
no second pressure formula exists on this path.

## 3. Temperature recovery

```text
inverse form      T = (e - h_ref + cp T_ref) / cv        CLOSED FORM, exact, no iteration
P15's bound       kInversionToleranceKelvin = 1e-12 K    inherited verbatim, not re-derived
measured T(h(T))  4.5474735088646412e-13 K               2.2x inside, on the UNCHANGED tree
measured T(e(T))  within 1e-12 K at 5 temperatures x 2 references
out of range      an ERROR, never a clamp -- V4 asserts it throws, and asserts an in-range state
                  still succeeds so the window is real rather than a blanket rejection
```

The closed-form inverse and P15's bisection are checked **against each other**
(`InternalEnergyInverseAgreesWithTheForwardModel`) rather than either being trusted alone. P15's
root find is not duplicated.

## 4. Conservation — every term, never one opaque scalar

`logs/04_diagnose_g2.log`, a closed adiabatic 7×5 domain, non-uniform `T` and `U`, an irregular
internal mass flux, 20 explicit steps:

```text
term                        measured                       judged against
---------------------------------------------------------------------------------------------
sum(convectiveOutflow)      -1.16415e-10                   0.95 eps of the worst cell (5.5e5)
sum(conductiveOutflow)      2.66454e-15                    1.9 eps of the worst cell (6.3)
domain energy drift, 20 st  1.86e-09 on 1.0068664e7        0.83 eps -- ONE ulp
```

```text
G3 uniform heating, U = 0, 10 steps    dE = S_E V_domain dt n   within the derived bound
G4 uniform state                        imbalance within bound; absent terms EXACTLY 0
G5 interior faces telescope             per-cell > 1e3, global sum within bound
   and with the box OPENED              the sum BREAKS -- so G5 is not vacuous
```

The bound is `kAccum * eps * N * scale` with `kAccum = 8` **EMPIRICAL and labelled as such**, reused
verbatim from P17.1/P17.3/P17.4. `scale` is the **largest single term**, not the imbalance: a bound
derived from the imbalance would shrink to nothing exactly when two large terms cancel, which is when
a conservation check most needs to stay honest.

### The G2 failure that produced this, and what it was

Gate attempt 1's G2 drifted **1.15e-9 relative**, about 5e6 eps. That is not round-off and was not
treated as a threshold problem. Measurement showed conduction summing to **5.64 W** out of a domain
whose every boundary prescribed a zero gradient, because `discretization::diffusion` fits a quadratic
at the wall and so returns a non-zero flux for a prescribed zero gradient. Rebuilt on P15's
prescribed-flux-exact helpers, the same sum is 1.9 eps. Full account:
`AMENDMENT_conduction_boundary.md`.

## 5. The canonical cases the authorization lists

```text
A uniform thermodynamic state         G4 -- imbalance within bound, absent terms exactly 0
B kinetic-energy state                T1 -- exact, including a three-component velocity
C pure thermal conduction, U = 0      section 23 probe: convective outflow EXACTLY 0, body-force
                                      power EXACTLY 0, conduction large (17.48) and summing to
                                      -5.33e-15 = 1.37 eps
D adiabatic closed domain             G2 -- drift <= 1 ulp over 20 steps
E uniform volumetric heating          G3 -- dE = S_E V dt exactly, within bound
F pressure work                       E4 -- dropping p changes the answer by > 1e-6 relative
G viscous work                        NOT VERIFIED as a flux. P17.4 provides no stress tensor;
                                      the shortfall is reported by viscousWorkLimitation() and a
                                      null term is exactly 0 rather than folded in
H primitive/conserved round trip      C5, C6 -- 4 states x 2 references
```

## 6. Adiabatic compression — §22

`logs/07_adiabatic.log`. Along an isentrope with `T2 = T1 (rho2/rho1)^(gamma-1)`:

```text
rho2/rho1   T2 [K]        p2 [Pa]          p2/rho2^gamma   rel dev      T recovery err [K]
1.25        327.99537     117688.8385      86115           1.69e-16     5.684e-14
1.5         352.7978      151905.9145      86115           1.69e-16     5.684e-14
2.0         395.80269     227230.3262      86115           0            5.684e-14
4.0         522.19924     599589.1676      86115           1.69e-16     0
8.0         688.95955     1582126.716      86115           1.69e-16     1.137e-13
```

```text
worst relative deviation of p/rho^gamma     1.68982e-16  = 0.76 eps
worst T recovery error along the isentrope  1.13687e-13 K
```

This verifies the `rho <-> rhoE <-> T <-> p` coupling along an isentrope. **P17.5 does not SOLVE a
compression**, and no shock capability is claimed from it — §22's own caution.

## 7. Determinism

```text
D1   rhoE, T, p and e are bitwise identical across 8 repeated evaluations
     RepeatedEvaluationIsBitwiseIdentical, EXPECT_EQ on every quantity
```

No iteration counts or residual histories exist to compare, because there is no solver loop — which
is stated rather than reported as "identical".

## 8. Non-vacuity controls — all 8 DETECTED

`logs/05_controls.log`. Probe set: `-R 'TotalEnergy'`, **all 34 tests**, deliberately the whole phase
suite rather than a hand-picked regex — P17.4's N6 was wrongly declared vacuous because its regex
excluded the file holding an existing detector.

```text
control  planted defect                              tests fired  the criterion that caught it
-----------------------------------------------------------------------------------------------
N1       e = h + R T (defining relation, sign)        11 of 34     T5, T6, T7, T8, C1-C7, E3
N2       ke = |U|^2 (0.5 dropped)                      3 of 34     T1, C1, C7
N3       flux carries eTotal, no pressure work          1 of 34     E4
N4       conductive flux sign flipped                   1 of 34     E5
N5       transient differences rhoE_new against itself  1 of 34     E1
N6       rhoE = rho e (ke omitted)                      8 of 34     T2/T3, C1..C6, E3, T7
N7       inadmissible e ACCEPTED instead of throwing     1 of 34     V3
N8       h = cp*T instead of P15's integral              5 of 34     Z1, T5, T6, T7, T9
```

All restored and **proved back at baseline by SHA-256**, 0 surviving markers, suite back to 34/34.

### Two predicted detectors did NOT fire, for structural reasons worth recording

The gate's "must break" column is a prediction, and two entries of it were wrong. Neither control was
vacuous — both DETECTED — but *which* test caught them differed from what I expected, and the reasons
generalise:

```text
N2 predicted T1, T2, C5   -- only T1 fired.
   T2 (eTotal = e + ke) and C5 (round trip) compute ke through the SAME broken function on both
   sides, so they are self-consistent under the defect. A ROUND-TRIP TEST CANNOT CATCH AN ERROR IN
   A FUNCTION USED IN BOTH DIRECTIONS. T1 caught it because it compares against a hand-computed
   value -- which is why value-anchored tests must sit alongside round trips, not be replaced by
   them.

N4 predicted E5, G2      -- only E5 fired.
   Flipping the conductive sign leaves the operator ANTISYMMETRIC, so it is still conservative and
   the global sum is still zero. A CONSERVATION TEST CANNOT DETECT A SIGN ERROR IN A CONSERVATIVE
   OPERATOR. G2 passing under N4 is correct behaviour, not a gap -- but it means a conservation
   gate proves less than it looks like it proves, and an analytical-value test (E5) is what actually
   pins the sign.
```

This is the sharper version of the P17.4 lesson: it is not enough to ask whether every criterion has
a test, one must ask whether that test is *capable* of failing for the defect in question.

## 9. Preservation

```text
P15 thermal suite              161/161    exactly the frozen baseline
P14 transport                   68/68
compressible incl. new         210/210    = 176 at the P17.4 baseline + 34
P13-001 SIMPLE fingerprint     BITWISE IDENTICAL, diff file 0 bytes, 10 cases / 130 lines
upstream files                 15 UNTOUCHED, 0 TOUCHED, by SHA-256
clang-format on 5 changed files exit 0
incompressible path EOS-free   0 references
```

Attempt 1 reported P15 as "171 with 1 failure". That was an **instrument defect in my own gate**: my
test names contain "Enthalpy", which the frozen P15 regex matches, so the probe was measuring
161 P15 tests + 10 of mine, and the one failure was mine. Adding `-E TotalEnergy` restored it to
161/161. A preservation probe that includes the phase's own new tests is not a preservation probe.

## 10. CPU/GPU equivalence — examined, and blocked by absence

```text
symbol                    hits in cuda/ + include/cfd/gpu/
----------------------------------------------------------
compressible              1   <- a COMMENT in DevicePressureCorrection.hpp reading "the body
                                 behind the incompressible", i.e. a false positive
IdealGas                  0
EquationOfState           0
ThermodynamicProperties   0
Enthalpy                  0
rhoE                      0
totalEnergy               0
TotalEnergy               0
machNumber                0
```

A GPU total-energy path would require, in order: a GPU EOS, a GPU compressible mass flux, a GPU
compressible momentum assembly, and a GPU compressible solver to drive them. **None exists.** §27 is
therefore answered on the evidence rather than by preference: not deferred, blocked by absence.

```text
X1 is reported NOT PRESENT, never PASS. No GPU path was invented to satisfy the checkbox, and no
CPU numerics were altered to make one easier.
```

The nearest reusable GPU machinery is the generic scalar-transport family, and
`DeviceScalarTransport.hpp` states that `assembleScalarConvectionDevice` ports
`thermal::assembleThermalConvectionContribution` — i.e. the GPU already carries the `cp*T` form of
the defect recorded in `audit.md` §3a. Porting energy onto it would propagate that, which is a second
reason not to start there.

## 11. Numerical conditioning — §25, answered with numbers

```text
|U|      Mach          ke            ke/e          recovered |e_back - e|
0.1      0.000288      0.005         2.32e-08      0
1.0      0.00288       0.5           2.32e-06      0
10.0     0.0288        50            2.32e-04      0
100.0    0.288         5000          2.32e-02      0
300.0    0.864         45000         2.09e-01      0
```

The recovered error is **exactly 0** at every speed up to Mach 0.86. `rhoE/rho` is *dominated by* `e`
in this regime, so the subtraction is benign; cancellation needs `ke >> e`, which is hypersonic — the
opposite end from this solver. **No energy floor and no low-Mach preconditioning is introduced**, and
§25 is closed by measurement rather than by a precaution.

## 12. What is NOT verified

```text
NOT VERIFIED   the viscous-work FLUX. P17.4 builds no stress tensor, so (tau.U).n cannot be formed
               exactly; reported by viscousWorkLimitation(), null terms are exactly 0.
NOT VERIFIED   CPU/GPU equivalence -- blocked by absence, section 10.
NOT VERIFIED   an implicitly coupled rhoE solve. Not implemented; hTotal is nonlinear in rhoE.
NOT VERIFIED   k(T) conduction. Constant k only; a variable-k flux needs a face-interpolated k.
NOT VERIFIED   cp(T). Rejected at construction with a named error and a stated reason.
NOT VERIFIED   case-file reachability. rhoE has no case key or export field, and P15's enthalpy
               model is itself unreachable from a case.
NOT VERIFIED   low-Mach solver behaviour (§24). The compressible solver has no energy loop to run,
               so there is no coupled low-Mach energy result to preserve. The low-Mach and MMS
               suites pass unchanged because P17.5 modifies no existing path -- which is
               preservation, not low-Mach energy validation. P17.6 owns that.
NOT VERIFIED   3D. The compressible path is 2D-only.
```
