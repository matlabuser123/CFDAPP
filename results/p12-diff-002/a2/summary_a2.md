# P12-DIFF-002 Amendment A2 — decision report

**A2 gate:** `results/p12-diff-002/acceptance_gate_A2.md`, sha256
`3a268060809a4566331163f4fa582d5ba5e08398368b4814a08256a2605d8d40`, frozen **before** any change to
production behaviour (`a2/logs/02_gate_a2_freeze.log`).

All original and A1 evidence is preserved unchanged; hashes recorded at A2 freeze time:
`acceptance_gate.md` `51079f6d…`, `acceptance_gate_A1.md` `c4b08824…`, the original W3b failure
(`logs/08`, `logs/09`), the A1 PASS evidence (`a1/logs/04`), the W7 failure (`logs/15`).

---

## 1. Activation architecture, before and after

Full audit (written before any change): `a2/activation_architecture.md`.

**Before A2** — one boolean, `gradPhi != nullptr`, conflated three independent questions: is a cell
gradient available; should the **internal**-face non-orthogonal correction apply; should the
**Dirichlet boundary** face use DIFF-002's second-order wall flux.

```text
solver.json non_orthogonal_corrections = N  (default 0)
  -> SIMPLESettings::nonOrthogonalCorrections
       (a) nonOrthogonalOptions() -> {enabled = (N > 0)}
             -> assembler computes NO gradient when disabled -> gradPhi == nullptr
             -> boundaryFaceDiffusionTerms: gradPhi == nullptr  => two-point wall flux
       (b) SIMPLE.cpp / CompressibleSIMPLE.cpp: N = the number of correction passes
```

**After A2** — scheme selection is separated from the iterative control:

```text
  (a) Dirichlet wall flux: the gradient is ALWAYS built and always passed to
      boundaryFaceDiffusionTerms, so the branch is chosen by GEOMETRY alone:
          valid inward stencil     -> DIFF-002 reconstruction
          no valid inward stencil  -> historical two-point fallback
  (b) INTERNAL-face non-orthogonal correction: still gated by (N > 0), unchanged
  (c) number of iterations: still N, untouched
```

`boundaryFaceDiffusionTerms` itself is unchanged. `NonOrthogonalCorrectionOptions::enabled` keeps its
meaning and still drives (b) and (c). The `non_orthogonal_corrections` key, its default of 0, its
validation and its iteration semantics are all untouched, and no case-specific k-ε workaround exists.

**Decisive supporting finding.** `src/discretization/Diffusion.cpp`'s explicit scalar operator has
computed the higher-order one-sided boundary flux **unconditionally since P0** (`ownerOrientedFlux`
:195 calls `uncorrectedBoundaryFlux` before any gate; only the `S_nonorth · grad` term is gated). A2
therefore makes the implicit assembly path match a convention already present, unconditional, in the
explicit path — the removal of an accidental divergence, not a new policy. `Diffusion.cpp` needed no
change and received none.

## 2. Non-vacuous negative control

`a2/tools/diff2_activation.cpp`, run against the **unchanged pre-A2 library** and frozen into the
gate: `a2/logs/01_negative_control_preA2.log`.

Two observables, both through the real production assemblers: **(A)** `boundaryValueCoefficient`
isolated *exactly* by differencing the prescribed boundary value (assembly is linear in it, so every
internal-face term, source and diagonal cancels); **(B)** the whole assembled system, bitwise, on an
orthogonal mesh, where NUM-003's internal correction is verified bit-identical.

**Result: `N0 == N1` read DIFFERENT on all 4 callers × all 4 geometries** — thermal, species, k-ε,
momentum, on Cartesian 16×16, Cartesian 3D 8×8×8, graded 16×16 r=1.2 and distorted 16×16 shear 0.45.
On the uniform grid the observed jump is exactly the hand-derived ratio `Γ|S|c_B / (Γ|S_orth|/d) = 4/3`
(corner rows 2.4 → 3.2). `N1 == N2` already read BITWISE-SAME. The instrument demonstrably detected
the live defect before the gate was frozen.

## 3. A2-1 Activation invariance — PASS

### Orthogonal geometry: bitwise, every caller, whole system

`a2/logs/03_A2_1_invariance.log` — the same frozen instrument, after the change:

| caller | Cartesian 16×16 | Cartesian 3D 8×8×8 | graded 16×16 r=1.2 |
| --- | --- | --- | --- |
| thermal | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME |
| species | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME |
| k-epsilon | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME |
| momentum | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME | bvc + system BITWISE-SAME |

for N = 0, 1 and 2 — both the isolated boundary coefficient and the **entire matrix and RHS**.

### Distorted geometry: measured with a contamination-free observable

On a non-orthogonal mesh observable (A) is **contaminated**: the explicit non-orthogonal
*internal*-face flux depends on grad(φ), which depends on the boundary values, and that internal term
legitimately exists only when the correction is enabled. So a raw rhs difference across N mixes the
wall scheme with a term that is *supposed* to change. Two further probes settle it:

- `a2/logs/04_A2_1_diagnostic.log` — the residual difference reaches **96 of 196** interior-only rows
  on 16×16 (and 223 of 900 on 32×32). `boundaryValueCoefficient` is exactly zero on a row touching no
  boundary face, so those differences cannot be the wall scheme.
- `a2/logs/05_A2_1_wallcoeff.log` — **decisive.** The wall coefficient is isolated from the assembled
  diagonal by subtracting the exactly-recomputed internal-face coefficients (which are purely
  geometric — `Γ|S_orth|/d_PN` or `Γ|S_f|/d_PN`, no gradient in them), using the same public
  production function the assembler calls:

| mesh | wall rows | scale | N0 vs N1 | N1 vs N2 |
| --- | --- | --- | --- | --- |
| orthogonal Cartesian 16×16 | 60 | 3.60e+00 | 0 rows differ | 0 rows differ |
| orthogonal Cartesian 3D 8×8×8 | 296 | 6.75e-01 | 0 rows differ | 0 rows differ |
| orthogonal graded 16×16 r=1.2 | 60 | 6.25e+00 | 0 rows differ | 0 rows differ |
| DISTORTED 16×16 shear 0.20 | 60 | 3.98e+00 | 37 rows, max\|d\| **8.88e-16** | 0 rows differ |
| DISTORTED 16×16 shear 0.45 | 60 | 4.63e+00 | 36 rows, max\|d\| **1.33e-15** | 0 rows differ |
| DISTORTED 32×32 shear 0.45 | 124 | 4.67e+00 | 68 rows, max\|d\| **1.33e-15** | 0 rows differ |

The distorted residual is **1–2 ulp of the row scale** (≈3e-16 relative) — the round-off of the
isolation subtraction itself, which differences two quantities that genuinely differ at N = 0 vs 1
(internal faces use `|S_f|` vs `|S_orth|`). For comparison, the pre-A2 negative control on the same
meshes showed **8.8e-01** on a scale of 4.9: fifteen orders of magnitude larger. The wall coefficient
is a purely geometric expression (`Γ|S|·c_P`, `c_P = h2/(h1(h2−h1))`) whose code path no longer
depends on N at all, so bit-identity holds by construction and the measurement is consistent with
that and inconsistent with any formula change.

Reported precisely, per the authorization's own "to the strongest meaningful tolerance, preferably
bitwise where deterministic": **bitwise on every orthogonal mesh and for the whole assembled system;
ulp-level (≤ 1.33e-15 absolute, ≤ 3e-16 relative) on distorted meshes, where the isolation arithmetic
is not bit-deterministic.**

## 4. A2-2 Orthogonal correctness (mandatory) — PASS

`logs/16_A2_W1_W2_W3.log`, the frozen W1/W2/W3 gate probe rerun unchanged. The already-established
thresholds all hold:

| family | constant | linear | quadratic (bound 1e-12) | cubic order (bound 1.8) |
| --- | --- | --- | --- | --- |
| 2D Cartesian | 3.23e-14 | 3.55e-14 | **1.50e-16** | **2.000** |
| 2D Cartesian translated | 3.99e-14 | 4.04e-14 | **1.83e-16** | **2.000** |
| 2D distorted 48° | 3.61e-14 | 4.04e-14 | **1.59e-16** | **1.963** |
| 3D Cartesian | 3.55e-15 | 5.96e-15 | **1.85e-16** | **2.000** |
| curved multi-block (committed) | 1.45e-14 | — | radial **3.36e-04** (bound 1.8616e-02) | — |

W2 translation/scale invariance also passes (worst difference 1.05e-15 against a 6.38e-12 envelope).
Together with §3's orthogonal bitwise invariance at N = 0 and W4's hand-derived coefficients, this
establishes that an orthogonal mesh with `non_orthogonal_corrections = 0` now receives DIFF-002 and
retains second-order wall-flux behaviour — so A2 is not a k-ε test workaround.

## 5. A2-3 A1 topology compatibility — PASS

`a2/logs/06_A2_3_topology.log`, the frozen A1 oracle probe rerun unchanged:

```text
TOTAL boundary faces 12920 | oracle HO 12366 FB 554 | prod HO 12366 FB 554
class-mismatch 0 | fb-bit-mismatch 0 | ho-not-exercised 0 | oracle T/G disagree 0 of 1284
W3b-A1 PASS
```

Identical to the A1 run: valid stencil → higher order, invalid stencil → historical fallback, and the
554 fallback faces remain **bitwise identical** to the pre-DIFF-002 expression. **W3b-A1 is not
invalidated by A2.**

## 6. A2-4 k-ε regression — PASS

`a2/logs/07_A2_4_kepsilon.log`. Test source unchanged (`5f98c8046eec7f96…`), original threshold,
original configuration:

```text
k_epsilon Re_tau: Cartesian 438.690, distorted 439.024
[       OK ] StructuredQuadProductionCase.KEpsilonChannelMatchesCartesianOnTiltedMesh (2844 ms)
```

| quantity | pre-A2 | post-A2 |
| --- | --- | --- |
| Re_τ Cartesian arm (`non_orthogonal_corrections = 0`) | 447.902 | **438.690** |
| Re_τ distorted arm (`non_orthogonal_corrections = 1`) | 439.024 | **439.024** (unchanged) |
| absolute difference | 8.878 | **0.334** |
| relative difference | 1.98 % | **0.076 %** |
| threshold (unmodified) | 1 % | 1 % |
| result | FAIL | **PASS** |

This confirms the diagnosis end to end: only the **Cartesian** arm moved, because it is the arm that
previously missed DIFF-002; the distorted arm is bit-for-bit unchanged. The two arms now share one
wall discretization, and the agreement returns to the historically recorded order (0.06 %). The
threshold was not touched.


## 8. A2-6 W6 revalidated, complete dataset — PASS

`a2/logs/09_A2_6_w6_full.log`. Exact dp/dx −1.200000; exact wall shear 0.600000. Developed region
0.50 L .. 0.85 L, the historical test's own window, mesh mapping and exact solution.

| grid | iterations | dp/dx | dp/dx err % | vel L1 | vel L2 | vel L∞ | wall flux L1 % | wall flux L∞ % | mass imbalance |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 64×8 | 2222 | −1.202088 | 0.1740 | 9.3496e-03 | 1.0827e-02 | 2.2207e-02 | 0.9924 | 2.5713 | 3.798e-12 |
| 96×12 | 2181 | −1.199674 | 0.0271 | 4.1358e-03 | 4.7562e-03 | 1.0203e-02 | 0.6259 | 1.9984 | 3.970e-13 |
| 144×18 | 2794 | −1.197516 | **0.2070** | 2.1422e-03 | 2.6537e-03 | 9.7926e-03 | 0.4004 | 1.8533 | 9.922e-13 |

Observed orders (ratio 1.5): pair 0 — dp/dx 4.584, L1 2.012, L2 2.029, L∞ 1.918, wall flux 1.137;
pair 1 — dp/dx −5.012, L1 1.622, L2 1.439, L∞ 0.101, wall flux 1.102.

**Frozen bound: 144×18 dp/dx error ≤ 0.747 %. Measured 0.2070 % — PASS**, a 3.28× improvement on the
pre-change 0.6788 %. The dp/dx and velocity-L2 values reproduce the historical test's own output
exactly, which confirms the probe is faithful. This case runs with `non_orthogonal_corrections = 1`,
so A2 does not change it — the numbers match the A1 run, as expected. The previously partial record
(L1, L∞, wall flux) is now complete and is no longer relied on from the earlier run.

## 7. A2-5 W5 revalidated under the broader activation — PASS

`logs/05_cases_before.log` (the **original frozen** pre-DIFF-002 values) vs
`logs/17_A2_5_cases_after.log`, same frozen instrument, guard **≤ 1.25×** unweakened:

| case | frozen before | after A2 | ratio | bound | `non_orthogonal_corrections` | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| **poiseuille_flow** | 1319 | **1524** | **1.1554** | 1648 | 0 — newly activated | PASS |
| poiseuille_distorted | 1983 | 2222 | 1.1205 | 2478 | 1 | PASS |
| **lid_driven_cavity** | 3036 | **3312** | **1.0909** | 3795 | 0 — newly activated | PASS |
| curved_channel_multiblock | 1400 | 1405 | 1.0036 | 1750 | 1 | PASS |
| **duct_3d** | 67 | **65** | **0.9701** | 83 | 0 — newly activated | PASS |
| **channel_transpiration_graded** | 1563 | **1714** | **1.0966** | 1953 | 0 — newly activated | PASS |

**Worst ratio 1.1554×** against the 1.25× guard. Unlike the A1 run, all six cases now genuinely
exercise DIFF-002 — four of them for the first time — so the guard is exercised throughout rather
than trivially satisfied. Still nowhere near GRAD-002's 4.1× slowdown.

Other committed cases, for completeness (not part of the frozen six): lid_driven_cavity_40x40
5615 to 6159 (1.0969x), lid_driven_cavity_80x80 9643 to 10919 (1.1323x), lid_driven_cavity_3d
85 to 85, lid_driven_cavity_3d_re1000 249 to 255, obstacle_channel_multiblock 3486 to 3486,
step_channel_multiblock 633 to 633, compressible_validation 2225 to 2453 (1.1025x),
compressible_channel_coupled 12191 to 12012 (0.985x). All inside 1.25x.

**Thermal and species**, the newly activated `N = 0` configuration, measured with the `-DPRECHANGE`
link-substituted pre-DIFF-002 library so the comparison isolates the wall treatment
(`a2/logs/08_A2_5_n0_iters.log`; orthogonal/rectilinear meshes only, where that substitution
reproduces the pre-A2 `N = 0` behaviour exactly):

| mesh (N = 0) | thermal outer | thermal linear | species linear | worst ratio |
| --- | --- | --- | --- | --- |
| Cartesian 40x40 | 3 to 3 | 3 to 2 | 94 to 90 | 1.000 |
| Cartesian 80x80 | 3 to 3 | 5 to 4 | 176 to 175 | 1.000 |
| Cartesian 3D 16x16x16 | 3 to 3 | 1 to 1 | 44 to 44 | 1.000 |
| graded 40x40 r=1.15 | 3 to 3 | 13 to 11 | 155 to 167 | **1.077** |

The corrections-ON attribution run is unchanged by A2 (`logs/13_W5_attribution.log`): thermal worst
1.143x, species worst 1.077x. Thermal *linear* iterations again mostly fall.

## 8. A2-7 W7 — FAILED, STOP

`a2/logs/10_A2_7_focused.log`, against the pre-DIFF-002 baseline
`results/p12-grad-002/a1/logs/10_focused_new.log`.

| suite | baseline | A1 run | **A2 run** |
| --- | --- | --- | --- |
| CFDDiscretizationTests | 155/156 (1 fail) | 168/169 (1 fail) | **168/169 (1 fail)** |
| CFDMeshTests | 156/156 | 156/156 | **156/156** |
| CFDPisoTests | 81/81 | 81/81 | **81/81** |
| CFDSolverTests | NOT BUILT | 87/87 | **87/87** |
| CFDCoreTests | 30/30 | 30/30 | **30/30** |
| CFDFieldTests | 40/40 | 40/40 | **40/40** |
| CFDAlgebraTests | 97/97 | 97/97 | **97/97** |
| CFDThermalTests | 92/92 | 92/92 | **92/92** |
| CFDTurbulenceTests | 136/136 | 136/136 | **136/136** |
| CFDMMSValidationTests | 19/19 | 19/19 | **19/19** |
| CFDCaseIntegrationTests | 61/63 (2 fail) | 60/63 (3 fail) | **59/63 (4 fail)** |

Totals: **970 run, 966 passed, 4 failed.**

### Classification of every failure

| test | classification | evidence |
| --- | --- | --- |
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | **PRE-EXISTING** | Reproduces the recorded GRAD-002 values digit for digit: 1.93558 / 1.96913 / 1.98396. A2 touches no gradient code. Not modified |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | **PRE-EXISTING** | Failing on the pre-DIFF-002 library (`p12-grad-002/a1/logs/10`); W8's own test, not amended |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | **PRE-EXISTING** | Same — failing before DIFF-002; W8's own test, not amended |
| `StructuredQuadProductionCase.NonOrthogonalCorrectionIsActiveOnTheProductionPath` | **NEW — EXPECTED / JUSTIFIED CHANGE** | see below |
| `MultiBlockProductionCase.SectorConductionInterfaceConservation` | **NEW — EXPECTED / JUSTIFIED CHANGE** | see below |
| `StructuredQuadProductionCase.KEpsilonChannelMatchesCartesianOnTiltedMesh` | **FIXED BY A2** | was the A1-era new regression; now passes at 0.076 % |

### The two new failures, diagnosed

**1. `NonOrthogonalCorrectionIsActiveOnTheProductionPath`** (test lines 428-441). Two assertions; the
*first* — uncorrectedError >= 1.5 * correctedError, i.e. "the correction still helps" — **passes**.
The *second* is a **lower bound requiring the uncorrected run to be bad**:

```text
EXPECT_GT(uncorrectedError, 1.5 * cartesianVelocityL2(8))
  actual: 0.016546990514834162  vs  0.02277438742162494
```

The test's own comment records the pre-A2 uncorrected error as **3.31e-2**; it is now **1.65e-2**, a
2x improvement, because the `non_orthogonal_corrections = 0` arm now receives the second-order wall
flux. The assertion encodes the premise that N = 0 is substantially *worse* — exactly the premise A2
was authorized to remove. The failure is caused by the intended improvement, not by a defect. It is
nevertheless a **frozen failure that stands**: per the A2 gate, "a classification does not
automatically waive a frozen failure", and the test was not modified.

**2. `SectorConductionInterfaceConservation`** (test line 613). The conjugate annular-sector
conduction case. The solver is *not* losing conservation — its own energy balance is exact:

```text
sector 8x20 x3:  hot end 0.1468661248, interface 1 0.1470374558, interface 2 0.1470374558,
                 cold end 0.1468661248  (exact 0.1470904001)
                 spread 8.567e-05 vs bound 1.471e-07   -> FAIL
  energy balance: in 0.146866124809, out 0.146866124809, |in - out| = 7.359e-14 Q
sector pair 0/1: temperature L2 observed order 2.998 / 2.999
```

The cause is the test's own flux estimator (test lines 565-572), which measures the **boundary** line
flux with `boundaryFaceDiffusionTerms(mesh, face, k, distance, nullptr, false).coefficient *
(temperature[owner] - tb)` — explicitly `gradPhi = nullptr, prescribedValue = false`, i.e. the
**pre-A2 two-point** coefficient — while the solver now produces the solution with the DIFF-002 wall
flux. Interior lines use `internalFaceDiffusionTerms`, which still matches the solver exactly, which
is why the two interface lines agree with each other to all printed digits and sit closer to the
exact value (3.6e-04 relative) than the two end lines. The "spread" is a mismatch between the probe
and the shipped discretization, not a conservation defect. Supporting evidence: the end-to-end energy
balance is 7.4e-14, and the temperature error now converges at observed order **~3.0**, where the
test's own comment expects second order. Again a frozen failure that stands, not waived.

### Why this stops the phase

A2-1 through A2-6 and the k-epsilon gate all pass, and A2 achieved what it set out to do — including
repairing the regression that stopped A1. But `CFDCaseIntegrationTests` went from 61/63
(pre-DIFF-002) to **59/63**, and two tests that previously passed now fail because of this production
change. Both are explained, neither is waived, and both encode MESH-001/MESH-003 expectations this
authorization forbids amending. **Stopped at A2-7, the first failed frozen criterion.**

## 9. Not run

**A2-8 (W8)** was **not** run — the frozen gate order is not bypassed and the phase stops at A2-7.
The two historical tests remain in their previously recorded state, with the A1 chronology preserved:
DIFF-002 fixed the structured-quad GCI assertion and the multi-block radial-rise monotonicity
failure, while order assertions still fail. **A2-9 (W9/W10)** not run. GRAD-002 not revalidated;
MESH-007 not resumed, its G6.3 criterion unchanged (`max |u_B - b - u_A| <= 1e-8`, Cartesian 16x16
translating lid cavity, no substitute mesh).

## 10. A2-10 Production consistency — verified

Every caller of the implicit Dirichlet boundary flux now selects its spatial scheme from geometry
alone, independent of the requested number of non-orthogonal correction iterations:

| caller | boundary gradient | internal-face gradient | verified by |
| --- | --- | --- | --- |
| `MomentumEquation.cpp` (const mu, :100-108) | always | `applyNonOrthogonalCorrection ? gradPhi : nullptr` | A2-1 momentum rows |
| `MomentumEquation.cpp` (mu field, :207-215) | always | same | A2-1 momentum rows |
| `EnergyEquation.cpp` (const k, :114-118) | always | `nonOrthogonal.enabled ? &gradT : nullptr` | A2-1 thermal rows |
| `EnergyEquation.cpp` (k field, :200-203) | always | same | A2-1 thermal rows |
| `SpeciesEquation.cpp` (:59-65) | always | `nonOrthogonal.enabled ? &gradY : nullptr` | A2-1 species rows |
| `KEpsilonEquation.cpp` (:96-102) | always | `nonOrthogonal.enabled ? &gradPhi : nullptr` | A2-1 k-epsilon rows |
| `Diffusion.cpp` shared/scalar path | **already unconditional since P0** | gated (unchanged) | audit section 2; no change made |
| `ThermalInterface.cpp` (conjugate) | unchanged — deliberately never corrected | n/a | not modified |

No remaining path makes the spatial order of a Dirichlet boundary flux depend on N.

## 11. Source files changed by A2

```text
src/thermal/EnergyEquation.cpp        correctionGradient() always computes; boundary vs internal
                                      gradient pointers split in both assemblers
src/physics/MomentumEquation.cpp      same split in both assemblers
src/species/SpeciesEquation.cpp       same split
src/turbulence/KEpsilonEquation.cpp   same split
```

No header, no public signature, no `boundaryFaceDiffusionTerms`, no `Diffusion.cpp`, no test, no
threshold, no golden output and no case file was changed.

## 12. Verdict

```text
A2-1 activation invariance      PASS
A2-2 orthogonal correctness     PASS
A2-3 A1 topology compatibility  PASS
A2-4 k-epsilon regression       PASS  (1.98 % -> 0.076 %, threshold untouched)
A2-5 W5 revalidated             PASS  (worst 1.1554x vs 1.25x)
A2-6 W6 complete dataset        PASS  (144x18 dp/dx error 0.2070 % vs 0.747 %)
A2-7 W7 focused verification    FAILED -> STOP  (two new, explained, unwaived regressions)
A2-8 W8                         not run
A2-9 W9/W10                     not run
A2-10 production consistency    PASS
```

**`P12-DIFF-002 BLOCKED / FAILED A2 GATE`.** A decision is requested on the two new W7 failures: both
are assertions calibrated on the pre-A2 `non_orthogonal_corrections = 0` behaviour that A2 was
authorized to improve, and both belong to MESH-001/MESH-003, which this authorization forbids
amending.
