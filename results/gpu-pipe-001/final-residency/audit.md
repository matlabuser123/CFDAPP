# GPU-PIPE-001 Final Residency — audit

The record of what the production GPU path actually did before this work, what remains on the host
after it, and why each remaining host operation is there. Written before the SIMPLE-loop
implementation; the measured numbers in §2 are the frozen before-state.

Predecessor audits, not repeated here:

```text
results/gpu-pipe-001/persistent-fields/audit.md              the field residency design
results/gpu-pipe-001/gpu-resident-pressure-solve/audit.md    the pressure-system design
```

---

## 1. Starting state (Part 1)

Re-verified on the binaries under test rather than adopted from the earlier gate's record.

```text
libcfdcuda.a / libcfdcore.a sha256          results/gpu-pipe-001/final-residency/persistent-fields/build-identity.txt
field authority / dirty state / lifecycle   persistent-fields/dirty-state.log
stage-level residency + transfer baseline   transfers/before.log
15 GPU-DISC differential gates              persistent-fields/gpu-gates.log
```

Confirmed resident, by measurement and not by reading the code:

| property | how it was confirmed |
| --- | --- |
| pressure persists on device | `authority(Pressure) == DeviceOwned` after `updatePressure`; downloaded once after the loop |
| U/V/W persist on device | `authority(Velocity) == DeviceOwned` after `correctVelocityResident` |
| face flux persists on device | `authority(MassFlux) == DeviceOwned` after `correctFaceMassFluxResident` |
| response coefficients persist | the `computeResponseCoefficients` stage performs **0 D2H** — nothing on the device path reads a host copy |
| p' storage is persistent | `solvePressureCorrectionResident` returns an **empty** host solution; p' lives in `impl_->pPrime` |
| matrices / workspaces persist | a repeated assemble + resident solve allocates **0** and reallocates **0**; `residentSolveBytes()` is unchanged across calls |
| steady-state field reallocations | **0** on every measured case, 2D and 3D, both arms |
| 2D/3D field contracts | 2D: the resident velocity's W component is exactly zero; 3D: W is a live component; downloaded field sizes match the mesh |
| authority / dirty-state semantics | the full persistent-fields dirty-state suite re-passes |

### 1.1 One finding, recorded before it could be mistaken for a residency defect

`computeResponseCoefficients(threeDimensional=false)` uploads a **host zero-vector** into
`velocityStar.z` on every 2D iteration:

```cpp
// cuda/kernels/GpuSimpleDiscretizationCuda.cpp
if (!threeDimensional) {
  const std::vector<Real> zeros(static_cast<std::size_t>(impl_->cellCount), 0.0);
  impl_->velocityStar.z.uploadFrom(zeros.data(), impl_->cellCount);
}
```

This is GPU-DISC-001 behaviour and it is correct — a 2D solve must hold zeros there rather than
stale values. But it is a **full-field H2D of `nc` doubles per iteration that the device can
produce itself**, so it is a Part 3 elimination candidate, not a defect of the persistent-fields
gate.

The first version of this probe asserted "the response-coefficient stage transfers nothing", which
the 2D case failed and the 3D case passed. The criterion was wrong, not the code: the residency
property is that the coefficients never come *back*. The criterion was corrected to `D2H == 0`
and the H2D recorded as its own measured fact **before** any gate was run against it.

---

## 2. The before-state: transfers per SIMPLE iteration

Fitted from a delta between two runs of the same case at different outer budgets, with a third
budget as a linearity control. `production` = device discretization + device linear solvers +
resident pressure solve. `disc-only` = device discretization + host linear solvers, which makes
every remaining transfer a *field* transfer with no Krylov reduction traffic mixed in.

```text
                             H2D calls   H2D bytes    D2H calls   D2H bytes    sync    alloc  realloc
cavity 2d 16   disc-only          6.00      12,288        17.00      81,148    0.00     0.00     0.00
cavity 2d 16   production        13.00      41,984       387.50      66,892  370.50     0.00     0.00
cavity 3d 6    disc-only          7.00      12,096        21.00     107,988    0.00     0.00     0.00
cavity 3d 6    production        18.00      57,024       242.33      92,086  220.33     0.00     0.00
channel 2d 16  production        13.00      41,984       405.50      67,143  388.50     0.00     0.00
cavity 2d 160  disc-only          6.00   1,228,800        17.00   8,368,636    0.00     0.00     0.00
cavity 2d 160  production        13.00   4,290,560      2662.33  10,031,645 2645.33     0.00     0.00
```

Krylov reductions are the bulk of the production D2H **call** count and are counted separately by
`GPUExecutionStats::reductionGroups`. Subtracting them:

```text
cavity 2d 160 production, per SIMPLE iteration
  H2D                     13.00 calls    4,290,560 bytes
  D2H, reductions       2645.33 calls    (one 8-byte round trip each)
  D2H, non-reduction      17.00 calls    ~10,010,483 bytes      <- what Part 3 targets
  explicit syncs        2645.33          (the reductions')
  allocations              0.00
  reallocations            0.00
```

The 17 non-reduction D2H calls and 13 H2D calls per iteration are not noise: at 160² they carry
**~10.0 MB down and ~4.3 MB up every outer iteration**, and every byte of it was produced by the
device in the iteration that just ended.

---

## 3. Where those transfers come from

Measured stage by stage through the facade (`transfers/before.log`, section A), not inferred.

### 3.1 Down (D2H), per iteration, 2D

| source | calls | what |
| --- | --- | --- |
| `assembleMomentum` ×2 | 8 | `rowOffsets`, `columnIndices`, `values`, `rhs` per component, rebuilt into a host `LinearSystem` |
| momentum solve ×2 | 2 | the solution vector, downloaded by `GpuBiCGSTAB::solve` |
| `downloadVelocity` | 3 | U, V, W of the corrected velocity |
| `downloadMassFlux` | 1 | the corrected face flux |
| resident pressure solve | 2 | the fused 8-byte system check, and the p' finiteness guard |
| pressure finiteness | 1 | `residentPressureAllFinite` |

### 3.2 Up (H2D), per iteration, 2D

| source | calls | what |
| --- | --- | --- |
| `assembleMomentum` ×2 | 2 | `previousComponent` — a host copy of a field the device already holds |
| `computeResponseCoefficients` | 1 | the 2D `velocityStar.z` zero-fill (§1.1) |
| `setMomentumSolution` ×2 | 2 | u*, v* going back to the device |
| momentum solve ×2 | 8 | per solve: matrix values, RHS, initial guess, Jacobi diagonal |

In 3D each per-component row grows by one component: 12 assembly D2H, 3 solutions, 3 `previous`
uploads, 3 `setMomentumSolution`, 12 solver uploads — the 18 H2D / 21 non-reduction D2H measured
above.

**Every one of these exists for one reason: `LinearSolver` is a host interface that takes a host
`LinearSystem`.** The pressure stage already escaped it. Momentum has not.

---

## 4. Remaining host operations inside the GPU outer iteration

Every host operation in `SIMPLE::solve`'s outer loop on the GPU path, classified as the brief asks.

```text
A. must remain host-side
B. replaceable by a scalar / device reduction
C. unnecessary legacy field transfer
D. output / debug only
```

| # | SIMPLE.cpp | operation | class | disposition |
| --- | --- | --- | --- | --- |
| 1 | 373 | `cancellationCheck_()` | A | host control flow; no field |
| 2 | 378–381 | `selectComponent(velocity, U/V/W)` → `previousU/V/W` | **C** | the device holds `velocity`; take the components there |
| 3 | 385 | `monitor.relaxation()` | A | host scalar |
| 4 | 403 | `activeModel->correct(mesh, velocity, pressure)` | A | a no-op for the laminar model; see §6 |
| 5 | 404 | `effectiveViscosity()` | A | host field, uploaded only when it changes |
| 6 | 418–428 | initial upload / `beginIterationResident` / `setViscosity` | A | once per solve, or on an actual viscosity change |
| 7 | 449–460 | `assembleMomentum` → host `LinearSystem` | **C** | 4 D2H per component, to rebuild a matrix the device already has |
| 8 | 507–516 | `gpuResidency.syncMatrix/syncField` | A | only under `enableGpuResidency`, which the resident path declines |
| 9 | 532–560 | `momentumSolver->solve(system, toVector(previous))` | **C** | uploads the system straight back |
| 10 | 562–564 | `combineComponents` → host `velocityStar` | **C** | only consumed by the CPU path and by `setMomentumSolution` |
| 11 | 568–572 | `setMomentumSolution` | **C** | H2D of a vector the device computed |
| 12 | 573 | `allFinite(velocityStar)` | **B** | a predicate — reduction order carries no bitwise consequence |
| 13 | 594–623 | non-orthogonal corrector passes | A | the GPU path declines `N > 1` entirely |
| 14 | 642 | `computeResponseCoefficients` 2D zero-fill | **C** | a device fill produces the same bits |
| 15 | 662 | `computePredictedFaceFlux` | — | already resident |
| 16–19 | 725–862 | pressure assembly, resident solve, p' guard, pressure update | — | already resident |
| 20 | 886–898 | `correctVelocityResident` + `downloadVelocity` | **C** | the download exists only to feed #2 and #9 |
| 21 | 901–902 | `correctFaceMassFluxResident` + `downloadMassFlux` | **A** | see §5 — this one stays, and why |
| 22 | 920–924 | `allFinite(velocityNew)` / `allFinite(fluxNew)` | **B** / A | velocity moves to the device; flux is already on the host for #21 |
| 23 | 936–941 | residuals; `evaluateContinuity`; `rms` | A | see §5 |
| 24 | 943–949 | residual-history `push_back` | A | scalars |
| 25 | 957–959 | `velocity = velocityNew`, `massFlux = fluxNew` | **C** / A | velocity becomes a post-loop download |
| 26 | 973 | `progressCallback_` | A | scalars |
| 27 | 984 | `activeModel->convergenceResidual()` | A | scalar, `nullopt` for laminar |
| 28 | 993 | `monitor.record(...)` | A | **the convergence decision, on scalars only** |

Nothing is class D: the production loop has no debug-only transfer.

---

## 5. The one full-field transfer that stays, and why it is not "avoidable"

`downloadMassFlux` — one D2H of `nFaces` doubles per outer iteration — **remains**. It is class A,
and the reason is a constraint of this milestone, not an implementation shortcut.

The corrected face flux feeds `evaluateContinuity(mesh, fluxNew)` and then `rms(...)`:

```cpp
Real rms(const ScalarField& field) {
  Real sumSquares = 0.0;
  for (Index i = 0; i < field.size(); ++i) sumSquares += field[i] * field[i];
  return std::sqrt(sumSquares / static_cast<Real>(field.size()));
}
```

That is a **strictly sequential** floating-point sum over the cells, and its result is
`continuityResidual` — which goes into `monitor.record(...)`, the production convergence decision,
and into `result.continuityHistory`.

The existing acceptance gate for CPU/GPU equivalence
(`results/gpu-disc-001/full-solve-equivalence/tools/full_solve_equivalence.cpp`) requires:

* layer C — `cpu.iterations == gpu.iterations`;
* layer H — every residual history **bitwise identical**, `continuityHistory` included.

A device tree reduction sums in a different order. Floating-point addition is not associative, so
it would produce a different `continuityResidual` in the last bits, a different convergence
decision near the tolerance, and a different iteration count. That fails layer H and layer C of a
gate this milestone is explicitly forbidden to weaken:

> Never weaken tolerances or convergence criteria. · Use existing tolerances unchanged.
> Preserve the exact production convergence definitions. Do not create a GPU-specific convergence
> criterion.

The only device reduction that *would* be bitwise is a single-threaded sequential one, which at
640² is ~409,600 dependent FP adds — slower than the 6.6 MB copy it would replace. So it is not an
optimisation either.

**Therefore: the continuity reduction stays on the host, and the face-flux download that feeds it
stays with it.** It is documented as a recurring transfer rather than eliminated, which is the
disposition the brief allows ("justify it or eliminate it"). Changing it requires authorisation to
change the convergence definition, which this milestone does not have.

The velocity `allFinite` guard is a different case and *does* move: it is a **predicate**, so
reduction order has no bitwise consequence — the one reduction in this codebase for which that is
true, and the persistent-fields gate already established that argument for pressure.

---

## 6. Where the resident SIMPLE loop is declined

Same discipline as the pressure stage: engaged only where it is *exactly* equivalent to what it
replaces. Each condition is a real difference, not caution.

| condition | why |
| --- | --- |
| everything the resident pressure solve already requires | the resident loop is built on it |
| the turbulence model is the laminar one | `activeModel->correct(mesh, velocity, pressure)` reads the **host** velocity. For the laminar model that call is a documented no-op and `effectiveViscosity` is constant, so a stale host velocity changes nothing. For a transport model it would, so that configuration keeps today's per-iteration download. |
| `momentumSolver.backend == GPU` and `type == BiCGSTAB` | only BiCGSTAB has a resident entry point; `GpuCG` was deliberately not touched |
| momentum solver fallback off | the fallback wrapper re-solves on the CPU when a GPU solve fails; bypassing it would change failure behaviour |

Every other configuration runs exactly what it ran before.

---

## 7. Target architecture

```text
host case initialization
  |
  v  ONE upload: velocity, pressure, massFlux, viscosity
+---------------------------------------------------------------+
| GPU SIMPLE outer iteration                                     |
|                                                                |
|   previousU/V/W   <- device velocity            (device copy)  |
|   momentum assembly -> DeviceMomentumSystem     (resident CSR) |
|   momentum solve    -> velocityStar             (resident)     |
|   predictor finiteness                          (8 bytes)      |
|   response coefficients                         (resident)     |
|   Rhie-Chow / predicted face flux               (resident)     |
|   pressure-correction assembly                  (resident CSR) |
|   GPU BiCGSTAB pressure solve -> p'             (resident)     |
|   pressure update                               (resident)     |
|   velocity correction                           (resident)     |
|   face-flux correction                          (resident)     |
|   velocity / pressure finiteness                (8 bytes)      |
+---------------------------------------------------------------+
  |
  v  face flux D2H  -> evaluateContinuity, rms   (section 5)
  v  scalar residuals, iteration counts, status
  |
  v  next iteration
  |
  v  ONE download: velocity, pressure
```

Momentum reuses **the same** `bicgstabCore` the pressure stage reuses and the host entry point
uses. No second solver, no new numerics, no new scheme.

---

## 8. What this audit does not authorise

* The Krylov reduction round trips (`reductionGroups`) are the BiCGSTAB algorithm's own and are
  explicitly out of scope, exactly as the pressure audit put them.
* The known GPU BiCGSTAB restart asymmetry stays reproducible and unfixed.
* `GpuCG` is not touched.
* No tolerance, convergence criterion or iteration budget moves.
