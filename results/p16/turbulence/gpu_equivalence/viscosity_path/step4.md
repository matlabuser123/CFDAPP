# Step 4 — qualified resident k-epsilon through production SIMPLE

**Status: PARTIAL.** The seam is built, the guard is a capability rather than a
name, and a real `SIMPLE::solve` runs turbulent k-epsilon on the resident loop
and matches the CPU reference at convergence. Four of the Step-4 gates are not
yet demonstrated, and the reason is the same for all of them: the only k-epsilon
case that CONVERGES in this repository runs with wall functions OFF.

---

## 1. The guard is now a capability

```cpp
const bool residentTurbulence = activeModel->supportsResidentGpuCorrection();
const bool useResidentSimpleLoop =
    useResidentPressureSolve &&
    (activeModel->name() == "laminar" || residentTurbulence) && ...
```

The old condition was `name() == "laminar"`, and SIMPLE's own comment said
exactly why: a resident loop stops downloading velocity, so a model whose
`correct()` reads the host field would be fed a value frozen at iteration 0.
Laminar was immune only because its `correct()` is a no-op.

So the replacement asks the model whether it can be corrected **without the host
velocity**. `TurbulenceModel` answers `false` by default, which keeps SST and
anything added later out *by construction* rather than by an exclusion list
someone has to remember to extend. `KEpsilonModel` answers `gpuActive_` — the
single conjunction, decided once in its constructor, of "GPU backend requested,
device usable, every plan built".

The correction itself goes through `correctFromResidentDiscretization`, one call
a CPU-only translation unit may name, with no CUDA type crossing the header. It
borrows the resident `U(N-1)` and corrected face flux, runs the qualified
resident correction, then publishes `mu_eff` to the device and scatters the
compact wall-face viscosity overrides back — so `SIMPLE` never builds an
effective-viscosity field on the host to upload, and the `setViscosity` upload is
skipped whenever a resident correction produced it.

Iteration 0 deliberately stays on the host path: `uploadInitialState` has not run
yet, and the host velocity *is* current there, being the initial condition.

## 2. Gates that PASS

| gate | evidence |
|---|---|
| resident capability query | `QualifiedKEpsilonEngagesTheResidentLoop` asserts `residentSimpleLoop` directly — a field comparison alone would pass just as happily if the path had declined |
| laminar compatibility | `LaminarStillEngagesTheResidentLoop` |
| SST refusal | `SstIsRefusedByTheCapabilityQuery` — both the query and the loop |
| unqualified model refusal | `CpuBackendKEpsilonIsNotAdmitted` — a CPU-configured k-epsilon answers false |
| real turbulent SIMPLE GPU run | the differential below runs `SIMPLE::solve`, not a harness |
| CPU/GPU production differential | converged, see §3 |
| U / p / flux equivalence | 3.7e-09 / 2.7e-08 / 9.8e-10 |
| k / epsilon / mu_t equivalence | 4.7e-07 / 2.8e-07 / 7.7e-07 |
| convergence classification | both `Converged` |
| outer iteration count | 1061 on both |
| continuity | 3.207e-13 vs 3.208e-13 |
| determinism | repeated resident solve is bitwise |
| device mu_eff reaches momentum | `DeviceEffectiveViscosity.*`, both stale-mirror directions |
| wall override does not leak | `DeviceWallFaceViscosity.WallValueDoesNotLeakToInteriorFaces` |

Suites: GPU 181/181, SIMPLE 123/123, turbulence 246/246 (CUDA and CUDA-OFF),
CPU-only build clean.

## 3. The differential, and why it is run to convergence

```text
CPU/GPU production differential, 100 cells, 1061 outer iterations
  U 3.736e-09  p 2.721e-08  flux 9.831e-10
  k 4.722e-07  eps 2.791e-07  mu_t 7.676e-07
  continuity cpu 3.207e-13 gpu 3.208e-13   mass 0.000e+00 both
```

Bound: 1e-6, the one `test_simple_gpu_solver.cpp` already adopted. Not loosened.

The difference falls monotonically as the two arms converge —
2.1e-3, 1.9e-4, 7.3e-6, 4.7e-7 at 50, 200, 600 and 2000 outer iterations — which
is the signature of one fixed point reached by two transient paths. The test
therefore asserts `Converged` on **both** arms before applying the bound, because
applying a converged-solution bound to an exhausted iteration budget is the
error `results/gpu-pipe-001/equivalence/summary.md` §2 records.

### Two defects found in this test while building it

Both are recorded because both produced confident-looking results:

1. **The first case diverged on the CPU.** An inlet/outlet channel at 16x8 took
   continuity from 6.2e-2 to 1.5e+9 over 2000 iterations, on *both* backends. A
   differential against a diverging reference measures nothing. Replaced with
   the repository's own converging k-epsilon cavity.
2. **The U and p comparisons were vacuous.** `SIMPLE::solve` takes the initial
   fields BY VALUE and returns the solution in `SIMPLEResult`; the test read the
   local fields back, comparing two untouched copies of the initial condition
   and reporting exactly `0.000e+00` forever. It now reads `result.velocity`,
   `result.pressure` and `result.massFlux`, and asserts the CPU solution is
   non-trivial before comparing.

## 4. Gates NOT yet demonstrated, and the single reason

**The lid-driven cavity reaches `SIMPLEStatus::NonFiniteState` after 3 outer
iterations with wall functions ON — on BOTH backends.** The CPU arm runs no GPU
code at all, so this is the case, not the resident path. A cavity is a poor
wall-function geometry: its corners put y+ far outside the law's valid band. The
repository's validated wall-function case is the **channel**, not the cavity.

With wall functions off, `wallFaces_` is empty, so these remain open:

```text
wall viscosity reaches momentum (in production)   NOT DEMONSTRATED
production provenance (WallTreatment = Cpu)       NOT DEMONSTRATED
wrong-flux negative control                       NOT WRITTEN
production residency ledger                       NOT MEASURED
```

The wall-face override itself **is** qualified against the host assembly
(`DeviceWallFaceViscosity.*`, five gates including the leak test and its vacuity
guard). What is missing is a *production* differential with wall functions
active, which needs a converging wall-function case — the channel harness in
`tests/integration/turbulence_channel/`, not a unit-test cavity.

## 5. What this does not claim

Step 4 is not complete, so P16 is not ready for final qualification. In
particular nothing here justifies ticking `GPU equivalence`: the production
wall-function path, the residency ledger for a real turbulent outer iteration,
and the wrong-flux control are all still outstanding.
