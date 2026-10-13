# P16 turbulent heat-transfer benchmark -- FROZEN DEFINITION

**Everything in this file was fixed BEFORE the CFD Nusselt number was
computed**, including the acceptance tolerance and the a-priori prediction in
the last section. Nothing here was chosen after seeing a result.

---

## Reference

**Gnielinski's smooth-wall turbulent internal-flow correlation.**

```
          (f/8)(Re_Dh - 1000) Pr
Nu_Dh = --------------------------------------
        1 + 12.7 sqrt(f/8) ( Pr^(2/3) - 1 )

f = (0.79 ln(Re_Dh) - 1.64)^-2        (smooth-wall friction, Petukhov form)
```

Chosen over Dittus-Boelter because it is valid down to transitional Reynolds
numbers and is the standard engineering reference for smooth turbulent internal
convection.

### What this reference is NOT

It is a correlation for **circular tubes**, applied here to a **planar channel**
through the hydraulic diameter. Hydraulic-diameter substitution is standard
engineering practice for non-circular ducts, but it is an approximation, not a
geometry-exact reference: real duct geometry shifts Nu by a geometry-dependent
amount that the correlation does not carry.

So this is the **P16 engineering benchmark**, not a geometry-exact validation.
A geometry-exact claim would need channel DNS, which is out of scope here and
is recorded as such in `known_limitations.md`.

## Geometry and flow -- from the committed case, unchanged

`cases/turbulent_heated_channel`.

| quantity | value |
|---|---|
| channel height `H` | 0.1 m |
| length `L` | 2.0 m (`L/D_h = 10`) |
| hydraulic diameter `D_h = 2H` | 0.2 m |
| heated area `A = 2 L` (both walls, unit depth) | 4.0 m^2 |
| density | 1.225 kg/m^3 |
| dynamic viscosity | 1.8e-5 Pa s |
| `k_mol` | 0.0257 W/m/K |
| `cp` | 1005 J/kg/K |
| inlet velocity | 10 m/s |
| **`Re_Dh = rho U D_h / mu`** | **1.361e5** |
| **`Pr = mu cp / k_mol`** | **0.704** |
| turbulence | k-epsilon, wall functions on |
| `Pr_t` | default, 0.85 |

`D_h = 2H` is the parallel-plate hydraulic diameter (`4A/P` for an infinitely
wide duct). Frozen here so it cannot be re-chosen later.

## Thermal boundary conditions

* both walls isothermal at `T_w = 400 K`
* inlet `T_in = 300 K`
* outlet zero-gradient

## How Nu_CFD is evaluated -- FROZEN

```
Q_wall      = the wall heat flux from the conservation integrator, computed
              with the SAME k_eff and the SAME boundary reconstruction the
              solve used (see conservation.md). NOT recomputed by a different
              formula here.

T_bulk_in   = 300 K  (prescribed)
T_bulk_out  = Q_out / (mdot * cp), i.e. the mass-flow-weighted mean the
              advective integrator already produces

DT_lm       = ((T_w - T_bulk_in) - (T_w - T_bulk_out))
              / ln((T_w - T_bulk_in)/(T_w - T_bulk_out))     LOG-MEAN

h           = Q_wall / (A * DT_lm)
Nu_CFD      = h * D_h / k_mol
```

**The log-mean temperature difference is the frozen convention**, because the
wall is isothermal and the bulk temperature varies along the channel; an
arithmetic mean would be a different (and, for this BC, wrong) definition.

**Properties are evaluated at the inlet bulk temperature** (300 K) and held
constant, because the case itself uses constant properties. No film-temperature
correction is applied; introducing one after seeing the answer is exactly the
manipulation this freeze exists to prevent.

## Acceptance tolerance -- FROZEN, from reference uncertainty only

Derived from the reference, **never** from the CFD result:

* Gnielinski's own quoted accuracy for smooth tubes: about **+/-10%**;
* hydraulic-diameter substitution onto a non-circular duct: literature reports
  additional geometry-dependent error, commonly of order **+/-15-20%**.

Combined in quadrature: `sqrt(0.10^2 + 0.20^2) ~ 0.22`. Rounded outward to a
frozen band of

```
ACCEPTANCE:   0.5 <= Nu_CFD / Nu_Gnielinski <= 2.0
```

This is deliberately WIDE -- a factor of two either way. It is not a precision
claim; it is a statement that the model must predict turbulent wall heat
transfer of the right ORDER. A correlation-accuracy band of +/-22% would be
indefensible against a tube correlation on a channel, and a band wider than 2x
would accept a model that is qualitatively wrong.

This is kept deliberately separate from the conservation gate. Conservation
asks "did we discretise energy consistently?" and is satisfied at machine
precision (`eps_E = 2.8e-16`). This benchmark asks a completely different
question: "does the model predict credible heat transfer?"

## A-PRIORI PREDICTION -- written before running

**I expect this benchmark to FAIL, over-predicting Nu by roughly a factor of
4-8, and the reason is structural rather than a coding defect.**

There is **no thermal wall function** in this codebase. The momentum side has
one: the Launder-Spalding treatment gives the correct wall shear for a cell
that spans the log layer. The thermal side has no counterpart -- no `T+` law --
so the wall heat flux is taken from the raw cell-centre gradient with `k_eff`:

```
q_w,ours = k_eff (T_w - T_P) / y_P,     k_eff ~ mu_t cp / Pr_t
```

In the log layer `mu_t = rho kappa u_tau y`, so

```
q_w,ours = rho cp u_tau kappa (T_w - T_P) / Pr_t
```

whereas the correct wall-function form is

```
q_w,correct = rho cp u_tau (T_w - T_P) / T+,    T+ ~ Pr_t ln(E y+)/kappa
```

Taking the ratio, `kappa` and `Pr_t` cancel and

```
q_w,ours / q_w,correct ~ ln(E y+)
```

At this case's `y+ ~ 180`, `ln(9.793 * 180) = 7.5`. The coupled solve will damp
that somewhat, because `T_P` rises until the balance closes, so the realised
over-prediction should be smaller than 7.5x but still large.

Hand estimate from the converged run already in hand (`Q_wall = 4.137e4 W`,
`T_bulk_out = 333.6 K`): `DT_lm = 82.1 K`, `h = 126 W/m^2K`,
`Nu_CFD ~ 980`, against `Nu_Gnielinski ~ 228`. Ratio ~4.3.

**If that is what the run produces, the benchmark FAILS and the correct
conclusion is not that the coupling is broken** -- conservation proves the
discretisation is exact, and the unit tests prove `k_t` is right. The
conclusion is that turbulent heat transfer needs a thermal wall function, which
is a MISSING CAPABILITY, and `[x] Eddy-viscosity framework` cannot be marked
until that is either implemented or the scope is explicitly narrowed.

Recording the prediction here, before the run, so the outcome cannot be
rationalised afterwards in either direction.

## Negative controls for this benchmark

* **k_t = 0** -- `Nu_CFD` must collapse toward the laminar/conduction value and
  move far outside the band.
* **Pr_t swept** -- `Nu_CFD` must move monotonically, since `k_t ~ 1/Pr_t`.

---

# RESULT

Run after the definition above was frozen. **The prediction was correct, and
quantitatively so.**

```
Re_Dh            = 1.3611e+05
Pr               = 0.7039
Nu_Gnielinski    = 228.37
Q_wall           = 4.13672e+04 W
T_bulk_out       = 333.59 K
DT_lm            = 82.06 K
h                = 126.02 W/m2K
Nu_CFD           = 980.73
Nu_CFD / Nu_ref  = 4.295          frozen band 0.5 - 2.0   -> FAIL
```

Predicted ratio, written before the run: **~4.3**. Measured: **4.295**.

## Controls -- both PASS

```
A. k_t = 0        Nu = 19.28   (vs 980.73 with k_t; 51x collapse)
                  19.28 / 228.37 = 0.084, far outside the band -> the band
                  discriminates, and the benchmark is sensitive to k_t

B. Pr_t sweep     0.50 -> Nu = 1435.92
                  0.85 -> Nu =  980.73
                  1.50 -> Nu =  680.42
                  3.00 -> Nu =  449.90     strictly monotone, as k_t ~ 1/Pr_t
```

## What this does and does not mean

**It is not a defect in the coupling.** Three independent lines of evidence say
the plumbing is right:

* energy conservation closes at `eps_E = 2.8e-16` using the production
  discrete operator, while the two wrong-flux controls miss by ~1e-1;
* `k_t = mu_t cp / Pr_t` is unit-tested against its closed form, its exact
  laminar limit, and its `1/Pr_t` scaling;
* `mu_t = 0` reproduces the pre-P16 thermal result bitwise, and `Pr_t` changes
  the answer monotonically through the real case path.

**It is a missing capability.** The over-prediction is the `ln(E y+)` factor
derived before the run: the codebase has a momentum wall function but no
THERMAL one. The wall heat flux is taken as the raw cell-centre gradient with
`k_eff` across a cell that spans the log layer, which is precisely the error the
momentum wall function exists to remove on its own side.

**Consequence: `[x] Eddy-viscosity framework` CANNOT be marked.** The completion
gate requires `benchmark PASS`, and it does not pass. Turbulent heat transfer is
implemented, conservative, configurable and reachable -- but not quantitatively
predictive on a wall-function mesh.

## What would fix it

A thermal wall function: a `T+` law imposing the wall heat flux as

```
q_w = rho cp u_tau (T_w - T_P) / T+
T+  = Pr_t [ ln(E y+) / kappa ] + P(Pr/Pr_t)
```

with Jayatilleke's `P` jump function, replacing the raw gradient at wall faces
exactly as the momentum wall function replaces the raw velocity gradient. That
is a bounded, well-specified piece of work -- and it is NOT in this milestone's
authorised scope, so it is recorded rather than started.
