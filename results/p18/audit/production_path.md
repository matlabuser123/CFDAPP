# P18.0 — species production reachability (§6)

A class existing is not evidence of production capability. Every link below was traced to a file
and line.

**Verdict: species is PRODUCTION REACHABLE end to end** — case file → parser → config → builder →
dispatch → solver → export. This is unusual for this repository (P17's density-based solver, by
contrast, is library-only), and it is the single most important constraint on P18: there is a real
user-facing species feature that must not be broken.

---

## 1. The chain, link by link

```text
CASE FILE
  cases/species_diffusion/physics.json
      "species": [ {"name": "CO2", "diffusivity": 2.0e-3, "initial_concentration": 0.0} ]
  cases/species_diffusion/boundaries.json
      patches.<patch>.species.<name> = {"type": "fixed_value"|"fixed_gradient", "value": <Real>}
        |
PARSER
  src/io/case/PhysicsConfigParser.cpp:389-411
      one entry of physics.json's optional "species" array; keys are STRICTLY validated
      (rejectUnknownKeys: only name / diffusivity / initial_concentration), name must be
      non-empty, diffusivity may be 0 ("pure advection intentionally supported")
  src/io/case/BoundaryConfigParser.cpp:76-92
      parseConcentrationSpec -- type restricted to fixed_value | fixed_gradient
        |
CONFIG
  include/cfd/io/case/PhysicsConfig.hpp:127-223
      a VECTOR of species configs; an empty vector is legal (species is optional)
        |
BUILDER
  src/io/CaseBuilder.cpp:592
      .properties = SpeciesProperties(speciesConfig.name, speciesConfig.diffusivity)
  include/cfd/io/SimulationSetup.hpp:27-34, 202-204
      SpeciesSetup { properties, initialConcentration, concentrationBoundaries }
        |
DISPATCH
  src/app/ProjectRunner.cpp:664-695
      one SpeciesSolver::solve() per declared species, after SIMPLE, gated on massFlux existing
        |
SOLVER
  src/species/SpeciesSolver.cpp  -> SpeciesEquation.cpp -> BiCGSTAB
        |
EXPORT
  ProjectRunner: speciesForExport.emplace_back("concentration_" + name, result.concentration)
  include/cfd/io/CSVWriter.hpp -- one column per species
  plus SpeciesRunMetadata { name, diffusivity, status, converged, iterations, finalResidual }
```

## 2. Classification

| Capability | Class | Evidence |
|---|---|---|
| Passive species transport (steady, constant-rho) | **PRODUCTION REACHABLE** | full chain above; two shipped cases |
| Multiple independent species | **PRODUCTION REACHABLE** | `physics.json` species array; ProjectRunner loops per species |
| Per-species constant diffusivity | **PRODUCTION REACHABLE** | `diffusivity` key, incl. `0` for pure advection |
| Per-species, per-patch Dirichlet / Neumann BCs | **PRODUCTION REACHABLE** | `patches.<p>.species.<n>.{type,value}` |
| Per-species initial concentration | **PRODUCTION REACHABLE** | `initial_concentration` key |
| Species CSV export + run metadata | **PRODUCTION REACHABLE** | `concentration_<name>` column; `SpeciesRunMetadata` |
| Uniform volumetric source `S` | **LIBRARY/API ONLY** | `assembleSpeciesTransportEquation(..., volumetricSource)` defaults to 0 and ProjectRunner never passes it — **no case-file key exists** |
| Non-orthogonal correction for species | **PRODUCTION REACHABLE** | ProjectRunner passes `nonOrthogonalOptions(setup.solverSettings)` |
| Boundedness diagnostic `concentrationBounds` | **LIBRARY/API ONLY** | no production caller; tests only |
| Species restart / checkpoint | **MISSING** | species state is not in the checkpoint payload |
| Species on GPU | **MISSING** | 0 GPU references on the species path |
| Reaction / chemistry | **MISSING** | nothing exists |

## 3. The one-way coupling, which matters for P18

```text
ProjectRunner.cpp:664: "same one-way, best-available policy as thermal above -- run only once the
SIMPLE result is fully finite"

    SIMPLE (flow) ---- converges ----> massFlux (FIXED)
                                          |
                                          v
                        species solve, one per species, each with its own Picard loop
                                          |
                                          v
                                   export only -- never fed back

CONSEQUENCES FOR P18, stated now because they shape every later phase:
  * species cannot currently influence the flow. A reacting flow whose density or viscosity
    depends on composition would need this one-way arrow to become two-way, which is a
    ProjectRunner-level architecture change, not a species-module change.
  * because massFlux is FIXED before species runs, there is NO flow/species lag -- the only lag
    is the boundary one inside the species solve itself (see boundary_lag.md).
  * "out.speciesResults stays empty (not partially populated) when massFlux was never computed;
    speciesMetadataForExport still gets one 'NotRun' entry per declared species either way" --
    the failure reporting is already honest and should be preserved.
```

## 4. What this forbids P18 from doing casually

```text
Species is SHIPPED. Two case directories, a validated schema with strict key rejection, CSV export
columns and run metadata that users may already depend on. Any P18 change to the species schema,
field names, or export columns is a BREAKING CHANGE to a production feature, not a refactor of an
internal library -- and P18.0's authorization says explicitly: "Do not duplicate existing
passive-species functionality."

The migration path that respects this is to keep `physics.json`'s existing species keys working
unchanged and ADD to them, never to introduce a parallel "multi-species" schema alongside.
```
