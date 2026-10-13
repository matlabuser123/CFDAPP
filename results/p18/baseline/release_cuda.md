# P18.0 baseline — Release+CUDA

```text
RESULT: matches the P17.11 measured baseline EXACTLY. 0 failures.
```

| Quantity | Measured | P17.11 baseline | Verdict |
|---|---:|---:|---|
| registered | 2776 | 2776 | match |
| matching the frozen exclusions | 13 | 13 | match |
| **selected** | **2763** | 2763 | match |
| **passed** | **2712** | 2712 | match |
| **failed** | **0** | 0 | match |
| **skipped** | **2** | 2 | match |
| **disabled (Not Run)** | **49** | 49 | match |
| ctest `out of` | 2714 | 2714 | match |
| **runtime** | **2226 s** | 2061 s | +8.0%, machine load |

```text
configuration ASSERTED after configure, not merely echoed:
    build=Release  gui=OFF  cuda=ON  openmp=ON
build          exit 0 (52 s)
staleness      exit 0 -- 0 stale, 0 unverifiable
ctest exit     0
invocation     OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen exclusions>

src/libcfdcore.a   188e50397ed13718f449f369ef996768cc4011ac1048955dc312da92716390f6
                   IDENTICAL to the archive P17.8, P17.9 and P17.11 all recorded.
cuda/libcfdcuda.a  1e2e0f914afd40ef1f624f03cdb2bbee8aa141d46d780ad1bbcc3c29c404290a
                   recorded, NEVER used as an identity check -- nvcc is not bit-reproducible.

the 2 skipped are P17.10's sanitizer control kernels, which skip unless
CFD_P17_10_SANITIZER_CONTROL=1. Deliberate.
the 49 disabled are DISABLED_ macros in the tree, counted explicitly -- the inherited harness
reported "Not Run : 0" for every P17 phase while these were not running.

species rows passed: 76
```
