# CFDApp — TODO

> **This file owns:** project phases, phase status, the current blocker, execution order, and what
> is authorized next.
>
> **It does not own:** technical debt (see [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md)), measurements
> (see `results/`), or agent rules (see [CLAUDE.md](CLAUDE.md)). The same description must not
> appear in more than one of them.

## Legend

```text
[x] COMPLETE        implemented AND verified with evidence (CLAUDE.md §1.1)
[~] PARTIAL         implemented but not fully qualified; the reason is always stated
[ ] NOT AUTHORIZED  appearing here is not authorization (CLAUDE.md §2)
```

---

# 0. Project Goal

Build CFDApp into a production-grade finite-volume CFD platform with verified incompressible and
compressible flow, heat transfer, turbulence, species and reacting flow, multiphase flow, advanced
numerics, full 3D physics parity, moving meshes, CPU/GPU acceleration, distributed HPC, strong
deterministic verification, production case workflows, quantitative validation, and a final v1.0
qualification.

**Correctness and conservation come before performance.**

---

# 1. Current Project Status

## Complete

- [x] **P0–P12** — Core CFD
- [x] **P13** — Production Maturity
- [x] **P14** — Generic Transport Foundation
- [x] **P15** — Thermal / Energy
- [x] **P16** — Turbulence

## Partial

- [~] **P17** — Compressible Flow. Implementation complete; qualification partial. → §3
- [~] **P18** — Species / Reactions. Implementation substantially complete; final qualification
      partial because **G8 is NOT CLOSED**. → §4

## Future — Not Authorized

```text
P19  Multiphase                       P24  HPC & Distributed Scaling
P20  Advanced SST                     P25  Production Workflow & Reliability
P21  Advanced Numerics                P26  Verification & Validation Expansion
P22  3D Physics Parity                P27  Performance & Release Hardening
P23  Moving Mesh Production           P28  v1.0 Final Qualification
```

**No implementation phase is currently authorized.** The active work is technical-debt resolution
under a separate authorization.

---

# 2. Completed Foundation

## P0–P12 — Core CFD ← COMPLETE

Core finite-volume framework; production baseline established; core solver infrastructure qualified.
Historical details remain in project history and `results/`. **Do not reconstruct historical
sub-phases inside this file.**

## P13 — Production Maturity ← COMPLETE

Production qualification · CI / regression maturity · CPU/GPU qualification infrastructure ·
deterministic regression infrastructure · release evidence.

## P14 — Generic Transport Foundation ← COMPLETE

Generic scalar transport · transient transport support · convection · diffusion · source-term
infrastructure · variable coefficients · boundary-condition infrastructure · CPU/GPU transport
foundation.

Technical debt discovered later is tracked in `TECHNICAL_DEBT.md`.

## P15 — Thermal / Energy ← COMPLETE

Thermal transport · temperature-dependent material properties · sensible enthalpy · thermal boundary
conditions · transient thermal treatment · thermal validation · regression protection.

## P16 — Turbulence ← COMPLETE

Production turbulence capability within qualified scope · turbulence transport · wall treatment /
y+ · validation · CPU/GPU qualification within adopted scope.

Advanced SST work belongs to **P20**.

---

# 3. P17 — Compressible Flow ← PARTIAL

Implementation is complete within the existing architecture; qualification remains partial.

```text
results/p17/FINAL_QUALIFICATION.md
results/p17/KNOWN_LIMITATIONS.md          L1 (G6), L37 (G10)
```

## Qualified capability

Variable density · ideal-gas EOS · compressible continuity · compressible momentum · total-energy
equation · low-Mach validation · HLLC flux within qualified regime · density-based solver · shock
validation within qualified regime · CPU/GPU equivalence within qualified regime.

## Outstanding qualification

- [ ] **G6** — HLLC signal-speed / star-state positivity defect under strong head-on wall-normal
      flow. Clauses (a)(b)(c)(d)(f) pass; **(e) fails**. → TD-008
- [ ] **G10** — Debug+GUI `DriverSeegmiller*` timeout contract: 4 timeouts; Release+CUDA and
      CPU-only both PASS. → TD-009

**P17 remains usable only inside its documented qualified envelope.**

---

# 4. P18 — Species / Reactions ← PARTIAL

**Goal.** Conservative multi-species reacting-flow capability with mixture thermodynamics, kinetics,
heat release, quantitative conservation, validation and CPU/GPU equivalence.

```text
results/p18/FINAL_QUALIFICATION.md
results/p18/KNOWN_LIMITATIONS.md
```

| Sub-phase | Status | Principal result |
| --- | --- | --- |
| P18.0 Baseline & audit | [x] COMPLETE | fingerprint frozen, gates frozen |
| P18.1 Multi-species state | [x] COMPLETE | **G2 PASS** |
| P18.2 Species transport & diffusion | [x] COMPLETE | **G4 PASS** |
| P18.3 Mixture properties | [x] COMPLETE | **G5 PASS** |
| P18.4 Reaction source framework | [x] COMPLETE | mass-conserving `omega_i` |
| P18.5 Arrhenius chemistry | [x] COMPLETE | **G6 PASS** |
| P18.6 Heat-release coupling | [x] COMPLETE | **G7 PASS** |
| P18.7 Conservation & validation | [~] PARTIAL | **G8 NOT CLOSED** |
| P18.8 CPU / GPU equivalence | [x] COMPLETE | within the frozen P18.8 contract |
| P18.9 Final qualification | EXECUTED | **P18 OVERALL: PARTIAL** |

**P18.7 completed:** species mass conservation · `ΣY_i = 1` · energy conservation · zero-reaction
limit · zero-heat-release limit · batch-reactor validation · reacting plug-flow validation · grid
sensitivity · time-step sensitivity · deterministic repeatability. Elemental conservation is
**N/A** — elemental metadata is not modelled.

**P18.7 not closed:** frozen **G8** canonical premixed-flame validation. The existing plug-flow
benchmark does not replace G8.

**Do not claim reacting-shock capability from P18.6.**
**Do not downgrade P18.8** because additional future GPU capability is desirable.

## P18.9 final gate state

```text
G1 PASS   G2 PASS   G3 PASS   G4 PASS   G5 PASS   G6 PASS
G7 PASS   G8 NOT CLOSED   G9 PASS (scoped)   G10 PASS   G11 PASS
```

P18.9 execution is finished; the **outcome** is partial because G8 remains open.

---

# 5. Current Blocker — P18 G8

G8 requires a 1D laminar premixed flame whose flame speed lies inside a **cited published spread**,
frozen *before* measurement at `results/p18/g8_closure/g8_reference.md`:

```text
35.0 – 37.0 cm/s      Hermanns (2007), DOI 10.6100/IR630126
```

## The dependency graph is NO LONGER a straight line

It was `TD-001 → … → TD-007`. A root cause found on 2026-10-06 (**TD-032**) sits underneath several
items at once, so the order below reflects what is actually blocking what. Status, priority, root
cause and acceptance criteria for each: **[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md)**.

```text
TD-001  Reacting-flow production reachability      RESOLVED 2026-10-03
TD-002  Stiff / bounded chemistry integration      RESOLVED 2026-10-04
   ↓
TD-032  Transient time level ONE STEP STALE        OPEN / BLOCKED — fix retained
   ↓    root cause under TD-028 and TD-031; invalidates every field-derived flame number
   ├─ TD-027  Cell-diffusion cap                   REOPENED — C4 to be re-run
   ├─ TD-028  Period-2 alternation                 root-caused to TD-032
   ├─ TD-031  Final march length                   BLOCKED on TD-032
   ├─ TD-029  Error cancellation in 2 passing tests OPEN
   └─ TD-030  Reaction integral excess             OPEN
   ↓    carved out by TD-032, none authorized
   ├─ TD-033  a G8 test that cannot reach its body OPEN
   ├─ TD-034  clang-format not clean               OPEN — blocks EVERY commit
   └─ TD-035  full sanitizer run infeasible for G8 OPEN
   ↓
TD-003  Reacting-flow linear-solver robustness     OPEN — NOT AUTHORIZED
TD-004  1D premixed-flame capability               OPEN / STOPPED at gate C2-A1
TD-005  Flame-speed extraction                     OPEN / PARTIAL (C4d resolved)
   ↓
TD-006  Close P18 G8                               BLOCKED
TD-007  Re-run P18 final qualification             BLOCKED
   ↓
P18 COMPLETE only if G1–G11 PASS
```

## TD-032 — the current blocker

```text
status          OPEN / BLOCKED. Fix implemented, all ten frozen criteria PASS, fix RETAINED.
blocker         it caused a recorded PASS → FAIL on a G8 method-insensitivity test, attributed
                by a two-library comparison. Whether that is a lost capability or a false pass
                become truthful is OPEN.
next action     the discriminating probe named in the item's attribution record — NOT AUTHORIZED
authorization   TD-032 was authorized in full; it is stopped under CLAUDE.md §12, not finished
evidence        results/technical_debt/TD032_lagged_time_level/
```

**Consequence for the register:** every field-derived premixed-flame number recorded before
2026-10-06 was measured on an alternating field and must be re-measured under its own item's gate.
See [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md).

**SUPERSEDED decision:** the earlier "`TD-004 → TD-005` order is inverted because the blocker is
flame stabilisation" question is on hold — that classification rested on the pre-fix front position
and must be re-measured first.

## Required work to close G8

- Qualify the reacting-flow linear solver (TD-003)
- Re-measure the premixed-flame items on the fixed march
- Implement/confirm deterministic flame-speed extraction (TD-005)
- Run G8 controls and close G8 (TD-006)
- Re-run P18.9 and record a new final P18 fingerprint (TD-007)
- Mark P18 COMPLETE **only** if all mandatory gates pass

**No P18 gate may be weakened to accomplish this.**

---

# 6. Technical Debt

The authoritative register is **[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md)**. `TODO.md` does not
accumulate detailed debt descriptions.

Debt spans: P18 production reachability · stiff chemistry · reacting-flow solver robustness ·
premixed-flame/G8 support · **premixed-flame march time-level staging (TD-032)** · P17 HLLC
positivity · Debug timeout contract · gradient boundary debt · variable-`cp` total energy ·
reversible-chemistry thermodynamic consistency · GPU species transport · repository hygiene.

```text
results/p17/   results/p18/   results/technical_debt/
```

---

# 7. P19 — Multiphase ← PLANNED · NOT AUTHORIZED

**Goal.** Conservative interface-capturing multiphase capability without regressing single-phase CFD.

```text
P19.0  Baseline & architecture    audit transport infrastructure · define multiphase state ·
                                  freeze conservation requirements · freeze gates · freeze fingerprint
P19.1  Bounded VOF transport      alpha · conservative transport · 0 <= alpha <= 1 · mass
                                  conservation · sharp-interface tests · CPU reference numerics
P19.2  Interface compression      compression flux · boundedness · strength controls · static
                                  advection · rotating interface
P19.3  Surface tension/curvature  normals · curvature · CSF source · static droplet · parasitic
                                  currents · Laplace pressure
P19.4  Gravity                    body-force coupling · hydrostatic balance · density contrast ·
                                  well-balanced validation where applicable
P19.5  Canonical validation       dam break · rising/falling droplet · interface advection · grid
                                  and time-step sensitivity · mass conservation · determinism
P19.6  CPU/GPU equivalence        VOF · compression · surface tension · gravity · conservation ·
                                  CUDA diagnostics · residency evidence
P19.7  Final qualification        full regression · P14–P18 preservation · canonical suite ·
                                  conservation · determinism · equivalence · limitations · fingerprint
```

---

# 8. P20 — Advanced SST ← PLANNED · NOT AUTHORIZED

**Goal.** Extend existing turbulence to a fully qualified wall-resolved / automatic SST workflow.

```text
P20.0  Baseline & audit           freeze current SST behaviour · audit wall distance and wall
                                  treatment · freeze validation gates
P20.1  Wall-resolved SST          near-wall formulation · wall-distance consistency · limiting
                                  functions · production/destruction terms · boundary treatment
P20.2  Automatic low/high-y+      low-y+ branch · high-y+ branch · smooth transition · invalid y+
                                  guards · deterministic switching
P20.3  Separation/reattachment    separation · reattachment · wall shear · Cf · Cp
P20.4  Driver & Seegmiller        resolve timeout/robustness issues (→ G10, TD-009) · quantitative
                                  benchmark · grid sensitivity · convergence characterization
P20.5  APG benchmark              adverse pressure gradient · velocity profiles · wall shear ·
                                  separation response · published comparison
P20.6  GPU equivalence            SST fields · wall treatment · production/destruction · residuals ·
                                  determinism · CUDA diagnostics
P20.7  Final qualification        full regression · canonical turbulence suite · equivalence ·
                                  limitations · fingerprint
```

---

# 9. Future Phases P21–P28 ← PROPOSED · NOT AUTHORIZED

Sub-phase definitions for **P21** Advanced Numerics · **P22** 3D Physics Parity · **P23** Moving
Mesh Production · **P24** HPC & Distributed Scaling · **P25** Production Workflow & Reliability ·
**P26** Verification & Validation Expansion · **P27** Performance & Release Hardening · **P28**
v1.0 Final Qualification live in **[ROADMAP.md](ROADMAP.md) §15–§22**, which owns capability,
direction and dependency order (CLAUDE.md §10).

This file carries their authorization state only, in §1: **none of them is authorized.**

---

# 10. Major Milestones

```text
M1   Core Solver Foundation        P0–P12                COMPLETE
M2   Production CFD Foundation     P13–P16               COMPLETE
M3   Compressible + Reacting       P17 + P18             PARTIAL
M4   Multiphase + Advanced Turb.   P19 + P20             PLANNED
M5   High-Accuracy General CFD     P21 + P22             PLANNED
M6   Dynamic Geometry              P23                   PLANNED
M7   Scalable CFD                  P24                   PLANNED
M8   Production Workflow           P25                   PLANNED
M9   Broad Verification & Valid.   P26                   PLANNED
M10  Production Hardening          P27                   PLANNED
M11  CFDApp v1.0                   P28                   PLANNED
```

**M3 completion requires:** P17 remaining qualification addressed as required; P18 G8 closed; P18
fully requalified.

---

# 11. Execution Order

```text
P0–P16 COMPLETE
   ↓
P17 PARTIAL
   ↓
P18 PARTIAL — G8 NOT CLOSED
   ↓
P18 critical technical-debt work (see §5 — NOT a straight line since TD-032)
   TD-001 ✓ → TD-002 ✓ → TD-032 → {TD-027, TD-028, TD-029, TD-030, TD-031}
                                 → TD-003 → TD-004 → TD-005 → TD-006 → TD-007
   ↓
P18 requalification · P18 COMPLETE only if G1–G11 PASS
   ↓
P17 remaining qualification debt as dependency/priority requires
   ↓
P19 → P20 → P21 → P22 → P23 → P24 → P25 → P26 → P27 → P28
   ↓
CFDApp v1.0
```

**Future phases are roadmap items only. They require explicit authorization before implementation.**

---

# 12. Global Qualification Rules

These restate [CLAUDE.md](CLAUDE.md), which remains authoritative.

```text
 1  Audit before implementing.
 2  Freeze acceptance criteria before final measurements.
 3  Preserve the original failure before fixing a defect.
 4  Never weaken a valid threshold to obtain PASS.
 5  [x] means implemented AND verified.
 6  Preserve negative controls.
 7  CPU numerics remain the reference unless explicitly superseded.
 8  Conservation before performance.
 9  Numerical equivalence before speed claims.
10  Do not repair unrelated technical debt inside another phase.
11  Reuse existing infrastructure before creating a new subsystem.
12  Avoid duplicate transport, EOS, energy or chemistry implementations.
13  Reject invalid physical states explicitly; do not silently clamp.
14  Do not normalize conserved states merely to hide drift.
15  Implementation and qualification remain separate.
16  Every major phase gets a frozen baseline and a final fingerprint.
17  Every major phase records known limitations.
18  Every CPU/GPU phase records CUDA diagnostic scope.
19  Every performance comparison uses equivalent numerical solves.
20  Full regression follows production-code changes.
21  Detailed evidence belongs under results/.
22  Technical debt belongs in TECHNICAL_DEBT.md.
23  TODO.md contains current roadmap/status, not historical logs.
24  No commit or push without authorization.
25  No future phase begins without explicit authorization.
```

---

# 13. Evidence Layout

```text
results/
├── p13/ … p18/          one directory per qualified phase
├── p19/ … p28/          created ONLY when that phase is authorized
└── technical_debt/      one directory per debt item
```

---

# 14. Current Action

```text
PROJECT STATE          P17 PARTIAL · P18 PARTIAL
BLOCKING ISSUE         P18 G8 — canonical reacting validation not closed
ROADMAP AUTHORIZATION  NO NEW FUTURE PHASE AUTHORIZED
```

## Stopped — TD-032

```text
gate            all ten frozen criteria PASS; the fix is RETAINED
stopped under   CLAUDE.md §12 — a recorded failure changed value, and the gate does not name it
not run         Debug+GUI regression; the full sanitizer suite (infeasible as specified, TD-035)
blocks commit   TD-034 — clang-format is not clean on this tree
next decision   whether to authorize the discriminating probe for TD-032's blocker
evidence        results/technical_debt/TD032_lagged_time_level/summary.md
```

**TD-027 is REOPENED** by user decision of 2026-10-06: its C4 was invalidated by TD-032 and its
closure rested on it. The cap value 5.0 still stands on C1/C2/C3.

## Not authorized, and not to be started implicitly

TD-003 and every later debt item · P19 and P20 (§7–§8) · every phase in ROADMAP.md §15–§22 · any commit or push.

**Do not begin P19 merely because it appears in this roadmap.**

---

# 15. End Goal

CFDApp v1.0 is a solver that is conservative · quantitatively verified · validated against
independent references · deterministic within documented contracts · CPU/GPU equivalent within
frozen tolerances · scalable · restartable · diagnosable · regression protected · sanitizer
qualified · documented · reproducible · and **explicit about every known limitation**.

Performance matters only after those properties are demonstrated.
