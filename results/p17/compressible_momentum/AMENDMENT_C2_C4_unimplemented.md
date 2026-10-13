# P17.4 amendment — criteria C2 and C4 were frozen with a bound and implemented by nothing

Raised 2026-09-27, while auditing the frozen gate against the tests that exist.
The frozen gate (`acceptance_gate.md`, sha256 `ded6feaa…`) is **unchanged**.

---

## 1. The finding

`acceptance_gate.md` §5 (Group C — conservation) freezes five criteria and gives C2 and C4 a
derived bound:

```text
C2  Uniform flow (rho, U, p all constant) gives a momentum residual at round-off. | derived bound
C4  The integral momentum balance closes.                                         | derived bound
```

Mapping every criterion onto the tests that actually run:

```text
C1  interior telescopes, global sum is boundary-only     measured in logs/00_dryrun.log
C2  uniform flow residual at round-off                   *** NO TEST ***
C3  variable-density constant-mass-flux flux             structural, via N4, documented
C4  the integral momentum balance closes                 *** NO TEST ***
C5  linear pressure, net force matches analytically      PressureForceMatchesMinusVolume...
```

Two of five conservation criteria were measured by nothing. The gate would have reported a
clean pass with both unverified.

This is the **third** instance of the same failure mode in P17.4, and the pattern is worth
naming: **a criterion can be written, reviewed, frozen, and still be measured by nothing.**

```text
D3   specified, never implemented      found by control N6 coming back vacuous
D1   probed by grepping a header       found by control N6 coming back vacuous
C2   specified, never implemented      found by auditing the gate against the test list
C4   specified, never implemented      found by the same audit
```

D3 and D1 were found because a negative control had nothing to break. C2 and C4 had **no
control assigned at all**, so nothing would ever have surfaced them. Only reading the gate
against the suite did.

## 2. The bound, reused rather than re-derived

```text
bound = kAccum * eps * N * scale
  kAccum = 8        EMPIRICAL, labelled as such, taken verbatim from P17.1/P17.3
  eps    = 2.2204460492503131e-16
  N      the case's own cell count (C2) or face count (C4)
  scale  measured from the case
```

Never an absolute constant, so it scales with mesh and magnitude.

## 3. Why neither criterion can have a planted-defect control

Both C2 and C4 exercise `physics::assembleConvectionContribution` and its siblings — code in
`physics/MomentumEquation.{cpp,hpp}`, which criterion **L5 forbids P17.4 to touch** and whose
SHA-256 the freeze recorded. Planting a defect there to build a control would violate L5 to
test L5-protected code.

So each criterion carries an **in-test control that perturbs the input instead of the code**,
and the control is an assertion inside the test: if the perturbation fails to break the
criterion, the test fails and says the criterion is vacuous. A criterion whose control lives
inside it cannot silently become vacuous later.

## 4. Measured — `logs/11_conservation.log`

```text
C2   uniform rho = 1.3, u = 2.0, p = 1000, mu = 0.05, dt = 0.5, 8x8 mesh
       cells N                          64
       scale = max|diag * u|            11
       worst |A u - b|                  1.7763568394002505e-15
       bound = 8 * eps * N * scale      1.2505552149377763e-12
       verdict                          WITHIN, margin 704x
     CONTROL  one cell perturbed by 1e-3
       worst |A u - b|                  0.0054499999999997328
       exceeds the bound by             4.35806e+09 x        DETECTED
```

```text
C4   closed box, 7x5 mesh, IRREGULAR internal flux, arbitrary non-uniform field
       cells N                          35
       faces N_f                        82
       worst PER-CELL |contribution|    2.42472970985897     <- non-vacuity
       GLOBAL sum over all cells        1.7763568394002505e-15
       bound = 8 * eps * N_f * scale    3.5318918671200724e-13
       verdict                          WITHIN
       worstCell / |global sum|         1.365e+15            <- 15 orders cancelled
     CONTROL  one boundary face given flux 1.7
       GLOBAL sum                       2.0260093662508583
       exceeds the bound by             5.73633e+12 x        DETECTED
```

The `worst PER-CELL` row is what makes C4 a real statement: the per-cell contributions are
O(1), so a global sum at 1.8e-15 is **fifteen orders of magnitude of genuine cancellation**,
not an operator that happens to be zero.

### A note on what C4 actually proves, stated precisely

The frozen wording is "accumulation + boundary flux = pressure + viscous + body force". Read
literally that is a property of a **converged solution**, not of an assembly, and this phase
assembles rather than converges. What is tested here is the discrete property that makes that
balance possible at all: **the assembled convective operator creates nothing in the interior**,
because each internal face contributes equal and opposite amounts to its two cells. With the
boundary flux zero the global sum must therefore vanish for any field whatsoever.

That is narrower than the frozen sentence and it is stated as narrower rather than dressed up
as the whole thing. The converged-solution balance needs a solver run and is recorded as
follow-on work, not claimed here.

## 5. What was NOT done

```text
NOT DONE   editing acceptance_gate.md to match what the tests happened to cover
NOT DONE   deleting C2/C4, or reporting them as "structural" to avoid implementing them
NOT DONE   weakening kAccum, or fitting the bound to the measured numbers -- the bound form
           was reused verbatim and the measured values came in 704x and 1e15 inside it
NOT DONE   claiming the full converged momentum balance, which is not what was measured
DONE       implemented both criteria as gated tests, each with its own in-test control, and
           recorded the pattern that three criteria in one phase were unmeasured
```

## 6. Consequence for the suite count

```text
test_compressible_momentum.cpp TESTs   7 at the freeze -> 20 now
  9   planned at the freeze
  +1  IsWiredIntoTheRelaxedMomentumAssembly     D3, after N6 was vacuous
  +1  DefaultIsDisabledBehaviourally            D1, after N6 was vacuous
  +2  UniformFlowResidualIsAtRoundOff           C2, this amendment
      ConvectiveAssemblyIsGloballyConservative  C4, this amendment
  = 13 new tests
```

The regression prediction moves with it, declared **before** the run and with the frozen
figure kept beside it: Release+CUDA `2593 + 13 = 2606` registered / `2531 + 13 = 2544`
executed; CPU-only `2391 + 13 = 2404` / `2331 + 13 = 2344`. A test count is an arithmetic
consequence of the suite, not a tolerance, and no threshold moved.
