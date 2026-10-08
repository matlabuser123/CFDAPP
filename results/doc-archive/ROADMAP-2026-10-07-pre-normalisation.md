# CFDApp — Roadmap

> **Where is the project going?**
> This file defines capability direction, phase dependencies, and long-term development order.
>
> Current work: [TODO.md](TODO.md) · Rules: [CLAUDE.md](CLAUDE.md) · Evidence: [results/](results/)

## Status Legend

| Status             | Meaning                                                         |
| ------------------ | --------------------------------------------------------------- |
| **COMPLETE**       | Implemented and verified with preserved evidence.               |
| **ACTIVE**         | Explicitly authorized and currently in progress.                |
| **BLOCKED**        | Authorized, but stopped at a genuine failed gate or dependency. |
| **PLANNED**        | Intended future phase, not yet authorized.                      |
| **NOT AUTHORIZED** | Candidate direction only.                                       |

A roadmap entry does **not** authorize implementation by itself. Authorization and the active working scope are recorded in `TODO.md`.

---

# 1. Vision

CFDApp aims to become a **trustworthy general-purpose finite-volume CFD platform**.

A capability is considered mature only when it is:

* numerically verified using analytical solutions, MMS, convergence studies, and conservation checks;
* physically validated against appropriate benchmark data;
* integrated through a production solver path rather than existing only as an isolated library API;
* regression tested;
* deterministic where required;
* CPU/GPU equivalent where a GPU implementation is claimed;
* supported by preserved evidence under `results/`.

Development proceeds by verified dependency order:

```text
Core CFD
  ↓
Production maturity
  ↓
Generic transport
  ↓
Thermal / energy
  ↓
Turbulence
  ↓
Compressible flow            ← CURRENT
  ↓
Species / reactions
Multiphase
Advanced SST
  ↓
Advanced numerics
3D physics parity
Moving mesh production
HPC
  ↓
v1.0
```

Correctness takes priority over performance.

---

# 2. Current Project State

## Completed major phases

* **P0–P12 — Core CFD** — COMPLETE
* **CUDA-QUAL-001** — COMPLETE
* **GPU-PCORR-001** — COMPLETE
* **AGENT-INFRA-001** — COMPLETE
* **CI-PERF-001** — COMPLETE
* **GPU-DISC-001** — COMPLETE
* **GPU-PIPE-001** — COMPLETE
* **P13 — Production Maturity** — COMPLETE
* **P14 — Generic Transport Foundation** — COMPLETE
* **P15 — Thermal / Energy** — COMPLETE
* **P16 — Turbulence within adopted scope** — COMPLETE

## Active phase

* **P18 — Species / Reactions** — implementation COMPLETE, qualification **PARTIAL**

  * last completed step: **P18.9 — Final Qualification** (executed in full; verdict PARTIAL)
  * next step: **none authorized.**
  * **P18.9 verdict:** 10 of 11 gates PASS. **G8 — canonical reacting validation — NOT CLOSED**, so
    P18 is PARTIAL. All three regressions matched a prediction frozen before the run, with **0 new
    failures** and **0 P18 failures** in any configuration; P14–P17 preservation measured at 32/32,
    195/195, 246/246 and 282/282; **host sanitizers run for the first time in P18 — ASan, UBSan and
    LeakSanitizer all 0 findings over 2 768 tests, with a proven live control**; compute-sanitizer
    memcheck and initcheck clean with their controls firing. Final source fingerprint
    `83d43668dfd4b198…` (433 files), verified stable through every step: P18.9 changed no source.
    Report: `results/p18/FINAL_QUALIFICATION.md`; limits: `results/p18/KNOWN_LIMITATIONS.md`.
  * **What blocks P18:** G8 requires a 1D laminar premixed flame with flame speed inside a cited
    published spread. That needs a reacting-flow solver, a stiff integrator and flame-speed
    extraction — none exist, and all are new feature development outside a qualification phase. G8
    was **not** amended, weakened or restated. Separately, and not a gate: **no case file can reach
    any of P18.2–P18.8**, so the reacting-species capability is a verified library rather than
    production capability (CLAUDE.md §9).
  * **P18.8 capability gained:** the species and chemistry thermochemistry runs on the GPU, from
    **one shared `CFD_HLLC_HOST_DEVICE` source that gcc and nvcc both compile** — mixture
    properties, the equation of state, Arrhenius rate coefficients, concentrations, progress rates,
    stoichiometric sources, both routes to the heat release, the energy pools, temperature recovery
    and one coupled reactor step with every intermediate exposed. Five kernels, one thread per cell,
    no shared memory, no atomics, no reductions. Equivalence against the CPU is **BITWISE for every
    quantity built from `+ − × ÷`** — the mixture properties, the EOS, the concentrations, the
    status and its first reason, every branch decision, the energy pools, the recovered `T`, the
    updated composition, and **the marched state over 50 steps** in which `T` moved 28.6 K. The four
    quantities downstream of a transcendental are bounded instead, at a **derived** ≤ 8 ULP against a
    measured ≤ 3. Determinism is bitwise across repeats and across block sizes 64/128/256/512;
    residency is **measured** at 0 host↔device calls and 0 bytes inside a 10-step resident loop; and
    the CUDA diagnostics are reported only alongside live controls the tools must flag.
  * **P18.8 limits, and the central one is a hard result:** **bitwise CPU/GPU equality is
    unreachable for any quantity that passes through a transcendental.** glibc and CUDA libdevice
    `exp`/`pow` are different implementations, and IEEE-754 requires correct rounding only for
    `+ − × ÷` and `sqrt` — which is why P17.7 and P17.10 reached bitwise, their shared header calling
    only `sqrt`. Removing the difference would mean replacing the host libm, i.e. changing the CPU
    reference. So `k`, `q`, `ω_i` and `S_E` are bounded, under authorized amendment **A2**, while ten
    of thirteen criteria stay at zero tolerance. Further limits: the device carries **constant `cp`
    only** while the CPU path supports ranged `cp`, so equivalence is established for the
    constant-`cp` subset and is silent outside it; integer reaction orders only; **species transport
    is not on the device** at all; **no reacting-flow solve**, so **G8 remains NOT closed**; no
    performance claim of any kind is made; and WSL2 evidence does not establish native Windows CUDA.
    Still **library-level and not case-file reachable** — the gap now spans **P18.2–P18.8**.
  * P18.8 regression: Release+CUDA 3014/0, CPU-only 2775/0, Debug+GUI 3072/**4** — **0 new
    failures**. The 4 Debug timeouts are the inherited set `P18_GATES.md` G11 describes, all four
    DriverSeegmiller cases passing in **both** Release configurations (1012–2011 s); the frozen
    7200 s timeout was not raised and no test was excluded. `DriverSeegmiller.MeshSequence` timed out
    at 7200.08 s, an **eighth** measurement (5 PASS / 3 TIMEOUT, all within 6% of the cap) carried to
    P18.9's G10. A first regression attempt was invalid — it deviated from the inherited
    `OMP_NUM_THREADS=1 ctest -j 8` policy and 15 of 16 long tests timed out from oversubscription —
    and is preserved as `INVALID AS AUTHORITATIVE`.
    report: `results/p18/cpu_gpu_equivalence/summary.md`
  * previous step: **P18.7 — Conservation & Validation** (PARTIAL)
  * **P18.7 capability gained:** quantitative, independently-referenced evidence that the
    integrated reacting system conserves what it must. Species mass closes per species on closed
    and open domains with every term reported separately; `ΣY_i = 1` holds to P18.1's own
    tolerance **with no normalization anywhere in the production path** (verified: nothing calls
    `normalizeMassFractions`); total modeled energy closes per step and overall; and the
    zero-reaction limit is **bitwise**, with `A = 0` and chemistry-off agreeing bit for bit. A
    closed adiabatic batch reactor is validated against an **independent mpmath quadrature** that
    shares no algorithm with CFDApp's explicit Euler, and a steady 1D adiabatic reacting plug flow
    exercises advection, species transport, chemistry and thermal coupling together against the
    same reference. Errors fall under both time-step and grid refinement, with observed order ≈ 1
    — exactly what explicit Euler and first-order upwind predict — and the two studies are
    separated **by construction** rather than by argument.
  * **P18.7 limits:** **G8 is NOT closed.** It requires a 1D laminar premixed flame with flame
    speed inside a published spread, and nothing drives the chemistry — 0 files outside `species/`
    reference it, 0 case keys define a mechanism — so closing it means building a reacting-flow
    solver, a stiff integrator and a flame-speed extraction, which this phase was told not to do.
    **Elemental conservation is N/A: no elemental metadata is modeled.** No stiff integrator, so
    the batch case is kept inside the explicit stability envelope and that is asserted. The
    explicit march carries a **sum-to-one step budget that is tighter for fewer species**. Still
    **library-level and not case-file reachable** — the gap now spans P18.2–P18.7.
  * P18.7 regression: Release+CUDA 2977/0, CPU-only 2758/0, Debug+GUI 3036/**3** — all three
    exactly as predicted, +19 everywhere and nothing else moved. `DriverSeegmiller.MeshSequence`
    passed at 6963.18 s, a **seventh** measurement (5 PASS / 2 TIMEOUT, all within 6% of the cap).
    report: `results/p18/conservation_validation/summary.md`
  * previous step: **P18.6 — Heat-Release Coupling** (COMPLETE)
  * **P18.6 capability gained:** a thermodynamically consistent, closed chemistry-energy loop. The
    same reaction progress rates now generate mass-conserving species sources **and** correctly
    signed chemical energy transfer; temperature is recovered from the updated thermochemical state
    and fed back into Arrhenius kinetics. `h_f,i` — the one thermochemical quantity CFDApp lacked —
    is added as a distinct type, never conflated with P15's sensible reference enthalpy. The source
    `S_E = −Σ_i h_f,i ω_i` was **derived from `dE/dt = 0` before coding**, resting on the measured
    fact that CFDApp transports `ρ·cp·T`, which holds no formation energy — so double counting is
    structurally impossible rather than merely avoided. **G7 — reacting energy conservation — is
    CLOSED**: closed-reactor total-energy drift **6.29 eps** against an allowance of 128, and the
    adiabatic final temperature matches an **independent** enthalpy balance **bitwise**.
  * **P18.6 limits:** **library-level, not case-file reachable** — no case key, parser field,
    solver dispatch, CLI or GUI defines a mechanism, mixture or formation enthalpy, so per
    CLAUDE.md §9 this is not production capability, and the gap now spans **P18.2–P18.6**.
    **Coupled to the P15 THERMAL equation only**: P17's `TotalEnergyState` still refuses a
    temperature-dependent `cp`, lifting it needs a variable-gamma EOS, and **no reacting-shock,
    detonation or supersonic-combustion capability exists or is claimed**. The coupling is
    **explicit operator splitting** — no sub-stepping, no iteration, no implicit treatment — and
    the march carries a **measured ~72-step sum-to-one budget** before P18.1 refuses the state,
    since §41 forbids renormalising. No equilibrium constant (P18.5's limit, carried forward); the
    Patankar `Sp` stays zero.
  * P18.6 regression: Release+CUDA 2958/0, CPU-only 2739/0, Debug+GUI 3017/**3** — all three as
    predicted, Debug+GUI on its second declared branch, +42 everywhere and nothing else moved.
    `DriverSeegmiller.MeshSequence` passed at 7168.06 s, a **sixth** measurement of that test
    (4 PASS / 2 TIMEOUT, all within 6% of the unchanged cap).
    report: `results/p18/heat_release/summary.md`
  * previous step: **P18.5 — Arrhenius Chemistry** (COMPLETE)
  * **P18.5 capability gained:** temperature-dependent kinetics, so a reaction rate is now
    *computed* rather than supplied. `k(T) = A T^n exp(-Ea/(R_u T))` with `R_u` reused from P18.3,
    elementary mass-action progress rates `q = k Π_i C_i^(ν'_i)` from `C_i = ρY_i/M_i`, and each
    cell evaluated from **its own** `T`, `ρ` and `Y_i` — no cached global rate coefficient, which
    would be wrong for every spatially varying temperature field. Kinetics bind to reactions **by
    name**, so a reordered parameter list cannot silently attach to the wrong reaction. Verified
    against an independent mpmath reference (dps=60, no CFDApp code) over 4 compositions × 3
    pressures × 3 temperatures: worst relative error `k` **11.05 eps**, `q` **10.24 eps**, `ω_i`
    **≤ 10.12 eps** against an allowance of 32. **G6 criterion (a) is CLOSED, and with P18.4's
    criterion (b), G6 is now CLOSED overall.**
  * **P18.5 limits:** **library-level, not case-file reachable** — no case key, parser field,
    solver dispatch, CLI or GUI surface defines a mechanism or its Arrhenius parameters, so per
    CLAUDE.md §9 this is not production capability, and the same now holds for P18.2–P18.5 as a
    four-phase-wide gap. No heat release: P18.5 **reads** `T` and never writes it, so the
    `reaction → heat → T → reaction` loop does not exist (P18.6). No equilibrium constant —
    forward and reverse parameters are supplied independently and a thermodynamically
    inconsistent pair is **not detected**. No third-body or pressure-dependent kinetics, no stiff
    integration, and the implicit Patankar `Sp` stays zero (`d(ω)/d(Y)` is still absent).
    `A`'s units depend on reaction order and cannot be validated in code.
  * P18.5 regression: Release+CUDA 2916/0, CPU-only 2697/0, Debug+GUI 2974/**4** — all three as
    predicted, Debug+GUI on its second declared branch, +38 everywhere and nothing else moved.
    `DriverSeegmiller.MeshSequence` timed out at 7200.12 s, a **fifth** measurement of that test
    (3 PASS / 2 TIMEOUT, all within 6% of the unchanged cap).
    report: `results/p18/arrhenius_chemistry/summary.md`
  * previous step: **P18.4 — Reaction Source Framework** (COMPLETE)
  * **P18.4 capability gained:** a generic, deterministic, mass-conserving reaction-source layer in
    which reaction TOPOLOGY is independent from KINETICS. A `Reaction` canonicalises
    `ν_i = ν''_i − ν'_i` against the authoritative registry; a `ReactionMechanism` refuses any
    reaction that does not satisfy `Σ_i M_i ν_i = 0`, **per reaction** rather than in aggregate;
    and `ω_i = Σ_r M_i ν_{i,r} q_r` fills the `Su` field P18.2 already consumed — no second
    assembly path, and molecular weights are read from P18.3 rather than duplicated.
    Reversible and irreversible forms carry distinct rate-validity semantics, and
    `ω(−q) = −ω(q)` holds bitwise. **G6 criterion (b), the stoichiometric closure, is CLOSED.**
  * **P18.4 limits:** no kinetics — `q` is supplied, and nothing here knows `A`, `n`, `Ea` or
    `exp()`. No heat release, no formation enthalpy, no chemistry time integration (the negative
    projection is *diagnosed*, never clamped), and the implicit Patankar `Sp` stays zero because
    linearising `ω` in `Y` needs a kinetics derivative. At P18.4's close **G6 overall was NOT
    closed** — its rate criterion and two of its three controls were P18.5's, as its own `phase`
    field says; **P18.5 has since closed them.** Elemental validation deferred; no case key
    defines a mechanism.
  * P18.4 regression: Release+CUDA 2878/0, CPU-only 2659/0, Debug+GUI 2937/3 — **all three exactly
    as predicted**, +38 everywhere and nothing else moved. The marginal
    `DriverSeegmiller.MeshSequence` passed at 7035 s, a fourth measurement of that test.
    report: `results/p18/reaction_source/summary.md`
  * previous step: **P18.3 — Mixture Properties** (COMPLETE)
  * **P18.3 capability gained:** a thermodynamically consistent composition-dependent ideal-gas
    mixture. `Y_i` now determines `M_mix` (via the mass-fraction relation `1/M = Σ Y_i/M_i`),
    `R_mix = Σ Y_i R_i`, `cp_mix(T,Y) = Σ Y_i cp_i(T)` from P15's `cp_i(T)`, and through the
    **existing** `ThermodynamicProperties` also `cv_mix`, `gamma_mix` and `rho = p/(R_mix T)`.
    Only two quantities are computed here; everything else is inherited, so no second EOS and no
    second enthalpy stack exist and `cp`, `cv` and `R` cannot drift apart. **G5 PASS**, with every
    identity inside 8·eps (two exactly zero) and the **single-species limit bitwise** against a
    directly-built P17 object.
  * **P18.3 limits:** ideal gas only. The mixture EOS is **library-level, not case-file reachable** —
    no solver consumes it, and the P17 single-species path is unchanged and remains production.
    `TotalEnergyState` still refuses a temperature-dependent `cp`: P18.3 supplies the prerequisite
    and documents the extension point but does not widen a qualified P17 path whose G6 is open.
    No chemistry, no heat release, no formation enthalpy, no GPU path.
  * P18.3 regression: Release+CUDA 2840/0, CPU-only 2621/0 — both exact at +34 — and Debug+GUI
    2899/3, the marginal `DriverSeegmiller.MeshSequence` passing this time. That test has now been
    measured on both sides of the 7200 s cap across three phases, so the inherited set is settled as
    **three reliable timeouts plus one marginal test**.
    report: `results/p18/mixture_properties/summary.md`
  * previous step: **P18.2 — Species Transport & Diffusion** (COMPLETE)
  * **P18.2 capability gained:** genuinely conservative multi-species transport. `rho Y_i` is
    advanced on the authoritative mass flux with the density-weighted transient
    `a_P = rho^{n+1}V/dt`, `b_P = rho^n Y^n V/dt`; each species carries its own
    `Gamma_i = rho D_i`; and the multi-species diffusive closure
    `J_i = J_i^0 − Y_i Σ_j J_j^0` is assembled **implicitly** as convection by the correction
    velocity, so `Σ_i J_i = 0` and `Σ_i Y_i = 1` emerge from the formulation rather than being
    repaired afterwards — no normalisation and no clipping anywhere.
    **G4 PASS, all four criteria**, including **the P18.0 species boundary lag FIXED**: ≤ 2 outer
    iterations against the previously measured 335.
    Every per-species term is assembled by P14; no second transport stack was created.
  * **P18.2 limits:** the conservative transport is **library-level, not case-file reachable** —
    that needs a carrier-species decision, because a shipped species field is a passive *tracer*,
    not a composition. The pre-P18 passive path remains production, unchanged. Convection is
    first-order upwind; no bounded scheme and no limiter were invented. `omega_i = 0`; no chemistry,
    no mixture properties, no GPU species path. Turbulence keeps the lagged boundary.
  * P18.2 regression: Release+CUDA 2806/0, CPU-only 2587/0, Debug+GUI 2864/4 — both Release
    configurations exact at +34, and the Debug+GUI set is the known inherited four, with the
    marginal `DriverSeegmiller.MeshSequence` timing out this time exactly as the pre-declared
    prediction allowed for.
    report: `results/p18/species_transport/summary.md`
  * **P18.1 capability gained:** one authoritative, bounded, deterministic multi-species
    mass-fraction state — `cfd::species::SpeciesRegistry` (ordered identity, bijective
    name↔index) and `cfd::species::MassFractionState` (`Y_i` per species per cell, with
    `0 ≤ Y_i ≤ 1`, `|ΣY_i − 1| ≤ 4·eps·N` per cell, and finiteness *enforced*, never repaired).
    Valid by construction; the only mutator is per-cell atomic; normalisation exists but is never
    implicit. **G2 PASS**, all four criteria. Strictly additive — no pre-existing production or
    test file was modified, so the pre-P18 passive-scalar stack is unchanged.
  * **P18.1 limits:** the state is **not yet reachable from a case file**, so no
    configuration→state→serialization round trip is claimed. No transport, no mixture properties,
    no chemistry, no GPU path — those remain P18.2–P18.8. A shipped species field is a passive
    *tracer*, not a composition, and is correctly rejected as a one-species mixture; how a case
    declares a carrier species is **P18.2's decision**.
  * P18.1 regression: Release+CUDA 2772 P / 0 F, CPU-only 2553 P / 0 F, Debug+GUI 2831 P / 3 F.
    The Debug+GUI set is one *fewer* inherited timeout than predicted; attributed to
    `DriverSeegmiller.MeshSequence` landing at 7123 s under an unchanged 7200 s cap, on a run
    4.0 % faster overall. **No new failure in any configuration.** The known limitation is refined:
    3 reliable timeouts plus 1 marginal test, not 4 reliable ones.
  * report: `results/p18/multi_species_state/summary.md`
  * previous step: **P18.0 — Baseline & Audit** (COMPLETE)
  * P18.0 changed no production code. The start fingerprint
    `60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318` is unchanged at phase end.
  * baseline reproduces P17.11 exactly in all three configurations: Release+CUDA 2712/0,
    CPU-only 2493/0, Debug+GUI 2770/4 -- the same 4 inherited pre-P17 timeouts, test id for test id.
  * audit headline: the existing species capability is a **shipped, production-reachable passive
    scalar** solving `div(rho U Y) - div(rho D grad Y) = S`, steady, constant density, one constant
    D per species. It is **not** a mass-fraction model: `sum(Y_i) = 1`, bounds, mixture properties,
    reactions and any GPU species path do not exist. 6 IMPLEMENTED / 8 PARTIAL / 10 MISSING.
  * one confirmed defect in shipped code: the species Dirichlet-gradient boundary is **lagged**,
    costing 335 outer iterations where thermal's exact treatment takes 2. P14 carries the same lag,
    so it cannot be fixed by migration alone.
  * verification gates for all of P18 are **frozen** at `results/p18/gates/P18_GATES.md`
    (sha256 `921616544e3631d4...`). G4(c) is the only criterion that fails on existing code.
  * next step: **P18.8 — CPU / GPU Equivalence. NOT AUTHORIZED YET.**
  * report: `results/p18/P18_0_SUMMARY.md`, audit evidence: `results/p18/audit/`

## Prior phase, closed as PARTIAL

* **P17 — Compressible Flow** — QUALIFICATION **PARTIAL**, not reopened by P18.0

  * last completed step: **P17.11 — Final Qualification** (run; result PARTIAL)
  * implementation: COMPLETE (P17.1–P17.10)
  * qualification: 8 of 10 gates PASS. **G6 PARTIAL** (HLLC star-density positivity fails for
    admissible head-on input) and **G10 PARTIAL** (Debug+GUI has 4 timeouts on pre-P17 tests).
  * next step: **none authorized.** Closing G6 needs a wave-speed-estimator change with its own
    frozen gate; closing G10 needs a decision about the Debug timeout contract. Both are outside
    a qualification phase's remit.
  * report: `results/p17/FINAL_QUALIFICATION.md` · limits: `results/p17/KNOWN_LIMITATIONS.md`

## Future phases

* **P19 — Multiphase** — NOT AUTHORIZED
* **P20 — Advanced SST Near-Wall Treatment** — NOT AUTHORIZED

---

# 3. Qualified Baseline

The most recent final P13 qualification recorded:

```text
Release+CUDA   2488 / 2488   PASS
Debug+GUI      2550 / 2550   PASS
CPU-only       2288 / 2288   PASS
```

Qualified source state:

```text
HEAD                   3bd32f918656
Working tree           DIRTY
SOURCE_FINGERPRINT_V2  721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955
```

Detailed qualification evidence:

```text
results/p13/final-qualification/
```

P17.0 must establish its own frozen starting fingerprint before new P17 implementation begins.

---

# 4. Current Capability

| Area                | Current capability                                                                                                   | Main remaining limitation                                                         |
| ------------------- | -------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| Incompressible flow | Steady SIMPLE, transient PISO, validated 2D foundation                                                               | Advanced numerics and broader 3D parity remain                                    |
| Linear solvers      | CG, BiCGSTAB, GMRES; CPU and qualified GPU paths                                                                     | Multigrid and stronger preconditioners remain                                     |
| Generic transport   | Reusable convection, diffusion, transient and source infrastructure                                                  | Must not be forced onto equations with incompatible conservative semantics        |
| Thermal / energy    | Temperature-dependent `k(T)` / `cp(T)`, enthalpy relations, Robin BCs, transient thermal support                     | Full compressible total-energy coupling is P17                                    |
| Turbulence          | Eddy-viscosity framework, k-epsilon, wall treatment / y+, validated CPU/GPU scope                                    | Advanced SST near-wall and separated-flow validation deferred to P20              |
| Compressible        | Variable-density foundation, EOS, compressible continuity/momentum, coupled compressible SIMPLE, low-Mach validation | Total energy, higher-Mach density-based solver, Riemann fluxes, shocks            |
| Species             | Reacting species: transport, mixture thermodynamics, Arrhenius chemistry, heat release, CPU and GPU (library-level)   | Not case-file reachable; no reacting-flow solve (G8 open); constant cp on device  |
| Multiphase          | Volume-fraction and mixture-property foundation                                                                      | Interface compression, surface tension, curvature and full validation are P19     |
| Meshes              | Structured 2D production meshes; Cartesian 3D foundation; ALE/GCL foundation                                         | General 3D geometry and production moving-mesh integration remain                 |
| CUDA                | Qualified GPU solver/discretization infrastructure and resident scalar capabilities                                  | Native Windows CUDA remains unqualified; some future physics still need GPU ports |
| Application         | CLI and Qt/QML GUI on shared production infrastructure                                                               | New future capabilities must be integrated deliberately                           |
| Restart             | Existing steady solver checkpointing foundation                                                                      | Turbulence checkpointing remains unsupported                                      |

---

# 5. Development Principles

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

The binding execution rules remain in `CLAUDE.md`.

---

# 6. Completed Development Lineage

## P0–P12 — Core CFD — COMPLETE

Established the CFDApp foundation, including:

* finite-volume discretization;
* structured meshes;
* steady SIMPLE;
* transient PISO;
* thermal transport;
* turbulence foundations;
* species and multiphase foundations;
* compressible-flow foundations;
* numerical robustness;
* production mesh work;
* 3D Cartesian foundations;
* ALE / moving-mesh foundations;
* CLI / GUI integration;
* validation and regression infrastructure.

Important P12 compressible foundation includes:

* scalar / face density infrastructure;
* ideal-gas EOS support;
* coupled `CompressibleSIMPLE`;
* density as an iterated state;
* compressible continuity;
* compressible momentum;
* low-Mach limiting behaviour;
* Arkilic-style low-Mach compressible validation.

These capabilities are inputs to P17 and must be reused where valid.

---

# 7. P13 — Production Maturity — COMPLETE

P13 closed the production-maturity and final-qualification line.

Major outcomes include:

* restart / checkpoint infrastructure;
* robust case validation;
* CLI / GUI consistency;
* results / export verification;
* production hardening;
* qualification evidence;
* regression preservation;
* CPU / CUDA qualification evidence;
* deterministic and compatibility checks;
* formal source fingerprinting.

P13 establishes the qualification discipline that later phases must follow.

Evidence root:

```text
results/p13/
```

---

# 8. P14 — Generic Transport Foundation — COMPLETE

P14 established reusable scalar-transport infrastructure rather than creating additional duplicated assemblers.

Foundation includes reusable support for:

* convection;
* diffusion;
* transient terms;
* source terms;
* boundary semantics;
* CPU scalar transport;
* GPU scalar transport;
* imposed-cell handling;
* under-relaxation semantics;
* resident scalar solves where supported.

P17 should reuse P14 infrastructure only where the mathematical semantics are compatible.

Conservative Euler / total-energy fluxes must not be forced into generic scalar transport merely to maximize code reuse.

---

# 9. P15 — Thermal / Energy — COMPLETE

P15 expanded thermal and thermodynamic infrastructure.

Delivered foundation includes:

* temperature-dependent thermal conductivity `k(T)`;
* temperature-dependent heat capacity `cp(T)`;
* closed-form enthalpy / energy relationships;
* inverse temperature recovery;
* Robin thermal boundary conditions;
* transient thermal terms;
* thermal validation;
* CPU/GPU preservation within the adopted scope.

P15 is a direct dependency of P17 thermodynamic closure and compressible energy work.

P17 must determine precisely which P15 energy abstractions can be reused for a conservative total-energy formulation.

---

# 10. P16 — Turbulence — COMPLETE WITH DEFERRED ADVANCED SST SCOPE

Completed within the adopted P16 scope:

* eddy-viscosity framework;
* k-epsilon;
* wall functions / y+ treatment;
* turbulence benchmark suite;
* CPU/GPU equivalence;
* GPU turbulence transport infrastructure;
* deterministic turbulence comparisons;
* resident scalar transport support.

Deferred to P20:

* fully wall-resolved SST at `y+ ≈ 1`;
* automatic low/high-y+ treatment;
* stronger separated-flow validation;
* Driver & Seegmiller benchmark closure;
* adverse-pressure-gradient validation.

Do not restart the abandoned P16 automatic-wall approach inside P17.

Evidence root:

```text
results/p16/
```

---

# 11. P17 — Compressible Flow — ACTIVE

> **Current development phase**

P17 extends the existing low-Mach pressure-based compressible foundation into a validated higher-Mach, shock-capable compressible solver while preserving existing SIMPLE behaviour.

The intended end state is:

* verified variable-density thermodynamics;
* ideal-gas closure;
* conservative compressible continuity;
* conservative compressible momentum;
* conservative total-energy transport;
* low-Mach limiting behaviour;
* Riemann-based inviscid fluxes;
* HLLC capability;
* a density-based compressible solver;
* shock-containing benchmark solutions;
* CPU/GPU numerical equivalence where GPU support is claimed;
* full regression and final qualification.

## P17 dependency order

```text
P17.0  Baseline & capability audit       complete
  ↓
P17.1  Variable density                 complete
  ↓
P17.2  Ideal-gas EOS                    complete
  ↓
P17.3  Compressible continuity          complete
  ↓
P17.4  Compressible momentum            complete
  ↓
P17.5  Total-energy equation            complete
  ↓
P17.6  Low-Mach validation              complete
  ↓
P17.7  HLLC / Riemann flux              complete
  ↓
P17.8  Density-based solver             complete
  ↓
P17.9  Shock validation                 complete
  ↓
P17.10 CPU / GPU equivalence            complete
  ↓
P17.11 Final qualification              RUN -- result PARTIAL   ← the last finished step
```

```text
P17 IS NOT COMPLETE. Its implementation is; its qualification is not. Two gates did not close:

  G6  PARTIAL   a REAL numerical defect. HLLC's wave-speed estimate does not bound a strong
                symmetric head-on collision, so star densities go negative for admissible input
                above a wall-normal Mach of ~1.76. The solver refuses loudly rather than answering
                wrongly; a direct caller of hllcFlux() does not.
                results/p17/qualification/G6_POSITIVITY_FAILURE.md
  G10 PARTIAL   a CONFIGURATION limitation. Four pre-P17 backward-facing-step tests cannot finish
                under Debug inside the frozen 7200 s timeout. They pass in both Release
                configurations and produced no wrong answer, only no answer in time. The timeout
                was not raised.
                results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md

Everything else passed on the final integrated tree, re-measured rather than inherited: SIMPLE
preserved BITWISE, P14/P15/P16 preserved, the compressible canonical and shock suites, CPU/GPU
equivalence bitwise at every level, determinism, and clean CUDA and host sanitizers.
```

```text
WHAT P17.11 INHERITED THAT WAS NOT A PASS, AND STILL IS NOT. P17.10 found that Toro's
pressure-based wave-speed estimate does not bound a strong symmetric head-on collision: above a
normal Mach number of ~1.76 the wave ordering inverts, the star densities go negative, and HLLC
returns the undisturbed upstream flux while still reporting valid = true.

P17.11 confirmed this maps onto frozen criterion G6(e) -- "rho and p remain strictly positive for
admissible input, a boolean, no tolerance" -- and left G6 at PARTIAL. No amendment was proposed:
the criterion is correct and the implementation does not meet it, so amending it would have been
weakening a valid threshold. In fairness to P17.7, its own summary had already disclaimed universal
signal bounding as unproven; what it over-claimed was one word, describing star-state admissibility
as "proven" when the proof covers only the denominator bound.
results/p17/cpu_gpu_equivalence/BLOCKING_FINDINGS.md ·
results/p17/qualification/G6_POSITIVITY_FAILURE.md.
```

## P17.9 — capability gained, and its limits

**Gained:** quantitative evidence that the density-based HLLC solver captures shocks correctly, not
merely plausibly. Sod's three structures are located by measurement against an independent 60-digit
exact Riemann solution; the M1 = 2 normal shock reproduces every analytical jump ratio to 1.1e-04
and satisfies Rankine–Hugoniot to 1.8e-04; a stationary contact is preserved *exactly*; compression
and expansion waves match their star states to 4e-03; mass and energy are conserved to round-off;
and the whole set is bitwise reproducible.

**P17.9 also found its own gate vacuous and had it amended.** HLL — HLLC with the contact wave
deleted — passed the entire shock-validation gate, which by G7's own text means the gate was not
measuring what HLLC is for. Three authorised amendments fixed it: one strengthening (a stationary
contact must be preserved with exactly zero smeared cells), one dimensional correction (the
rarefaction head converges like √dx, not dx), and one contamination removal (a contact's tail
corrupts a density plateau but not a pressure one). No acceptance bound was loosened, and the frozen
gate document is unmoved.

```text
1D only            every case is a 1D problem on a 2D mesh. No oblique shock, no shock reflection,
                   no shock-boundary-layer interaction.
Euler only         the shock layer captured here is NUMERICAL, not physical. No viscous structure.
first order        §34 requires the first-order baseline to qualify first, and it does. No
                   higher-order scheme was used to mask a first-order failure.
no order claim     the L1 rates are reported (0.66 / 0.83 / 0.75). There is no Richardson
                   extrapolation and no GCI; a discontinuous solution has no single rate to assert.
no GPU             G8 is NOT APPLICABLE, not deferred: P17.8 built no GPU density-based solver.
supersonic BCs     subsonic open boundaries remain unimplemented; every case is built around that.
```

## P17.8 — capability gained, and its limits

**Gained:** a density-based compressible solver that evolves the conserved state
`Q = [ρ, ρu, ρv, ρw, ρE]` directly using the verified HLLC flux, with explicit CFL control, a
deterministic retry policy that never clamps, five per-equation residual norms, a multi-signal
convergence test, and both global-physical and local-pseudo-time stepping. It conserves mass and
energy to round-off in a closed box, balances momentum against the integrated wall force, and
reproduces bitwise on repeat.

**Limits, all decided before implementation and none of them discovered late:**

```text
Euler only          no viscous stress, no heat conduction. Compressible Navier-Stokes is NOT
                    provided. The seam for adding them is defined, with NO zero stub.
library level       no dispatch change and no case-format key, so a case file cannot select this
                    solver. That is also what makes SIMPLE preservation structural rather than
                    merely measured. No production capability is claimed.
host only           no GPU density-based solver. The FLUX is already bitwise-identical on CPU and
                    GPU; the residual assembly, CFL reduction and update are not ported.
restart unsupported and it refuses explicitly, naming the fields the format cannot carry.
first order         piecewise-constant reconstruction, single-stage explicit Euler. Higher order
                    can be added without touching the residual or the flux.
three boundaries    inviscid wall, supersonic inflow, supersonic outflow. Subsonic open boundaries
                    need incoming-characteristic reconstruction and are not implemented.
low Mach            the explicit step saturates at the acoustic limit as Ma -> 0, so the solver is
                    stiff there. CompressibleSIMPLE remains the low-Mach reference.
```

**It also corrected a defect in P17.7.** The HLLC star-energy had a sign error, present in both the
C++ and P17.7's independent reference. An inviscid wall is the cheapest configuration that exposes
it, and P17.7 had no boundaries — so the defect sat exactly in the gap between the two phases'
scopes. Shock capability is **not** claimed on that basis: P17.9 owns validation.

## P17.0 — Baseline & Capability Audit — COMPLETE

Before writing new physics, P17 must determine exactly what already exists.

P17.0 must:

* freeze the P17 starting source fingerprint;
* record repository and working-tree state;
* run baseline CPU regression;
* run baseline CUDA regression;
* inventory existing compressible classes, APIs, runtime paths and tests;
* classify each P17 requirement as `IMPLEMENTED`, `PARTIAL`, or `MISSING`;
* map reuse of P14 and P15 infrastructure;
* define objective P17 gates under `results/p17/`.

Known existing foundation to audit:

* compressible SIMPLE;
* scalar density field;
* ideal-gas EOS;
* thermodynamic-property infrastructure;
* compressible continuity;
* compressible momentum;
* low-Mach validation.

Do not duplicate working functionality.

## P17 architecture direction

The existing pressure-based solver is preserved.

```text
Existing path
Pressure-based CompressibleSIMPLE
        │
        └── retained for low-Mach / pressure-based operation

New P17 path
Conserved state
[rho, rho*u, rho*v, rho*w, rho*E]
        ↓
reconstruction
        ↓
Riemann / HLLC face flux
        ↓
finite-volume residual
        ↓
density-based update
        ↓
EOS / primitive recovery
        ↓
shock-capable compressible solution
```

P17 must add the new density-based architecture without replacing the established SIMPLE backend.

## P17 validation direction

At minimum, P17 should ultimately cover:

* low-Mach recovery;
* EOS closure;
* mass conservation;
* momentum conservation;
* total-energy conservation;
* contact-wave behaviour;
* Sod shock tube;
* normal shock;
* supersonic transport / advection;
* shock position and state-ratio checks;
* deterministic repeats;
* CPU/GPU equivalence;
* complete regression.

Plots alone are not sufficient validation evidence.

Evidence root:

```text
results/p17/
```

---

# 12. P18 — Species / Reactions — implementation COMPLETE, qualification PARTIAL (G8 not closed)

P18 builds reacting-flow capability on top of the generic transport, thermal, and eventually compressible foundations.

Planned scope:

* passive-scalar preservation;
* multi-species transport;
* species-dependent diffusion;
* mixture properties;
* reaction source terms;
* Arrhenius chemistry;
* heat-release coupling;
* species / elemental conservation;
* thermal-chemistry coupling;
* canonical reacting-flow validation;
* CPU/GPU equivalence where adopted.

Existing non-reacting species support must be audited before new implementation begins.
**That audit is P18.0, and it is COMPLETE** -- `results/p18/P18_0_SUMMARY.md`.

What the audit established, which constrains the scope above:

* the existing species capability is production-reachable and **shipped** (case -> parser ->
  builder -> ProjectRunner -> export, two case directories). "Passive-scalar preservation" is
  therefore a backward-compatibility obligation on a user-visible feature, not a nice-to-have;
* the equation actually assembled is `div(rho U Y) - div(rho D grad Y) = S`: steady, constant
  density, one constant D per species, one uniform source, passive. There is no transient term to
  reuse;
* "multi-species transport" already exists as N independent scalars driven by a loop. What is
  missing is the **collection**: stable identity, `sum(Y_i) = 1`, and a bounds policy;
* P14's generic transport already supplies the transient, the per-cell `Su`/`Sp` source pair and
  the per-cell diffusivity that P18 needs. P18 should **collapse the two stacks**, not add a third;
* a real defect exists in shipped code: the species gradient boundary is lagged (335 outer
  iterations against thermal's 2). It is separable from the rest of P18;
* there is **no GPU species path at all**, so P18.8 is an implementation phase, as P17.10 was.

P18's verification gates are frozen for the whole phase at `results/p18/gates/P18_GATES.md`.

Each subsequent P18 step still requires its own explicit authorization.

---

# 13. P19 — Multiphase — NOT AUTHORIZED

P19 extends the existing volume-fraction foundation toward validated interface-capturing multiphase flow.

Planned scope:

* VOF volume fraction;
* bounded alpha transport;
* density / viscosity mixture laws;
* interface compression;
* surface tension;
* curvature;
* gravity;
* static droplet validation;
* interface-advection validation;
* dam-break validation;
* conservation checks;
* CPU/GPU equivalence where adopted.

Do not claim full multiphase CFD from the current volume-fraction foundation.

P19 does not begin until explicitly authorized.

---

# 14. P20 — Advanced SST Near-Wall Treatment — NOT AUTHORIZED

P20 contains the turbulence scope intentionally deferred from P16.

Planned scope:

* `y+ ≈ 1` low-Re SST mesh family;
* wall-resolved SST;
* automatic low/high-y+ treatment;
* separation / reattachment validation;
* Driver & Seegmiller benchmark;
* adverse-pressure-gradient benchmark;
* CPU/GPU equivalence.

Do not restart the P16 automatic-wall attempts without a newly defined P20 architecture and gate set.

---

# 15. P21 — Advanced Numerics — PLANNED

Future numerical infrastructure may include:

* Rhie–Chow as the appropriate default where justified;
* multigrid;
* stronger preconditioners;
* coupled pressure-based solution;
* improved Krylov reductions;
* higher-order reconstruction for compressible flow;
* broader irregular-mesh robustness.

These are not automatically part of P17 unless a scoped P17 blocker genuinely requires them.

## P21 sub-phases, when authorized

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

**Capability gained:** higher formal accuracy on the same meshes, and a solver framework that can
be chosen by matrix class rather than by default.

**Limit:** the validated first-order paths must survive unchanged. A second-order scheme that
replaces rather than supplements them is a regression, not an improvement.


---

# 16. P22 — 3D Physics Parity — PLANNED

Long-term 3D scope:

* transient flow;
* thermal;
* turbulence;
* species;
* compressible flow;
* non-Cartesian geometry;
* broader production integration;
* parallel execution.

The existing Cartesian 3D foundation should be extended incrementally rather than bypassed.

## P22 sub-phases, when authorized

```text
P22.0  3D core audit              mesh topology · face orientation · vector/tensor handling ·
                                  boundary geometry · volume calculations
P22.1  3D incompressible          continuity · momentum · pressure correction · canonical validation
P22.2  3D thermal                 conduction · convection · thermal boundaries · conjugate-ready
                                  interfaces if scoped
P22.3  3D turbulence              k-epsilon · SST · wall distance · y+ · canonical validation
P22.4  3D compressible            EOS · continuity · momentum · total energy · fluxes ·
                                  canonical 3D test
P22.5  3D species / reactions     species transport · diffusion closure · chemistry · heat
                                  release · conservation
P22.6  3D multiphase              VOF · interface compression · surface tension · gravity
P22.7  CPU/GPU parity             all qualified 3D physics · deterministic comparisons ·
                                  memory/residency evidence
P22.8  Final qualification        full 3D regression · conservation · benchmark matrix · fingerprint
```

**Capability gained:** the 2D physics stack usable in 3D with equivalent evidence.

**Limit:** 3D parity is claimed per physics, never as a blanket statement. Each sub-phase carries
its own canonical validation.


---

# 17. P23 — Moving Mesh Production — PLANNED

The ALE / GCL foundation already exists at library level.

Future production scope:

* case-format integration;
* CLI integration;
* GUI integration;
* production 3D ALE;
* moving-mesh MMS;
* restart / checkpoint support;
* interaction with turbulence, thermal and compressible physics.

Do not silently convert library-level ALE support into a claimed production capability.

## P23 sub-phases, when authorized

```text
P23.0  Architecture & baseline    audit existing moving-mesh code · define ALE formulation ·
                                  freeze conservation requirements
P23.1  Mesh motion                prescribed rigid motion · translation · rotation · deformation
P23.2  Geometric conservation     GCL formulation · volume change · face swept volume ·
                                  uniform-flow preservation
P23.3  ALE transport              continuity · momentum · scalar transport · thermal · species
P23.4  Moving boundaries          moving wall · no-slip consistency · force extraction ·
                                  conservation
P23.5  Mesh quality / failure     quality metrics · inversion guards · timestep restrictions ·
                                  failure diagnostics
P23.6  Validation                 moving piston · translating body · oscillating body · GCL
                                  benchmark · grid and time-step sensitivity
P23.7  CPU/GPU equivalence        geometry update · ALE flux · fields · conservation
P23.8  Final qualification        full regression · moving-mesh canonical suite · fingerprint
```

**Capability gained:** conservative mesh motion reachable from a case file.

**Limit:** uniform-flow preservation under motion (P23.2) is the gate that decides whether any of
the rest can be claimed. Without the GCL satisfied discretely, moving-mesh results are not
conservative whatever the benchmarks show.


---

# 18. P24 — HPC & Distributed Scaling — PLANNED

Long-term scaling direction:

* OpenMP scaling;
* MPI domain decomposition;
* multi-GPU;
* larger-grid optimization;
* reduced host/device synchronization;
* scalable 3D execution;
* reproducible performance baselines.

Performance work must remain secondary to numerical correctness.

## P24 sub-phases, when authorized

```text
P24.0  HPC baseline               profile CPU and GPU · memory model · communication model ·
                                  freeze single-device reference
P24.1  Domain decomposition       partition mesh · owner/ghost cells · halo exchange ·
                                  partition boundaries
P24.2  MPI                        distributed scalar and vector fields · global reductions ·
                                  convergence synchronization
P24.3  Distributed linear solvers distributed assembly · preconditioning · convergence ·
                                  partition-independent results
P24.4  Multi-GPU                  device partitioning · halo transfers · peer transfer and
                                  GPU-aware MPI where supported
P24.5  Comm/compute overlap       asynchronous halo exchange · interior/boundary split ·
                                  transfer overlap · profiling
P24.6  Scaling                    strong and weak scaling · CPU and GPU scaling · efficiency ·
                                  memory scaling
P24.7  Reproducibility            partition-count comparison · deterministic reductions where
                                  required · conservation · physics-equivalent solves
P24.8  Final qualification        multi-node regression · CPU/GPU/HPC comparison · scaling
                                  report · fingerprint
```

**Capability gained:** the same mathematics on more hardware.

**Limit:** P24.7 is the non-negotiable one — a result that depends on the partition count is not a
scaled solve, it is a different solve. Scaling numbers mean nothing until it passes.

---

# 19. P25 — Production Workflow & Reliability — PLANNED

Make qualified physics practical for sustained production use. This phase adds no physics; it makes
the physics already qualified survivable in real use.

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

**Limit:** a restart that is not bitwise-or-documented-equivalent to an uninterrupted run is not a
restart. P25.2 and P25.7 are one requirement split across two sub-phases.

---

# 20. P26 — Verification & Validation Expansion — PLANNED

A broad, traceable benchmark matrix supporting credible engineering use.

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

**Limit:** every reference needs provenance frozen before measurement, and every comparison needs a
negative control. A validation suite without those is a collection of agreements, not evidence.

---

# 21. P27 — Performance & Release Hardening — PLANNED

Optimize **only after** numerical equivalence and production reliability are established.

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

**Limit:** a change that alters floating-point association is a numerics change, not a performance
change, and earns the full numerical ladder. Reordering a reduction, fusing a kernel, switching to
atomics or enabling FMA contraction all qualify.

---

# 22. P28 — v1.0 Final Qualification — PLANNED

Freeze and qualify the complete production scope.

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

## P28.9 — the release decision

Declare `CFDApp v1.0 — QUALIFIED` **only** if every mandatory v1.0 gate passes. Otherwise
`v1.0 — PARTIAL / BLOCKED`, with explicit blockers named. P18's current state is the worked example:
ten of eleven gates pass and the phase is still PARTIAL, because G8 does not.

---

# 23. Technical Debt

The authoritative register is [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md); `TODO.md` carries the
current blocker only.

Examples include:

## Numerics

* CG absolute breakdown threshold;
* 2D Linear face-flux pressure mode;
* Green–Gauss four-sweep limitation;
* fine-grid instability;
* two-cell `diffusion()` defect;
* irregular-mesh convergence;
* warped 3D faces;
* large-coordinate geometry;
* sheared-mesh asymmetry;
* cross-patch well-posedness validation.

## Physics / Solver

* species boundary lag;
* turbulence checkpoint support;
* checkpoint segment setup overhead.

## GPU / Performance

* GPU Krylov reduction overhead;
* 3D BC lookup performance;
* benchmark raw-CSV path;
* `enableGpuDiscretization` case-file key.

## Platform / Tooling

* GUI `-Wconversion` warnings;
* native Windows CUDA qualification.

Technical debt must not be folded into an unrelated phase merely because it is encountered.

---

# 24. Current Known Limitations

Current known limitations include:

* advanced SST validation deferred to P20;
* native Windows CUDA not qualified;
* native Windows packaging not qualified;
* DEB / RPM / macOS packaging not qualified;
* turbulence checkpointing unsupported;
* `/dev/full` writer test is POSIX-only;
* benchmark regression detection remains insensitive below the existing threshold;
* no portable timing baseline exists across arbitrary hardware.

P16 sanitizer scope remains recorded as:

```text
44 / 44 exercised CUDA kernels covered
114 / 196 tests sanitized
synccheck negative-control detection unproven
```

These limitations remain explicit and do not automatically become P17 work.

---

# 25. v1.0 Direction

A v1.0 release should represent more than feature accumulation.

Expected maturity gates include:

* benchmark / V&V library;
* stable API boundaries;
* stable case format and migration policy;
* cross-platform qualification;
* reproducible CPU/GPU validation;
* robust diagnostics;
* documentation;
* packaging;
* installer qualification;
* release-candidate qualification;
* preserved known limitations.

Potential phase sequence:

```text
P17 Compressible Flow
        ↓
P18 / P19 / P20
        ↓
Advanced Numerics
        ↓
3D Physics Parity
        ↓
Moving Mesh Production
        ↓
HPC
        ↓
v1.0 Qualification
        ↓
v1.0 Release
```

The exact order of P18–P20 may be changed by explicit authorization, but no phase should silently begin.

---

# 26. Explicit Non-Goals / Guardrails

* **No duplicate physics implementations** when an existing implementation can be verified and reused.
* **No replacement of SIMPLE solely to add a density-based compressible solver.**
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

# 27. Roadmap Execution State

```text
P0–P12   Core CFD                         COMPLETE
   ↓
P13      Production Maturity              COMPLETE
   ↓
P14      Generic Transport Foundation     COMPLETE
   ↓
P15      Thermal / Energy                 COMPLETE
   ↓
P16      Turbulence                       COMPLETE
   ↓
P17      Compressible Flow                PARTIAL (qualified within documented limits)
   ↓
P18      Species / Reactions              ACTIVE
          ├─ P18.0 Baseline & Audit       COMPLETE
          └─ P18.1 Multi-Species State    NOT AUTHORIZED YET
P19      Multiphase                       NOT AUTHORIZED
P20      Advanced SST                     NOT AUTHORIZED
   ↓
Advanced Numerics                         PLANNED
3D Physics Parity                         PLANNED
Moving Mesh Production                    PLANNED
HPC                                       PLANNED
   ↓
v1.0                                     PLANNED
```

---

# 28. Immediate Direction

The current project direction is unambiguous:

> **Complete and verify P17.0 before implementing new compressible physics.**

P17.0 determines which parts of variable density, EOS, compressible continuity, compressible momentum, low-Mach validation, total energy, Riemann fluxes, shock validation, density-based solution, and GPU support genuinely remain to be built.

Only after that audit is complete should `TODO.md` authorize the next concrete P17 implementation step.
