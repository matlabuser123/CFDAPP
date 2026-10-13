# CFDApp — Roadmap

**Strategic direction.** What CFDApp is becoming, in dependency order. This file changes slowly and
only when direction, phase scope or the v1.0 definition changes — never because a checkbox moved.
Live status is [TODO.md](TODO.md), which is authoritative for it; debt is
[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md); rules are [CLAUDE.md](CLAUDE.md); evidence is under
`results/`. Pre-rewrite copy: `results/doc-archive/ROADMAP-2026-10-12-pre-rewrite.md`.

## Current Position

A validated 2D finite-volume CFD platform: incompressible SIMPLE/PISO, generic scalar transport,
thermal/energy, turbulence, compressible flow and multi-species reacting flow, on one solver backend
shared by a CLI and a Qt/QML GUI, with CPU/OpenMP/CUDA paths and preserved evidence per phase.

Maturity is **pre-v1.0**. Foundations P0–P16 are complete. P17 (compressible) and P18 (species /
reactions) are implemented but not fully qualified: P18's G8 — canonical reacting validation — is
the single blocking gate, and P17 carries two inherited qualification defects. No implementation
phase is authorized; the active work is technical-debt resolution.

A capability counts as mature here only when it is numerically verified, physically validated,
reachable through a production case-file path rather than a library API, regression tested,
CPU/GPU equivalent where a GPU is claimed, and backed by evidence under `results/`.
**Correctness takes priority over performance.**

## Development Phases

| Phase | Objective | Key Deliverable | Status |
|-------|-----------|-----------------|--------|
| P0–P12 | Core CFD platform | FV discretization, structured meshes, steady SIMPLE, transient PISO, 3D Cartesian and ALE foundations, compressible foundation | COMPLETE |
| P13 | Production maturity | Restart/checkpoint, case validation, CLI/GUI consistency, qualification discipline every later phase follows | COMPLETE |
| P14 | Generic transport foundation | One reusable convection/diffusion/transient/source stack, so later phases collapse onto it | COMPLETE |
| P15 | Thermal / energy | `k(T)`, `cp(T)`, sensible enthalpy, thermal BCs, transient thermal | COMPLETE |
| P16 | Turbulence | Eddy-viscosity framework, k-ε, wall treatment / y+, CPU/GPU within scope | COMPLETE |
| P17 | Compressible flow | Variable density, ideal-gas EOS, total energy, HLLC, density-based solver inside a documented envelope — SIMPLE not replaced | **PARTIAL** — G6, G10 open |
| P18 | Species / reactions | Multi-species state with `ΣY=1`, conservative `ρY` transport, mixture properties, Arrhenius chemistry, heat release, CPU/GPU | **PARTIAL** — G8 not closed |
| P19 | Multiphase | Bounded VOF, interface compression, surface tension/curvature, gravity, canonical validation | NOT AUTHORIZED |
| P20 | Advanced SST near-wall | Wall-resolved SST validated on separated flow — the scope deliberately deferred from P16 | NOT AUTHORIZED |
| P21 | Advanced numerics | Higher-order schemes and stronger solvers, preserving the validated first-order reference paths | PLANNED |
| P22 | 3D physics parity | The validated 2D physics stack working equivalently in 3D | PLANNED |
| P23 | Moving mesh production | ALE/GCL from library foundation to a conservative production path, integrated through case/CLI/GUI | PLANNED |
| P24 | HPC & distributed scaling | Qualified physics across multiple nodes and devices, numerics unchanged | PLANNED |
| P25 | Production workflow | Reliability, diagnostics and usability for sustained production use; no new physics | PLANNED |
| P26 | V&V expansion | A broad, traceable benchmark matrix supporting credible engineering use | PLANNED |
| P27 | Performance & release hardening | Optimization, only after equivalence and reliability are established | PLANNED |
| P28 | v1.0 final qualification | Freeze and qualify the complete production scope | PLANNED |

Milestones group these: **M1** P0–P12 · **M2** P13–P16 · **M3** P17+P18 · **M4** P19+P20 ·
**M5** P21+P22 · **M6** P23 · **M7** P24 · **M8** P25 · **M9** P26 · **M10** P27 · **M11** v1.0.

## Next Milestone

**M3 — Compressible + Reacting.**

- **Objective:** close P18's canonical reacting validation and address P17's remaining
  qualification, so the compressible and reacting capabilities are usable inside stated envelopes.
- **Exit Criteria:**
  - P18 G8 closed — a 1D laminar premixed flame whose flame speed falls inside the pre-frozen
    35.0–37.0 cm/s spread (Hermanns 2007), with G8's controls run and the gate unweakened;
  - P18 requalified end to end, G1–G11 all PASS, with a new fingerprint recorded;
  - P17's G6 and G10 resolved or their limits formally accepted and recorded;
  - every premixed-flame quantity measured before the stale-time-level fix re-measured under its
    own gate.

## Long-Term Direction

- **Physics breadth:** multiphase interface capturing, wall-resolved SST, reacting-shock capability
  once a variable-γ total energy exists.
- **Numerics:** higher-order convection and stronger linear solvers — multigrid and better
  preconditioners — without displacing the validated first-order reference.
- **Geometry:** general 3D parity and production moving-mesh integration.
- **Scale:** distributed CPU/GPU execution with numerics held invariant.
- **Release:** a V&V benchmark library, stable API and case-format boundaries with a migration
  policy, cross-platform qualification, packaging, and every known limitation stated explicitly.

## Guardrails

- No duplicate physics when an existing implementation can be verified and reused.
- No replacement of SIMPLE merely to add a density-based compressible solver.
- No full-multiphase claim from the volume-fraction foundation; no moving-mesh production claim
  before case/CLI/GUI integration is verified.
- No GPU-performance claim without equivalent numerical solutions.
- No threshold weakening to obtain PASS; no silent removal of failed evidence.
- No unrelated debt cleanup inside active physics work.
- **No phase begins without explicit authorization** — appearing in this file is not authorization.
