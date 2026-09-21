# GPU-PIPE-001 rho-carry — dependency proof

Established **before** any code change. The question: can iteration *k+1*'s
`rho = dot(rHat_, r_)` be computed at the end of iteration *k*, fused with that iteration's
residual-norm reduction?

## 1. Where the operands change

`GpuBiCGSTAB::solveImpl`, `cuda/kernels/GpuLinearSolverCuda.cpp`:

**`r_` is written in exactly two places.**

```text
setup             waxpby(1.0, b_, -1.0, v_, r_)      r := b - A*x0
end of loop body  waxpby(1.0, s_, -omega, t_, r_)    r := s - omega*t
```

**`rHat_` is written once, in setup** — `deviceCopy(rHat_, r_)` — and is fixed for the whole solve
(the code's own comment says so, and `rHatNorm = b0` depends on it).

**Nothing between the end-of-iteration write to `r_` and the next iteration's `rho` touches either.**
Walking the loop body from `rhoOld = rho;` to the next `dot(rHat_, r_)`:

```text
waxpby(1.0, p_, -omega, v_, p_)   reads p_, v_        writes p_
waxpby(1.0, r_, beta, p_, p_)     reads r_, p_        writes p_
applyPreconditioner(p_, pHat_)    reads p_            writes pHat_
spmv(A, pHat_, v_)                reads pHat_         writes v_
...
waxpby(1.0, r_, -alpha, v_, s_)   reads r_, v_        writes s_
spmv(A, sHat_, t_)                reads sHat_         writes t_
waxpby(1.0, s_, -omega, t_, r_)   reads s_, t_        WRITES r_   <- first write
```

`r_` is read but never written until the end of the body. **Therefore `dot(rHat_, r_)` evaluated at
the end of iteration *k* and at the start of iteration *k+1* sees identical operands**, and the
reduction is being *moved*, not recomputed or approximated.

## 2. Bitwise identity

Both quantities come from the same kernel over the same operands, so the products, the block
partition, the per-block tree and the final in-order host sum are all unchanged:

* `rho` — `dot(rHat_, r_)` moved earlier in time, same inputs → bit-for-bit equal.
* `residualNorm` — was `l2Norm(r_)` = `sqrt(dot(r_, r_))`; now `sqrt` of the first quantity of
  `dot2(r_, r_, rHat_, r_)`. Phase 2 established (and tested, 120/120) that a fused quantity is
  bitwise identical to the separate `dot()` it replaces.

**Iteration 1 is the special case.** Its `rho` has no previous iteration to be carried from. But at
setup `rHat_` is a *bitwise copy* of `r_`, so `dot(rHat_, r_)` and `dot(r_, r_)` multiply the same
pairs in the same order — identical. The existing setup reduction `l2Norm(r_)` already computes
`dot(r_, r_)` internally, so taking the raw value costs **no extra reduction**:

```cpp
const Real r0DotR0 = dot(r_, r_);
const Real b0      = std::sqrt(r0DotR0);
Real rhoNext       = r0DotR0;          // == dot(rHat_, r_) for iteration 1
```

Note `b0 * b0` would **not** be bitwise `dot(r_, r_)` — `b0` is its square root. The raw dot is
taken deliberately for that reason.

## 3. Validity on every path

| path | valid? | why |
| --- | --- | --- |
| normal iteration | **yes** | operands unchanged between the two evaluation points (§1) |
| first iteration | **yes** | `rHat_` is a bitwise copy of `r_` at setup; raw dot reused (§2) |
| converged exit | **yes** | `rhoNext` computed, then the function returns without reading it — no state touched |
| non-finite residual exit | **yes** | same: computed, unused, returns |
| MaxIterations exit | **yes** | last iteration prefetches a `rho` no iteration will consume; inert |
| breakdown (rho, beta, rHatDotV, tDotT, tDotS, omega) | **yes** | breakdown returns immediately and recomputes `l2Norm(r_)` for `finalResidual` via its own reduction, which is unchanged |
| **restart** | **n/a** | `GpuBiCGSTAB` has **no restart path** — this is the documented CPU/GPU asymmetry recorded as debt in `TODO.md` and `results/gpu-pcorr-001/` §7. Every breakdown returns. There is no path that resets `r_` mid-loop and would invalidate a carried `rho`. |

The breakdown test itself is unchanged: it reads `rho` and `rNorm`, and `rNorm` is assigned from
the same iteration's `residualNorm`, so both are available with the same values they had before.

## 4. Cost of the inert prefetch

On the single iteration that exits, `rhoNext` is computed and discarded. That is not an extra round
trip: it is one additional *quantity* inside a reduction the iteration performs regardless. Measured
`reductionQuantities` per Krylov iteration is **7 before and 7 after** — the arithmetic is
unchanged; only its packaging into round trips changed.

## 5. Verified, not just argued

| grid | krylov | groups before | groups after | per iteration | pressure residual |
| --- | ---: | ---: | ---: | ---: | --- |
| 160² | 2 338 | 11 684 | 9 346 | **5.00 → 4.00** | `0.0018474694761252055` → identical |
| 320² | 2 293 | 11 458 | 9 165 | **5.00 → 4.00** | `0.0011156223124478067` → identical |
| 640² | 2 160 | 10 798 | 8 638 | **5.00 → 4.00** | `0.00067291708800742717` → identical |

Krylov iteration counts, continuity residuals and mass imbalance are identical at every grid.
D2H calls and synchronizations each fall **−20.0 %**.
