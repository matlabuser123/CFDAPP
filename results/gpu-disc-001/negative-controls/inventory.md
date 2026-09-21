# GPU-DISC-001P — Phase A: inventory of every negative control created during GPU-DISC-001

**112 controls across 13 gates — 102 observable, 10 documented null/unreachable.**

```text
gate                              controls   observable   null   driver preserved?
001B gradients                        2          2          0    no  -- hand-run
001C diffusion                        4          4          0    no  -- hand-run
001D convection                      14         12          2    no  -- hand-run
001E boundary conditions              6          6          0    no  -- hand-run
001F momentum assembly                7          7          0    no  -- hand-run
001G momentum response                6          6          0    no  -- hand-run
001H Rhie-Chow / predicted flux       8          8          0    no  -- hand-run
001I pressure-correction assembly    10          8          2    yes
001J velocity correction             15         11          4    yes
001K face-flux correction            11         11          0    yes
001L single-iteration                10          9          1    yes
001M integrated SIMPLE               11         10          1    yes
001N full-solve                       8          8          0    yes
                                    ---        ---        ---
                                    112        102         10
```

## The two findings this inventory produced

**1. Seven gates left no re-executable driver.** 001B–001H recorded the mutated and restored
harness output, but the mutation itself survives only as a prose row in each `summary.md`. They
cannot be re-run, and nothing checks that the defect they once caught is still caught.

**2. Three of them mutated code that has since moved.** GPU-DISC-001F extracted the diffusion and
convection face terms out of their kernels into `include/cfd/gpu/DeviceDiffusionTerms.hpp` and
`include/cfd/gpu/DeviceConvectionTerms.hpp`. The 001C and 001D controls named files that no longer
contain the lines they mutated. The extraction was re-verified at the time (528/528 and
10352/10352 still bitwise), so this is not a correctness problem — but it does mean those controls'
evidence describes a tree that no longer exists.

Both are why this checkbox could not be ticked from the union of the per-gate runs, and both are
addressed in `summary.md`: every control below is re-derived against the current tree and
re-executed.

## Restoration method

Uniform across all 13 gates and both campaigns: the mutation is applied to a single file, the tree
is rebuilt, the narrowest test is run, the **original bytes** are written back, the tree is rebuilt
again, the file's sha256 is compared against the pre-mutation hash, and the clean baseline is
re-run. Where a column below says `sha256 + re-pass`, that whole sequence was completed and
recorded.

Two refinements were forced by defects in the instrument itself and apply from 001K/001J onward:

* a mutated build that fails to **compile** proves nothing about the port, so the driver returns
  rather than raising — raising once left a mutation in the working tree (001K);
* sha256 is taken over **bytes**, not over a `read_text`/`write_text` round trip, which translates
  newlines and can report a match that is not one (001J).

---

## 1. Gradients — 001B

Harness: `gradients/tools/gradient_equivalence.cpp`, 132 cases, bitwise.
Evidence: `results/gpu-disc-001/gradients/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| NC1 | interior face value uses `dPf`/`dNf` swapped | every interior face value shifts | OBSERVABLE | yes | `cartesian2d 40` | 1828 differing, maxAbs 6.6e-14 | `b1_interior_weights_swapped` |
| NC2 | `-fmad=false` removed from the gradient kernel | nvcc contracts `a*b+c`; CPU rounds twice | OBSERVABLE | yes | first case | 3080 differing, maxRel 0.125 | `x1_fmad_contraction_enabled` |

**Gap found:** no control ever exercised the **boundary** gradient treatment, which is where every
special case in this operator lives (`bcells` 60+ per case). Three new controls added — `b2`, `b3`,
`b4` — plus `b5` for owner/neighbour area orientation.

## 2. Diffusion — 001C

Harness: `diffusion/tools/diffusion_equivalence.cpp`, 528 cases, bitwise.
Evidence: `results/gpu-disc-001/diffusion/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| NC1 | face gradient divides by `(dPf+dNf)` instead of multiplying by the reciprocal | scalar/vector overload confusion | OBSERVABLE | yes | 119/528 cases | **maxAbs 6.5e-19** | `c1_face_gradient_divide` |
| NC2 | internal-face correction applied regardless of the `enabled` flag | non-orthogonal term appears when off | OBSERVABLE | yes | 120/528 | diag 0.185, off-diag 0.048 | `c2_ignores_correction_flag` |
| NC3 | neighbour row adds the explicit flux instead of subtracting | the two rows of a face stop cancelling | OBSERVABLE | yes | 120/528 | RHS maxAbs 0.71 | `c3_rhs_neighbour_sign` |
| NC4 | `-fmad=false` removed from the diffusion kernel | contraction | OBSERVABLE | yes | 468/528 | RHS maxAbs 1.78e-15 | `x1_fmad_contraction_enabled` |

NC1 is the case for a bitwise gate rather than a tolerance: **6.5e-19** is below anything a
tolerance would flag.

**Gap found:** the brief's `diagonal sign reversal` and `omitted off-diagonal` had no control in
this gate. (001F's A1/A6 cover them in the *momentum* assembly, not here.) Added as `c4` and `c5`.

## 3. Convection — 001D

Harnesses: `convection_equivalence.cpp` (1684 cases) and `momentum_convection_equivalence.cpp`
(10352 cases), both bitwise. Evidence: `results/gpu-disc-001/convection/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| NC2 | cell-sum flux sign reversed | owner/neighbour orientation | OBSERVABLE | yes | 1260/1684 | maxAbs 4.7e3 | `d2_reversed_flux_sign` |
| NC3 | Central interpolation weights swapped | wrong interpolation weight | OBSERVABLE | yes | 100/1684 | — | `d3_central_weights_swapped` |
| NC4 | assembly off-diagonal `A(n,o)` sign flipped | matrix asymmetry | OBSERVABLE | yes | 336/1684 | 480 entries differ | `d4_offdiagonal_sign` |
| NC5 | assembly inflow RHS term sign flipped | wrong RHS contribution | OBSERVABLE | yes | 336/1684 | maxAbs 6.3e3 | `d5_rhs_sign` |
| NC6 | `-fmad=false` removed from the convection kernel | contraction | OBSERVABLE | yes | L1 | quickFaceValue 1317/10000 | `x1_fmad_contraction_enabled` |
| NC7 | upwind/downwind swapped on every internal face | owner/downwind swap | OBSERVABLE | yes | 945/1684 | — | `d6_upwind_downwind_swapped` |
| M1 | Central face value divides instead of multiplying by the reciprocal | overload confusion, vector path | OBSERVABLE | yes | 544 cases | **maxAbs 1.6e-16** | `d7_central_divides` |
| M2 | Symmetry boundary keeps the normal component in x | BC projection lost | OBSERVABLE | yes | 904 cases | — | `d8_symmetry_keeps_normal_x` |
| M3 | Outlet (identity) boundary returns the stored constant in x | wrong BC evaluation | OBSERVABLE | yes | 1872 cases | — | `d9_outlet_returns_constant_x` |
| M4 | deferred-correction RHS applied with the same sign to both rows | correction stops conserving | OBSERVABLE | yes | 3840 cases | — | `d10_deferred_rhs_same_sign` |
| M5 | velocity-gradient cell sum divides instead of `*(1/V)` | overload confusion | OBSERVABLE | yes | 446 cases | maxAbs 1.78e-15 | `d11_velocity_gradient_divide` |
| M7 | 3D skew correction transports w with `grad(u)` | 3D-only component error | OBSERVABLE | yes | 96 cases | — | `d13_3d_skew_wrong_gradient` |
| **NC1** | `Ff >= 0.0` → `Ff > 0.0` (zero flux picks the neighbour) | **none** | **PROVABLY NULL** | no, correctly | — | — | `d1_zero_flux_picks_neighbour` |
| **M6** | 2D skew-corrected face value carries the interpolated z instead of `0.0` | **none** | **PROVABLY NULL** | no, correctly | — | — | `d12_2d_skew_z_carried` |

Null proofs are reproduced in `null-controls/proofs.md` rather than restated here.

## 4. Boundary conditions — 001E

Harness: `boundary_condition_equivalence.cpp`, 75 cases, bitwise.
Evidence: `results/gpu-disc-001/boundary-conditions/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| B1 | recorded boundary **type** shifted by one | wrong per-face dispatch | OBSERVABLE | yes | 75/75 | `type[d=32]` | `e1_wrong_type_lookup` |
| B2 | vector evaluator swaps x and y | wrong velocity component | OBSERVABLE | yes | 15/75 | `vector[d=64]` | `e2_component_swap` |
| B3 | ghost mirror drops the factor of two | wrong ghost-value formula | OBSERVABLE | yes | 40/75 | `ghost[d=11]` | `e3_ghost_formula` |
| B4 | constant vector condition returns zero for x | wall/inlet value corruption | OBSERVABLE | yes | 15/75 | `vector[d=32]` | `e4_wall_inlet_value_corrupted` |
| B5 | symmetry projection ignores the z term | **3D-only** defect | OBSERVABLE | yes | 6/75, only 3D meshes | — | `e5_symmetry_ignores_z` |
| B6 | `prescribesBoundaryValue` inverted | wrong classification | OBSERVABLE | yes | 75/75 | — | `e6_prescribes_inverted` |

B5 behaves exactly as a 3D-only defect should: invisible on every 2D mesh, where `n.z == 0` makes
the z term vanish anyway. That is a coverage property of the mesh set, not a null mutation.

## 5. Momentum assembly — 001F

Harness: `momentum_assembly_equivalence.cpp`, 27744 cases, bitwise.
Evidence: `results/gpu-disc-001/momentum-assembly/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | boundary diffusion diagonal sign reversed | diagonal dominance lost | OBSERVABLE | yes | 11560/11560 | — | `f1_boundary_diffusion_diagonal_sign` |
| A2 | pressure-source sign reversed | momentum driven the wrong way | OBSERVABLE | yes | 11560/11560 | — | `f2_pressure_source_sign` |
| A3 | relaxation RHS correction dropped | relaxed system's fixed point moves | OBSERVABLE | yes | 8680/11560 | — | `f3_relax_rhs_dropped` |
| A4 | relaxation factor rewritten `(1-α)/α` | algebraically identical | OBSERVABLE | yes | 5800/11560 | **maxAbs 1.39e-17** | `f4_relax_factor_rewritten` |
| A5 | boundary velocity always reads the U component | wrong component source | OBSERVABLE | yes | 3468/11560 | — | `f5_component_lookup_always_u` |
| A6 | internal diffusion off-diagonal dropped | cells decoupled | OBSERVABLE | yes | 11560/11560 | — | `f6_internal_diffusion_offdiag_dropped` |
| A7 | boundary RHS uses the diagonal coefficient instead of the prescribed-value one | differs only where P12-DIFF-002 fires | OBSERVABLE | yes | 5780/11560 | — | `f7_boundary_rhs_wrong_coefficient` |

A4 is the concrete argument against algebraically rewriting CPU expressions: it is not a bug in any
normal sense, and it is caught only because the gate is bitwise.

## 6. Momentum response coefficients — 001G

Harness: `momentum_response_equivalence.cpp`, 260 cases, bitwise.
Evidence: `results/gpu-disc-001/momentum-response/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| R1 | `1/aP` instead of `V/aP` | cell volume dropped | OBSERVABLE | yes | 92/92 | — | `g1_one_over_ap` |
| R2 | response sign reversed | — | OBSERVABLE | yes | 92/92 | — | `g2_sign_reversal` |
| R3 | `aP/V` instead of `V/aP` | inverted | OBSERVABLE | yes | 92/92 | — | `g3_inverted` |
| R4 | diagonal read with an off-by-one index | wrong cell's diagonal | OBSERVABLE | yes | 88/92 | — | `g4_indexing_off_by_one` |
| R5 | assembly reports the **unrelaxed** diagonal | stale relaxation data | OBSERVABLE | yes | 60/92 — only α ≠ 1 | — | `g5_stale_unrelaxed_diagonal` |
| R6 | assembly reports the row's first entry instead of the diagonal | wrong diagonal | OBSERVABLE | yes | 80/92 | — | `g6_row_first_entry` |

R5's detection *pattern* is itself the result: it fires on exactly the relaxed cases and not the
α = 1 ones.

**Recorded at the time and still true:** the brief's `wrong 2D/3D indexing` category has no
applicable mutation here — `d = V/aP` is a flat per-cell map with no dimension-dependent indexing
at all. R4 is the closest real defect and is detected.

## 7. Rhie–Chow / predicted face flux — 001H

Harness: `face_flux_equivalence.cpp`, 2628 cases, bitwise, plus a checkerboard layer.
Evidence: `results/gpu-disc-001/rhie-chow/negative-control/`.

| ID | mutation | expected effect | obs? | detected | first divergence | metric | re-run here as |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F1 | **Rhie–Chow pressure term removed entirely** | checkerboard decoupling | OBSERVABLE | yes — **by the checkerboard layer** | L4 | — | `h1_rhie_chow_removed` |
| F2 | interpolated gradient term added instead of subtracted | pressure-gradient sign | OBSERVABLE | yes | — | — | `h2_gradient_term_added` |
| F3 | general branch uses the owner response instead of interpolating | wrong response interpolation | OBSERVABLE | yes | — | — | `h3_owner_response_only` |
| F4 | owner/neighbour interpolation weights swapped | — | OBSERVABLE | yes | — | — | `h4_swapped_weights` |
| F5 | area-vector sign reversed | every flux reversed | OBSERVABLE | yes | — | — | `h5_area_vector_sign` |
| F6 | density omitted | coupling and flux stop sharing one density | OBSERVABLE | yes | — | — | `h6_density_omitted` |
| F7 | boundary face uses the raw owner velocity instead of the BC | — | OBSERVABLE | yes | — | — | `h7_boundary_raw_owner_velocity` |
| F8 | axis-aligned response uses the **vector** interpolation form | overload confusion | OBSERVABLE | yes — needs the **full** run | 437 cases | **maxAbs 1.01e-28** | `h8_axis_aligned_vector_form` |

F8 is the reason `--quick` is not a universal substitute: both quick meshes are distorted, so the
axis-aligned branch never executes there. The mutation is genuinely detectable; quick mode simply
does not reach it. `h8` therefore carries no `--quick` flag.

## 8. Pressure-correction assembly — 001I

Harness: `pressure_correction_equivalence.cpp`, 4950 cases, bitwise.
Driver: `pressure-correction-assembly/tools/negative_controls.py` — **re-executed verbatim**.

| ID | mutation | obs? | detected | re-run |
| --- | --- | --- | --- | --- |
| G1 | RHS sign reversed (`rhs = +imbalance`) | OBSERVABLE | yes | yes |
| G2 | the whole face skipped when one endpoint is pinned | OBSERVABLE | yes | yes |
| G3 | additive pin (row accumulates, then `+= 1.0`) | OBSERVABLE | yes | yes |
| G4 | `decomposeAreaVector` fed the geometric area vector | OBSERVABLE | yes | yes |
| G5 | off-diagonal sign reversed | OBSERVABLE | yes | yes |
| G6 | a Neumann-like boundary face given a real coupling and explicit flux | OBSERVABLE | yes | yes |
| G7 | continuity accumulated in reverse traversal order | OBSERVABLE | yes | yes |
| G8 | the `1e-6` well-posedness factor dropped | OBSERVABLE | yes — needs layer L7 | yes |
| **G6n** | the matrix-side FixedValue filter removed | **PROVABLY NULL** | no, correctly | yes |
| **G7n** | continuity gathered in face-id order | **PROVABLY NULL** | no, correctly | yes |

## 9. Velocity correction — 001J

Harness: `velocity_correction_equivalence.cpp`, 894 cases, bitwise.
Driver: `velocity-correction/tools/negative_controls.py` — **re-executed verbatim**.

| ID | mutation | obs? | detected | re-run |
| --- | --- | --- | --- | --- |
| H1 | pressure-gradient term added instead of subtracted | OBSERVABLE | yes | yes |
| H2 | the v correction uses the x gradient | OBSERVABLE | yes | yes |
| H3 | the v correction uses the U response coefficient | OBSERVABLE | yes | yes |
| H4 | the v correction drops the response coefficient | OBSERVABLE | yes | yes |
| H5 | the u correction applied twice | OBSERVABLE | yes | yes |
| H6 | p' gradient not recomputed — a stale gradient reused | OBSERVABLE | yes — needs layer L1b | yes |
| H7 | `makeGradientBoundaries` skipped — correcting with p, not p' | OBSERVABLE | yes | yes |
| H8 | the 2D branch preserves the predictor's z instead of zeroing it | OBSERVABLE | yes | yes |
| H9b | the packed 2D layout read backwards | OBSERVABLE | yes | yes |
| H9c | the 3D adjugate's second row uses the wrong cofactor | OBSERVABLE | yes | yes |
| H11 | oblique-Neumann displacement uses the face centroid | OBSERVABLE | yes | yes |
| **H9** | the 2×2 solve's factors reordered **within** each product | **PROVABLY NULL** | no, correctly | yes |
| **H10** | the Green–Gauss fallback for an ill-conditioned cell dropped | **UNREACHABLE** | no, correctly | yes |
| **H12** | the zero-distance displacement skip removed | **UNREACHABLE** | no, correctly | yes |
| **H13** | a 2D cell's weight includes the z term | **PROVABLY NULL** | no, correctly | yes |

## 10. Face-flux correction — 001K

Harness: `face_flux_correction_equivalence.cpp`, 1848 cases, bitwise.
Driver: `face-flux-correction/tools/negative_controls.py` — **re-executed verbatim**. 11/11
observable, no nulls.

| ID | mutation | obs? | detected |
| --- | --- | --- | --- |
| k1 | correction subtracted instead of added | OBSERVABLE | yes |
| k2 | face jump taken neighbour minus owner | OBSERVABLE | yes |
| k3 | face jump replaced by the owner value alone | OBSERVABLE | yes |
| k4 | the NEXT face's coefficient used | OBSERVABLE | yes |
| k5 | a boundary face reads a neighbour value instead of the Dirichlet 0.0 | OBSERVABLE | yes |
| k6 | correction applied twice | OBSERVABLE | yes |
| k7 | owner's p' read from the next face's owner cell — stale/misindexed | OBSERVABLE | yes |
| k8 | explicit non-orthogonal term dropped | OBSERVABLE | yes |
| k9 | explicit term folded into the correction rather than added separately | OBSERVABLE | yes |
| k10 | a density factor introduced — the double-count | OBSERVABLE | yes |
| k11 | a zero-coefficient boundary face short-circuited rather than evaluated | OBSERVABLE | yes |

**k11 was originally classified null and the driver caught the error.** Production evaluates
`F* + 0.0*(p'_owner - 0.0)`, which turns a `-0.0` predictor into `+0.0`; the branch returns `-0.0`
unchanged. Numerically identical (`maxAbs = 0`), bitwise different — so it is observable, and it is
counted. The lesson is in `null-controls/proofs.md`.

## 11. Single-iteration integration — 001L

Harness: `single_iteration_equivalence.cpp`, 42 cases, 11-stage ladder.
Driver: `single-iteration/tools/negative_controls.py` — **re-executed verbatim**. These mutate the
ladder's *composition*, and each declares the stage at which divergence must **first** appear; a
control detected at the wrong stage is a failure.

| ID | mutation | expected first stage | obs? | detected | re-run |
| --- | --- | --- | --- | --- | --- |
| L1 | velocity correction skipped | 8 corrected U/V/W | OBSERVABLE | yes, stage 8 | yes |
| L2 | face-flux correction skipped | 9 corrected face flux | OBSERVABLE | yes, stage 9 | yes |
| L4 | pressure correction assembled from the start-of-iteration flux | 5 pressure system | OBSERVABLE | yes, stage 5 | yes |
| L5 | pressure update skipped | 7 updated pressure | OBSERVABLE | yes, stage 7 | yes |
| L6 | p' negated before the corrections | 8 corrected U/V/W | OBSERVABLE | yes, stage 8 | yes |
| L7 | flux correction consumes the corrected velocity's flux | 9 corrected face flux | OBSERVABLE | yes, stage 9 | yes |
| L8 | pressure update drops the relaxation factor | 7 updated pressure | OBSERVABLE | yes, stage 7 | yes |
| L9 | momentum fed a pressure that is not the start-of-iteration one | 1 momentum systems | OBSERVABLE | yes, stage 1 | yes |
| L10 | velocity correction applied to the old velocity, not u* | 8 corrected U/V/W | OBSERVABLE | yes, stage 8 | yes |
| **L3** | `dV` fed where `dU` belongs | — | **PROVABLY NULL** | no, correctly | yes |

## 12. Integrated SIMPLE — 001M

Harness: `integrated_simple_equivalence.cpp`, 26 cases, bitwise.
Driver: `integrated-simple/tools/negative_controls.py` — **re-executed verbatim**. These mutate the
production integration (`SIMPLE.cpp` and the device facade), not an operator's arithmetic.

| ID | mutation | obs? | detected | re-run |
| --- | --- | --- | --- | --- |
| M1 | momentum assembly routed back to the CPU, everything else on device | OBSERVABLE | yes | yes |
| M2 | velocity and face-flux corrections routed back to the CPU | OBSERVABLE | yes | yes |
| M3 | `beginIteration` skipped — the device runs on stale fields | OBSERVABLE | yes | yes |
| M4 | the solved predictor never uploaded — stale `velocityStar` | OBSERVABLE | yes | yes |
| M5 | p' never uploaded — both corrections use a stale correction | OBSERVABLE | yes | yes |
| M6 | dispatch flag inverted — the wrong backend entirely | OBSERVABLE | yes | yes |
| M8 | the linear predictor built from the start-of-iteration velocity | OBSERVABLE | yes | yes |
| M9 | the Rhie–Chow predictor built from the start-of-iteration velocity | OBSERVABLE | yes | yes |
| M10 | flux correction applied to the carried mass flux, not the predictor | OBSERVABLE | yes | yes |
| M11 | a fallback reporting `gpuDiscretization = true` — the record lying | OBSERVABLE | yes | yes |
| **M7** | swaps which component's diagonal feeds which response coefficient | **PROVABLY NULL** | no, correctly | yes |

## 13. Full-solve / long-run — 001N

Harness: `full_solve_equivalence.cpp`, 58 cases, iteration-by-iteration histories.
Driver: `full-solve-equivalence/tools/negative_controls.py` — **re-executed verbatim**. Each
declares the outer iteration at which equivalence must first break.

| ID | mutation | expected | obs? | observed | re-run |
| --- | --- | --- | --- | --- | --- |
| N1 | every 5th iteration commits the previous mass flux | iteration 5 | OBSERVABLE | **5** | yes |
| N2 | p' upload skipped on the 3rd iteration | iteration 3 | OBSERVABLE | **3** | yes |
| N3 | the U predictor uploaded into the V slot | iteration 1 | OBSERVABLE | **1** | yes |
| N4 | response coefficients computed on iteration 1 only, then reused | iteration 2 | OBSERVABLE | **2** | yes |
| N5 | pressure update skipped every 7th iteration of the GPU arm | iteration 7 | OBSERVABLE | **7** | yes |
| N6 | pressure correction assembled from the previous iteration's flux | iteration 1 | OBSERVABLE | **1** | yes |
| N7 | one iteration forced through a CPU/GPU mixed path | explicit error | OBSERVABLE | **explicit error** | yes |
| N8 | the V predictor never uploaded | explicit error | OBSERVABLE | **explicit error** | yes |

N3 and N8 are the two that found a **real production defect** — a missing residency guard, since
fixed with `requireResident`. N7 and N8 are detected by an explicit, named exception rather than a
numbered iteration, which is the all-or-nothing rule from 001M holding under an adversarial
mutation: a half-GPU iteration cannot run, it can only fail.

---

## Coverage of the required matrix, before this gate

| layer | brief's examples | covered before | missing before |
| --- | --- | --- | --- |
| 1 gradients | owner/neighbour · boundary treatment · 2D packing · 3D cofactor | NC1 · H9b · H9c | **boundary gradient treatment** |
| 2 diffusion | diagonal sign · omitted off-diagonal · face coefficient · RHS sign | NC1 · NC3 | **diagonal sign** · **omitted off-diagonal** |
| 3 convection | flux sign · owner/downwind · weight · RHS | NC2 · NC7 · NC3 · NC5 | — |
| 4 boundary conditions | type lookup · component · ghost formula · value corruption | B1 · B2 · B3 · B4 | — |
| 5 momentum assembly | pressure sign · relaxation · dropped term · component | A2 · A3 · A1/A6 · A5 | — |
| 6 momentum response | omit volume · wrong diagonal · component · sign | R1 · R6 · R4 · R2 | — |
| 7 Rhie–Chow | **remove RC** · pressure sign · response interp · area sign | F1 · F2 · F3/F4 · F5 | — |
| 8 pressure-correction | RHS sign · off-diag sign · skip opposite row · additive pin · FixedValue | G1 · G5 · G2 · G3 · G6 | — |
| 9 velocity correction | grad sign · response index · component · twice | H1 · H3 · H2 · H5 | — |
| 10 face-flux correction | sign · owner/neighbour · stale p' · coefficient · twice | k1 · k2 · k7 · k4 · k6 | — |
| 11 integrated SIMPLE | skip p / skip corrections / stale / reorder / CPU fallback | L5 · L1 · L2 · L3→N4 · L4 · L7/M7 · M1/M2 | — |
| 12 full-solve / long-run | intermittent, multi-iteration | N1 · N2 · N5 · N6 | — |

Six new controls close the three gaps: `b2`, `b3`, `b4`, `b5` (gradients) and `c4`, `c5`
(diffusion). Results are in `summary.md`.
