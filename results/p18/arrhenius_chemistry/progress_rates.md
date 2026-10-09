# P18.5 — progress rates (§15–§22, §27, §28)

```text
    q_f = k_f(T) Pi_i C_i^(nu'_i)     over REACTANTS, using nu'_i
    q_r = k_r(T) Pi_i C_i^(nu''_i)    over PRODUCTS, reversible only
    q   = q_f - q_r
    C_i = rho Y_i / M_i               [mol/m^3]

THE ORDER IS THE REACTANT COEFFICIENT nu'_i, NOT THE NET ONE nu_i. For 2 H2 + O2 -> 2 H2O the net
coefficient of H2 is -2 and the reactant coefficient is +2; using the net one would give a negative
exponent and a rate that blows up as the reactant is consumed. The two are kept as separate
vectors in P18.4's Reaction precisely so this cannot be confused -- the dense net vector for the
sources, the one-sided vectors for the orders.
```

---

## 1. Concentrations reuse the authoritative state

`molarConcentrations(mixture, state, density)`:

```text
    C_i = rho Y_i / M_i

rho       from the caller
Y_i       from P18.1's VALIDATED MassFractionState -- bounded and summing to one
M_i       from P18.3's MixtureThermodynamics

P18.5 STORES NO COMPOSITION AND NO MOLECULAR WEIGHT, so the basis cannot drift from the rest of
the stack. There is no second copy to keep in sync.
```

The guard is on the **density**, not on the concentration: a negative `C_i` cannot arise from a
valid `MassFractionState` and a positive `rho`, so checking `C_i` afterwards would be checking a
consequence rather than a cause. `RejectsANegativeOrNonFiniteDensity` covers `-1`, NaN and `inf`.

`ConcentrationsReuseTheAuthoritativeStateAndWeights` asserts the values against an independent
computation and asserts the species count and ordering come from the registry.

## 2. Mass action — measured

`logs/05_mpmath_comparison.log`, 27 non-zero rows of the G6 matrix:

| quantity | worst relative error | allowance |
|---|---:|---:|
| `q` | **10.24 eps** | 32 eps |

and the two sharp cases:

```text
    C_H2 = 0        =>  q = 0  EXACTLY (9 rows, asserted as exact equality, not a tolerance)
    rho -> 2 rho    =>  q x 8  EXACTLY (2^3, total reactant order 3)
```

## 3. Each cell from its own state (§27, §28)

```text
ACCUMULATION ORDER is ascending reaction index, then ascending cell. No rate coefficient is cached
across cells, because a cached one would be wrong for every spatially varying temperature field --
which is the normal case, not an edge case.
```

Separated into two tests so the two dimensions cannot mask each other:

| test | varies | holds fixed | measured |
|---|---|---|---|
| `N6_EachCellUsesItsOwnTemperature` | `T` = 800..2550 K | composition, `rho` | rate ratio **7.12e5**, strictly increasing cell to cell, each cell matching a direct evaluation at its own `T` within 32 eps |
| `SpatiallyVaryingCompositionGivesVaryingRates` | `Y_H2` 0.02→0.32, `Y_O2` 0.60→0.40 | `T`, `rho` | ratio **> 10** (second order in `Y_H2`) |

## 4. Reversible kinetics (§22) — and a stated limitation

```text
FORWARD AND REVERSE PARAMETER SETS ARE SUPPLIED INDEPENDENTLY. They are NOT derived from an
equilibrium constant: P18.5 has none, and §20 forbids inventing one.

THE LIMITATION THIS LEAVES, stated plainly: a caller can specify a thermodynamically INCONSISTENT
pair -- a forward/reverse ratio that does not correspond to any K_eq -- and nothing in P18.5
detects it. That is not an oversight to be fixed quietly later; it is what "no equilibrium
constant" means. Equilibrium consistency needs the Gibbs-energy route, which needs the full
thermodynamic polynomials, which is not in P18.5's scope.
```

`ReversibleKineticsUseIndependentlySuppliedReverseParameters` tests the three regimes §22 names,
with the balance point derived analytically rather than searched for:

```text
q_net = 0 when k_r = k_f * (forward product) / (reverse product), and since k is linear in A at
fixed n and Ea, that is A_r = A_f * (forward product) / (reverse product).

    A_r = 1          ->  q_net > 0   net forward
    A_r = 1e14       ->  q_net < 0   net reverse
    A_r = balanced   ->  |q_net| / q_forward <= 32 eps

The balance point is COMPUTED FROM THE DEFINITION, not tuned until the test passed. A numerically
located root would have been a self-consistency check on whatever the implementation does.
```

`RejectsMismatchedDirectionParameters` refuses reverse parameters on an irreversible reaction and
missing reverse parameters on a reversible one — both at construction.

## 5. Binding by name (§37)

`ReactionRateParameters` carries a reaction **name**, and `ReactionKinetics` reorders the supplied
sets into mechanism order.

```text
A POSITIONAL BINDING WOULD BE SILENTLY WRONG whenever the supplied order differed from the
mechanism's: every rate plausible, every one attached to the wrong reaction, nothing to detect.
Name binding makes that unrepresentable rather than merely detectable -- control N8.
```

Refused at construction: an unknown reaction name, two sets for one reaction, a reaction with no
set, and a wrong count. `RejectsMisnamedMissingAndDuplicateParameterSets` covers all four.

## 6. The boundary with P18.4 (§25, §26)

```text
    P18.5                 P18.4                      P18.2
    T, rho, Y_i, A, n, Ea
      -> k(T)
      -> C_i
      -> q_r
                          -> omega_i = sum_r M_i nu_i q_r
                                                     -> conservative transport

P18.5 PRODUCES q AND NOTHING ELSE. The conversion to omega_i stays entirely in
reactionSpeciesSources(), and the mass-conservation formula appears nowhere in
src/species/ArrheniusKinetics.cpp -- not as a comment, not as a cross-check.

WHY THAT MATTERS: a duplicated conservation formula is a second thing to keep correct, and the
duplicate would be verified by the same reasoning as the original. Control N9 drives the real
chain end to end and asserts closure through P18.4's OWN diagnostic, so the test does not
duplicate the formula either.
```
