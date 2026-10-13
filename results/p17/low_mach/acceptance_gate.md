# P17.6 acceptance gate — FROZEN

Frozen before any P17.6 test code exists. Every threshold is either **INHERITED VERBATIM** from the
frozen G5 criterion in `results/p17/gates/P17_GATES.md`, or **derived** from the dry-run measurements
in `logs/01_dryrun.log` taken on the unchanged tree. None is a round number chosen for comfort.

Carries a mandatory CRITERION → TEST coverage map (§12), completed in `coverage_map.md`.

---

## 0. Scope

```text
IN SCOPE
  a genuine Mach FAMILY: CompressibleSIMPLE solved at several decreasing inlet Mach numbers, each
    against its own matched incompressible SIMPLE reference
  field comparisons: velocity, pressure (gauge-referenced), density, mass flux
  temperature behaviour (isothermal by construction -- the EXPECTED behaviour is stated)
  residual and iteration behaviour, reported not gated on equality
  conservation: mass, an x-momentum budget with an analytical cross-check, and the P17.5 energy
    diagnostic with the KINETIC contribution bounded separately
  determinism, bitwise
  preservation of the existing low-Mach tests and the incompressible SIMPLE references
  an ANTI-FALSE-SUCCESS check that the compressible path is genuinely active

OUT OF SCOPE, and NOT to be claimed
  wiring the energy equation into CompressibleSIMPLE's outer loop. Decided in audit.md 4: not
    needed, because section 12 is answerable by evaluating P17.5's stateless converter on the
    converged fields. That wiring is P17.8's.
  low-Mach preconditioning, a coupled solver, or any density-based method
  HLLC or any Riemann flux. Explicitly forbidden.
  3D. The compressible path is 2D-only.
  changing the EXISTING low-Mach test. G5 requires it unchanged; it is re-run verbatim.
```

## 1. Measured baseline — the dry-run these thresholds come from

The channel of the existing test, 8.0 × 1.0, μ = 0.1, T = 300 K isothermal, p_ref = 101325,
R = 287.05, cp = 1005, mesh 32×6. `rho_ref = p_ref/(R T) = 1.17662428`, **derived** so both
formulations describe the same gas. `a = 347.196 m/s`.

```text
U0     inletMa     machMax     drho/rho    maxEosErr  relL2(|U|)  keRelDiff   itComp  itInc
1      0.0028802   0.0041552   1.1192e-4   0          7.9159e-5   1.6233e-4   1959    2031
0.5    0.0014401   0.0020790   5.4856e-5   0          3.1193e-5   7.8081e-5   2261    2450
0.25   0.00072005  0.0010397   2.7130e-5   0          1.6984e-5   3.8465e-5   2403    2626
0.125  0.00036003  0.00051985  1.3963e-5   0          1.0e-5      1.9409e-5   ~2500   ~2700
```

```text
observed trend exponents over the full 8x range, error ~ C Ma^n
  drho/rho        n = 1.0178      theory for THIS case: 1 (see below)
  keRelDiff       n = 1.0216
  relL2(p')       n = 1.0432
  fluxRelDiff     n = 0.9706
  relL2(|U|)      n = 0.8848
```

**Why the theoretical exponent is 1 and not 2.** For this channel the pressure drop is
viscous-dominated, `dp ~ 12 mu U L / H^2`, not dynamic (`rho U^2`). So
`drho/rho = dp/p ~ (12 mu L / (H^2 p)) U ~ Ma^1`. A quadratic expectation would be wrong here, and
is not assumed.

```text
the FAMILY IS BOUNDED ON BOTH SIDES by the inherited maxIterations = 3000, measured:
  U0 = 2 and 4   the INCOMPRESSIBLE reference hits maxIterations -- so the family cannot go UP
  U0 <= 0.125    iterations climb (1959 -> 2261 -> 2403 -> ~2500) toward the same limit
Section 22 forbids raising an iteration limit to obtain a pass, so the family is 4 levels spanning
8x, descending from the existing test's own anchor (U0 = 1). The iteration growth is recorded as a
CONDITIONING characteristic under section 11, not worked around.
```

## 2. Group M — the Mach family  (the primary gate, G5)

| id | criterion | threshold |
| --- | --- | --- |
| M1 | At least 4 Mach levels, each CONVERGED in both formulations | status == Converged, all levels |
| M2 | The family spans at least 4× in achieved inlet Mach | measured ≥ 4, and the **achieved** Mach is reported, not the requested |
| M3 | `machMax < 0.1` | **INHERITED VERBATIM** |
| M4 | `machMax > 1.25 * inletMach` | **INHERITED VERBATIM** — the non-vacuity clause that stops M3 passing on a flow that never accelerates |
| M5 | `deltaRhoOverRho < 1e-2` at every level | **INHERITED VERBATIM** |
| M6 | `maxEosError < 1e-9` at every level | **INHERITED VERBATIM**; measured **exactly 0** |
| M7 | `maxRelativeMassFluxDifference < 0.15` | **INHERITED VERBATIM** |

## 3. Group A — asymptotic behaviour  (the trend, not one case)

| id | criterion | threshold, and its derivation |
| --- | --- | --- |
| A1 | `deltaRhoOverRho` **strictly decreases** across the family | monotone; measured 1.12e-4 → 1.40e-5 |
| A2 | The velocity difference strictly decreases | monotone on `relL2(\|U\|)`; measured 7.92e-5 → 1.0e-5 |
| A3 | The pressure difference strictly decreases | monotone on `relL2(p')` |
| A4 | The mass-flux difference strictly decreases | monotone on `fluxMaxRelDiff` |
| A5 | The observed exponent for `deltaRhoOverRho` lies in **[0.80, 1.25]** | theory gives exactly 1 for this viscous-dominated case; the band is ±0.2 around it, about 4× the ~0.05 exponent uncertainty implied by the family's 8× span and the ~10 % convergence noise. Measured 1.0178. |
| A6 | Every other error metric has observed exponent **> 0.5** | i.e. genuinely decreasing rather than plateauing. A tighter band is NOT imposed where theory does not pin the exponent. Measured 0.88 – 1.04. |

```text
A5 is the only exponent gated tightly, and only because theory fixes it. A1-A4 gate MONOTONICITY,
which is what section 14 accepts when no formal order is specified.
```

## 4. Group E — energy, and the trap the frozen G5 clause names

The frozen clause: *"at Ma = 1e-3 the kinetic term is ~1e-6 of the internal energy, so a criterion on
e_t can be satisfied while the kinetic part is entirely wrong. The criterion must bound the KINETIC
CONTRIBUTION SEPARATELY, relative to its own magnitude."*

Measured: `ke/e` runs 4.83e-6 → 7.57e-8 across the family — six to eight orders down. A criterion on
`rhoE` would be satisfied by anything.

| id | criterion | threshold |
| --- | --- | --- |
| E1 | The domain kinetic energy of the compressible solution matches the incompressible one, **relative to its own magnitude** | `keRelDiff < 1e-3` at every level; measured 1.62e-4 → 1.94e-5 |
| E2 | `keRelDiff` strictly decreases, exponent > 0.5 | measured n = 1.0216 |
| E3 | `ke/e` is reported at every level and strictly decreases | measured 4.83e-6 → 7.57e-8 |
| E4 | Temperature recovery from `rhoE/rho - ke` returns the imposed T | **exactly 0** at every level, measured |
| E5 | The total-energy BALANCE is reported with its signal-to-noise, and is **NOT gated below S/N = 1** | derived: the balance's noise floor is `\|massImbalance\| * h`, and S/N falls 46.97 → 12.1 → 3.18 → 0.82 |

```text
WHY E5 IS REPORTED AND NOT GATED. On an isothermal solve the balance cannot close: T is an INPUT, so
the isothermal constraint is an unmodelled heat sink, and P17.4 supplies no stress tensor for the
viscous-work term. Worse, h*mdot ~ 3e5 is nine orders above the kinetic gain ~1e-4, so a mass-flux
residual of 1e-9 implies an enthalpy-flux error that SWAMPS the signal. Measured: the
imbalance/predicted ratio is 0.979, 0.917, 0.686 and then -0.220 -- it inverts exactly where S/N
drops below 1. Gating that ratio would be gating noise. E1 is the criterion; E5 is the diagnostic
that explains why E1 has to exist.
```

## 5. Group C — conservation

| id | criterion | threshold |
| --- | --- | --- |
| C1 | `globalMassImbalance <= its computed bound` | **INHERITED VERBATIM** |
| C2 | Inlet and outlet mass flows balance | `\|in + out\| / \|in\| <= 8 eps * N`; measured 1.5e-8 relative at U0 = 1 |
| C3 | The x-momentum budget's implied viscous force matches the analytical Poiseuille wall shear | within **10 %**, and the ratio must APPROACH 1 as Re falls; measured 1.0713 → 1.0559, monotone. The ~6 % excess is the entrance effect of a uniform inlet developing over L/H = 8 on a 6-cell-high mesh, which shrinks with Re -- an explanation the monotone trend supports. |
| C4 | No P17.4 solver-level momentum-balance evaluator exists, so C3 is computed here from the converged fields and cross-checked ANALYTICALLY rather than against a self-closing residual | structural |

## 6. Group P — preservation

| id | criterion |
| --- | --- |
| P1 | `test_low_mach_regression.cpp` runs **UNCHANGED** and passes — all 7 TESTs |
| P2 | `test_mms_compressible.cpp` runs **UNCHANGED** and passes |
| P3 | Poiseuille (5 TESTs) and cavity Ghia (5 TESTs) pass |
| P4 | The P13-001 SIMPLE fingerprint is BITWISE identical, empty diff |
| P5 | P14 **68**, P15 **161**, P16 **315**, compressible **210** + new |
| P6 | Release+CUDA **2642 / 2580** + new; CPU-only **2440 / 2380** + new; 0 failed |
| P7 | `clang-format-18` clean on every changed file |
| P8 | Every P17.1–P17.5 production file is **UNTOUCHED** — by SHA-256, never `git diff HEAD` |

```text
P8 says SHA-256 on purpose. P17.4's L5 used `git diff --numstat HEAD` and reported a file TOUCHED
that the phase never opened, because nothing since P12 is committed. That probe is banned here.
```

## 7. Group D — determinism  (G9)

| id | criterion | threshold |
| --- | --- | --- |
| D1 | A repeated compressible solve from an identical initial state is **bitwise** identical in velocity, pressure, density and mass flux | `==` on every entry |
| D2 | Iteration counts and final residuals are identical | `==` |

## 8. Group X — CPU/GPU

| id | criterion |
| --- | --- |
| X1 | The GPU compressible surface is probed and REPORTED |

```text
X1 IS EXPECTED TO BE "NOT PRESENT", NOT "PASS". P17.4 and P17.5 both measured 0 hits for every
compressible and energy symbol across cuda/ and include/cfd/gpu/. There is no GPU compressible
solver, so there is no GPU low-Mach behaviour to preserve. This phase re-measures and reports it;
it does not claim P17.10's qualification, which the gate matrix marks DEFERRED as vacuous.
```

## 9. Group F — anti-false-success  (§20, and the reason this phase exists)

The audit found the existing low-Mach test never solves the compressible system. P17.6's own harness
must prove it is not repeating that mistake.

| id | criterion | threshold |
| --- | --- | --- |
| F1 | The compressible density is **not uniform** | `rhoMax - rhoMin > 0` at every level, and it must scale with Ma; measured 1.32e-4 → 1.59e-5 |
| F2 | The solver's density satisfies the P17.2 EOS at the solved pressure | `maxEosError < 1e-9`; measured exactly 0 — a constant-density fallback would give O(drho), not O(eps) |
| F3 | The compressible and incompressible solutions **differ** | `relL2(\|U\|) > 0` at every level. If they were bitwise equal the comparison would be measuring nothing. |
| F4 | `CompressibleSIMPLEResult::status == Converged` and `iterations > 0` | a solve genuinely ran |

```text
F3 is the mirror image of A2. A2 requires the difference to SHRINK; F3 requires it to be NON-ZERO.
Together they say "converging to the same answer by a real computation", which neither alone does.
```

## 10. Group N — non-vacuity controls  (each MUST fail)

| id | planted defect | must break |
| --- | --- | --- |
| N1 | Compare the compressible solution against ITSELF instead of the incompressible reference | F3 — the difference becomes exactly 0 |
| N2 | Use `rho_ref = 1.0` (the existing test's inconsistent value) instead of the derived EOS density | A2/E1 — a 15 % density mismatch swamps the trend |
| N3 | Report the REQUESTED Mach instead of the achieved one | M2/M4 |
| N4 | Drop the `1.25 * inletMach` clause | M4 becomes vacuous |
| N5 | Gate the energy criterion on `rhoE` instead of the kinetic part | E1 — must pass trivially, demonstrating the trap G5 names |

```text
N5 is unusual: it must show that the WRONG criterion PASSES. That is the only way to demonstrate the
trap is real rather than theoretical, and it is why E1 is written the way it is.
Each control's probe set is the WHOLE phase suite, not a hand-picked regex -- P17.4's N6 was wrongly
called vacuous because its regex excluded the file holding an existing detector.
```

## 11. Gate verdict

P17.6 closes only when **M, A, E, C, P, D, F all pass, every N control fails, and X1 is reported as
measured**.

```text
FORBIDDEN
  weakening, rescaling or reinterpreting any of G5's six inherited thresholds
  raising maxIterations, or any residual/conservation/field-error threshold, to obtain a pass
  changing the EXISTING low-Mach test or the incompressible reference solution
  gating the energy criterion on rhoE or e_t rather than on the kinetic contribution
  gating the energy BALANCE below S/N = 1
  introducing low-Mach preconditioning or a coupled solver
  `git diff --numstat HEAD` as an "untouched" predicate
  claiming X1 as PASS, or claiming P17.10's qualification
  implementing anything in P17.7 - P17.11, and HLLC in particular
```

## 12. Coverage map

Completed in `coverage_map.md`, a separate file **on purpose**: P17.5 embedded a `TO BE FILLED`
table inside its frozen gate, which meant completing it would have broken the freeze hash. Criteria
and map are frozen separately here.
