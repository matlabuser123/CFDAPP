# P16-GPU-TURB Stage 6 — wall-function / turbulence-state integration

Status: **COMPLETE**, 5/5 Stage-6 tests green (122/122 across the whole GPU
suite) in `build/cuda`.

`[ ] GPU equivalence` stays unchecked. Three of eight operations are still on
the host, by measurement and by admission — see §5.

**Two gates FAILED during this stage and are preserved below with their
diagnosis.** Neither was resolved by moving a threshold.

---

## 1. The audited production order (Stage 6A)

`KEpsilonModel::correct`, entry to exit. Every step reads the PRE-UPDATE state:

```text
 1  massFlux                                    from velocity
 2  velocityGradient    GreenGauss
 3  P_k = mu_t_OLD * S^2                        <-- the OLD mu_t
 4  wall treatment      y+, u_tau, tau_w, wall cells        (if wallFunctions)
 5  P_k[wallCell] = equilibrium                 <-- REPLACEMENT, not addition
    imposedEpsilon += {wallCell, wallCellDissipation}
    publish wallViscosity_, yPlus_, wallShearStress_, counters
 6  Gamma_k, Gamma_eps                          from mu_t_OLD
 7  k sources           from k_OLD, epsilon_OLD, modified P_k
 8  epsilon sources     from k_OLD, epsilon_OLD, modified P_k
 9  k:        assemble -> relax -> solve -> floor           NO imposed rows
10  epsilon:  assemble -> relax -> fix imposed -> solve -> floor -> re-assert
11  mu_t_NEW = f(k_NEW, epsilon_NEW)            <-- the FRESH fields
12  publish k, epsilon, mu_t ATOMICALLY
```

**Both equations are lagged against the same old state.** The epsilon equation
reads `k_OLD`, not the k solved at step 9. Publishing k early is the obvious
"optimisation" and it is a different model. Nothing is written to state until
both solves succeed, so a throwing solve leaves the state untouched.

---

## 2. FAILURE 1 — one plan pair is not enough (found, fixed)

The first `correctKEpsilonDevice` took ONE `DeviceDiffusionPlan` /
`DeviceConvectionPlan` pair and used it for both equations.

```text
one correction, wall functions OFF:
  k        0.000e+00        <-- exact
  epsilon  2.351e-01        <-- 23.5% wrong
  mu_t     5.274e-01
```

**Cause:** k and epsilon have DIFFERENT boundary condition sets —
`kBoundaries_` and `epsilonBoundaries_` are separate members — and a
`DeviceDiffusionPlan` bakes the encoded boundary values in at `build()` time.
Sharing the plans applied **k's inlet value to epsilon**, producing a fully
converged, entirely plausible, wrong epsilon field.

**Why every earlier gate missed it.** Stages 4 and 5 each built their own plans
from the right boundary set, because each tested one equation. Only a
composition that assembles both can see that they need different plans. This is
exactly the class of defect Stage 6 exists to find, and it is the argument for
integration testing in one line.

Fixed by `DeviceKEpsilonPlans`, which carries a pair per equation and refuses a
null. Result: `k 0.000e+00`, `epsilon 1.481e-16`, `mu_t 7.965e-17`.

---

## 3. FAILURE 2 — the chain measured the solver, not the discretization

With the plans fixed, one correction passed but the five-correction chain still
failed:

```text
correction 1:  k 0.000e+00   eps 1.481e-16   mu_t 8.414e-17     machine precision
correction 2:  k 2.374e-16   eps 3.222e-16   mu_t 3.822e-16     machine precision
correction 3:  k 2.234e-11   eps 4.819e-12   mu_t 5.000e-11     <-- 5-order JUMP
correction 4:  k 1.525e-11   eps 9.477e-12   mu_t 3.323e-11
correction 5:  k 8.472e-12   eps 5.250e-12   mu_t 8.395e-11     plateau
```

A five-order jump at one correction followed by a **plateau** is not
accumulation. Accumulation grows smoothly; this did not.

**Hypothesis:** the BiCGSTAB stopping point. The default solver tolerance is
`1e-10` relative. Once the CPU and device matrices differ by the 1–2 ulp that
P14 measured and froze, the two residual histories differ slightly, and at some
correction one arm satisfies the stopping test an iteration before the other.
The two solutions then differ by **the solver tolerance**, not by machine
epsilon — and that difference does not compound, it re-establishes itself each
correction. Which is a plateau at ~1e-11.

**Test:** tighten the solver tolerance and see whether the plateau moves with
it. If the difference is discretization, it will not move.

```text
relativeTolerance 1e-10          relativeTolerance 1e-14
  1:  k 0.000e+00  mu_t 8.4e-17    1:  k 0.000e+00  mu_t 7.965e-17
  2:  k 2.374e-16  mu_t 3.8e-16    2:  k 2.682e-16  mu_t 4.179e-16
  3:  k 2.234e-11  mu_t 5.0e-11    3:  k 1.074e-15  mu_t 2.532e-15
  4:  k 1.525e-11  mu_t 3.3e-11    4:  k 5.611e-15  mu_t 3.055e-14
  5:  k 8.472e-12  mu_t 8.4e-11    5:  k 5.488e-15  mu_t 2.509e-14
```

**Confirmed.** A four-order tightening of the tolerance moved the plateau down
four orders. The difference was where the solver stopped, not what the
discretization computed.

**THE GATE WAS NOT WEAKENED.** It is still P14's frozen 2.1e-12. What changed is
the experiment: a chained CPU/GPU comparison run at a loose solver tolerance
measures the solver's stopping point, and a test that cannot distinguish that
from a discretization error is not measuring the thing it claims to. The
harness now solves to `1e-14` so the comparison is dominated by the
discretization.

This is worth stating generally, because it will recur at the production
benchmark: **a solver tolerance looser than the equivalence bound makes the
equivalence bound unmeasurable.**

---

## 4. Measured, after both fixes

```text
one correction, wall functions OFF   k 0.000e+00  eps 1.584e-16  mu_t 1.624e-16
one correction, wall functions ON    k 0.000e+00  eps 1.481e-16  mu_t 7.965e-17
five chained corrections, walls ON   max over all five:
                                     k 5.6e-15    eps 5.1e-15    mu_t 3.1e-14
```

All against the frozen 2.1e-12. k is **bit-exact** after one correction.

The chain is asserted to have MOVED: k changes by more than 1e-6 over the five
corrections, so the comparison is not a fixed point comparing itself.

---

## 5. Provenance — five of eight, and why (Stage 6I)

```text
StrainRate            Gpu
Production            Gpu
TurbulentViscosity    Gpu
KAssembly             not Gpu      assembled on device, but from a test harness
EpsilonAssembly       not Gpu      (see below)
KSolve                Cpu
EpsilonSolve          Cpu
WallTreatment         Cpu
allOnGpu()            FALSE        asserted
```

Two genuine gaps, both found by this stage and neither closed by it:

**The wall treatment.** `computeWallTreatment` is a 270-line host function over
WALL FACES — an O(sqrt(N)) set, negligible against the transport assembly — and
its corner rule (*"a cell touching two wall faces appears ONCE, carrying the
value from its NEAREST wall face"*) is sequential by construction. It is
evaluated on the host and its small outputs uploaded. A CUDA port is real work
with essentially no performance return; whether it is worth doing for the
`allOnGpu` claim is a scope decision, not an engineering one.

**The linear solves.** `makeGpuBiCGSTAB` takes a **host** `LinearSystem` and
uploads internally. Feeding it a device-assembled CSR costs a D2H of the matrix
and an H2D straight back — destroying the residency this assembly exists to
provide. `solveDeviceSystem` performs exactly that round trip and is written in
one place, unhidden, so the residency audit can point at it. Closing this needs
a device-resident scalar solve, the way `GpuResidentSolve` provides one for
pressure.

`KAssembly`/`EpsilonAssembly` are not recorded as `Gpu` either: they do run on
the device, but from a test harness. `KEpsilonModel::correct` is still an
unconditional host call and the resident SIMPLE loop still requires
`activeModel->name() == "laminar"`. Recording them now would attribute CUDA
execution to a shipped path that has not performed it. Stage 7 flips them.

---

## 6. Non-vacuity controls

| control | asserts | result |
|---|---|---|
| wall P_k bypass | dropping the production replacement (keeping imposed rows) must be DETECTED — measured `k` relative L2 **5.253e-02**, four orders above the gate | PASS |
| chain moves | `k` changes by > 1e-6 over five corrections, so the chain is not a fixed point | PASS |
| trace honesty | `allOnGpu()` must be FALSE while three operations are on the host — **and the numbers agree anyway**, which is why the trace and not the numbers is the discriminator | PASS |
| determinism | two identical corrections are bitwise identical | PASS |
| both wall modes | one correction verified with wall functions ON and OFF | PASS |

---

## 7. Residency (Stage 6H)

Per correction, the avoidable traffic is entirely in `solveDeviceSystem`: one
D2H of the CSR (offsets, columns, values, RHS) and the implicit H2D inside the
host solver, twice — once for k, once for epsilon. Everything else stays
resident: `S^2`, `P_k`, the diffusivities, both source pairs and the assembled
systems are produced and consumed on the device.

The wall treatment contributes an upload of `O(wall faces)` values per
correction, which is the small end of the O(sqrt(N)) set.

No claim of full residency is made, and none is warranted until the solve moves.

## 8. Files

| file | change |
|---|---|
| `include/cfd/gpu/DeviceKEpsilon.hpp` | new — state, plans, config, the correction |
| `cuda/kernels/DeviceKEpsilonCuda.cpp` | new — the composition, orchestration only |
| `tests/unit/gpu/test_device_kepsilon_correction.cpp` | new — 5 tests |
| `cuda/CMakeLists.txt`, `tests/unit/gpu/CMakeLists.txt` | registration |

Next: Stage 7 — wiring this into `KEpsilonModel::correct` / the resident SIMPLE
path, which is what flips the production provenance.
