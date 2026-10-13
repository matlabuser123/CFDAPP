# P18.2 — boundary treatment (§16, §19–§23)

```text
Starting point: results/p18/audit/boundary_lag.md and the P18.0 boundary audit. What follows is what
the code does, traced from source, and what enters the convective and diffusive species flux at each
boundary kind.
```

---

## 1. What exists, and what does not

No species-specific boundary classes exist, before or after P18.2. The generic scalar conditions are
used directly, one `BoundaryConditionSet` **per species**:

| Boundary kind | Expressed as | Convective species flux | Diffusive species flux |
|---|---|---|---|
| fixed mass fraction (Dirichlet / inlet) | `FixedValue(Y_i,in)` | `ṁ_f · Y_i,b` on inflow (upwind takes the boundary value) | `bvc·Y_b − coeff·Y_P (+ far-cell term)` — the P12-DIFF-002 second-order reconstruction applies |
| zero gradient (wall, symmetry, outflow) | `FixedGradient(0)` | `ṁ_f · Y_i,P` on outflow; `ṁ_f = 0` at an impermeable wall | **exactly 0** — the Exact treatment gives `coeff · g · d` with `g = 0` |
| prescribed gradient | `FixedGradient(g)` | as above | `bvc · g · d`, known exactly, **zero diagonal** |
| **prescribed diffusive flux** | **not available** | — | — |
| symmetry / periodic as named types | **not available** | — | — |
| Robin / convective | **refused** by the generic path with a named error | — | — |

```text
§21 -- PRESCRIBED SPECIES FLUX IS NOT SUPPORTED. No boundary type in this repository prescribes a
diffusive species flux, so there is nothing to verify and no test claims it. Recorded as an ABSENT
CAPABILITY, not as a passing criterion. A caller wanting a prescribed flux today must express it as
the equivalent gradient, which is a different thing and is not silently substituted.
```

## 2. The gradient face is now exact (§17, §18)

The one substantive change. Full treatment in `boundary_lag.md`; in short: for `dY/dn = g` the flux
into the owner is `Df·g·d`, which contains no `Y_P`, so the exact contribution is a **zero diagonal**
and a fully known RHS. G4(c) met at ≤ 2 outer iterations against the previous 335.

## 3. Dirichlet composition (§19)

```text
VERIFIED, on a multi-species boundary rather than a single scalar:
  the slab case prescribes Y_A = 0.8 / 0.2 at the two walls and Y_B = 1 - Y_A at each, so the
  PRESCRIBED BOUNDARY COMPOSITION SUMS TO EXACTLY 1.
  the open-domain case prescribes 0.3 / 0.7 at the inlet -- likewise exactly 1.

WHY IT MATTERS FOR THE CLOSURE, and it is not decoration: the correction at a face uses the face
composition, and sum_i J_{i,f} = Sigma_f (1 - sum_i Y_{i,f}). At a Dirichlet face the face
composition IS the prescribed one, so a prescribed composition that did not sum to 1 would make the
closure inexact at that face -- silently. That is the mechanism behind §19 and §23's requirement to
validate an inlet composition, and it is why the cases above set the exact complement rather than a
convenient pair of numbers.
```

```text
NOT ENFORCED BY THE TYPE SYSTEM, and stated as a limit: nothing checks that the N per-species
boundary sets prescribe a composition summing to 1 at a shared patch. The boundary sets are
independent objects, one per species, with no cross-species validation -- exactly as the pre-P18
stack has them. Adding that check means a multi-species boundary type, which is a case-schema change
and therefore outside P18.2. REPORTED for whoever wires the carrier-species decision.
```

## 4. Zero-gradient impermeable wall (§20)

```text
ASSERTED QUANTITATIVELY, for every species, at every boundary face:
  ZeroGradientWallCarriesExactlyZeroDiffusiveFlux -- EXPECT_EQ(raw[i][f], 0.0), exact equality.

With g = 0 the Exact branch's RHS term is bvc * boundaryValue(0, d) = bvc * 0 = 0, so the flux is
exactly zero rather than approximately. And with the enclosure's mass flux at 0 on every boundary
face, the total species flux through the wall is zero for both terms.
```

## 5. Outlet (§22)

```text
Expressed as FixedGradient(0), so:
  DIFFUSIVE   grad(Y).n = 0 gives exactly zero diffusive contribution -- same mechanism as §4.
  CONVECTIVE  upwind on the authoritative mass flux takes the OWNER value on outflow, so the
              outgoing species flux is mdot_f * Y_{i,P}. No artificial source: the open-domain
              balance reports the convective out term separately and it closes to 1e-10 relative.
```

## 6. Inlet (§23)

```text
Y_i,in prescribed per species. Validated in the cases as summing to 1 (section 3).
The inlet does NOT rely on downstream normalisation -- there is no normalisation anywhere on any
accepting path, which §7 requires and the integrity check verifies by grep.
```

## 7. What P18.2 deliberately did not touch

```text
No boundary class was modified. No new boundary type was added. The Robin refusal in the generic
path is unchanged. The P12-DIFF-002 second-order Dirichlet reconstruction is unchanged and still
applies to prescribed-value faces -- the Exact branch never handles those, so the two cannot
interact, which is checked structurally rather than assumed (boundary_lag.md section 4).
```
