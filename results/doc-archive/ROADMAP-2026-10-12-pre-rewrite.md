# CFDApp — Roadmap

> **This file owns:** project vision, milestones, phase definitions and goals, major capabilities,
> dependencies, scope boundaries, long-term sequencing and the v1.0 definition. It answers *where is
> CFDApp going, and why*.
>
> **It does not own:** live phase status, blockers or next actions → [TODO.md](TODO.md) · technical
> debt → [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) · measurements, test counts, fingerprints and gate
> evidence → `results/` · engineering rules → [CLAUDE.md](CLAUDE.md).
>
> **TODO.md is authoritative for current status.** This file describes what a phase *is*; TODO
> describes where it *stands*. ROADMAP naming a phase while TODO calls it PARTIAL is not a
> contradiction. This file changes slowly — only when direction, phase scope, dependencies or v1.0
> content change, never because a checkbox moved.

---

# 1. Vision

CFDApp aims to become a **trustworthy general-purpose finite-volume CFD platform**.

A capability is mature only when it is:

* numerically verified using analytical solutions, MMS, convergence studies and conservation checks;
* physically validated against appropriate benchmark data;
* integrated through a production solver path rather than existing only as a library API;
* regression tested;
* deterministic where required;
* CPU/GPU equivalent where a GPU implementation is claimed;
* supported by preserved evidence under `results/`.

**Correctness takes priority over performance.**

Development proceeds in verified dependency order:

```text
Core CFD -> Production maturity -> Generic transport -> Thermal / energy -> Turbulence
   -> Compressible flow -> Species / reactions -> Multiphase · Advanced SST
   -> Advanced numerics · 3D parity · Moving mesh · HPC -> Production workflow
   -> V&V expansion -> Release hardening -> v1.0
```

---

# 2. Milestones

```text
M1   Core Solver Foundation        P0–P12
M2   Production CFD Foundation     P13–P16
M3   Compressible + Reacting       P17 + P18
M4   Multiphase + Advanced Turb.   P19 + P20
M5   High-Accuracy General CFD     P21 + P22
M6   Dynamic Geometry              P23
M7   Scalable CFD                  P24
M8   Production Workflow           P25
M9   Broad Verification & Valid.   P26
M10  Production Hardening          P27
M11  CFDApp v1.0                   P28
```

**M3 completes when** P17's remaining qualification is addressed as required, P18's G8 is closed,
and P18 is fully requalified. Live status for each: [TODO.md](TODO.md).

---

# 3. Development Principles

1. **Numerical correctness before optimization.**
2. **New physics requires independent physical validation.**
3. **New numerical methods require quantitative verification.**
4. **The CPU implementation remains the numerical reference.**
5. **GPU claims require CPU/GPU equivalence evidence.**
6. **GPU residency claims require transfer evidence.**
7. **Performance claims require equivalent numerical solves.**
8. **Failed and excluded results remain preserved.**
9. **Do not weaken gates or tolerances to obtain PASS.**
10. **Audit existing functionality before creating replacements.**
11. **Do not fix unrelated technical debt inside an active phase.**
12. **Implementation and final qualification are separate gates.**
13. **Preserve existing backends when adding a new solver architecture.**
14. **A capability is not production-ready merely because an API exists.**
15. **No phase begins without explicit authorization in `TODO.md`.**

The binding execution rules remain in [CLAUDE.md](CLAUDE.md).

---

# 4. Current Capability

What the platform can do, and the main boundary of each area. This table describes *capability*, not
qualification state — for which gates pass and which are open, see [TODO.md](TODO.md).

| Area | Capability | Main remaining limitation |
| --- | --- | --- |
| Incompressible flow | Steady SIMPLE, transient PISO, validated 2D foundation | Advanced numerics and broader 3D parity remain |
| Linear solvers | CG, BiCGSTAB, GMRES; CPU and qualified GPU paths | Multigrid and stronger preconditioners remain |
| Generic transport | Reusable convection, diffusion, transient and source infrastructure | Must not be forced onto equations with incompatible conservative semantics |
| Thermal / energy | Temperature-dependent `k(T)` / `cp(T)`, enthalpy relations, Robin BCs, transient support | Variable-`cp` total energy remains debt |
| Turbulence | Eddy-viscosity framework, k-epsilon, wall treatment / y+, validated CPU/GPU scope | Advanced SST near-wall and separated-flow validation deferred to P20 |
| Compressible | Variable density, EOS, compressible continuity/momentum, total energy, HLLC, density-based solver, shock and low-Mach validation | Qualified only inside a documented envelope; HLLC positivity under head-on flow remains debt |
| Species / reactions | Multi-species transport, mixture thermodynamics, Arrhenius chemistry, heat release, CPU and GPU; reacting chemistry reachable from a case file and marchable | No canonical reacting-flow validation (G8); constant `cp` on device |
| Multiphase | Volume-fraction and mixture-property foundation | Interface compression, surface tension, curvature and validation are P19 |
| Meshes | Structured 2D production meshes; Cartesian 3D foundation; ALE/GCL foundation | General 3D geometry and production moving-mesh integration remain |
| CUDA | Qualified GPU solver/discretization infrastructure and resident scalar capabilities | Native Windows CUDA unqualified; some future physics still need GPU ports |
| Application | CLI and Qt/QML GUI on shared production infrastructure | New capabilities must be integrated deliberately |
| Restart | Steady-solver checkpointing foundation | Turbulence checkpointing unsupported |
| Sanitizer coverage | ASan/UBSan over the suite at CI settings; a dedicated 150-cell premixed-flame fixture puts the reacting path (`recentre`, `locateFront`, `shiftField`, the reactor sub-stepping) under the sanitizers, clean | The heavy G8 premixed-flame cases, 500–1500 cells, are **excluded** from the `asan` preset — they exceed `ctest --timeout 7200` (the 1000-cell level was killed at the cap), so those problem sizes run under no sanitizer. The fixture covers the same functions, not the same sizes |

---

# 5. P0–P16 — Foundation — COMPLETE

## P0–P12 — Core CFD

Established the platform: finite-volume discretization, structured meshes, steady SIMPLE, transient
PISO, thermal transport, turbulence foundations, species and multiphase foundations,
compressible-flow foundations, numerical robustness, production mesh work, 3D Cartesian foundations,
ALE / moving-mesh foundations, CLI / GUI integration, and validation and regression infrastructure.

The P12 compressible foundation — scalar/face density, ideal-gas EOS, coupled `CompressibleSIMPLE`,
density as an iterated state, compressible continuity and momentum, low-Mach limiting — is an input
to P17 and must be reused where valid.

## P13 — Production Maturity

Closed the production-maturity and final-qualification line: restart/checkpoint infrastructure,
case validation, CLI/GUI consistency, results and export verification, production hardening,
qualification and regression evidence, CPU/CUDA qualification, determinism and compatibility checks,
and formal source fingerprinting.

**P13 establishes the qualification discipline every later phase follows.**

## P14 — Generic Transport Foundation

Reusable scalar-transport infrastructure — convection, diffusion, transient terms, source terms,
boundary semantics, variable coefficients, CPU/GPU foundation — built *instead of* additional
duplicated assemblers. Later phases collapse onto this stack rather than adding a third.

## P15 — Thermal / Energy

Thermal transport, temperature-dependent material properties, sensible enthalpy, thermal boundary
conditions, transient thermal treatment, validation and regression protection.

## P16 — Turbulence

Production turbulence within a qualified scope: turbulence transport, wall treatment and y+,
validation, and CPU/GPU qualification within the adopted scope. **Advanced near-wall SST work was
deliberately deferred to P20** rather than left half-done.

---

# 6. P17 — Compressible Flow

**Goal.** Compressible flow on the existing SIMPLE foundation, extended to a density-based solver
where the physics requires it, without replacing the incompressible path.

**Major capabilities.** Variable density · ideal-gas EOS · compressible continuity · compressible
momentum · total-energy equation · HLLC flux · density-based solver · low-Mach validation · shock
validation · CPU/GPU equivalence.

**Depends on.** P12's compressible foundation, P14's generic transport, P15's thermal/energy.

**Major qualification objective.** Quantitative validation across the low-Mach limit and a shock
regime, with CPU/GPU equivalence, inside an explicitly documented envelope.

**Scope boundaries.** The density-based solver does **not** replace SIMPLE. Reacting-shock
capability is not claimed. The phase is usable only inside its documented qualified envelope, and
the envelope is part of the deliverable rather than a caveat added afterwards.

**Milestone.** A compressible solver qualified within a stated envelope, with its limits recorded.

---

# 7. P18 — Species / Reactions

**Goal.** Conservative multi-species reacting flow on the generic transport, thermal and
compressible foundations.

**Major capabilities.** Multi-species state with `ΣY_i = 1` and a bounds policy · conservative
`ρY_i` transport with species-dependent diffusivity · mixture properties and composition-dependent
EOS · reaction source framework · Arrhenius chemistry · heat-release coupling · conservation and
validation · CPU/GPU equivalence.

**Depends on.** P14 (transport), P15 (thermal/energy), P17 (compressible coupling).

**Major qualification objective.** Canonical reacting-flow validation — a 1D laminar premixed flame
whose flame speed falls inside a cited published spread, with the reference frozen before
measurement.

**What P18.0's audit established, and it constrains the scope above:**

* existing species support is production-reachable and **shipped**, so passive-scalar preservation
  is a backward-compatibility obligation on a user-visible feature, not a nice-to-have;
* "multi-species transport" already existed as N independent scalars; what was missing was the
  **collection** — stable identity, closure, bounds;
* P14 already supplies the transient, the per-cell source pair and the per-cell diffusivity, so
  P18 **collapses two stacks** rather than adding a third;
* there was no GPU species path at all, making the equivalence step an implementation phase.

**Scope boundaries.** Reacting-shock capability is not claimed from heat-release coupling. Elemental
conservation is not modelled, so elemental metadata is out of scope. A library API that no case file
can reach is not production capability (CLAUDE.md §9).

**Milestone.** A reacting-flow capability with canonical validation closed and conservation
quantified.

---

# 8. P19 — Multiphase — NOT AUTHORIZED

**Goal.** Conservative interface-capturing multiphase flow, without regressing single-phase CFD.

**Depends on.** Mature generic transport and the conservation infrastructure.

```text
P19.0  Baseline & architecture    audit transport infrastructure · define multiphase state ·
                                  freeze conservation requirements · freeze gates · freeze fingerprint
P19.1  Bounded VOF transport      alpha · conservative transport · 0 <= alpha <= 1 · mass
                                  conservation · sharp-interface tests · CPU reference numerics
P19.2  Interface compression      compression flux · boundedness · strength controls · static
                                  advection · rotating interface
P19.3  Surface tension/curvature  normals · curvature · CSF source · static droplet · parasitic
                                  currents · Laplace pressure
P19.4  Gravity                    body-force coupling · hydrostatic balance · density contrast
P19.5  Canonical validation       dam break · rising/falling droplet · interface advection · grid
                                  and time-step sensitivity · mass conservation · determinism
P19.6  CPU/GPU equivalence        VOF · compression · surface tension · gravity · conservation
P19.7  Final qualification        full regression · P14–P18 preservation · canonical suite ·
                                  conservation · determinism · limitations · fingerprint
```

**Scope boundaries.** **Do not claim full multiphase CFD from the current volume-fraction
foundation.** Density and viscosity mixture laws are in scope; phase change is not.

**Milestone.** A qualified multiphase solver with quantitative validation.

---

# 9. P20 — Advanced SST Near-Wall Treatment — NOT AUTHORIZED

**Goal.** The turbulence scope intentionally deferred from P16: a fully qualified wall-resolved and
automatic SST workflow.

**Depends on.** P16's eddy-viscosity framework and wall treatment.

```text
P20.0  Baseline & audit           freeze current SST behaviour · audit wall distance and wall
                                  treatment · freeze validation gates
P20.1  Wall-resolved SST          near-wall formulation · wall-distance consistency · limiting
                                  functions · production/destruction terms · boundary treatment
P20.2  Automatic low/high-y+      low-y+ branch · high-y+ branch · smooth transition · invalid y+
                                  guards · deterministic switching
P20.3  Separation/reattachment    separation · reattachment · wall shear · Cf · Cp
P20.4  Driver & Seegmiller        resolve the timeout/robustness debt · quantitative benchmark ·
                                  grid sensitivity · convergence characterization
P20.5  APG benchmark              adverse pressure gradient · velocity profiles · wall shear ·
                                  separation response · published comparison
P20.6  GPU equivalence            SST fields · wall treatment · production/destruction · residuals
P20.7  Final qualification        full regression · canonical turbulence suite · limitations
```

**Scope boundaries.** **Do not restart P16's automatic-wall attempts** without a newly defined P20
architecture and gate set. No turbulence-model proliferation without a validation need.

**Milestone.** Wall-resolved SST validated on separated flow.

---

# 10. P21 — Advanced Numerics — PLANNED

**Goal.** Improve accuracy and robustness while preserving the validated first-order reference paths.

Future numerical infrastructure may include Rhie–Chow as the default where justified, multigrid,
stronger preconditioners, coupled pressure-based solution, improved Krylov reductions, higher-order
reconstruction for compressible flow, and broader irregular-mesh robustness. These are not
automatically part of P17 unless a scoped P17 blocker genuinely requires them.

```text
P21.0  Numerical baseline         inventory discretisation schemes · freeze first-order
                                  fingerprints · define numerical-order gates
P21.1  Higher-order spatial       second-order convection · gradient reconstruction ·
                                  non-orthogonal correction · skewness treatment · limiting
P21.2  Bounded high-resolution    TVD/NVD framework · bounded scalar reconstruction · species
                                  boundedness · VOF compatibility where applicable
P21.3  Higher-order time          second-order transient scheme · variable time step · temporal
                                  consistency tests · startup handling
P21.4  Solver & preconditioner    selection by matrix class · GMRES · BiCGSTAB robustness policy ·
                                  preconditioners · breakdown handling · convergence diagnostics
P21.5  Convergence verification   manufactured solutions · spatial and temporal observed order ·
                                  distorted meshes · conservation preservation
P21.6  CPU/GPU equivalence        higher-order operators · limiters · solvers · determinism
P21.7  Final qualification        full regression · first-order paths preserved · quantitative
                                  order verification · fingerprint
```

**Capability gained.** Higher formal accuracy on the same meshes, and a solver framework chosen by
matrix class rather than by default.

**Scope boundary.** The validated first-order paths must survive unchanged. A second-order scheme
that *replaces* rather than supplements them is a regression, not an improvement.

---

# 11. P22 — 3D Physics Parity — PLANNED

**Goal.** The validated 2D physics stack working correctly and equivalently in 3D. The existing
Cartesian 3D foundation should be extended incrementally rather than bypassed.

```text
P22.0  3D core audit              mesh topology · face orientation · vector/tensor handling ·
                                  boundary geometry · volume calculations
P22.1  3D incompressible          continuity · momentum · pressure correction · canonical validation
P22.2  3D thermal                 conduction · convection · thermal boundaries
P22.3  3D turbulence              k-epsilon · SST · wall distance · y+ · canonical validation
P22.4  3D compressible            EOS · continuity · momentum · total energy · fluxes
P22.5  3D species / reactions     species transport · diffusion closure · chemistry · heat release
P22.6  3D multiphase              VOF · interface compression · surface tension · gravity
P22.7  CPU/GPU parity             all qualified 3D physics · deterministic comparisons
P22.8  Final qualification        full 3D regression · conservation · benchmark matrix
```

**Capability gained.** The 2D physics stack usable in 3D with equivalent evidence.

**Scope boundary.** 3D parity is claimed **per physics**, never as a blanket statement. General 3D
geometry is a separate concern from Cartesian 3D parity.

---

# 12. P23 — Moving Mesh Production — PLANNED

**Goal.** Move from an experimental, library-level ALE/GCL foundation to a conservative production
moving-mesh capability: case-format, CLI and GUI integration, production 3D ALE, moving-mesh MMS,
restart support, and interaction with turbulence, thermal and compressible physics.

```text
P23.0  Architecture & baseline    audit existing moving-mesh code · define ALE formulation ·
                                  freeze conservation requirements
P23.1  Mesh motion                prescribed rigid motion · translation · rotation · deformation
P23.2  Geometric conservation     GCL formulation · volume change · face swept volume ·
                                  uniform-flow preservation
P23.3  ALE transport              continuity · momentum · scalar transport · thermal · species
P23.4  Moving boundaries          moving wall · no-slip consistency · force extraction
P23.5  Mesh quality / failure     quality metrics · inversion guards · timestep restrictions
P23.6  Validation                 moving piston · translating body · oscillating body · GCL
                                  benchmark · grid and time-step sensitivity
P23.7  CPU/GPU equivalence        geometry update · ALE flux · fields · conservation
P23.8  Final qualification        full regression · moving-mesh canonical suite · fingerprint
```

**Capability gained.** Conservative mesh motion reachable from a case file.

**Scope boundary.** **Do not silently convert library-level ALE support into a claimed production
capability.** Uniform-flow preservation under motion (P23.2) is the gate that decides whether
anything else can be claimed: without the GCL satisfied *discretely*, moving-mesh results are not
conservative whatever the benchmarks show.

---

# 13. P24 — HPC & Distributed Scaling — PLANNED

**Goal.** Scale qualified physics from one CPU/GPU to multiple nodes and devices **without changing
the mathematics**. Long-term direction includes OpenMP scaling, MPI domain decomposition, multi-GPU,
larger-grid optimization, reduced host/device synchronization and reproducible performance baselines.

```text
P24.0  HPC baseline               profile CPU and GPU · memory model · communication model ·
                                  freeze single-device reference
P24.1  Domain decomposition       partition mesh · owner/ghost cells · halo exchange
P24.2  MPI                        distributed scalar and vector fields · global reductions ·
                                  convergence synchronization
P24.3  Distributed linear solvers distributed assembly · preconditioning · convergence ·
                                  partition-independent results
P24.4  Multi-GPU                  device partitioning · halo transfers · peer transfer and
                                  GPU-aware MPI where supported
P24.5  Comm/compute overlap       asynchronous halo exchange · interior/boundary split · profiling
P24.6  Scaling                    strong and weak scaling · efficiency · memory scaling
P24.7  Reproducibility            partition-count comparison · deterministic reductions where
                                  required · conservation · physics-equivalent solves
P24.8  Final qualification        multi-node regression · CPU/GPU/HPC comparison · scaling report
```

**Capability gained.** The same mathematics on more hardware.

**Scope boundary.** P24.7 is non-negotiable: a result that depends on the partition count is not a
scaled solve, it is a *different* solve. Scaling numbers mean nothing until it passes. Performance
work remains secondary to numerical correctness.

---

# 14. P25 — Production Workflow & Reliability — PLANNED

**Goal.** Make qualified physics practical for sustained production use. This phase adds no physics;
it makes the physics already qualified survivable in real use.

```text
P25.1  Case schema                versioned schema · validation · migration strategy · diagnostics
P25.2  Restart / checkpoint       checkpoint fields · restart · deterministic restart ·
                                  compatibility checks
P25.3  Failure recovery           clean solver failure propagation · invalid-state diagnostics ·
                                  partial-result preservation · crash-safe output where practical
P25.4  Batch / parameter studies  batch runner · parameter sweep · reproducible metadata ·
                                  case fingerprints
P25.5  Output & post-processing   consistent CSV/VTK/JSON · derived quantities · probes · forces ·
                                  residual histories · conservation summaries
P25.6  GUI production integration case creation · physics configuration · validation errors ·
                                  solver monitoring · result inspection
P25.7  Reliability qualification  restart equivalence · invalid-case suite · long-run stability ·
                                  deterministic metadata · full regression
```

**Scope boundary.** A restart that is not bitwise-or-documented-equivalent to an uninterrupted run
is not a restart. P25.2 and P25.7 are one requirement split across two sub-phases.

---

# 15. P26 — Verification & Validation Expansion — PLANNED

**Goal.** A broad, traceable benchmark matrix supporting credible engineering use.

```text
Verification        manufactured solutions · analytical solutions · conservation cases ·
                    spatial-order · temporal-order · distorted-mesh tests
Incompressible      cavity · channel · external flow
Thermal             conduction · convection · conjugate-ready cases where scoped
Turbulence          channels · separation · APG · reattachment
Compressible        shock tubes · nozzles · low-Mach limit · qualified shock cases
Reacting            batch reactor · plug flow · premixed flame · additional chemistry
Multiphase          interface advection · droplet · dam break · capillary
Infrastructure      reference provenance · frozen tolerances · automated comparison ·
                    negative controls · generated qualification reports
```

**Scope boundary.** Every reference needs provenance frozen before measurement, and every comparison
needs a negative control. A validation suite without those is a collection of agreements, not
evidence.

---

# 16. P27 — Performance & Release Hardening — PLANNED

**Goal.** Optimize **only after** numerical equivalence and production reliability are established.

```text
Performance         authoritative benchmark suite · CPU, GPU, memory, transfer and solver profiling
Optimization        remove unnecessary transfers · reduce synchronization · kernel fusion where
                    justified · memory locality · fewer allocations · optimize sparse solves
Guardrails          no performance claim without an equivalent solve · CPU reference preserved ·
                    conservation preserved · determinism measured · no tolerance weakening
Release hardening   sanitizer matrix · static analysis · warnings clean · formatting clean ·
                    git diff --check clean · package/install testing · version metadata ·
                    reproducible build evidence
```

**Scope boundary.** A change that alters floating-point association is a **numerics** change, not a
performance change, and earns the full numerical ladder. Reordering a reduction, fusing a kernel,
switching to atomics and enabling FMA contraction all qualify.

---

# 17. P28 — v1.0 Final Qualification — PLANNED

**Goal.** Freeze and qualify the complete production scope.

```text
P28.0  Scope freeze               physics matrix · platforms · CPU/GPU configurations ·
                                  known limitations · acceptance gates
P28.1  Full regression            Release+CUDA · CPU-only · Debug+GUI · supported platform matrix
P28.2  Physics preservation       incompressible · thermal · turbulence · compressible ·
                                  species/reactions · multiphase · moving mesh · 3D
P28.3  Conservation qualification mass · momentum where measurable · energy · species ·
                                  interface volume · moving-mesh/GCL
P28.4  CPU/GPU/HPC equivalence    CPU · single GPU · multi-GPU · distributed CPU · distributed GPU
P28.5  Determinism                repeated runs · restart · thread counts · GPU block
                                  configurations · distributed decomposition where required
P28.6  Safety & diagnostics       ASan · UBSan · LSan · CUDA memcheck · racecheck · initcheck ·
                                  synccheck · live controls
P28.7  Documentation              user guide · developer guide · case-format specification ·
                                  physics limitations · validation report · benchmark
                                  methodology · release notes
P28.8  Final fingerprint          source · dependency versions · toolchain versions · benchmark
                                  fingerprints · release artifacts
```

**P28.9 — the release decision.** Declare `CFDApp v1.0 — QUALIFIED` **only** if every mandatory gate
passes. Otherwise `v1.0 — PARTIAL / BLOCKED`, with the blockers named.

---

# 18. Dependency Map

```text
P0–P12 core
   ↓
P13 production maturity        the qualification discipline every later phase follows
   ↓
P14 generic transport
   ↓
P15 thermal / energy
   ↓
P16 turbulence
   ↓
P17 compressible flow
   ↓
   ├─> P18 species / reactions
   ├─> P19 multiphase
   └─> P20 advanced SST
          ↓
   P21 advanced numerics · P22 3D physics parity
          ↓
   P23 moving mesh production
          ↓
   P24 HPC & distributed scaling
          ↓
   P25 production workflow & reliability
          ↓
   P26 V&V expansion
          ↓
   P27 performance & release hardening
          ↓
   P28 v1.0 final qualification
```

P18, P19 and P20 are independent of one another; their order may be changed by explicit
authorization. **No phase begins silently.**

---

# 19. v1.0 Definition

A v1.0 release represents more than feature accumulation. Expected maturity:

* a benchmark / V&V library;
* stable API boundaries;
* a stable case format with a migration policy;
* cross-platform qualification;
* reproducible CPU/GPU validation;
* robust diagnostics;
* documentation;
* packaging and installer qualification;
* release-candidate qualification;
* preserved known limitations.

The shipped solver must be conservative, quantitatively verified, validated against independent
references, deterministic within documented contracts, CPU/GPU equivalent within frozen tolerances,
scalable, restartable, diagnosable, regression protected, sanitizer qualified, reproducible, and
**explicit about every known limitation**.

Performance matters only after those properties are demonstrated.

---

# 20. Explicit Non-Goals / Guardrails

* **No duplicate physics implementations** when an existing one can be verified and reused.
* **No replacement of SIMPLE** solely to add a density-based compressible solver.
* **No full-multiphase claim** from the existing volume-fraction foundation.
* **No turbulence-model proliferation** without a validation need.
* **No unstructured-mesh expansion by accident.**
* **No moving-mesh production claim** until case / CLI / GUI integration is verified.
* **No GPU-performance claim** without equivalent numerical solutions.
* **No new phase without authorization.**
* **No threshold weakening to obtain PASS.**
* **No silent removal of failed evidence.**
* **No unrelated technical-debt cleanup inside active physics work.**

---

# 21. Where Everything Else Lives

```text
CLAUDE.md            engineering and process rules
TODO.md              live execution state, the current blocker, what is authorized next
TECHNICAL_DEBT.md    the technical-debt graph and each item's status, gate and evidence
results/             measurements, test counts, fingerprints, gate logs, known limitations
results/doc-archive/ snapshots of these control documents before structural rewrites
```

Qualification evidence per phase is under `results/p13/` … `results/p18/`, and each qualified phase
records its own limits in its `KNOWN_LIMITATIONS.md`. Debt discovered inside a phase goes to
`TECHNICAL_DEBT.md`, never into this file.
