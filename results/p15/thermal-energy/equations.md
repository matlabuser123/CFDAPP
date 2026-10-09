# P15 — the equations actually discretised

Not the equations P15 could have used: the ones the code implements, with the form each term
takes in the assembled matrix.

---

## 1. The transported quantity

CFDApp's energy equation transports **`cp·T`**, not `T`. That is a pre-existing decision, visible
in `assembleThermalConvectionContribution`, which scales the face mass flux by `cp` "since the
transported quantity here is cp*T rather than T itself".

```
d(rho cp T)/dt + div(rho u cp T) = div(k grad T) + q
```

The mass flux `F_f = rho (u . S_f)` already carries `rho` (`physics::calculateMassFlux`), so
convection multiplies it by `cp` and diffusion uses `k` directly — the two are dimensionally
consistent as a pair, which is the property that matters.

Mapping onto P14's generic form `d(rho phi)/dt + div(rho u phi) = div(Gamma grad phi) + S`:

```
phi = T ,  Gamma = k ,  S = q ,  with cp folded into the convective flux
```

## 2. Assembled form

```
a_P T_P + sum_N a_N T_N = b_P
```

| term | contribution |
|---|---|
| transient | `a_P += rho^{n+1} cp^{n+1} V_P / dt` ; `b_P += rho^n cp^n T^n_P V_P / dt` |
| convection | upwind on `cp F_f`, coefficient 1 |
| diffusion | `k_f |S_f| / d` |
| source | `b_P += q V_P` |
| Dirichlet | `D_b = k_f A_f / d_Pb` ; `a_P += D_b` ; `b_P += D_b T_w` |
| Neumann | prescribed flux enters `b_P` |
| convective (Robin) | `G = 1/(d/k + 1/h_c)` per unit area ; `a_P += G A_f` ; `b_P += G A_f T_inf` |

### 2.1 The transient term is conservative

```
a_P^time = rho^{n+1}_P cp^{n+1}_P V_P / dt      NEW level
b_P^time = rho^n_P cp^n_P T^n_P V_P / dt        OLD level, old temperature
```

**Both `rho` and `cp` are carried at two time levels.** Using the new `cp` against the old
temperature is the same non-conservative slip P14 guards against for density, and it is worse here
because `cp(T)` is exactly what P15 makes vary. `ThermalTimeState` names the five fields separately
so writing the wrong combination has to be deliberate.

A steady solve passes `transient = false` and **no transient code executes at all** — not a
zero-valued term. Verified bitwise against the pre-P15 overload
(`ThermalTransient.SteadyStateIsBitwiseUnchanged`).

## 3. Sensible enthalpy

```
h(T) = h_ref + integral_{T_ref}^{T} cp(theta) dtheta
```

For `cp(T) = sum_i b_i T^i` the integral is exact term by term, so **no quadrature is used and no
quadrature error is introduced**:

```
h(T) - h_ref = sum_i  b_i/(i+1)  ( T^{i+1} - T_ref^{i+1} )
```

**This is not `h = cp·T`.** For `cp = b0 + b1 T` the integral carries `b1/2 · T^2` while the
product carries `b1 · T^2` — a factor of two on the quadratic term. A solver using the product
would conserve something, just not energy.
`SensibleEnthalpyTest.NaiveCpTimesTemperatureIsWrongForVariableCp` asserts the two differ by more
than 10% relative, so the shortcut cannot be reintroduced silently.

### 3.1 The inverse `T(h)`

No closed form for a general polynomial, so it is solved by **bisection** on `[T_min, T_max]`.
`h` is strictly increasing wherever `cp > 0`, which
`TemperatureDependentProperty` guarantees by refusing a polynomial that is non-positive anywhere in
its declared range — so the inverse exists, is unique, and bisection cannot fail to converge.

Bisection rather than Newton, deliberately: no derivative, no overshoot outside the validity range,
and an iteration count fixed by the bracket and tolerance rather than by an initial guess.

An enthalpy outside `[h(T_min), h(T_max)]` **throws**. Clamping would return a temperature that does
not correspond to the energy that produced it, and would let a diverging solve look converged.

## 4. Temperature-dependent properties

```
k(T)  = a0 + a1 T + a2 T^2 + ...
cp(T) = b0 + b1 T + b2 T^2 + ...
```

Evaluated by **Horner from the highest coefficient down** — stated because a polynomial summed in
a different order gives different bits, and this project compares bits. Horner is also the
numerically better choice, so the two reasons do not conflict.

A **constant** property is the single-coefficient case, using the same class and the same
evaluation path, so the constant result cannot drift from the varying one.

Each property carries an explicit **validity range**, and evaluation outside it is an error.
Extrapolating a fitted polynomial produces confident nonsense: a cubic `cp` fit can go negative a
few hundred kelvin past its data, which makes enthalpy non-monotonic and `T(h)` ill-posed.
Positivity over the range is checked at construction by sampling 1024 points — sampling can in
principle miss a narrow dip, which is stated rather than hidden.

## 5. Convective (Robin) boundary

```
-k dT/dn = h_c (T_w - T_inf)
```

Eliminating the wall temperature against the one-sided conduction estimate:

```
T_w = ( (k/d) T_P + h_c T_inf ) / ( k/d + h_c )
G   = 1 / ( d/k + 1/h_c )                       series conductance per unit area
```

Both limits are verified rather than assumed:

```
h_c -> 0         insulated: G = 0, T_w = T_P
h_c -> infinity  Dirichlet: T_w -> T_inf, G -> k/d
```

**`boundaryValue()` throws.** The face value depends on the local conductivity, which
`ScalarBoundaryCondition::boundaryValue(ownerValue, normalDistance)` does not supply and cannot: `k`
varies per cell and, after P15, with temperature. A BC caching its own `k` would silently disagree
with the material model the moment `k(T)` moved. Returning a plausible wrong number would be
consumed by every generic assembler without complaint — the worst failure mode available. So the
condition carries only `h_c` and `T_inf`, and the discretisation computes the coefficient, which is
the split `BoundaryCondition.hpp` itself prescribes.

**Consequence, stated plainly:** P14's generic scalar transport path does **not** support this
condition — it queries `boundaryValue()` and throws. Generalising it means putting `Gamma` into the
base-class boundary query, changing a signature used by every scalar and vector BC. That is a
deliberate future change, not a P15 side effect.

## 6. Buoyancy coupling (unchanged)

```
rho(T) = rho_0 [ 1 - beta (T - T_0) ]        retained only in the gravity term
S_b    = -rho_0 beta (T - T_0) g
```

Pre-existing and untouched by P15. Its sign convention is documented at length in
`BoussinesqBuoyancy.hpp`, including the specific error of dropping the leading minus.
