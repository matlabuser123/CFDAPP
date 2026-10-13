# P17.5 — Total-Energy Equation: summary

Gate: `acceptance_gate.md`, sha256 `ca84e432a277a2724f0c94a8ccc46294ff405b5b8168b66327690a01ba72a04b`,
frozen before any total-energy code existed and **byte-identical at close**. Nothing was committed or
pushed; `HEAD` is still `3bd32f91` of 2026-09-21.

---

## 1. What was built

```text
TotalEnergyState        the thermodynamic state and BOTH conversions. Owns the EOS and the enthalpy
                        model together, which is how the dual-cp hazard is closed rather than moved.
TotalEnergyEquation     the conservative total-energy equation in flux form, a balance diagnostic
                        that reports every term separately, and an explicit conservative advance.
```

```text
NEW   include/cfd/compressible/TotalEnergy.hpp              src/compressible/TotalEnergy.cpp
NEW   include/cfd/compressible/TotalEnergyEquation.hpp      src/compressible/TotalEnergyEquation.cpp
NEW   tests/unit/compressible/test_total_energy.cpp         34 TESTs
EDIT  src/CMakeLists.txt  +2 sources   tests/unit/compressible/CMakeLists.txt  +1
```

Before this phase there was **no total-energy state and no compressible energy equation at all**:
`rhoE`, `totalEnergy`, `internalEnergy`, `pressureWork` and `viscousWork` returned **0 hits** across
`include/` and `src/`, and `CompressibleSIMPLE.hpp` says in its own words that temperature is "never
updated by this solver itself (no energy-equation)".

## 2. The contract, and the one decision that carries it

```text
e        specific internal energy    ke      specific kinetic energy
eTotal   specific total energy       rhoE    conserved total-energy density
h        static enthalpy             hTotal  total enthalpy
```

`e(T) := h(T) - R T`, with `h` from P15's closed-form `SensibleEnthalpy`. That one definition makes

```text
h - e = R T          EXACTLY 0, measured under BOTH reference conventions
de/dT = cp - R = cv  automatic, nothing to assert
rho hTotal = rhoE + p  an identity, which is why the convective flux can carry hTotal and so
                       cannot forget pressure work
```

and it **preserves P15's reference convention instead of redefining it** — `e` inherits its zero
point from `h`.

## 3. Verdict against the frozen gate

```text
group                                    verdict
------------------------------------------------------------------
T  the thermodynamic state               PASS
C  conversion and recovery               PASS
E  the conservative equation             PASS, E9 reports the viscous shortfall
G  global conservation                   PASS
V  invalid states                        PASS
Z  the dual-cp decision                  PASS
D  determinism                           PASS
P  preservation                          PASS
N  non-vacuity controls                  ALL 8 DETECTED
X  CPU/GPU equivalence                   NOT PRESENT (never "PASS")
```

Key measurements — the full set with derivations is in `verification.md`:

```text
h - e = R T                              EXACTLY 0, both references
rho hTotal = rhoE + p                    5.91e-12 vs a 9.2e-11 derived bound
T round trip                             4.55e-13 K vs P15's inherited 1e-12 K
prim -> cons, vs hand-computed           EXACT
pressure vs IdealGasEOS::pressure        BITWISE equal
adiabatic closed domain, 20 steps        drift <= 1 ulp (0.83 eps)
sum(conductiveOutflow), closed domain    1.9 eps   (5.64 W before the fix)
sum(convectiveOutflow)                   0.95 eps
p/rho^gamma along an isentrope           0.76 eps over ratios 1.25 - 8
static thermal limit, U = 0              convection and body power EXACTLY 0
P13-001 SIMPLE fingerprint               BITWISE IDENTICAL, 0-byte diff
P15 thermal suite                        161/161, exactly the frozen baseline
upstream files                           15 UNTOUCHED, 0 TOUCHED, by SHA-256
```

## 4. Regression

```text
                  registered  predicted  executed  predicted  failed  runtime
------------------------------------------------------------------------------
Release + CUDA        2642       2642      2580      2580        0    42.2 min
CPU-only              2440       2440      2380      2380        0    33.0 min
Debug + GUI           not run this phase -- deferred to P17.11 by standing decision
```

Every predicted figure matched exactly; both `COUNT VERDICT` lines read `MATCHES the frozen
prediction`; the closing arithmetic balances in both trees
(`2642 - 13 - 49 = 2580`, `2440 - 13 - 47 = 2380`); all 34 P17.5 rows were enumerated **by name**
and passed in both configurations, so they executed rather than merely registering; and the code
fingerprint was **identical before and after** the run. `TotalEnergy.cpp` and
`TotalEnergyEquation.cpp` are both compiled in the CPU-only tree (nvcc and libcudart at 0), which
closes the `if(NOT CFDAPP_ENABLE_CUDA)` blind spot.

The prediction was `2608 + 34` and `2406 + 34`, declared before the run. **Unlike P17.4 it needed no
revision**: the coverage map was built against the frozen criteria *before* implementation, so the
test count was settled once.

## 5. The three things this phase found that were not its own work

```text
1  THE DUAL-cp HAZARD, closed.
   physics.thermal.specific_heat (a cp(T) model, feeding P15's temperature solve) and
   physics.compressible.specific_heat_pressure (one constant, feeding P17.2) were parsed
   independently with ZERO cross-validation sites. In a thermal-coupled compressible case the
   temperature was produced with one cp and consumed with the other, and a case declaring two
   different gases ran silently. TODO.md had recorded this as "decide before P17.5"; it is now a
   diagnostic at the only place that can see both.

2  THE PRODUCTION THERMAL PATH TRANSPORTS cp*T.
   P15's own Enthalpy.hpp exists to say that is wrong for varying cp -- "a solver that used the
   product would conserve something, just not energy" -- yet assembleThermalConvectionContribution
   still scales the mass flux by cp, and ThermalSolver calls it with a cp(T) FIELD. Measured with
   cp = 900 + 0.15T: the naive product is wrong by 103.2%, 39.5% and 26.3% at 400, 800 and 1500 K.
   NOT FIXED -- repairing it would move every variable-cp result in the qualified baseline.

3  SensibleEnthalpy IS UNREACHABLE FROM A CASE FILE.
   0 hits in src/io and src/app. The correct machinery exists, is unit-tested, and no production
   path uses it. So P17.5's energy path -- which is built on it -- is library-only by inheritance,
   and wiring P17.5 to the case format requires wiring P15's enthalpy model first.
```

And one found by the gate failing:

```text
4  discretization::diffusion AND P15's THERMAL ASSEMBLER DISAGREE ON PRESCRIBED-FLUX BOUNDARIES.
   The generic operator fits a quadratic through (boundary, owner, opposite-neighbour), so a
   prescribed ZERO gradient yields a NON-ZERO flux on a curved profile -- 5.64 W out of a closed
   "adiabatic" box, measured. P15's assembler assembles the prescribed flux exactly. Neither is a
   defect on its own terms; they are different accuracy trades. NOT reconciled -- either change
   would move a qualified baseline.
```

## 6. What gate attempt 1 caught, and what it says about my own gate

Three of 34 failed. **Two were my own mis-derived thresholds; the third was a real design error
in the code.** None was a case of the criterion being too strict.

```text
T7  rho hTotal = rhoE + p        MY THRESHOLD WAS WRONG. I bounded a CANCELLATION by the size of
                                 its (small) result instead of by the larger operand. Corrected with
                                 a derivation; still discriminates by 16 orders. AMENDMENT_thresholds
T8  e = cv T "exactly 0"         MY THRESHOLD WAS WRONG. cv is the rounded difference cp - R, so
                                 (cp-R)T differs from cpT - RT by one ulp. The dry-run measured 0 at
                                 four temperatures and I wrote exactness into the gate as a property.
                                 h = cp T stays exact; e = cv T is now eps-scaled.
G2  adiabatic closed domain      A REAL DESIGN ERROR IN THE CODE: the wrong reuse target for
                                 conduction. Fixed in the IMPLEMENTATION, not the threshold.
                                 AMENDMENT_conduction_boundary.md
```

Also, my own **preservation probe was contaminated**: attempt 1 reported P15 as "171 with 1 failure",
because my test names contain "Enthalpy" and the frozen P15 regex matches them. The probe was
measuring 161 P15 tests plus 10 of mine, and the failure was mine. `-E TotalEnergy` restored it to
161/161. A preservation probe that includes the phase's own new tests is not a preservation probe.

A third one, in the integrity check: its surviving-control-marker probe used an unanchored
`// N[0-9]` and reported **2 surviving markers** that were pre-existing comments in
`src/discretization/Diffusion.cpp` about Newton interpolation *nodes* named N1 and N2 — verified
present at `HEAD`, nothing to do with this phase. Anchoring to `// N[0-9]b?$` gives 0, and the probe
now also sanity-checks that the anchored pattern still matches a synthetic marker, so it cannot
become silently blind.

```text
THE PATTERN, worth naming because it happened THREE times in one phase: every one of my own
instruments over-matched. The P15 preservation regex caught my tests because their names contain
"Enthalpy"; the marker probe caught unrelated prose; and the gate embedded an editable placeholder
in a frozen document. None was a code defect and all three produced a misleading report. A probe
needs its own non-vacuity check -- "does it fire on a synthetic positive, and stay silent on the
unchanged baseline" -- exactly as a negative control does.
```

And a flaw in the gate's construction: §13's coverage map was written into the **frozen** document as
`TO BE FILLED`, so completing it would have broken the freeze hash. It is completed in
`coverage_map.md` instead, and the lesson is to freeze the criteria and the map separately.

## 7. Two findings about what controls actually prove

All 8 controls DETECTED, but two of the gate's *predicted* detectors did not fire, for structural
reasons that generalise well beyond this phase:

```text
N2  ke = |U|^2 (0.5 dropped)   predicted T1, T2, C5 -- only T1 fired.
    T2 and C5 compute ke through the SAME broken function on both sides, so they are self-consistent
    under the defect. A ROUND-TRIP TEST CANNOT CATCH AN ERROR IN A FUNCTION USED IN BOTH DIRECTIONS.
    T1 caught it because it compares against a hand-computed value.

N4  conductive sign flipped    predicted E5, G2 -- only E5 fired.
    A flipped sign leaves the operator ANTISYMMETRIC, so it is still conservative and the global sum
    is still zero. A CONSERVATION TEST CANNOT DETECT A SIGN ERROR IN A CONSERVATIVE OPERATOR.
```

This sharpens the P17.4 lesson. It is not enough to ask whether every criterion has a test; one must
ask whether that test is **capable of failing** for the defect in question. A conservation gate in
particular proves less than it appears to.

## 8. What is NOT done, and is not claimed

```text
NOT DONE       an implicitly coupled rhoE solve inside CompressibleSIMPLE. hTotal is nonlinear in
               the unknown rhoE through T, so an implicit form needs a Newton linearisation --
               solver architecture that belongs with P17.8. What exists is the conservative flux
               form plus an explicit conservative advance.
NOT DONE       the viscous-work FLUX. P17.4 builds no stress tensor, so (tau.U).n cannot be formed
               exactly. Substituting tau : grad(U) would silently switch to the INTERNAL-energy
               form of the equation. Reported by viscousWorkLimitation(); null terms are exactly 0.
NOT DONE       any GPU path. 9 probes over cuda/ and include/cfd/gpu/ return 0. A GPU total-energy
               path needs a GPU EOS, mass flux, momentum assembly and solver first. Reported NOT
               PRESENT, never PASS; nothing was invented to tick the box.
NOT DONE       case-file reachability. rhoE has no case key and no export field, and the P15
               enthalpy model it rests on is itself unreachable from a case.
NOT DONE       k(T) conduction -- a variable-k flux needs a face-interpolated k, which is new
               discretisation, left out rather than guessed at.
NOT DONE       cp(T) -- rejected at construction with a named error, because the EOS block stores a
               single cp from which it derives cv and gamma.
NOT DONE       3D. The compressible path is 2D-only. |U|^2 uses all three components because
               Vector2 is a Vector3 with z = 0, so the inactive dimension is handled by the type,
               but no 3D claim is made.
NOT DONE       low-Mach ENERGY validation (§24). There is no energy loop inside the solver to run,
               so the low-Mach and MMS suites pass because P17.5 modifies no existing path. That is
               preservation, not validation. P17.6 owns it.
NOT DONE       repairing finding 2 or 4 of §5, or anything in P17.6-P17.10. No HLLC.
```

## 9. Standing debt carried forward

```text
the cp*T thermal transport          §5 finding 2, measured at 26-103% for cp(T)
SensibleEnthalpy unreachable        §5 finding 3
the two conduction boundary treatments disagree   §5 finding 4
IdealGasEOSTest "PREDICTED 21"      an obsolete constant now carried through THREE phases; the real
                                    count has been 30 since P17.3. Fix it in the P17.6 harness.
two B6 probes read a stale filename and print BLANK rather than 0 -- fail-open; also P17.4's
P13 never ran clang-format          22 files still fail it; P17.5's own 5 files are clean
```

## 10. Evidence index

```text
audit.md                            the pre-implementation audit, frozen
thermodynamic_contract.md           the contract and the dual-cp decision, frozen
acceptance_gate.md                  the frozen gate, sha256 ca84e432..., UNCHANGED at close
coverage_map.md                     CRITERION -> TEST, complete. The deliverable P17.4 lacked.
formulation.md                      the discrete equation, the sign convention, the limits
implementation.md                   what was built, and what was reused rather than duplicated
verification.md                     measured values, controls, conservation, CPU/GPU, conditioning
regression.md                       policy, prediction and result
AMENDMENT_thresholds.md             T7 and T8 were mis-derived by me
AMENDMENT_conduction_boundary.md    the wrong reuse target for conduction, and the 5.64 W leak
logs/PRESERVED_gate_attempt1/       the three initial failures, preserved verbatim
```

## 11. The single recommended next step

**P17.6 — Low-Mach Validation.**

```text
why   P17.5 leaves a conservative, thermodynamically closed total-energy formulation that nothing
      yet SOLVES. P17.6 is the first phase that exercises energy inside a converging solve, and it
      is the natural consumer: it needs exactly the rhoE <-> T <-> p coupling this phase verified,
      and its own §24 obligation ("do not destroy existing low-Mach behaviour") is the statement
      P17.5 could only partially make, because there is no energy loop to run yet.
why not P17.7/P17.8   a Riemann flux or a density-based solver would be built ON this equation, and
      the authorization is explicit that the total-energy equation must be correct independently
      first. It now is, for the state and the fluxes -- but not yet inside a solve, which is
      precisely the gap P17.6 closes.
one prerequisite to settle at ITS freeze, not during it: decide whether P17.6 requires the energy
      equation to be WIRED into CompressibleSIMPLE's outer loop. If it does, that wiring is the
      implicit-coupling work this phase deliberately left to P17.8, and the two phases' scopes
      overlap. Decide before freezing, the way the dual-cp hazard should have been.
```

Not started. No P17.6 file was created and no low-Mach code was written.
