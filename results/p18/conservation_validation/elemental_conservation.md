# P18.7 — elemental conservation (§9, §10, §39)

```text
ELEMENTAL CONSERVATION: NOT MODELED
```

---

## 1. The measurement

```text
files in include/ or src/ carrying elemental composition : 0

Searched for: elemental, atomCount, elementCount. Recorded at logs/00_p18_7_start_state.log.
```

CFDApp's species model carries a NAME, a MOLECULAR WEIGHT (P18.3), a SENSIBLE ENTHALPY MODEL
(P18.3, from P15) and a FORMATION ENTHALPY (P18.6). It does not carry an elemental formula, so
there is no `a_{e,i}` matrix and no element inventory to conserve.

## 2. Why nothing was invented

```text
Section 9, verbatim:
    "If elemental composition is NOT modeled, record ELEMENTAL CONSERVATION: NOT MODELED. Do not
     invent an elemental database solely to check this box."
and
    "lack of elemental metadata is not itself a P18.7 failure if mass conservation is correctly
     enforced and the limitation is documented."

MASS CONSERVATION IS CORRECTLY ENFORCED AND MEASURED -- species_conservation.md -- so the
condition section 9 attaches is met.

P18.4 reached the same conclusion and recorded it as debt; P18.7 does not reverse that decision on
its own authority.
```

## 3. Consequences

| section | requirement | status |
|---|---|---|
| §10 | per-reaction elemental balance `Σ_i a_{e,i} ν_i = 0` | **N/A** |
| §39 | elemental balance through boundaries | **N/A** |

```text
WHAT STANDS IN FOR THEM, and it is not nothing: P18.4 enforces sum_i M_i nu_i = 0 PER REACTION, on
the definition, EXACTLY -- which is the MASS statement of the same conservation principle. A
reaction that violated elemental balance while conserving mass is constructible in principle, but
no such mechanism can be expressed here because the coefficients are supplied directly rather than
derived from formulae.

Measured on every P18.7 case: max_cell |sum_i omega_i| = EXACTLY 0.
```

## 4. TODO.md representation

Per §66, recorded truthfully rather than ticked:

```text
[~] Elemental conservation -- N/A: elemental composition metadata is not modeled
```
