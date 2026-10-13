# TD-001 — reproducer: the debt demonstrated on the PRE-FIX implementation

Step C of the protocol. Everything here was measured **before** any fix, against the sources hashed
in `logs/00_freeze.log`.

```text
build      build/final, target cfdapp (the production CLI), build exit 0
case       cases/reacting_plug_flow/   -- the first case in the repository declaring a mechanism
entry      ./build/final/apps/cli/cfdapp --case <dir>     (the real production path)
```

The case reuses **P18.7's qualified reference problem** — `A -> B`, `A = 5.0`, `Ea = 30000 J/mol`,
`M = 0.028` for all three species, `cp = 1000`, `h_f,B = -2e6 J/kg`, `T_0 = 1000 K`, inert `N2`
carrier — so the chemistry it asks for is already validated against an independent
arbitrary-precision implementation. Nothing about the chemistry is new; only the path to it.

---

## The debt reproduces in three distinct forms

### V1 — a per-patch species composition is REFUSED

```text
$ cfdapp --case cases/reacting_plug_flow
Case configuration error: cases/reacting_plug_flow/boundaries.json: field "species" must satisfy
be a recognized field of boundaries.json patch "bottom"; received unknown field
exit 2
```

`boundaries.json` gates the per-patch `species` map on the **passive** `physics.species` list. A
`reacting` block does not enable it, so a reacting case cannot declare the boundary compositions its
own transport requires.

**Log:** `logs/02_prefix_variant1.log`

### V2 — a per-patch temperature is REFUSED

```text
$ cfdapp --case <species boundaries stripped>
Case configuration error: boundaries.json: field "temperature" must satisfy be a recognized field
of boundaries.json patch "bottom"; received unknown field
exit 2
```

Same shape: `temperature` is gated on a `thermal` block. A `reacting` block does not enable that
either — so the energy coupling the mechanism needs has no boundary surface.

**Log:** `logs/03_prefix_variant2.log`

### V3 — with flow-only boundaries the case RUNS and the mechanism is SILENTLY IGNORED

This is the form that matters, because nothing refuses it.

```text
$ cfdapp --case <species and temperature boundaries stripped>
Results:
  JSON:      results/metadata.json
  Residuals: results/residuals.csv
  CSV:       results/fields.csv
  VTK:       results/solution.vtk
```

The run completes and exports a full result set. Inspecting it:

```text
metadata top-level keys : application build_identity case compressible conservation
                          format_version mesh mesh_quality multiphase numerics physics
                          residuals robustness solver species thermal version
metadata "species"      : []                       <-- EMPTY
keys mentioning react / mechan / chem / omega / mass_fraction : NONE
```

**`physics.json` declares three species and a one-step mechanism. The production output contains
`species: []` and no reaction quantity of any kind.** The configuration is parsed, validated, built
into live `ReactionMechanism` / `ReactionKinetics` objects by `CaseBuilder` — and then dropped,
because `ProjectRunner` never reads `setup.reacting`.

The CLI's exit 3 is a flow non-convergence on this coarse mesh with these solver settings; it is not
the reacting failure and is not what this reproducer measures. The measured fact is `species: []`
beside a declared mechanism.

**Log:** `logs/04_prefix_variant3.log`

---

## Pre-fix measurements against the frozen gate

| gate criterion | pre-fix result |
| --- | --- |
| C1 case selects reacting species | the file does; **V1/V2 refuse it**, V3 drops it |
| C2 parser reaches the configuration | **PASS already** (pre-existing half of the chain) |
| C3 mechanism instantiated | **PASS already** — `CaseBuilder` builds it, then it is discarded |
| C4 multi-species state initialised | **PASS already** |
| C5 dispatch through `ProjectRunner` | **FAIL** — `grep -c 'setup.reacting' src/app/ProjectRunner.cpp` = **0** |
| C6 chemistry executes | **FAIL** — composition unchanged; no `omega` computed anywhere |
| C7 energy coupling executes | **FAIL** — no reacting temperature solve |
| C8 outputs expose species / T / reaction | **FAIL** — `species: []`, no reaction key |
| C9 invalid mechanism fails clearly | partially PASS (parser-level); untested through the CLI |
| C10 chemistry-off preserved | baseline to be captured before the fix |
| C11 patch compositions valid | **FAIL** — no representation exists to validate |
| C12 no unchecked solve added | n/a pre-fix |

**Seven criteria fail. C2–C4 already pass**, which is why the register records the first half of the
chain as existing: the debt is specifically dispatch, boundaries and outputs, not parsing.

---

## Why V3 is the dangerous one

V1 and V2 fail closed — a user gets a clear error and cannot proceed. V3 does not: it produces a
complete, plausible, exported result set for a case that asked for combustion and got none. Nothing
in the output says the mechanism was ignored.

That is the precise sense in which `CLAUDE.md` §9 is violated: the capability is *verified* and
*unreachable*, and the unreachability is **silent** at the one entry point users actually run.
