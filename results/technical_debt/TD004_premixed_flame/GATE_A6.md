# TD-004 GATE AMENDMENT A6 — C6a only

```text
amends        C6a of GATE.md (12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73)
authorized    2026-10-10, both clauses, after the §5.3 investigation in amendment_A6_proposal.md
supersedes    C6a's "fuel balance at every level < 0.02" and its "observed order p in [1.5, 2.5]"
leaves intact C1, C2/C2-A1, C3, C4, C5 (as amended by GATE_A3), C6, C6b, C7, and C6a's
              admissibility clauses (every level converged, > 4 cells of front, differences shrink,
              no sign change) — all four of which the post-fix tree already satisfies
status        DESIGN. NOT FROZEN. The dry-run and non-vacuity check are dry_run_A6.md; this file's
              sha256 is frozen in logs/A6_00_freeze.log only after they pass.
```

---

## 1. Why C6a is amended rather than failed, and why it is amended rather than deleted

`GATE.md` §1 records the dry-run as covering *"C1 and its controls N2/N3"*. **C6a was never dry-run.**
Both clauses amended here are defective in ways that only became visible once the gate reached them:

```text
the balance clause   fails on the BASELINE at all three levels (0.4703 / 0.5457 / 0.0433 against
                     a flat 0.02). CLAUDE.md §5.2: "A sanity criterion that the baseline fails is a
                     broken criterion." §2 derives the replacement.
the order clause     is computed from successive differences that the locator fix shrank by ~8x,
                     onto the eigenvalue lattice TD-043 derived. Its finer difference is now TWO
                     lattice quanta, so the order's uncertainty interval is (2.00, 3.81) and the
                     band's own edge lies inside it. §3 derives the replacement.
```

**And C6a is not deleted**, because the cheap triplet it introduced is forced rather than chosen:
`logs/A6_02_thousand_cell.log` tested the expensive `{250, 500, 1000}` family against the real
`ctest --timeout 7200` cap and it was **killed at exit 124**. `logs/07` had ruled it out at 1750 s
on a pipeline's masked `exit 0`; both of those defects are corrected and the conclusion survives.
So a cheaper family is unavoidable, and its criteria must be correct for it.

## 2. C6a-bal — the balance bound scales with the mesh

```text
fuel balance(cells)  <  0.02 * (500 / cells)^2
```

**Derivation.** The fuel balance residual is a discretisation error. Measured on the fixed tree:

```text
0.1505 / 0.0283 = 5.318  ->  order 2.411
0.0283 / 0.0063 = 4.492  ->  order 2.167
```

`CLAUDE.md` §5.1: *"If the quantity scales with 1/h or a coordinate ratio, the threshold must scale
too."* A flat `0.02` applied across a 4x mesh range is dimensionally wrong, which is exactly why the
baseline fails it at every level.

`h^2` is used rather than the measured 2.17-2.41 **deliberately**: it is the scheme's formal order,
so the bound is looser than the observed convergence and therefore cannot pass a solver whose
balance converges more slowly than second order. Fitting the exponent to the measurement would be
deriving a threshold from the observed output, which §5.3 forbids.

```text
cells   bound
  125   0.3200
  250   0.0800
  500   0.0200    <- UNCHANGED. This is the value C3 calls "the existing test threshold, neither
                     weakened nor tightened", so the reference configuration is gated as before.
```

**This is a dimensional correction, not a relaxation.** The baseline fails all three levels under the
new bound as well.

## 3. C6a-ord — an order may only be claimed when it is RESOLVED

```text
(a) RESOLUTION PRECONDITION. Both successive differences must exceed 4 lattice quanta, where one
    quantum is (2^(1/1024) - 1) of S_L at the middle level -- the eigenvalue lattice derived in
    TD043_eigenvalue_lattice/audit.md §1.
    If either difference is below 4 quanta, the observed order is UNRESOLVED and C6a-ord is
    RECORDED AS UNRESOLVED. That is a STOP. It is NOT a pass.

(b) WHEN RESOLVED, the order's FULL UNCERTAINTY INTERVAL must lie within [1.5, 2.5]:
        p_lo = log2( (n1 - 1) / (n2 + 1) )        p_hi = log2( (n1 + 1) / (n2 - 1) )
    where n1, n2 are the two differences in quanta.
```

**Derivation of the 4-quantum floor.** The reported eigenvalue is the terminal bracket's geometric
centre, so it lies within ±0.5 quantum of the true root and an `N`-quantum difference bounds the
true difference to `(N-1, N+1)`. Propagating that, `dp ~ (1/ln 2)(1/n1 + 1/n2)`. For the interval to
be narrower than `0.5` with `n1 ~ 13`, one needs `n2 >= 4`. The floor is derived from the lattice and
the target interval width, not from the observed `n2 = 2`.

**Derivation of (b).** The band `[1.5, 2.5]` is UNCHANGED — it is not re-derived, and deriving a new
band around the measured `p = 2.69` would be fitting a threshold to an observation. What changes is
that the criterion must now prove the order is in band rather than estimate it there. **Requiring
the whole interval inside the band is STRICTER than the point-estimate test it replaces.**

## 4. What this amendment does NOT do

```text
does NOT weaken  the order band. [1.5, 2.5] is untouched, and (b) is strictly harder to satisfy
                 than the point test. A resolved p of 2.69 FAILS under both.
does NOT change  the 500-cell balance bound, C3's reference threshold.
does NOT relax   C6a's four admissibility clauses. They are carried over verbatim.
does NOT unblock the gate. Applied to the post-fix tree, C6a-bal PASSES and C6a-ord is UNRESOLVED,
                 so the gate still stops at C6a -- now for a stated reason instead of a meaningless
                 one. C6b and C7 remain unevaluated.
does NOT touch   production. No source file is changed by this amendment.
does NOT settle  whether p ~ 2.69 is the family's true order. Resolving that needs a finer
                 `settings.tolerance` (TD-043), which is a production change outside this scope and
                 which the evidence suggests would CONFIRM the band violation rather than clear it.
```

## 5. Dry-run and non-vacuity — REQUIRED BEFORE FREEZING

`dry_run_A6.md` evaluates both clauses against every triplet/library combination already measured,
from two libraries. Summary of what it must establish:

| | order clause | balance clause |
| --- | --- | --- |
| pre-fix `{125,250,500}` | **PASS** — p 2.057, interval (1.99, 2.12) | **FAIL** at all three |
| post-fix `{125,250,500}` | **UNRESOLVED** — n2 = 2.0 | **PASS** at all three |
| pre-fix `{63,125,250}` | **FAIL** — p 0.523, interval (0.50, 0.54) | — |
| post-fix `{63,125,250}` | **FAIL** — p 3.176, interval (3.06, 3.30) | — |

**All four verdict states are demonstrated on real measurements**, so neither clause is vacuous. The
order clause can pass, can fail, and can report UNRESOLVED; the balance clause fails the baseline and
passes the fixed tree.

## 6. Execution order, and the stop rule

```text
1  dry-run both clauses on both libraries and both triplets          -> dry_run_A6.md
2  confirm non-vacuity: each clause must be shown able to FAIL
3  ONLY THEN freeze this file's sha256, with the source and library hashes, in logs/A6_00_freeze.log
4  re-run FRESH on the post-fix library and record the verdict, reusing no earlier result (§5.3
   step 6). The dry-run's post-fix rows do NOT serve as the verification.
```

**C6a-ord returning UNRESOLVED is a stop, and the gate does not proceed past it.** Recording an
unresolved criterion as satisfied would be exactly the selective reading this campaign exists to
prevent.
