# The same-state map found a real defect that 600-iteration trajectories hid

The implementation-equivalence gate — clone a mature state into both arms, run
**one** production outer iteration, compare at the unchanged 1e-6 bound.

It immediately failed, and the failure was not trajectory sensitivity. It was a
genuine defect in the Step-4 wiring that two independently evolved trajectories
had been masking as nonlinear accumulation.

---

## 1. What it found

```text
MATURE SAME-STATE ONE-STEP MAP, before the fix
from    U          p          flux       k          eps        mu_t
10      2.432e-02  2.622e+00  2.434e-02  3.431e-16  3.098e-14  2.949e-13
50      2.381e-02  2.771e+00  2.380e-02  2.062e-16  3.619e-17  4.769e-16
300     2.381e-02  2.770e+00  2.380e-02  1.659e-16  3.502e-17  3.921e-16
600     2.381e-02  2.768e+00  2.380e-02  1.834e-16  2.109e-17  4.341e-16
```

Two things stood out and together they identified the cause:

* **The turbulence agreed to 1e-16 while the flow did not.** k, epsilon and
  mu_t were at machine precision; U was 2.4% out and pressure 277%.
* **The error was constant across every checkpoint.** Trajectory sensitivity
  grows with maturity. A fixed magnitude at iteration 10 and at iteration 600 is
  a structural difference.

The laminar control settled it. Same experiment, same mature-state clone, no
turbulence path on either arm:

```text
LAMINAR one-step from a mature state: U 4.010e-16   p 4.235e-14
```

So the GPU momentum and pressure map is exact. The difference required
turbulence to be present, yet the turbulence output was identical. That leaves
only what turbulence hands to momentum.

## 2. The cause

`SIMPLE` pushed the wall-face viscosity override to the device **only from
inside the resident turbulence correction**. A solve where that correction does
not run — iteration 0 always, because there is no resident state to correct
from yet — assembled momentum on the device with **no wall-function shear
override at all**, while the CPU arm's host assembly received it.

This is exactly the gap the Phase-2 audit identified and warned must not be
reached; it was reintroduced through the one path Step 4 did not wire.

A one-iteration solve is *entirely* iteration 0, which is why the same-state map
exposed it at full strength while a 600-iteration run showed it only as a
transient seed that then decayed.

## 3. The fix

`SIMPLE` now pushes `activeModel->wallViscosity()` to the device on every
GPU-discretization path, not only when the resident correction ran, through a
`SurfaceField` overload that compacts to the non-zero entries internally. The
transfer stays `O(N_wall)`; a null field clears the override, so laminar is
untouched.

## 4. Effect

```text
MATURE SAME-STATE ONE-STEP MAP, after the fix
from    U          p          flux       k          eps        mu_t       tau_w
10      2.572e-10  1.048e-08  2.626e-10  3.431e-16  3.098e-14  2.949e-13  0.000e+00
50      7.086e-09  1.748e-06  7.053e-09  2.062e-16  3.619e-17  4.769e-16  0.000e+00
300     3.405e-10  8.228e-08  3.464e-10  1.659e-16  3.502e-17  3.921e-16  0.000e+00
600     1.072e-09  2.578e-07  1.078e-09  1.834e-16  2.109e-17  4.341e-16  0.000e+00
```

Fifteen of sixteen cells pass, most by three to six orders. **One remains:
pressure from checkpoint 50, at 1.748e-06 — 1.75x the bound.** The bound was not
changed and that cell is reported as failing.

### The long-horizon differential improved by the same fix

```text                        before        after
U                             2.330e-07     4.521e-09
pressure                      1.058e-04     1.194e-06
face flux                     5.806e-09     3.803e-09
k                             2.402e-06     2.394e-06
epsilon                       3.994e-06     3.973e-06
mu_t                          1.734e-06     1.733e-06
y+                            5.596e-07     5.284e-07
wall shear                    1.129e-06     1.070e-06
wall viscosity                1.080e-06     1.077e-06
```

Pressure fell by a factor of 89, from 106x the bound to 1.19x. The
"checkerboard" structure reported earlier — `jump/mag` 1.99 — was the signature
of iteration 0 assembling momentum without the wall override, not of a
pressure-coupling defect.

The turbulence fields are essentially unchanged, which is consistent: they were
never the ones affected.

## 5. Why this vindicates the gate

Two independently evolved 600-iteration trajectories reported this as a 1e-4
pressure discrepancy with a plausible story attached — nonlinear sensitivity,
supported by a monotone decay and by iteration-1 agreement at 2.1e-15. That
story was true about the *mechanism* and wrong about the *cause*.

The instruction not to classify a failing same-state map as trajectory
sensitivity is what surfaced it.

## 6. A CPU-only link break, caught and fixed

The new `setWallFaceViscosity(const SurfaceField*)` is called from `SIMPLE.cpp`,
which compiles in both configurations, but was defined only in the CUDA
translation unit:

```text
undefined reference to `cfd::gpu::GpuSimpleDiscretization::setWallFaceViscosity(
    cfd::fields::SurfaceField const*)'
```

Four CPU-only test targets failed to link. A no-op stub was added to
`src/gpu/GpuSimpleDiscretization.cpp` — a no-op rather than a throw, for the
same reason `setViscosity` beside it is one: `SIMPLE` calls it unconditionally
on a branch that a CPU-only build never takes, and throwing would turn a build
configuration into a runtime failure.

This is the CPU-only blind spot in its usual form: the CUDA build was green
throughout.
