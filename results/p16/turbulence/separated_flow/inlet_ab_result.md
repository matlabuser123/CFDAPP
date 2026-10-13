# P16 — inflow verification and the medium-grid A/B

Written for: the reader deciding whether the undeveloped-inlet root cause was
the right diagnosis. **It was.**

Raw log: `../logs/ab.log`.

## 1. Inflow verified BEFORE any separation result

| Quantity | Measured | Reference | |
|---|---|---|---|
| **delta/H** | **1.5097** | ~1.5 | PASS |
| Extraction station | x/H = 93.50 | found by scan | — |
| C_f | 0.002865 | > 0 | PASS |
| Mass flow | 7.999998 | 8.000000 | PASS |
| max mu_t/mu | 250.2 | strongly turbulent | PASS |
| **k peak** | **19.6x centreline**, at y/H = 0.150 | inside the layer | PASS |
| omega | decreases wall to core | | PASS |
| Mapped inlet mass flow | 8.021146 (0.26% of precursor) | < 2% | PASS |

The k figure is the one that matters. The OLD inlet was flat by construction,
so the incoming layer carried essentially no turbulence.

Development length measured at **93.5H**, not the 79H a flat-plate estimate
predicted -- which is why the station is scanned for rather than assumed.

## 2. The controlled A/B (medium grid, everything else fixed)

| inlet | x_r/H | **E_x_r** | max mu_t | iterations | converged |
|---|---|---|---|---|---|
| old analytic | 8.502 | **0.3582** | 7.994e-3 | 7988 | yes |
| **mapped precursor** | **7.583** | **0.2113** | 1.083e-2 | 7655 | yes |

**41% of the error removed.** E_x_r falls from 0.3582 to **0.2113 -- below the
frozen cap of 0.25.**

The old inlet reproduced its recorded 8.502 exactly, so the comparison did
isolate the inlet and nothing else drifted.

## 3. The hypothesis survives, and the mechanism is visible

Predicted before the run: too little turbulence at the step lip gives a shear
layer that mixes too slowly and reattaches too far downstream.

Observed: max mu_t rose **7.994e-3 -> 1.083e-2 (+35%)** with the developed
inlet, and reattachment moved **upstream** by 0.92 step heights. The incoming
turbulence now feeds the shear layer, it mixes faster, and the bubble shortens.
Right direction, right mechanism, and a material fraction of the error.

The refutation condition fixed in `reference_specification.md` section 9 --
"if the corrected inlet does NOT move x_r/H materially toward 6.26" -- did not
trigger. No investigation of wall-function applicability, near-wall coupling,
the limiter, convection discretisation, outlet influence, wall distance or
unsteadiness is required on this evidence.

## 4. What this does NOT yet establish

**One mesh is not a grid sequence.** With the OLD inlet the sequence was
6.51 -> 8.50 -> 9.44, moving monotonically AWAY from the reference under
refinement. A single medium-grid improvement says nothing about whether that
trend is fixed.

So `k-omega SST` still cannot close. Outstanding:

1. the coarse/medium/fine sequence **with the mapped inlet**;
2. mesh stability, or demonstrable convergence;
3. E_x_r <= the derived threshold on a **G1-valid, resolved** mesh -- not the
   coarse one, which had 45 wall faces outside the band;
4. the SST mechanism controls repeated on the corrected case.

If the old refinement trend persists, the fine mesh may exceed 0.25 again even
with the corrected inlet. That is the open question.
