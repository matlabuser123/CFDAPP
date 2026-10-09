# P17.8 — CPU/GPU: NOT IMPLEMENTED

Covers the authorization's §32 and §33, and gate criterion **G8**.

---

## 1. The claim

```text
There is NO GPU density-based solver in P17.8.

G8 is reported NOT IMPLEMENTED. It is not reported as PASS, and it is not reported as satisfied by
the flux layer's existing equivalence. "Nothing to compare, therefore equivalent" is the vacuous
pass P17_GATES.md's own G8 text forbids by name.
```

## 2. Why this is in scope to decide, and not a shortfall

Both authorization sections are **conditional**:

```text
§32  "Use the P17.7 CPU/GPU HLLC implementation WHERE AVAILABLE."
§33  "IF the GPU density-based solver is implemented in P17.8, compare ..."
```

and the frozen gate matrix assigns P17.8 the rows `G1 G3 G4 G6 G9`. **G8 is not in that row.**
`architecture.md` §8 recorded the decision before any source was written, so this is a scope
boundary declared in advance, not a task dropped when it got hard.

## 3. What IS already qualified, and what is not

```text
ALREADY QUALIFIED, from P17.7 -- and inherited unchanged by P17.8:
  the HLLC flux itself is BITWISE IDENTICAL on CPU and GPU, 17/17 rows, maximum difference
  EXACTLY 0. That holds because HllcFlux.hpp is marked CFD_HLLC_HOST_DEVICE and compiled by both
  gcc and nvcc from ONE source; the .cu file contains 0 formulas.

NOT PORTED, and therefore not qualified:
  * the residual assembly -- it walks the mesh, which needs device-resident face and cell topology
  * the CFL reduction    -- min over cells, whose ORDER is exactly what would need proving
  * the conserved update -- needs device-resident conserved fields
  * the positivity check and the retry policy, whose control flow would have to match bitwise
```

The CFL reduction is the interesting one and is worth naming explicitly: `globalTimeStep` is a
`min` over cells taken in serial cell-id order. A device reduction over the same values in a
different order gives the same answer for `min` (it is associative and commutative in IEEE double,
unlike a sum) — but the retry loop's `dt *= 0.5` and the acceptance decision that follows are
control flow driven by that value, so a future GPU port must prove the decisions match, not just the
number.

## 4. Was the flux re-verified after the star-energy correction?

Yes, and this matters because P17.8 changed `HllcFlux.hpp` — the single source both paths compile.

```text
logs/08_focused.log, Release+CUDA:   74 / 74 passed, including all 39 P17.7 Hllc rows,
                                     of which 4 are the GPU equivalence rows.
```

Because the correction was made in the shared `CFD_HLLC_HOST_DEVICE` header rather than in the
`.cu`, both paths received the same change from the same line of source, and P17.7's bitwise
equivalence criterion re-ran and still passes. That is a property of the one-source design, not a
coincidence.

## 5. What P17.10 inherits

```text
inherited PASSING   the flux layer's bitwise CPU/GPU equivalence (P17.7, re-verified here)
inherited OPEN      residual assembly, CFL reduction, conserved update, positivity control flow
inherited AS DEBT   nothing -- no partial GPU solver was written, so there is no half-ported path
                    to audit. P17.10 starts from a clean CPU reference.
```

The CPU implementation is the reference, as `CLAUDE.md` §6 requires. Any future GPU density-based
solver must preserve identical flux formulas (already shared), state conversion, CFL rules,
positivity criteria, residual definitions and convergence criteria — and must be compared on the
four case types §33 names (uniform flow, smooth flow, moving contact, short discontinuity
evolution), all four of which now exist as CPU tests and can be pointed at a GPU path unchanged.
