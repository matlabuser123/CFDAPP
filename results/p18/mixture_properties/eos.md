# P18.3 — composition-dependent EOS (§9, §18, §19) and the P18.2 join (§25)

```text
rho = p / (R_mix T)        and its inverses  p(rho, T)  and  T(p, rho)

NOT A NEW EOS. P18.3 writes no density formula. It constructs the existing
cfd::compressible::ThermodynamicProperties from (R_mix, cp_mix), which owns an IdealGasEOS, and
reaches density through it. Section 9's "do not create a separate unrelated MixtureEOS if
IdealGasEOS can be extended cleanly" is satisfied by extending the VALUE of R, which was the only
single-species thing about it.
```

---

## 1. Density measured against the independent reference

| Case | `rho` at p = 101325 Pa, T = 300 K |
|---|---|
| binary 50/50 H2/CO2 | 0.15661372942897156 |
| binary 25/75 H2/CO2 | 0.28799806267521055 |
| ternary 0.2/0.3/0.5 | 0.3279362949277524 |

```text
max |rho - p/(R_mix T)| / rho  over 9 compositions x 5 T x 3 p :  EXACTLY 0

That zero is architectural: density comes from the SAME IdealGasEOS the check compares against. There
is no second density path that could disagree -- which is the evidence section 9 asks for.
```

## 2. Composition actually reaches the EOS — §18, controls N2 and N6

§18: *"A mixture API that calculates `R_mix` but leaves density using the old constant `R` is
incomplete."*

```text
IDENTICAL p = 101325 Pa AND T = 300 K, four compositions:

    Y_H2 = 1.00   ->  rho = 0.0818940...
    Y_H2 = 0.75
    Y_H2 = 0.50   ->  rho = 0.1566137...
    Y_H2 = 0.00   ->  rho = 1.7876...

    rho(pure CO2) / rho(pure H2) = 21.8304x   -- the full molecular-weight ratio
```

Two things are asserted, not one:

```text
1. the densities DIFFER, monotonically, by a factor of 21.83 -- so a constant-R substitution is
   unmistakable;
2. EVERY rho equals p/(R_mix T) computed from the REPORTED R_mix -- so density is provably routed
   through the composition rather than merely correlated with it.
```

## 3. Round trips — §19

```text
(p, T, Y) -> rho    then    (rho, T, Y) -> p        max relative error  2.872325e-16
(p, T, Y) -> rho    then    (p, rho, Y) -> T        max relative error  3.031649e-16
over 3 pressures x 3 temperatures x 4 compositions, binary and ternary
allowance 8 eps = 1.7763568394002505e-15
```

Each round trip is two divisions, so ~1.3 eps is the expected scale and is what was measured.

## 4. The P18.2 → P18.3 join — §25

`evaluateMixtureFields(mixture, state, pressure, temperature)` evaluates **each cell from its own
local composition**, so a transported `Y_i` field produces spatially varying properties.

```text
8-cell strip, Y_H2 graded 0.05 -> 0.95, Y_CO2 the exact complement:

  R_mix[last] / R_mix[first]     > 5x
  cp_mix[last] / cp_mix[first]   > 5x
  rho[first] / rho[last]         > 5x
  M_mix[first] / M_mix[last]     > 5x

and EVERY cell's field value is asserted BITWISE equal to a direct evaluation of that cell's own
composition -- so the field path and the scalar path cannot diverge.
```

```text
ORDERING IS CHECKED, NOT ASSUMED. evaluateMixtureFields REFUSES a state whose SpeciesRegistry differs
from the mixture's. Ordering is part of a registry's value (P18.1), so a mismatch would read every
composition against the wrong species -- silently, and with plausible-looking numbers. Asserted by
RejectsAStateWhoseRegistryDiffersFromTheMixture, which swaps H2 and CO2 and requires a throw.
```

## 5. Composition validity — §20

```text
Every property evaluation takes its composition from a P18.1 MassFractionState, which has ALREADY
enforced finiteness, 0 <= Y_i <= 1 and |sum Y_i - 1| <= 4 eps N at construction.

So P18.3:
  * duplicates NO validation logic (section 20's second clause) -- the only check is that the
    composition length matches the registry;
  * performs NO renormalisation, silent or otherwise (section 20's first clause) -- there is no
    normalisation call anywhere in the new code, verified by grep in the integrity check;
  * CANNOT be handed an invalid composition through the typed path at all, which is stronger than
    checking one. Control N7 asserts the three refusals.
```

## 6. What the EOS does not do

```text
IDEAL GAS ONLY. No real-gas, no compressibility factor, no Van der Waals. p = rho R_mix T.
NO composition-dependent TRANSPORT properties. D_i, mu and k are not mixture-averaged here; P18.2
  takes D_i as given and P18.3 does not change that.
NO pressure- or temperature-dependent M. Molecular weight is a per-species constant.
NOT WIRED INTO ANY SOLVER. The mixture EOS is library-level: no case-format key selects it, and
  neither CompressibleSIMPLE nor the density-based solver consumes it. Stated as a LIMIT -- the
  P17 single-species path is unchanged and remains what production uses.
```
