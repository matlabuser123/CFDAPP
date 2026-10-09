# P18.3 — determinism (§22)

```text
SCOPE: the mixture-property layer. G10's solver-level, cross-process claim is P18.8/P18.9's and is
NOT made here.
```

---

## 1. Measured

| Quantity | Repeats | Comparison | Result |
|---|---:|---|---|
| `M_mix` | 1000 | bitwise | identical |
| `R_mix` | 1000 | bitwise | identical |
| `cp_mix` | 1000 | bitwise | identical |
| `cv_mix` | 1000 | bitwise | identical |
| `gamma_mix` | 1000 | bitwise | identical |
| `rho` | 1000 | bitwise | identical |
| `h_mix` | 50 | bitwise | identical |

```text
bitwise differences over 1000 repeats x 6 quantities : 0      logs/03_closure_probe.log
```

Bitwise, not `EXPECT_NEAR`: determinism is not an accuracy property, and a tolerance would let a
genuine nondeterminism hide. This is the convention G10 inherits from the existing species suite and
from P17.10.

## 2. Why it holds — the mechanisms, named

```text
FIXED SPECIES ORDER      every sum -- 1/M_mix, R_mix, cp_mix, h_mix -- accumulates over ASCENDING
                         species index, left to right, into one Real. The registry stores names in a
                         vector in insertion order, and that order is part of its value (P18.1), so
                         it is stable across copies.
NO UNORDERED ITERATION   nothing in the property path iterates a hash container. The registry's map
                         is used for name lookup only, and the property accessors do not use it.
NO PARALLEL REDUCTION    the species sums are serial. Adding OpenMP to them would change the
                         accumulation order and must be treated as a numerical change, not an
                         optimisation.
NO CACHED STATE          MixtureThermodynamics stores no mutable state and memoises nothing, so a
                         repeat cannot return a differently-rounded cached value. properties()
                         constructs a fresh ThermodynamicProperties each call.
DERIVED, NOT STORED      cv and gamma are computed by ThermodynamicProperties from cp and R at
                         construction, so there is no second value to fall out of step.
```

## 3. Not measured here

```text
cross-PROCESS repeatability          P18.8 / P18.9, G10
CPU/GPU equivalence                  P18.8, G9 -- there is no GPU mixture path
solver-level determinism             no solver consumes the mixture properties yet
```
