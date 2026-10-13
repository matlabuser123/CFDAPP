# P14 migration -- closeout

## Result

```
Six turbulence equations
        |
        v
one scalar transport assembly   (cfd::transport)
        |
        v
frozen characterization oracle  (matrix + sparsity + RHS + solved field)
        |
        v
no migration switch             (ScalarAssemblyRoute: zero hits)
        |
        v
no duplicate turbulence assembler
```

## What moved, and what deliberately did not

| stays in `solveRelaxedScalarTransport` | moved to `cfd::transport` |
|---|---|
| Patankar implicit under-relaxation | diffusion contribution |
| `fixRow` imposed cells (applied AFTER relaxation) | convection contribution |
| BiCGSTAB solve | implicit Patankar source |
| the floor | |
| re-assertion of imposed values over the floor | |

`cfd::transport` never sees an imposed cell, so it needed NO imposed-cell
semantics. That corrected the audit's Step 1, which had proposed adding them:
the wrapper never hands a built matrix to anyone, it drives a
`SparseMatrixBuilder` that it later relaxes and modifies itself, and
`cfd::transport` already exposed all three contributions at builder level.

## Migration order

1. epsilon (k-epsilon) -- first ON PURPOSE: the only equation using imposed
   cells, so the riskiest path was exercised immediately rather than last
2. k (k-epsilon)
3. k + omega (k-omega)
4. k + omega (SST)

Equivalence gate and unit suites green after every group; full channel
regression green after epsilon and again after all six.

## The oracle that replaced the equivalence tests

| layer | file | what it catches |
|---|---|---|
| sparsity (raw CSR row offsets + column indices) | `tests/unit/transport/test_transport_characterization.cpp` | re-decomposition, duplicate-entry handling, an entry that stops being stored |
| matrix coefficients | same | any value change |
| RHS | same | implicit/explicit redistribution |
| non-orthogonal matrix + RHS | same, on a DISTORTED mesh | correction-specific change |
| solved field | `tests/unit/turbulence/test_p14_migration_equivalence.cpp` | relaxation, fixRow, solve, floor, imposed-value re-assertion |
| non-vacuity control | both files | proves the oracles can detect a real assembly change |

Values are CHARACTERIZATION data, not mathematical truth. If they go red, the
question is whether the discretisation was meant to change; if it was, re-derive
and justify new values rather than preserving old bits.

## Two defects found along the way

1. **`LegacyMatrixEquivalenceWithNonOrthogonalCorrection` was vacuous.** It
   claimed to protect "an explicit RHS term and a far-cell implicit entry" but
   ran on `createCartesian2D`, where the non-orthogonal correction is
   identically zero. Found because the first frozen non-orthogonal capture came
   back byte-identical to the steady one. The replacement uses
   `createDistortedQuad2D` and asserts the correction actually changes the
   system, permanently, so it cannot silently go vacuous again.
2. **An orphaned declaration.** The deletion pass removed
   `assembleScalarDiffusionContribution`'s comment block but left its
   declaration in `KEpsilonEquation.hpp` with no definition. It compiled and
   every test passed because nothing called it. Found by the post-deletion
   sweep, which is why that sweep is a scripted gate step
   (`tools/p14_deletion_sweep.sh`) rather than an informal grep.

## Final sweep state

| pattern | expected | actual |
|---|---|---|
| `ScalarAssemblyRoute` | no hits | no hits |
| `LegacyMatrixEquivalence` | none in `tests/`; `results/` is evidence | as expected |
| turbulence-namespace assembly symbols | none | only `cfd::transport::`-qualified calls |
| `assembleScalarTransportEquation` | canonical P14 only | canonical P14, its tests and docs |

## Documentation

`ScalarTransport.hpp`'s production-consumer statement was FALSE before this
migration (the audit flagged it). It is now true, and was corrected last --
after every gate was green, not before.
