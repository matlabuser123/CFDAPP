# P12-DIFF-002 W8 — run unchanged at its own decision point

Evidence: `logs/01_W8.log` (complete raw output), `tools/run_w8.sh`.
Library `143a1dda…`; test sources **unchanged by VAL-001** (hashes in the log header).
**Nothing was amended.** W8's two tests, its cases, meshes, quantities, references, extraction and
thresholds are exactly as frozen.

## Result

```
StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence   FAILED
MultiBlockProductionCase.CurvedChannelGridConvergence             FAILED
```

**First failed criterion:** `test_structured_quad_production_case.cpp:406`
`EXPECT_GE(velocityOrder, 1.5)` — actual **1.4390906187401413**.

## Raw values, recorded before any order is computed

### StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence

| grid | nx × ny | cells | h | solver | accepted |
|---|---|---|---|---|---|
| coarse | 64 × 8 | 512 | 0.125 | Converged (2222 it) | yes |
| medium | 96 × 12 | 1152 | 0.0833333 | Converged (2181 it) | yes |
| fine | 144 × 18 | 2592 | 0.0555556 | Converged (2794 it) | yes |

`pressure_gradient` (exact −1.2):

| grid | value | error | relative error |
|---|---|---|---|
| coarse | −1.2020884 | **−0.002088** | 0.00174 |
| medium | −1.1996745 | **+0.0003255** | 0.0002713 |
| fine | −1.1975162 | **+0.002484** | 0.00207 |

`velocity_l2_error` (exact 0):

| grid | value |
|---|---|
| coarse | 0.010826713 |
| medium | 0.0047562152 |
| fine | 0.0026536902 |

### MultiBlockProductionCase.CurvedChannelGridConvergence

| grid | nx × ny | cells | h | solver |
|---|---|---|---|---|
| coarse | 8 × 60 ×3 | 480 | 0.121352 | Converged (1551 it) |
| medium | 12 × 90 ×3 | 1080 | 0.0809011 | Converged (1405 it) |
| fine | 18 × 135 ×3 | 2430 | 0.0539341 | Converged (3160 it) |

`pressure_gradient` G (exact −1.8282207):

| grid | value | error | relative error |
|---|---|---|---|
| coarse | −1.8199633 | 0.008257 | 0.004517 |
| medium | −1.8228270 | 0.005394 | 0.002950 |
| fine | −1.8254041 | 0.002817 | 0.001541 |

`velocity_l2_error`: 0.011092668 / 0.0048237168 / 0.0020951372.
`radial_pressure_rise_ratio` (exact 1): 1.0083088 / 1.0010636 / 0.99951751.

## Orders recomputed independently (r = 1.5, p = ln(e_coarse/e_fine)/ln r)

| quantity | pair | computed here | test reported |
|---|---|---|---|
| StructuredQuad velocity L2 | coarse→medium | ln(0.010826713/0.0047562152)/ln1.5 = **2.0298** | 2.029 |
| StructuredQuad velocity L2 | medium→fine | ln(0.0047562152/0.0026536902)/ln1.5 = **1.4389** | 1.439 ✗ < 1.5 |
| StructuredQuad dp/dx | medium→fine | ln(0.0003255/0.002484)/ln1.5 = **−5.013** | −5.012 ✗ |
| MultiBlock G error | coarse→medium | ln(0.008257/0.005394)/ln1.5 = **1.0502** | 1.050 ✗ < 1.5 |
| MultiBlock G error | medium→fine | ln(0.005394/0.002817)/ln1.5 = **1.6024** | 1.602 |

Every reported order is reproduced independently.

## Observation recorded (no amendment, no reclassification)

The StructuredQuad `dp/dx` error **changes sign** between the coarse and medium grids
(−0.002088 → +0.0003255 → +0.002484): the solution crosses the exact value, so |error| falls then
rises and no observed order is defined across that crossing. The −5.012 is that artifact. The
relative-error sequence 0.174 % → 0.027 % → 0.207 % reproduces A1's recorded
"0.174 % → 0.027 % → 0.207 %" exactly.

This is **reported, not presupposed**, exactly as the frozen W8 requires, and **W8 is not
amended**.

## Verdict

```
P12-DIFF-002 BLOCKED — HISTORICAL W8 GATE DECISION REQUIRED
```

W9 and W10 were not reached. The frozen W8 states: "If either still fails: **STOP**, preserve the
result, and request a separate decision on amending the historical gate. **Do not retune it**."
