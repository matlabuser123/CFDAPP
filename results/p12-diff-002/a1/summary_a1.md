# P12-DIFF-002 Amendment A1 — decision report

**A1 gate:** `results/p12-diff-002/acceptance_gate_A1.md`, sha256
`c4b08824cf98e95f6bf3d424e7e0fd006231f1ed955cbfb9f6f641d45faae7d0`, frozen before the fresh run
(`a1/logs/03_gate_a1_freeze.log`).

**Chronology:**

```text
Original W3b     — FAILED   (preserved: logs/08, logs/09, summary.md §3)
Amendment A1 W3b — PASS
W4               — PASS
W5               — PASS   (worst iteration ratio 1.12x against the 1.25x guard)
W6               — PASS   (144x18 dp/dx error 0.207% against the 0.747% bound)
W7               — FAILED -> STOP (one NEW regression; first failed criterion in gate order)
W8               — FAILED (both historical tests; measured in the same run as W6)
W9, W10          — not run
```

**Verdict: `P12-DIFF-002 BLOCKED / FAILED GATE`.** Amendment A1's own criterion passed; the resumed
DIFF-002 sequence then stopped at W7. Two separate authorizations are requested: one for the **new
k-ε regression / activation-gating question** (§4, §5), one for the **historical validation gates**
(§6). Neither was amended here.

---

## 1. W3b-A1 — PASS

Fresh post-freeze run: `a1/logs/04_W3bA1_FRESH_production.log`.

```text
TOTAL boundary faces 12920 | oracle HO 12366 FB 554 | prod HO 12366 FB 554
class-mismatch 0 | fb-bit-mismatch 0 | ho-not-exercised 0 | oracle T/G disagree 0 of 1284
BOTH CLASSES PRESENT: HIGHER_ORDER_REQUIRED 12366, FALLBACK_REQUIRED 554 -> yes
W3b-A1 PASS
```

Every A1 acceptance requirement met: A1-1 classification agreement 100 % (0 mismatches of 12 920
faces); A1-2 bitwise identity on all 554 `FALLBACK_REQUIRED` faces; A1-3 every one of the 12 366
`HIGHER_ORDER_REQUIRED` faces genuinely exercised (non-zero far-cell coupling, terms differing from
the two-point ones, identity `cP − cF = cB` to ≤ 1e-12); A1-4 the two oracles agree on all 1 284
faces where both apply; A1-5 both classes present.

### Per-mesh classification table

| mesh | faces | oracle HO | oracle FB | prod HO | prod FB | class mismatch | fb bit mismatch |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2D 1×1 | 4 | 0 | 4 | 0 | 4 | 0 | 0 |
| 2D 8×1 | 18 | 2 | 16 | 2 | 16 | 0 | 0 |
| 2D 1×8 | 18 | 2 | 16 | 2 | 16 | 0 | 0 |
| 2D 8×8 | 32 | 32 | 0 | 32 | 0 | 0 | 0 |
| 3D 1×1×1 | 6 | 0 | 6 | 0 | 6 | 0 | 0 |
| 3D 8×1×1 | 34 | 2 | 32 | 2 | 32 | 0 | 0 |
| 3D 1×8×1 | 34 | 2 | 32 | 2 | 32 | 0 | 0 |
| 3D 1×1×8 | 34 | 2 | 32 | 2 | 32 | 0 | 0 |
| 3D 8×8×1 | 160 | 32 | 128 | 32 | 128 | 0 | 0 |
| 3D 8×1×8 | 160 | 32 | 128 | 32 | 128 | 0 | 0 |
| 3D 1×8×8 | 160 | 32 | 128 | 32 | 128 | 0 | 0 |
| 3D 8×8×8 | 384 | 384 | 0 | 384 | 0 | 0 | 0 |
| graded 2×4 r=2 | 12 | 12 | 0 | 12 | 0 | 0 | 0 |
| graded 16×16 r=1.2 | 64 | 64 | 0 | 64 | 0 | 0 | 0 |
| distorted 16×16 shear 0.20 | 64 | 64 | 0 | 64 | 0 | 0 | 0 |
| distorted 16×16 shear 0.45 | 64 | 64 | 0 | 64 | 0 | 0 | 0 |
| distorted 8×1 shear 0.20 | 18 | 2 | 16 | 2 | 16 | 0 | 0 |
| distorted 1×8 shear 0.20 | 18 | 2 | 16 | 2 | 16 | 0 | 0 |
| annular_sector_conduction_multiblock | 204 | 204 | 0 | 204 | 0 | 0 | 0 |
| channel_transpiration_graded | 144 | 144 | 0 | 144 | 0 | 0 | 0 |
| compressible_channel_coupled | 112 | 112 | 0 | 112 | 0 | 0 | 0 |
| compressible_validation | 60 | 60 | 0 | 60 | 0 | 0 | 0 |
| curved_channel_multiblock | 204 | 204 | 0 | 204 | 0 | 0 | 0 |
| duct_3d | 1664 | 1664 | 0 | 1664 | 0 | 0 | 0 |
| heated_cavity | 80 | 80 | 0 | 80 | 0 | 0 | 0 |
| heated_species_diffusion | 48 | 48 | 0 | 48 | 0 | 0 | 0 |
| lid_driven_cavity | 80 | 80 | 0 | 80 | 0 | 0 | 0 |
| lid_driven_cavity_3d | 1536 | 1536 | 0 | 1536 | 0 | 0 | 0 |
| lid_driven_cavity_3d_re1000 | 6144 | 6144 | 0 | 6144 | 0 | 0 | 0 |
| lid_driven_cavity_40x40 | 160 | 160 | 0 | 160 | 0 | 0 | 0 |
| lid_driven_cavity_80x80 | 320 | 320 | 0 | 320 | 0 | 0 | 0 |
| multiphase_validation | 40 | 40 | 0 | 40 | 0 | 0 | 0 |
| obstacle_channel_multiblock | 248 | 248 | 0 | 248 | 0 | 0 | 0 |
| poiseuille_distorted | 144 | 144 | 0 | 144 | 0 | 0 | 0 |
| poiseuille_flow | 144 | 144 | 0 | 144 | 0 | 0 | 0 |
| species_diffusion | 48 | 48 | 0 | 48 | 0 | 0 | 0 |
| step_channel_multiblock | 256 | 256 | 0 | 256 | 0 | 0 | 0 |
| **TOTAL** | **12 920** | **12 366** | **554** | **12 366** | **554** | **0** | **0** |

`cases/backward_facing_step` and `cases/channel_flow` have no `case.json` and are not buildable here;
they are reported as skipped, exactly as the original W3a run reported them.

### Fallback bitwise comparison

All 554 `FALLBACK_REQUIRED` faces return `coefficient`, `explicitFlux`, `boundaryValueCoefficient`
and `farCellCoefficient` **bit-for-bit identical** to the pre-DIFF-002 two-point expression, compared
as raw bit patterns (`memcmp`), not to a tolerance. The pre-change expression is transcribed
literally from the production fallback branch in the probe.

### Non-vacuity

Two negative controls, run before the freeze and recorded in the frozen gate (§6):

| mode | class mismatches | verdict |
| --- | --- | --- |
| `production` | 0 | PASS |
| `baseline` (pre-DIFF-002: fallback everywhere) | 12 366 | **FAIL**, as required |
| `overeager` (reconstruct everywhere) | 554 | **FAIL**, as required |

The `baseline` control is the decisive one: it is exactly the library state on which the **original**
W3b passed vacuously, and W3b-A1 rejects it outright. Neither control modifies production code; both
substitute the classification a broken library would report.

### Methodological lesson (recorded as required)

The original gate's pre-freeze dry-run gave W3b no protection because W3b passed there **vacuously**:
the baseline library had no higher-order path at all, so "100 % fallback" was tautologically true on
it. **A baseline dry-run proves nothing about a criterion the baseline satisfies vacuously.** A
criterion that asserts the *absence* of behaviour the baseline cannot exhibit must additionally be
checked against "can a correct post-change implementation satisfy this at all?" — and every geometry
in a criterion's mesh list must actually have the property the list is named for. Recorded in the
frozen A1 gate §3 and in the persistent memory note on frozen-gate derivation errors.

### One architectural note, measured rather than assumed

Production's stencil availability flows through `MeshGeometry::oppositeInteriorFace`, which selects
the most **anti-parallel** interior face using the floating-point test `alignment < 0`. A1-4 is the
check that this never diverges from pure topology: Oracle-T (integer comparison on the generator's
`nx, ny, nz`, no floating point at all) and Oracle-G (connectivity walk by normal depth) agree with
each other and with production on every face tested, 0 disagreements of 1 284 where both apply. The
predicate is not near-zero in practice — the genuinely opposite face has alignment ≈ −1 — but this is
now measured, not assumed.

---

## 2. W5 — PASS (mandatory convergence/iteration guard)

### SIMPLE outer iterations, against the frozen pre-change values

`logs/05_cases_before.log` (before) vs `logs/11_cases_after.log` (after), same instrument
(`tools/diff2_cases.cpp`, frozen and unmodified), guard ≤ 1.25×:

| case | before | after | ratio | bound | `non_orthogonal_corrections` | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| poiseuille_flow | 1319 | 1319 | 1.0000 | 1648 | 0 — reconstruction inactive | PASS |
| **poiseuille_distorted** | 1983 | **2222** | **1.1205** | 2478 | 1 — active | PASS |
| lid_driven_cavity | 3036 | 3036 | 1.0000 | 3795 | 0 — inactive | PASS |
| **curved_channel_multiblock** | 1400 | **1405** | **1.0036** | 1750 | 1 — active | PASS |
| duct_3d | 67 | 67 | 1.0000 | 83 | 0 — inactive | PASS |
| channel_transpiration_graded | 1563 | 1563 | 1.0000 | 1953 | 0 — inactive | PASS |

Worst ratio **1.1205×**, against the 1.25× bound. **No GRAD-002-style slowdown**: GRAD-002's lagged
boundary correction cost 4.1×; this reconstruction costs 1.12× on the case that exercises it most.

**Reported plainly:** four of the six cases are unchanged because the reconstruction is *inactive*
there, not because it is cheap — see §4. The guard is genuinely exercised only by
`poiseuille_distorted` (1.12×) and `curved_channel_multiblock` (1.004×).

### Thermal and species solver iterations

The frozen instrument reports only SIMPLE's outer count, and every committed thermal/species case
runs with `non_orthogonal_corrections = 0`, so two additional probes were written rather than
modifying the frozen one.

`logs/12_W5_scalar_iters.log` compares corrections OFF → ON. On an **orthogonal** mesh that isolates
DIFF-002 exactly, because NUM-003's internal-face correction is documented and verified
bit-identical there:

| mesh | thermal outer | thermal linear | species linear | verdict |
| --- | --- | --- | --- | --- |
| Cartesian 20×20 | 3 → 3 (1.000×) | 2 → 2 | 44 → 43 (0.977×) | PASS |
| Cartesian 40×40 | 3 → 3 (1.000×) | 3 → 2 | 94 → 90 (0.957×) | PASS |
| Cartesian 80×80 | 3 → 3 (1.000×) | 5 → 4 | 176 → 175 (0.994×) | PASS |
| Cartesian 3D 16³ | 3 → 3 (1.000×) | 1 → 1 | 44 → 44 (1.000×) | PASS |

**A correction to my own first reading.** That probe's *distorted* row reported thermal outer
3 → 18 (6.0×), which I initially read as a W5 failure. It is not: on a non-orthogonal mesh, turning
corrections ON also turns on NUM-003's own lagged internal-face term, so that row compares two
**configurations**, not before/after DIFF-002. `logs/13_W5_attribution.log` resolves it by building
one probe source twice — once against the real library, once with `-DPRECHANGE`, where the probe
defines the three symbols of `NonOrthogonalDiffusion.cpp` itself with their pre-DIFF-002 bodies, so
the linker never pulls that archive member and the whole library above it runs against the pre-change
boundary treatment. No production source was modified. With corrections ON in **both** binaries:

| mesh | thermal outer PRE → DIFF-002 | ratio | thermal linear | species linear | verdict |
| --- | --- | --- | --- | --- | --- |
| Cartesian 40×40 | 3 → 3 | 1.000 | 3 → 2 | 94 → 90 (0.957×) | PASS |
| graded 40×40 r=1.15 | 3 → 3 | 1.000 | 13 → 11 | 155 → 167 (1.077×) | PASS |
| distorted 40×40 shear 0.20 | 7 → 8 | **1.143** | 47 → 22 | 101 → 101 (1.000×) | PASS |
| distorted 40×40 shear 0.45 | 16 → 18 | **1.125** | 23 → 18 | 102 → 102 (1.000×) | PASS |

The true pre-change value on the shear-0.45 mesh is **16** outer iterations, not 3: the pre-existing
NUM-003 correction accounts for 3 → 16, and DIFF-002 adds only 16 → 18. Worst thermal ratio 1.143×,
worst species ratio 1.077×, both inside 1.25×. Thermal *linear* iterations consistently **fall**
(47 → 22, 23 → 18, 13 → 11, 3 → 2): making the far-cell coupling implicit improves the conditioning
of the system it enters.

**W5 verdict: PASS.** Worst ratio anywhere 1.1205× (SIMPLE), 1.143× (thermal), 1.077× (species).

---

## 3. W6 — PASS (production accuracy)

`logs/14_W6_W8_historical.log`, from the historical distorted-Poiseuille grid-convergence run
(exact dp/dx = −1.2):

| grid | iterations | velocity L2 | Cartesian ref | dp/dx | dp/dx error | pre-change dp/dx error |
| --- | --- | --- | --- | --- | --- | --- |
| 64×8 | 2222 | 1.0827e-02 | 1.5183e-02 | −1.202088 | **0.1740 %** | 2.2801 % |
| 96×12 | 2181 | 4.7562e-03 | 6.9492e-03 | −1.199674 | **0.0272 %** | 1.0958 % |
| 144×18 | 2794 | 2.6537e-03 | 3.1294e-03 | −1.197516 | **0.2070 %** | 0.6788 % |

**Frozen threshold: the 144×18 dp/dx error ≤ 1.10 × 0.6788 % = 0.747 %. Measured 0.2070 % — PASS**,
with margin: a **3.28× improvement**, not merely "not worse".

Velocity L2 improves 1.61× / 1.58× / 1.40×, and is now **better than the Cartesian reference at every
resolution** (it was worse at all three before). Conservation is unaffected (column flow error
1.36e-11 / 2.53e-11 / 3.60e-11). Observed orders: velocity 2.029 then 1.439; dp/dx 4.584 then −5.012.

**Recorded honestly as incomplete:** W6 also asks for velocity L1/L∞ and the wall flux. The
historical test instrument reports L2 only, and no separate L1/L∞/wall-flux probe was written before
the phase stopped at W8. The *threshold* W6 states is on the 144×18 dp/dx error and it passes; the
supporting record is partial and is not claimed otherwise.

---

## 4. Finding: the reconstruction is gated behind `non_orthogonal_corrections`

Not a gate failure, and not something changed under this authorization — but material, and reported
rather than left implicit.

`boundaryFaceDiffusionTerms` applies the reconstruction only when `gradPhi != nullptr`, i.e. only when
`NonOrthogonalCorrectionOptions::enabled` is set, which comes from each case's
`solver.json: non_orthogonal_corrections`. Measured per case: `poiseuille_distorted`,
`curved_channel_multiblock`, `obstacle_channel_multiblock` and `step_channel_multiblock` have 1;
`poiseuille_flow`, `lid_driven_cavity`, `duct_3d`, `channel_transpiration_graded`, `heated_cavity`,
`species_diffusion` and `annular_sector_conduction_multiblock` have 0 (the parser's default).

Consequence: on a case with the flag at 0 the DIFF-002 reconstruction never runs, and the half-cell
first-order wall-flux error that P12-DIFF-001 measured — which DIFF-001 proved is present **on
perfectly orthogonal meshes too**, at exactly 0.5 h — remains. This is faithful to the DIFF-002
authorization, which required the new form to "replace, not augment, the existing value-boundary
non-orthogonal diffusion treatment", and that treatment lives inside this branch. But the *purpose*
(a second-order wall flux) is not achieved on Cartesian cases as shipped, because a flag that was
historically a no-op on orthogonal meshes is now the switch for something that is not a no-op there.
Widening activation is a scope decision, not an A1 change, so nothing was modified.

W3a and W3b-A1 measure stencil **availability** (100 %) and classification, which are properties of
the geometry and are unaffected; W1/W2/W4 drive the operator directly with `gradPhi` supplied.

---

## 5. W7 — FAILED → STOP (the first failed criterion in gate order)

`logs/15_W7_focused.log`, against the pre-DIFF-002 baseline
`results/p12-grad-002/a1/logs/10_focused_new.log`, same suite list and format:

| suite | baseline (pre-DIFF-002) | now | delta |
| --- | --- | --- | --- |
| CFDDiscretizationTests | 155/156, 1 failed | 168/169, 1 failed | +13 tests (W4's), same single failure |
| CFDMeshTests | 156/156 | 156/156 | — |
| CFDPisoTests | 81/81 | 81/81 | — |
| CFDSolverTests | NOT BUILT | 87/87 | now built, all pass |
| CFDCoreTests | 30/30 | 30/30 | — |
| CFDFieldTests | 40/40 | 40/40 | — |
| CFDAlgebraTests | 97/97 | 97/97 | — |
| CFDThermalTests | 92/92 | 92/92 | — |
| CFDTurbulenceTests | 136/136 | 136/136 | — |
| CFDMMSValidationTests | 19/19 | 19/19 | — |
| CFDCaseIntegrationTests | 61/63, **2** failed | 60/63, **3** failed | **one NEW failure** |

Everything is unchanged except `CFDCaseIntegrationTests`, which gains one failure that was **passing**
on the pre-DIFF-002 library:

### NEW REGRESSION — `StructuredQuadProductionCase.KEpsilonChannelMatchesCartesianOnTiltedMesh`

```text
k_epsilon Re_tau: Cartesian 447.902, distorted 439.024
  Expected: |distorted.reTau - cartesian.reTau| <= 0.01 * cartesian.reTau
  actual:   8.8778403309750615  vs  4.4790232267296828
```

The test asserts the tilted-mesh and Cartesian Re_τ agree to **1 %**; the historically measured
agreement recorded in the test's own comment is **0.06 % at 48×12, 0.02 % at 96×24**. It is now
**1.98 %** — a real degradation against a MESH-001 frozen threshold, not a near-miss.

**Cause, diagnosed — the activation gating of §4, not a defect in the reconstruction's mathematics.**
`runChannel` (test lines 565–599) builds the two arms of the comparison differently:

```cpp
if (distorted) {
  setQuadMesh(definition, nx, ny, mappedVertices(...));
  definition.solver.nonOrthogonalCorrections = 1;   // <-- only this arm
} else {
  definition.mesh.nx = nx; definition.mesh.ny = ny;  // corrections stay 0
}
```

So the **distorted** arm now gets the second-order wall flux for momentum, k and ε, while the
**Cartesian** arm keeps the two-point one, because `non_orthogonal_corrections = 0` leaves
`gradPhi == nullptr` and the reconstruction never runs. Before DIFF-002 both arms used the same
(first-order) wall treatment and agreed to 0.06 %; the test now measures the difference between **two
different wall discretizations** rather than the mesh-distortion sensitivity it was written to
measure. The Cartesian arm provably cannot have changed — with `gradPhi == nullptr` the code path is
byte-identical to the pre-DIFF-002 one — so the whole 8.88 shift is in the distorted arm.

Which arm is closer to the truth cannot be read off this test: it asserts only agreement between the
two, never an absolute Re_τ. That question is not answered here and is not claimed either way.

**W7 verdict: FAIL.** W7 precedes W8 in the frozen gate order, so **W7 is the first failed criterion
and the phase stops here.** W8 was already measured in the same instrument run as W6 and is reported
below for completeness, not as the stopping point.

---

## 6. W8 — also FAILED

Both historical tests were rerun **unchanged** (sha256 recorded in the log header: structured-quad
`5f98c8046eec7f96…`, multi-block `5b6eca7e7df2ad12…`). No threshold, test or golden output was
modified.

### `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` — FAILED

```text
pair 0: observed order velocity 2.029, dp/dx 4.584
pair 1: observed order velocity 1.439, dp/dx -5.012
  Expected: (velocityOrder) >= (1.5), actual: 1.4390906187401413 vs 1.5   [pair 1]
  Expected: (gradientOrder) >= (1.5), actual: -5.0119102605839547 vs 1.5  [pair 1]
```

### `MultiBlockProductionCase.CurvedChannelGridConvergence` — FAILED

```text
pair 0 observed order: velocity L2 2.054, G error 1.050, radial rise error 5.059
pair 1 observed order: velocity L2 2.057, G error 1.602, radial rise error 1.946
  Expected: (gradientOrder) >= (1.5), actual: 1.0503234189823614 vs 1.5   [pair 0]
```

### Attribution — both were already failing before DIFF-002, but the failing assertion changed

`results/p12-grad-002/a1/logs/10_focused_new.log` records both of these failing on the pre-DIFF-002
GRAD-002 A1 library. They are not new failures. However the *specific* assertions differ, so this is
not a clean "pre-existing, unchanged" attribution and is not reported as one:

| test | failing before (GRAD-002 A1) | failing now (DIFF-002) |
| --- | --- | --- |
| structured-quad | dp/dx order 1.181; fine error outside GCI band (0.006788 vs 0.002851) | velocity order 1.439; dp/dx order −5.012. **The GCI assertion now passes** |
| multi-block | radial pressure-rise error non-monotonic 1.0031e-03 → 1.4654e-03 (order −0.935); error outside uncertainty (0.0017644 vs 6.1408e-05) | dp/dθ order 1.050 on the coarse→medium pair. **The radial-rise failure is fixed**: now monotonic 8.309e-03 → 1.064e-03 → 4.825e-04 (orders 5.059 / 1.946) and inside its uncertainty (4.825e-04 vs U21 5.243e-04) |

So DIFF-002 repaired one previously-failing assertion in each test and left or produced another.

### Why the order assertions fail while the accuracy improves

This is the substantive finding, and it is the reason a separate decision is needed rather than a
threshold tweak. The dp/dx error sequence is now **0.174 % → 0.027 % → 0.207 %** — non-monotonic,
because the medium grid lands almost exactly on the exact value (−1.199674 against −1.2). An
"observed order" computed across an error that passes through ≈ 0 is not a convergence rate; it is an
artifact, which is why the number comes out as −5.012. The same mechanism produces the multi-block
`G error` order of 1.050. These assertions were calibrated against an error field dominated by a
**first-order wall flux**; DIFF-002 removes that dominant term, the residual error is several times
smaller, and the assertions no longer describe it.

Put plainly: the fine-grid accuracy is substantially better (dp/dx 3.28× better, velocity L2 1.4–1.6×
better and now better than Cartesian), and the historical order/GCI/monotonicity assertions still
fail. Both statements are true, and W8 was frozen precisely to force this to be reported rather than
resolved by retuning.

**Per the frozen W8 and the A1 authorization: preserved, not presupposed, not retuned.** A
**separate authorization** is requested to decide whether these historical validation gates should be
amended. They were **not** amended here. Note that the phase's formal stopping point is W7 (§5),
which precedes W8.

---

## 7. Not run, not claimed

- **W9** (committed-case backward compatibility) and **W10** (full Release/Debug/GUI regression,
  sanitizers, clang-format, static analysis) were **not run** — the phase stops at W8.
- GRAD-002's frozen compatibility gates were **not** rerun; MESH-007 G6.3 was **not** rerun, and its
  original failure stands as recorded. When eventually rerun, its criterion is unchanged: Cartesian
  16×16 translating lid cavity, `max |u_B − b − u_A| ≤ 1e-8`, no substitute mesh.
- Nothing is marked complete in `TODO.md`: not DIFF-002, not GRAD-002, not MESH-007.

## 8. Known pre-existing failure, not caused by DIFF-002

`GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` —
**PRE-EXISTING / NOT CAUSED BY DIFF-002.** A *gradient* test asserting the distorted-mesh global
order stays *below* 1.9 ("observed order suspiciously high"), an upper bound that held only while the
Green–Gauss boundary gradient was first order. Measured now: 1.93558 / 1.96913 / 1.98396 —
digit-for-digit the values recorded in `results/p12-grad-002/a1/summary_a1.md` item 1 and
`.../a1/logs/10_focused_new.log`, both from before DIFF-002 existed. DIFF-002 modifies no gradient
code. Not modified under this authorization.

## 9. Regenerated tracked artefacts — disclosed

Running the committed cases and the W7 suites rewrote tracked *generated* outputs. Following the
GRAD-002 A1 precedent for exactly this situation, I **reverted `results/validation/mms/` to HEAD**
rather than leave silently-updated reference numbers in the tree — those 8 files are validation
*reference* reports and every one is regenerable by rerunning the suite. They had not been modified
before this run.

The remaining churn is left in place and is reported rather than quietly reverted, because it cannot
be cleanly separated from what earlier phases in this session already had modified: about 20 newly
rewritten run-output files under `cases/*/results/` (`compressible_channel_coupled`,
`compressible_validation`, `heated_species_diffusion`, `lid_driven_cavity_40x40`,
`lid_driven_cavity_80x80`, `multiphase_validation`) plus the validation reports that earlier phases
had already left modified. These are solver run outputs, not thresholds or golden references, and
none of them gates any criterion. Nothing is committed.

`clang-format --dry-run --Werror` is clean on all five sources this phase added
(`diff2_oracle.cpp`, `diff2_degenerate.cpp`, `diff2_scalar_iters.cpp`, `diff2_w5_isolate.cpp`,
`test_boundary_reconstruction.cpp`). The two W5 probes were formatted after their first run and
both logs regenerated; every number reproduced identically, so the recorded probe hashes match the
files on disk. The frozen `diff2_oracle.cpp` needed no reformatting, so its freeze hash
`b832a1b05e4f254e…` remains valid.

## 10. Files added by A1 (nothing committed, nothing pushed)

Production source: **unchanged by A1** — hashes recorded in `a1/logs/03_gate_a1_freeze.log`.

```text
results/p12-diff-002/acceptance_gate_A1.md          (frozen, c4b08824cf98e95f...)
results/p12-diff-002/a1/summary_a1.md               (this file)
results/p12-diff-002/a1/tools/diff2_oracle.cpp      (the independent oracle, both negative controls)
results/p12-diff-002/a1/tools/run_a1.sh
results/p12-diff-002/a1/tools/freeze_a1.sh
results/p12-diff-002/a1/logs/01_oracle_dryrun.log
results/p12-diff-002/a1/logs/02_dryrun_{production,baseline,overeager}.log
results/p12-diff-002/a1/logs/03_gate_a1_freeze.log
results/p12-diff-002/a1/logs/04_W3bA1_FRESH_production.log
results/p12-diff-002/tools/diff2_scalar_iters.cpp   (W5 thermal/species)
results/p12-diff-002/tools/diff2_w5_isolate.cpp     (W5 attribution, -DPRECHANGE link substitution)
results/p12-diff-002/tools/run_w5_isolate.sh
results/p12-diff-002/tools/run_w6_w8.sh
results/p12-diff-002/tools/run_w7.sh
results/p12-diff-002/logs/11_cases_after.log        (W5/W9 data)
results/p12-diff-002/logs/12_W5_scalar_iters.log
results/p12-diff-002/logs/13_W5_attribution.log
results/p12-diff-002/logs/14_W6_W8_historical.log
results/p12-diff-002/logs/15_W7_focused.log
```

The original frozen gate, its W3b failure evidence and all prior-phase evidence are preserved
unchanged; their hashes are recorded in `a1/logs/03_gate_a1_freeze.log`.
