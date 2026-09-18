# CPU/GPU numerical equivalence and determinism — `logs/04`, `logs/09`

Existing repository tolerances were used unchanged; none was loosened.

## Per-operation comparison

| problem | size (unknowns) | CPU | GPU | abs error | rel error | tolerance | result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SpMV | 16 | `SparseMatrix::multiply` | `csrSpmvCuda` | 4.441e-16 | 2.228e-16 | 1e-12 | PASS |
| SpMV | 4096 | " | " | 8.882e-16 | 4.441e-16 | 1e-12 | PASS |
| SpMV | 65536 | " | " | 4.441e-16 | 5.685e-16 | 1e-12 | PASS |
| CG | 9216 | 92 it, res 5.410e-09 | 92 it, res 5.410e-09 | 8.882e-16 | 6.780e-16 | 1e-8 | PASS |
| CG | 25600 | 90 it, res 1.076e-08 | 90 it, res 1.076e-08 | 1.332e-15 | 1.106e-15 | 1e-8 | PASS |
| CG | 102400 | 88 it, res 2.067e-08 | 88 it, res 2.067e-08 | 1.776e-15 | 8.898e-16 | 1e-8 | PASS |
| BiCGSTAB | 9216 | 65 it, res 6.616e-09 | 65 it, res 6.785e-09 | 1.044e-10 | 7.966e-11 | 1e-8 | PASS |
| BiCGSTAB | 25600 | 66 it, res 8.594e-09 | 62 it, res 9.045e-09 | 2.821e-10 | 2.343e-10 | 1e-8 | PASS |
| BiCGSTAB | 102400 | 55 it, res 1.772e-08 | 62 it, res 1.493e-08 | 3.179e-09 | 1.593e-09 | 1e-8 | PASS |

**NaN = 0 and Inf = 0** in every comparison. CG reproduces the CPU iteration count exactly at all
three sizes; BiCGSTAB's count differs at the larger two, the expected consequence of floating-point
ordering in its reductions, and its solutions still agree well inside tolerance.

## Determinism

**Bitwise deterministic**, within a process on this device and driver:

- 5 repeated GPU SpMV runs at 128²: **0 of 65536** entries differ;
- a repeated GPU BiCGSTAB solve on the same system: **0 of 9216** entries differ, absolute
  difference 0.

Not claimed: determinism across devices, drivers or toolkit versions — none was tested. The
iteration-count differences against the CPU above are numerical, not non-determinism: each GPU run
reproduces itself exactly.

## Where equivalence fails

At production scale the GPU backend does not complete a SIMPLE solve that the CPU completes
(320² and 640², `PressureCorrectionFailure`). See `summary.md` §7 — that is this phase's stopping
point. The linear-solver comparisons above were repeated at exactly that size (102400 unknowns) and
pass, which places the defect outside the GPU linear algebra.
