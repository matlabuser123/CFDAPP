---
name: cfdapp-architect
description: Design before implementation for substantial CFDApp changes - problem, constraints, interfaces, ownership, data flow, failure behavior, compatibility, test strategy, acceptance gates and negative controls. Use before any non-trivial implementation.
---

# CFDApp architecture step

Produce a coherent design **before** writing implementation code. If the design does not hold
together, stop and report rather than implementing around the gap.

## Required content

```text
PROBLEM              what must be true afterwards that is not true now
CONSTRAINTS          governance, compatibility, performance, scope limits, what must not change
CURRENT ARCHITECTURE how it works today, with real file paths
PROPOSED             the smallest design that satisfies the problem
INTERFACES           signatures, headers, who may call what
OWNERSHIP            who owns each object; lifetime; copy/move semantics
DATA FLOW            inputs → transforms → outputs; where state is mutated
FAILURE BEHAVIOR     what happens on non-convergence, NaN/Inf, bad input, OOM; what is reported
COMPATIBILITY        case format, schema, committed cases, CLI/GUI, CPU/GPU, exports
TEST STRATEGY        what proves it works; which level of cfdapp-verify each check sits at
ACCEPTANCE GATES     the frozen criteria, with derived thresholds
NEGATIVE CONTROLS    what must fail, and how it will be made to fail
```

## Design rules

* **Smallest correct change.** Prefer a dedicated new path over conditionals bolted onto shared
  code (`CLAUDE.md` §6). If shared code must change, plan the proof that existing results are
  unchanged — bitwise where that is claimed.
* **The CPU path is the reference.** A GPU or OpenMP design must state how equivalence will be
  shown, and may not redefine CPU semantics unless explicitly authorized.
* **Production means the whole path** (`CLAUDE.md` §9): case format → parser → builder → dispatch →
  results/export → CLI → GUI. A library API alone is not a capability. If scope is library/API
  only, say so explicitly and record the limit.
* **Backward compatibility is the default.** Committed cases stay byte-identical unless a breaking
  change is separately authorized.
* Document every non-obvious constant: tolerance, relaxation factor, CFL limit, iteration cap.

## By change type

**Numerical** — the mathematical formulation comes first: continuous equation, discrete equation,
sign convention, units, boundary treatment, conservation statement, expected order. Hand off to
`cfdapp-numerics` before implementing. No implementation until the formulation is written down.

**Performance** — a **measured baseline is required before any optimization**, not after.
Profile first; do not optimize from intuition. Every optimization keeps all correctness gates, and
no tolerance may be loosened because a parallel or GPU path fails.

**GPU** — see `cfdapp-cuda`. Plan residency and transfers as part of the design, not as a later
fix; state which data stays on the device and for how long.

**I/O / case format** — plan the migration and the compatibility proof alongside the feature.

## Gate design

Acceptance gates are frozen **before** any production change (`CLAUDE.md` §5). At design time:

* name the exact quantity each threshold bounds, and **derive** its floor from that quantity's
  dimensional, round-off and discretization behaviour — if it scales with 1/h or a coordinate
  ratio, the threshold must scale too;
* label empirical constants as empirical;
* check criteria pairwise for contradiction, and check that every mesh in a criterion's list
  actually has the property the list is named for;
* include an exactly representable (dyadic) control that must give exactly 0 where that helps;
* plan the dry-run against the unchanged baseline, and the non-vacuity control that must fail.

A criterion the baseline passes vacuously has not been dry-run.
