# P12-GRAD-002 Amendment A1 — audit of every original criterion

Written before A1 and before any source change. Classification vocabulary as authorized:
`valid unchanged` / `invalid threshold derivation` / `invalid test domain` / `invalid instrument` /
`not yet evaluated`.

The original gate (`../acceptance_gate.md`, sha256 `a46973ed…`) and its failure
(`../summary.md`) stand unchanged. Nothing below re-scores the original run.

| id | original requirement | classification | evidence and reason |
| --- | --- | --- | --- |
| **C1** | constant field: `max \|∇φ\| ≤ max(1e-13, 100 η)`, η = ε X/h | **invalid threshold derivation** | The quantity's floor is dimensional, not absolute. A Green–Gauss gradient of a constant field is `φ Σ_f S_f / V`; the closure `Σ_f S_f` is exactly zero (measured `closure/V = 0.000e+00` on every mesh), so what remains is face-value round-off amplified by `Σ_f\|S_f\|/V = 4/h` (2D). At L = 1e-3 that floor is ε·φ·4/h = 3.6e-11 while the frozen bound is 3.6e-13 — **100× below the floor**. The unchanged baseline measures the identical 6.939e-12 (`../logs/04`, `../logs/05`). No implementation can pass it. |
| **C2** | linear fields ≤ 1e-11 Cartesian, ≤ 1e-9 distorted, all cells | **valid unchanged** | Passed: 2.82e-15 exact Cartesian, 3.13e-14 translated 16², 4.74e-13 translated 64², 3.77e-10 Q16, 2.55e-15 3D 8³. Kept verbatim in A1. |
| **C3(a)** | quadratic field exact to ≤ 1e-11 in **all** cells on aligned Cartesian | **valid unchanged** | Passed exactly: **0.000e+00** on 2D exact Cartesian, 2D shoelace Cartesian and 3D 8³. Kept verbatim. |
| **C3(b)** | refinement order ≥ 1.8, within 0.1 of pre-GRAD-002 | **not yet evaluated** | Never run (the gate stopped at C1). Kept verbatim. |
| **C4** | translation agreement ≤ `max(1e-13, 200 ε (X/h))` **including** the offset (1234.5678, 987.6543) | **invalid test domain** | Two independent proofs that the failing rows are not the formulation: (i) interior cells, which never touch the new correction, fail identically to boundary cells (5.960e-08 vs 5.961e-08); (ii) the unchanged baseline reproduces the interior numbers to every printed digit (3.639e-01 at 256²). The cause is `createStructuredQuad2D`'s shoelace geometry: centroid error reaches 5.0e-04·h at X/h ≈ 2e4 and **4.119·h** at X/h ≈ 3e5 (`../logs/04`). The criterion asked the gradient to be translation-invariant on meshes whose own centroids are wrong by four cell widths. Split in A1 into **C4a** (valid-geometry domain, acceptance) and **C4b** (extreme coordinates, diagnostic only). *Also* mis-derived in its constant: the coefficient of the geometric term is ~192 measured on the baseline, against the `C ≤ 50` estimate the 200 factor was built on — a 4× error that the frozen bound only just covered. A1 replaces the whole expression. |
| **C5** | continuity sweep, `\|Δe\| ≤ 10 Δm + 1e-12`, with a negative control that must fail | **valid unchanged**; first instrument **invalid instrument** | The criterion is sound and **passes** (constant Lipschitz quotient 1.563e-02 across nine decades; `../logs/07`), and the negative control fails at the first step as required (`../logs/08`). The *first* instrument sheared boundary vertices, which silently swapped an exact Dirichlet condition for a Neumann one above amplitude ≈1.4e-6 and produced a spurious O(1) jump in **all three libraries including the baseline** (`../logs/04`–`06`, preserved). The corrected instrument shears strictly interior vertices and reports whether every prescribed boundary value is exact. Criterion text unchanged; rerun fresh under A1. |
| **C6** | scale invariance: dimensionless errors agree ≤ 1e-9 across L = 1e-3, 1, 1e3 | **invalid threshold derivation** | Inherits C1's mistake: it compares errors across physical scales without the ε·φ/h floor, which varies by 10⁶ across those three scales (8.9e-18 → 8.9e-12). Replaced in A1. |
| **C7** | static cavity reproducer ≤ 1e-9, 16² and 32² | **not yet evaluated** | Never run. Kept verbatim — its derivation (per-step O(η) difference, ≤ a few hundred× amplification over 20 implicit steps, 10× below MESH-007 G6.3's own 1e-8) is independent of the defects above. |
| **C8** | previous-phase verification against **each phase's own originally frozen** thresholds | **not yet evaluated** | Never run. Kept verbatim; it references thresholds frozen by earlier phases, not by me now, so it cannot carry this gate's derivation errors. |
| **C9** | 3D: C1–C4 and C6 on 3D meshes; MESH-005/006 suites | **not yet evaluated**, inherits corrections | Never run. Kept, with C1/C4/C6 read in their A1 form. |
| **C10** | interior cells bitwise identical where no skewed faces; ≤ 1e-13 otherwise | **not yet evaluated** | Never run formally. Consistent with the probe's interior columns matching the baseline to every printed digit. Kept verbatim. |
| **C11** | aligned-Cartesian equivalence ≤ 1e-13; case outputs byte-identical or bounded ≤ 1e-6 and explained | **not yet evaluated** | Never run. Kept verbatim. |
| **C12** | no geometric threshold branch remains in the boundary-gradient path | **valid unchanged** — PASS | Audit complete: the alignment and equal-area tests are gone, `FaceAlignment`/`boundaryFaceAlignment` deleted, `obliqueNeumannFace`'s exact-zero test replaced by a zero-effect iteration trigger. The one remaining exact-zero test in the path (`decomposeAreaVector`'s exact-parallel short-circuit) is in the documented benign class — its two branches agree exactly at the crossing. |
| **C13** | focused tests, full regression, sanitizers at CI settings | **not yet evaluated** | Never run. Kept verbatim. |
| **C14** | clang-format, rebuild, freeze the binary hash | **valid unchanged** — done | clang-format clean over all three changed files (also clearing five pre-existing MESH-007 violations); rebuilt library byte-identical (`51e82ee8e0ed1636…`). |

## Summary

- **Amended by A1 (3):** C1, C6 (invalid threshold derivation), C4 (invalid test domain, split into
  C4a acceptance + C4b diagnostic).
- **Unchanged and already passing (5):** C2, C3(a), C5, C12, C14.
- **Unchanged and still to run (7):** C3(b), C7, C8, C9, C10, C11, C13.

No production source was modified during this audit. The common root of all three invalid criteria is
the same: a threshold written as an absolute number for a quantity whose floating-point floor is
dimensional, and a mesh list extended past the domain where the inputs themselves are meaningful.
A1's replacements are therefore all expressed as **dimensional envelopes evaluated per mesh**, and
every A1 test is dry-run against the unchanged baseline before the freeze.
