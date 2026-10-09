# P18.2 — the species boundary lag: fixed (§17, §18, G4(c))

```text
STATUS: FIXED on the species path. G4(c) MET.

P18.0 measured   335 outer Picard iterations for species against thermal's 2 on an identical
                 pure-Neumann problem. 167x.
G4(c) criterion  <= 2 outer iterations. DERIVED in the frozen gate from the thermal path's
                 measured 2 -- "match the path that is already correct", not "fewer than 335".
MEASURED NOW     <= 2, with the lagged control needing far more on the same problem.
```

---

## 1. The defect, and why the exact form is right

```text
src/transport/ScalarTransport.cpp, the boundary branch, BEFORE:

    const Real phiB = scalarBc->boundaryValue(phi[ownerId], distance);   // the PREVIOUS iterate
    builder.add(ownerId, ownerId, terms.coefficient);                    // a non-zero diagonal
    rhs[ownerId] += terms.boundaryValueCoefficient * phiB;               // the lagged value

DERIVED, not copied from thermal. For dphi/dn = g the boundary value is phi_b = phi_P + g d, so the
diffusive flow into the owner is

    Df (phi_b - phi_P) = Df (g d) = Gamma |S| g          with Df = Gamma |S| / d

which contains NO phi_P. The row holds -flux_into_owner, so the exact contribution is a ZERO
diagonal and a fully known right-hand side, and boundaryValue(0.0, d) returns exactly g d.

The lagged form instead adds +Df to the diagonal and -Df phi_P^{old} to the RHS. Those cancel only
at convergence: it is a FIXED-POINT ITERATION FOR A QUANTITY THAT HAS A CLOSED FORM. That is why the
converged answer was never wrong and the cost was 167x.
```

## 2. The six steps §17 requires, in order

```text
1. PRESERVE THE REPRODUCER      P18.0's probe and its 335-iteration measurement are untouched at
                                results/p18/logs/11_lag_probe.log and results/p18/audit/boundary_lag.md.
                                P18.2 added no edit to either.
2. FREEZE THE EXPECTED BEHAVIOUR  G4(c) was already frozen before P18.2 began: <= 2 outer
                                iterations. Its sha256 was re-verified at P18.2 open and close.
3. DEMONSTRATE THE LAG ON THE PRE-FIX PATH   LaggedGradientBoundaryNeedsFarMoreIterations runs the
                                same problem with GradientBoundaryTreatment::Lagged and requires
                                > 2 iterations. It is a PERMANENT test, not a one-off measurement.
4. THE MINIMAL CORRECTION       two lines in the boundary branch, guarded by a named selector. No
                                other behaviour changed; see section 3.
5. RERUN THE REPRODUCER         ExactGradientBoundaryConvergesInAtMostTwoOuterIterations: <= 2.
6. PERMANENT REGRESSION TEST    both of the above are in tests/unit/species/test_species_transport.cpp
                                and run in every configuration.
```

## 3. §18 — the control asserts the ITERATION COUNT, not the field

```text
Section 18 warns that "a test that only compares final converged fields may be too weak because an
iteration-lag bug can disappear after convergence."

THAT IS NOT A HYPOTHETICAL HERE -- it is measured. ExactAndLaggedAgreeOnceConverged runs both
treatments on the same problem and asserts the converged fields agree to 1e-8. They do. So a field
comparison would pass under the defect, and the ONLY observable that discriminates is the count of
outer assemblies before the change falls below tolerance.

Both tests are therefore required, and they say opposite things on purpose:
    ExactAndLaggedAgreeOnceConverged        the answer is the SAME      -> the lag is a cost defect
    LaggedGradientBoundaryNeedsFarMore...   the cost is NOT the same    -> and it is detectable
```

## 4. Why the fix is opt-in, and why that is not a hedge

`assembleScalarDiffusionContribution` is the **only** scalar transport assembly in the codebase, and
all six production turbulence equations route through it. Changing their assembled matrices is not
P18.2's business — §17 says "do not fix unrelated boundary infrastructure".

```text
enum class GradientBoundaryTreatment { Lagged, Exact };   default Lagged

THIS IS P14'S OWN DOCUMENTED PATTERN, for the same reason, twice already:
  FaceDiffusivityInterpolation  "Selecting it is deliberate and never automatic: the default keeps
                                 every existing result bit-for-bit."
  requireNonPositiveSp          "defaults to OFF, because turning it on by default would change the
                                 behaviour of the existing turbulence call sites, which is not
                                 P14's business."

Species passes Exact; everything else keeps Lagged. MEASURED (logs/10_preservation_final.log):
  transport characterization (frozen matrix, sparsity, RHS)   3 / 3
  transport unit suite                                        22 / 22
  P14 migration equivalence (frozen solved field)             3 / 3
  turbulence unit suite                                       246 / 246
  thermal unit suite                                          163 / 163
Both frozen oracles carry their own CanDetectAnAssemblyChange control, so they are proven able to
fail -- a pass from an instrument that cannot fail would prove nothing.
```

```text
ONE STRUCTURAL DETAIL, checked rather than assumed: the Exact branch adds no far-cell entry. The
three-point Dirichlet reconstruction is the only case with a non-zero farCellCoefficient, and it
applies only to a PRESCRIBED-value face -- which the Exact branch never handles. So there is nothing
to add there by construction, not merely nothing observed.
```

## 5. Debt, recorded rather than left implicit

```text
TURBULENCE STILL CARRIES THE LAGGED TREATMENT. Flipping it to Exact would fix the same defect for
all six turbulence equations, and the P18.0 audit recommends it -- but it needs its own frozen gate
and a re-derivation of the P14 migration oracle, whose frozen values encode the lagged behaviour.
That is a separate authorization. Reported in TODO.md.

ALSO RECORDED: tests/unit/transport/test_transport_characterization.cpp -- the frozen oracle for
P14's matrix, sparsity and RHS -- contains ZERO FixedGradient cases. The gradient branch of the only
scalar transport assembly in the repository was never characterised, which is why the lag survived
P14's own migration gates. P18.2 adds Exact-path coverage from the species side; characterising the
LAGGED branch belongs with whoever owns the turbulence fix.
```
