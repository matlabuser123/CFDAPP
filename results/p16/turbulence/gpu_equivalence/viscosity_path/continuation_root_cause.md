# Cross-backend continuation — first failing stage: the face mass flux

The continuation gate fails from horizon 2 onward, and the cause is not
nonlinear sensitivity. **The two turbulence paths are handed different mass
fluxes.**

```text
stage                         CPU/GPU difference        verdict
input state                   identical by construction  ok
velocity entering correction  1.585e-16                  ok
pressure / flux entering      5.9e-15 / 1.9e-16          ok
TRANSPORT-PLAN INPUT: flux    3.231e-06                  <-- FIRST FAILING ROW
resulting k                   2.4e-06 .. 3.7e-06         downstream
resulting mu_t                1.7e-06 .. 3.0e-06         downstream
```

Nothing downstream is worth fixing until this row is.

---

## 1. The two paths

```cpp
// src/turbulence/KEpsilonModel.cpp:234  -- the CPU reference
const auto massFlux = calculateMassFlux(mesh, velocity, fluid_, velocityBoundaries_);
```

The CPU turbulence correction **recomputes** the flux from the cell velocity.
It is handed no flux at all.

The resident path instead consumes `Impl::massFlux` — SIMPLE's **carried
corrected face flux**.

In SIMPLE these are not the same quantity. The corrected face flux differs from
one recomputed from the corrected cell velocities by the Rhie-Chow face
correction; that difference is the whole reason the face correction exists.

## 2. Measured, not inferred

At a mature state (checkpoint 300):

```text
carried vs recomputed face flux:   relative L2 3.231e-06   max|d| 1.104e-06
continuation k discrepancy:        2.4e-06 .. 3.7e-06
continuation mu_t discrepancy:     1.7e-06 .. 3.0e-06
```

Same order, essentially unit gain — which is what a linear convection operator
does to a perturbation in its flux. The wrong-flux control independently
supports the sensitivity: a 5 % flux perturbation moves k by 3.28e-05.

## 3. Why every earlier gate passed

`tests/unit/gpu/test_resident_wall_exchange.cpp:153` supplies
`calculateMassFlux(...)` as the "resident" flux. So in the Step-3 ladder **both
arms consumed the same quantity**, and they agreed to 1e-14 — correctly, for
the question that harness was asking.

The coupled production path is the first place the two fluxes can differ, and
the continuation gate is the first test that runs the resident correction twice
in a row against a CPU arm evolving beside it. Horizon 1 agrees at 1e-16
precisely because the GPU arm is still on the HOST turbulence path there
(iteration 0, `correctedResident == false`); horizon 2 is the first resident
correction.

## 4. A header comment that is wrong

`include/cfd/gpu/GpuSimpleResidentTurbulence.hpp` says of the carried flux:

> "which is exactly the flux the CPU turbulence correction receives"

It is not. The CPU turbulence correction receives no flux; it builds its own
from the velocity. The comment should be corrected along with the code.

## 5. The fix, and the judgement inside it

`calculateMassFluxDevice(plan, velocity, density, out)` already exists as the
device port of the host routine, so the resident path can build the same
quantity the CPU builds, from the resident velocity. That requires threading a
`DeviceFaceFluxPlan` into `ResidentTurbulenceInputs`.

**There is a real question underneath, and it should be decided rather than
assumed.** The carried corrected flux is *conservative* — divergence-free by
construction — while the recomputed one is not. On physical grounds the carried
flux is arguably the better input to a turbulence transport equation, and
earlier P16 evidence leaned on exactly that property when a non-conservative
flux broke convergence.

But this is an equivalence gate, and the standing rule is that CPU numerics are
the reference. Matching the CPU means recomputing. Changing the CPU to consume
the carried flux would be a physics change to the reference implementation,
which is a different decision with a different authorisation.

Recommended: make the resident path recompute, matching the reference, and
record the conservative-flux question as a separate item rather than settling it
inside a GPU-equivalence task.
