# P18.1 — multi-species state — design

```text
WRITTEN BEFORE ANY IMPLEMENTATION OR MEASUREMENT EXISTS.
AUTHORITATIVE INPUTS   results/p18/audit/*.md (capability matrix + reuse map, per the authorization)
                       results/p18/gates/P18_GATES.md, sha256 verified 921616544e3631d4... at
                       P18.1 start -- unchanged since it was frozen.
TREE AT P18.1 START    P18_START_SOURCE_FINGERPRINT
                       60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318
                       IDENTICAL to the P18.0 freeze. P18.1 starts from the qualified baseline.
```

---

## 1. Requirement classification (§1)

Each P18.1 checklist item against what the P18.0 audit found. The capability-matrix row is cited so
the classification is traceable rather than asserted.

| P18.1 requirement | Classification | Basis |
|---|---|---|
| Define species state `Y_i` | **MISSING** | row 3 — `Y` is a generic passive scalar; `SpeciesProperties.hpp` explicitly *declines* the mass-fraction convention. 0 matches for `massFraction` in `src/` or `include/` |
| Support multiple transported species | **NEEDS EXTENSION** | row 2 PARTIAL — N independent scalars already work, by a `ProjectRunner` loop. No collection type, no shared identity beyond a name string |
| Enforce valid mass fractions | **MISSING** | rows 3, 4 |
| Verify `0 ≤ Y_i ≤ 1` | **NEEDS EXTENSION** | `concentrationBounds()` exists but returns a global {min,max} only, is reporting-only, and is never per-cell |
| Verify `ΣY_i = 1` | **MISSING** | row 4 — nowhere in the repository, deliberately |
| Preserve passive-scalar behaviour | **ALREADY SATISFIED** — to be *proved*, not built | see §4 below: P18.1 is strictly additive |
| Add unit tests | **MISSING** for the new type | the existing 92 species rows cover the passive stack only |

## 2. What P18.1 deliberately does not do

```text
NOT TOUCHED   SpeciesEquation · SpeciesSolver · SpeciesProperties · the case schema · the parser ·
              CaseBuilder · ProjectRunner · export · any boundary class · any CUDA kernel
NOT BUILT     transport, diffusion, sources, reactions, mixture properties, GPU kernels  (P18.2+)
NOT FIXED     the species boundary lag (P18.0 finding; belongs to P18.2 or its own authorization)
```

P18.1 owns the **state**, not the PDE. The new types are additive: no existing translation unit
changes, so passive-scalar behaviour is preserved *by construction* and G1 measures it rather than
relying on it.

## 3. The two types, and why two

```text
cfd::species::SpeciesRegistry    IDENTITY.  ordered species names, bijective name <-> index.
cfd::species::MassFractionState  STATE.     the registry + one mass-fraction value per species
                                            per cell, with the mixture invariants enforced.
```

Identity is split from values because they have different lifetimes and different consumers. P18.3
needs per-species molecular weights and `cp` keyed by identity with no field in sight; P18.2 needs
the fields. Folding both into one class would force every future consumer to carry a full field set
to ask a question about a name. This also matches how the repository already separates
`SpeciesProperties` (identity + coefficient) from the `ScalarField` it is applied to.

`MassFractionState` holds the registry **by value**, so a state is self-describing: a field set can
never be interpreted against the wrong species ordering.

## 4. Storage layout (§5)

```text
LAYOUT        SPECIES-MAJOR -- std::vector<cfd::fields::ScalarField>, one ScalarField per species,
              each sized cellCount.
              Y_i[cell]  ==  fields_[i][cell]
number of cells    cellCount, fixed at construction, identical for every species
number of species  registry.size(), fixed at construction, >= 1
ownership          MassFractionState owns the registry and every field, by value
copy/move          implicit -- std::vector<ScalarField> and SpeciesRegistry are both regular types,
                   so the compiler-generated copy/move preserve identity, values and ordering.
                   No custom operators are written (§21 says not to add them if the standard
                   semantics already suffice).
```

**Why species-major, stated before measuring anything (§32 — this is not a performance choice).**
The repository's established convention is field-per-species: `SpeciesProperties.hpp` documents it
("a caller transports as many independent species as it wants by holding one
`cfd::fields::ScalarField` per species"), `SpeciesSetup`/`SpeciesRunResult` each own one
`ScalarField`, and export writes one `concentration_<name>` column per species. Species-major makes
the P18.1 state hold *exactly the objects the existing stack already holds*, so the §11/§31
compatibility claim is a bitwise identity rather than a reinterpretation across a layout change.

Cell-major (`Y[cell * N + i]`) would suit the per-cell sum and a future device kernel better. It is
**not** chosen here: P18.8 owns GPU layout, §32 forbids changing layout for presumed GPU
performance, and adopting it now would make the compatibility proof a transformation instead of an
identity. Recorded as a deliberate, reversible decision, not an oversight.

## 5. Invariants — all three inherited from the frozen gate, none chosen here (§7, §30)

`P18_GATES.md` G2 fixes these. P18.1 adopts them verbatim; no threshold is derived or selected
after seeing an implementation, because none is derived here at all.

```text
finiteness     std::isfinite(Y_i) at every (species, cell).  NaN and Inf are rejected.
bounds         0 <= Y_i <= 1.  G2(b): "boolean, no tolerance. A mass fraction is in [0,1] or it
               is not."  -1e-12 must fail. 1+1e-12 must fail.
sum            |sum_i Y_i - 1| <= 4 * eps * N_species, per cell.
               G2(a), derived there: a sum of N_species doubles each <= 1 accumulates at most
               ~N_species/2 eps of round-off; 4x that is a bound, not a fitted number. Explicitly
               NOT zero, because a normalised set cannot be exactly 1 in binary floating point
               for general N.
eps            std::numeric_limits<Real>::epsilon() = 2.220446049250313e-16  (Real = double)
```

**Summation order (§7, §17).** Ascending species index, sequential left-to-right accumulation into
one `Real`, starting from the index-0 value. No `std::accumulate` over an unordered container, no
pairwise or compensated summation, no parallel reduction. The registry stores names in a
`std::vector` in insertion order, so the order is a property of the object and is stable across
copies. Two validations of the same state therefore produce a bitwise identical sum.

## 6. Mutation policy (§18) — valid by construction, per-cell atomic

```text
CONSTRUCTION   every constructor validates the complete state and THROWS on an invalid one.
               There is no way to obtain an invalid MassFractionState.
MUTATION       exactly one mutator:  setCellComposition(cell, composition)
               It validates the complete incoming composition for that cell -- length, finiteness,
               bounds, sum -- and commits only if all four pass. On failure the state is UNCHANGED.
NO SETTER      setMassFraction(species, cell, value) is DELIBERATELY NOT PROVIDED.
               Changing one Y_i necessarily breaks sum(Y)=1, so a single-species setter can only
               exist by either leaving the object invalid or silently repairing the remainder.
               Both are forbidden -- the first by §18 ("avoid temporary invalid states leaking into
               solver code"), the second by §8 and §24. A caller that wants to change a composition
               supplies the whole composition.
```

There is consequently no `validate()`-later mode and no "finalize" step: the invariant holds
between every pair of public calls. `validate()` exists anyway, as a re-check a caller (or a test,
or a negative control) can invoke on demand — it can never fail on a state built through the public
API, which is exactly the property the negative controls attack.

## 7. Normalization is never implicit (§8, §9)

```text
normalizeMassFractions(composition)   FREE FUNCTION, named, separate from validation, never called
                                      by any constructor or by setCellComposition.
Y = [0.8, 0.8]   ->  construction / setCellComposition   REJECTED (sum 1.6)
                 ->  normalizeMassFractions              [0.5, 0.5], only because it was asked for
```

It rejects what cannot be normalised meaningfully: an empty composition, any non-finite entry, any
negative entry, and a sum that is zero or non-finite. It does **not** reject entries > 1, because
`[2, 2]` is a legitimate un-normalised ratio; it normalises to `[0.5, 0.5]`. Its output is then
subject to the ordinary validation like any other composition — normalising is not a way to enter
the state through a side door.

## 8. Remainder species — not used (§24)

`Y_N = 1 - sum_{i<N} Y_i` is **not** implemented. Nothing in the existing architecture or in the
frozen P18 gates chooses that model, and §24 says to prefer an explicit full composition absent
such a choice. Every species carries its own stored value. The round-off, boundedness and
species-asymmetry consequences of a remainder rule are therefore not incurred, and the sum
criterion stays a genuine check rather than an identity that cannot fail.

## 9. Single-species limit (§10)

For `N_species == 1` the sum criterion reduces to `|Y_0 - 1| <= 4 * eps`, so `Y_0 = 1` is the only
admissible composition and `Y_0 = 0.5` is rejected. This falls out of the general invariant; no
special case is written for it. Verified explicitly rather than assumed.

## 10. Diagnostics (§15)

`cfd::InvalidArgumentError` — the repository's existing type for a structurally invalid argument —
carrying the species name, the cell, the offending value or sum, and the admissible range:

```text
mass fraction for species "O2" at cell 17 is -0.003; must be in [0, 1]
mass fractions at cell 17 sum to 1.041; must be within 8.88e-16 of 1
mass fraction for species "O2" at cell 17 is not finite
composition for cell 17 has 2 entries; expected 3
duplicate species name "O2" at index 2
unknown species "Ar"
```

Per-cell, not aggregate (§16): validation walks every cell and reports the **first** offending cell
by index, so a single bad cell among thousands cannot hide behind a global min/max/mean.

## 11. Boundary compatibility (§23)

P18.1 does not touch boundaries and does not fix the lag. Recorded for the compatibility evidence:
species boundary composition today is stored **per species, as an independent scalar BC object**
(`FixedValue` / `FixedGradient` per species per patch) with no cross-species constraint and no
remainder rule. `MassFractionState` neither requires nor contradicts that. Making boundary
compositions satisfy ΣY=1 is a P18.2 concern and is explicitly not claimed here.

## 12. Conservation interface preparation (§25)

`field(i)` returns the species' `ScalarField` by const reference — enough for P18.2 to form `rho*Y_i`
against a density field. **No `rhoY_i` is stored**, so there is no duplicate state to keep in sync.

## 13. Gates this phase may claim

```text
MAY CLAIM      G2 (species-state validity)  -- in full, all four criteria
               G1 (baseline preservation)   -- the regression half; P18.1 is additive
               G10 determinism              -- only the state-layer part: repeated validation of the
                                              same state is bitwise identical. NOT the solver-level
                                              claim, which is P18.8/P18.9.
MUST NOT CLAIM G3 G4 G5 G6 G7 G8 G9 G11 -- none is measurable from a state-only change, and §36
               forbids claiming them from state-only tests.
```
