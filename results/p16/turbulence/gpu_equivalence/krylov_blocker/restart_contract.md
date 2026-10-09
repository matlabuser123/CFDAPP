# The CPU BiCGSTAB restart contract, and its GPU mapping

Step 1 (audit the CPU reference) and Step 2 (map its state onto the GPU) of the
authorized shared-solver fix. Read against `src/algebra/BiCGSTAB.cpp`, which was
**not modified** — it is the reference, and the GPU was brought to it.

---

## 1. What the CPU actually does

### Which conditions permit a restart

Exactly **three**, and they are all the same kind of thing: an inner product of
the recurrence that has cancelled to the rounding level of its own terms, as
judged by `cancelledToRoundingLevel` (P12-MESH-004's scale-relative criterion).

| site | inner product | line |
|---|---|---|
| top of the loop | `rho = (rHat, r)` | `BiCGSTAB.cpp:161` |
| after the first spmv | `rHatDotV = (rHat, v)` | `BiCGSTAB.cpp:181` |
| after the second spmv | `tDotS = (t, s)` | `BiCGSTAB.cpp:231` |

### Which conditions do NOT permit a restart

Every `!std::isfinite(...)` test returns `Breakdown` immediately — `rho`,
`rHatDotV`, `beta`, `alpha`, `tDotT`, `tDotS`, `omega`. So does
`tDotT < DBL_MIN`: `(t, t)` is a sum of squares and cannot cancel, so a zero
there means `t` itself is zero or has underflowed, which no restart can fix.

This distinction is the whole point. A cancelled inner product says the
**Lanczos biorthogonality** has broken down; the iterate is still healthy and a
fresh shadow residual recovers it. A non-finite scalar says the **iterate** is
gone.

### What bounds the restart

There is **no counter**. The bound is:

```cpp
if (!(rNorm < restartResidual)) return Restart::Stop;
```

`restartResidual` is the residual norm at which the current Krylov sequence
started. A restart is taken only if the residual has *strictly decreased* since
then — so a breakdown in a sequence's own first iteration (the true breakdown a
restart cannot cure) still reports `Breakdown`, and `rNorm` only changes when an
iteration completes, which caps restarts at one per completed iteration. It is
bounded, and bounded without a magic number.

### The restart actions, in order

```cpp
r = b - A.multiply(x);        // the TRUE residual, recomputed
rNorm = l2Norm(r);
if (!std::isfinite(rNorm)) return Restart::Stop;
if (converged(rNorm))       return Restart::Converged;
rHat = r;  rHatNorm = rNorm;  restartResidual = rNorm;
rhoOld = 1.0;  alpha = 1.0;  omega = 1.0;
v = Vector(n, 0.0);  p = Vector(n, 0.0);
++result.restarts;
```

Two properties are load-bearing and easy to lose:

* **The residual is recomputed, not carried.** The recurrence residual is
  precisely what has just been shown to be untrustworthy.
* **A restart never declares success.** `converged(rNorm)` returns a *distinct*
  outcome, so a restart that lands on the tolerance reports `Converged` rather
  than silently continuing.

Only `v` and `p` are reset. `s`, `t`, `pHat`, `sHat` are not — they are written
before every use. The fix resets exactly this set and nothing more.

### The loop shape

```cpp
Index completed = 0;        // "a restart completes none"
while (completed < settings_.maxIterations) {
  const Index iter = completed + 1;
  ...
  completed = iter;         // banked at BiCGSTAB.cpp:257, end of a full body
```

A restart `continue`s **without** advancing `completed`. Writing this as
`for (iter = 1; iter <= max; ++iter)` with a `continue` would charge each
restart an iteration, which the reference does not do.

---

## 2. Mapping onto the GPU solver

`bicgstabCore` in `cuda/kernels/GpuLinearSolverCuda.cpp`. Every CPU operation
has an existing device primitive; **no new kernel, no new buffer and no host
transfer of the matrix, RHS or solution was introduced.**

| CPU | GPU | note |
|---|---|---|
| `r = b - A.multiply(x)` | `spmv(deviceMatrix_, x_, v_)` then `waxpby(1.0, b_, -1.0, v_, r_)` | the same two-call pattern setup already uses at line 452; `v_` is free because it is about to be zeroed |
| `rNorm = l2Norm(r)` | `l2Norm(r_)` | already used by `breakdown()` |
| `rHat = r` | `deviceCopy(rHat_, r_)` | the same call setup uses at line 454 |
| `v = Vector(n, 0.0)` | `fill(v_, n, 0.0)` | already used at line 486 |
| `p = Vector(n, 0.0)` | `fill(p_, n, 0.0)` | already used at line 487 |
| `rHatNorm` (fixed) | **was `const Real rHatNorm = b0`** | had to become mutable: a restart replaces `rHat_`, so it replaces its norm |
| `restartResidual` | **did not exist** | added |
| `completed` | **did not exist** | the `for` loop was converted to the CPU's `while` form |

### The one hazard that is GPU-specific

GPU-PIPE-001 fuses the residual norm and the *next* iteration's `rho` into a
single reduction at the tail of the loop body:

```cpp
dot2(r_, r_, rHat_, r_, rDotR, rhoNext);
```

`rhoNext` is therefore computed against the **old** `rHat_`. After a restart
sets `rHat_ = r_` that carried value is stale, and using it would silently solve
a different recurrence than the CPU. The restart recomputes it:

```cpp
rhoNext = dot(rHat_, r_);
```

as a raw reduction rather than as `rNorm * rNorm`, for the reason the setup code
already documents about `b0`: the square of a square root is not bitwise the
reduction it came from.

---

## 3. What was deliberately not done

* No tolerance was loosened and no iteration budget was raised.
* No CPU fallback; the restart runs entirely on the device.
* No turbulence-specific branch — the change is in the shared solver and the
  turbulence code was not touched.
* No grid-size special case.
* No unlimited restart loop: the CPU's strict-decrease bound was adopted
  verbatim, with no counter added and none removed.
* `Breakdown` was not redefined. `GpuBiCGSTABRestartTest.BreakdownIsStillReachable`
  pins that a system which breaks down before making any progress still reports
  it.
