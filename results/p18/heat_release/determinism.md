# P18.6 — determinism (§48)

```text
ACCUMULATION ORDER IS FIXED BY CONSTRUCTION throughout P18.6: ascending species index, ascending
reaction index, ascending cell, over dense vectors. No reduction, no atomic, no parallel region,
no map iteration anywhere in src/species/HeatRelease.cpp.

THAT IS A PROPERTY OF THE CODE, NOT A MEASUREMENT RESULT -- but it is measured anyway, because
"there is no reduction" is a claim about code someone will later change.
```

---

## 1. Measured

| route | scope | bitwise differences |
|---|---|---:|
| probe (`logs/06_probe.log`) | 200 repeats × 64 cells × (`S_E` + `T` + 3 `Y`) + total energy | **0** |
| test (`TheWholeCoupledStepIsBitwiseDeterministic`) | 50 repeats × 64 cells × (`S_E` + `T` + `rho` + 3 `Y` + 3 `omega`) + 3 energies | **0** |

Comparison is `EXPECT_EQ` / `!=` on doubles — **exact equality, not `EXPECT_NEAR`**. A tolerance
here would defeat the purpose.

## 2. Everything is rebuilt on every repeat

```text
BOTH ROUTES RECONSTRUCT THE MIXTURE, THE FORMATION ENTHALPIES AND THE MECHANISM EACH TIME, rather
than reusing one set of objects. That is the point, and it is what the P18.5 determinism test
established as the right pattern:

  - FormationEnthalpies binds to the registry and stores a dense vector. If that binding were
    order-dependent -- for example if it used an unordered container -- reusing ONE object would
    hide it completely, and every repeat would agree while two separately built objects disagreed.
  - ReactionMechanism canonicalises net stoichiometry at construction.
  - MixtureThermodynamics derives R_i = R_u/M_i at construction.

A determinism test that reuses its inputs measures only that the arithmetic is deterministic, which
was never in doubt. Rebuilding measures that the SETUP is too.
```

## 3. What is covered, quantity by quantity (§48)

Section 48 lists the quantities to compare. All of them, in one or both routes:

| quantity | covered |
|---|---|
| `Y_i` | yes — 3 species × 64 cells |
| reaction rates | yes — the rates are an input, and `omega_i` derived from them is compared |
| `omega_i` | yes — 3 species × 64 cells (test route) |
| reaction enthalpy | yes — implicitly, via `S_E`, which is `-q ΔH` |
| `S_E` | yes — 64 cells, both routes |
| energy | yes — `E_total`, `E_sensible`, `E_chemical` separately |
| `T` | yes — 64 cells |
| mixture properties | yes — via `E_sensible`, which is `rho cp(T,Y) T` |
| `dt`, state histories | n/a — `dt` is an input constant; the march is one step per repeat |

```text
THE THREE ENERGY POOLS ARE COMPARED SEPARATELY, not only their sum. Two pools could drift in
opposite directions and leave the total bitwise identical, so comparing only E_total would be a
weaker check than it looks.
```

## 4. What determinism does NOT cover

```text
SINGLE-CONFIGURATION, SINGLE-THREAD. These runs are Release, CUDA off, host code, one thread.

P18.6 HAS NO GPU PATH AND NO OpenMP REGION -- section 47 says CPU numerics remain authoritative and
that broad CUDA chemistry/energy work is P18.8's, not P18.6's. So there is nothing to establish
CPU/GPU equivalence for. That is a statement about today's code, which is why the CPU-ONLY
configuration is in the regression: a file reachable only under one build option is compiled by no
routine gate, and that blind spot has caught this project before.

CROSS-CONFIGURATION BITWISE EQUALITY IS NOT CLAIMED. Different optimisation levels may contract
a*b+c into an FMA differently. Claiming bitwise equality across build types would be claiming
something neither measured nor required -- and the regression's three configurations are run to
check COUNTS and PASS/FAIL, not to compare bits between them.

G9 (final CPU/GPU equivalence) IS NOT CLAIMED. Section 51 forbids it.
```

## 5. The one place determinism is load-bearing for a gate

```text
The non-reacting limit (section 34) requires BITWISE equality after ten zero-rate steps. That is a
determinism claim and a no-op claim at once, and it FAILED against the first implementation at
~1 ulp per step. The fix made a zero increment an exact no-op rather than a near-identity --
nonreacting_limit.md sections 1-2.

So determinism here is not a formality appended at the end: it is the mechanism by which the
backward-compatibility gate is even expressible.
```
