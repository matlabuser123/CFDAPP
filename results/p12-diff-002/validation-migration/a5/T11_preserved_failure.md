# A5-1 — the T11 failure, preserved as historical evidence

The ThermalInterface fix phase ended:

```text
BLOCKED AT T11
```

That result is **preserved exactly**. `results/p12-diff-002/thermal-interface-fix/acceptance_gate.md`
is not rewritten (sha256 `9944d066659d4127cbfdba5246f67b023461738fb9e9f5cd96d0d289258e8870`), and
neither is its report `summary.md` §8 nor any of its logs `00`–`13`.

## What the record shows

```text
T11 correctly prevented automatic closeout;
0 new failures were introduced by the ThermalInterface fix;
the remaining failure set contains previously identified migration/policy items;
one frozen M-A classification was subsequently proven incorrect.
```

## Supporting measurements, unchanged

| fact | evidence |
| --- | --- |
| T11's criterion was not weakened and not declared passed | `thermal-interface-fix/summary.md` status block, verdict `BLOCKED AT T11` |
| 0 new failures: the post-fix failure set is a strict **subset** of the pre-fix one | `thermal-interface-fix/logs/11_T11_failure_set_comparison.log` |
| 9 tests were **fixed** by the change (7 new reproducer cases + 2 detectors) | same log, "FIXED by this change" |
| the pre-fix comparison used the hash-verified pre-fix library `58be6b75…` | `logs/10_T11_prefix_attribution.log`, `logs/03_T1_bitwise.log` |
| 14 remaining failures, all in the frozen inventory: 11 **M-A** + 3 **U-H** | `logs/08_T11_regression.log` |
| the frozen **M-A** classification of `RegionAwareThermalDiffusionTest.`<br>`EqualConductivityMatchesSingleMaterialPathExactly` was proven incorrect — it passes untouched once the production defect is fixed | `logs/11_T11_failure_set_comparison.log`, "FIXED by this change" |

That last row is the reason A5 exists: a failing test inside a "known-obsolete instrument" inventory
is not thereby proven obsolete. The inventory was written before the defect was known, so it cannot
be treated as automatically authoritative.

## Status of the gate being amended

A5 does **not** amend `thermal-interface-fix/acceptance_gate.md`. T11 is re-run in A5-9 **exactly as
frozen**, against its original criterion. Whatever it then reports is recorded alongside — never in
place of — this initial failure.
