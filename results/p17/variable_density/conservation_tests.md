# P17.1 — conservation tests

Measured numbers, not verdicts. Raw: `logs/07_conservation.log`, probe
`tools/conservation.cpp`. The in-suite test is
`CompressibleMassFluxTest.RowVaryingDensityConservesMassAndScalesFluxPerRow`.

---

## 1. The derived bound

```text
bound = kAccum * eps * N_f * max|mDot_f|

eps         2.2204460492503131e-16      double epsilon
N_f         the case's own face count   so the bound SCALES with the mesh
max|mDot_f| measured from the case       so the bound SCALES with the flux magnitude
kAccum      8   EMPIRICAL, labelled as such per CLAUDE.md section 5.1 -- a safety
                factor on the summation order (pairwise vs sequential differ by a small
                constant), not a value fitted to an observed result
```

Never an absolute constant. This follows the precedent of the existing
`GlobalMassImbalanceIsSmall`, which already bounds against a computed flux scale rather
than a literal.

For the 3x2 case below: `8 * 2.2204e-16 * 17 * 4 = 1.2079226507921703e-13`.

## 2. The case, and why the answer is exact

```text
mesh            createCartesian2D(3, 2, 3.0, 2.0)   6 cells, 17 faces
temperature     varies by ROW only: 300 K bottom, 600 K top (exactly 2x)
density         the EOS value at each row's temperature, so the field is
                thermodynamically self-consistent -- INCLUDING at boundary faces, where
                in isothermal mode the face takes its owner cell's own temperature
                rho_bottom = 2, rho_top = 1, ratio exactly 2
velocity        uniform (2, 0)
```

The construction makes the discrete result exact rather than toleranced:

```text
* a cell's left and right faces lie in the SAME row, so both take the same interpolated
  face density; their fluxes are equal and opposite and cancel bit-for-bit
* top and bottom faces have a normal along y while U has no y component, so they carry
  exactly zero
=> every per-cell imbalance is EXACTLY 0, with a genuinely non-uniform density
```

## 3. Measured — variable density

```text
max cell imbalance          0
total |imbalance|           0
global net boundary flux    0
mdot_in                    -6
mdot_out                   +6
mdot_in + mdot_out          0
max |mDot_f|                4
derived bound               1.2079226507921703e-13
verdict                     WITHIN BOUND  (exactly zero, 13 orders inside it)
```

## 4. Measured — constant density, the comparison

```text
max cell imbalance          0
total |imbalance|           0
global net boundary flux    0
mdot_in                    -8
mdot_out                   +8
mdot_in + mdot_out          0
max |mDot_f|                4
derived bound               1.2079226507921703e-13
verdict                     WITHIN BOUND
```

Note the throughput differs — **6 vs 8** — because the variable-density case carries
`4 + 2` across its two rows while the constant case carries `4 + 4`. Conservation holds
in both; the *amount* conserved is genuinely different. That difference is the evidence
that the density field is being consumed rather than ignored.

## 5. The discriminator, quantified

Conservation alone is **not** evidence of variable-density correctness: a face density
collapsed to a single constant would still produce fluxes that cancel, so the imbalance
would still be zero. The discriminating quantity is the per-row flux magnitude.

```text
|mDot| on a bottom-row x-normal face     4
|mDot| on a top-row x-normal face        2
ratio                                    2      exactly, matching rho_bottom/rho_top
a collapsed constant face density would give ratio 1
```

Verified against the controls rather than asserted: **controls N4 and N5 both collapse
the face density to a constant, and `RowVaryingDensityConservesMassAndScalesFluxPerRow`
detected both.** It is the only test in the suite that does — see
`AMENDMENT_N_predictions.md`, which records why two existing tests are blind to that
collapse for numeric-coincidence reasons.

## 6. Coverage against the frozen Group K

```text
id   criterion                                        status
------------------------------------------------------------------------------------
K1   closed domain: net boundary mass flux zero        SATISFIED -- globalNetFlux = 0
                                                       exactly (both cases)
K2   through flow: mdot_in + mdot_out = 0              SATISFIED -- exactly 0, with
                                                       mdot_in = -6, mdot_out = +6
K3   per-cell imbalance for a divergence-free field    SATISFIED -- max and total both
                                                       exactly 0
K4   the same with a genuinely varying density         SATISFIED -- rho 2:1 by row
K5   K4 would FAIL if density were internally constant SATISFIED -- by controls N4 and
                                                       N5, both DETECTED
```

## 7. What this does and does not establish

```text
ESTABLISHES   the mass-flux and continuity path conserves mass exactly on a
              constructed variable-density field, and demonstrably consumes the local
              density rather than a global constant.
DOES NOT      establish conservation for a CONVERGED production solve. That is the
ESTABLISH     existing low-Mach suite's GlobalMassImbalanceIsSmall (which uses its own
              computed bound) and the coupled production case's
              MassFlowRateIsConservedAtEveryInternalCrossSection. Both pass in this
              tree -- 137/137 compressible -- and neither was modified by P17.1.
DOES NOT      say anything about ENERGY conservation. No energy equation exists; that
ESTABLISH     is P17.5. See integration_matrix.md section 3.
```
