# P17 — Final Qualification Report

```text
P17.11 STATUS      PARTIAL
P17 OVERALL        PARTIAL

Two of ten gates are not PASS, for unrelated reasons, and neither is forced green:
  G6  PARTIAL  -- HLLC star-density positivity fails for a characterized class of admissible input
  G10 PARTIAL  -- the Debug+GUI configuration has 4 timeouts; the other two configurations pass

Everything else passes on the FINAL INTEGRATED TREE, re-measured rather than inherited.
```

This report links to evidence rather than reproducing it. Raw logs are under
`results/p17/qualification/logs/` and each sub-phase's own directory.

---

## 1. Candidate source state

```text
HEAD                    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
branch                  main
commits ahead of origin 0            (nothing committed, nothing pushed, no stash, no reset)
working tree            DIRTY -- 265 entries (129 modified, 136 untracked)

P17_QUALIFICATION_CANDIDATE_FINGERPRINT
    60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318

FINAL P17 SOURCE FINGERPRINT (re-generated after ALL qualification work)
    60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318      IDENTICAL

  components: tracked-diff b4500002...  untracked-set ff02adfe...
  methodology: P13/P17.0's, unchanged -- sha256 of the tracked diff against HEAD plus every
  untracked file, both restricted to code; results/ and the top-level control docs are excluded by
  construction so that writing evidence cannot move the fingerprint of the code being qualified.
```

**The fingerprint did not move across P17.11.** That is the single strongest statement that this
phase qualified the tree rather than changing it — including through the planted-defect control in
§5, which modified `SIMPLE.cpp`, was rebuilt, and was restored with its sha256 verified.

```text
toolchain ACTUALLY used by the qualification builds (not what a bare shell resolves):
  g++ 11.4.0 · clang 14.0.0 · cmake 3.22.1 · ninja 1.10.1 · python 3.10.12
  nvcc /usr/local/cuda-12.9/bin/nvcc, release 12.9 V12.9.86
  compute-sanitizer 2025.2.1.0
  GPU  NVIDIA RTX 5000 Ada Generation Laptop, driver 580.97, 15352 MiB, compute 8.9
  WSL2 Ubuntu 22.04.5 LTS, kernel 6.18.33.2-microsoft-standard-WSL2

  RECORDED BECAUSE IT IS AMBIGUOUS: a bare login shell resolves nvcc to the apt package (11.5) and
  compute-sanitizer to 2021.3.1. Every harness exports /usr/local/cuda-12.9/bin first, and both CUDA
  build trees record CMAKE_CUDA_COMPILER=/usr/local/cuda-12.9/bin/nvcc. The first candidate freeze
  logged the bare-shell versions, which built nothing being qualified; logs/02_toolchain_actual.log
  reports both so the distinction is visible.
```

## 2. Phase scope and implementation summary

P17 added a **second compressible architecture** alongside the existing solver stack; it replaced
nothing. Sub-phases P17.1–P17.10 delivered variable density, the ideal-gas EOS, compressible
continuity and momentum, total energy, a low-Mach family study, the HLLC Riemann flux, a
density-based explicit solver, quantitative shock validation, and a GPU implementation of the
density-based solver proved bitwise-equivalent to the CPU.

## 3. Gate table

| Gate | Requirement | Result | Evidence |
|---|---|---|---|
| G1 | Baseline preservation | **PASS** | `logs/70_fingerprint_summary.log` — P13-001 SIMPLE fingerprint BITWISE IDENTICAL, 10 cases / 130 lines, empty diff; planted defect detected (112 differing lines) |
| G2 | Thermodynamic closure | **PASS** | `logs/30_g2_eos.log`, `30_g2_totalenergy.log` — 76 assertions, 0 failed; round-trip rho/rhoE BITWISE, worst T and p error 0.000e+00 |
| G3 | Variable-density conservation | **PASS** | `logs/30_g3_*.log`, `30_g4_dbconserve.log` — mass rel 8.31e-16, wall mass flux 2.84e-14 vs bound 6.12e-12 |
| G4 | Total-energy conservation | **PASS** | `logs/30_g4_dbconserve.log`, `30_g7_shock.log` — energy rel 1.70e-16; closed-box mass 2.31e-15 / energy 6.31e-15 vs bound 3.55e-13 |
| G5 | Low-Mach limit | **PASS** | `logs/30_g5_lowmach.log` — 4-point Mach family, every error falls monotonically with Ma |
| G6 | HLLC / Riemann correctness | **PARTIAL** | `qualification/G6_POSITIVITY_FAILURE.md` — (a)(b)(c)(d)(f) pass; **(e) positivity fails** for admissible head-on input |
| G7 | Shock validation | **PASS** | `logs/30_g7_shock.log` — 13 tests; all wave positions inside frozen bounds; RH residuals ≤ 1.8e-04 |
| G8 | CPU/GPU equivalence | **PASS** | `logs/30_g8_gpu.log` — BITWISE at every level; 8 negative controls all fire |
| G9 | Deterministic repeatability | **PASS** | CPU and GPU bitwise; GPU also across separate processes and block sizes |
| G10 | Full regression | **PARTIAL** | `qualification/G10_DEBUG_GUI_TIMEOUTS.md` — Release+CUDA and CPU-only PASS; **Debug+GUI has 4 timeouts** |

## 4. Regression table

| Configuration | Total | Passed | Failed | Skipped | Disabled | Runtime | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| Release+CUDA | 2763 | 2712 | 0 | 2 | 49 | 2061 s | **PASS** |
| CPU-only | 2540 | 2493 | 0 | 0 | 47 | 1527 s | **PASS** |
| Debug+GUI | 2825 | 2770 | **4** | 2 | 49 | 8857 s | **FAIL** |

```text
"Total" is SELECTED tests (registered minus the 13 frozen exclusions). Release+CUDA and CPU-only
matched their pre-declared predictions exactly. No prediction was declared for Debug+GUI because the
configuration had never been run in any P17 phase, and a number invented for it would have been a
guess dressed as a control.

THE DISABLED COLUMN IS NEW. The inherited harness printed "Not Run : 0" in every P17 phase by
grepping '\(Not Run\)' while ctest writes 'Not Run (Disabled)'. 49 DISABLED_ tests have never run and
were never counted. None is a P17 test, and no executed count was ever wrong. Fixed in P17.11's
harness; recorded in cpu_gpu_equivalence/HARNESS_FINDINGS.md §1.

THE 4 FAILURES ARE ALL TIMEOUTS at exactly 7200 s, all DriverSeegmiller* -- pre-P17 backward-facing-
step validation using no compressible code. They PASS in both Release configurations (1199-1883 s),
implying a Debug slowdown of >3.8x to >6.0x. P17 did not cause it: their CPU-only runtimes versus
P17.9 are +8.3%, -27.0%, -10.0% (two got FASTER), and libcfdcore.a is byte-identical to P17.8/P17.9's
archives in both Release configurations. The frozen timeout was NOT raised.
```

## 5. Preservation table

| Foundation | Final status | Evidence |
|---|---|---|
| SIMPLE (incompressible) | **PASS — bitwise** | P13-001 fingerprint identical, empty diff; 98 SIMPLE rows pass in all three configurations; 0 EOS references under the pressure-velocity path |
| CompressibleSIMPLE | **PASS** | 33 rows pass in all three configurations |
| P14 generic transport | **PASS** | 40 rows (Release+CUDA / Debug+GUI), 20 CPU-only — the difference is the GPU scalar-transport suite |
| P15 thermal / energy | **PASS** | 422 rows (426 under Debug+GUI, +4 GUI-side) |
| P16 turbulence | **PASS** | 644 rows (550 CPU-only — the difference is the GPU turbulence suite). Deferred P20 advanced SST scope not reopened |

```text
THE SIMPLE FINGERPRINT'S NON-VACUITY WAS DEMONSTRATED, which G1 demands and which no P17 phase
before P17.8 had done: a one-character perturbation of the SIMPLE velocity relaxation (x0.999) was
planted, cfdcore rebuilt, and the fingerprint DIFFERED in 112 lines. Source restored and verified by
sha256 (34f4f995...), library rebuilt from the restored source.
```

## 6. Canonical validation table

| Capability | Result | Key metric |
|---|---|---|
| Variable density | PASS | spatially varying rho through the EOS; time-derivative field exact |
| EOS | PASS | 42 assertions; p = rho R T, cp − cv = R, gamma = cp/cv, a = sqrt(gamma R T) |
| Continuity | PASS | 38 assertions; internal-face cancellation exact by construction |
| Momentum | PASS | 22 assertions; operator vs analytical value worst error 0 (EXACT); divergence-free field bitwise 0 in all 100 cells |
| Total energy | PASS | round-trip rho and rhoE BITWISE, worst relative T and p error 0.000e+00 |
| Low Mach | PASS | drho/rho 1.12e-04 → 1.35e-05 as inlet Ma falls 2.88e-03 → 3.60e-04 |
| HLLC | **PARTIAL** | X1 worst 3.25e-16 vs bound 7.11e-15; antisymmetry BITWISE over 15 cases; **star positivity fails for head-on input** |
| Density-based solver | PASS | closed-box mass 2.31e-15, energy 6.31e-15; 0 retries, 0 rejections, 0 non-finite |

## 7. Shock validation table

| Case | Result | Key quantitative evidence |
|---|---|---|
| Sod | PASS | head err 0.2225 dx (limit 2), tail 0.6180 dx (limit 4), contact 0.1008 dx (limit 2), shock 0.0837 dx (limit 2); L1 rates rho 0.66 / u 0.83 / p 0.75 |
| Contact (stationary) | PASS | position, pressure and velocity variation all **exactly 0**; **0 smeared cells** |
| Contact (moving) | PASS | position 0.141 dx; p variation 4.09e-14, u variation 3.00e-15 |
| Supersonic advection | PASS | centroid position error 2.0e-13; p and u variation ~1e-14 |
| Normal shock M=2 | PASS | rho2/rho1 1.09e-04, p2/p1 5.71e-05, T2/T1 5.23e-05, u2/u1 7.07e-05, M2 9.68e-05 relative; RH residuals mass 1.80e-04, momentum 1.19e-04, enthalpy 4.02e-05; drift 0.0178 dx over 1389 steps |
| Compression | PASS | p* rel 1.77e-03, rho* rel 3.98e-03, both rise; u* exactly 0 |
| Expansion | PASS | p* rel 3.97e-03, rho* rel 2.91e-03, both fall; min rho 0.688, min p 0.612 |

## 8. CPU/GPU equivalence table

```text
quantity                       max CPU-GPU difference     note
density, rhoU, rhoV, rhoW      0  (BITWISE)               1736 recovery values, 1025/step x 50 steps
pressure, temperature          0  (BITWISE)               via the recovery comparison
rhoE                           0  (BITWISE)
face flux (5 components)       0  (BITWISE)               472 faces x 5 = 2360 values
cell residual                  0  (BITWISE)               2495 values over uniform / graded / 3D
                                                          the graded mesh has a 6849:1 volume ratio
dt (local and global)          0  (BITWISE)               global dt 5.55587098248344881e-08 both
mass / momentum / energy       0  (BITWISE)               host-side from the state, identical when
  conservation                                            the state is
detected shock position        0  (BITWISE)               0.7649115536763863 on both

CPU repeat   BITWISE (34 steps, state and every dt)
GPU repeat   BITWISE, same process (30 steps); BITWISE across SEPARATE PROCESSES (stdout sha256
             6e44d998... both, with a 1-character perturbation confirmed detected); BITWISE across
             block sizes 64 vs 128

CONTROL-FLOW EQUIVALENCE (§14): dt history bitwise at every one of 50 steps; step count identical;
HLLC fallback activations 0 on both; step rejections 0 on both; retries 0 on both; positivity
decisions identical including the FIRST offending cell in ascending id order over three kinds of
inadmissible state with two offenders each.

NEGATIVE CONTROLS, all firing: HLL instead of HLLC 868/1085 · reversed gather order 821/1085 ·
neighbour's own normal 3/1085 · CFL sound speed from mean T 217/217 · range check removed (device
accepts 4730.93 K vs a 2500 K ceiling) · regrouped products 1/20 · rhoE carried over 2/3 normals ·
--fmad=true 7 of 15 tests fail. Production with all controls off: 0 of 1085.
```

## 9. Determinism

```text
CPU  low Mach, moving contact, Sod, normal shock -- bitwise across repeats, including dt history,
     residual history, iteration/step count, fallback and retry counts, and conservation
GPU  bitwise across repeats, across separate processes, and across launch configurations
```

## 10. Sanitizer diagnostics

```text
P17 CUDA kernels total        12   (10 production + 2 deliberate-fault control kernels)
kernels exercised             12   ARGUED FROM CALL SITES, not traced -- see the caveat below
memcheck                      0 errors     (15 equivalence tests)
racecheck                     0 hazards    (residual + trajectory)
initcheck                     0 errors
synccheck                     0 errors
  CONTROLS that make those zeros mean something:
    memcheck on a deliberate out-of-bounds read   68 errors, 64 lines naming sanitizerOobKernel
    racecheck on a deliberate shared-memory race  1 hazard, naming sanitizerRaceKernel

host sanitizers  ASan + UBSan, RERUN ON THE FINAL TREE (not previous-phase evidence)
  configuration  Debug + SANITIZERS ON + CUDA OFF + GUI OFF, -fsanitize=address,undefined
  scope          the FOCUSED P17 suite, 315 tests -- NOT the full regression
  result         315 passed, 0 failed; 0 AddressSanitizer errors, 0 leaks, 0 UBSan runtime errors
  control        52 __asan/__ubsan symbols in the test binary, so the zeros are not an
                 uninstrumented build

SCOPE LIMITS, stated rather than implied:
  * NO kernel-launch tracer exists on this machine (nsys and ncu both absent), so kernel coverage is
    argued from call sites plus a launch-count cross-check (180 launches / 20 steps = 9 per step,
    matching the nine production launches a step performs). It is not a traced measurement.
  * initcheck and synccheck have NO dedicated non-vacuity control. They share compute-sanitizer's
    instrumentation with memcheck, which is demonstrated active, so their zeros are not baseless --
    but they are weaker than memcheck's and racecheck's.
  * host sanitizer scope is the P17 suite with CUDA OFF. Whole-program sanitizer coverage is NOT
    claimed, and the CUDA kernels are not covered by ASan.
```

## 11. Transfer and residency

```text
a 20-step resident GPU run:
  kernel launches                       180
  host->device calls PER STEP             0
  device->host calls                     40    (2 per step: the dt reduction's partials and the
                                                validity reduction's -- a scalar and a flag)
  a full per-step state round trip would be 100 device->host calls
  resident bytes                      50488
  upload/download pair                  460 values round-tripped BITWISE unchanged

residency, per field:  rho, rhoU/rhoV/rhoW, rhoE   DEVICE-RESIDENT across steps
                       p, T                        DEVICE-RESIDENT (recomputed per step from Q,
                                                   never transferred)
                       face flux, residual         DEVICE-RESIDENT, never transferred in a
                                                   resident run
No residency claim is made that a measurement does not support.
```

## 12. Performance — separate from qualification (§30, §31, §45)

```text
Numerical qualification: see the gate table. PERFORMANCE IS NOT A GATE and none was applied.

case             cells  mesh             steps  CFL  cpu wall(s)  gpu wall(s)   ratio  equivalence
sod_200x3          600  200x3              200  0.4       0.0302       0.0674   0.45x  BITWISE
sod_800x3         2400  800x3              200  0.4       0.1186       0.0780   1.52x  BITWISE
smooth_100x40     4000  100x40             200  0.4       0.1900       0.0773   2.46x  BITWISE
smooth_200x80    16000  200x80             200  0.4       0.7636       0.0842   9.07x  BITWISE
smooth_400x160   64000  400x160            100  0.4       1.5457       0.0630  24.54x  BITWISE

Every row's numerical equivalence was confirmed BITWISE on the very state that was timed, before any
comparison was made -- §30 forbids comparing timing otherwise.

THE GPU IS SLOWER ON THE SMALLEST CASE (0.45x) and that is reported as measured, not omitted. The
crossover is near ~2400 cells. No speedup is claimed as a result: the CPU side is SINGLE-THREADED,
no multi-core CPU density-based path exists to compare against, and the ratio describes the resident
STEP LOOP with one warm-up step excluded -- not an end-to-end solve.
```

## 13. Known limitations

37 limitations are documented in `results/p17/KNOWN_LIMITATIONS.md`, each backed by evidence, with
deferred future-phase scope listed separately. The load-bearing ones:

```text
L1   HLLC does not bound a strong head-on collision -- the G6 defect. OPEN.
L2   first-order reconstruction only
L3   Euler only -- no viscous stress, no conduction in the density-based path
L4   three boundary conditions; NO subsonic open boundary
L7   the density-based solver is not reachable from a case file
L18  GPU covers the density-based solver only
L22  --fmad=false is REQUIRED for bitwise equivalence, not optional
L36  a Debug regression rewrites tracked validation reports with Debug-computed physics values
L37  four pre-P17 tests cannot complete under Debug within the frozen timeout -- the G10 defect
```

## 14. Amendments

| Amendment | Original issue | Reason | Replacement | Negative control | Result |
|---|---|---|---|---|---|
| P17.9 A1 | Group V vacuous — HLL passed the entire shock gate | a contact is linearly degenerate; no p/u criterion can see it | stationary contact must be preserved with **exactly 0** smeared cells | HLLC 0 cells vs HLL 21 and Rusanov 21 | **GATE-STRENGTHENING**; both controls now FAIL |
| P17.9 A2 | X3 bounded the rarefaction head at 2 dx; measured 11.78 dx and worse under refinement | the head is a derivative discontinuity smeared over sqrt(2Dt), so position moves like sqrt(dx), not dx | gate the derived scaling with **no free constant** | Rusanov gives a larger head error, 6.348e-2 vs 5.892e-2 | corrects a dimensional error; a rigorous absolute bound was **computed and rejected** as tuning |
| P17.9 A3 | S6 required rho*_R to converge; it did not | p* converges at rate 1.367 from the same window while rho*_R does not (−0.045); a contact has no pressure jump | measure the flattest 9-cell sub-window | isolated error 14x / 948x / 227x cleaner | removes contamination; the weaker non-monotone limit is stated, not buried |
| P17.7 | star energy sign | recorded in `hllc/AMENDMENT_star_energy_sign.md` | — | — | preserved |
| P17.8 A6 | "every P17.1–P17.7 source untouched" not met | HllcFlux.hpp changed to fix a real sign error P17.8 found | documented exception | — | preserved as `AMENDMENT_A6_upstream_exception.md` |
| P17.8 N2/N3 | control predictions | recorded in `AMENDMENT_N2_N3_control_predictions.md` | — | — | preserved |

```text
ALL ORIGINAL EVIDENCE IS PRESERVED. The P17.9 frozen gate (b7185cea...) is UNMOVED through all three
amendments -- they live in amendments/ and edit it not at all. The original failing results are
preserved in place. Verified this phase: both inherited gate hashes unmoved, all four amendment files
present.

NO NEW AMENDMENT WAS CREATED IN P17.11. Two gate problems were found (G6, G10) and neither was
amended, because in both cases the CRITERION IS CORRECT and the implementation or configuration does
not meet it. Amending either would have been weakening a valid threshold.
```

## 15. Repository integrity

```text
git status         265 entries -- 129 modified, 136 untracked. DIRTY, and recorded as such.
classification     ALL 265 classified; 0 unexplained. logs/96_tree_integrity.log
    P17 implementation (compressible)     19      pre-existing: core/IO/solver (P13-P16)   39
    P17 implementation (GPU)               6      pre-existing: P16 turbulence             27
    P17 evidence                           1      pre-existing: GPU-DISC discretization    18
    tests (P17 + earlier phases)          99      pre-existing: P15 thermal                14
    evidence / case data                  11      pre-existing: P13 checkpoint/restart     10
    control docs                           2      pre-existing: benchmarks                 10
                                                  pre-existing: P14 transport               5
                                                  pre-existing: GUI                         4

frozen sources at their expected values:
    src/compressible/DensityBasedSolver.cpp   53e84ac7...   MATCHES
    include/cfd/compressible/HllcFlux.hpp     fc4a8db5...   MATCHES
    src/compressible/TotalEnergy.cpp          23bde46f...   MATCHES
    src/pressure_velocity/SIMPLE.cpp          34f4f995...   MATCHES (planted defect restored)

runtime noise      50 tracked results/validation files rewritten by the regressions; every changed
                   line classified by its own file's table header and confirmed a timing value;
                   restored with git checkout; 0 modified afterwards.
history            nothing committed, nothing pushed, no reset, no clean, no stash.
```

## 16. Conclusion

```text
The final integrated P17 tree PRESERVES every earlier CFDApp capability -- SIMPLE bitwise, plus
CompressibleSIMPLE, P14 transport, P15 thermal and P16 turbulence -- and satisfies eight of the ten
compressible-flow gates on re-measurement.

IT DOES NOT FULLY SATISFY TWO:

  G6  a real numerical defect. HLLC's wave-speed estimate does not bound a strong symmetric head-on
      collision, so star densities go negative for admissible input above a wall-normal Mach of
      ~1.76. The solver refuses loudly rather than answering wrongly; a direct caller of hllcFlux()
      does not. Fixing it is a numerical-scheme change that needs its own frozen gate.

  G10 a configuration limitation. Four pre-P17 tests cannot finish under Debug inside the frozen
      7200 s timeout. Nothing is numerically wrong: they pass in both Release configurations and
      produced no wrong answer, only no answer in time.

P17 IS THEREFORE PARTIAL, NOT COMPLETE. The distinction matters: the compressible capability is
qualified and usable within its documented limits, and two specific, characterized items stand
between it and an unqualified PASS. Neither was discovered by accident -- G6 came out of P17.10's
equivalence work, and G10 came out of running a configuration that had been deferred eleven times.
```
