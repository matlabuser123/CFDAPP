# P17.4 — numerical verification

Measured values, not verdicts. Every number below is from `logs/10_verify_values.log`
(`tools/verify_values.cpp`, run against the built `build/qual-final` libraries) or from the
gate log `logs/01_gate.log`. Mesh, unless stated: **10 x 10 over 20.0 x 20.0**, so `h = 2` and
**cell volume = 4**. That mesh is not cosmetic — a unit cell volume is what made control N2
vacuous, because multiplying by `V_P = 1` and omitting the factor are the same computation.

---

## 1. The operator against an analytical field

`U = (x^2/2, 0)` gives `div U = x` and `grad(div U) = (1, 0)` exactly, so the contribution has
a closed form: `-(2/3) mu * 1 * V`.

```text
quantity                                        measured                    expected
------------------------------------------------------------------------------------------
worst |div U - x|, interior cells               0                           0
worst |div U - x|, boundary-adjacent cells      10                          (see section 4)
deep-interior cells available on this mesh      36                          > 0 required
worst |grad(div U).x - 1|, deep interior        0                           0
the contribution, mu = 0.1, V = 4               -0.26666666666666666        -0.26666666666666666
worst |rhs - expected|, deep interior           0                           0
```

`worst |rhs - expected| = 0` is **exact**, not "within tolerance": Green-Gauss is exact for a
linear field, `div U = x` is linear, and no tolerance is consumed in the deep interior.

**The `V_P` factor is now observable.** With `V = 4` the expected value is
`-0.26666666666666666`; with `V = 1` it would be `-0.066666666666666666`. Dropping the factor
is therefore a **factor-4 error**, which is exactly what control N2 could not see when the
mesh gave `V = 1`.

## 2. Linearity in viscosity, and the inviscid limit

Probed at cell 22, centroid `(5, 5)`, deep interior. `mu = 0` must give zero rather than throw:
an inviscid call is a legitimate call.

```text
mu       measured rhs[22]            expected -(2/3) mu * 1 * V
--------------------------------------------------------------------
0.00     0                           -0
0.05     -0.13333333333333333        -0.13333333333333333
0.10     -0.26666666666666666        -0.26666666666666666
0.20     -0.53333333333333333        -0.53333333333333333
```

Every value matches the analytical figure to the last printed digit, and the ratios are exactly
2 and 4. The first row is exact zero, so `mu = 0` contributes nothing at all.

## 3. The incompressible limit — bitwise, not approximate

Solid-body rotation `U = (-y, x)` has `div U = 0` identically.

```text
worst |rhs| over all cells        0
cells with rhs != 0               0 of 100
```

This is the structural reason the term's absence was harmless through P16: for a
divergence-free field it vanishes identically, so enabling it **cannot** disturb an
incompressible result. That is measured here, not argued.

## 4. The boundary limitation — quantified, and why the claim is deep-interior only

`div U` is a **derived scalar with no physical boundary condition**, so its gradient is taken
with zero-gradient. The cost is real and is stated rather than hidden:

```text
worst |grad(div U).x - 1|      all cells        2.5
                              one cell in      2.5
                              two cells in     0        <- the deep interior
worst |div U - x|             boundary cells   10
                              interior cells   0
```

The zero-gradient velocity BC sets a boundary face value to the owner cell's, which halves the
one-sided difference and makes `div U` wrong in that layer; a Green-Gauss gradient of a wrong
neighbour then pollutes one cell further in. **Two layers, exactly as the structure predicts.**

So the accuracy claim is asserted in the deep interior only. Asserting it globally would fail a
correct implementation. An earlier draft of `formulation.md` claimed `div U` was "exact
EVERYWHERE"; that was measured **false** here and is withdrawn — see the correction in
`formulation.md` §5. The figure had been taken over interior cells only and the claim
generalised it.

## 5. The wired path — the term reaches the momentum assembly, and correctly

This is gate criterion **D3**, which the freeze specified and no test implemented until
control N6 exposed the omission (`AMENDMENT_N2_N6_vacuous.md` §2). Verified through
`assembleRelaxedCompressibleMomentumComponent`, not the standalone operator:

```text
claim                                                   measured
-----------------------------------------------------------------------------------
the wiring is LIVE: cells where on != off               100 of 100
the wiring is CORRECT, not merely present:
  worst |(on - off) - the standalone operator|          1.4432899320127035e-15
the DEFAULT is the disabled branch, bitwise:
  cells where default == off                            100 of 100
a divergence-free field is untouched:
  cells where on != off                                 0
```

The middle row is the load-bearing one: a wiring that is present but scaled, mis-signed, or
fed the wrong component would still change the RHS and so still satisfy "on differs from off".
Requiring the difference to **equal the operator cell by cell** is what makes it verification
rather than a smoke test.

### Why that residual is 1.44e-15 and not zero

In the wired path the contribution is added to an RHS that already holds the diffusion,
convection and pressure terms, so recovering it by subtraction is a cancellation of larger
numbers. In units of the value being recovered:

```text
|standalone|                    0.26666666666666666
eps                             2.220446049250313e-16
|standalone| * eps              5.9211894646675e-17
measured residual               1.4432899320127035e-15   =  24.4 eps relative
```

24 eps of accumulated round-off across a multi-term assembly is round-off, not a defect. The
test's tolerance is `1e-10 * (|standalone| + 1e-30)`, i.e. `2.67e-11` here — a generic
round-off allowance, **derived as a bound rather than fitted to this number**, and about
18,000x above what was measured. It is loose against round-off and still far tighter than any
realistic wiring error: a dropped `V_P` would be 0.2 absolute, a dropped `2/3` would be 0.13,
a sign flip 0.53. Discrimination is not left to the tolerance alone — controls N1, N2b and N3
demonstrate it empirically.

## 6. The other momentum terms, re-measured

From `logs/00_dryrun.log` and the gate's own rows.

```text
term          claim                                          measured
-------------------------------------------------------------------------------------------
pressure      p = 100 + 7x, rhs = -V dp/dx at deep-interior  worst relative error 0, EXACT
              cells
              boundary cell under zero-gradient              -2.333 vs -7: the BC's one-sided
                                                             gradient, not an error in the
                                                             pressure term -- which is why the
                                                             criterion is stated deep-interior
body force    rho f weighting, g = -9.81, V = 1
                rho 1.0                                      -9.8100000000000005
                rho 2.0                                      -19.620000000000001   exactly 2x
                rho 4.0                                      -39.240000000000002   exactly 4x
transient     d(rho U)/dt, NOT rho dU/dt
              rho 1 -> 2, u_old 3, dt 0.25, V 1
                diagonal  = rho_new V/dt                     8
                source    = rho_old V/dt u_old               12
                the WRONG rho_new-weighted form would give   24     -- a factor of 2 apart
```

The transient row is the one worth keeping: the two forms differ by a factor of 2 on this
input, and `TransientTermDistinguishesDRhoUDtFromRhoDUDt` asserts both the correct values
**and** that they differ from the wrong form by more than 1.0, so the test cannot pass if the
two were confused.

## 6b. Conservation — C2 and C4, both frozen with a bound and previously measured by nothing

`logs/11_conservation.log`. The bound form is the project's existing one, reused verbatim:
`bound = kAccum * eps * N * scale`, `kAccum = 8` **EMPIRICAL and labelled as such**, with `N` and
`scale` taken from the case so it is never an absolute constant.

```text
C2   uniform rho = 1.3, u = 2.0, p = 1000, mu = 0.05, dt = 0.5, on an 8x8 mesh
       cells N                              64
       scale = max|diag * u|                11
       worst |A u - b|                      1.7763568394002505e-15
       bound = 8 * eps * N * scale          1.2505552149377763e-12
       margin                               704 x inside the bound
```

Uniform flow is an **exact discrete solution** here for structural reasons, not numerical ones:
`sum_f m_f = 0` on a Cartesian mesh so convection vanishes, `lap(constant) = 0`,
`grad(constant) = 0`, and the transient term cancels because `rho_old == rho_new` and
`u_old == u_new`. Anything above round-off would be a real defect.

```text
C4   closed box, 7x5 mesh, IRREGULAR internal flux, arbitrary non-uniform field
       cells N                              35
       faces N_f                            82
       worst PER-CELL |contribution|        2.42472970985897
       GLOBAL sum over all cells            1.7763568394002505e-15
       bound = 8 * eps * N_f * scale        3.5318918671200724e-13
       worstCell / |global sum|             1.365e+15
```

The per-cell contributions are **O(1)** while their sum is at 1.8e-15 — fifteen orders of
magnitude of genuine cancellation, not an operator that happens to be zero. No divergence-free
assumption is needed: the cancellation is **per face**, because an internal face contributes `+x`
to its owner and `-x` to its neighbour.

**What C4 proves, stated precisely.** The frozen wording is "accumulation + boundary flux =
pressure + viscous + body force", which read literally is a property of a **converged solution**;
this phase assembles rather than converges. What is measured is the discrete property that makes
that balance possible at all — the assembled convective operator **creates nothing in the
interior**. That is narrower than the frozen sentence and is reported as narrower. The
converged-solution balance needs a solver run and is recorded as follow-on work.

## 6c. Invalid states and boundary momentum

```text
V1/V2   the dilatational operator rejects a non-finite viscosity and a size mismatch
        RejectsInvalidInputs                                          Passed
V3      a non-finite ASSEMBLED system raises NumericalError, distinct from the
        InvalidArgumentError a precondition raises. Pressure values are not
        precondition-validated, so a NaN or Inf there reaches the assembly.
        NonFiniteAssembledSystemThrowsNumericalError                  Passed
        The test first asserts the SAME call with finite pressure does NOT throw, so the
        throw is caused by the NaN and not by an invalid fixture.
V4      nothing is clamped -- invalid input throws rather than being silently repaired.
        Structural: no clamp exists on this path.
B1      a wall carries exactly zero normal mass flux
        MEASURED by a PRE-EXISTING test, tests/unit/physics/test_momentum_assembly.cpp,
        which asserts massFlux == 0 on "top" and "bottom" to 1e-12.
        That a wall still transmits pressure and viscous shear is STRUCTURAL only --
        argued from the assembler layout, isolated by no test.
B2      inlet/outlet convective signs: PARTLY measured. The same pre-existing file checks
        that inlet and outlet mass flows balance globally; the per-face momentum SIGN is
        not asserted anywhere.
B3      Symmetry carries zero normal momentum flux
        SymmetryCarriesZeroNormalMomentumFlux                          Passed
        The audit first found this uncovered and it was going to be reported NOT VERIFIED;
        section 12 of the frozen gate requires Group B to pass, so it was measured instead.
        See formulation.md section 7.
```

## 7. Suite results

Three gate runs measured the final state. Attempt 4 ran the full preservation groups on the
19-test suite; 4b and 4c re-measured the groups that read the momentum test file, after C2/C4 and
then B3 were added. P14/P15/P16 were not re-run in 4b/4c because the momentum tests live in
different executables -- **checked, not assumed**: the binary-overlap probe in
`logs/14_gate_4c.log` reports `NONE`. The full mandated regression re-runs all 2608 registered
tests in both trees regardless, and that is the authoritative preservation measurement.

```text
group                                      attempt 4   4b          4c (final)
-------------------------------------------------------------------------------
G6  clang-format on the changed files      exit 0      exit 0      exit 0
L5  physics::MomentumEquation              UNTOUCHED   UNTOUCHED   UNTOUCHED
D1  default disabled (text + behaviour)    1 / pinned  1 / pinned  1 / pinned
    the momentum suite                     19/19       22/22       23/23
    P17.4 rows passing                     11 of 11    14 of 14    15 of 15
L   low-Mach + MMS                         10/10       10/10       10/10
    the whole compressible suite           172/172     175/175     176/176
G1/G2  P14 transport                       68/68       not re-run  not re-run
       P15 thermal                         161/161     not re-run  not re-run
       P16 turbulence                      315/315     not re-run  not re-run
G8  GPU compressible momentum              0,0,0,0     0,0,0,0     0,0,0,0
```

Attempt 3 is deliberately absent from the table. Its G6 genuinely **FAILED** with exit 74 --
ten clang-format violations in the new test code -- and that failure is preserved rather than
re-measured into silence: it was fixed with `clang-format-18 -i` and has been clean since. Its
other rows are not cited at all, because a patch script rewrote the gate script while it was
running. See `logs/PRESERVED_gate_attempt3_edited_midrun/`.

The compressible-suite arithmetic closes exactly: **161** at the P17.3 baseline **+ 15** new
= **176**. P14 68, P15 161 and P16 315 each match the frozen baseline figure exactly.


## 7b. Non-vacuity controls — every one must FAIL

First pass (`logs/06_controls.log`), second pass after the fixes (`logs/08_controls_N2_N6b.log`):

```text
control                                          pass 1        pass 2        detected by
-----------------------------------------------------------------------------------------------
N1   drop the -(2/3) factor                      DETECTED 1    DETECTED 1    MatchesMinusTwoThirds
N2   drop the V_P factor                         *** VACUOUS ***  --          nothing: V_P was 1
N2b  the same, on a mesh with V = 4                --          DETECTED 1    MatchesMinusTwoThirds
N3   div U instead of grad(div U)                DETECTED 1    DETECTED 1    MatchesMinusTwoThirds
                                                                             + ScalesLinearly
N4   convection recomputes rho_f U_f              no code control -- F4 is STRUCTURAL: one
                                                  massFlux object is passed to both the momentum
                                                  assembly and the continuity evaluation, so
                                                  there is no second reconstruction to corrupt.
                                                  Recorded rather than faked.
N5   flip the transient source sign              DETECTED 3    DETECTED 3    the three transient
                                                                             tests
N6   default the option to ENABLED               *** VACUOUS ***  --         nothing IN THE
                                                                             PROBED SET
N6b  the same, probed against the momentum set     --          DETECTED 2    DefaultIsDisabled-
                                                                             Behaviourally
                                                                             + a PRE-EXISTING test
L5a  the untouched baseline                      instrument must be SILENT   UNTOUCHED
L5b  one comment appended to the guarded file    instrument must FIRE        *** TOUCHED ***
C2   in-test: perturb one cell by 1e-3           must exceed the bound       4.36e9 x over
C4   in-test: open the box, boundary flux 1.7    must exceed the bound       5.74e12 x over
```

N1 and N3 were **re-run rather than assumed**: the N2 fix changed the mesh of the very test they
rely on, and a fix that repaired one control while silently disarming two others would be worse
than the original defect. N5 was re-run as a control on the harness itself — its detectors were
untouched, so a moved verdict would have meant the harness had changed and no other verdict could
be trusted. It did not move.

All controls restored and **proved back at baseline by SHA-256**, with 0 surviving markers, and
the suite green at baseline afterwards.

### The one place the second pass contradicted my own diagnosis

N6b broke **two** tests, and only one was mine. The other,
`CompressibleRelaxedMomentumTest.AlphaOneMatchesUnrelaxedCompressibleMomentumComponentExactly`,
is **pre-existing** — present at `HEAD`, and `git diff --numstat HEAD` on its file is empty. The
original N6's fail-regex was `CompressibleSimpleMMS|LowMach|CompressibleSimpleTest`, which does
not match `CompressibleRelaxedMomentumTest`, so the control never ran the test that detects it.

So N6 was **a too-narrow probe, not a coverage gap** — the same class of error as the defective
L5 probe. What survives is narrower and still real: gate criterion **D3** was specified and
implemented by no test, because the pre-existing test detects only that *something changed*, not
that the change *equals the operator*. The full correction is in
`AMENDMENT_N2_N6_vacuous.md` §5-6.

## 8. What is NOT verified, stated plainly

```text
NOT VERIFIED   CPU/GPU equivalence for compressible momentum. There is no GPU compressible
               momentum path to compare against -- G8 probes cuda/ and include/cfd/gpu/ for
               compressibleMomentum, CompressibleRelaxed, dilatational and DilatationalStress
               and finds 0 hits on all four. Reported [~], not [x], and no path was invented
               to satisfy the checkbox.
NOT VERIFIED   accuracy of the dilatational term within two cells of a boundary. Measured to
               be wrong there (section 4) and deliberately not claimed.
NOT VERIFIED   a formal order-of-accuracy study for the dilatational term. The convective
               scheme is first-order upwind and the system MMS band is [0.55, 1.30] at
               nx <= 32, which control N6 showed is NOT sensitive enough to detect this term
               at all. Manufacturing a forcing that includes it is follow-on work.
NOT VERIFIED   case-file reachability. No case-format key reaches the option.
NOT VERIFIED   3D. requireTwoDimensional rejects 3D at entry; every claim here is a 2D claim.
```
