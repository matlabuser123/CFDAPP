# P15 — Thermal / Energy: what was built

Written for: a reviewer deciding whether P15 can close, and an engineer who
later needs to change one of these pieces.

P15 was scoped as a **verification and production-integration closeout**, not
new feature work. That framing held: most of what follows connects, validates
or constrains machinery that already existed. Where something genuinely new was
added, it is called out as such.

---

## 1. What already existed (established by the audit)

`audit.md` records this in full. The short version, because it determines what
"implementing P15" could honestly mean:

| Capability | State before P15 |
|---|---|
| Energy equation, scalar properties | Complete, validated |
| Energy equation, **ScalarField** properties | Complete — `assembleEnergyEquation` already had field overloads |
| Picard loop over varying properties | Complete — `ThermalSolver::solve`'s variable-property overload |
| `cfd::physics::TemperatureProperty` (Constant/Linear/Tabulated) | Complete since P3-PHYS-003 |
| `evaluatePropertyField` with positivity checking | Complete |
| A **model** producing k(T) or cp(T) | **Absent** — zero matches for a polynomial property anywhere |
| Enthalpy h(T), inverse T(h) | **Absent** |
| Convective (Robin) boundary | **Absent** |
| Transient term in the energy equation | **Absent** |
| Any case-file route to a varying property | **Absent** — see §5 |

So the gap was never "the solver cannot do varying properties". It was that
nothing could *produce* the variation, nothing could integrate cp to enthalpy,
there was no Robin wall, there was no time derivative, and no case file could
reach any of it.

---

## 2. New numerics

### `cfd::thermal::TemperatureDependentProperty`

A polynomial in temperature with an **explicit validity range**.

- Horner from the highest coefficient down. The order is part of the contract
  and is stated in the header, because a polynomial summed in a different order
  gives different bits and this project compares bits.
- A constant is the single-coefficient case — the same class, so the constant
  and varying paths cannot drift apart.
- Positivity is checked over the whole range at construction by sampling 1024
  points. Sampling can in principle miss a narrow dip; that limitation is
  stated in the header rather than hidden.
- Out of range is an **error**, not a silent extrapolation. A fitted cubic cp
  can go negative a few hundred kelvin past its data, which makes enthalpy
  non-monotonic and the T(h) inversion ill-posed.

It **implements `cfd::physics::TemperatureProperty`**, the interface the solver
already consumed. That inheritance edge is the whole of the integration work —
see §5.

### `cfd::thermal::SensibleEnthalpy`

`h(T) - h_ref = Σ b_i/(i+1) · (T^{i+1} - T_ref^{i+1})` — the **closed-form
integral** of the cp polynomial, not `cp·T`.

This is why `TemperatureDependentProperty` exists alongside the pre-existing
property hierarchy rather than being replaced by it: integrating cp needs the
coefficients *symbolically*, which a virtual `value(T)` cannot provide.

`temperature(h)` inverts by bisection to `1e-12` K. Out-of-range throws; it
never clamps, because a clamped inverse silently returns a temperature the
enthalpy does not correspond to.

### `cfd::boundary::ConvectiveHeatTransfer` (Robin)

Series resistance through the wall and then the film:

```
G = 1 / (d/k + 1/h_c)          h_c = 0 is the insulated limit, handled explicitly
T_w = ( (k/d)·T_P + h_c·T_inf ) / ( k/d + h_c )
```

Checked against both physical limits: `h_c → 0` gives the cell value at the
wall, `h_c → ∞` pins the wall to `T_inf`.

`boundaryValue()` returns the owner value — the zero-gradient equivalent. **This
reverses an earlier decision of mine and the reason matters**, so it is recorded
in full in §6.

### Transient term

Conservative implicit Euler:

```
a_P += ρ^{n+1} c_p^{n+1} V / Δt
b_P += ρ^{n}   c_p^{n}   T^{n} V / Δt
```

The old **level** supplies ρ and cp on the right-hand side, not the new one.
Mixing the levels there is the classic way to break discrete conservation while
still converging to something plausible; NC7 exists to prove the trap fires.

A default-constructed `ThermalTimeState` adds **nothing at all** — the code is
not executed, so the steady path is bitwise what it was.
`ThermalTransient.SteadyStateIsBitwiseUnchanged` holds that.

---

## 3. Architecture decisions taken, and their cost

### Robin on the GPU: Option C (deferred), closed by the user

`robin_architecture_decision.md` has the full analysis. The blocker is
structural, not effort:

`G = 1/(d/k_f + 1/h_c)` needs the runtime conductivity. A device plan is built
**once** from mesh and boundaries alone, before Γ exists, and the device encoder
represents a boundary condition by probing `boundaryValue()` — which cannot be
given k through that interface. Changing the interface touches 7 scalar BCs, 5
vector BCs and 51 call sites.

So a mesh carrying a convective wall is classified **GPU-unsupported** and runs
on the CPU. What P15 owed was that this be *graceful and honest*, which §4
covers.

### The generic transport path refuses Robin, loudly

`cfd::transport::assembleScalarDiffusionContribution` throws on a convective
boundary, naming the condition and the runtime-diffusivity cause. This is the
one place a generic scalar solve could otherwise treat a convective wall as
adiabatic and report convergence on the wrong problem.

---

## 4. The device refusal, and a defect the gate found

Acceptance criteria 3–5 required that a convective wall make a device plan
report itself unsupported **without throwing**, with a reason naming the actual
cause. Criteria 3 and 4 passed. **Criterion 5 failed**, and it failed in an
instructive way:

```
gradient plan unusable: face 4: boundary condition 'ConvectiveHeatTransfer'
is not bitwise-reproducible as a constant, a shift or an affine function of
phi_P; mesh rejected for the device gradient path
```

Two things wrong, and only the first is what the criterion asked about:

1. it does not name the cause;
2. **it states something untrue**. A Robin wall *is* affine in T_P — the
   encoder could represent that form. What it cannot obtain is k.

The type-specific reason already added to `DeviceDiffusionPlan` and
`DeviceConvectionPlan` was never reached: the **gradient** plan builds first and
its generic message shadowed both.

The fix was not a third hand-written copy. `BoundaryEncoding.hpp` — whose own
header says *"Moved, not copied: two copies of this would drift"* — now owns
`unsupportedEncodingReason()`, used at all four scalar rejection sites. For
every condition other than Robin the message is reproduced **byte for byte**,
including each site's own noun and path clause.

The refusal now reads:

```
face 4: boundary condition 'ConvectiveHeatTransfer' requires the runtime
thermal conductivity to form its coefficients and is not supported by the
current device gradient plan; the CPU thermal assembly handles it
```

The probe was then tightened to fail if the message ever again claims the
condition is not representable — the defect is now a gate, not a memory.

---

## 5. Case-file reachability, and the gap it exposed

`physics.json`'s `thermal.conductivity` and `thermal.specific_heat` each accept
**either** a number (the pre-existing constant, unchanged) **or** an object
selecting a model:

```json
"conductivity": {
  "model": "polynomial",
  "coefficients": [2.0, 0.01],
  "min_temperature": 200.0,
  "max_temperature": 1200.0
}
```

Two layers validate it, which is this file's documented convention:

- the **parser** checks structure and evaluates at `initial_temperature`. It
  deliberately does *not* construct a `TemperatureDependentProperty` — that
  would create an `io → thermal` dependency. It runs Horner inline in the same
  operand order, so the value a case reports and the value the model computes
  agree bit for bit.
- **CaseBuilder** constructs the real object, which re-validates positivity over
  the *whole* range — something the parser cannot do, since it only evaluated at
  one temperature.

### The gap

Writing the parse tests exposed that the model reached the typed config **and
then stopped**. `CaseBuilder` read only the evaluated scalar and built a
constant `ThermalProperties`. A case naming a varying conductivity would have
been solved with a fixed one — precisely the silent fallback P15 forbids, and
worse than not parsing the block at all, because the case file would claim a
capability the run does not deliver.

Worth recording: `ThermalSolver`'s variable-property overload has existed since
**P3-PHYS-003** and no case file could ever reach it. This closes a pre-existing
reachability gap; P15 did not introduce it.

### The fix, which invents nothing

One inheritance edge —
`TemperatureDependentProperty : public cfd::physics::TemperatureProperty` — plus
the routing it unlocks:

- `SimulationSetup` carries two `shared_ptr<const TemperatureProperty>`,
  populated **iff** the object form was used;
- `ProjectRunner` selects the variable-property `solve()` when either is
  present, wrapping the constant side in `ConstantProperty` (that class's
  documented purpose, not a substitution invented here);
- neither present → the original call, unchanged.

No new numerical convention, which was the standing P14/P15 constraint.

### Out-of-range during a solve

`value()` throws, deliberately, and is **already handled**: the variable-property
overload wraps `evaluatePropertyField` in a try/catch that turns
`InvalidArgumentError` into `NumericalError`, which `runPicardLoop` reports as
`ThermalStatus::NonFiniteState`. A mid-iteration excursion past the fitted range
surfaces as a *failed solve naming the property* — not an uncaught exception,
and not a confidently extrapolated wrong answer. No third policy was introduced;
the existing channel was used.

---

## 6. A reversal, recorded

`ConvectiveHeatTransfer::boundaryValue()` originally **threw**, on the argument
that a plausible wrong value would be consumed silently by every generic
assembler — "the worst possible failure mode".

The first run of the thermal validation suite showed the argument was right
about the generic *assembly* path and wrong about the *gradient*:
`discretization::gradient()` evaluates `boundaryValue()` on every boundary face
while building the correction gradient, so throwing killed every convective case
before any physics ran.

It now returns the owner value. That is sound because of what consumes it: the
correction gradient feeds **only** the non-orthogonal correction, which is
identically zero on an orthogonal face. On an orthogonal mesh the value has no
effect whatsoever; on a non-orthogonal one it perturbs a correction term.

The loud refusal did not disappear — it moved to `cfd::transport`, the one place
it can actually be reached.

**A loose end from that reversal was found by the negative-control run.** The
unit test `ConvectiveBoundary.BoundaryValueThrowsRatherThanGuessing` still
pinned the old contract and had been failing since the reversal. It was not
deleted: it was re-pointed at where the guarantee moved to, and a second test
now pins the new `boundaryValue` contract. The control harness's own "does the
restored tree still pass" self-check is what surfaced it — which is exactly what
that check is for.

---

## 7. Deferred, with reasons

| Item | Why |
|---|---|
| Robin on the GPU | `G` needs runtime k; the plan predates Γ. Option C, closed by the user. Mesh is classified unsupported and runs on the CPU. |
| A full thermal CUDA solver | The user's explicit instruction: do not expand P15 into one; document it as deferred. |
| Per-face boundary values | Would make the exact Robin gradient treatment possible. `BoundaryConditionSet` is per-*patch*; this is Option B's territory. |
| `linear` / `table` case models | The case format already dispatches on `"model"`, so adding them needs no new convention — only the parser branch and the `TemperatureProperty` subclass, both of which already exist. |
