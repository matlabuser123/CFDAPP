---
name: cfdapp-blast-radius
description: Determine what a proposed CFDApp change can reach - callers, tests, case formats, numerical operators, CPU/GPU paths, CLI/GUI, compatibility, performance and previously-recorded evidence. Use before any non-trivial edit, and to decide how much verification the change earns.
---

# CFDApp blast radius

Run **before** editing. Its output decides the verification ladder in `cfdapp-verify`, so producing
it is cheaper than guessing wrong in either direction.

## Determine

```text
direct callers · indirect callers · affected tests · affected case formats
affected numerical operators · affected CPU/GPU paths · affected GUI/CLI paths
backward compatibility · performance implications · evidence potentially invalidated
```

## How to find them

```bash
# direct callers of a symbol
grep -rn "symbolName" include/ src/ cuda/ apps/ tests/ benchmarks/

# which tests cover a file's area (one CFD<Area>Tests target per area)
ls tests/unit/<area>/ tests/integration/<area>/ tests/solver/<area>/
ctest --preset debug -N -R '<Area>'

# case-format reach
grep -rn "<jsonKey>" src/io/case/ include/cfd/io/case/ cases/ tests/data/cases/

# does a GPU path share this code?
grep -rn "<symbol>" cuda/ include/cfd/gpu/ src/gpu/

# which recorded evidence cited this behaviour?
grep -rln "<symbol or behaviour>" results/*/summary.md
```

## Known high-reach points

Changing any of these reaches far more than its file:

| Touching | Also reaches |
| --- | --- |
| `src/app/ProjectRunner.cpp` | **CLI and GUI both** — it is the single shared backend |
| `src/pressure_velocity/SIMPLE.cpp` | every steady case, and ProjectRunner's authoritative status |
| `src/algebra/LinearSolverFactory.cpp` | CPU **and** GPU backend selection, and the fallback counter |
| `src/algebra/{CG,BiCGSTAB}.cpp` | every solve; the GPU mirrors in `cuda/kernels/` may now diverge |
| `src/discretization/Gradient.cpp`, `Convection.cpp`, `Diffusion.cpp` | all physics — flow, thermal, species, turbulence, multiphase, compressible |
| `src/mesh/MeshGeometry.cpp` | every mesh kind: Cartesian, graded, multi-block, 3D |
| `src/io/CaseReader.cpp`, `src/io/case/*` | all 21 `cases/` and 16 `tests/data/cases/` fixtures — compatibility risk |
| `include/cfd/gpu/*.hpp` | `cfdcuda` **and** the CPU stubs in `src/gpu/` — both must stay consistent |
| `cmake/*.cmake`, `CMakePresets.json` | every build and every CI job |

## Output format

Keep it short — this is a decision aid, not a report:

```text
CHANGE:              <what, in one line>
DIRECT IMPACT:       <files/symbols that call it>
INDIRECT IMPACT:     <what those reach — physics, CLI/GUI, backends>
TEST IMPACT:         <test targets and fixtures that must run>
NUMERICAL RISK:      <can a computed value change? bitwise-identical claim possible?>
PERFORMANCE RISK:    <hot path? transfer count? allocation in a loop?>
COMPATIBILITY RISK:  <case format, schema, export, committed cases, API>
EVIDENCE AT RISK:    <which results/<phase>/ conclusions would need rerunning>
VERIFICATION EARNED: <the cfdapp-verify levels this implies>
```

## Judging risk

* **Numerical risk exists** whenever a computed value *could* change — including a refactor
  believed to be neutral. If the claim is "bitwise identical", that is a claim to be **proven**,
  not assumed.
* **Shared-code changes** (`CLAUDE.md` §6) must prove existing behaviour unchanged. Prefer a
  dedicated new path over conditionals bolted onto shared code.
* **Evidence at risk** matters: a change that invalidates a prior phase's conclusion must say so.
  Do not silently leave a `results/` summary asserting something no longer true — report it.
* If the blast radius turns out to reach outside the authorized scope, **stop and report**. Do not
  expand scope to cover it.
