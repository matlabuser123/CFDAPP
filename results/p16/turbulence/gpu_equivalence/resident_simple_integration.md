# P16-GPU-TURB Stage 7I — resident SIMPLE + k-epsilon

Status: **BLOCKED.** Audit, Step 1 (seam) and Step 2 (resident entry point) are
complete and green. Step 3's compact wall exchange is implemented and its wall
gates pass — but a **GPU solver convergence failure above ~192 cells** was found
while measuring transfer scaling, and it blocks Step 3 and everything after it.

**See §6. Do not read the earlier green results as covering production grid
sizes — they do not.**

The laminar guard has **not** been touched, as instructed. The audit below
establishes the invariant it protects, and it changes what the remaining work
is — see §4.

`[ ] GPU equivalence` stays unchecked.

---

## 1. Stage 7I-1 — the CPU SIMPLE ordering, traced not assumed

`src/pressure_velocity/SIMPLE.cpp:537-547`, and the comment is the code's own:

> *"Correct the turbulence model against the current (previous-iteration)
> velocity/pressure **before assembling momentum with it** — the same
> lagged-current-state convention this loop already applies to boundary
> conditions like Outlet/Symmetry."*

So one outer iteration N is:

```text
 1  activeModel->correct(mesh, velocity, pressure)     <-- FIRST, before momentum
 2  effectiveViscosity = activeModel->effectiveViscosity(mu)
 3  wallViscosity = activeModel->wallViscosity()        re-read every iteration
 4  momentum assembly, using that mu_eff
 5  momentum solve                                      -> predictor velocity
 6  predicted face flux (Rhie-Chow)
 7  pressure-correction assembly
 8  pressure-correction solve
 9  velocity correction                                 -> corrected velocity
10  face-flux correction
    ... iteration N+1 begins, and step 1 reads what step 9 produced
```

### THE ANSWER TO THE QUESTION THAT MATTERS

**The velocity passed to `KEpsilonModel::correct()` is the PRESSURE-CORRECTED
velocity from the END of the PREVIOUS outer iteration.**

It is not the momentum predictor. It is not this iteration's corrected
velocity. It is deliberately one iteration lagged, and the code says so.

### What the resident loop actually holds at that point

`SIMPLE.cpp:562`: on the resident path *"the device carries its own corrected
velocity, corrected flux and updated pressure forward"*.

So at step 1 of iteration N, the **device** velocity is exactly the
end-of-iteration-(N−1) corrected velocity — precisely the state the CPU
algorithm requires. It is the **host mirror** that is stale, because the
resident loop stops downloading it.

```text
CPU semantics need   velocity at end of iteration N-1
device holds         velocity at end of iteration N-1     CORRECT
host mirror holds    velocity from iteration 0            STALE
```

**The guard is right and the invariant is real** — but it is a statement about
the *host field*, not about k-epsilon. The correct state is already resident.

---

## 2. Stage 7I-5 — wall-treatment data dependency audit

`computeWallTreatment(mesh, wallFaces, velocity, turbulentEnergy, density,
dynamicViscosity, coefficients, options)` takes two full fields by reference.
What it actually reads:

| input | what it touches | classification |
|---|---|---|
| `velocity` | `velocity[owner]` only, `WallTreatment.cpp:84` | **compact D2H** — O(wall cells) |
| `turbulentEnergy` (k) | `turbulentEnergy[owner]` only, lines 98–159 | **compact D2H** — O(wall cells) |
| `wallFaces` | `wallDistance`, `area`, `unitNormal`, `wallVelocity`, `ownerCell` | **already host-resident** — geometry, collected ONCE at construction |
| `density`, `dynamicViscosity` | scalars | already host |
| `coefficients`, `options` | scalars | already host |
| `mesh` | `mesh.cell(ownerId).centroid()` for the wall-face offset | already host |
| `epsilon`, `mu_t` | **not read at all** | **no transfer** |

**It reads only wall-cell entries of two fields, and touches neither epsilon
nor mu_t.** A full-field download is not required to run the wall law.

So the architecture in the brief is achievable as written:

```text
resident device velocity
   ├─→ GPU velocity gradient / S^2 / raw P_k      (no transfer)
   └─→ compact wall-cell velocity + k   D2H       O(wall cells)
             ↓
        CPU wall law
             ↓
        compact wall outputs             H2D      O(wall cells)
   ↓
GPU k/epsilon transport
```

---

## 3. What this means for the guard

The guard cannot be widened by name, and the audit says why more precisely
than "k-epsilon reads velocity":

* `LaminarModel::correct()` ignores velocity, so a stale host field is harmless;
* `KEpsilonModel::correct()` reads velocity, and on the resident path the host
  field it would read is **frozen at iteration 0** — not merely one iteration
  stale, but stale for the whole solve;
* the value it *should* read is sitting on the device already.

So widening the guard is safe **only after** the GPU turbulence path stops
consuming the host velocity field. Until then, widening it would authorize
k-epsilon to run against iteration-0 velocity for every iteration — a defect
that would still converge, to the wrong answer.

---

## 4. THE FINDING THAT CHANGES THE REMAINING WORK

**The Stage 7D facade cannot be used from the resident loop as it stands.**

`GpuKEpsilonCorrection::correct()` takes a **host** `VectorField` and computes
two things on the host from it:

```text
calculateMassFlux(mesh, velocity, fluid, velocityBoundaries)
computeVelocityGradient(mesh, velocity, velocityBoundaries)
```

Both are correct for the non-resident path, where the host velocity is current.
Both are wrong for the resident loop, where it is not. Calling the current
facade from a resident loop would consume exactly the stale field the guard
exists to prevent — the defect would be invisible in a one-correction test and
would produce a converged, wrong solve.

### What 7I therefore requires, in order

1. **A device-velocity entry point on the facade.** Smallest possible: an
   overload taking the resident `DeviceVelocity` (and the resident face flux,
   which the loop already maintains corrected on device) instead of a host
   `VectorField`. The CPU signature stays exactly as it is — 7I-4's
   requirement.
2. **Device velocity gradient.** `computeVelocityGradientDevice` already exists
   and is the qualified GreenGauss port; it needs a `DeviceMomentumConvectionPlan`,
   which the resident loop already builds.
3. **Device mass flux.** The resident loop already carries a corrected face
   flux on device; the turbulence path should adopt it rather than recompute.
4. **Compact wall exchange** per §2 — a narrow view, not a field download, and
   not a redesign of the validated wall model.
5. Only then: the capability-based guard, the stale-velocity control (7I-7),
   the full turbulent SIMPLE differential (7I-9) and the residency audit
   (7I-10).

Steps 1–3 are the real content. None of it is speculative: every operator
needed already exists and is qualified, which is the same shape Stages 4, 5 and
7B had.

---

## 4b. The seam, implemented and building

The audit named three pieces the resident turbulence correction needs. All
three live inside `GpuSimpleDiscretization::Impl`, and `beginIterationResident`
states which ones carry the CPU-equivalent state:

> *"Nothing crosses PCIe. The corrections already carried their results into
> `velocity` and `massFlux` ... this is the explicit statement that a new outer
> iteration begins from them."*

| needed | Impl member | why this one |
|---|---|---|
| corrected velocity from N-1 | `velocity` | start-of-iteration == end of N-1 |
| corrected face flux from N-1 | `massFlux` | commented "carried between iterations, **not the predictor**" |
| gradient plan | `momentum.convectionPlan()` | already encodes the velocity BCs |

Exposed by a **borrowed view**, not a download:

```text
include/cfd/gpu/GpuSimpleResidentTurbulence.hpp   CUDA-only, defines the view
include/cfd/gpu/GpuSimpleDiscretization.hpp       friend decl, type left OPAQUE
cuda/kernels/GpuSimpleDiscretizationCuda.cpp      residentTurbulenceInputs()
```

The struct is **declared but not defined** in the CUDA-free header: its members
are CUDA types and that header must stay includable from a CPU-only build. A
friend declaration returning an incomplete type is legal precisely because
nothing there can call it. Both configurations compile clean --
`cfdcuda` and CPU-only `cfdcore`.

Nothing is copied and nothing crosses PCIe. The alternative was a full velocity
D2H every outer iteration, which is what the resident design exists to avoid.

**Ownership:** every pointer aliases Impl-owned buffers. The view must not
outlive the discretization, and frees nothing -- the same contract
`DeviceCsrMatrix::adoptDevice` already carries.

## 4c. Step 2 — the resident entry point

### The API

```cpp
// unchanged, non-resident
void correct(mesh, velocity, velocityBoundaries, fluid, config, wall, result);

// new, resident
void correctResident(const ResidentTurbulenceInputs& inputs, mesh, fluid,
                     config, wall, result);
```

**The resident signature takes no velocity and no velocity boundaries.** The
gradient comes from `inputs.convectionPlan` (which already encodes those
boundary conditions) applied to `inputs.velocity`; the flux is adopted from
`inputs.massFlux`. There is no host field to get wrong — the absence is the
design, not an omission.

`ResidentTurbulenceInputs` is taken by reference to an **incomplete type**,
which is legal in a declaration and is what lets the CUDA-free header accept a
CUDA-typed view.

### One algorithm, two entry points

Both funnel into a private `runCorrection()`, which calls the single qualified
`correctKEpsilonDevice`. They differ only in where two device buffers come
from. Every downstream semantic — the `mu_t_OLD` lag, `k_OLD`/`epsilon_OLD`
sources, epsilon never seeing fresh k, atomic publication — is the one
implementation both share, so a semantic added to one cannot be missing from
the other.

### Borrowed-view lifetime contract

`inputs` aliases buffers owned by `GpuSimpleDiscretization::Impl`. The call
BORROWS them for its duration only: nothing frees, resizes or retains them.
`Impl::residentMassFlux` holds the borrow and is **cleared immediately after
the call**, so a stale borrow cannot survive into a later non-resident
correction.

### Measured

```text
resident entry point vs CPU model   k 3.610e-16  epsilon 3.356e-16  mu_t 7.726e-16
```

Against the frozen 2.1e-12.

### Negative controls — why they are differential

`correctResident()` takes no host velocity, so "it does not read the host
mirror" is true by construction and asserting it directly would be **vacuous**.
What is not automatic is that it reads the RIGHT device state. So:

| control | asserts | result |
|---|---|---|
| host corrupted, device fixed | the resident result is **bit-identical** — a host mirror is not being reached for | PASS |
| device perturbed, host fixed | the result **changes** — without this the row above passes because velocity reaches nothing at all | PASS |
| flux substituted | a different face flux **changes** k and epsilon — "adopts the corrected flux" has evidence | PASS |
| empty view | refused (`InvalidArgumentError`), not treated as "no state" | PASS |
| mesh-size mismatch | refused | PASS |
| determinism | two identical resident corrections are bitwise identical | PASS |

The second row is the one that makes the first non-vacuous, and it is the
reason both are present.

### Transfers

The resident entry point performs **no velocity D2H and no mass-flux D2H**.
The gradient is computed on device from the borrowed velocity; the flux is
adopted by pointer. The three result fields still come down because
`TurbulenceModel` publishes host `ScalarField`s — an interface fact, recorded
rather than hidden, and unchanged from Stage 7D.

### Regression after Step 2

```text
GPU suite (CUDA)          134/134 PASS
turbulence (CUDA)         246/246 PASS
turbulence (CPU-only)     246/246 PASS
CPU-only build            0 errors
```

`SIMPLE.cpp` untouched. Resident capability guard untouched. The non-resident
production path untouched. `correctResident()` has no production caller yet.

## 4d. Step 3 — compact wall exchange (implemented; wall gates PASS)

`velocity[owner]` and `k[owner]` are gathered on device by
`gatherByIndexDevice` and downloaded as compact buffers. The host wall law runs
**unchanged**, fed full-LENGTH fields whose wall-owner entries hold the gathered
values and whose every other entry is a quiet **NaN**.

The NaN is the control, not laziness: if the wall law ever read a cell the audit
says it does not, every wall output would be NaN on the first run. That covers
every interior cell at once, which a perturbation sweep does not.

| gate | result |
|---|---|
| compact path reproduces the full-field wall treatment **bitwise** | PASS |
| ... including the ORDER of `wallCells` on a shared-owner mesh (ny=1, 32 faces → 16 cells) | PASS |
| y+, wall shear, wall viscosity, band counters, y+ extremes | PASS, bitwise |
| all wall outputs finite despite NaN-poisoned interior | PASS |
| perturbing a wall owner changes y+ | PASS |
| resident correction with compact wall vs CPU model | PASS, k 2.759e-16 |

**On the corner-order control, stated honestly.** The brief asks for a reorder
control that must be detected. In this design it would be **vacuous**:
`wallFaces` is passed to `computeWallTreatment` untouched and in order, the
gather is a permutation keyed by owner cell, and the scatter writes by owner
index — so sorting or deduplicating the gather list would fill the same sparse
entries with the same values. The compact representation has no mechanism by
which it could change the corner semantics. What IS proven is the pass-through:
on a mesh where owners are genuinely shared, the compact path reproduces the
full-field path bitwise **including the `wallCells` order**, which is exactly
what the sequential corner rule determines.

---

## 6. FAILURE — the GPU solve does not converge above ~192 cells

Found while measuring 3J transfer scaling on a second grid. **Preserved, not
worked around.**

### Measured

Uniform velocity (divergence-free, so the flux is conservative),
production-default solver settings, wall functions on, one correction:

```text
nx=16  cells=128    CPU ok   GPU ok
nx=24  cells=192    CPU ok   GPU ok
nx=32  cells=256    CPU ok   GPU NO   epsilon solve did not converge
nx=48  cells=384    CPU ok   GPU NO   k solve did not converge
nx=64  cells=512    CPU ok   GPU NO   k solve did not converge
```

**The CPU converges at every size. The GPU resident BiCGSTAB fails from 256
cells upward**, on the same matrices, with the same tolerances.

### Two hypotheses I tested and DISPROVED first

1. *The strict 1e-14 equivalence tolerance is too tight at larger sizes.*
   **Wrong** — it fails at the production-default tolerance too.
2. *My synthetic velocity is not divergence-free, so the flux is
   non-conservative and the convection operator loses diagonal dominance.*
   **Wrong** — it fails with a uniform, divergence-free velocity.

Both are recorded because each looked plausible and neither survived contact.

### Why this blocks more than Step 3

**Every GPU turbulence differential in Stages 4–7 ran on a grid below the
failure threshold**: 12×9 = 108, 12×8 = 96, 14×10 = 140, 14×8 = 112. All under
192. So the equivalence established so far is real but its **coverage is
narrower than it appeared** — it says nothing about the grid sizes a production
turbulent case uses. The Re_tau = 590 channel is far larger.

That is a statement about coverage, not a retraction: the numbers measured are
still the numbers measured. But Stage 8's benchmark equivalence cannot be
reached through this path until the failure is understood.

### Not yet investigated

Whether this is the same mechanism as `DEBT-GPU-001` (the recorded GPU BiCGSTAB
restart asymmetry — *"GPU raises PressureCorrectionFailure after 74 iterations
where CPU runs the full outer budget"*, present at the known-good baseline) is a
HYPOTHESIS, not a conclusion. It has the same shape — GPU-only Krylov failure
where CPU succeeds — but nothing here has been traced to it.

The probe that produced this table is kept as
`ResidentWallExchange.ProbeWhereTheKSolveStopsConverging` so the threshold can
be re-measured after any change.

### What was NOT done in response

* the tolerance was not loosened;
* the test grid was not shrunk to stay under the threshold;
* the transfer-scaling gate was not marked PASS.

## 5. Gate status

```text
CPU SIMPLE ordering audit              PASS   §1, SIMPLE.cpp:537-547, 562
exact turbulence velocity state known  PASS   end of previous outer iteration
wall-data dependency audit             PASS   §2, wall-cell entries only
resident equivalent ordering           NOT STARTED
GPU turbulence reads live device flow  NOT STARTED   -- blocked on §4 steps 1-3
compact wall exchange                  NOT STARTED
stale-velocity negative control        NOT STARTED
resident SIMPLE k-epsilon capability   NOT STARTED   -- guard deliberately untouched
full turbulent production solve        NOT STARTED
CPU/GPU full-field equivalence         NOT STARTED
transfer/residency audit               NOT STARTED
```

Nothing was edited in `SIMPLE.cpp`. No guard was widened. No test was written
that would pass against stale velocity.
