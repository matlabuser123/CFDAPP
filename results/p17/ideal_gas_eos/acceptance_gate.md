# P17.2 — Ideal-Gas EOS: acceptance gate

**Frozen 2026-09-26, after the dry-run and before any production edit.** Every baseline
is *measured* on the unchanged tree and recorded in `logs/00_dryrun.log`, never inferred.
Stop at the first failure. No threshold may be weakened; a mis-derived criterion is
preserved and amended in a separate file.

`eps` below means the double epsilon, `2.2204460492503131e-16`.

---

## 0. What this item changes

```text
ONE production addition:
  EquationOfState::temperature(pressure, density)   pure virtual, T = p/(rho R)
  IdealGasEOS::temperature(...)                     the override
```

Everything else is verification, testing or documentation of behaviour that already
exists. `audit.md` §4 classifies all thirteen requirements; twelve are already satisfied
or are documentation.

```text
NOT changed   the R + cp configuration convention (cv and gamma stay derived)
NOT added     e(T), T(e)  -> P17.5.  HLLC/wave speeds -> P17.7.  A validity BAND on the
              EOS -> not applicable to an ideal gas, see group R.
NOT touched   P15 reference-state conventions; the 22 pre-existing clang-format failures
```

## 1. Measured baseline — the dry-run record

```text
EquationOfState pure virtuals     density, dDensityDPressure, dDensityDTemperature,
                                  pressure          -- temperature() ABSENT (count 0)
implementations                   1 (IdealGasEOS)
GPU EOS arithmetic                gasConstant 0, speedOfSound 0, idealGas 0  -- NONE
existing tests                    test_ideal_gas_eos.cpp 21, test_thermodynamic_properties.cpp 13
```

```text
identity residuals, measured
  (cp-cv)-R        air -5.6843418860808015e-14   helium 0   water vapour 0
                   -> 0.89 eps relative to R, worst case
  gamma-cp/cv      0 for all three gases          -> EXACT
  p - rho*R*T      up to -1.4551915228366852e-11 absolute at p = 101325
                   -> 1.44e-16 relative = 0.65 eps
  pressure(rho,T) recovering p        up to 2.2737367544323206e-16 relative = 1.02 eps
  sqrt(gamma R T) vs sqrt(gamma p/rho) up to 2.005166712758568e-16 relative = 0.90 eps
  a strictly increasing in T          yes, over T = 100 … 2400
  determinism, 64 repeats             density / pressure / speedOfSound BITWISE stable
                                      rho = 1.2250122659906946, p = 101325.00000000001,
                                      a = 340.27028004636702
invalid states already rejected
  p in {0, <0, NaN, +Inf}             all THREW "IdealGasEOS: pressure must be finite and > 0"
  T in {0, <0, NaN, +Inf}             all THREW "IdealGasEOS: temperature must be finite and > 0"
  R in {0, <0, NaN, +Inf}             all THREW "IdealGasEOS: gasConstant must be finite and > 0"
  cp in {== R, 100, 0, -1}            all THREW "… cp must be finite and > gasConstant (R) …"
```

**The gap is non-vacuous:** `temperature()` is absent, and the dry-run shows that
recovering `T` today forces the *caller* to write `p/(rho*R)` itself — duplicating the EOS
relation outside the EOS, the exact thing `EquationOfState`'s own header says the
abstraction exists to prevent.

### A display artifact, recorded so it does not mislead

The dry-run prints one rejected case as `cp = 287.1`. The value passed was `287.05`,
exactly equal to `R`; `%-8.4g` rounded it for display. The rejection is correct — `cp > R`
is required, and `cp == R` gives `cv = 0` — but the printed number makes it look like
`cp > R` was rejected. Not a defect; a formatting artifact in my probe.

## 2. Group C — the three-way closure  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| C1 | `density(p,T)` returns `p/(R T)` against an independent expectation. | rel <= 1e-12 |
| C2 | `pressure(rho,T)` returns `rho R T`. | rel <= 1e-12 |
| C3 | **`temperature(p,rho)` returns `p/(rho R)`.** NEW. | rel <= 1e-12 |
| C4 | Round trip `(p,T) -> rho -> p`. | rel <= 1e-12 |
| C5 | Round trip `(rho,T) -> p -> rho`. | rel <= 1e-12 |
| C6 | Round trip `(p,rho) -> T -> p`. NEW. | rel <= 1e-12 |
| C7 | All of C1-C6 hold across **3 gases x 3 state ranges** — low/ambient/high p and T — not one nominal point. | >= 9 states |
| C8 | `temperature()` is reachable through `EquationOfState&`, without down-casting. | compiles and agrees |

**Why 1e-12:** inherited verbatim from the earlier P17.2 gate, where it was derived from
two roundings of a double (~4500x eps) and proved load-bearing by a control perturbing `R`
by 1e-6. The dry-run measures the actual residuals at **0.65-1.02 eps**, so 1e-12 is about
four orders of slack — deliberately, because it is the *established* project bound for
this quantity and re-deriving a tighter one per item would make results incomparable.

## 3. Group T — thermodynamic property identities  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| T1 | `cp - cv = R`. | **rel <= 4 eps** — see derivation |
| T2 | `gamma = cp/cv`. | **EXACT**, `==` |
| T3 | `gamma > 1` for every constructible object. | boolean |
| T4 | `cv > 0`. | boolean |
| T5 | T1-T4 hold together for the **same** object across 3 gases — one coherent state, not four separate formula tests. | >= 3 gases |

```text
DERIVATION of T1's 4 eps
  cv is computed as one subtraction, cp - R, which rounds once. Recovering cp - cv rounds
  once more. Two roundings of quantities of magnitude ~cp give an error of order
  (cp/R) * eps relative to R; for air cp/R ~ 3.5, so ~3.5 eps is the floor. Measured worst
  case 0.89 eps. 4 eps bounds the measurement with margin while remaining ~250 billion
  times tighter than the 1e-12 used for the state relations. NOT equality: the dry-run
  proves equality fails for air.

WHY T2 IS EXACT and T1 IS NOT
  gamma is STORED as cp/cv. Recomputing cp/cv repeats one operation on unchanged bits, so
  it returns the same double. cv, by contrast, is stored having already lost information
  in cp - R, and that loss cannot be undone. The asymmetry is real and is gated as such.

T3 IS A THEOREM, NOT A CHECK
  R > 0 is enforced by IdealGasEOS; cp > R by ThermodynamicProperties. Hence
  cv = cp - R > 0 and gamma = cp/(cp-R) > 1 <=> R > 0. So T3/T4 cannot fail for any
  constructible object. They are gated as OBSERVABLE assertions over a range of admissible
  (R, cp) rather than as constructor guards, because a constructor guard for them would be
  unreachable code.
```

## 4. Group A — speed of sound  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| A1 | `a = sqrt(gamma R T)`. | rel <= 1e-12 |
| A2 | `sqrt(gamma R T) == sqrt(gamma p / rho)` for the same state. NEW. | **rel <= 8 eps** |
| A3 | `a > 0` and finite for every accepted state. | boolean |
| A4 | `a` strictly increases with `T` at fixed `gamma, R`. | boolean, >= 6 temperatures |
| A5 | `a` rejects a non-positive or non-finite `T`. | throws |

```text
DERIVATION of A2's 8 eps
  The two forms are different arithmetic paths to the same value. Path 1: gamma*R*T then
  sqrt -- 2 multiplications + 1 sqrt. Path 2: rho = p/(R T) (2 ops), then gamma*p/rho
  (2 ops), then sqrt. Path 2 carries ~4 roundings and path 1 ~3, so their difference is
  bounded by roughly 7 eps in the worst case. 8 eps is the next power of two above that
  and is derived from the operation counts, not from the observed result. Measured worst
  case 0.90 eps.
  A2 is NOT gated as equality: the paths genuinely differ, and demanding bitwise identity
  would fail for a correct implementation.
```

## 5. Group R — the validity domain  (DOCUMENTATION + DETECTION)

| id | criterion |
| --- | --- |
| R1 | The `IdealGasEOS` **mathematical** domain is documented as exactly `p > 0, rho > 0, T > 0, R > 0`, all finite — no upper bound, no fitted range. |
| R2 | That domain is documented as **distinct** from P15 `TemperatureDependentProperty`'s correlation range `[Tmin, Tmax]`, which exists because `cp(T)`/`k(T)` are polynomial fits. |
| R3 | No arbitrary physical limit is invented. |
| R4 | The three validation layers are named and kept separate: configuration/input validation, runtime EOS state validation, internal numerical protection. |

```text
R3 is a criterion about what the item must NOT do. For an ideal gas, p = rho R T holds for
all positive finite arguments; inventing a ceiling would reject valid high-pressure states
and would be a fabricated limit. Recorded as a gate item so that a future reader sees the
absence of a band was DECIDED, not overlooked.
```

## 6. Group V — invalid-state guards  (DETECTION)

| id | criterion |
| --- | --- |
| V1 | `temperature()` rejects non-positive and non-finite `p`. NEW |
| V2 | `temperature()` rejects non-positive and non-finite `rho`. NEW |
| V3 | `temperature()` rejects a non-finite or non-positive **result**. NEW |
| V4 | Messages name the offending field, as the two siblings do. |
| V5 | The existing rejections still hold: `p`, `T`, `R`, `cp` — all four classes, 16 cases, as measured in the dry-run. |
| V6 | No invalid state is silently clamped anywhere. |

## 7. Group D — determinism

| id | criterion | bound |
| --- | --- | --- |
| D1 | `density`, `pressure`, `temperature`, `speedOfSound` are bitwise repeatable. | **`==`**, 64 repeats |
| D2 | Repeated full density-field evaluation over a mesh is bitwise identical. | `==` |

Bitwise is the correct criterion here and needs no caveat: every path is straight-line
double arithmetic with no reduction, no threading and no iteration. The dry-run already
measured the three existing entry points as bitwise stable over 64 repeats.

## 8. Group K — constant-property compatibility

| id | criterion |
| --- | --- |
| K1 | `R`, `cp`, `cv`, `gamma` are constants, and every accessor returns the same value on every call. |
| K2 | P15's `SensibleEnthalpy` with a **constant** `cp` reduces exactly to `h = h_ref + cp (T - T_ref)`. |
| K3 | P15 reference-state conventions are unchanged — no edit to `Enthalpy.hpp` or `TemperatureDependentProperty`. |
| K4 | `e(T)` is **absent** and stays absent. Asserting its absence is the criterion; adding it is P17.5. |

## 9. Group I — P17.1 integration

| id | criterion |
| --- | --- |
| I1 | A known `(p, T, R)` flows through the EOS into the authoritative P17.1 density field and arrives at the expected value. |
| I2 | `evaluateDensityField` uses the same EOS instance — no second gas constant, no duplicated `p/(R T)` on the production path. |
| I3 | Verified by grep that no production file computes `p/(R*T)` outside `IdealGasEOS`. |
| I4 | The P17.1 face-density and mass-flux path consumes that same field. |

## 10. Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | `temperature()` returns `p/rho` (drop `R`). | C3, C6 for any `R != 1`; **not** C1/C2 — proving the new criteria are what detect it |
| N2 | `temperature()` returns a constant. | C3, C6, C7 at every non-default state |
| N3 | `temperature()` drops its input validation. | V1, V2 |
| N4 | Perturb `R` by 1e-6 relative inside `temperature()` only. | C3 and C6 at the 1e-12 bound, while C1/C2 still pass — proves the bound's *tightness* detects, not the plumbing |
| N5 | `speedOfSound` returns `sqrt(gamma R T)` with `gamma` replaced by 1. | A1, A2, A4 |
| N6 | `cv` stored independently as a rounded constant instead of `cp - R`. | T1 at 4 eps, while T2 may still pass — proves T1's bound is load-bearing |

N4 and N6 are the two that prove criteria rather than plumbing. N6 is the sharpest: it
attacks the exact asymmetry the dry-run exposed between T1 and T2.

## 11. Group G — preservation gate

| id | criterion |
| --- | --- |
| G1 | EOS + thermodynamics tests pass: 21 + 13 existing, plus the new ones. |
| G2 | The whole compressible suite passes — **137** at the P17.1 baseline, plus new. |
| G3 | P14 transport — **68**, frozen narrow regex. |
| G4 | P15 thermal — **161**. |
| G5 | P16 turbulence — **315**. |
| G6 | The P13-001 SIMPLE fingerprint is **BITWISE identical**, 10 cases / 130 lines, empty diff. |
| G7 | Release+CUDA regression: **2505 + new**, 0 failed. |
| G8 | CPU-only regression: **2305 + new**, 0 failed. |
| G9 | `clang-format-18` clean **on the files P17.2 changes**. The 22 pre-existing failures are recorded debt and are neither fixed nor counted here. |
| G10 | `P17_CODE_FINGERPRINT` moves only by the authorized delta, attributed file by file. |

G7/G8 baselines are P17.1's measured counts in `results/p17/variable_density/regression.md`.
The expected new totals are predicted exactly, before the run, in `regression.md`.

## 12. Gate verdict

P17.2 closes only when **C, T, A, R, V, D, K, I and G all pass and every N control
fails**, with the delta attributed file by file and both fingerprints recorded.

```text
FORBIDDEN
  weakening any bound above, including T1's 4 eps and A2's 8 eps
  gating T1 as equality (the dry-run proves it fails for air)
  gating A2 as equality (the two paths genuinely differ)
  inventing an upper bound or a fitted range for the ideal-gas EOS
  clamping any invalid state
  changing the R + cp configuration convention
  implementing e(T), T(e), HLLC, or anything in P17.3-P17.10
```
