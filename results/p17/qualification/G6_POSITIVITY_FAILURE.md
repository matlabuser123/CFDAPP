# P17.11 — G6(e) fails on the final integrated tree

Required by §44: when a gate problem is found during qualification, stop that gate and produce the
observed defect, why the original criterion is or is not invalid, any proposed amendment, and the
impact on qualification — **before** applying anything.

```text
GATE            G6 -- Riemann / HLLC flux correctness
SUB-CRITERION   G6(e) POSITIVITY: "rho and p remain strictly positive for admissible input"
                acceptance: "strictly positive -- a boolean, no tolerance"   (P17_GATES.md:201-240)
P17.7 MAPPING   Group S, criterion S1: "rho*_L > 0 and rho*_R > 0 | boolean, no tolerance"
                and Group W, criterion W2: "S_L < S* < S_R strictly, every reference case"
VERDICT         G6 = PARTIAL. (a)(b)(c)(d)(f) pass. (e) FAILS for a characterized class of
                admissible input.
AMENDMENT       NONE PROPOSED. The criterion is correct. The implementation does not meet it.
```

---

## 1. The observed defect

For a **symmetric head-on collision** — two identical states approaching at ±u, which is exactly
what an inviscid wall produces when the flow is normal to it — above a normal Mach number of about
**1.76**:

```text
  Ma      S_L estimated      S_L exact     rho*_L         ordering S_L<=S*<=S_R
  1.73          -8.6937      -260.0987     8.4019e+01     held
  ------------------------------------------- onset between 1.73 and 1.79 ------------
  1.79          +4.5001      -260.8025    -1.6413e+02     VIOLATED
  2.02         +57.9965      -264.8709    -1.3284e+01     VIOLATED
  2.88        +267.0950      -293.1294    -3.2928e+00     VIOLATED
```

Both input states pass `validateState()` — the flux layer's own admissibility predicate — which the
reproducer asserts explicitly before evaluating anything. So these are **admissible inputs**, and
G6(e) admits no scope qualifier on that phrase.

Reproducer, runs in both build configurations, needs no GPU:
`P17_10_Finding.DOCUMENTS_A_DEFECT_ToroWaveSpeedsDoNotBoundAHeadOnCollision`
(`results/p17/cpu_gpu_equivalence/BLOCKING_FINDINGS.md`, found during P17.10).

## 2. Why the original criterion is NOT invalid — so no amendment is proposed

```text
G6(e) says positivity is "a boolean, no tolerance". That is exactly right for a positivity property:
a star density is positive or it is not, and there is no scale on which -3.29 is nearly positive.
Nothing about the criterion is dimensionally wrong, mis-scaled, contaminated or vacuous -- the four
reasons this project has previously accepted for amending a frozen criterion.

WHAT WENT WRONG IS COVERAGE, NOT THE CRITERION. P17.7's S1 test iterates the 15-case independent
reference set:

  low_pressure  moving_contact  near_sonic_left  sod_atmospheric  sod_literature  sod_y  sod_z
  stationary_contact  strong_density_ratio  strong_pressure_ratio  supersonic_left
  supersonic_right  tangential_shear  uniform_oblique  uniform_x

NONE of them is a head-on collision. supersonic_left and supersonic_right are single-direction; the
Sod variants start from rest. So S1 passed on every case it was given, and the property it asserts
is false outside them. This is the project's own recorded lesson -- non-vacuity is not coverage --
landing on G6.

AMENDING G6(e) TO EXCLUDE HEAD-ON COLLISIONS WOULD BE WEAKENING A VALID THRESHOLD, which the
standing prohibitions forbid and which the P17.9 amendment policy explicitly rules out. It is not
proposed. G6 is reported as PARTIAL instead.
```

## 2a. In fairness to P17.7: the residual risk was named, not overlooked

```text
P17.7's own summary says, under what it does NOT claim:

    "NOT CLAIMED  universal signal bounding. Toro PVRS is measured to bound in all 14 reference
     cases, spanning pressure ratios to 1e5, density ratios to 1e6, Mach ~2.4 and a near-sonic
     point. It is an ESTIMATE; no proof of universal bounding is offered."

That is exactly the risk that has now materialised, stated in advance and in the right words. P17.7
did not assume universal bounding; it measured bounding on its case set and said plainly that no
proof existed beyond it.

WHAT P17.7 DID OVER-CLAIM is narrower, and it is one word. Its summary also says star states are
"proven (W3) and tested (S1-S4)". W3 proves the DENOMINATOR is bounded away from zero. Star-density
POSITIVITY does not follow from W3 -- it follows from the ordering S_L < S* < S_R, which is W2, and
W2 is tested "every reference case", not proven. So the gap is between an accurate disclaimer about
the wave-speed ESTIMATE and an inaccurate word about what the star-state proof covers.

THIS MATTERS FOR THE VERDICT. G6(e) is still not met on the final tree -- a disclaimer elsewhere in
the evidence does not satisfy a gate criterion. But the failure is the realisation of a risk the
phase identified and recorded, not an unflagged assumption, and the distinction belongs in the
qualification record.
```

## 3. Root cause, stated precisely

```text
It is NOT that W3 is wrong. W3 bounds the contact DENOMINATOR away from zero:
    |denom| >= (rho_L a_L + rho_R a_R)(1 - 4 eps)
and that still holds. W3 is what justifies having no epsilon guard, and it is untouched.

The positivity of rho*_K does not follow from W3. It follows from the ORDERING:
    rho*_L = rho_L (S_L - u_nL) / (S_L - S*)
    with S_L < S*  the denominator is negative, and with S_L < u_nL the numerator is negative,
    so the quotient is positive.
When W2 fails -- S_L > S* -- the denominator flips sign and rho*_L goes negative. S1 is a
CONSEQUENCE of W2, and W2 is a TESTED property over a case set ("every reference case"), not a
proof.

P17.7's summary states "admissible star states for admissible inputs, proven (W3) and tested
(S1-S4)". The word "proven" over-reaches: W3 proves the denominator is bounded, not that the
ordering holds. That distinction is the whole defect.

WHY THE ESTIMATE FAILS. Toro's pressure-based (PVRS) estimate predicts p* from a LINEARISED Riemann
solve. At Ma 2.02 it gives p*/p1 = 3.82 against an exact 8.84, so the shock factor
q_L = sqrt(1 + (g+1)/(2g)(p*/p_L - 1)) comes out 1.849 where the true shock Mach is 2.780, and
S_L = u - a q_L lands on the wrong side of zero. A head-on collision is the configuration furthest
from linear.
```

## 4. Impact on qualification, gate by gate

```text
G6  PARTIAL. (a) consistency, (b) supersonic limits, (c) antisymmetry, (d) rotational consistency
    and (f) the exact-Riemann comparison all pass on the final tree (35 Hllc assertions, 0 failed).
    (e) fails for the class above.

G7  UNAFFECTED, and scoped. Its cases are Sod, normal shock, stationary and moving contacts,
    supersonic advection, compression and expansion. None is a head-on collision; none approaches
    the threshold. G7's 13 tests pass on the final tree.

G8  UNAFFECTED. CPU and GPU agree BITWISE, including on the defective case -- which is the clearest
    possible demonstration that equivalence is not correctness, and is why G8 is a separate gate.

G3, G4  UNAFFECTED. Conservation is a property of the discrete flux being single-valued per face;
    it holds whatever the flux value is. A wrong-but-conservative flux is still conservative.

G9  UNAFFECTED. Determinism is unaffected by the value being deterministically computed.

G10 UNAFFECTED by this finding specifically.
```

## 5. What is actually at risk in use

```text
REACHABLE BY A SUPPORTED CONFIGURATION. The inviscid wall builds its exterior state by mirroring the
normal velocity, so a supersonic flow normal to a wall IS a symmetric head-on collision by
construction: a blunt body, a closed end, a forward-facing step. This is not exotic.

THE SOLVER REFUSES RATHER THAN ANSWERING WRONGLY. DensityBasedSolver::assembleResidual calls
hllcFluxChecked, whose star-density test catches exactly this and throws; solve() returns
NonFiniteState naming the face and cell. So a user of the density-based solver gets a loud refusal.
That is the mitigating fact, and it is why this is PARTIAL rather than a silent-wrong-answer
catastrophe.

A DIRECT CALLER OF hllcFlux() GETS NO WARNING. It returns branch = LeftSupersonic, valid = true, and
a flux that is the undisturbed upstream flux for a configuration in which a strong shock is forming.
Nothing in the validity predicate examines the ordering.

NO EXISTING QUALIFIED RESULT IS INVALIDATED. P17.7's 15 reference cases, P17.8's canonical cases and
P17.9's shock cases are all outside the failing class; their walls are tangential (v ~ 0 normal to
the wall) so the mirrored exterior state differs from the interior by ~0.
```

## 6. What would close it — for whoever authorizes the work, not for P17.11

```text
Three options, none of which P17.11 is authorized to take, and all of which change the numerical
scheme and therefore need their own frozen gate, non-vacuity control, and a re-run of P17.7's
bounding study and P17.9's shock validation:

  (a) Add the ordering S_L <= S* <= S_R to the validity predicate so hllcFlux() REFUSES instead of
      returning a confident wrong branch. Smallest change. Turns a silent error into a loud one and
      fixes no case -- it would make G6(e) hold vacuously by rejecting the input, which is honest
      only if the refusal is what the caller wants.
  (b) Use a two-rarefaction / two-shock adaptive p* estimate (Toro 9.4.2), exact in the strong-shock
      limit the PVRS form linearises away. The textbook remedy, and the only one that makes the
      failing cases WORK rather than merely fail loudly.
  (c) Fall back to HLL bounds (Davis/Einfeldt) when the ordering fails. Cheapest robust option;
      loses the contact wave exactly where P17.9's amendment A1 showed it matters least.

P17.11's scope is qualification. §2 of its authorization permits a fix only where "a genuine
qualification failure exposes a correctness defect that must be fixed to satisfy an already-defined
gate" -- and each option above is a numerical-scheme change, not a defect repair. Doing any of them
inside a qualification phase, without a frozen gate written in advance, is precisely the pattern
this project's gate protocol exists to prevent.
```

## 7. Status

```text
G6            PARTIAL -- five of six sub-criteria pass; (e) fails for admissible head-on input
AMENDMENT     none proposed, none applied. The criterion stands unweakened.
EVIDENCE      preserved: BLOCKING_FINDINGS.md, the executable reproducer, and this document
P17 OVERALL   cannot be declared COMPLETE while a required gate is not PASS
```
