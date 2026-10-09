# P18.4 — reaction model, stoichiometry and source formulation (§2–§8)

```text
FROZEN BEFORE IMPLEMENTATION AND BEFORE ANY MEASUREMENT.
```

---

## 1. The reaction

```text
    sum_i nu'_i A_i   ->   sum_i nu''_i A_i

    nu'_i   reactant stoichiometric coefficient,  >= 0
    nu''_i  product  stoichiometric coefficient,  >= 0
    nu_i  = nu''_i - nu'_i                        the NET coefficient
```

## 2. THE SIGN CONVENTION, frozen (§2)

```text
    nu_i < 0     species i is consumed   (reactant)
    nu_i > 0     species i is produced   (product)
    nu_i = 0     species i is uninvolved -- OR is a catalyst that cancels (§27)

These two cases are DELIBERATELY INDISTINGUISHABLE in the net vector, and that is correct: a
catalyst appearing as A -> A contributes nothing to any species source, which is what a catalyst
does. The reactant and product coefficients are retained separately so a caller can still tell the
two apart for reporting, but nothing in the source arithmetic reads them.
```

## 3. Canonicalisation (§27, §28)

```text
INPUT     two lists of (species name, coefficient), one for reactants and one for products.
STORED    ONE dense net-stoichiometry vector over the authoritative SpeciesRegistry, indexed by
          species index, plus the two dense one-sided vectors for reporting.

  * species are referenced BY NAME and resolved through the registry once, at construction. No
    reaction keeps a species list of its own (§3).
  * a species appearing on both sides is summed into nu'_i and nu''_i respectively and then
    cancelled ONCE into nu_i -- never double-counted (§27).
  * a species appearing TWICE on the SAME side is summed, not rejected: 2A + A is 3A. That is a
    deliberate choice and it is documented here because the alternative (rejection) is equally
    defensible; summing was chosen because it is what the algebra means.
  * ordering is by ASCENDING SPECIES INDEX, always. The dense vector is the storage, so there is
    no map to iterate and no ordering to depend on (§28, §30).
  * ZERO COEFFICIENTS ARE ACCEPTED ON INPUT and contribute nothing. They are NOT rejected, because
    "0 A" is meaningless but harmless, and rejecting it would make a programmatically generated
    mechanism harder to build than it needs to be. A reaction that is empty AFTER canonicalisation
    -- every nu_i zero -- IS rejected (§26).
```

## 4. The progress rate (§6)

```text
    q       the reaction progress rate      [mol / (m^3 s)]
```

P18.4 **does not compute `q`.** It is supplied per reaction, per cell. This is the whole point of
§33's separation:

```text
    P18.4:  reaction topology + q   ->  omega_i
    P18.5:  state + Arrhenius       ->  q
```

Nothing in P18.4's production code knows about `A`, `n`, `Ea` or `exp()`.

```text
IRREVERSIBLE:  q = q_f,  with q_f >= 0 ENFORCED. A negative forward rate is not a slow reaction,
               it is a reaction running backwards, which an irreversible reaction cannot do.
REVERSIBLE:    q = q_net = q_f - q_r,  ANY SIGN. Negative means net reverse.
These are distinct semantics and are kept distinct in the API (§24), so the validity check can
differ between them rather than being relaxed to the weaker of the two.
```

## 5. The species source (§7)

```text
    molar:   omega_dot_i,molar = nu_i q                    [mol / (m^3 s)]
    mass:    omega_i           = M_i nu_i q                [kg  / (m^3 s)]

    over a mechanism of R reactions:

    omega_i = sum_r M_i nu_{i,r} q_r
```

```text
UNITS, ONE SYSTEM THROUGHOUT -- mol, not kmol (§31):
    q        [mol / (m^3 s)]
    M_i      [kg / mol]          the SAME values P18.3 holds; P18.4 stores none of its own
    nu_i     [-]
    omega_i  [kg / (m^3 s)]      which is what P14's Su requires, since b_P += Su_P V_P and the
                                 transported quantity is rho Y_i [kg/m^3]

A kmol/mol confusion is a factor of 1000. The control N9 exists for exactly this and is measurable
because M_i is ~1e-2 kg/mol: a kmol value would be ~10, which is not a plausible molecular weight
for any species and shows up as a 1000x error in omega_i.
```

## 6. ACCUMULATION ORDER, frozen (§30)

```text
    for r in 0 .. R-1:            ascending REACTION index
        for i in 0 .. N-1:        ascending SPECIES index
            omega[i] += M_i * nu[i][r] * q[r]

Both loops are over dense vectors, so there is no unordered container anywhere and no ordering to
be accidental. Two evaluations of the same mechanism therefore produce bitwise identical omega.
The multiplication is grouped as (M_i * nu) * q consistently, so a regrouping would be a visible
change rather than a silent one.
```

## 7. MASS CONSERVATION — the primary criterion (§8, §9, §21)

```text
    A reaction conserves mass  <=>  sum_i M_i nu_i = 0

and therefore, for ANY progress rate q,

    sum_i omega_i = q sum_i M_i nu_i = 0

THE SECOND FOLLOWS FROM THE FIRST IDENTICALLY, for every q, in every cell. So the criterion is
checked ON THE REACTION DEFINITION (§8: "Do not merely verify this after running the PDE"), and the
per-cell source sum is then a consequence -- measured anyway (§22), because "follows identically"
is a claim about code.
```

```text
THRESHOLD, INHERITED FROM G6 VERBATIM, not derived here:

    | sum_i M_i nu_i |  <=  4 * eps * max_i | M_i nu_i |

G6's own words: "stoichiometric closure: sum_i W_i nu_i = 0 EXACTLY for each reaction (mass is
conserved by the stoichiometry itself), to <= 4 eps * max|W_i nu_i|".

RELATIVE, scaled by the largest term in the sum -- because an absolute tolerance on a dimensional
mass would be meaningless across mechanisms.
```

```text
PER REACTION, NOT PER MECHANISM (§21). Each reaction is checked individually at mechanism
construction, because two unbalanced reactions could cancel in an aggregate and a mechanism-level
check would pass a mechanism containing two errors.
```

## 8. Rejection, never repair (§4, §9, §24, §25, §26)

```text
REJECTED AT CONSTRUCTION, with the reaction and species named:
    unknown species name                        (§3)
    negative, NaN or Inf stoichiometric input   (§4) -- NOT silently abs()'d
    a reaction with no reactants or no products (§26)
    a reaction whose net stoichiometry is entirely zero after canonicalisation (§26)
    a mass-unbalanced reaction                  (§9) -- NOT renormalised
    a duplicate reaction name in one mechanism  (§25) -- NOT silently overwritten
REJECTED AT EVALUATION:
    a non-finite progress rate                  (§24)
    a NEGATIVE progress rate for an IRREVERSIBLE reaction (§24)
```

```text
NO SILENT STOICHIOMETRIC CORRECTION AND NO SOURCE CLIPPING. §9 forbids renormalising stoichiometry;
§23 forbids clamping a source that would drive a species negative. The latter is DIAGNOSED rather
than prevented -- see §9 below.
```

## 9. What P18.4 deliberately does NOT do

```text
kinetics                 no A, n, Ea, exp(). q is supplied. (§33)
heat release             no reaction enthalpy, no energy source. (§34) P18.6's.
formation enthalpy       none. P15's referenceEnthalpy is a SENSIBLE offset and is NOT repurposed.
time integration         NO chemistry integrator. (§23)
                         Whether rho Y_i + dt omega_i stays non-negative is a property of an
                         integrator that does not exist. The RATE is validated; the BOUNDED STEP IS
                         DEFERRED and labelled. A diagnostic is exposed so a future integrator can
                         detect the condition; nothing is clamped.
elemental conservation   NO elemental composition exists in the repository (§10). Recorded as
                         ELEMENTAL VALIDATION DEFERRED, MASS CONSERVATION ENFORCED. P18.7's.
implicit Patankar Sp     stays 0. Linearising omega_i in Y_i needs d(omega)/d(Y), a kinetics
                         derivative P18.4 does not have. See audit.md section 4.
transport changes        NONE. P18.4 produces values for the source hook P18.2 already has.
```
