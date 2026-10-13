# P18.4 — reversible and irreversible reactions (§11, §12, §13, §24, §40)

```text
IRREVERSIBLE   q = q_f,  and q_f >= 0 is ENFORCED
REVERSIBLE     q = q_net = q_f - q_r,  ANY SIGN. Negative means net reverse.

omega_i = M_i nu_i q   in BOTH cases. The direction affects only what is a VALID q, never the
                       source algebra -- which is why one code path serves both.
```

---

## 1. Why the semantics are kept distinct (§24)

```text
A negative forward rate is not a slow reaction. It is a reaction running backwards, which an
IRREVERSIBLE reaction by definition cannot do -- so accepting one would silently produce reactants
from products through a reaction declared not to do that.

Relaxing the check to the weaker of the two (accept any sign, because the reversible case needs it)
would lose that. So ReactionDirection is part of the reaction, and the validity check reads it.

MEASURED: the SAME reaction, differing only in its declared direction --
    Irreversible + q = -1   REFUSED
    Reversible   + q = -1   ACCEPTED
so the check is provably direction-dependent rather than absent or universal.
```

## 2. Direction results (§12)

```text
q_net > 0    A consumed, C produced        forward
q_net < 0    A PRODUCED, C CONSUMED        reverse
q_net = 0    every omega_i EXACTLY 0       no net change
```

The third is section 40's equilibrium-like test. It is built as `q_forward - q_reverse` with the two
equal, so the zero arises from the subtraction rather than from passing a literal 0 — which is what
makes it a test of the reversible algebra rather than of the zero-rate shortcut.

```text
IT CLAIMS NOTHING ABOUT THERMODYNAMIC EQUILIBRIUM. P18.4 has no equilibrium constant and no way to
compute one. It verifies only that equal forward and reverse rates give no net source.
```

## 3. Direction symmetry is BITWISE (§13)

```text
    omega_i(-q) = -omega_i(q)    for every species, EXACTLY
```

Asserted with `EXPECT_EQ`, not `EXPECT_NEAR`. Negating `q` negates each product `(M_i nu_i) * q`
exactly in IEEE arithmetic, so anything other than bitwise would mean the sign is entering
somewhere it should not — which is precisely what a sign-convention test is for.

## 4. What reversibility does NOT include here

```text
NO equilibrium constant K_eq, and no way to derive q_r from q_f. P18.4 takes q_net as given.
NO detailed balance, no thermodynamic consistency between forward and reverse rates.
NO Arrhenius parameters for either direction.

All three need kinetics and thermodynamics of reaction, which are P18.5's and P18.6's. P18.4
provides the ALGEBRA that consumes whatever q_net those phases produce, and the separation is what
lets them be added without touching stoichiometry or source code.
```
