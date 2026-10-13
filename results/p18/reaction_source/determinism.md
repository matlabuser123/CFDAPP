# P18.4 — determinism (§29, §30)

```text
SCOPE: reaction construction, canonicalisation and source evaluation.
```

---

## 1. Measured

```text
200 repeats x 4 species x 256 cells, source evaluation      0 bitwise differences
50 repeats, RECONSTRUCTING the mechanism each time          0 bitwise differences
    -- so parsing and canonicalisation are included, not only evaluation
net stoichiometry after reconstruction                      bitwise identical
```

Bitwise, not `EXPECT_NEAR`: determinism is not an accuracy property, and a tolerance would let a
genuine nondeterminism hide.

## 2. Why it holds — the mechanisms, named

```text
DENSE STORAGE           the net stoichiometry is a dense vector indexed by species index. There is
                        NO map to iterate for arithmetic, so there is no ordering to be accidental
                        (section 30's "do not rely on unordered-map iteration"). The only hash
                        container in the phase is the duplicate-NAME check at mechanism
                        construction, which affects no arithmetic.
FIXED ACCUMULATION      ascending REACTION index, then ascending SPECIES index. Frozen in
                        source_formulation.md section 6 before implementation.
CONSISTENT GROUPING     omega += (M_i * nu) * q, always grouped the same way, so a regrouping would
                        be a visible source change rather than a silent numerical one.
EXACT SKIPS             a zero progress rate and a zero net coefficient are SKIPPED, not multiplied
                        by zero. That makes the zero-rate limit exactly the untouched zero source
                        rather than a sum of zeros -- which is what makes it bitwise comparable to
                        P18.2's own zeroSpeciesSources.
NO CACHED STATE         nothing is memoised, so a repeat cannot return a differently-rounded cached
                        value.
```

## 3. Accumulation round-off, quantified (§30)

```text
Section 30 asks: "If accumulation round-off matters, quantify it."

multi-reaction accumulation error    0
    -- measured as | omega(both reactions) - (omega(r1 alone) + omega(r2 alone)) | relative.

It is exactly zero here because the accumulation IS that sum, in that order: adding r1's
contribution then r2's is the same sequence of operations either way. The measurement is kept
because that equality is a property of the implementation, not of arithmetic, and an implementation
that batched or reordered would break it.
```

## 4. Not measured here

```text
cross-PROCESS repeatability     P18.8 / P18.9, G10
CPU/GPU equivalence             P18.8, G9 -- there is no GPU reaction path
solver-level determinism        the closed-reactor test is deterministic, but that is P18.2's
                                transport plus this source; the solver-level claim is P18.9's
```
