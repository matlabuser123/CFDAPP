# P17.10 — finding: Toro's wave-speed estimate does not bound a strong head-on collision

```text
STATUS      REPORTED, NOT FIXED. It is in P17.7's flux layer, which P17.10 is not authorized to
            rewrite, and §2 is explicit that a CPU formula is not changed for GPU convenience --
            this is not for GPU convenience either, but the same restraint applies.
BLOCKS      P17.10's own gate: NOTHING. Every P17.10 criterion passes. CPU and GPU agree BITWISE,
            including on this case -- which is the point: equivalence is not correctness.
            P17.11 final qualification: THIS SHOULD BE SETTLED FIRST. See §5.
REPRODUCER  tests/integration/compressible/test_p17_10_dryrun.cpp,
            P17_10_Finding.DOCUMENTS_A_DEFECT_ToroWaveSpeedsDoNotBoundAHeadOnCollision
            -- runs in BOTH build configurations, needs no GPU.
EVIDENCE    logs/07_dryrun_and_finding.log
FOUND BY    a 3D equivalence case in which a Mach-2 stream met an inviscid wall.
```

---

## 1. What happens

For a **symmetric head-on collision** — two identical states approaching at ±u — Toro's
pressure-based (PVRS) wave-speed estimate stops bounding the signal speeds above a normal Mach
number of about **1.76**. `gamma = 1.4`, `rho = 1.2 kg/m^3`, `T = 300 K`, `a = 347.19 m/s`:

```text
  Ma      S_L estimated      S_L exact     rho*_L        exact p*/p1   ordering  branch
  0.58        -251.5155      -287.3372     2.1542e+00        2.132     held      LeftStar
  1.15        -135.8985      -262.0443     4.7320e+00        4.075     held      LeftStar
  1.44         -73.4523      -258.8148     9.3686e+00        5.406     held      LeftStar
  1.58         -41.3356      -258.9626     1.7167e+01        6.167     held      LeftStar
  1.73          -8.6937      -260.0987     8.4019e+01        6.993     held      LeftStar
  ------------------------------------------------- onset between 1.73 and 1.79 ---------------
  1.79          +4.5001      -260.8025    -1.6413e+02        7.341     VIOLATED  LeftSupersonic
  1.84         +17.7684      -261.6381    -4.2023e+01        7.701     VIOLATED  LeftSupersonic
  2.02         +57.9965      -264.8709    -1.3284e+01        8.843     VIOLATED  LeftSupersonic
  2.88        +267.0950      -293.1294    -3.2928e+00       16.016     VIOLATED  LeftSupersonic
```

```text
THE EXACT COLUMN IS NOT ANOTHER SOLVER'S OUTPUT. By symmetry the post-shock velocity is exactly 0,
so the velocity jump across the left shock is exactly u, and the normal-shock relations give

    u = a (2/(g+1)) (Ms - 1/Ms)   ->   Ms^2 - k Ms - 1 = 0,  k = u (g+1) / (2a)
    S_L = u - Ms a

which is closed form. The exact left wave speed is LEFT-RUNNING at every speed in the sweep --
roughly -260 m/s throughout, because the shock strengthens as fast as the flow accelerates. The
estimate instead RISES with Mach number and crosses zero.
```

## 2. Four things go wrong at once, and only one of them is caught

```text
1  THE ORDERING S_L <= S* <= S_R IS VIOLATED. At Ma 2.02 the estimate gives S_L = +58.0 while
   S* = 0 exactly (symmetry) and S_R = -58.0. The fan is inside out.

2  THE STAR DENSITIES GO NEGATIVE. rho*_L = rho_L (S_L - u_L)/(S_L - S*) has a positive denominator
   and a negative numerator once S_L > 0.

3  HLLC SELECTS THE LeftSupersonic BRANCH AND RETURNS THE UNDISTURBED UPSTREAM FLUX -- for a
   configuration in which a strong shock is forming (exact p*/p1 = 8.84 at Ma 2.02). This is the
   part that matters physically: the answer is not merely imprecise, it is the answer to a
   different problem.

4  hllcFlux() STILL REPORTS valid = true. The ordering is not part of validateState's predicate,
   and nothing downstream of the branch selection re-examines it.

CAUGHT BY: hllcFluxChecked()'s star-density test, and nothing else. Its message says the situation
is "impossible for admissible inputs under the proven denominator bound -- this indicates a defect
in the flux layer". The first clause is what these inputs contradict: both states pass
validateState. The second clause is correct.
```

```text
WHY THE PVRS ESTIMATE MISSES. It predicts p* from a linearised (acoustic) Riemann solve. At Ma 2.02
it gives p*/p1 = 3.82 against an exact 8.84 -- low by 2.3x -- so the shock factor
q_L = sqrt(1 + (g+1)/(2g) (p*/p_L - 1)) comes out 1.849 where the true shock Mach is 2.780, and
S_L = u - a q_L lands on the wrong side of zero. The estimate is a LINEARISATION, and a head-on
collision is the configuration furthest from linear.
```

## 3. What is NOT affected — checked, not assumed

```text
P17.7's flux qualification      its 14 bounding cases and 17 equivalence rows do not include a
                                symmetric head-on collision. They remain valid for what they cover.
P17.8's canonical cases         walls are TANGENTIAL there (v ~ 0 normal to the wall), so the
                                mirrored exterior state differs from the interior by ~0 and no
                                collision forms.
P17.9's shock validation        Sod, normal shock, contacts, compression and expansion -- all 1D,
                                none with an opposing pair above the threshold. Its 13 cases pass
                                unchanged in this phase's regression.
P17.10's equivalence claims     UNAFFECTED, and this is worth stating plainly: CPU and GPU produce
                                the SAME result on this case, bitwise. A pair of backends agreeing
                                exactly on a wrong answer is precisely why tolerance_policy.md §3
                                says bitwise equivalence "WOULD NOT PROVE that either is CORRECT".
```

## 4. What IS affected

```text
ANY CASE WITH A WALL-NORMAL MACH NUMBER ABOVE ~1.76. The inviscid wall builds its exterior state by
mirroring the normal velocity, so a supersonic flow normal to a wall IS a symmetric head-on
collision by construction. This is not exotic -- it is a blunt body, a closed end, or a step.

ANY STRONG INTERIOR COMPRESSION with a comparable relative approach Mach number. The threshold
above is derived for the symmetric case; a general pair fails when the estimate's q_L is small
enough that S_L crosses S*. Nothing in P17.10's scope measured where that boundary lies for
asymmetric states, and this document does not claim to know.

HOW IT SURFACES TODAY, which is the mitigating fact: DensityBasedSolver::assembleResidual calls
hllcFluxChecked, so the solver THROWS and solve() returns NonFiniteState naming the face and the
cell. A user of the density-based solver gets a loud refusal, not a silent wrong answer. A caller
of hllcFlux() DIRECTLY gets branch = LeftSupersonic, valid = true, and no indication at all.
```

## 5. What P17.10 did about it, and why that is the limit

```text
DID        reproduced it against a closed-form exact solution; bracketed the onset; recorded it
           here; left a permanent, executable reproducer that fails if the defect is ever fixed,
           so the finding cannot be quietly forgotten; fixed the TEST helper whose silent default
           produced the configuration (see below).

DID NOT    change HllcFlux.hpp, ThermodynamicProperties, or any wave-speed estimate. P17.10's
           authorization says "P17.10 is the only newly authorized implementation scope" and names
           CPU/GPU equivalence as that scope. Changing a wave-speed estimator is a numerical-scheme
           change with its own verification surface -- P17.7 needed a 14-case bounding study and a
           60-digit reference to qualify the current one, and a replacement deserves the same
           rather than a patch applied mid-phase.

RECOMMENDED FOR WHOEVER AUTHORIZES THE FIX, offered as options rather than a decision:
  (a) add the ordering S_L <= S* <= S_R to the validity predicate, so hllcFlux() REFUSES instead of
      returning a confident wrong branch. Smallest change; turns a silent error into a loud one but
      fixes no case.
  (b) use a two-rarefaction / two-shock adaptive p* estimate (Toro 9.4.2), which is exact in the
      strong-shock limit the PVRS form linearises away. This is the textbook remedy.
  (c) fall back to HLL bounds (Davis/Einfeldt) when the ordering fails. Cheapest robust option;
      loses the contact wave exactly where P17.9's amendment A1 showed it matters least (a strong
      shock, not a contact).
  ANY of these changes the numerical scheme and therefore needs its own frozen gate, its own
  non-vacuity control, and a re-run of P17.7's bounding study and P17.9's shock validation. None of
  them should be done inside P17.10.
```

## 6. A second, smaller finding — in this phase's own test harness

```text
The configuration that exposed all of the above was not chosen: it was DEFAULTED INTO. The 3D
equivalence case used a boundary helper that tested for the patch names "left" and "right" and fell
through to InviscidWall for anything else. createCartesian3D names its patches
xmin/xmax/ymin/ymax/zmin/zmax, so all six became walls and the intended through-flow became a
Mach-2 stream driven head-on into a solid surface.

That is trap 6 -- "an unmapped patch is not assumed to be a wall" -- reappearing in the very test
harness written to check for it, which is worth recording rather than quietly correcting. The
production code refuses an unmapped patch; the test helper guessed. It now throws on a name it does
not recognise, and the 3D case runs the through-flow it was meant to run (B2 cartesian_3d: 900
values, 0 differing).

The finding above is a real defect and would have been found eventually. It was found by an
accident, and the accident was a silent default.
```
