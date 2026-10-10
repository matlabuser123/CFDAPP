# Generic scalar transport (`cfd::transport`)

One finite-volume engine for every transported scalar. A future thermal,
turbulence, species or multiphase equation configures this rather than writing
its own assembly.

## The equation

```
d(rho phi)/dt + div(rho u phi) = div(Gamma grad phi) + S
```

Integrated over a control volume and discretised:

```
a_P phi_P + sum_N a_N phi_N = b_P
```

with

```
a_P = a_P^time + a_P^conv + a_P^diff - Sp V
b_P = b_P^time + Su V + b_P^BC
```

## Term by term, as implemented

| term | discretisation |
|---|---|
| transient | `a_P += rho^{n+1} V / dt`, `b_P += rho^n phi^n V / dt` |
| convection | upwind on the owner-oriented face mass flux `F_f`, coefficient 1 |
| diffusion | `Gamma_f |S_f| / d`, `Gamma_f` face-interpolated |
| source | Patankar `S = Su + Sp phi`: `a_P -= Sp V`, `b_P += Su V` |
| Dirichlet | `D_b = Gamma_f A_f / d_Pb`; `a_P += D_b`, `b_P += D_b phi_b` |
| Neumann | prescribed flux `q_b = Gamma_b g A_b` enters `b_P` |

**The mass flux already carries rho.** `physics::calculateMassFlux` returns
`rho (u . S_f)`, so convection assembles `div(rho u phi)` with the transported
quantity entering at coefficient 1. A caller whose `Gamma` is a rho-free
diffusivity (e.g. species `D`) must pass `rho*D`, or the convection and
diffusion terms are dimensionally inconsistent. `SpeciesEquation.hpp` documents
the same requirement for the same reason.

### The transient term is conservative, and that is load-bearing

```
a_P^time uses rho^{n+1}        b_P^time uses rho^n
```

`TransportTimeState` carries `rhoOld` and `rhoNew` as separately named
pointers. Using `rho^{n+1} phi^n` on the right-hand side is the
non-conservative form: invisible in any constant-density test and wrong as soon
as density changes. Separate names make writing it require naming the wrong
one.

Only implicit Euler is implemented. BDF2
(`(3 phi^{n+1} - 4 phi^n + phi^{n-1}) / 2 dt`) is a natural extension of the
same structure and is **not** implemented — it needs a second old level, which
`TransportTimeState` does not carry.

### Face diffusivity: two conventions, one default

```
Arithmetic (default)  Gamma_f = (dNf*Gamma_P + dPf*Gamma_N) / (dPf + dNf)
Harmonic              Gamma_f = d_PN / (d_Pf/Gamma_P + d_fN/Gamma_N)
```

Arithmetic is the default because it is what the production
variable-diffusivity path already used; changing it would have moved existing
numbers. Harmonic is available for a **discontinuous** `Gamma`, where the
flux-preserving value is correct and an arithmetic mean across a large jump
overestimates the interface conductance badly. It reduces to
`2 Gamma_P Gamma_N / (Gamma_P + Gamma_N)` at a midpoint face. Selecting it is
always explicit.

### Sources

`Sp <= 0` is the Patankar stability requirement. It is **validated on request**
(`requireNonPositiveSp`) and **never clamped** — a positive `Sp` throws rather
than being silently zeroed, because a clamp hides a caller's modelling error
behind a matrix that merely looks well behaved. The default is off, so existing
turbulence call sites behave exactly as before.

### Boundary conditions

Reuses `cfd::boundary::ScalarBoundaryCondition` unchanged — `FixedValue`,
`FixedGradient`, and anything else implementing that interface. No second BC
hierarchy was created.

**Robin / mixed (`a phi + b dphi/dn = c`) is not implemented.** It is what a
convective wall (`-k dT/dn = h(T - T_inf)`) needs and belongs with P15's
thermal work.

**The Dirichlet treatment is deferred and iterative.** The boundary
reconstruction (P12-DIFF-002) is evaluated from the *current* field, so the
assembled system depends on the iterate it was built around. **Assemble and
solve once and you get the wrong answer** — measured at max error 231 against
an analytical range of 400. Production iterates; so must any caller.

## Using it

```cpp
#include "cfd/transport/ScalarTransport.hpp"

// Evaluate your model into Su/Sp above this layer -- the assembly path takes
// fields, not virtual callbacks, so it can be mirrored on the device.
cfd::transport::TransportTimeState time;   // steady by default
time.transient = true;
time.dt        = dt;
time.phiOld    = &phiOld;
time.rhoOld    = &rhoOld;
time.rhoNew    = &rhoNew;

const auto assembly = cfd::transport::assembleScalarTransportEquation(
    mesh, phi, massFlux, gamma, boundaries, Su, Sp, nonOrthogonal, time);

const auto result = solver.solve(assembly.system, phi);
```

A future physics module supplies only its own coefficients:

```
thermal    phi = T,  Gamma = k/cp
species    phi = Y,  Gamma = rho*D
k-equation phi = k,  Gamma = mu + mu_t/sigma_k
VOF        phi = alpha
```

## GPU

`cfd::gpu::assembleScalarTransportDevice` assembles the same equation on the
device, composing the already-qualified `assembleScalarDiffusionDevice` and
`assembleScalarConvectionDevice` ports with new source and transient kernels.
It is not host-assemble-then-upload.

**Equivalence is measured, not assumed** (`results/p14/transport-foundation/`):

```
off-diagonals, RHS, structure, source-only systems    BITWISE (0 ulp)
diagonals where both convection and diffusion act     <= 2 ulp
solution relative L2                                  <= 2.1e-12
device determinism                                    bitwise identical
```

The 2-ULP diagonal difference is **association order**, not FMA (the kernel is
compiled `-fmad=false` like every qualified kernel) and not a discretisation
difference: the host accumulates `((d1+d2+..)+c1)+c2+..` while the device adds
each term's total. Off-diagonals receive one contribution per term, which is
why they are exact.

## What this layer does not do

* **Does not migrate thermal, species, turbulence or multiphase.** They keep
  their own assemblers; this is the foundation they move onto in P15–P19.
* **Does not own physics.** No temperature, turbulence or species concept
  appears in it.
* **Layering debt:** the convection contribution delegates to
  `thermal::assembleThermalConvectionContribution` with `cp = 1`, as the
  turbulence path already did. `transport -> thermal` is wrong-way and should
  reverse when P15 migrates thermal onto this layer. Fixing it inside P14 meant
  either editing `thermal/` or copying the loop — a tenth scalar convection
  implementation.
