# P13-001 — Restart / checkpointing: audit

Written before any production change, against `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274`.

The brief says "audit first … identify all state required to resume a SIMPLE solve exactly enough
to preserve production semantics" and "do not blindly serialize CUDA implementation details".
This file answers those two questions from the source, not from `TODO.md`.

---

## 1. What already exists

`TODO.md` lists `Restart / checkpointing` as unstarted under P13. That is drift in both
directions at once: a restart subsystem exists and is tested, but it does not do what P13 asks
for.

```text
include/cfd/solver/RestartSnapshot.hpp   solver-state model + validation   (P2 "Restart-A/B")
include/cfd/io/RestartWriter.hpp         JSON writer                       (P2 "Restart-C")
include/cfd/io/RestartReader.hpp         JSON reader                       (P2 "Restart-D")
src/solver/RestartSnapshot.cpp
src/io/RestartWriter.cpp
src/io/RestartReader.cpp
tests/unit/solver/test_restart_snapshot.cpp
tests/integration/restart/test_restart_io.cpp
tests/integration/restart/test_restart_resume.cpp
```

What it carries (`kRestartFormatVersion = 1`):

```text
format_version
state.time, state.step, state.delta_t
mesh.cell_count, mesh.face_count, mesh.fingerprint
fields.pressure, fields.velocity_x, fields.velocity_y, fields.mass_flux
```

### 1.1 Four gaps, each load-bearing for P13

| # | gap | evidence |
| --- | --- | --- |
| 1 | **Transient/PISO only.** The model is built from `TransientState` and is parameterised by physical time, step index and `delta_t`. The P13 production solver is *steady* `SIMPLE`, which has none of those. | `RestartSnapshot.hpp`; `makeRestartSnapshot(mesh, TransientState, time, deltaT, step)` |
| 2 | **2D only, by explicit refusal.** | `src/solver/RestartSnapshot.cpp:37,55` — `requireTwoDimensional(mesh, …)`; the writer emits only `velocity_x` / `velocity_y` |
| 3 | **Library-only — no production path.** Nothing under `apps/`, `src/app/` or the GUI references any of it. `CLAUDE.md` §9: a feature is not production capability because a library API exists. | `grep -rn 'RestartWriter\|RestartReader' apps src/app` → no hits |
| 4 | **Carries no configuration identity.** Mesh identity only. Nothing records the case, the solver settings, the schemes, the relaxation factors, the backend or the CFDApp version, so a snapshot cannot tell whether it is being resumed into a compatible configuration. | the schema above |

P13-001 therefore extends this lineage rather than replacing it. Format version 1 stays exactly as
it is and keeps its tests; the production checkpoint is a new, separately versioned artifact.

---

## 2. What a steady SIMPLE solve actually carries between outer iterations

Read from `src/pressure_velocity/SIMPLE.cpp` (1126 lines), not assumed.

### 2.1 The loop-carried state

```text
velocity   VectorField   cell    carried  (SIMPLE.cpp:1047  velocity = velocityNew)
pressure   ScalarField   cell    carried  (SIMPLE.cpp:1048  pressure = pressureNew)
massFlux   SurfaceField  face    carried  (SIMPLE.cpp:1049  massFlux = fluxNew)
```

All three are read by the next iteration's momentum assembly — `SIMPLE.cpp:517,523,530` pass
`velocity, pressure, massFlux` into the assembly call — so all three are genuine state, not
derived output.

### 2.2 The decisive finding — F is recomputed, not accepted

```cpp
// SIMPLE.cpp:238
SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
```

`SIMPLE::solve()` takes `initialVelocity` and `initialPressure` and always regenerates the face
mass flux by interpolating the cell velocity. There is no entry point that accepts an incoming
`F`.

So resuming a steady solve through today's public API cannot be exact. It would reintroduce
exactly the inconsistency this project already identified and refused for PISO:

> "a restart that only saved U/p and rebuilt F afterward would silently reintroduce exactly the
> inconsistency PISO was built to avoid" — `RestartSnapshot.hpp`

This is the one production change P13-001 genuinely requires inside the solver. Everything else is
additive plumbing.

### 2.3 The outer-iteration monitor is state too

`cfd::solver::OuterIterationMonitor` (`SolverRobustness.hpp:409`) holds five `ResidualTracker`s, a
distance tracker and an `AdaptiveRelaxationController`. Those feed:

* the normalization reference (`ConvergenceCriterion::Normalized`),
* stagnation detection (rolling window over the convergence distance),
* divergence detection (windowed growth),
* the adaptive relaxation factor used by the *next* iteration.

A resumed solve that started those from scratch at iteration `N1+1` would judge convergence
against a different reference and relax differently.

It does not have to be serialized. `OuterIterationMonitor::record()`
(`src/solver/SolverRobustness.cpp:430`) is a pure function of the sequence of
`OuterResidualSample`s, and `AdaptiveRelaxationController` says so in its own header:

> "Deterministic: the factors depend only on the sequence of measures."

So the checkpoint stores the outer residual sample history — solver state — and the resume replays
it into a fresh monitor. Nothing about the monitor's internal layout is serialized. That is what
"represent solver state, not raw device pointers" means here.

The full sample is wider than `SIMPLEResult`'s public histories:

```text
u, v, pressure, continuity        already in SIMPLEResult::*History
globalImbalance                   NOT historised today -- only the final value is kept
w                                 3D only; already in SIMPLEResult::wResidualHistory
turbulence                        optional; NOT historised today
```

`globalImbalance` and `turbulence` both feed `distance_` and `isConverged`
(`SolverRobustness.cpp:462,464`), so both must be in the checkpoint for an exact replay.

### 2.4 State that is NOT solver state

| quantity | why it is not checkpointed |
| --- | --- |
| Krylov scratch (`GpuKrylovWorkspace`, BiCGSTAB vectors) | `p'` is reset to 0 at the top of every outer iteration. Nothing survives an outer iteration. |
| device buffers, streams, `DeviceCsrMatrix`, allocator state | rebuilt from host state by `uploadInitialState` (`SIMPLE.cpp:461`). The brief forbids serializing these and they carry no information. |
| effective viscosity | recomputed every iteration from the active model. |
| `stageSeconds` | timing, not state. |
| linear-solver iterate | every inner solve is warm-started from the current field, which *is* checkpointed. |

### 2.5 State this checkpoint deliberately CANNOT represent

`KEpsilonModel` / `KOmegaModel` / `SSTModel` carry `k`, `epsilon`/`omega` and `mu_t` across outer
iterations, and all three are constructible only from uniform scalar initial values
(`KEpsilonModel.hpp`: "k/epsilon are initialized to config.initialK/initialEpsilon in every
cell"). There is no setter that could restore a checkpointed field.

Restoring them would mean adding state-injection APIs to three turbulence models and verifying
each numerically — a numerics change, not restart plumbing, and outside this item.

**Decision, recorded rather than worked around:** the production checkpoint refuses — at write
time *and* at resume time, with a diagnostic naming the model — any case running a
transport-equation turbulence model. It is never written silently and never resumed silently.
Logged as new technical debt.

`LaminarModel` (mu_t = 0) and `MixtureViscosityModel` (P6-PHYS-002: evaluated once at the case's
initial alpha and held fixed for the whole solve) are both stateless across outer iterations and
are therefore exactly resumable.

---

## 3. The state inventory the brief asks for

| brief item | where it lives | in the checkpoint |
| --- | --- | --- |
| mesh identity | `computeMeshFingerprint`, cell/face counts, `mesh.dimension()` | yes — identity, must match |
| case identity | `CaseDefinition` (physics / solver / boundary / initial) | yes — fingerprint, must match |
| iteration number | `SIMPLEResult::iterations` | yes |
| pressure | `ScalarField`, cell | yes — field data |
| U, V, W | `VectorField`, cell | yes — field data (W only when 3D) |
| face mass flux | `SurfaceField`, face | yes — field data, **the gap in §2.2** |
| material state | `physics.json` density / viscosity, constant | yes — inside the case fingerprint |
| relaxation settings | `SIMPLESettings` + adaptive controller | settings: case fingerprint; controller: reconstructed by replay (§2.3) |
| solver configuration | `SolverConfig` | yes — inside the case fingerprint |
| boundary conditions | `BoundaryConfig` | yes — inside the case fingerprint |
| convergence history / state | `SIMPLEResult` histories + monitor | yes — the residual sample history |
| backend | `LinearSolverSpec::backend`, `gpuDiscretization` | recorded, and **not** required to match — see §4 |
| dimensionality | `mesh.dimension()` | yes — identity, must match |
| transient state | n/a — the production path is steady SIMPLE | format version 1 already covers the transient / PISO case |

---

## 4. Backend policy

The checkpoint records which backend produced it, but resuming on a different backend is
**allowed**, because GPU-PIPE-001 already qualified CPU/GPU equivalence for this solver and the
checkpoint holds no device-specific data. A mismatch is reported, never rejected.

Refusing it would be the wrong call for a production feature: "the GPU node died, finish it on
CPU" is a main reason checkpointing exists.

---

## 5. What this means for the implementation

1. `SIMPLE` gains an optional, non-owning resume state, set through a setter — exactly the idiom
   `setMomentumSource` already established. Null by default, so every existing call site is
   structurally unchanged.
2. `ProjectRunner` gains checkpoint-interval and resume options, threaded the way
   `ProjectRunOptions` already threads the progress callback.
3. The CLI gains the flags. The GUI reaches the same code through `ProjectRunner` — one path.
4. A new versioned production checkpoint format, separate from `kRestartFormatVersion = 1`.

The equivalence property P13-001 must prove:

```text
one solve of N iterations   ==   a solve of N1 iterations + a resumed solve of (N - N1)
```

bitwise, on CPU and GPU, in 2D and 3D.
