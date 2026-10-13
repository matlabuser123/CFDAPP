# P16-GPU-TURB Stage 7 — production GPU k-epsilon execution

Status: **7A–7H COMPLETE and green** (129/129 GPU suite, 246/246 turbulence in
BOTH build configurations). **7I is NOT done** — the resident SIMPLE loop still
requires `name() == "laminar"`. See §6.

`[ ] GPU equivalence` stays unchecked.

---

## 1. Stage 7A — the audit, and the thing that made 7B small

`GpuResidentSolve.hpp` already says of itself:

> *"A SECOND ENTRY POINT INTO THE EXISTING SOLVER, NOT A SECOND SOLVER. …
> There is exactly one copy of the BiCGSTAB iteration, one set of breakdown
> tests and one convergence test."*

Everything a resident scalar solve needs already exists:

| need | existing |
|---|---|
| device CSR without copying | `DeviceCsrMatrix::adoptDevice` |
| device RHS / solution | `GpuKrylovWorkspace` (`x` IS the workspace's) |
| persistent Krylov scratch | `GpuKrylovWorkspace::resize`, a no-op once sized |
| validity + Jacobi on device | `checkSystemAndBuildJacobi` — one 8-byte read |
| the solve | `solveBiCGSTABResident` |
| DeviceBuffer ↔ DeviceVector | `copyToWorkspaceRhs/Guess`, `copyWorkspaceSolution` |
| convergence / iterations / residual | `SolverResult` |

**So Stage 7B wrote no solver.** `solveScalarTransportResident` is 25 lines that
point `adoptDevice` at a `DeviceScalarTransportSystem` and call the resident
entry point. There is still exactly one BiCGSTAB in the codebase, so a
turbulence solve cannot drift numerically from a pressure solve.

The earlier plan — `makeGpuBiCGSTAB`, which takes a **host** `LinearSystem` —
is not used, because it would have cost a D2H of the matrix and an H2D straight
back.

---

## 2. Stage 7B — measured

```text
one correction, wall functions OFF   k 3.610e-16  eps 3.356e-16  mu_t 7.726e-16
one correction, wall functions ON     k 3.115e-16  eps 3.647e-16  mu_t 6.023e-16

five chained corrections, walls ON, worst over all five:
    k 2.748e-15      epsilon 2.095e-15      mu_t 1.163e-14
```

Against the frozen 2.1e-12. Every field is two to four orders inside it.

k is no longer bit-exact (3.6e-16 rather than 0.000e+00) and that is **expected
and correct**: the device arm now runs the GPU BiCGSTAB rather than the CPU one,
so the two solves differ by their own iteration arithmetic. Bit-exactness
against a *different solver* would have been the surprising result.

### Residency (Stage 7J, partial)

Per correction, for each equation:

```text
CSR D2H                 0      adopted in place, never downloaded
RHS D2H                 0      copyToWorkspaceRhs is device-to-device
solution round-trip     0      stays in the workspace, copied device-to-device
Krylov allocations      0      after the first correction (workspace lives in
                               DeviceKEpsilonState, resize() is a no-op)
scalar host reads       the SolverResult counters, as the pressure path already does
```

`S^2`, `P_k`, both diffusivities, both source pairs and both assembled systems
are produced and consumed on the device.

**Remaining host traffic** is the wall treatment's, and only the wall
treatment's: `O(wall faces)` values down (k for the wall cells, via the state)
and `O(wall cells)` up (production, imposed epsilon). That is the documented
cost of the P16 scope decision, measured separately as required.

Two workspaces are held, not one shared: k and epsilon are solved in sequence
and could share, but a shared workspace would make epsilon's initial guess
depend on k's leftovers — a coupling the CPU path does not have.

---

## 3. Stage 7C — the plan-sharing control

Mandatory after the Stage 6 failure, and it reproduces that failure exactly:

```text
shared plans: epsilon relative L2 = 2.114e-01     (must exceed 2.1e-12)
              k       relative L2 within 2.1e-12  (unaffected, as before)
```

A 21% epsilon error with k untouched — the same signature as the original
defect. `DeviceKEpsilonPlans` also refuses a null plan outright rather than
crashing later.

The control asserts BOTH halves: that sharing is detected, **and** that k is
unaffected. The second half is what makes it a control rather than a
coincidence: if k had also moved, the test could be passing for an unrelated
reason.

---

## 4. Stage 7F — provenance, and a predicate that is not a relaxation

```text
StrainRate            Gpu
Production            Gpu
TurbulentViscosity    Gpu
KAssembly             Gpu
KSolve                Gpu        <-- new in Stage 7
EpsilonAssembly       Gpu
EpsilonSolve          Gpu        <-- new in Stage 7
WallTreatment         NotAttempted (host, by scope decision)

turbulenceGpuPathQualified()   true
allOnGpu()                     FALSE
```

`turbulenceGpuPathQualified()` requires the seven operations the GPU path is
meant to perform and says nothing about `WallTreatment`.

**It is a different question from `allOnGpu()`, not a relaxation of it.**
`allOnGpu()` still exists, still means exactly what it says, and still returns
false — and a test asserts that it does. A caller who wants "everything on the
device" keeps asking `allOnGpu()` and keeps getting the honest answer. Nothing
was renamed or widened to make a gate pass.

---

## 5. THE HOST WALL TREATMENT IS A LIMITATION, STATED PLAINLY

P16's GPU turbulence path executes the high-Re wall function **on the CPU**.
Every correction evaluates `computeWallTreatment` on the host and uploads its
outputs.

This is a deliberate, authorized scope decision, not an oversight and not a
performance claim:

* it touches only `O(sqrt(N))` wall faces;
* it carries sequential corner semantics (*"a cell touching two wall faces
  appears ONCE, carrying the value from its NEAREST wall face"*);
* it is already validated;
* a CUDA port would add substantial complexity for negligible expected gain.

Consequences a reader must not be allowed to miss:

1. **`allOnGpu()` is false for any turbulent case**, and will stay false in P16.
2. A turbulent GPU solve is **mixed CPU/GPU execution**, with a host round trip
   per correction proportional to the wall-face count.
3. Any future claim of full turbulence residency must revisit this.

---

## 6. Stage 7D — the production seam (COMPLETE)

### The seam

`KEpsilonConfig::kSolver.backend`. That is the field every other backend
decision in this project reads (`LinearSolverFactory`, SIMPLE's pressure and
momentum stages), so no new interface, no new configuration concept, and
nothing in `TurbulenceModel` changes.

**A pre-existing finding surfaced during the audit:**
`solveRelaxedScalarTransport` constructs `cfd::algebra::BiCGSTAB` directly and
**ignores `settings.backend` entirely**. Before this stage, a user setting
`kSolver.backend = GPU` got CPU numbers with nothing recorded anywhere. That is
now a real selection.

### Crossing the CUDA boundary

`KEpsilonModel` lives in cfdcore and must compile in a CPU-only build;
`DeviceKEpsilon.hpp` pulls in `<cuda_runtime.h>`. The project's existing answer
is a CUDA-free pimpl facade plus a stub under `if(NOT CFDAPP_ENABLE_CUDA)` --
the arrangement `GpuSimpleDiscretization` and `GPUBackend` already use. Stage 7D
follows it exactly:

```text
include/cfd/gpu/GpuKEpsilonCorrection.hpp    CUDA-free facade, pimpl
cuda/kernels/GpuKEpsilonCorrectionCuda.cpp   real, forwards to correctKEpsilonDevice
src/gpu/GpuKEpsilonCorrection.cpp            stub, available() == false
```

Production and the equivalence tests call the **same** `correctKEpsilonDevice`.
If they did not, the equivalence those tests establish would say nothing about
what ships.

**The CPU-only stub was compiled and the CPU-only build verified** --
`build/final/.../gpu/GpuKEpsilonCorrection.cpp.o` exists and 246/246 turbulence
tests pass there. This repository has a recorded blind spot for files under
`if(NOT CFDAPP_ENABLE_CUDA)`: no routine gate compiles them, so it was checked
deliberately rather than assumed.

### Backend behaviour

Decided ONCE at construction, so a solve cannot change backend halfway through:

```text
CPU requested                  -> CPU path, unchanged
GPU requested + available      -> GPU path; usingGpuBackend() == true
GPU requested + unavailable    -> CPU path; usingGpuBackend() == false
                                  gpuUnsupportedReason() says why
```

A refusal is **not** a silent fallback. `usingGpuBackend()` reports what will
actually run, so a numerically-correct CPU fallback cannot be mistaken for GPU
execution -- and the differential asserts it before comparing anything.

### 7G / 7H measured, through the PUBLIC entry point

Both arms are `KEpsilonModel::correct`, differing only in `kSolver.backend`.

```text
one correction, walls OFF   k 3.324e-16  epsilon 3.088e-16  mu_t 8.208e-16
one correction, walls ON    k 3.916e-16  epsilon 6.721e-16  mu_t 8.725e-16

five chained corrections through the public interface, walls ON:
  1  k 3.916e-16   eps 6.721e-16   mu_t 8.725e-16
  2  k 4.772e-16   eps 5.407e-16   mu_t 6.723e-16
  3  k 1.030e-15   eps 1.321e-15   mu_t 2.622e-15
  4  k 2.023e-15   eps 1.300e-15   mu_t 8.295e-15
  5  k 1.965e-15   eps 4.212e-15   mu_t 7.563e-15
```

Against the frozen 2.1e-12 — three to four orders inside it, and not growing.

The wall diagnostics a user exports (`y+`, wall shear, the band counters, the
y+ extremes) are compared **bitwise**, not approximately: both arms run the same
host wall treatment, so anything but exact equality would mean one of them
re-derived it.

### Production provenance (7D-5)

```text
StrainRate  Production  TurbulentViscosity       Gpu
KAssembly   KSolve                               Gpu
EpsilonAssembly  EpsilonSolve                    Gpu
WallTreatment                                    host (NotAttempted)
turbulenceGpuPathQualified()                     true
allOnGpu()                                       FALSE
```

Asserted after a correction driven through the real `KEpsilonModel::correct`.

### The CPU path is unchanged

Two CPU-configured models constructed identically produce bit-identical k,
epsilon and mu_t. The seam does not leak into the default path.

---

## 6b. WHAT STAGE 7 STILL DOES NOT DELIVER

**7I — resident SIMPLE integration — is not done.** `SIMPLE.cpp:458` still
requires `activeModel->name() == "laminar"` for the GPU-resident loop, so a
turbulent case still takes the per-iteration-download path even when
`KEpsilonModel` itself is running on the device.

That check protects a real invariant, documented at `SIMPLE.cpp:440-447`: a
resident loop deliberately leaves the host velocity stale, which is harmless
for `LaminarModel::correct()` (a documented no-op) but not for a model that
genuinely reads velocity. `KEpsilonModel::correct` reads velocity, so the check
cannot simply be widened to "k-epsilon is fine" -- the velocity it receives has
to be the one the CPU algorithm would have given it.

Also not done: 7I-3 (a full turbulent SIMPLE case, GPU vs CPU) and the 7J
end-to-end residency measurement that depends on it.

---

## 7. Files

| file | change |
|---|---|
| `include/cfd/gpu/DeviceKEpsilon.hpp` | `solveScalarTransportResident`, `DeviceKEpsilonSolveReport`, `turbulenceGpuPathQualified`, per-equation Krylov workspaces in the state |
| `cuda/kernels/DeviceKEpsilonCuda.cpp` | host round-trip solver REMOVED; resident solve wired; trace records the four assembly/solve operations |
| `tests/unit/gpu/test_device_kepsilon_correction.cpp` | 6 tests, including the plan-sharing control |

## 8. Gate status

```text
resident scalar solver                  PASS
k matrix stays device resident          PASS
epsilon matrix stays device resident    PASS
k/epsilon plans remain distinct         PASS  (+ control)
CPU wall treatment preserved            PASS  (documented, §5)
old-state lag semantics                 PASS
atomic turbulence-state publication     PASS
production provenance                   PASS  for the qualified path
determinism                             PASS
negative controls                       PASS
existing turbulence regression          PASS  246/246
production GPU k/epsilon assembly+solve NOT WIRED  -- 7D
resident SIMPLE integration             NOT WIRED  -- 7I
production one-correction differential  BLOCKED on 7D
production chained differential         BLOCKED on 7D
transfer/residency audit                PARTIAL   -- §2, full audit needs 7I
```
