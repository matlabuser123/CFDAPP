# GPU-PIPE-001 — architecture decision for the remaining residency items

Recommendation: **B — close GPU-PIPE-001 as PARTIAL and move persistent CFD fields, the
GPU-resident pressure solve and the GPU-resident SIMPLE loop into a separately authorized phase.**

This is a measured recommendation, not a scoping preference. The numbers below are from
`baseline/`, `phase3-sync/`, `phase4-persistence/` and `phase5-rho-carry/`.

## 1. Where the remaining cost actually is

After Phase 3 and the rho-carry, at 160² with 8 outer iterations:

```text
synchronizations      15 580      all of them reduction round trips
D2H calls             15 604      15 580 reductions + 24 solution downloads
H2D calls                 76      matrix values, RHS, initial guess -- 3.2 per solve
allocations               28      constant, independent of solve count and grid
```

`synchronizations == reductionGroups` exactly. There is no other host-device wait left in the
solver path.

## 2. What each remaining item could remove

| item | host/device calls removable | bytes removable | measured time attributable |
| --- | ---: | ---: | --- |
| **Persistent CFD fields** | ≤ 76 H2D per run | ≤ 36.7 MB per run at 160² | H2D is **0.22 s of a 15.5 s solve** at 160² (`upload_seconds`), and most of it is matrix values that genuinely change every outer iteration and cannot persist |
| **GPU-resident pressure solve** | **24** D2H per run (the per-solve `result.solution` download) | 13–62 % of D2H *bytes* | bytes cost ~nothing: the round trip is **79–101 µs regardless of payload** (baseline §5). 24 calls out of 15 604 is **0.15 %** |
| **GPU-resident SIMPLE loop** | 0 additional beyond the two above | 0 | the loop's per-iteration host work is assembly, which is CPU code; moving it is a rewrite, not a transfer removal |

**The decisive number: 24 of 15 604 D2H calls.** Phase 6's headline target — never downloading the
solution vector only to re-upload it — removes **0.15 %** of the remaining round trips. It looks
large by *bytes* (up to 62 % at 20², 13 % at 640²) and that is exactly the trap Phase 1 was built to
avoid: bytes are not what this path pays for.

## 3. What it would cost

Persistent fields and a GPU-resident SIMPLE loop require:

* device-resident ownership for `u`, `v`, `w`, `p`, `p'`, face fluxes and momentum coefficients,
  with explicit dirty-on-host / dirty-on-device state for each;
* a solver interface that accepts and returns device-resident vectors instead of
  `cfd::algebra::Vector`, without breaking the CPU backend that shares it;
* changes to `SIMPLE.cpp`, which is the single production path behind **both** CLI and GUI
  (`ProjectRunner`), and to `CompressibleSIMPLE`, which mirrors its structure;
* new lifetime rules exactly where `CLAUDE.md` §12 names a stop condition — "ambiguous CPU/GPU
  ownership".

For a measured saving of 24 round trips and ~0.2 s of upload time per run.

## 4. Where the remaining gain actually is

The same measurements point at a different target. Per Krylov iteration the solver now performs:

```text
4 reduction round trips   (7 scalar quantities, fusion factor 1.75x)
~12 kernel launches       (waxpby, spmv, preconditioner -- all asynchronous since Phase 3)
```

Those 4 round trips are 15 580 of the 15 604 remaining D2H calls. Reducing them further means
either fewer reductions per iteration (the algorithm needs all seven scalars; three fusions have
already been taken and the remaining ones are genuinely dependent), or **removing the host from the
iteration's control flow**, which means device-side breakdown/convergence decisions — a different
and much larger piece of work than field residency, and one whose cost/benefit is not yet measured.

Notably, Phase 2 already measured that naive device-side finalisation **loses** (25 % slower at
640²), so "keep the scalars on the device" is not automatically a win and would need its own
investigation.

## 5. Recommendation

**B.** Close GPU-PIPE-001 as PARTIAL with four checklist items verified and three left `[ ]`, and
raise a separate phase if and when device-side Krylov control flow is worth investigating. Do not
mark the residency items `[x]` — they are not implemented, and the checklist should not be
reinterpreted to close them.

What was achieved against what the phase set out to do:

```text
D2H calls          27 280 -> 15 604     -42.8 %   (160^2, paired)
synchronizations   58 480 -> 15 580     -73.4 %
reductions/Krylov    7.00 -> 4.00       -42.9 %
gpu_solve           3.208 -> 1.948 s    -39.3 %
speed-up at 640^2   3.040x -> 3.599x
```

all bitwise identical, with the crossover unchanged between 160² and 320².
