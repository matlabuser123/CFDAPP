# P18.0 — P18 capability matrix (§26) and build-phase reassessment (§27)

Classification is deliberately conservative. Where something exists but is narrower than its name
suggests, it is **PARTIAL** with the narrowing stated — §26 says not to inflate status.

---

## 1. Capability matrix

| # | Capability | Status | What actually exists, and what does not |
|---|---|---|---|
| 1 | Passive species transport | **IMPLEMENTED** | steady, constant-ρ, conservative-LHS, upwind convection on the authoritative massFlux, one constant D. Production reachable, 2 shipped cases, 92 test rows |
| 2 | Multi-species state | **PARTIAL** | N independent scalars, each with its own field/BCs/D, driven by a loop. No collection type, no cross-species invariant, no shared identity beyond a name string |
| 3 | Mass-fraction representation | **MISSING** | `Y` is a generic passive scalar. `SpeciesProperties` explicitly declines the mass-fraction convention. 0 matches for `massFraction` anywhere |
| 4 | `ΣY_i = 1` handling | **MISSING** | nowhere in the repository, deliberately |
| 5 | Species-dependent diffusivity | **PARTIAL** | each species has its *own* constant `D`. No mixture-averaged `D_i`, no Schmidt model, no `D(T,Y)`, no per-cell D on the species path |
| 6 | Species boundary conditions | **PARTIAL** | Dirichlet + Neumann, per species per patch, schema-validated. **Gradient faces are lagged** (335 vs 2 outer iterations) — a confirmed defect. No symmetry/periodic types |
| 7 | Species conservation | **PARTIAL** | steady balance measured and exact in 4 tests. The transient term is not expressible; no production-facing conservation report; no per-boundary species flux API |
| 8 | Reaction source framework | **MISSING** | species source is one uniform `Real` on the RHS. No per-cell source, no implicit `Sp`, no `ω_i`. (P14 *has* the Su/Sp pair species does not use) |
| 9 | Stoichiometry | **MISSING** | nothing |
| 10 | Arrhenius chemistry | **MISSING** | nothing |
| 11 | Mixture molecular weight `W_mix` | **MISSING** | no `W_i` on any species type |
| 12 | Mixture gas constant `R_mix` | **MISSING** | `IdealGasEOS` holds one `Real gasConstant_`; `ρ = p/(RT)` in 4 places |
| 13 | Mixture `cp`/`cv`/`γ` | **MISSING** | `ThermodynamicProperties` stores one cp and *derives* cv and γ at construction; `TotalEnergyState` **refuses** a T-dependent cp outright |
| 14 | Species enthalpy `h_i(T)` | **MISSING** | P15's `SensibleEnthalpy` is single-substance. No `h_f,i` — and `referenceEnthalpy` is a sensible offset, not a heat of formation |
| 15 | Heat release | **MISSING** | needs `h_f,i` and `ω_i`, neither of which exists |
| 16 | Thermal coupling | **PARTIAL** | the *plumbing* is there: `assembleThermalSourceContribution` already takes a per-cell `ScalarField`. Nothing produces a species-derived source, and the coupling is one-way |
| 17 | Total-energy coupling | **PARTIAL** | `TotalEnergyEquation` already accepts `volumetricSource` and carries it in its balance. But that path is **P17 library-only, not production reachable**, and the density-based solver has no source term at all |
| 18 | CPU production path | **IMPLEMENTED** | case → parser → config → builder → ProjectRunner → solver → CSV/VTK/JSON export + run metadata |
| 19 | GPU production path | **MISSING** | 0 GPU references on the species path. The generic device scalar transport exists but every consumer is k-epsilon |
| 20 | Restart | **MISSING** | species state is not in the checkpoint payload |
| 21 | CLI | **IMPLEMENTED** | species runs through the standard case-directory CLI path; covered by the production-case tests |
| 22 | GUI | **MISSING** | no species GUI surface, no species GUI test |
| 23 | Export | **IMPLEMENTED** | `concentration_<name>` per species to CSV/VTK/JSON, plus `SpeciesRunMetadata` (name, D, status, converged, iterations, residual) |
| 24 | Validation | **IMPLEMENTED** *for the passive steady capability* | analytical slab profiles at 3 grids, 2 Péclet regimes, 4 conservation tests, determinism at 3 levels. Nothing for anything in rows 3–15 |

```text
TALLY:  IMPLEMENTED 6   ·   PARTIAL 8   ·   MISSING 10
```

## 2. The two findings that most shape P18

```text
1. SPECIES IS A SHIPPED PRODUCTION FEATURE, not a library experiment. Two case directories, a
   strictly-validated schema (14 parser tests including duplicate-name and unknown-key rejection),
   export columns and run metadata. P18 must EXTEND this compatibly. The authorization's "do not
   duplicate existing passive-species functionality" is not a style note -- duplicating it would
   fork a user-visible feature.

2. THERE ARE ALREADY TWO SCALAR-TRANSPORT STACKS, and species uses the weaker one. P14's
   ScalarTransport has the transient (rho-weighted, with rhoOld/rhoNew), per-cell Su/Sp sources, and
   per-cell diffusivity with face interpolation -- precisely the three things P18 needs and species
   lacks. Species does not reference it at all. P18 should COLLAPSE the two, not add a third.
   And migrating does NOT inherit a boundary fix: P14 carries the same lag (p14_reuse.md §4).
```

## 3. Build-phase reassessment (§27)

The planned sequence still makes sense. Two phases are lighter than their names suggest and one is
heavier; none should be reordered.

| Phase | Planned as | Reassessed | Why |
|---|---|---|---|
| **P18.1** Multi-Species State | implementation | **IMPLEMENTATION — genuinely new** | a species *collection* with stable identity, `ΣY_i = 1`, and bounds policy has no precedent in the repository. This is the one place new architecture is unavoidable |
| **P18.2** Species Transport & Diffusion | implementation | **MOSTLY MIGRATION + ONE REAL FIX** | the transport already exists and works. The work is (a) migrate onto P14's assemblers, (b) make the gradient boundary **exact** — which cannot be inherited and is a defect fix, (c) prove the two shipped cases are preserved |
| **P18.3** Mixture Properties | implementation | **IMPLEMENTATION, and the hardest** | needs `W_i`, `R_mix`, `cp_mix(Y,T)`, `h_i`, `h_f,i`, plus lifting `TotalEnergyState`'s constant-cp refusal — and that refusal's own message names the prerequisite: "supporting it requires a variable-gamma EOS". Touches P17 |
| **P18.4** Reaction Source Framework | implementation | **LIGHTER THAN IT SOUNDS** | P14's `ScalarSourceFields{Su, Sp}` + `applyImplicitScalarSource` already provide the assembler. The work is producing the values and wiring, not writing a source assembler |
| **P18.5** Arrhenius Chemistry | implementation | **IMPLEMENTATION — genuinely new** | nothing exists |
| **P18.6** Heat-Release Coupling | implementation | **IMPLEMENTATION + AN ARCHITECTURE CHANGE** | both energy equations already accept a per-cell source field, so the *sink* is ready. The hard part is that ProjectRunner is strictly one-way (flow → thermal → species → export) with no outer loop over the species↔energy cycle. That loop is a runner-level change |
| **P18.7** Conservation & Validation | validation | **VALIDATION, with new instrumentation** | the steady balance is already measured; the transient and reaction terms need new diagnostics |
| **P18.8** CPU/GPU Equivalence | equivalence | **IMPLEMENTATION, exactly as P17.10 was** | there is no GPU species path to compare against. P17.10's precedent applies: it looked like a comparison phase and was an implementation phase |
| **P18.9** Final Qualification | qualification | **QUALIFICATION** | and it inherits P17's two open gates as constraints, not as work |

```text
ONE SEQUENCING OBSERVATION, offered without acting on it: the boundary-lag fix in P18.2 is
independent of everything else in P18 -- it is P12-era debt in a shipped feature. It could
legitimately be done before P18.1, or as its own small authorized item, and it would immediately
benefit the existing production species cases (167x fewer outer iterations on a pure-Neumann
problem). Whether to split it out is the user's call, not mine; I am recording that it is separable.
```

## 4. Inherited P17 constraints relevant to reacting-compressible work (§2)

```text
Recorded here only because P18.3/P18.6 would build on the compressible path, and these are the
conditions it is in. NOT reopened, NOT fixed -- P18.0's authorization forbids both.

  P17 G6 PARTIAL   HLLC's wave-speed estimate does not bound a strong symmetric head-on collision;
                   star densities go negative above a wall-normal Mach of ~1.76. The solver refuses
                   loudly. Any reacting DENSITY-BASED work inherits this.
                   results/p17/qualification/G6_POSITIVITY_FAILURE.md
  P17 G10 PARTIAL  4 pre-P17 tests time out under Debug+GUI at the frozen 7200 s. A configuration
                   limit, not numerical. P18's baseline reproduces it by design.
                   results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md
  P17 total-energy path is LIBRARY-ONLY, not production reachable -- so P18.6's compressible route
  inherits a reachability gap on top of its own work.
```
