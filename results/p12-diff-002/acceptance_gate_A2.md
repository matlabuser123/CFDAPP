# P12-DIFF-002 — Amendment A2 acceptance gate (Dirichlet wall-flux activation consistency)

Authorized 2026-09-16 after A1. Status on entry: `P12-DIFF-002 BLOCKED / FAILED GATE`.

A1 is accepted as historically valid and is **not** re-litigated: `W3b-A1 PASS`, `W4 PASS`,
`W5 PASS`, `W6 PASS`, `W7 FAIL → STOP`. Frozen **before any change to production behaviour**.

**Preserved, not deleted, not rewritten** (hashes recorded in `a2/logs/02_gate_a2_freeze.log`):
the original gate `acceptance_gate.md` (`51079f6d…`), the A1 gate `acceptance_gate_A1.md`
(`c4b08824…`), the original W3b failure (`logs/08`, `logs/09`), the A1 PASS evidence
(`a1/logs/04_W3bA1_FRESH_production.log`), and the W7 failure (`logs/15_W7_focused.log`).

**Stop rule.** Stop at the first failed criterion. Never adjust a threshold after seeing a result.

---

## 1. Established finding this amendment corrects

`results/p12-diff-002/a2/activation_architecture.md` (audit, written before any change). In short:
one boolean, `gradPhi != nullptr`, currently conflates *scheme selection* for the Dirichlet wall flux
with *whether the iterative non-orthogonal correction is enabled*, so

```text
non_orthogonal_corrections = 0  -> old two-point Dirichlet wall flux
non_orthogonal_corrections > 0  -> DIFF-002 second-order wall flux
```

DIFF-002 corrects a boundary-normal discretization error that P12-DIFF-001 measured as exactly
`0.5 h` **on a perfectly orthogonal mesh**, so its activation must not depend on an iterative
non-orthogonality control.

**Supporting audit finding:** `src/discretization/Diffusion.cpp`'s explicit scalar operator has
computed the higher-order one-sided boundary flux **unconditionally since P0** (`ownerOrientedFlux`
line 195 calls `uncorrectedBoundaryFlux` before any gate; only the `S_nonorth · grad` term is gated).
A2 therefore makes the implicit assembly path match a convention already present, unconditional, in
the explicit path — it is the removal of an accidental divergence, not a new policy.

## 2. The intended change (smallest architecture change)

```text
valid DIFF-002 stencil   -> DIFF-002 reconstruction
no valid stencil         -> historical two-point fallback
```
independent of `non_orthogonal_corrections`. Each affected assembler always builds the correction
gradient and always passes it to `boundaryFaceDiffusionTerms`; the **internal**-face correction stays
gated by `enabled`, and the iteration count keeps its meaning. `boundaryFaceDiffusionTerms` itself is
unchanged. The setting is not deleted; no unrelated solver semantics are redefined; no case-specific
k-ε workaround.

## 3. Pre-freeze dry-run — non-vacuous, recorded

`a2/tools/diff2_activation.cpp`, run against the **unchanged** (pre-A2) library:
`a2/logs/01_negative_control_preA2.log`.

Observables, both taken through the real production assemblers so nothing re-implements production's
gating rule:

- **(A) `boundaryValueCoefficient`, isolated exactly.** Assembly is linear in the prescribed boundary
  value, which enters a row only as `rhs += boundaryValueCoefficient · φ_b`. Assembling with values
  `V` and `V+Δ` and differencing yields, per row and exactly, the sum of `boundaryValueCoefficient`
  over that row's value-prescribing faces — every internal-face term, source and diagonal cancels.
  Works on orthogonal **and** distorted geometry.
- **(B) the whole assembled system, bitwise, on an orthogonal mesh**, where NUM-003's internal-face
  correction is documented and verified bit-identical, so any difference across N is attributable to
  the boundary treatment alone.

**Negative-control result (pre-A2): `N0 == N1` reads DIFFERENT on all 4 callers × all 4 geometries**
— thermal, species, k-ε and momentum, on Cartesian 16×16, Cartesian 3D 8×8×8, graded 16×16 r=1.2 and
distorted 16×16 shear 0.45. Example, thermal Cartesian 16×16: two-boundary-face corner rows give
2.4 at N=0 and 3.2 at N=1, i.e. the hand-derived ratio `Γ|S|cB / (Γ|S_orth|/d) = 4/3` on a uniform
grid. `N1 == N2` already reads BITWISE-SAME everywhere, as expected (both map to `enabled = true`).

The instrument therefore demonstrably detects the live activation defect, in both geometries and for
every affected caller, before the gate was frozen.

## 4. Criteria

| id | criterion | threshold |
| --- | --- | --- |
| **A2-1** | **Activation invariance.** For identical field + mesh, the selected Dirichlet wall discretization is invariant under `non_orthogonal_corrections` = 0, 1, 2 where DIFF-002 is applicable: same reconstruction coefficients, same far-cell coefficient, same boundary coefficient, same fallback classification | **bitwise identical** across N, for all 4 callers × all 4 geometries; and the whole assembled system bitwise identical across N on every orthogonal mesh. (The converged CFD solution need not be bitwise identical — extra non-orthogonal passes may legitimately alter convergence — but the scheme selection must be.) |
| **A2-2** | **Orthogonal correctness (mandatory).** An orthogonal mesh with `non_orthogonal_corrections = 0` now receives DIFF-002. Rerun the manufactured wall-flux convergence test | the **already established** behaviour, unchanged thresholds: W1b quadratic exactness **≤ 1e-12**, W1c cubic observed order **≥ 1.8**, and W1a constant/linear **≤ 1e-12**. This is what prevents A2 from being a k-ε test workaround |
| **A2-3** | **Fallback preservation.** Rerun the A1 topology set | classification mismatches **0**, fallback bitwise mismatches **0**, higher-order-not-exercised **0**, oracle T/G disagreements **0**. A2 must not invalidate W3b-A1 |
| **A2-4** | **k-ε regression**, rerun with its **original** threshold and configuration | `\|Re_τ,distorted − Re_τ,Cartesian\| ≤ 0.01 · Re_τ,Cartesian`, the unmodified assertion. Record both Re_τ, the relative difference, iterations and residuals. If it still fails: **STOP and diagnose** |
| **A2-5** | **W5 revalidated** under the broader activation, against the **original frozen** pre-change values (`poiseuille_flow` 1319, `poiseuille_distorted` 1983, `lid_driven_cavity` 3036, `curved_channel_multiblock` 1400, `duct_3d` 67, `channel_transpiration_graded` 1563, `logs/05`), plus thermal, species, k-ε, multi-block and 3D | **≤ 1.25×**, the original guard, not weakened. Pre-registered risk: cases that previously ran with N = 0 will now exercise DIFF-002 for the first time, so this criterion is genuinely at risk and is not presupposed |
| **A2-6** | **W6 revalidated, complete dataset** this time: dp/dx error, velocity L1, L2, L∞, wall flux, observed order, iterations at 64×8 / 96×12 / 144×18 | the original bound: 144×18 dp/dx error **≤ 0.747 %** (1.10 × the pre-change 0.6788 %). The previous partial record is not relied on |
| **A2-7** | **W7 complete**, every failure classified `NEW DIFF-002 REGRESSION` / `PRE-EXISTING` / `EXPECTED / JUSTIFIED CHANGE` / `UNKNOWN` | no new unexplained regression. A classification does not waive a frozen failure. `GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` stays `PRE-EXISTING` only while it reproduces the recorded GRAD-002 values (1.936 / 1.969 / 1.984) |
| **A2-8** | **W8 unchanged** — `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence`, `MultiBlockProductionCase.CurvedChannelGridConvergence` | reported, not presupposed, not amended. If either fails: **STOP** — that is the separate historical-gate decision point |
| **A2-9** | **W9 / W10** run only if W8 passes | frozen gate order not bypassed |
| **A2-10** | **Production consistency.** No remaining path where the spatial order of a Dirichlet boundary flux depends on the number of non-orthogonal correction iterations requested — momentum, thermal, species, k-ε, and the `Diffusion.cpp` shared/scalar path | verified per caller, by the A2-1 instrument plus the audit table |

## 5. Out of scope (unchanged, recorded, not fixed)

MESH-001/MESH-003 historical gates; the k-ε 1 % threshold; GRAD-002; MESH-007; the pre-existing
gradient-order assertion; the MESH-004 sanitizer UAF; warped 3D faces. No commit, no push.

## 6. Verdicts

```text
A2 itself fails                        -> P12-DIFF-002 BLOCKED / FAILED A2 GATE
A2 and W7 pass but W8 fails            -> P12-DIFF-002 BLOCKED — HISTORICAL W8 GATE DECISION REQUIRED
all required gates pass                -> P12-DIFF-002 COMPLETE UNDER AMENDMENTS A1/A2 — READY FOR
                                          GRAD-002 REVALIDATION
```
