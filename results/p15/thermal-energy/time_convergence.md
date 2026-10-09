# P15 — timestep convergence

Written for: a reviewer checking that the transient term integrates at the order
its scheme claims.

---

## What was measured

`TimestepConvergence.ImplicitEulerIsFirstOrderInTime` integrates the transient
energy equation to a fixed end time on a fixed mesh, halving the step:

```
steps =   10   dt = 0.20000   error = 1.286465e+00
steps =   20   dt = 0.10000   error = 6.365978e-01
steps =   40   dt = 0.05000   error = 3.154636e-01
steps =   80   dt = 0.02500   error = 1.531360e-01

steps   10 ->   20   observed order p = 1.0150
steps   20 ->   40   observed order p = 1.0129
steps   40 ->   80   observed order p = 1.0427
```

**p ≈ 1. That is the correct answer**, not a shortfall: the scheme is
conservative implicit Euler, chosen for unconditional stability and for the
`a_P`/`b_P` structure it shares with the steady assembly.

---

## Why the reference is not the analytical solution

The discrete solution carries two independent errors: spatial `O(h²)` and
temporal `O(Δt)`. Comparing against the exact PDE solution measures their
**sum**, so refining Δt alone stops improving once the spatial error dominates —
and the measured "order" then decays toward zero for reasons that have nothing
to do with the time scheme.

That is a trap, not a test. So the reference is the **same semi-discrete system
on the same mesh**, integrated with 1280 steps — 16× finer than the finest
compared against it. The spatial error is then identical in both and cancels
exactly, leaving the temporal error alone. The reference's own temporal error is
~1/16 of the smallest measured one, so it does not distort the ratios.

---

## Criteria, and why they bracket from both sides

| Criterion | Rules out |
|---|---|
| every error > 0 | the reference and the run being the same integration — a study that measures nothing |
| error falls at every halving | a transient term that does not respond to Δt |
| every `p > 0.8` | the transient term not doing what implicit Euler does |
| every `p < 1.3` | a wrong reference or metric |

The upper bracket is deliberate. A first-order scheme **cannot** converge faster
than first order, so a p near 2 would mean the reference or the error metric is
wrong — not that the scheme got better. Measuring an implausibly good result and
recording it as success is how a study stops being evidence.

---

## What the transient term must get right, and where that is checked

The conservative form takes ρ and cp at the **old level** on the right-hand
side:

```
a_P += ρ^{n+1} c_p^{n+1} V / Δt
b_P += ρ^{n}   c_p^{n}   T^{n} V / Δt
```

Using `ρ^{n+1}` on the right-hand side is the classic way to break discrete
conservation while still converging to something plausible — the error is
invisible at constant density and appears only when ρ varies in time, which is
exactly when nobody is looking.

- `ThermalTransient.OldTimeRhsUsesOldDensityAndOldSpecificHeat` pins the level
  assignment directly.
- **NC7** substitutes the new level and requires the conservation trap to fire.
  It does.

`ThermalTransient.SteadyStateIsBitwiseUnchanged` holds the other half: a
default-constructed `ThermalTimeState` executes no transient code at all, so
every steady result is bitwise what it was before P15.
