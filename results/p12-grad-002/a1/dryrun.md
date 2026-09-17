# P12-GRAD-002 A1 — pre-freeze baseline dry-run

Required by the authorization: every proposed A1 test was built and run against the **unchanged
pre-MESH-007 baseline** (`$HOME/m7ref/base`, library sha256 `eaadaa635e70adb1…`) *before* A1 was
frozen, to detect impossible or self-contradictory criteria. Log:
[logs/00_dryrun_baseline.log](logs/00_dryrun_baseline.log).

## Outcome

**61 PASS, 7 FAIL.** Every one of the 7 failures is a pre-registered negative control — the
**quadratic field on a translated Cartesian mesh** (C4a at 16², 32², 64², 128², 256²; C6 at
L = 1e-3 and L = 1), where the baseline loses the second-order boundary treatment and measures
4.88e-04 … 7.81e-03 against envelopes of 1.0e-07 … 2.1e-11. Every ordinary floating-point and
geometry sanity test passes on the baseline:

| test group | baseline rows | result |
| --- | --- | --- |
| C1-A1, constant and linear, 18 rows (Cartesian 16²/64²/256² plain and translated, dyadic, L = 1e-3, L = 1e3, graded 1.2, 3D 8³) | 18 | **all PASS**, ratios 1.4e-02 … 5.8e-02 |
| C2 row for Q16's linear field | 1 | **PASS** (1.091e-10 ≤ 1e-9) |
| C4a constant and linear, all valid-geometry meshes | 12 | **all PASS** |
| C4a all three fields on **dyadic** offsets | 15 | **all PASS**, every Δ exactly 0.000e+00 |
| C4a all three fields on translated **Q16** | 3 | **all PASS** (both meshes take the same non-aligned branch, so the defect cannot manifest — consistent with MESH-007's Q16 result of 1.5e-13) |
| C6-A1 constant and linear, three scales | 6 | **all PASS** |
| C6-A1 ρ-spread, per field across scales | 3 | **all PASS** (1.12, 25.6, 1.65 against 100) |
| geometry validity gate | — | VALID for small/dyadic/scaled offsets; INVALID for the large offset at every resolution, and beyond X/h ≈ 1e3 in the sweep |

The dry-run therefore establishes that (a) no A1 criterion demands accuracy below its numerical
floor, (b) the negative controls have real power (ratios up to 3.7e+08), and (c) the geometry gate
correctly separates valid meshes from corrupted ones.

## Two defects the dry-run caught, and the pre-freeze fixes

Both were found **before** the freeze and fixed by changing what A1 measures, never a bound.

1. **C1-A1's envelope was impossible on the distorted mesh Q16.** The baseline measured a
   linear-field error of 1.725e-10 against an envelope of 2.279e-11 (ratio 7.6) — a *sanity* test
   failing on unmodified code, i.e. the criterion, not the code, was wrong. Cause: on a skewed mesh
   the linear-field error is not floating-point round-off but the truncation residual of
   P12-NUM-003's four-sweep skewness-correction fixed point, whose established level (~1e-9) is
   exactly what the original **C2** bound was derived from. Applying a floating-point envelope to an
   iteration-limited quantity is an invalid test domain — the same class of mistake as the original
   C4. **Fix:** C1-A1's domain is restricted to orthogonal, unskewed meshes (where the skew
   correction is inactive and the fixed point is exact), and Q16's linear field stays governed by the
   unchanged, already-passing C2, still reported. Recorded in C1-A1's domain note.
2. **The C4b coordinate sweep measured nothing.** Its first version stepped the offset through
   powers of ten, which are all exactly representable, as is `i/16`; every row therefore reported
   geometry error exactly 0 and "valid" at every magnitude, including 1e+06. **Fix:** a
   non-representable mantissa at every magnitude (offset = 1.2345678 × 10^k). The sweep then
   locates the validity boundary properly, between X/h ≈ 1.0e3 (centroid error 1.7e-07, valid) and
   X/h ≈ 9.9e3 (2.2e-04, invalid).

A third, smaller correction was made to the *expectations* rather than to a test: §4's table
originally predicted the baseline would pass C6-A1 outright. It cannot — C6 includes the quadratic
field on translated Cartesian meshes, which is the same negative control as C4a's. The table now
states that every criterion translating a Cartesian mesh with a quadratic field is a negative
control, and the ρ-spread and constant/linear rows are the sanity part.

## What this does not cover

The dry-run exercises C1-A1, C2's Q16 row, C4a, C4b and C6-A1. C5's instrument was already
dry-run against the baseline in the original phase (`../logs/08`), where it failed at the first step
as its negative control requires; it is rerun fresh after the freeze. The carried-over criteria
C3, C7–C14 are not envelope-based and were frozen in the original gate.
