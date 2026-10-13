# P18.2 — formulation and sign convention (§2, §3, §4)

```text
FROZEN BEFORE IMPLEMENTATION AND BEFORE ANY MEASUREMENT.
Nothing in this document was derived from an observed result.
```

---

## 1. The continuous equation

```text
    d(rho Y_i)/dt  +  div(rho U Y_i)  =  -div(J_i)  +  omega_i
```

with, for P18.2, `omega_i = 0` as the authoritative physical limit (§14). `J_i` is the corrected
multi-species diffusive mass flux of `diffusion_closure.md`.

## 2. The transported quantity

```text
CONSERVATIVE VARIABLE     rho Y_i          NOT Y_i
TRANSIENT TERM            d(rho Y_i)/dt    NOT rho dY_i/dt
```

The distinction is the whole point of §2 and §34, and it is only visible when `rho` varies. The
**solved unknown** remains `Y_i`, because that is what the invariants of P18.1 are stated on and
what the boundary conditions prescribe; the **conservation** is of `rho Y_i`, and it is the
transient term's two density levels that make it so. This is the same choice P14 already made and
documented.

## 3. The discrete equation, per cell P, per species i

```text
  [ (rho_P^{n+1} Y_{i,P}^{n+1}) - (rho_P^n Y_{i,P}^n) ] V_P / dt
     +  sum_f  ( mdot_f  Y_{i,f} )
  =  -sum_f ( J_{i,f} . n_f ) A_f
     +  omega_{i,P} V_P
```

In CFDApp's assembled form — `a_P phi_P + sum_N a_N phi_N = b_P`, stated once in
`ScalarTransport.hpp` and used by every assembler in the repository — with `phi = Y_i`:

| term | diagonal contribution `a_P` | right-hand side `b_P` | provided by |
|---|---|---|---|
| transient | `+ rho_P^{n+1} V_P / dt` | `+ rho_P^n Y_{i,P}^n V_P / dt` | P14 `TransportTimeState` |
| convection | upwind on `mdot_f`, coefficient 1 | upwind, coefficient 1 | P14 `assembleScalarConvectionContribution` |
| diffusion | `+ Gamma_{i,f} |S_f| / d` | prescribed-value / gradient term | P14 `assembleScalarDiffusionContribution` |
| closure correction | — (explicit) | `-` the corrected face flux | **new**, `diffusion_closure.md` |
| source | `- Sp_P V_P` | `+ Su_P V_P` | P14 `applyImplicitScalarSource` |

```text
THE TRANSIENT TERM'S TWO DENSITY LEVELS, which is what §34's control attacks:

    a_P^time = rho_P^{n+1} V_P / dt        the NEW density, on the diagonal
    b_P^time = rho_P^n  Y^n_{i,P} V_P / dt  the OLD density, on the RHS

Writing rho^{n+1} Y^n on the right-hand side is NOT conservative when the density changes.
P14 carries rhoOld and rhoNew as separate, separately-named pointers precisely so that writing
the wrong form requires actively naming the wrong one. P18.2 passes rhoOld=rho^n, rhoNew=rho^{n+1}.
```

## 4. The diffusivity handed to P14 is `rho * D_i`, not `D_i`

P14's diffusion term is `Gamma_f |S_f| / d` acting on the solved variable, which here is `Y_i`.
Since

```text
    J_i = -rho D_i grad(Y_i)        =>        Gamma_i = rho D_i
```

the field passed as P14's `diffusivity` is `Gamma_i = rho * D_i`, **per cell**, so a variable
density and a per-species `D_i` are both carried with no new machinery. This is also exactly what
the pre-P18 species stack does (`fluid.density() * species.diffusivity()`), so the constant-density
constant-`D` case reproduces it.

```text
UNITS, stated explicitly (§5)
    D_i      [m^2 / s]
    rho      [kg / m^3]
    Gamma_i  [kg / (m s)]
    J_i      [kg / (m^2 s)]          a MASS flux per unit area
    mdot_f   [kg / s]                already carries rho (physics::calculateMassFlux)
    Y_i      [-]                     dimensionless mass fraction
```

## 5. THE SIGN CONVENTION, frozen (§2, §4)

```text
FACE ORIENTATION      every face's area vector S_f and mass flux mdot_f are OWNER-ORIENTED:
                      mdot_f > 0 means mass leaving the owner cell through that face.
FLUX INTO OWNER       positive contributes to accumulation in the owner.
ASSEMBLED ROW         holds  -flux_into_owner, so that a_P > 0 for a well-posed diffusion problem.
                      This is stated in NonOrthogonalDiffusion.hpp and is not P18.2's invention:
                          builder.add(P, P, coefficient);
                          rhs[P] += boundaryValueCoefficient * phi_b;
                          rhs[P] += explicitFlux;
                          if (farCellCoefficient != 0) builder.add(P, farCell, -farCellCoefficient);
INTERNAL FACE         each face is visited ONCE, one `terms` value is computed, and it is applied
                      to owner and neighbour with opposite sign. Conservation is therefore
                      structural, not asserted -- but §33 asserts it anyway, because "structural"
                      is a claim about code that can be broken by an edit.
EXPLICIT FACE FLUX    a known flux F_f into the owner enters as
                          rhs[owner]     -= F_f
                          rhs[neighbour] += F_f
                      so that the two contributions sum to exactly zero for any F_f, in any
                      floating-point arithmetic, because they are the same value negated.
```

## 6. The mass flux is reused, never recomputed (§3)

```text
    mdot_f = rho_f (U_f . n_f) A_f
```

is taken from the **authoritative** `cfd::fields::SurfaceField massFlux` computed by
`physics::calculateMassFlux` and passed in from the converged flow solve. P18.2 computes no
`rho_f`, no `U_f` and no `mdot_f` of its own. This is already true of the pre-P18 species stack and
is the one thing it always got right; P18.2 keeps it.

The convective species flux is therefore

```text
    F_{i,conv,f} = mdot_f Y_{i,f}          with Y_{i,f} taken UPWIND on the sign of mdot_f
```

## 7. The constant-density limit (§9)

With `rho = const` and `dt -> inf` (steady), the transient term vanishes and

```text
    div(rho U Y_i) - div(rho D_i grad Y_i) = S
```

which is **exactly** the operator the pre-P18 `SpeciesEquation` assembles (its header states this
form verbatim). With one species and a uniform `Gamma`, P14's Arithmetic face interpolation of a
uniform field reproduces the constant-`Gamma` coefficient exactly, and the convection term already
shares `massFlux`. **So the constant-density single-species steady limit must reproduce the shipped
answer bitwise**, and G1(b) requires exactly that. This is the compatibility bridge §9 asks for,
and it is a falsifiable prediction rather than an aspiration.

## 8. What is deliberately NOT in this formulation

```text
omega_i           = 0. No reaction rate, no stoichiometry, no Arrhenius. (P18.4-P18.6)
mixture W, R, cp  absent. Gamma_i = rho D_i uses a GIVEN D_i. (P18.3)
D_i(T, Y)         absent. D_i is a given per-species constant or a given per-cell field.
higher-order      convection is first-order upwind, which is what P14 provides. No limiter is
                  invented (§8) -- boundedness is REPORTED, never imposed.
post-normalisation FORBIDDEN (§7). sum(Y_i) = 1 must emerge from the closure, and if it does not,
                  that is a finding, not something to repair after the fact.
```
