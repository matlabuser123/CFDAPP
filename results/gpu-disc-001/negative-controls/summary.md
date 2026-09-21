# GPU-DISC-001P — Negative controls

**Result: PASS.** Every observable negative control in GPU-DISC-001 was re-executed against the
current tree and **every one was detected** — 106 of 106. Ten null or unreachable controls were
also executed, all confirmed unobservable, and none counted.

```text
controls inventoried          112   (13 gates)
controls in the suite         116   = 112 + 6 new gap-fillers - 2 (fmad controls consolidated)
observable                    106
DETECTED                      106/106 = 100%
provably null                   8    confirmed unobservable, excluded from the denominator
unreachable                     2    confirmed unobservable, excluded from the denominator
sha256 restoration            116/116 byte-for-byte
clean baseline re-pass        116/116
verdict drift vs history        0    all six preserved gates identical
```

This checkbox could not be ticked from the union of the per-gate runs, and the audit says exactly
why. Three findings drove the work.

## 1. The three findings

**Seven gates left no re-executable driver.** 001B–001H preserved their mutated and restored
harness output, but not the mutation. Their controls existed only as prose rows in each
`summary.md`; nothing could re-check that the defect they once caught is still caught.

**Three of them mutated code that has since moved.** GPU-DISC-001F extracted the diffusion and
convection face terms into `include/cfd/gpu/DeviceDiffusionTerms.hpp` and
`DeviceConvectionTerms.hpp`. The 001C and 001D controls name files that no longer contain the lines
they mutated. The extraction was re-verified at the time (528/528 and 10,352/10,352 still bitwise),
so this was never a correctness problem — but their evidence described a tree that no longer
exists.

**Three layers had no control for a defect class the matrix requires.** Recorded in
`inventory.md` §"Coverage of the required matrix, before this gate":

| layer | missing class | why it was missed |
| --- | --- | --- |
| gradients | wrong **boundary** gradient treatment | 001B's two controls were an interior interpolation weight and a compile flag. Every special case in this operator — the P12-GRAD-002 claim, the oblique-Neumann re-evaluation, the boundary encoding — was unmutated. |
| diffusion | **diagonal sign reversal** | 001F's A1 covers it for the *momentum* assembly. The diffusion gate itself had none. |
| diffusion | **omitted off-diagonal** | likewise: 001F's A6, not 001C. |

Six new controls close them, and all six are detected.

## 2. What was built

```text
tools/control_engine.py       the Phase D discipline, once, for every control
tools/operator_controls.py    51 controls for the seven gates that preserved no driver
tools/integration_campaign.sh re-executes the six gates that did, VERBATIM
tools/baseline_sha.sh         31 mutable production sources, hashed before anything is touched
tools/organise_evidence.py    files each control's logs under its subsystem
tools/post_restoration_diagnostics.sh
tools/regression.sh
tools/finalise.sh
```

The six preserved drivers are run **verbatim, not transcribed**. Transcription is the one way a
control can silently stop testing what its name says, and there was no reason to take that risk
when the originals still apply to the current tree. The campaign also **diffs each gate's verdicts
against its historical `driver.log`**, snapshotted first, so drift would be visible rather than
silently overwritten by the re-run.

### Phase D, applied identically to all 116

```text
1  record the clean source sha256      (once, before anything is touched)
2  apply exactly ONE mutation
3  rebuild only as necessary
4  run the NARROWEST test expected to detect it
5  record whether detection occurred
6  record the first divergence
7  restore the production source, byte for byte
8  verify sha256 restoration
9  rebuild
10 re-run the clean baseline
```

Two refinements inherited from earlier gates' own instrument defects are enforced by the engine: a
mutated build that fails to **compile** is reported `ILL-FORMED`, never as a pass, and the driver
returns rather than raises so the caller still restores the file (001K left a mutation in the tree
exactly once by raising); and sha256 is taken over **bytes**, never a `read_text`/`write_text`
round trip, which translates newlines and can report a match that is not one (001J).

## 3. Results by layer

| layer | controls | observable | detected | null | evidence |
| --- | --- | --- | --- | --- | --- |
| 1 gradients | 5 | 5 | **5/5** | 0 | `gradients/` |
| 2 diffusion | 5 | 5 | **5/5** | 0 | `diffusion/` |
| 3 convection | 13 | 11 | **11/11** | 2 | `convection/` |
| 4 boundary conditions | 6 | 6 | **6/6** | 0 | `boundary-conditions/` |
| 5 momentum assembly | 7 | 7 | **7/7** | 0 | `momentum-assembly/` |
| 6 momentum response | 6 | 6 | **6/6** | 0 | `momentum-response/` |
| 7 Rhie–Chow / predicted flux | 8 | 8 | **8/8** | 0 | `rhie-chow/` |
| build flags (`-fmad=false`) | 1 | 1 | **1/1** | 0 | `build-flags/` |
| 8 pressure-correction assembly | 10 | 8 | **8/8** | 2 | `pressure-correction/` |
| 9 velocity correction | 15 | 11 | **11/11** | 4 | `velocity-correction/` |
| 10 face-flux correction | 11 | 11 | **11/11** | 0 | `face-flux-correction/` |
| 11a single-iteration integration | 10 | 9 | **9/9** | 1 | `single-iteration/` |
| 11b integrated SIMPLE | 11 | 10 | **10/10** | 1 | `integrated-simple/` |
| 12 full-solve / long-run | 8 | 8 | **8/8** | 0 | `full-solve/` |
| **total** | **116** | **106** | **106/106** | **10** | |

### The six new controls

| id | layer | mutation | detected |
| --- | --- | --- | --- |
| `b2` | gradients | the boundary face value ignores the condition and carries the owner value — zero-gradient assumed on every boundary face whatever the patch says | yes |
| `b3` | gradients | the P12-GRAD-002 claim keeps the raw face value and drops the second-derivative correction — the defect GRAD-002 exists to fix | yes |
| `b4` | gradients | the P12-MESH-001 oblique-Neumann face drops the tangential gradient term | yes |
| `b5` | gradients | the Green–Gauss sum negates the area vector for the faces it OWNS instead of the faces it neighbours | yes |
| `c4` | diffusion | the internal-face contribution is subtracted from the diagonal instead of added | yes |
| `c5` | diffusion | the internal-face off-diagonal coefficient is dropped, decoupling neighbouring cells | yes |

### The Rhie–Chow requirement, met specifically

The brief requires at least one control that removes or corrupts the actual Rhie–Chow pressure
term, and that **the checkerboard-sensitive case must detect it**. `h1_rhie_chow_removed` zeroes the
term, leaving plain linear interpolation — the classical checkerboard-prone mode. Counted by
harness layer, it fails:

```text
FAIL L1   864 cases   the correction and the combined Rhie-Chow flux
FAIL L2     8 cases   the chained predictor
FAIL L4     2 cases   the CHECKERBOARD layer  <- the brief's specific requirement
```

So the checkerboard-sensitive case does detect it, which is what was asked; it is not the only
layer that does, and claiming otherwise would overstate the result. `h8` needed the **full**
differential rather than
`--quick`, because both quick meshes are distorted and the axis-aligned coupling branch never runs
there; that is a coverage property of quick mode, not a property of the mutation, and the control
carries no `--quick` flag for exactly that reason.

## 4. First-divergence localization — and the gap that was found in it

The brief calls a test that reports only "final result differs" insufficient for integration
controls. Checking that requirement against the harnesses found a real gap:

* **001L** localized to one of 11 named stages. Good.
* **001N** localized to an outer **iteration** — but not to a stage or a metric.
* **001M** localized to **neither**: it reported per-field discrepancy counts and nothing else.
  Ten production-integration controls had no localization at all.

**Harness improvement.** Each per-outer-iteration residual history is now attributed to the SIMPLE
stage that produces it, and both harnesses report the **earliest** divergence across all five
histories with its stage and metric. Scanning for the earliest rather than the first-found means
the answer does not depend on the order the histories are compared in; a genuine tie keeps the
earlier stage in SIMPLE's own order, which is that order. No threshold moved and no production code
changed — the new lines print only on a failure.

The result, with the attributions visibly correct rather than merely present:

| control | mutation | first divergence |
| --- | --- | --- |
| `m8` | predicted face flux built from the start-of-iteration velocity | iteration 1, **pressure-correction solve**, pressure residual |
| `m9` | Rhie–Chow predictor from the start-of-iteration velocity | iteration 1, **pressure-correction solve**, pressure residual |
| `m10` | flux correction applied to the carried mass flux | iteration 1, **velocity / face-flux correction → continuity** |
| `n1` | every 5th iteration commits the previous mass flux | **iteration 5**, correction → continuity |
| `n2` | p' upload skipped on the 3rd iteration | **iteration 3**, correction → continuity |
| `n3` | the U predictor uploaded into the V slot | **iteration 1**, pressure-correction solve |
| `n4` | response coefficients computed once, then reused | **iteration 2**, correction → continuity |
| `n5` | pressure update skipped every 7th iteration of the GPU arm | **iteration 7**, momentum solve |
| `n6` | pressure correction assembled from the previous iteration's flux | **iteration 1**, pressure-correction solve |

`m8` and `m9` land on the pressure residual because the predicted face flux is what feeds the
pressure-correction RHS; `m10` lands on continuity because the corrected flux is what continuity
measures; `n5` lands on the *momentum* solve because a skipped pressure update is not visible until
the next iteration's momentum assembly reads that pressure. Each is the stage the mutation actually
breaks, not merely a stage.

### The controls that report no iteration are localized more sharply, not less

Seven controls (`m1`–`m6`, `m11`, `n7`, `n8`) produce no numeric divergence because they never get
far enough to produce one:

```text
m1  momentum routed back to the CPU -> InvalidArgumentError:
      "GpuSimpleDiscretization: the U momentum diagonal is not resident
       -- the stage that supplies it was skipped"
m4  the solved predictor never uploaded ->
      "the U momentum predictor is not resident -- the stage that supplies it was skipped"
m11 a fallback reporting gpuDiscretization = true -> caught by the dispatch layer, not by numbers
```

That is the `requireResident` guard added in 001N — itself found by a negative control — and the
all-or-nothing rule from 001M holding under adversarial mutation. A half-GPU iteration cannot
quietly compute something wrong; it can only fail, and name the stage that was skipped. A named
failure is a stronger localization than a numbered iteration, and it is recorded as such rather
than as a missing measurement.

## 5. Null and unreachable controls

Ten, with proofs in `null-controls/proofs.md`, all **executed** and all confirmed unobservable.
The drivers treat a control declared `null` that *is* detected as a run failure, so each claim is
checked rather than asserted.

```text
PROVABLY NULL    8   NC1, M6 (convection) · G6n, G7n (pressure-correction)
                     H9, H13 (velocity correction) · L3 · M7
UNREACHABLE      2   H10, H12 (velocity correction), each with a MEASURED count of zero
```

Proof categories used: multiplication by exact zero; addition of exact `+0.0` with the `-0.0` case
eliminated; a value written but never read in the active dimension; two traversals that are one
sequence; IEEE multiplication being commutative; an algebraic identity from a property of the
codebase; and a branch no mesh `MeshGeometry` can build.

**The counter-example that shapes the classification is preserved rather than tidied away.** 001K's
`k11` was claimed null on the reasoning that multiplying by exactly `0.0` and adding the result
changes nothing. That is false — `x + (+0.0) == x` for every finite `x` **except** `x = -0.0`, which
becomes `+0.0` — and the driver caught it. `k11` is observable and is counted. Every proof in the
document is written to survive that test: each either names why the `-0.0` case cannot arise, or
does not depend on additive identity at all.

Two consequences are stated rather than hidden, because they are limits on what this project can
claim:

* the `>=` / `>` boundary of the upwind predicate is **not verified by test** — no observable
  depends on it, so that code matches the CPU by transcription;
* `dU == dV == dW` exactly and always, so the anisotropic response `D = diag(d_u, d_v, d_w)` is
  isotropic in practice and a whole class of component mis-wiring is undetectable. The 001L harness
  **asserts** the property on every case, so the day it stops holding, L3 and M7 stop being null.

## 6. Baseline integrity

Every mutation was applied to a file whose pre-mutation **bytes** were captured before anything was
touched, and restored from those bytes. `baseline-sha256.txt` records all 31 mutable production
sources as they stood before the first mutation; `regression/post-campaign-sha256.txt` records them
after the last one.

```text
diff baseline-sha256.txt post-campaign-sha256.txt   ->  empty
31/31 sources IDENTICAL, byte for byte
```

That set includes `cuda/CMakeLists.txt`, which the `-fmad` control mutated, and
`src/pressure_velocity/SIMPLE.cpp`, which nine integration controls mutated. **No mutation remains
in the production source.**

### The archive hash moved, and the reason was measured rather than assumed

`build/cuda/cuda/libcfdcuda.a` hashes differently after the campaign even though every source is
restored byte for byte. The obvious explanation — "archives aren't reproducible" — is **wrong
here**: `ar tv` shows the archive is built in deterministic mode, with uid/gid `0/0` and mtime
zeroed. So the difference had to be either a member object or the member order, and that is a
question worth an experiment rather than a paragraph.

`tools/rebuild_reproducibility.sh` settles it. One kernel is touched — mtime only, so its sha256 is
unchanged — and recompiled:

```text
source sha256   662b72d4...  before AND after   (unchanged, as required)
object sha256   a8f27a95...  ->  fbe28141...    (CHANGED)

VERDICT: nvcc is NOT bit-reproducible on this toolchain.
```

So the archive hash carries **no information** about source identity, and it is not used as
evidence anywhere in this gate. What does carry the information is the 31-source sha256 set, and
the behaviour of the binaries built from it.

### Which forced a second finalisation, and that turned out to be worth having

The reproducibility probe rebuilt the tree, which meant the diagnostics, gates and regression
already recorded corresponded to a *previous* binary. That is precisely the stale-binary hazard
this project has a rule about, self-inflicted. Rather than argue it did not matter, all three were
re-run; `*_run1.log` preserves the first pass.

| | run 1 | run 2 (independent build) | |
| --- | --- | --- | --- |
| sanitizer runs | 20/20 clean, non-vacuous | 20/20 clean, non-vacuous | **identical** |
| kernel counts | 1000 / 2911 / 4925 / 5370 / 37932 | identical | **identical** |
| differential gates | 15/15 green | 15/15 green | case and failure counts **identical** |
| full regression | 1998/1998, 1310.19 s | 1998/1998, 1286.16 s | **identical** |

Two independently compiled binaries from byte-identical sources produce identical results
everywhere. That is what makes nvcc's non-determinism demonstrably *metadata* rather than
behaviour — measured, not asserted.

## 7. CUDA diagnostics after the campaign

The campaign mutated indexing (`g4` off-by-one, `k4`/`k7` next-face indices), buffer layout (`h9b`
packed 2D least-squares), memory ownership (`m3`/`m4`/`m5`/`n8` skipped uploads) and 2D/3D buffer
contracts (`d12`, `d13`, `e5`, `h8`), so the brief's rule applies: the repository must not be left
in a state where the sanitizers were only run *before* a mutation campaign.

`cuda-diagnostics/` — the GPU-DISC-001O workload, same build flags, same non-vacuity rule (a log
without `PRODUCTION GPU PATH EXERCISED` is VACUOUS whatever its summary says):

```text
tool        modes                                            result
memcheck    cavity2d case3d schemes nonorthogonal gpusolver   5/5   0 errors
initcheck   "                                            "    5/5   0 errors
synccheck   "                                            "    5/5   0 errors
racecheck   "                                            "    5/5   0 hazards
                                                            20/20  clean, all non-vacuous
```

Modes were chosen to cover exactly the contracts the mutations touched: `case3d` for the 3D
cofactor path and 3D BC storage (`d13`, `e5`, `h9c`), `schemes` for the packed 2D least-squares
layout (`h9b`), `nonorthogonal` for oblique-Neumann and skew geometry (`b4`, `h11`), and
`gpusolver` because it is the only mode that reaches the reduction kernels — the only code in the
GPU path with shared memory and barriers, and therefore the only place racecheck and synccheck are
non-vacuous at all (GPU-DISC-001O §1).

## 8. Gates and regression

`restoration/final_all_gates.log`, `regression/`

```text
15/15 GPU-DISC differential gates green
  mesh 96 | gradients 132 | diffusion 528 | convection 1684 | momentum convection 10352
  boundary conditions 75 | momentum assembly 27744 | momentum response 260
  Rhie-Chow 2628 | pressure-correction 4950 | velocity correction 894
  face-flux correction 1848 | single-iteration 42 | integrated SIMPLE 26 | full-solve 17
  all failures = 0

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998   (45 disabled of 2043 registered)
  Total Test time (real) = 1286.16 s
  `ninja: no work to do` BEFORE and AFTER ctest
```

Integrated SIMPLE and full-solve equivalence are both among the 15 and both green, including with
the improved harnesses.

## 9. Where the evidence lives

```text
results/gpu-disc-001/negative-controls/
  inventory.md                 Phase A -- all 112 historical controls, 10 fields each
  summary.md                   this file
  baseline-sha256.txt          31 sources, before the first mutation
  null-controls/proofs.md      Phase C -- 10 proofs, and the k11 counter-example
  tools/                       the engine, the control definitions, the campaign scripts
  gradients/ diffusion/ convection/ boundary-conditions/ momentum-assembly/
  momentum-response/ rhie-chow/ build-flags/
                               102 per-control logs from the operator campaign
  pressure-correction/ velocity-correction/ face-flux-correction/
  single-iteration/ integrated-simple/ full-solve/
                               each gate's driver.log from this campaign, plus
                               historical-driver.log -- the verdict record BEFORE it
  cuda-diagnostics/            20 sanitizer logs (run.log) + run1.log
  restoration/                 baseline and final gate runs, both finalisation passes,
                               the campaign transcripts, the reproducibility probe
  regression/                  ctest log, freshness log, post-campaign sha256, the diff
```

The six preserved drivers write their **per-control** logs into their own gate trees —
`results/gpu-disc-001/<gate>/negative-control*/` — which is where the originals already lived. This
directory holds their `driver.log` and the pre-campaign `historical-driver.log` snapshot, so the
comparison is self-contained here while the detailed output stays with its gate.

## 10. What is NOT claimed, and what is NOT started

* **`Performance qualification` is not touched and is not ready.** GPU-DISC-001N §10 measured the
  GPU arm *slower* — 12.1x at 8² falling to 1.24x at 40² — because every assembled system still
  round-trips to a host solver. Nothing has been optimized, by instruction.
* **`Full regression` as a GPU-DISC-001-wide checkbox is not ticked.** 1998/1998 passes here as it
  has after every gate, but that checkbox's own scope has never been written down, and this gate
  did not audit it.
* **GPU-PIPE-001 persistent residency is not started.**
* **Nothing was committed or pushed.**

Two limits on coverage are restated because they are real and should not be rediscovered as
surprises: the `>=`/`>` boundary of the upwind predicate is not verified by any test, and
`dU == dV == dW` holds exactly and always, so component mis-wiring between response coefficients is
undetectable. Both are argued in `null-controls/proofs.md`, and the 001L harness asserts the second
on every case so it cannot silently stop being true.
