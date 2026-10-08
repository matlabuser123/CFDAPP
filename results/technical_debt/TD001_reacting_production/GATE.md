# TD-001 — ACCEPTANCE GATE (frozen before implementation)

```text
item        TD-001  Reacting-species production reachability
priority    P0 — blocks correctness / qualification
origin      P18.2, restated P18.3 / P18.5 / P18.7; KNOWN_LIMITATIONS 1.4
audit       results/technical_debt/TD001_reacting_production/audit.md
frozen at   git HEAD 3bd32f9
```

**This file is frozen before the final measurements.** Its sha256 is recorded in
`logs/00_freeze.log` together with the production-source and test hashes it was frozen against. No
criterion in it may be weakened to obtain a PASS (`CLAUDE.md` §1.4, §5.3).

---

## 1. Requirement

A user-authored case file must be able to drive the **complete** reacting system through the
production path, and the proof must run through the real `ProjectRunner` entry point — **not** a
unit test that constructs the solver directly.

```text
case file -> parser/schema -> species definitions -> carrier policy -> mixture composition
          -> reaction mechanism -> Arrhenius parameters -> solver configuration
          -> reacting-flow dispatch -> species transport -> chemistry -> thermal coupling -> outputs
```

## 2. Criteria

Each criterion is measured, with the stated metric and threshold. Order is the order of execution;
**execution stops at the first failure** (§5.3).

| # | criterion | metric | acceptance threshold |
| --- | --- | --- | --- |
| C1 | a case file genuinely selects reacting species | a `cases/` case with a `reacting` block parses | parse succeeds; the mechanism it declares is the one built |
| C2 | the parser reaches the new configuration | `ReactingPhysicsConfig` populated from the file | every declared species, reaction, order and Arrhenius triple present, value-for-value |
| C3 | the mechanism is instantiated from configuration | `ReactingSetup.mechanism` built by `CaseBuilder` | `reactionCount`, `speciesCount`, per-reaction stoichiometry and orders match the file |
| C4 | the multi-species state is initialised correctly | `MassFractionState` from the file | constructs without refusal; `max_cell abs(sum_i Y_i - 1) <= 4*eps*N_species` (P18.1's own frozen invariant, not a new one) |
| C5 | **the solver dispatches through the production `ProjectRunner` path** | `ProjectRunner::run(caseDir)` on the case | returns a reacting result; **`grep -c 'setup.reacting' src/app/ProjectRunner.cpp > 0`** |
| C6 | **chemistry actually executes** | reaction progress over the run | `max_cell abs(omega_i) > 0` for at least one species, and the composition CHANGES: `max_cell abs(Y_i^end - Y_i^0) > 1e-12` |
| C7 | **energy coupling actually executes** | temperature response to heat release | `max_cell abs(T^end - T^0) > 1e-9 K`, with the sign matching the mechanism's `standardReactionEnthalpy` (exothermic => T rises) |
| C8 | outputs expose species / T / reaction quantities | exported artefacts | every `Y_i` present by name, `T` present, and at least one reaction quantity (heat release or a progress rate) present |
| C9 | an invalid mechanism fails clearly | malformed `reacting` blocks | each refused with a message naming the offending key/species/reaction; **no** silent acceptance, **no** partial build |
| C10 | a chemistry-off case preserves existing behaviour | a case with no `reacting` block | **bitwise identical** results to the pre-change build: exported fields, residuals and metadata (timing-only fields excluded) |
| C11 | patch compositions are valid | the shared multi-species boundary representation | a patch whose per-species boundary values do not sum to 1 is **refused at construction**, before any solve consumes it |
| C12 | no unchecked linear solve is added | every solve TD-001 introduces | `SolverStatus` checked; breakdown and non-finite never accepted as a solution |

### Thresholds — derivation, not selection

* **C4** reuses P18.1's frozen `4*eps*N_species`. TD-001 does not invent a composition tolerance.
* **C6/C7** use *existence* thresholds (`> 0`, `> 1e-12`, `> 1e-9 K`) deliberately. The question is
  "did chemistry run at all", not "is it accurate" — accuracy is P18.4–P18.6's, already qualified,
  and re-gating it here would be re-measuring a closed gate. A tight tolerance would be the wrong
  instrument: it would fail for accuracy reasons while the reachability question stayed unanswered.
* **C10 is bitwise** because the change must be purely additive. Anything less than bitwise on a
  chemistry-off case means `ProjectRunner` behaviour moved for every existing user.
* **C7's sign check** exists because a magnitude alone would pass for a heat *sink*; the sign ties
  the temperature response to the declared thermochemistry.

## 3. Negative controls

A criterion that cannot fail proves nothing. Each control must be **demonstrated to fail** the
criterion it guards.

| id | control | must produce |
| --- | --- | --- |
| N1 | remove the `reacting` dispatch from `ProjectRunner` (revert C5's hook only) | C5, C6, C7, C8 all FAIL — this is the pre-fix state and is the primary reproducer |
| N2 | set every `pre_exponential` to 0 in the case file | C6 FAILS (`omega == 0`, composition frozen) while C1–C5 still pass — proves C6 measures chemistry and not parsing |
| N3 | set every `formation_enthalpy` to 0 | C7 FAILS (`T` unchanged) while C6 still passes — proves C7 measures the ENERGY coupling, not merely that chemistry ran |
| N4 | declare a patch whose species boundary values sum to 0.9 | C11 FAILS to construct — proves the shared boundary representation is load-bearing |
| N5 | declare `initial_mass_fraction` summing to 0.9 | C4 refused by the parser with the measured sum — proves the carrier policy is enforced, not documented only |

**N2 and N3 are the two that matter**, because C6 and C7 are the criteria most easily satisfied
vacuously: a case that merely *parses* a mechanism would pass a weaker version of both.

## 4. Regression scope

| suite | reason | expectation |
| --- | --- | --- |
| `CFDIoTests` | case format, parser, builder | all pass; **325 baseline** |
| `CFDAppLayerTests` | `ProjectRunner` behaviour — the file being changed | all pass; **32 baseline** |
| `CFDSpeciesTests` | the P18 library must be untouched | all pass; **339 baseline** |
| committed `cases/` | §9 backward compatibility | existing case results byte-identical |

A changed count in any of these three baselines is a regression and stops the item.

## 5. Definition of done

TD-001 is RESOLVED when, and only when:

1. C1–C12 all PASS, measured, with the numbers recorded;
2. N1–N5 each demonstrated to FAIL the criterion it guards;
3. the three regression baselines are unchanged (325 / 32 / 339);
4. `git diff --check` introduces no new warnings beyond the three pre-existing CRLF files (TD-018);
5. evidence written: `audit.md`, `GATE.md`, `reproducer.md`, `implementation.md`,
   `negative_controls.md`, `verification.md`, `regression.md`, `summary.md`;
6. `DEBT_REGISTER.md` updated OPEN -> RESOLVED **after** the above, not before.

## 6. Explicitly NOT in this gate

So that TD-001 is closable and does not silently absorb its successors:

* stiff / implicit chemistry, `d(omega_i)/d(Y_j)`, the `sum Y = 1` step budget — **TD-002**. A
  reacting case driven far enough **will** hit the budget; that is a stated limitation of what
  TD-001 ships, not a TD-001 failure.
* the repository-wide reacting-flow linear-solver audit — **TD-003**. C12 only forbids TD-001 from
  *adding* an unchecked solve.
* premixed flame, flame-speed extraction, G8 closure — **TD-004 / TD-005 / TD-006**.
* `PremixedFlame.{hpp,cpp}`. It is a TD-004 artefact; TD-001 neither uses nor validates it, and its
  open fuel-balance anomaly is not TD-001's to answer.
* any performance claim. None is made (`CLAUDE.md` §4 would require end-to-end measurement).
