# TD-001 — ACCEPTANCE GATE AMENDMENT A1

```text
amends      GATE.md criterion C4, sha256 0749efbf1120db1fa6689a5b86be2e5045a20722413e6c2770e4605d5c7c319b
reason      C4 as frozen is UNACHIEVABLE BY CONSTRUCTION -- not failed by the implementation
preserved   the original failure is recorded verbatim in verification.md section 2-3
authorized  by the user, after the failure was reported and before this amendment was written
frozen      before the replacement is measured
```

**The original C4 is not deleted and its failure is not reclassified.** `GATE.md` keeps its text;
this file states what replaces it and why, and the replacement is derived here before any number is
measured against it.

---

## 1. Why the original C4 cannot be met

```text
C4 (original)   max_cell |sum_i Y_i - 1| <= 4*eps*N_species = 2.66e-15
```

`4*eps*N_species` is **P18.1's invariant for a `MassFractionState` constructed from exact data**. It
is the right bound in the place P18.1 uses it: a composition assembled from declared numbers, where
the only error is the floating-point summation itself.

TD-001 applied it to a composition produced by an **iterative linear solve**. The closure of such a
field cannot be better than the solve's own convergence, and the solve stops at a relative tolerance
of `1e-8`. Measured: the best-conserving source treatment reached `4.92e-07` — eleven orders above
`2.66e-15`, and the shortfall is the solver tolerance, not a modelling error.

**No implementation of any source treatment can meet the original C4.** That makes it mis-derived
rather than demanding: a machine-precision bound asked of a quantity whose floor is set by the
linear solver.

## 2. The replacement, derived

The error has two structurally different parts, and the amendment separates them because they have
different floors.

### Part 1 — the CONSTRUCTED state keeps P18.1's bound unchanged

The initial composition really is built from exact declared data by `MassFractionState`, so
`4*eps*N_species` applies to it exactly as P18.1 intends. This half of the original C4 is correct
and is kept verbatim.

### Part 2 — the SOLVED field is bounded by the solve, and the bound is proved by SCALING

Write the N per-species transport equations as `A_i Y_i = b_i`. With operator splitting the transport
step carries **no reaction source**, and with equal species diffusivities every `A_i` is the same
matrix `A`. Summing:

```text
A (sum_i Y_i) = sum_i b_i
```

and `sum_i b_i` is the right-hand side of the equation whose exact solution is the constant 1,
because the inlet composition sums to 1 (enforced at parse time, gate C11) and the zero-gradient
patches contribute nothing. So **the exact discrete solution has `sum_i Y_i = 1` identically**, and
the computed deviation is purely the accumulated linear-solve error of N separate solves.

That gives the form of the bound but not a defensible constant: turning the residual bound
`||r_i|| <= tol * ||b_i||` into an error bound on `Y_i` needs `||A^-1||`, and inventing a
conditioning allowance to fit `4.92e-07` would be deriving the threshold from the observation — which
§5.3 forbids.

**So the criterion is a SCALING criterion, not an absolute one.** If the deviation is solver error,
it must fall in proportion to the solver tolerance. If it is a conservation defect in the
discretisation, it will not move at all. That distinction is exactly what needs proving, and it
needs no fitted constant:

```text
C4a  the CONSTRUCTED initial state:  max_cell |sum_i Y_i - 1| <= 4*eps*N_species   (P18.1, unchanged)

C4b  the SOLVED composition, SCALING:
       solve the same case at relative tolerances tol and tol/10
       deviation(tol/10) / deviation(tol)  <=  0.25
     i.e. a tenfold tighter solve must reduce the sum deviation by at least fourfold.

C4c  the SOLVED composition, POSITIVITY:
       min_cell min_i Y_i >= 0            exactly, no tolerance

C4d  the SOLVED composition, a REPORTED absolute figure, not a gate:
       max_cell |sum_i Y_i - 1| is recorded with the tolerance that produced it.
```

### Why `0.25` for a tenfold tolerance reduction

A pure linear-solve error would fall by the full factor of ten. The threshold is deliberately much
weaker than that — fourfold — because the measured deviation is a maximum over cells of a sum of N
independent solve errors, and the stopping criterion is relative to each `||b_i||` rather than to the
sum. Demanding the ideal tenfold would risk failing for a reason that is not a defect. Fourfold is
far beyond what a fixed discretisation error could produce (which is a factor of **one**), so the
criterion still separates the two cases decisively.

### Why C4c is new, and is not a weakening

The original C4 never stated positivity separately, and the failure exposed why it must be: the
fully explicit treatment achieved the *best* sum closure (`4.92e-07`) while driving `Y_A` to
**-0.297**. A criterion set that gates only the sum would have rewarded the physically invalid
variant. A4c is therefore **stricter** than the original gate, not looser — it rules out a solution
the original would have accepted.

## 3. Non-vacuity — the controls this amendment must survive

| id | control | must produce |
| --- | --- | --- |
| A1-N1 | the **Patankar-split steady** variant (deviation `3.00e-01`, from a source-treatment asymmetry rather than solver error) | **C4b FAILS** — the deviation does not fall with tolerance, because it is not solver error |
| A1-N2 | the **fully explicit steady** variant (`Y_A = -0.297`) | **C4c FAILS** — proves the positivity criterion is load-bearing |
| A1-N3 | the original `4*eps*N` bound applied to the SOLVED field | still fails at any tolerance — recorded to show the amendment was necessary, not convenient |

**A1-N1 is the one that matters.** It is the variant whose number looks worst (`0.300`), and the
amendment must reject it for the right reason — not because `0.300 > 5e-7`, but because it does not
scale. If A1-N1 passed C4b, the criterion would be measuring nothing.

## 4. What this amendment does NOT change

* every other criterion in `GATE.md` — C1, C2, C3, C5, C6, C7, C8, C9, C10, C11, C12 — stands
  verbatim, including C10's **bitwise** chemistry-off preservation;
* the negative controls N1–N5 in `GATE.md`;
* the regression baselines 325 / 32 / 339;
* the definition of done, other than C4 being read as C4a–C4d.

## 5. Dry-run obligation

C4b requires two solves at different tolerances, so the criterion is only meaningful if the case's
linear tolerance is actually reachable at both. That is verified before the criterion is used, and
recorded in `logs/20_a1_dryrun.log`.
