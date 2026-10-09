# Step 4 on the validated channel — three gates closed, one blocked

Re-run of the four open Step-4 gates on the repository's accepted high-Re
wall-function case (Re_tau = 590 channel) instead of the cavity.

```text
validated channel CPU reference          PASS
wall functions genuinely active          PASS
wall viscosity produced                  PASS
WallTreatment = Cpu provenance           PASS   (after fixing the instrument)
qualified GPU provenance                 PASS
CPU/GPU turbulence fields                FAIL   k 2.4x, eps 4.0x over the bound
CPU/GPU flow fields                      FAIL   pressure 106x over the bound
CPU/GPU wall quantities                  FAIL   tau_w 1.13x, mu_wall 1.08x over
wrong-flux control                       NOT WRITTEN
production residency ledger              NOT MEASURED
```

The bound was not loosened, so the differential is reported as failing.

---

## 1. The case, reused rather than recreated

Geometry, Re_bulk, inlet turbulence estimates, relaxation, tolerances and
solver types come from
`tests/integration/turbulence_channel/test_wall_function_channel.cpp`. Mesh is
24x12 rather than the sweep's 64xN so it runs as a unit test, with ny inside the
validity band that case established.

**The earlier failed channel differed in one thing and it mattered:** it applied
`fixed_gradient` pressure on every patch. With no Dirichlet pressure anywhere,
the pressure equation has no reference and the CPU reference diverged —
continuity 6.2e-2 -> 1.5e+9 over 2000 iterations. The validated case fixes
pressure at the **outlet**.

## 2. A non-vacuous CPU baseline

```text
status=Converged  iters=52   maxU=1.0668   maxK=9.2897e-03   maxMuT=4.6723e-03
wall faces with a viscosity override: 48
max y+ = 50.60    continuity=1.098e-11   mass=2.026e-10
```

Asserted before any comparison: converged, non-trivial velocity, live turbulence
state, **48 wall faces carrying non-zero overrides**, finite y+ inside the
log-layer band. This is what the cavity could not provide.

## 3. Provenance — and an instrument that had never worked

```text
strain rate:      GPU (50 executions)
production P_k:   GPU (50)
mu_t:             GPU (50)
k assembly:       GPU (50)
k solve:          GPU (50)
epsilon assembly: GPU (50)
epsilon solve:    GPU (50)
wall treatment:   CPU
turbulenceGpuPathQualified() == true
allOnGpu() == false
```

The first run reported `wall treatment: NOT ATTEMPTED`. The cause was not the
resident path: **`TurbulenceOperation::WallTreatment` was never recorded
anywhere in the codebase** — only its name was defined — so a provenance gate
demanding `WallTreatment = Cpu` could not have passed on any path, and one
demanding Gpu would have failed for the right answer with the wrong evidence.
The trace's own rule is that `NotAttempted` is as bad as `Cpu` for a gate
wanting `Gpu`; that cuts both ways, and an operation that genuinely runs on the
host has to say so. Now recorded where the host wall law executes.

## 4. The differential, and why it fails

Both arms run to the **same fixed point** — 600 outer iterations with an
unreachable tolerance — rather than each to its own stopping criterion.

That choice was measured, not assumed. At the validated tolerances the two arms
stopped at 52 and 50 iterations and differed by exactly their own stopping
tolerances (U 1.8e-5 against `velocityTolerance` 2e-5; p 5.2e-4 against
`pressureTolerance` 5e-4). Comparing that against a 1e-6 field bound asks a
finer question than the criterion that ended the solve. A probe then showed the
outer **pressure residual plateaus at 1.1e-5** and cannot be driven to 1e-9 at
all, so tightening the tolerance instead is not available.

At the common fixed point (continuity 5.7e-11 CPU, 2.2e-11 GPU):

| quantity | relative L2 | vs 1e-6 bound |
|---|---:|---|
| face mass flux | 5.806e-09 | PASS |
| U | 2.330e-07 | PASS |
| y+ | 5.596e-07 | PASS |
| wall viscosity | 1.080e-06 | **1.08x over** |
| wall shear | 1.129e-06 | **1.13x over** |
| mu_t | 1.734e-06 | **1.73x over** |
| k | 2.402e-06 | **2.40x over** |
| epsilon | 3.994e-06 | **3.99x over** |
| **pressure** | **1.058e-04** | **106x over** |

### The pressure is the outlier, and it is not a level offset

An incompressible pressure is determined only up to its reference, so a uniform
shift would move no physics. That was tested and **rejected**: the means differ
by only 1.154e-06, and removing the level makes the disagreement *worse*
(2.018e-04). It is a genuine field difference.

It is consistent with the convergence state rather than with a defect: at
iteration 600 the outer pressure residual is still ~1e-5, so two arms taking
different Krylov paths land ~1e-4 apart in pressure while agreeing to 2.3e-07 in
velocity and 5.8e-09 in flux. A wiring defect in the momentum/pressure coupling
would not spare U and the flux.

## 5. What this does not conclude

It does **not** conclude that the bound should move. The bound is unchanged and
the test fails.

What it establishes is that **this case cannot support a 1e-6 pressure
comparison**, because its own outer pressure residual does not reach 1e-6 at any
iteration count. Three options exist and the choice is not mine to make
unilaterally:

1. a case-specific pressure bound derived from the measured plateau, justified
   the way the repository derives its other frozen thresholds;
2. a channel configuration whose pressure genuinely converges to 1e-6;
3. comparing the pressure **gradient**, which is what momentum consumes, rather
   than the level.

The turbulence fields (k 2.4x, epsilon 4.0x) are close enough to the bound that
whichever of these resolves pressure will probably resolve them too — but that
is a prediction, not a measurement, and it is recorded as such.

## 6. Suites

```text
GPU            183/184   (the differential above is the one failure)
SIMPLE         123/123   CUDA and CUDA-OFF
turbulence     246/246   CUDA and CUDA-OFF
CPU-only build clean
```
