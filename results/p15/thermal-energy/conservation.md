# P15 — conservation

Written for: a reviewer checking that the energy equation conserves what it
claims to, and that the check could fail.

---

## The discrete statement

`ThermalConservation.DiscreteEnergyBalanceCloses` asserts that the assembled
system satisfies its own balance cell by cell:

```
A·T − b = 0   (to solver tolerance, over every cell)
```

This is deliberately a statement about the **assembly**, not about a
post-processed flux sum. A flux-sum check re-derives the fluxes with its own
code and can agree with a broken assembly if both make the same mistake. Asking
the assembled operator whether it balances its own right-hand side cannot.

Boundary fluxes enter through the same path the solve uses — there is no second,
test-only flux routine to drift.

---

## Steady-state flux constancy

For 1-D steady conduction with no source, `k dT/dx` is constant across the
domain. Case 5 of the transport verification suite checks exactly that, face by
face.

*This metric replaced a worse one.* The first version summed a flux over a
region that **excluded the wall-to-first-centre interval** — so it could report
balance while the boundary contribution was wrong, which is the one place a
finite-volume conduction scheme most commonly is wrong. Face-flux constancy over
every face has no such blind spot.

---

## Transient conservation

The conservative implicit-Euler form is:

```
a_P += ρ^{n+1} c_p^{n+1} V / Δt
b_P += ρ^{n}   c_p^{n}   T^{n} V / Δt
```

The old **level** supplies ρ and cp on the right-hand side. Using `ρ^{n+1}`
there breaks discrete conservation while still converging to something
plausible — and the error is identically zero at constant density, so it only
appears when ρ varies in time, which is precisely when nobody is looking at it.

Two independent guards:

- `ThermalTransient.OldTimeRhsUsesOldDensityAndOldSpecificHeat` pins the level
  assignment directly, with a density and specific heat that actually differ
  between levels — otherwise the test would be vacuous by construction.
- **NC7** substitutes the new level and requires the trap to fire. It does.

---

## Why these gates are not vacuous

| Gate | Control | Result |
|---|---|---|
| `ThermalConservation.*` | NC8 — perturb the accounting by `1e-6` relative | **DETECTED** |
| transient level assignment | NC7 — new density on the RHS | **DETECTED** |
| heat-source balance | NC2 — source zeroed | **DETECTED** |

NC8's perturbation is worth noting: `1e-6` **relative** is far below anything a
casual eyeball check would catch, and the gate still fails on it. That is the
difference between a conservation check with a meaningful tolerance and one
whose tolerance is wide enough to admit the defect it exists to find.

Full control log: `logs/final/negative_controls.log`. Discussion:
`negative_controls.md`.

---

## Scope

These are **discrete** conservation statements about the energy equation as
assembled. They are not claims about global energy conservation of a coupled
momentum–energy solve; P15 did not change the coupling, and the SIMPLE
fingerprint check in `regression.md` is what holds that unchanged.
