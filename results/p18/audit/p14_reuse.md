# P18.0 — P14 generic transport reuse map (§17, §18)

```text
HEADLINE: THERE ARE ALREADY TWO SCALAR-TRANSPORT STACKS, and the species one is the weaker.

  cfd::transport::ScalarTransport   (P14)  transient, per-cell Su/Sp, per-cell diffusivity with
                                           face interpolation, GPU counterpart
  cfd::species::SpeciesEquation     (P3)   steady only, one uniform Real source, one constant
                                           diffusivity, no GPU

Species does NOT use P14: 0 references from src/species or include/cfd/species. It predates P14 and
was never migrated. So §17's objective -- "avoid creating a second scalar transport stack" -- is
already violated, and P18's job is to collapse the two rather than add a third.
```

---

## 1. What P14 actually provides

```text
include/cfd/transport/ScalarTransport.hpp

  struct TransportTimeState {                      <-- the transient term species lacks entirely
    bool transient; Real dt;
    const ScalarField* phiOld;
    const ScalarField* rhoOld;                     <-- rho AT TWO TIME LEVELS, so the transient
    const ScalarField* rhoNew;                         term is d(rho phi)/dt, NOT d(phi)/dt
  };

  struct ScalarSourceFields {                      <-- exactly the Su/Sp pair a reaction needs
    ScalarField Su;   // b_P += Su_P * V_P             explicit
    ScalarField Sp;   // a_P -= Sp_P * V_P             implicit / linearised
  };

  enum class FaceDiffusivityInterpolation { Arithmetic, Harmonic };
  Real faceDiffusivity(mesh, face, const ScalarField& diffusivity, interpolation);
                                                   <-- diffusivity is a FIELD, not a constant

  void assembleScalarDiffusionContribution(mesh, const ScalarField& diffusivity, phi, bcs, ...);
  void assembleScalarConvectionContribution(...);
  void applyImplicitScalarSource(...);
  void applyTransientContribution(...);
  ScalarTransportAssembly assembleScalarTransportEquation(...);

  IT IS ASSEMBLY ONLY. There is no ScalarTransport solver and no Picard loop. Its production
  consumer is P16 turbulence -- KEpsilonEquation.cpp's solveRelaxedScalarTransport, a relaxed
  SINGLE solve per outer SIMPLE iteration.
```

## 2. The reuse table (§17)

| P18 need | P14 reusable API | Gap | Action later |
|---|---|---|---|
| `d(rho Y_i)/dt` transient | `TransportTimeState` with `rhoOld`/`rhoNew` — already the ρ-weighted form | **none for the term itself.** Species has no transient at all, so this is pure gain | P18.2: adopt `TransportTimeState`; do not write a species transient term |
| ρ-weighted convection | `assembleScalarConvectionContribution` + the authoritative `massFlux` | none — species already uses the same `massFlux`, so both stacks agree here | P18.2: reuse directly |
| Species-dependent diffusivity `D_i` | `diffusivity` as a **ScalarField** + `faceDiffusivity` with Arithmetic/Harmonic | none for per-cell `D`. A *mixture-averaged* `D_i(Y, T)` model does not exist in either stack | P18.2 reuse the field plumbing; P18.3 supplies the values |
| Reaction source `omega_i` | `ScalarSourceFields{Su, Sp}` + `applyImplicitScalarSource` | none for the assembly. The RATES do not exist | P18.4/P18.5 produce Su/Sp; reuse the assembler unchanged |
| Bounded mass fractions `0 ≤ Y ≤ 1` | nothing in either stack | **real gap** — no limiter, no clipping policy, no positivity argument | P18.2/P18.7: decide an explicit documented policy; report, do not silently clip |
| Multiple coupled species | nothing — both stacks are per-scalar | **real gap** — `ΣY_i = 1` and cross-species coupling have no home | P18.1 owns the collection type |
| Gradient boundary, exact | **NOT in P14** — `ScalarTransport.cpp:129-133` carries the same lag as species | **real gap in BOTH stacks** | see §4 |
| GPU scalar path | `DeviceScalarTransport.hpp`, `DeviceScalarTransportKernel.cu` | P14 has a GPU counterpart; species has none | P18.8: reuse P14's, do not write a species kernel |
| Steady solve + Picard loop | `SpeciesSolver::runPicardLoop` (species side) | P14 has no solver at all | keep the species solver shell; swap its assembly |

## 3. Semantic mismatch classification (§18)

```text
DIRECT REUSE
  ρ-weighted convection with the authoritative massFlux
      both stacks already consume physics::calculateMassFlux output; no semantic difference.
  per-cell explicit source Su
      P14's b_P += Su_P * V_P is a strict generalisation of species' uniform S*V.
  per-cell implicit source Sp
      a_P -= Sp_P * V_P. Nothing in species to conflict with -- species has no implicit source.
  per-cell diffusivity + face interpolation
      a strict generalisation of species' single Real. Arithmetic interpolation reproduces the
      constant-D case exactly, so migrating cannot change existing answers.

REUSE WITH EXTENSION
  d(rho Y)/dt transient
      P14 already stores rhoOld/rhoNew, which is the correct ρ-weighted form -- but species is
      documented as CONSTANT-density and its assembled operator was DERIVED from that assumption
      (the rho multiplied through). Turning the transient on means committing to the
      variable-density conservative form, which changes what the steady operator means. Not a
      drop-in: P18.2 must re-derive, not just pass a flag.
  the boundary treatment
      reusable only after the gradient face is made exact. See §4.
  the GPU path
      DeviceScalarTransport exists, but whether it covers the transient and Su/Sp terms, and
      whether it is reachable outside turbulence, is NOT established by this audit. See
      gpu_inventory.md -- classified MISSING for species until measured.

NOT COMPATIBLE
  bounded mass fractions
      neither stack has any bounds machinery, and species deliberately refuses to clip. There is
      nothing to reuse; a policy must be invented and documented.
  multiple coupled species / sum(Y_i) = 1
      both stacks are strictly per-scalar with no notion of a set. A collection type is new code
      by necessity -- this is the one place P18.1 legitimately adds architecture.
  a mixture-property closure
      nothing in P14; see thermal_energy_coupling.md.
```

## 4. The boundary lag is in BOTH stacks — so migration does not fix it

```text
  thermal/EnergyEquation.cpp:104     return {0.0, coefficient * boundaryValue(0.0, distance)};  EXACT
  species/SpeciesEquation.cpp:80     yB   = boundaryValue(concentration[owner], distance);     LAGGED
  transport/ScalarTransport.cpp:129  phiB = boundaryValue(phi[ownerId], distance);             LAGGED

Both lagged sites then add a NON-ZERO diagonal (`:86` and `:132` respectively) and put the lagged
owner value on the RHS. Two of the three scalar assemblies in this repository carry the defect;
only thermal was fixed.

MEASURED COST (boundary_lag.md): 335 outer iterations for species against 2 for thermal on an
identical pure-Neumann problem.

WHY IT WENT UNNOTICED IN P14: ScalarTransport has no Picard loop. Its only production consumer
relaxes a single solve inside SIMPLE's outer iteration, so the lag is absorbed into a loop that was
going to iterate anyway and never surfaces as a count.

CONSEQUENCE FOR THE PLAN, and it is the most important line in this document:
    P18.2 must make the gradient-face treatment EXACT DELIBERATELY, on whichever stack it builds
    on. It cannot be inherited by migrating. Gate G4 carries the negative control.
```

## 5. Recommended direction, for a later authorized phase

```text
MIGRATE species onto cfd::transport::ScalarTransport; keep the species-facing shell.

  keep   SpeciesProperties (extended), SpeciesSolver's Picard shell, the case schema, the export
         columns, SpeciesRunMetadata -- all production-visible, all must keep working
  drop   SpeciesEquation's three assemblers, replaced by P14's
  fix    the gradient boundary face, in P14 (which fixes species and turbulence at once)
  add    only what genuinely does not exist: a species COLLECTION (P18.1), mixture properties
         (P18.3), reaction rates (P18.4/5)

WHY NOT the reverse (extend SpeciesEquation instead): it would mean adding transient, per-cell
sources, per-cell diffusivity and a GPU path to the species stack -- i.e. rebuilding P14 inside
species. That is the "second scalar transport stack" §17 exists to prevent, except it would be the
third.

ONE RISK TO TEST RATHER THAN ASSUME: migrating must not change existing species answers. P14's
Arithmetic face interpolation of a uniform diffusivity field should reproduce the constant-D
operator exactly, and the convection term already shares massFlux -- so bitwise preservation of the
two shipped species cases is a reasonable gate, and G1 asks for it.
```
