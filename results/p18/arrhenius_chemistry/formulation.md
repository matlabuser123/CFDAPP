# P18.5 — formulation (§6, §7, §18, §20, §23)

```text
FROZEN BEFORE IMPLEMENTATION AND BEFORE ANY MEASUREMENT OF THE IMPLEMENTATION.
```

---

## 1. The rate coefficient

```text
    k(T) = A T^n exp( -Ea / (R_u T) )
```

ONE authoritative implementation (§6). It is not reproduced in reaction classes, solver code, test
infrastructure or GPU code — the tests call the same function the production path does, and the
*reference* against which it is checked is an independent **mpmath** implementation outside CFDApp,
not a second C++ copy.

## 2. EVALUATED IN LOG SPACE, and why (§7)

```text
    ln k = ln A + n ln T - Ea/(R_u T)        then     k = exp(ln k)
```

```text
WHY NOT THE DIRECT FORM. A T^n can overflow while k itself is perfectly representable:
    A = 1e300, n = 10, T = 1000  ->  A T^n = 1e330 = +inf
    ... multiplied by exp(-large) the true k may be finite, but inf * 0 is NaN.
Log space combines the three terms BEFORE exponentiating, so an intermediate cannot overflow
independently of the result.

WHAT IT COSTS. ln and exp each carry ~1 eps, so log space is slightly less accurate than the direct
form for benign parameters. G6 allows 32 eps, which is generous for this -- but the claim is
MEASURED against mpmath rather than assumed, and the measurement decides.

A = 0 IS AN EXACT SPECIAL CASE, because ln(0) = -inf. k = 0 is returned directly, for every valid
T (§8). Not a clamp -- an algebraically exact branch.
```

```text
DOCUMENTED NUMERICAL RANGE (§7, §31):
    ln k underflows to k = 0 below about ln k < -745   (exp's subnormal limit)
    ln k overflows  to k = inf above about ln k > +709
P18.5 REFUSES a non-finite k with a named diagnostic rather than returning inf (§31). An underflow
to exactly 0 is ACCEPTED and documented: a rate that small is physically negligible, and refusing it
would make an unreachable reaction an error instead of a non-event.
```

## 3. The analytical temperature derivative (§14)

```text
    ln k = ln A + n ln T - Ea/(R_u T)
    d(ln k)/dT = n/T + Ea/(R_u T^2)
    dk/dT = k [ n/T + Ea/(R_u T^2) ]
```

Implemented from the closed form, and verified against a **central finite difference** of the
implementation at representative states. Two independent routes to the same number.

## 4. The progress-rate model — ELEMENTARY MASS ACTION (§18, §20)

```text
    q_f = k_f(T) Pi_i C_i^(nu'_i)        over the REACTANTS, using nu'_i (not nu_i)
    q_r = k_r(T) Pi_i C_i^(nu''_i)       over the PRODUCTS, for a reversible reaction
    q_net = q_f - q_r
```

```text
WHAT P18.5 SUPPORTS, stated plainly as §18 demands:
    FULL CONCENTRATION-DEPENDENT PROGRESS RATE, elementary mass action, with the reaction orders
    taken as the stoichiometric coefficients.

NOT supported, and not claimed:
    non-elementary / fitted reaction orders distinct from nu'
    third-body efficiencies, pressure fall-off, Lindemann/Troe forms
    reverse rates derived from an equilibrium constant -- §20 and §22 require them to be
      INDEPENDENTLY SUPPLIED, so a reversible reaction carries two Arrhenius parameter sets and
      DETAILED BALANCE IS NOT ENFORCED. A caller can specify a thermodynamically inconsistent pair;
      nothing detects it, because detecting it needs the equilibrium thermodynamics §20 excludes.
```

```text
WHY nu' AND NOT nu. The reaction order is the REACTANT coefficient, not the net one. For
A + 2B -> C the forward order in B is 2; the net coefficient of B is -2 and of C is +1. Using nu
would give a negative exponent on a reactant and a spurious dependence on a product. P18.4 retains
the one-sided coefficients precisely so this is available -- that is what they are for.
```

## 5. The zero-reactant limit (§23)

```text
    C_i = 0 with nu'_i > 0   =>   q_f = 0, EXACTLY
```

Implemented as an explicit branch: a zero concentration with a positive order short-circuits the
product to exactly zero. **`std::pow(0, positive)` is 0 anyway, but the branch makes it exact rather
than dependent on the library,** and it means a zero never reaches `pow`.

```text
0^0 and 0^negative ARE NEVER EVALUATED:
    nu'_i = 0  ->  the species is skipped entirely (it is not a reactant), so 0^0 never arises;
    nu'_i < 0  ->  impossible. P18.4 already refuses a negative stoichiometric coefficient at
                   construction, so a negative reaction order cannot exist.
Both are structural, not checked at the power call.
```

## 6. Parameter and state validity (§3, §4, §24)

```text
REJECTED, with the reaction and the offending value named:
    A  < 0, NaN, Inf
    n  NaN, Inf                     (any finite n is allowed, including negative -- see below)
    Ea < 0, NaN, Inf
    T  <= 0, NaN, Inf               checked BEFORE any exp() or division (§4)
    C_i < 0, NaN, Inf               checked before any pow() (§24)
    a non-finite resulting k
```

```text
NEGATIVE Ea IS REJECTED, and that is a model decision stated rather than assumed (§3). Negative
activation energies are physical for some elementary steps, but they are outside the adopted P18
model: the frozen G6 control sets Ea = 0 to remove the T dependence and nothing in the gate
contemplates Ea < 0. Admitting it would widen the accepted domain with no criterion judging it.
Documented here so a later phase can lift it deliberately.

NEGATIVE n IS ALLOWED, because it is ordinary -- many real rate expressions have n < 0 -- and
nothing breaks: T^n is well defined for T > 0 and any finite n. §17 asks for it to be tested, and
it is.

NO SILENT CLAMPING ANYWHERE (§3), and NO TEMPERATURE FLOOR (§50). An invalid T is refused, not
raised to a minimum.
```

## 7. Binding kinetics to reactions BY NAME (§37)

```text
Kinetic parameters are supplied as (reaction NAME, forward params, optional reverse params) and
resolved against the mechanism by name at construction.

WHY NOT BY INDEX: an index binding is silently wrong if the mechanism's reaction order changes --
the parameters would attach to the wrong reaction and every rate would be plausible and wrong.
That is control N8, and name binding makes it unrepresentable rather than merely detected.

REFUSED at construction:
    a parameter set naming a reaction the mechanism does not hold
    two parameter sets for one reaction
    a reaction with no parameter set
    reverse parameters on an IRREVERSIBLE reaction
    MISSING reverse parameters on a REVERSIBLE one
```

## 8. Determinism (§36)

```text
Reaction order is the mechanism's, species order is the registry's, and both are dense-indexed.
The concentration product accumulates over ASCENDING SPECIES INDEX; the progress-rate field is
filled over ASCENDING REACTION INDEX then ASCENDING CELL. No unordered container is iterated for
arithmetic.
```

## 9. What P18.5 does NOT do

```text
heat release, reaction enthalpy, energy source       P18.6 (§29). P18.5 READS T; it never writes it.
temperature / enthalpy / rhoE updates                none (§29)
a stiff chemistry ODE integrator                     none (§30)
the implicit Patankar Sp                             stays 0. P18.5 now has dk/dT but NOT
                                                     d(omega)/d(Y), which is an integrator concern.
                                                     Recorded as debt.
equilibrium constants, detailed balance              none (§20, §22)
omega_i                                              P18.4's. P18.5 produces q only (§25, §26).
case-file reachability                               no case key defines kinetics.
GPU                                                  no device path; P18.8's.
```
