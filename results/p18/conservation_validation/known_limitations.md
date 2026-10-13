# P18.7 — known limitations (§62)

```text
DEMONSTRATED, NOT SPECULATIVE. Section 62: "Do not list speculative limitations." Every item below
is backed by a measurement or a grep recorded in this directory.
```

---

## 1. G8 is not closed — no flame-speed validation

```text
G8 asks for "a 1D laminar premixed flame: flame speed, peak temperature, species profiles" against
a PUBLISHED SPREAD. It cannot be closed in P18.7's scope.

MEASURED:
    files outside species/ referencing the chemistry layer : 0
    case-file keys defining a mechanism                    : 0

Nothing drives the chemistry. Closing G8 requires BUILDING a coupled reacting-flow solver, a stiff
or sub-stepped chemistry integrator, mixture transport data for a real mechanism, and a
flame-speed eigenvalue extraction -- none of which exist.

Evidence: audit.md section 1. NOT weakened and NOT restated as something easier.
```

## 2. Elemental conservation is not modeled

```text
MEASURED: files carrying elemental composition = 0.

Section 9 anticipates this and forbids inventing a database to satisfy the checkbox. Sections 10
and 39 are therefore N/A. P18.4 deferred it for the same reason.
```

## 3. No stiff chemistry integrator

```text
Carried forward from P18.6. P18.7's batch reactor is kept INSIDE the explicit method's legitimate
stability envelope, and that is ASSERTED rather than assumed:

    StaysInsideTheExplicitStabilityEnvelope measures k at the initial and final temperatures and
    requires every dt used to satisfy dt < 0.05 * (2/k_final), the explicit-Euler limit.
    It also measures k_final/k_initial < 100, i.e. the case is NOT stiff.

Section 21 forbids stretching an explicit integrator beyond its envelope to produce a dramatic
test, and forbids hiding instability behind undocumented tiny time steps. Neither was done.
```

## 4. The explicit march has a measured sum-to-one step budget

```text
P18.6 measured this for three species. P18.7 measured it for TWO, and it is TIGHTER:

    tolerance = 4 * eps * N_species = 8 eps for two species, against 12 eps for three.

A 10000-step march at dt = 1e-4 was REFUSED by P18.1:
    "mass fractions at cell 0 sum to 1.000000000000002; must be within 1.7763568394002505e-15 of 1"

1000 steps at dt = 1e-3 is comfortably inside the budget and is what the checkpoint comparison
uses. TheSumToOneBudgetBoundsTheMarch pins both halves: the march survives inside the budget and
IS refused outside it.

SECTION 41 OF P18.6 FORBIDS THE RENORMALISATION THAT WOULD EXTEND IT, so P18.1 refusing the state
is the correct behaviour rather than a defect. The consequence for validation is real and is
stated: how far a batch reactor can be integrated is bounded by a state invariant, not by accuracy.
```

## 5. Low-Mach / incompressible-regime reacting flow only

```text
The plug-flow benchmark is subsonic by construction (U = 2 m/s) and nothing approaches P17's
compressible envelope. P18.6 coupled heat release to the P15 THERMAL equation only, and P17's
TotalEnergyState still refuses a temperature-dependent cp -- so a compressible reacting case is
not merely untested, it is unrepresentable.

NO REACTING SHOCKS, NO DETONATIONS, NO SUPERSONIC COMBUSTION, NO 3D REACTING FLOW. None is
implemented and none is claimed.
```

## 6. Separate-process determinism not measured (§43)

```text
Section 43 asks for it "where practical and part of CFDApp's determinism expectations" and adds
"If not part of the frozen gate, report but do not invent a new requirement after measurement."

G10 does not require it. It was NOT measured, and that is recorded here rather than left implied.
In-process determinism IS measured and is bitwise, including the outer-iteration count.
```
