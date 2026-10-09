# P18.2 — determinism (§36)

```text
SCOPE, stated first so it is not over-read: this is the ASSEMBLY AND CLOSURE layer only.
G10's solver-level claim -- repeated runs in separate processes and across launch configurations --
belongs to P18.8/P18.9 and is NOT made here.
```

---

## 1. What is measured, and how

`ClosureAndAssemblyAreBitwiseDeterministic`, 20 repeats on a 5×4 mesh with two species and unequal
diffusivities:

| Quantity | Comparison | Result |
|---|---|---|
| raw face fluxes `J^0_{i,f}` | `EXPECT_EQ` — bitwise | identical, all 20 repeats |
| corrected face fluxes `J_{i,f}` | `EXPECT_EQ` — bitwise | identical |
| assembled diagonal `a_P` | `EXPECT_EQ` — bitwise | identical |
| assembled right-hand side `b_P` | `EXPECT_EQ` — bitwise | identical |

Bitwise, not `EXPECT_NEAR`: determinism is not an accuracy property, and a tolerance would let a
genuine nondeterminism hide. This is the convention G10 inherits from the existing species suite and
from P17.10.

## 2. Why it holds — the mechanisms, named

```text
FIXED SPECIES ORDER        Sigma_f accumulates over ascending species index, left to right, into one
                           Real. The registry stores names in a vector in insertion order, and that
                           order is part of its value (P18.1), so it is stable across copies.
NO UNORDERED ITERATION     nothing in the flux or assembly path iterates a hash container. The
                           registry's map is used for lookup only.
NO PARALLEL REDUCTION      the face loop and the species sum are serial. Adding OpenMP to either
                           would change the accumulation order and must be treated as a numerical
                           change, not an optimisation.
SAME PRIMITIVES            the raw flux is computed with the same cfd::discretization calls, in the
                           same operand order, as the assembler -- which is amendment A1's
                           requirement and is what makes the species sum cancel to round-off rather
                           than merely nearly.
UPWIND ON A FIXED SIGN     the correction's upwind direction is decided by sign(Sigma_f) with a
                           deterministic tie-break (>= 0 takes the owner), so a Sigma_f of exactly
                           zero does not make the choice arbitrary.
```

## 3. Related determinism evidence from the same phase

```text
speciesDiffusiveFaceFluxes repeated on an unchanged state     bitwise identical (this test)
speciesTransportAssembly repeated on an unchanged state       bitwise identical (this test)
the state layer, inherited from P18.1                         compositionSum bitwise over 100
                                                              repeats and across a copy
the inner deferred-correction iteration                       converges to the same fixed point,
                                                              measured at logs/09_ -- 5 passes,
                                                              every linear solve Converged
```

## 4. Not measured here

```text
solver-level repeatability across PROCESSES        P18.8 / P18.9, G10
CPU/GPU equivalence                                P18.8, G9 -- no GPU species path exists
residual histories and iteration counts across     not asserted bitwise: the BiCGSTAB iteration
  repeated full runs                               count is reported in logs/09_ (1-3 per inner
                                                   pass) but P18.2 makes no claim on it, because a
                                                   Krylov iteration count is not part of the
                                                   assembly layer this phase owns
```
