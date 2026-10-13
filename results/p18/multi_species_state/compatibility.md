# P18.1 — compatibility (§11, §23, §26, §27, §31)

```text
HEADLINE   P18.1 is STRICTLY ADDITIVE. Two new production translation units, one new test file,
           and two CMakeLists lines that compile them. No pre-existing production or test file
           was modified. Passive-scalar behaviour is therefore preserved BY CONSTRUCTION, and the
           regression measures that rather than being asked to take it on trust.

           One REAL SEMANTIC GAP was found and is recorded rather than papered over: the shipped
           species case's field is a passive TRACER, not a composition, and by itself it is not a
           valid mixture. See section 3.
```

---

## 1. The changeset (`logs/09_changeset.log`)

```text
?? include/cfd/species/SpeciesRegistry.hpp          c68ab74c6b836344758327714da3161b5bdf4447...
?? include/cfd/species/MassFractionState.hpp        20462909c405e0099c6023084f36d8136a4251f3...
?? src/species/SpeciesRegistry.cpp                  d55652da02e0a85f9d8fad7f87de664804057edb...
?? src/species/MassFractionState.cpp                3f7a6bc7f8576cb0d7791eaf66ace0994e0d7b0d...
?? tests/unit/species/test_mass_fraction_state.cpp  c2971b3e3347272df9e8df2586909815db89d0dc...
M  src/CMakeLists.txt                               (+1 target_sources block, 2 source entries)
M  tests/unit/species/CMakeLists.txt                (+1 line)

tracked files modified in the whole tree : 130
...of which P18.1 touched                :   2
```

The other 128 are the uncommitted P13–P17 working tree that P18.0 audited and froze as the P18
start state. P18.1 did not touch them.

**Not touched by P18.1:** `SpeciesEquation` · `SpeciesSolver` · `SpeciesProperties` · the case
schema · `PhysicsConfigParser` · `BoundaryConfigParser` · `CaseBuilder` · `ProjectRunner` ·
`CSVWriter` · any boundary class · any CUDA kernel.

## 2. Existing passive-species behaviour

```text
MECHANISM OF PRESERVATION   the passive stack's translation units are unchanged and neither
                            depends on the new headers. Nothing it compiles or links differs.
MEASURED                    the pre-existing species unit suite: 37 tests, 37 passed, in the same
                            binary as the new ones (logs/02_focused_tests.log).
                            The full three-configuration regression is regression.md.
```

## 3. THE GAP: a shipped passive field is not by itself a mixture (§31)

§31 asks for a compatibility test built from a real passive-species case, and says that if the old
semantics do not map one-to-one, the gap must be **documented, not faked**. They do not map
one-to-one, and here is exactly how.

The test uses the actual committed `concentration_CO2` column of
`cases/species_diffusion/results/fields.csv` — 80 cells of converged production output from the
pre-P18 chain (case → parser → CaseBuilder → ProjectRunner → SpeciesSolver → CSV), embedded
verbatim.

```text
(a) BOUNDS ARE ALREADY COMPATIBLE
    every one of the 80 shipped values lies in [0, 1].
    measured range: 2.49999680492757233e-02 .. 9.74999968833643260e-01
    -> the new bounds invariant does not reject data this project actually ships.

(b) BUT THE FIELD IS A TRACER, NOT A COMPOSITION
    the case declares ONE species. Read as a one-species MIXTURE the data must be REJECTED,
    because the only admissible one-species composition is Y = 1 and these values are not 1.
    -> MassFractionState(SpeciesRegistry({"CO2"}), {tracer})  THROWS.  This is CORRECT.
       A passive tracer is not a closed mixture, and pretending otherwise is precisely the
       silent-normalisation failure section 8 forbids.

(c) WITH AN EXPLICIT CARRIER IT IS A VALID MIXTURE, BITWISE
    adding N2 = 1 - Y_CO2 gives a closed two-species mixture holding the SAME values:
       state.massFraction("CO2", cell) == kShippedCO2[cell]   for all 80 cells, EXACT equality
    -> migrating a shipped field into the new state cannot perturb a number.
```

**Consequence for P18.2, stated now rather than discovered later.** Making the shipped species
cases into genuine mixtures requires a decision that does not exist in the case format today: how a
case declares the complementary / carrier species. P18.1 deliberately does not invent one — §24
forbids introducing a remainder rule that no frozen design chose. The three tests above are what
will change when P18.2 makes that decision.

## 4. Serialization and the case model (§26)

```text
CASE SCHEMA      UNCHANGED. P18.1 adds no key, removes none, and renames none. The 14 parser tests
                 (duplicate names, unknown keys) are untouched and still pass.
ROUND TRIP       not applicable at this phase in the sense §26 means it: MassFractionState is not
                 yet reachable from a case file, so there is no configuration -> state ->
                 serialization -> state path to round-trip. Recorded as a LIMIT, not a pass.
                 Wiring the state into the case model is P18.2's decision (see section 3).
```

## 5. Export (§27)

```text
UNCHANGED. Export writes one `concentration_<name>` column per species from the ScalarField the
pre-P18 stack owns. P18.1 adds no export path and removes none.
COMPATIBLE.  MassFractionState::field(name) returns the species' ScalarField by const reference --
             the same type the existing exporter already consumes -- so a future exporter needs no
             new subsystem. Asserted by FieldAccessorReturnsTheWholeFieldForExport, which checks
             the field accessor agrees with the per-cell accessor for every cell.
```

## 6. Boundaries (§23)

```text
NOT TOUCHED, and the boundary lag NOT fixed -- it is a P18.0 finding belonging to P18.2 or its own
authorization, and CLAUDE.md section 2 forbids repairing it inside this phase.

RECORDED FOR THE FUTURE: species boundary composition today is stored PER SPECIES as an
independent scalar BC object (FixedValue / FixedGradient per species per patch), with no
cross-species constraint and no remainder rule. MassFractionState neither requires nor contradicts
that. Whether boundary compositions must themselves satisfy sum(Y)=1 is a P18.2 question and is
NOT claimed here.
```

## 7. GPU (§33)

```text
NO CUDA CODE ADDED. P18.0 established no requirement for the state representation itself to be
device-visible for correctness, so section 33's exception does not apply. MassFractionState holds
std::vector<ScalarField>, each of which already exposes contiguous data() for a future transfer.
Full GPU equivalence is P18.8's.
```

## 8. Pre-existing condition found, not caused, and not fixed

```text
clang-format: 28 files in the tree violate the project's own format contract.
              NONE is a P18.1 file -- all five P18.1 files pass --Werror.
              clang-format's verdict on a file depends only on that file's content, and P18.1
              changed none of the 28, so the violations are inherited from the uncommitted
              P14-P17 working tree.
              NOT FIXED (CLAUDE.md section 2: do not repair unrelated defects inside another
              phase). Reported for TODO.md technical debt.
              logs/08_format.log, logs/09_changeset.log
```
