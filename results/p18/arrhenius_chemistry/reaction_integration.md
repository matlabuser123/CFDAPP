# P18.5 — integration with P18.1–P18.4 (§23–§26)

```text
P18.5 ADDED TWO FILES AND MODIFIED NO EXISTING PRODUCTION SOURCE. The only changes to pre-existing
files are two build-list additions and one comment:

    include/cfd/species/ArrheniusKinetics.hpp   NEW
    src/species/ArrheniusKinetics.cpp           NEW
    src/CMakeLists.txt                          one target_sources entry + comment
    tests/unit/species/CMakeLists.txt           one test file entry

So preservation of P18.1-P18.4 is structural, not merely measured. It is measured anyway, because
"it cannot have changed" is the claim most worth checking.
```

---

## 1. The chain, and who owns each step

| step | owner | P18.5's use of it |
|---|---|---|
| `Y_i`, bounded and summing to one | **P18.1** `MassFractionState` | read only; P18.5 stores no composition |
| `M_i`, `R_mix`, `cp_mix`, `rho(p,T)` | **P18.3** `MixtureThermodynamics` | read only; `R_u` comes from here, not a local copy |
| `C_i = rho Y_i / M_i` | **P18.5** | the only new per-cell quantity |
| `k(T)`, `q_r` | **P18.5** | the whole of P18.5's numerics |
| `omega_i = sum_r M_i nu_{i,r} q_r` | **P18.4** `reactionSpeciesSources` | called, never reimplemented |
| conservative transport of `Y_i` | **P18.2** `speciesTransportAssembly` | unchanged; `omega_i` fills the `Su` field it already takes |

```text
NO SECOND ASSEMBLY PATH EXISTS. omega_i fills the Su field of the SpeciesSources that P18.2's
speciesTransportAssembly already consumes, so adding chemistry did not add a code path through the
transport equation -- the same assembly serves a reacting and a non-reacting case.
```

## 2. Preservation, measured

`logs/03_preservation.log`, via `tools/preserve_predecessors.sh`:

| group | suites | listed | ran | passed | failed |
|---|---|---:|---:|---:|---:|
| **P18.1** | `MassFractionStateTest`, `SpeciesRegistryTest` | 60 | 60 | **60** | 0 |
| **P18.2** | the nine `Species*Test` suites of `test_species_transport.cpp` | 34 | 34 | **34** | 0 |
| **P18.3** | `MixtureThermodynamicsTest`, `SpeciesThermodynamicsTest` | 34 | 34 | **34** | 0 |
| **P18.4** | `ReactionTest`, `ReactionMechanismTest`, `ReactionSourceTest`, `ReactionTransportTest` | 38 | 38 | **38** | 0 |
| **P18.5** | `ArrheniusTest`, `ArrheniusKineticsTest` | 38 | 38 | **38** | 0 |
| **pre-P18 species** | the four P14/P15 files | 37 | 37 | **37** | 0 |

```text
60 + 34 + 34 + 38 + 38 + 37 = 241 = the whole binary.

THAT ARITHMETIC IS THE POINT, not decoration: it proves the six filters PARTITION the binary, with
nothing uncounted and nothing counted twice. Six groups each reporting "all passed" would be
consistent with a filter that silently covers a tenth of what its name suggests.
```

## 3. The probe fails closed on a vacuous filter

```text
THIS GUARD EXISTS BECAUSE OF A P18.3 DEFECT. A preservation filter there named
*TemperatureDependent* while the suite was actually called TemperatureProperty: it matched 0
tests and reported a pass. A 0-test filter reporting "all passed" is a vacuous claim.

preserve_predecessors.sh now COUNTS each filter's matches BEFORE running it and treats 0 as a hard
failure, and additionally asserts listed == ran so a filter that matches more than it runs is
caught too. The suite-to-phase map was read out of the test sources rather than assumed.
```

## 4. Case-file reachability — a stated limitation

```text
P18.5 IS LIBRARY/API SCOPE. There is no case-file key, parser field, solver dispatch or CLI/GUI
surface for a reaction mechanism or its Arrhenius parameters. No case file can specify a reacting
flow, so NO CASE FILE CAN REACH ANY OF THIS CODE.

Per CLAUDE.md §9 that means P18.5 is NOT production capability. It is recorded as such in TODO.md
and ROADMAP.md rather than described as shipped chemistry.

THE SAME LIMIT APPLIES TO P18.2, P18.3 AND P18.4 and was recorded at each of their closes. The
gap is now four phases wide, which is worth stating as one fact rather than four: the species
reacting-flow stack is complete as a library and reachable from nothing. Wiring it is a separate
authorized scope.
```

## 5. What P18.5 deliberately does not do

| not done | why |
|---|---|
| heat release / `T` feedback | P18.6's scope. P18.5 **reads** `T` and never writes it; the dependency is strictly one-way, so the `reaction -> heat -> T -> reaction` loop does not exist yet |
| equilibrium constants | needs the Gibbs-energy route and full thermodynamic polynomials; §20 forbids inventing one. Reverse parameters are supplied independently — see `progress_rates.md` §4 |
| third-body / pressure-dependent reactions | not in the frozen gate |
| `A`'s dimensional validation | impossible in code: `A`'s units depend on the reaction's total order. See `parameter_validation.md` §4 |
| stiff-chemistry time integration | P18.5 produces instantaneous rates; no sub-stepping, no operator splitting |
