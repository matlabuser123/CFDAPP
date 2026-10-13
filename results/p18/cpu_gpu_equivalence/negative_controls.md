<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Negative controls

Every control in the frozen table, what it is aimed at, and its measured effect. A criterion whose
control cannot fail it is not a criterion.

## Design: flags, not source edits

Each control is a field on `DeviceSpeciesControls`, passed to the kernel, so **a control run and a
production run are the same binary**. The one exception is `--fmad=true`, which is a compile option
and cannot be a runtime flag; it is a second translation unit with byte-identical kernel bodies.

## The table

| Control | Aimed at | Measured effect |
| --- | --- | --- |
| N1 species index permuted | **E1** | 3 072 values differ, worst 3.31 × 10¹⁵ ULP, worst relative 0.611746 |
| N2 one weight for all species | **E1** | 1 024 values differ, worst 4.21 × 10¹⁵ ULP, worst relative 0.483333 |
| N3 `ΣJ_i` correction omitted | — | **N/A — transport not implemented**, the frozen gate's own note |
| N4 Arrhenius exponent sign flipped | E3, E4 | 6 144 values differ; **3.52 × 10²² ×** the `q` bound; worst relative 9.21 × 10⁶ |
| N5 `M_i` omitted in `ω_i` | **E5** | 3 072 values differ, worst relative 15 |
| N6 heat-release sign flipped | E6 | 1 024 values differ, **9.44 × 10¹⁸ ULP**, worst relative 2 |
| N7 stale temperature | E7, E9 | link one |
| N8 one reaction omitted | **E5** (not E4) | 4 096 values differ, worst relative 2.01 × 10⁻⁴ |
| N9 stale composition in recovery | E7 | link one |
| N10 different timestep | E9 | `max \|ΔT\|` 0.0124043 K, `max \|ΔY\|` 3.26358 × 10⁻⁶ |
| N11 invalid state silently accepted | E2, E11 | link one — **found a real device defect** |
| N12 full-field transfer in the resident loop | E12 | a field-sized download is registered |
| `--fmad=true` | **E1** → G9 condition (4) | `R_mix` 662/4 096, `cp_mix` 543/4 096; `M_mix` 0/4 096 as expected |

## Four controls were pointing at the wrong criterion, or at none

Found by building `coverage_map.md`, not by any test failing:

1. **N1 and N2 run against the reaction sources** reported zero difference — indistinguishable from
   dead controls. They perturb the species index inside the **mixture property** functions only;
   `deviceConcentration` and `deviceSpeciesSources` never consult them. Re-aimed at E1.
2. **N8 classified against the bounded `q` criterion** measured **0** in `q`'s bound units. Omitting
   a reaction leaves the progress rates untouched and changes the **accumulation**. Reclassified.
3. **N10 was never implemented.** Now closed.

## The instrument that scores the controls was itself defective

The first ULP comparator reinterpreted two doubles as signed integers and subtracted. That overflows
when the signs differ, and the wrapped value read as **0** — so **N6, a sign flip, the largest error
the suite can inject, was scored as a perfect match** while its relative difference was 2.

Fixed with a monotonic unsigned key, correct across zero and across the sign. The comparator now:

* is **self-tested on planted disagreements** before it is trusted — identical values, adjacent
  doubles, the two zeros, a pair straddling zero, a sign flip, and a planted infinity (`DRYRUN_1B`);
* treats a non-finite pair as the largest possible disagreement and **counts** it, rather than
  skipping it and leaving the worst-case figures looking clean;
* fails closed on a **length mismatch** instead of comparing the common prefix;
* lives in **one shared header** used by both the gate and the dry-run, so the code under self-test
  and the code the gate trusts are the same code.

## G9 condition (4): the one control that could not go where it was first put

A2's first draft asserted the bounded criteria on `k`/`q` would still reject FMA contraction.
Measured in the bound's own units: contraction moves `q` by **0.843356**; the libm difference the
bound must **admit** is **3.13865**. The contraction effect is **smaller**, so no threshold that lets
a correct implementation pass can reject it.

Condition (4) therefore sits on **E1**, a zero-tolerance criterion on the **same translation unit**
with the **same single** `--fmad=false` property. The dry-run asserts the ordering that forces this,
so if a future toolchain reverses it the suite says so rather than leaving the allocation unexamined.
