# P18.4 — integration with P18.2 transport (§18, §19, §20)

```text
P18.4 PRODUCES omega_i. P18.2 CONSUMES IT through the source hook it already had. There is no
parallel species update and no second assembly path.
```

---

## 1. The join

```text
reactionSpeciesSources(...)  ->  SpeciesSources { vector<ScalarSourceFields> }
                                      Su = omega_i   [kg/(m^3 s)]
                                      Sp = 0
                                        |
                                        v
P18.2:  speciesTransportAssembly(..., const ScalarSourceFields& source, ...)
        which P18.2 has taken since it was written, and which P18.2's own tests already
        exercised with an explicit zero.
```

The PDE is unchanged:

```text
    d(rho Y_i)/dt + div(rho U Y_i) = -div(J_i) + omega_i
```

```text
UNITS FALL OUT OF THE ASSEMBLER; they are not a convention P18.4 chose freely. P14's Su enters
b_P as Su * V_P, the transported quantity is rho Y_i [kg/m^3], and the transient term is rho V/dt --
so b has units [kg/s] and Su must be [kg/(m^3 s)]. A molar source would be wrong by a factor of
M_i, which is 2e-3 to 4.4e-2 here: not subtle, but also not something the assembler could detect.
```

## 2. Sp stays zero, deliberately

```text
Patankar's form is S = Su + Sp phi, with Sp <= 0 giving an implicit, stabilising contribution. A
stiff reaction rate genuinely wants one.

P18.4 SETS Sp = 0, because linearising omega_i in Y_i requires d(omega_i)/d(Y_i) -- a KINETICS
derivative. P18.4 has no kinetics (section 33), so it has nothing to differentiate. An explicit
source is the mathematically correct Patankar split for something treated explicitly; the implicit
part belongs to P18.5, which will have the rate and therefore the derivative.

ASSERTED: Sp is exactly 0 for every species and every cell, in every source the phase produces.
```

## 3. The closed homogeneous reactor (§19)

```text
U = 0, J_i = 0, zero-gradient walls  =>  d(rho Y_i)/dt = omega_i

With implicit Euler and a CONSTANT explicit source the discrete update is EXACT:
    (rho V/dt) Y^{n+1} = (rho Y^n V/dt) + omega V   =>   rho Y^{n+1} = rho Y^n + omega dt

so the reference is rho Y_i(0) + omega_i t, computed in the test from M_i nu_i q -- NOT by calling
the production source function.

MEASURED, 10 steps, A + 2B -> C at q = 0.02, rho = 2:
    every species matches the analytical value to 1e-12, in every cell
    the INERT species does not move at all
```

## 4. Total species mass under reaction (§20)

```text
20 steps, closed enclosure:
    relative total-mass drift  within 8 eps N_cells
    max |sum Y - 1|            within the derived bound, WITHOUT any normalisation

The mass is conserved because sum_i omega_i = 0, which is a property of the STOICHIOMETRY and is
checked on the reaction definition -- not something the PDE run establishes. The run confirms the
consequence rather than discovering it.
```

## 5. The zero-rate limit is BITWISE P18.2 (§17, §47)

```text
A mechanism that EXISTS but has zero progress rate must give BITWISE what P18.2 produces with no
reaction at all:

    reactionSpeciesSources(mechanism, mixture, zeroProgressRates(...))  ==  zeroSpeciesSources(...)

asserted with EXPECT_EQ for Su AND Sp, every species, every cell. That is stronger than "small":
it means the reaction layer's presence cannot perturb a non-reacting result at all.

It holds because a zero rate is SKIPPED rather than multiplied through -- see determinism.md
section 2.

AND THE FIELDS DO NOT MOVE: after 10 steps with a zero-rate mechanism, every mass fraction is
unchanged to 1e-14.
```

## 6. What P18.4 did NOT change in P18.2 (§52)

```text
src/species/SpeciesTransport.cpp   0 changes. Its sha256 is recorded in the integrity log.
the PDE                            unchanged
the conservative discretisation    unchanged
the diffusive closure sum J_i = 0  unchanged
the boundary-lag fix               unchanged
species conservation               unchanged
P18.2's own 34 tests               34 / 34, unchanged
```

## 7. What is NOT wired

```text
NO SOLVER DRIVES A REACTION. The join above is library-level:
  * no case-format key defines a mechanism;
  * ProjectRunner does not build one;
  * nothing produces a progress rate in production -- P18.4 takes q as given, and the thing that
    would compute it is P18.5.

STATED AS A LIMIT, the same one P18.2 and P18.3 carry. P18.4 delivers the source layer and its
integration with transport; making a reacting case runnable needs the kinetics and the case-format
work that later phases own.
```
