# GPU-PIPE-001 — final 20²–640² benchmark

Same harness, machine, case and settings as the Phase-1 baseline
(`cfd_benchmark_cuda_end_to_end`, lid-driven cavity, medians of 3 except 640²=1).
Optimized tree = Phase 2 (fused reductions) + Phase 3 (synchronization removal).

Committed P6/P7 evidence under `results/performance/cuda_end_to_end/` was hashed before the run and
restored after; `sha256sum -c` verified **5/5 OK**.

## End-to-end (`simple_solve_seconds`, median)

| grid | cells | CPU | GPU baseline | GPU now | speed-up baseline | **speed-up now** | GPU time saved |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20² | 400 | 0.284 s | 12.401 s | 5.780 s | 0.023× | **0.049×** | −53.4 % |
| 40² | 1 600 | 1.382 s | 25.450 s | 15.695 s | 0.054× | **0.088×** | −38.3 % |
| 80² | 6 400 | 6.334 s | 37.382 s | 22.908 s | 0.169× | **0.277×** | −38.7 % |
| 160² | 25 600 | 10.886 s | 20.696 s | 15.500 s | 0.526× | **0.702×** | −25.1 % |
| 320² | 102 400 | 26.953 s | 21.715 s | 15.199 s | 1.241× | **1.773×** | −30.0 % |
| 640² | 409 600 | 89.304 s | 29.372 s | 24.812 s | 3.040× | **3.599×** | −15.5 % |

**Best end-to-end speed-up: 3.599× at 640².** GPU time fell at every grid, by 15.5–53.4 %.

Every GPU run is `ran_cleanly=yes`; all six report `MaxIterations`, which is the fixed
outer-iteration budget this harness sets, not a failure.

## Crossover — unchanged

```text
baseline   GPU slower at 20², 40², 80², 160²      faster at 320², 640²
optimized  GPU slower at 20², 40², 80², 160²      faster at 320², 640²
```

**The crossover is still between 160² and 320².** At 160² the GPU improved from 0.526× to 0.702×
but remains **slower than the CPU** — stated directly, as required. The phase moved 160² closer to
parity; it did not cross it.

## Transfers and reductions

| grid | D2H baseline | D2H now | change | H2D | D2H MB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20² | 73 840 | 53 132 | **−28.0 %** | unchanged | unchanged |
| 40² | 133 476 | 95 733 | **−28.3 %** | unchanged | unchanged |
| 80² | 159 930 | 114 626 | **−28.3 %** | unchanged | unchanged |
| 160² | 82 869 | 59 312 | **−28.4 %** | unchanged | unchanged |
| 320² | 53 446 | 38 217 | **−28.5 %** | unchanged | unchanged |
| 640² | 40 492 | 28 939 | **−28.5 %** | unchanged | unchanged |

Reductions per Krylov iteration, **7.00 → 5.00–5.05** across the whole ladder.

D2H **bytes** and all H2D traffic are unchanged by design: fusion removes round trips, not volume,
and volume was never the bottleneck (79–101 µs per call regardless of payload, Phase 1 §5).

## Correctness across the ladder

Krylov iteration counts are **identical to the baseline at every grid** — 10 516 / 19 047 / 22 818 /
11 838 / 7 639 / 5 785. The solver follows the same trajectory; it simply reaches it with fewer host
round trips.

## Reading this table honestly

Runtime here is a **cross-session** comparison, and Phase 1 measured ~10 % session-to-session
variance. The authoritative speed-up evidence is the **paired same-session** measurement in
`phase2-reductions/` and `phase3-sync/` (gpu_solve −30 % to −42 %). This ladder corroborates that
and supplies the CPU comparison and crossover, which paired GPU-only runs cannot.

Transfer, synchronization and iteration counts are exact and carry no such caveat.
