# P16 — P14 transport migration audit

Written for: whoever plans the migration. **Read-only audit** — no production
code, test, build file, case file or SST evidence was touched.

Performed while the SST precursor/A-B validation run was in flight.

---

## Headline

**All six production turbulence equations still use the old
`cfd::turbulence` assembler. `cfd::transport` has zero production consumers.**

And a documentation defect worth fixing on its own: `ScalarTransport.hpp`
states

> *"`cfd::turbulence`'s own entry points now forward here, so k-epsilon,
> k-omega and SST keep their exact previous numbers."*

**That is not true of the current tree.** `src/turbulence/KEpsilonEquation.cpp`
neither includes nor calls anything in `cfd::transport`; it carries its own
complete face loops. The forwarding the header describes does not exist.

The good news, established below, is that the migration is **much smaller than
it looks** — most of the apparent duplication is not duplication at all.

---

## 1. Which equations use the old assembler

Every one. All six route through
`cfd::turbulence::solveRelaxedScalarTransport`:

| Model | Equation | Call site |
|---|---|---|
| k-epsilon | k | `KEpsilonModel.cpp:199` |
| k-epsilon | epsilon | `KEpsilonModel.cpp:204` |
| k-omega | k | `KOmegaModel.cpp:157` |
| k-omega | omega | `KOmegaModel.cpp:160` |
| SST | k | `SSTModel.cpp:245` |
| SST | omega | `SSTModel.cpp:251` |

No other production caller exists. `cfd::transport`'s only references anywhere
in `src/`, `include/` or `apps/` are two **comments** in
`include/cfd/gpu/DeviceScalarTransport.hpp`.

---

## 2. What is actually duplicated

This is the section that changes the shape of the job.

| Piece | `cfd::turbulence` | `cfd::transport` | Duplicated? |
|---|---|---|---|
| **Convection** | delegates to `cfd::thermal::assembleThermalConvectionContribution(mesh, 1.0, …)` | **delegates to the same function, same unit coefficient** | **NO — one implementation, called by both** |
| **Diffusion** | own face loop | own face loop | **YES** |
| **Source (Su/Sp)** | own loop | own loop + optional `requireNonPositiveSp` | **YES** (behaviourally identical at the default) |
| **Non-orthogonal correction** | `NonOrthogonalCorrectionOptions`, same operator | same | shared operator, duplicated call site |
| **Boundary handling** | `boundaryConditionForFace` + `ScalarBoundaryCondition` | same, **plus a Robin rejection** (P15) | mostly shared; see §4 |
| **Transient** | **absent** | `applyTransientContribution` + `TransportTimeState` | P14 only |
| **Relaxation** | `pressure_velocity::applyImplicitUnderRelaxation` | **absent** | turbulence only |
| **Linear solve + floor** | BiCGSTAB + `max(phi, floor)` | **absent** | turbulence only |
| **Imposed cells / `fixRow`** | `ImposedCell` (P16) | **absent** | turbulence only |

So the genuine duplication is **two loops**: diffusion and source. Convection —
the piece one would most expect to be duplicated — already has a single
implementation that both layers call.

Both layers also inherit the same wrong-way dependency: `transport → thermal`
and `turbulence → thermal`, purely to reuse that one convection loop.
`ScalarTransport.cpp:170-173` records this as inherited rather than introduced,
and defers it.

---

## 3. Does P14 support what turbulence needs?

| Need | P14 | Note |
|---|---|---|
| Variable diffusivity | **yes** | per-cell `ScalarField`, distance-weighted at faces |
| `Su + Sp*phi` | **yes** | `requireNonPositiveSp` defaults **off**, so identical to today |
| Generic scalar BCs | **yes** | same polymorphic path |
| Non-orthogonal diffusion | **yes** | same options struct, same operator |
| Transient fields | **yes** | turbulence does not currently use it |
| Face-interpolation choice | **yes** | `Arithmetic` default = today's convention |
| CPU path | **yes** | |
| **Imposed cells (`fixRow`)** | **NO** | P16 added this to turbulence only |
| **Under-relaxation** | **NO** | P14 assembles; it does not relax |
| **Linear solve / floor** | **NO** | by design — P14 is an assembler |
| **`DeviceScalarTransport`** | exists, **unused**; no imposed-cell or relaxation support | would need the same additions |

**Only one capability gap actually blocks the migration: imposed cells.**
Relaxation, the solve and the floor are not gaps — they live in
`solveRelaxedScalarTransport`, which is a *solver wrapper*, and there is no
reason for it to move. It can keep doing those things while calling P14 for the
assembly triple.

---

## 4. Semantic mismatches against bitwise equivalence

| # | Mismatch | Blocks bitwise? |
|---|---|---|
| 1 | **Imposed cells absent from P14.** `solveRelaxedScalarTransport` applies `fixRow` + rhs overwrite *after* relaxation. | **No** — this step stays in the wrapper, after the P14 call. Unaffected. |
| 2 | **Assembly tail differs.** P14: diffusion → convection → source → **transient**. Turbulence: diffusion → convection → source → **relaxation** → imposed. | **No** — with a default `TransportTimeState` P14 adds *no call at all*, so the first three are the whole assembly and the wrapper continues from there. |
| 3 | **P14 diffusion rejects `ConvectiveHeatTransfer`** (P15); turbulence's does not. | **No** for turbulence — k/ε/ω never carry a Robin BC. It is a strictly-additive refusal on a path turbulence cannot reach. Would become a real difference only if someone gave a turbulence scalar a Robin wall, which the refusal is there to stop. |
| 4 | `requireNonPositiveSp` | **No** — defaults off. |
| 5 | `FaceDiffusivityInterpolation` | **No** — `Arithmetic` is today's convention. |
| 6 | Non-orthogonal gradient built unconditionally in both | **No** — same P12-DIFF-002 A2 behaviour. |

**Conclusion: nothing blocks a bitwise migration of the assembly triple.**

And the equivalence is not a hope — it is **already asserted by an existing
test**: `TransportFoundation.LegacyMatrixEquivalence` in
`tests/unit/transport/test_scalar_transport.cpp` compares the two assemblers
bitwise, *including sparsity structure*, on identical inputs. It was green in
P15's final regression (2160/2160).

**Caveat, stated rather than assumed:** that target (`CFDTransportTests`) was
not in the subset re-run during P16's SST work. P16 added `fixRow` and the
`imposedCells` parameter, neither of which changes default behaviour, so it
should still be green — but **it has not been re-verified since**, and the
migration must begin by re-running it rather than trusting this paragraph.

---

## 5. Minimum migration sequence

Designed so each step is independently revertible and none deletes anything
before equivalence is shown.

### Step 0 — re-establish the equivalence baseline
Run `CFDTransportTests`. If `LegacyMatrixEquivalence` is green, the bitwise
claim holds on today's tree. **If it is not, stop** — something in P16 changed
an assembler, and that is a bigger finding than the migration.

### Step 1 — add imposed-cell support to `cfd::transport`
The one real gap. Move `ImposedCell` into `cfd::transport`, defaulting to
empty. Empty must leave the assembled system byte-identical — the same property
`fixRow` already has and already tests.

### Step 2 — extend the equivalence test to cover it
Prove bitwise equality *with* imposed cells before any call site moves.

### Step 3 — migrate ONE equation
**k-epsilon's `epsilon`.** Chosen deliberately: it is the only equation that
currently uses imposed cells (the wall-function ε), so it exercises the new
support immediately rather than deferring the risk. `solveRelaxedScalarTransport`
keeps relaxation, the solve, the floor and the imposed-cell application; only
the assembly triple is replaced.

### Step 4 — regression
Turbulence unit, solver, physics, channel validation, and both SST sweeps. The
channel gates are the sharpest instrument available: `E_Reτ = 0.0003` at
Re_τ = 590 would move visibly if anything changed.

### Step 5 — migrate the remaining five
k-ε k, k-ω k/ω, SST k/ω. Same wrapper, same pattern.

### Step 6 — regression, then remove the duplicate loops
**Only now** may `cfd::turbulence`'s own diffusion and source loops be deleted,
and the header comment in `ScalarTransport.hpp` become true. Deleting earlier
would remove the reference implementation the equivalence test compares
against.

### Explicitly out of this sequence
- `DeviceScalarTransport` — it has neither imposed-cell nor relaxation support,
  and GPU turbulence is a later milestone.
- The `transport → thermal` convection dependency — inherited, recorded, and
  not made worse by any step above.

---

## 6. Defects found by this audit

| # | Defect | Severity |
|---|---|---|
| 1 | `ScalarTransport.hpp` claims turbulence forwards to `cfd::transport`. It does not. | **Documentation states a false fact about the code.** A reader would believe the migration is done. |
| 2 | `cfd::transport` has zero production consumers — P14 delivered a validated foundation nothing uses. | Recorded in the P16 audit; unchanged. |
| 3 | `CFDTransportTests` was not re-run during P16's SST work. | Low — no assembler was edited — but unverified, and Step 0 exists to close it. |

Defect 1 is worth fixing regardless of when the migration happens: a comment
that describes work as complete when it has not been done is worse than no
comment, because it stops the next reader from checking.
