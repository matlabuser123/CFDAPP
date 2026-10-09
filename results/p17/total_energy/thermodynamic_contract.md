# P17.5 — the thermodynamic energy contract

Every number here is measured on the **unchanged** tree in `logs/01_dryrun.log`
(`tools/dryrun.cpp`), before any total-energy code existed. The contract is derived from those
measurements rather than asserted and then checked.

```text
R  = 287.05                cp = 1005            cv = cp - R = 717.95000000000005
gamma = cp/cv = 1.3998189288947698               eps = 2.2204460492503131e-16
```

---

## 1. ITEM ZERO — the dual-`cp` hazard, and the decision

`audit.md` §4 measured the hazard: `physics.thermal.specific_heat` (a `cp(T)` model, used by P15
to produce temperature) and `physics.compressible.specific_heat_pressure` (one constant, used by
P17.2 to consume it) are parsed independently, **0 sites cross-validate them**, and in a
thermal-coupled compressible case the temperature is produced with one and consumed with the
other.

This had to be settled before anything else, because `rhoE = rho (e + ke)` is defined through
`cp`: an ambiguous `cp` makes the conserved energy ambiguous.

```text
DECISION
  R                   comes ONLY from ThermodynamicProperties (P17.2). It is the EOS's own
                      constant and nothing else may supply it.
  cp, and h(T)        come ONLY from the caller's enthalpy model, i.e. P15's SensibleEnthalpy.
                      The energy path NEVER reads ThermodynamicProperties::specificHeatPressure().
  consistency         is CHECKED, not assumed. The new state object owns both and refuses to
                      construct when the enthalpy model's cp disagrees with the EOS block's cp.
```

The third line is the point. Choosing one source silently would leave a case able to declare two
different gases and get a plausible answer; the hazard would move rather than close. Refusing
construction turns a silent inconsistency into a diagnostic at the only place that can see both.

**Why not simply make the compressible `cp` authoritative?** Because it is a single constant and
cannot represent `cp(T)`, which P15 supports and cases can already select. Making it authoritative
would silently discard a variable-property model a case had asked for — §4 of the authorization
forbids exactly that: *"the calorically perfect formulas are the required constant-property limit,
not permission to break the variable-property model."*

## 2. Naming, fixed here to avoid the `E` ambiguity

The TODO writes `E = rho e_t`, and many formulations use `E` for the *specific* total energy. That
collision is avoided by never using a bare `E`:

```text
e        specific internal energy           [J/kg]
ke       specific kinetic energy            [J/kg]
eTotal   specific total energy              [J/kg]
rhoE     conserved total-energy density     [J/m^3]
h        specific static enthalpy           [J/kg]
hTotal   specific total enthalpy            [J/kg]
```

## 3. The definitions, and the one that carries the whole contract

```text
h(T)      = h_ref + integral_{T_ref}^{T} cp(theta) dtheta      P15 SensibleEnthalpy, closed form
e(T)      = h(T) - R T                                          <-- THE DEFINING CHOICE
ke        = 0.5 |U|^2
eTotal    = e + ke
rhoE      = rho eTotal
hTotal    = h + ke
```

`e(T) = h(T) - R T` is the load-bearing line. It is not one option among several:

```text
1. h - e = R T becomes EXACT BY CONSTRUCTION, for ANY reference state.
   Measured: worst |(h-e) - R T| = 0 at T_ref = 0 AND at T_ref = 298.15. Exactly zero, not
   round-off, because the subtraction is algebraic.
2. de/dT = cv follows automatically, since d/dT[h - RT] = cp - R = cv.
   Measured by central difference at 800 K: 717.94999996 vs cv = 717.95000000, difference
   3.8e-08 -- which is the O(dT^2) truncation error of the difference stencil, not a defect.
3. P15's reference convention is PRESERVED rather than redefined. e inherits its zero point from
   h: e(T_ref) = h_ref - R T_ref. Nothing in P15 moves.
4. h = e + p/rho follows, because p/rho = R T for an ideal gas. So the enthalpy identity and the
   EOS are the same statement, not two that have to be reconciled.
```

And then, without any further assumption:

```text
rho hTotal = rho (h + ke) = rho (e + R T + ke) = rho eTotal + rho R T = rhoE + p
```

so `rho hTotal = rhoE + p` is an identity of the contract, which is why the convective
total-energy flux may be written either as `(rhoE + p) U` or as `m_dot_f hTotal_f`.

## 4. The calorically perfect shortcuts are CONDITIONAL, and the dry-run proved it

Two relations appear throughout the literature and in the authorization:

```text
e = cv T
p = (gamma - 1) [ rhoE - 0.5 rho |U|^2 ]
```

**Both are exact only under the zero reference** (`T_ref = 0`, `h_ref = 0`). Measured:

```text
                                T_ref = 0            T_ref = 298.15
------------------------------------------------------------------------------
worst |h - cp T|                0                    299640.75  = cp * T_ref
worst |e - cv T|                0                    299640.75
p(gamma) - rho R T              -2.91e-11            -143762
  as a relative error           1.7e-16 = 0.76 eps   0.83, i.e. 83 PERCENT WRONG
```

```text
CONSEQUENCE FOR THE IMPLEMENTATION, and it changed the design:
  pressure is recovered as  e -> T -> p = rho R T,  NEVER as p = (gamma-1)(rhoE - ke).
  Had the gamma form been used as the pressure path -- which is how it is usually written -- a
  case with P15's default nonzero reference would have been wrong by 83% and every internal
  consistency check would still have passed, because they would all have used the same wrong p.
  The gamma form is kept ONLY as a verification identity, asserted exclusively where the zero
  reference makes it exact, and measured (0.76 eps) rather than assumed.
```

## 5. `T(h)` inversion, and its inherited tolerance

Temperature recovery goes through P15's exact inverse, not a new solver:

```text
e  ->  h = e + R T ... but T is what is being solved for, so the inversion is on h directly:
  given e, h is NOT known without T. So the recovery inverts e(T) = h(T) - R T, which is
  strictly increasing in T because de/dT = cv > 0. P15's bisection on h is reused by inverting
  the composed monotone function rather than reimplementing a root find.
```

```text
documented bound   SensibleEnthalpy::kInversionToleranceKelvin = 1e-12 K
measured round trip T -> h -> T, worst over {250, 300, 700, 1500} K:
                   4.5474735088646412e-13 K        2.2x inside the documented bound
```

The bound is **inherited verbatim from P15, not re-derived**. Out-of-range is an ERROR in P15 and
stays an error here: no clamping, because a clamped temperature corresponds to an energy the state
does not have and would let a diverging solve look converged.

## 6. The `cp*T` defect this contract must not inherit

`audit.md` §3a recorded that the production thermal path transports `cp*T`. Quantified here with
`cp(T) = 900 + 0.15 T` on `[200, 2000]`, reference 200 K:

```text
T         h(T) closed form      cp(T)*T         cp(T)*T is wrong by
-------------------------------------------------------------------------
400       189000                384000          +195000   = 103.2 %
800       585000                816000          +231000   =  39.5 %
1500      1335750               1687500         +351750   =  26.3 %
```

`|h - closed form| = 0` at every point, so `SensibleEnthalpy` is exact for the configured
polynomial. The naive product is wrong by **tens of percent** — not a round-off concern, a physics
error. P17.5's energy flux therefore carries `hTotal` built from `h(T)`, never `cp(T) * T`.

Repairing the *existing* thermal path is **not** in P17.5's scope and would move every
variable-`cp` result in the qualified baseline; it is recorded as follow-on work.

## 7. Conditioning of `e = rhoE/rho - ke` — measured, not speculated

Authorization §25 asks about catastrophic cancellation. At `T = 300 K`, `rho = 1.2`,
`e = 215385 J/kg`:

```text
|U|      Mach          ke            ke/e          recovered |e_back - e|
--------------------------------------------------------------------------
0.1      0.000288      0.005         2.32e-08      0
1.0      0.00288       0.5           2.32e-06      0
10.0     0.0288        50            2.32e-04      0
100.0    0.288         5000          2.32e-02      0
300.0    0.864         45000         2.09e-01      0
```

The recovered error is **exactly 0** at every speed tested, up to Mach 0.86. The subtraction is
benign here because `rhoE/rho` is *dominated by* `e`: cancellation needs `ke >> e`, which is the
hypersonic end, the opposite of this solver's regime. So no energy floor and no low-Mach
preconditioning is introduced, and §25 is answered with numbers rather than a precaution.

## 8. The identities the closure suite must verify as ONE state

Not as isolated formulas with unrelated values — one coherent `(rho, U, T, p)` state at a time:

```text
p        = rho R T
cp - cv  = R
gamma    = cp / cv
ke       = 0.5 |U|^2
eTotal   = e + ke
rhoE     = rho eTotal
h        = e + p/rho
hTotal   = h + ke
rho hTotal = rhoE + p
e        = cv T        ONLY under the zero reference -- gated on it, not asserted blindly
h        = cp T        likewise
p        = (gamma-1)(rhoE - 0.5 rho |U|^2)   likewise
```

Thresholds are derived in `acceptance_gate.md` from the measurements above: the algebraic
identities are gated at **exactly 0**, and the ones that pass through a floating-point product or
an inversion are gated at a small multiple of `eps` times the quantity's own magnitude, or at
P15's inherited `1e-12 K`.
