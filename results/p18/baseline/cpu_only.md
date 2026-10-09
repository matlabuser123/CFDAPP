# P18.0 baseline — CPU-only

```text
RESULT: matches the P17.11 measured baseline EXACTLY. 0 failures.
        AND it confirms species is not CUDA-dependent: 76 species rows pass with CUDA OFF.
```

| Quantity | Measured | P17.11 baseline | Verdict |
|---|---:|---:|---|
| registered | 2553 | 2553 | match |
| matching the frozen exclusions | 13 | 13 | match |
| **selected** | **2540** | 2540 | match |
| **passed** | **2493** | 2493 | match |
| **failed** | **0** | 0 | match |
| **skipped** | **0** | 0 | match |
| **disabled (Not Run)** | **47** | 47 | match |
| ctest `out of` | 2493 | 2493 | match |
| **runtime** | **1675 s** | 1527 s | +9.7%, machine load |

```text
configuration ASSERTED after configure:
    build=Release  gui=OFF  cuda=OFF  openmp=ON
build          exit 0 (94 s)
staleness      exit 0
ctest exit     0

src/libcfdcore.a   1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a
                   IDENTICAL to P17.9's and P17.11's recorded CPU-only archive. P17.11 established
                   that this archive is reproducible from a from-scratch rebuild, so a change here
                   would be real evidence of a source change rather than build noise.

47 disabled, against 49 in Release+CUDA. The 2-test difference is GPU-only DISABLED_ tests absent
with CUDA OFF -- consistent, not a discrepancy.

SPECIES ROWS PASSED: 76 -- the same count as Release+CUDA.
  This is the measurement behind gpu_inventory.md's conclusion: the species path has no CUDA
  dependency because it has no GPU implementation at all. Every species test runs identically with
  CUDA OFF.
```
