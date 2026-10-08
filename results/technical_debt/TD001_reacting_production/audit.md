# TD-001 — audit: reacting-species production reachability

Step A of the per-item protocol. **No production file was changed to produce this.**

```text
audit date   2026-10-03
git HEAD     3bd32f9
priority     P0 — blocks correctness / qualification
```

---

## 1. Root cause

`CLAUDE.md` §9 defines production capability as

```text
case format -> parser -> builder -> solver dispatch -> results/export -> CLI -> GUI (where required)
```

P18.2–P18.8 built **everything except the dispatch and the export**. The chemistry was qualified as
a library, and each phase correctly recorded the gap rather than fixing it inside another phase's
scope (§2) — which is why the same fact appears five times in `TODO.md` and once in
`KNOWN_LIMITATIONS` 1.4.

**The measured root cause, today:**

```text
grep -c 'setup.reacting' src/app/ProjectRunner.cpp   ->  0
grep -rln 'reacting' cases/                          ->  (nothing)
```

`ProjectRunner` — the one production entry point the CLI and the GUI both run through — has **no
reference to the reacting configuration at all**, and no case file declares one.

## 2. What already exists, and it is more than the register's §6 implies

A prior authorization in this session built the first half of the chain. It is uncommitted and has
**not** been through this campaign's gate protocol, so it is treated as existing material to be
audited, not as credit.

| chain link | state | evidence |
| --- | --- | --- |
| case format | **EXISTS** — `reacting` block with species, reactions, orders, Arrhenius triples, initial temperature | `include/cfd/io/case/PhysicsConfig.hpp:173-221` |
| parser / schema | **EXISTS** — 5 parse helpers, unknown-key rejection, `species`+`reacting` mutual exclusion | `src/io/case/PhysicsConfigParser.cpp` |
| builder | **EXISTS** — `ReactingSetup` carries live `SpeciesRegistry`, `MixtureThermodynamics`, `FormationEnthalpies`, `ReactionMechanism`, `ReactionKinetics`, `MassFractionState`, transport, initial `T` | `include/cfd/io/SimulationSetup.hpp:56-65`, `src/io/CaseBuilder.cpp` |
| **solver dispatch** | **MISSING — 0 references** | `src/app/ProjectRunner.cpp` |
| **results / export** | **MISSING** — no `Y_i`, `T` or reaction quantity is exported on this path | — |
| CLI | reached transitively once dispatch exists (`apps/cli/main.cpp` calls `ProjectRunner::run`) | — |
| **boundaries** | **MISSING, and it blocks transport** — see §4 | — |

Focused tests already passing on the existing half: `tests/unit/io/test_reacting_case.cpp`, 9 tests.

## 3. The carrier-species question is ALREADY DECIDED — record, do not redo

P18.1 recorded: *"a shipped species field is a passive **tracer**, not a composition;
`cases/species_diffusion` read as a one-species mixture is correctly rejected. P18.2 must decide how
a case declares the complementary/carrier species."*

**The decision taken, and where it lives:** the case file declares **every** species explicitly,
including the carrier/diluent, and the declared `initial_mass_fraction` set must sum to 1 within
`1e-12`. There is **no** implicit carrier and **no** normalisation.

```text
src/io/case/PhysicsConfigParser.cpp:651-662
  "...must have initial_mass_fraction summing to 1 (this codebase refuses an
   out-of-bounds composition and never normalises one)"
```

**Why this is the right decision rather than the convenient one.** The alternative — deriving a
complementary species as `Y_carrier = 1 - sum(others)` — would manufacture a composition that
satisfies P18.1's invariant *by construction*, which is exactly the silent normalisation §41 and
P18.1 forbid. A case file that does not sum to one is a case file with a mistake in it, and it is
refused with the measured sum in the message.

**Consequence for this item:** the carrier sub-item folded into TD-001 is **closed by an explicit
policy**, and the audit's job is to verify the policy is enforced and documented, not to invent a
second mechanism.

## 4. The genuine remaining gap, in dependency order

### 4.1 No multi-species boundary representation — blocks transport

P18.2 recorded: *"No multi-species boundary type, so `sum Y_i = 1` is not enforced across a shared
patch; the per-species boundary sets are independent objects."*

Measured now: `ReactingPhysicsConfig` has **no** boundary field and `ReactingSetup` has **no**
boundary member. `speciesTransportAssembly` requires one `BoundaryConditionSet` per species. So
**species transport cannot be driven from a case file at all** — this is not a quality issue, it is
a hard blocker, and it answers the authorization's conditional ("if production reachability requires
it") with **yes, it is required**.

The risk the shared representation must address: N independent `FixedValue` sets can declare a patch
composition that does not sum to 1, and nothing would detect it. P18.1 refuses such a state only at
a step boundary — i.e. after the solve has already used it.

### 4.2 No solver dispatch

`ProjectRunner` must consume `setup.reacting` following the **same one-way, best-available pattern**
every other optional physics block already uses (`ProjectRunner.cpp:532-701`): SIMPLE flow solve ->
`calculateMassFlux` -> thermal -> species -> multiphase -> export. The reacting block belongs in that
sequence, not in a parallel driver.

### 4.3 No outputs

The passive path exports `concentration_<name>` as a `NamedScalarField` and a `SpeciesRunMetadata`
row. The reacting path needs the analogous surface for `Y_i`, temperature and reaction quantities.

## 5. Dependencies and what TD-001 must NOT absorb

| needed by TD-001 | source | note |
| --- | --- | --- |
| conservative `rho Y` transport | P18.2 `speciesTransportAssembly` | reuse, do not reimplement |
| mixture properties / EOS | P18.3 | reuse |
| reaction sources | P18.4 | reuse |
| Arrhenius kinetics | P18.5 | reuse |
| heat release + `T` feedback | P18.6 | reuse |
| a chemistry time step | P18.6 `advanceClosedReactor` | **interim.** TD-002 replaces it. |

**Explicitly out of TD-001 scope**, to keep the item closable:

* the stiff integrator and `d(omega)/d(Y)` — **TD-002**;
* the linear-solver status/robustness audit — **TD-003**, though TD-001 must not *add* an unchecked
  solve;
* the premixed flame, flame-speed extraction, G8 — **TD-004/005/006**;
* `PremixedFlame.{hpp,cpp}` as it stands. It is a TD-004 artefact and TD-001 neither uses nor
  validates it.

**The step-budget limitation (TD-002) will apply to whatever TD-001 ships**, and TD-001's gate must
state that rather than hide it: a reacting case driven far enough will hit the `sum Y = 1` budget.

## 6. Current reproducer for the debt

```text
grep -c 'setup.reacting' src/app/ProjectRunner.cpp   ->  0
grep -rln 'reacting' cases/                          ->  (nothing)
```

To be captured as a pre-fix artefact in `reproducer.md` (step C), together with the stronger form:
a case file containing a `reacting` block, run through `ProjectRunner`, producing **no** species
output and **no** chemistry — i.e. silently ignored rather than refused.

## 7. Risk

| risk | severity | mitigation the gate must force |
| --- | --- | --- |
| Changing `ProjectRunner` perturbs every existing case | **HIGH** — it is the single production entry point for CLI and GUI | chemistry-off cases must be preserved; the new block must be gated on `setup.reacting.has_value()` |
| A patch composition not summing to 1 is accepted | HIGH | the shared boundary representation must refuse it at construction |
| An unchecked linear solve enters production (the TD-003 defect) | HIGH | every solve TD-001 adds must check status; TD-003 then audits all of them |
| Case-format backward compatibility | MEDIUM | committed cases must stay byte-identical (§9) |

## 8. Existing tests that constrain the change

```text
tests/unit/io/test_reacting_case.cpp        9   the existing half of the chain
tests/unit/io/                            325   case format + parser + builder preservation
tests/unit/app/                            32   ProjectRunner behaviour
tests/unit/species/                       339   the P18 library (must stay untouched)
```

`CFDIoTests` and `CFDAppLayerTests` are the preservation suites for this change; `CFDSpeciesTests`
must be unaffected because TD-001 adds no library behaviour.
