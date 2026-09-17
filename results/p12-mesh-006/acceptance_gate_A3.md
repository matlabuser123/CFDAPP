# P12-MESH-006 — Acceptance-gate Amendment A3 (G5.1 and G6.2)

**Status of this document: frozen before the fresh A3 acceptance run.** Its sha256, and those of
the frozen expected values and the evaluator, are recorded in
[a3/logs/00_freeze.log](a3/logs/00_freeze.log) before any fresh CFDApp run was started.

**Authorization.** A3 was explicitly authorized by the user on 2026-09-15, after the completed
G5.1/G6.2 investigation ("Authorize P12-MESH-006 Acceptance-Gate Amendment A3 based on the completed
G5.1/G6.2 investigation"). The authorization covers documenting and installing A3, rerunning the
affected G5/G6 validation, and — only if A3 passes — completing MESH-006.

**Scope.** A3 replaces **G5.1** and **G6.2** only. Every other item of
[acceptance_gate.md](acceptance_gate.md) is unchanged: G1–G4, G5.2–G5.4, G6.1, G7–G11, the CPU
baseline, amendments A1 and A2, and the stop rules. The original gate is not edited. It stays the
historical record, together with its failure.

## 1. The historical record (unchanged)

```text
Original gate:                  FAILED G5.1 / G6.2   (acceptance_gate.md; summary.md §17, §18, §30)
Investigation classification:   D — PRE-REGISTERED THRESHOLD DESIGN DEFECT
                                (g5-investigation/README.md)
Authorization:                  A3 amendment explicitly authorized by the user after the investigation
```

### 1.1 Original criteria

From acceptance_gate.md G5.1 and G6.2:

- **G5.1:**
  - at n = 24: cross-section L∞(u − u_exact)/U ≤ 0.010, area-weighted RMS ≤ 0.005,
    |dp/dx error|/G ≤ 0.015, |u_max error|/u_max ≤ 0.010;
  - at n = 16: L∞ ≤ 0.020 and |dp/dx error|/G ≤ 0.030.
  - The derivation given there: "the 2D production Poiseuille (MESH-001) measured dp/dx within
    0.585 % with 18 cells across; second order ⇒ ≈ 0.74 % at 16 and 0.33 % at 24 cells — thresholds
    allow ≈ 4× that."
- **G6.2:** the z-directed duct also meets G5.1's n = 16 thresholds.

### 1.2 Original measured failure

The frozen-binary gate runs, [logs/05a–05d](logs/05d_g5_duct_gate.log) and
[logs/06](logs/06_g6_directional_symmetry.log):

| item | measured | limit | result |
| --- | --- | --- | --- |
| G5.1 n = 16 L∞ | 0.023357 | 0.020 | FAIL |
| G5.1 n = 16 dp/dx | 0.014742 | 0.030 | pass |
| G5.1 n = 24 L∞ | 0.010577 | 0.010 | FAIL |
| G5.1 n = 24 RMS | 0.0052021 | 0.005 | FAIL |
| G5.1 n = 24 dp/dx | 0.0066323 | 0.015 | pass |
| G5.1 n = 24 u_max | 0.0079896 | 0.010 | pass |
| G6.2 z-duct n = 16 L∞ | 0.023357 | 0.020 | FAIL |

G5.2, G5.3, G5.4 and G6.1 passed. Implementation stopped at this failure (stop rules), and
MESH-006 was reported BLOCKED / FAILED GATE.

### 1.3 Investigation trigger and outcome

The user requested an evidence-only investigation of whether the failure was:

- A: an implementation defect;
- B: a reference or error-metric defect;
- C: a benchmark configuration defect;
- D: a threshold design defect;
- E: inconclusive.

Full evidence, tools and logs are in
[g5-investigation/README.md](g5-investigation/README.md), which is preserved unchanged (its
original-evidence hash check:
[g5-investigation/logs/original_evidence_sha256_check.txt](g5-investigation/logs/original_evidence_sha256_check.txt)).

| hypothesis | evidence | verdict |
| --- | --- | --- |
| A implementation | CFDApp = independent exact solution of the same discrete equations to ≤ 8.1e-8 U (n = 8…32); identical observed orders; independent of relaxation (1.4e-10), tolerance (8.5e-9) and duct length (8.5e-10); code audit consistent; truncation error O(1) only at the wall cells (G/4, a property of the standard half-cell wall gradient), global order → 2.000; x/y/z identical to 3.4e-12 | excluded |
| B reference / metric | analytical series verified by two independent closed forms, literature constants (f·Re 14.2271, u_max/U 2.09626), PDE, walls, flow rate; evaluated at CFDApp's actual cell centres; independent Python norms = the gate's C++ norms to ≤ 3e-12 | excluded (the point-value metric is standard and correctly implemented; the cell-average alternative is recorded as a sensitivity only) |
| C configuration | Re = 10 on a = D_h, geometry and BCs as intended; the measurement plane is fully developed (axis variation 1.6e-6; 2× length changes it by 8.5e-10) | excluded |
| D threshold design | the scheme's own discretization error, computed without CFDApp, exceeds the limits: L∞ 0.023357 (16), 0.010577 (24), RMS 0.0052021 (24); the derivation carried the 2D channel's dp/dx error to the duct without computing the duct's velocity error, which is 4.05–4.11× (L∞) and 2.9× (RMS) the channel's, while the limits allowed 3.5–3.9× and 2.8× | **confirmed** |
| E inconclusive | every alternative tested directly | no |

Key numbers carried into A3:

- **Independent analytical verification.**
  - G = 2.845415376956; u_max = 2.096256014684.
  - The gate's 399-term reference is within 5.1e-7 of the 20 001-term truth at n ≤ 24.
- **Independent discrete verification.** The exact solution of the scheme's discrete equations
  (eigen-decomposition; a sparse direct solve agrees to ≤ 7e-14).
- **Refinement study** (independent discrete, pointwise metric):

  | n | L∞ | RMS |
  | --- | --- | --- |
  | 8 | 8.50e-2 | 4.19e-2 |
  | 16 | 2.34e-2 | 1.15e-2 |
  | 24 | 1.06e-2 | 5.20e-3 |
  | 32 | 5.99e-3 | 2.95e-3 |
  | 64 | 1.51e-3 | 7.42e-4 |
  | 256 | 9.45e-5 | 4.65e-5 |

- **Observed order.** 1.83 (8→12), 1.95 (16→24), 2.000 (192→256). CFDApp's orders are identical.
- **Richardson/GCI** (Celik et al. 2008). CFDApp 8/16/24: p = 1.88, asymptotic indicator
  1.008 (G) and 1.010 (u_max), exact value inside the GCI_fine band. The discrete indicator tends
  to 1.0002.
- **Resolution the old limits would need.** L∞ ≤ 0.020 from n = 18; L∞ ≤ 0.010 and
  RMS ≤ 0.005 from n = 25 (n = 26 for the even grids the u_max definition needs).
- **Directional symmetry.** x/y/z identical to 3.4e-12 (streamwise), 2e-13 (cross-flow),
  2e-11 (pressure / range). G6.1 passed.
- **G7** (preserved, completed after the stop, not used to bypass G5): 14/14 PASS. At 64³,
  max |Δ| 0.0532, RMS 0.0210, extrema 0.0176 / 0.0088 / 0.0289 (limit 0.03).

## 2. Rationale for A3

- No correct implementation of this second-order scheme can meet the original G5.1 velocity
  limits on the pre-registered grids.
- Loosening them to just above the measured values (e.g. 0.020 → 0.024) would be tuning the gate
  to the answer, and it is not done.
- A3 replaces the defective absolute coarse-grid velocity limits with the criteria the
  investigation established from **CFDApp-free** calculations:
  - (A) agreement with the independent exact discrete solution, which detects any implementation
    defect at ≥ 1e-5 U, three orders below the discretization error;
  - (B, C, E) convergence to the analytical continuum solution at the expected order and in the
    asymptotic range;
  - (D) physically meaningful fine-grid accuracy, with velocity envelopes derived from the scheme's
    independently predicted error.
- Same production problem (cases/duct_3d; n = 8, 16, 24; x = 4a − h/2 measurement plane; A2).
- Same pointwise analytical metric (cell-centre values).
- The expected values are frozen before the fresh run (§7).

## 3. Definitions (unchanged from G5 / A2 unless stated)

**Problem.** The production square duct: `cases/duct_3d` with an n × n cross-section and 6n cells
along the flow axis (the G5 grids n = 8, 16, 24). It runs through the production path. Each fresh
run is a new process on new case directories; no previous output is reused.

**Measurement plane.** The cell plane s = 4a − h/2, index 4n − 1 along the flow axis (A2.2).
Canonical cross-section coordinates are (c1, c2) = ((axis + 1) mod 3, (axis + 2) mod 3).

**Analytical truth** (the pointwise metric, as G5.1): the fully developed solution at the cell
centres ((i + ½)h, (j + ½)h), evaluated by
[g5-investigation/tools/duct_reference.py](g5-investigation/tools/duct_reference.py) (single
series, 20 001 terms). It agrees with the original gate's 399-term series to ≤ 5.1e-7 U at
n ≤ 24, which cannot affect any A3 decision.

**Norms** on the measurement plane (n² cells, equal areas):

- L∞ = max |u − u_exact|/U;
- RMS = √(mean (u − u_exact)²)/U;
- L1 = mean |u − u_exact|/U.

**dp/dx.** −(least-squares slope of the plane-mean pressure over the cell planes with centres in
[2.5a, 4.5a]) (G5). The dp/dx error is |dp/dx − G|/G.

**u_max.** The mean of the four axis-adjacent cells on the measurement plane against the exact
axis value; the error is |u_axis − u_max|/u_max (A2.3).

**Independent discrete solution.** u_disc(n), G_disc(n): the exact solution of the scheme's
fully developed discrete equations, from
[g5-investigation/tools/duct_discrete.py](g5-investigation/tools/duct_discrete.py). It is not
CFDApp code, and no CFDApp output is used. Its fully developed state has zero cross-flow.

**Observed order** between grids n_c < n_f: p = ln(E_c/E_f)/ln(h_c/h_f) = ln(E_c/E_f)/ln(n_f/n_c).
The pair 16 → 24 has h16/h24 = 1.5.

## 4. Frozen independent expected values

[a3/data/frozen_expected.json](a3/data/frozen_expected.json) (sha256 in
[a3/data/frozen_expected.json.sha256](a3/data/frozen_expected.json.sha256) and in
a3/logs/00_freeze.log) was generated by [a3/tools/freeze_expected.py](a3/tools/freeze_expected.py)
before this document was finalized and before any fresh run. It holds u_disc, u_exact at the cell
centres, G_disc and u_axis_disc for n = 8, 16, 24, and the predicted pointwise errors:

| n | G_disc | predicted L∞ | predicted RMS | predicted L1 | predicted dp/dx err | predicted u_max err |
| --- | --- | --- | --- | --- | --- | --- |
| 8 | 2.687456580471 | 8.4996680926e-2 | 4.1909786821e-2 | 3.5688246551e-2 | 5.5513440240e-2 | 6.6907128660e-2 |
| 16 | 2.803463883479 | 2.3356640117e-2 | 1.1483027320e-2 | 9.5307736543e-3 | 1.4743539315e-2 | 1.7760461718e-2 |
| 24 | 2.826539344540 | 1.0577249820e-2 | 5.2020528142e-3 | 4.3588969397e-3 | 6.6338407280e-3 | 7.9896186872e-3 |

G5.1-D envelopes at n = 24 (1.25 × predicted):

- L∞ ≤ **1.3221562275e-2**;
- RMS ≤ **6.5025660177e-3**.

## 5. The A3 criteria for G5.1 — ALL must pass

| id | criterion |
| --- | --- |
| **G5.1-A** independent discrete agreement | For each of n = 8, 16, 24, on the measurement plane: (i) max over the n² cells of \|u_CFDApp − u_disc\|/U ≤ **1e-5**; (ii) max cross-flow velocity (the two cross-section components; the discrete fully developed solution has none)/U ≤ **1e-5**; (iii) \|dp/dx_CFDApp − G_disc\|/G_disc ≤ **1e-4**. |
| **G5.1-B** monotonic convergence | Against the analytical continuum solution: velocity L∞, velocity RMS, dp/dx error and u_max error each decrease strictly from n = 8 to 16 to 24. No level may be skipped or failed. |
| **G5.1-C** observed order | The primary metric is **velocity L∞**. Its observed order for the pre-registered pair 16 → 24, p = ln(E16/E24)/ln(24/16), must lie in **[1.8, 2.2]**. The orders of RMS, L1, dp/dx and u_max for both pairs are reported, along with the independent prediction. |
| **G5.1-D** fine-grid accuracy (n = 24) | dp/dx error ≤ **0.015** and u_max error ≤ **0.010** (the original, physically meaningful G5.1 limits, kept). Velocity L∞ ≤ **1.25 × predicted** (1.3221562275e-2) and velocity RMS ≤ **1.25 × predicted** (6.5025660177e-3), with the predictions from §4, computed before the run and never from the fresh CFDApp result. Predicted, allowed, measured and margin are all reported. |
| **G5.1-E** Richardson / GCI | Celik et al. (2008) on CFDApp's n = 8, 16, 24 sequence (r21 = 1.5, r32 = 2; non-constant-ratio iteration; F_s = 1.25), for φ = G (dp/dx) and φ = u_axis: the asymptotic-range indicator GCI_coarse/(r21^p GCI_fine) must lie in **[0.95, 1.05]** for both. Reported: observed p, Richardson extrapolation, GCI_fine, GCI_coarse, the exact analytical value, and whether the exact value lies inside the fine-grid GCI band. |

Unchanged and still required:

- G5.2: RMS and dp/dx errors decrease; measured from the fresh ProjectRunner level runs with the
  unchanged test code.
- G5.3: every section's face-flux mass flow within 1e-6 of the inflow; global ≤ 1e-6.
- G5.4: every grid Converged from rest, finite, exported, Rhie–Chow.

**What each part detects.**

- G5.1-A detects any implementation defect that changes the discrete solution by ≥ 1e-5 U, for
  example a wrong coefficient, an axis- or w-specific error, or a pressure-gradient error. That is
  three orders of magnitude below the discretization error.
- G5.1-B, C and E verify convergence to the right continuum solution in the asymptotic range.
- G5.1-D bounds the fine-grid accuracy.

The envelope is 1.25× the scheme's own predicted error, so an implementation that reproduces the
scheme exactly has a margin of about 20 % there by construction. The discriminating power lies in
G5.1-A, and this is stated here rather than hidden.

## 6. The A3 criterion for G6.2; G6.1 unchanged

| id | criterion |
| --- | --- |
| **G6.1** (unchanged) | x-, y- and z-directed n = 16 ducts, compared at every cell in canonical coordinates, for the x-vs-y and x-vs-z pairs: streamwise velocity, each cross-flow component, pressure (relative to p_max − p_min), and dp/ds (relative to G), all ≤ 1e-6. Each duct Converged, finite, Rhie–Chow. |
| **G6.2-A3** | The z-directed n = 16 duct, on its measurement plane (z = 4a − h/2): max \|w_CFDApp − u_disc(16)\|/U ≤ **1e-5** (w is its streamwise velocity), and max cross-flow (u, v)/U ≤ **1e-5**. The y-directed duct's agreement and both ducts' dp/ds agreement are reported. |

G6.2-A3 compares the w-momentum solution directly with the independent discrete solution at
1e-5 U, so a w-specific or z-specific defect is detected. G6.1 additionally requires the three
orientations to agree at 1e-6.

## 7. Protocol

1. **Freeze.** This document, a3/data/frozen_expected.json and a3/tools/a3_gate.py (the
   evaluator implementing §5 and §6) are hashed, with a timestamp, in a3/logs/00_freeze.log
   before any fresh run. The same log records the hashes of the investigation tools A3 uses
   (duct_reference.py, duct_discrete.py, gen_duct_case.py) and of the run script
   a3/tools/a3_run.sh.
   - The only CFDApp output the evaluator has read before the freeze is the investigation's own
     output, in a declared dry run to catch coding errors:
     [a3/logs/00a_evaluator_dry_run_NOT_acceptance.log](a3/logs/00a_evaluator_dry_run_NOT_acceptance.log).
2. **Fresh run** (a3/tools/a3_run.sh):
   - rebuild the Release `cfdapp` and `CFDCaseIntegrationTests` from the current tree and freeze
     copies;
   - fresh production CLI runs: x-duct n = 8, 16, 24; y- and z-duct n = 16;
   - fresh ProjectRunner level runs of the unchanged G5 test code, n = 8, 16, 24, in a relocated
     working directory. The original results/p12-mesh-006/data files are never touched.
3. **Evaluate** with the frozen evaluator: a3/data/a3_gate_result.{txt,json},
   a3/logs/04_a3_gate.log.
4. **Decide.**
   - If any A3 item, or any unchanged G5.2–G5.4 or G6.1 item, fails: **P12-MESH-006 BLOCKED /
     FAILED A3 GATE**, stop. A3 is not changed.
   - If all pass: **A3 G5 PASS, A3 G6 PASS**, and the stopped MESH-006 work continues.
5. The final MESH-006 code (after formatting, GUI and CLI work) re-runs the same five CLI cases.
   Their fields.csv must be byte-identical to the acceptance run's; this is reported with the final
   evidence.

A3 is defined and frozen **before** its fresh acceptance run.
