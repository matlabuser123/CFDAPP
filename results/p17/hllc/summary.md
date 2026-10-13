# P17.7 — HLLC / Riemann Flux: summary

```text
VERDICT   COMPLETE. A coordinate-independent, conservative, contact-resolving HLLC numerical flux
          whose mass, momentum and total-energy components are thermodynamically consistent with
          P17.2/P17.5, verified analytically across every wave branch against an INDEPENDENT 60-digit
          exact Riemann solver, with CPU and GPU agreeing BITWISE.
          39 new tests, 0 failures. Both regression configurations matched every frozen prediction
          exactly. 0 production files modified; 22 upstream files UNTOUCHED by SHA-256.
```

---

## 0. THE FINDING THAT CHANGED THE DESIGN, before any code existed

The authorization (§9) offers Davis's direct bounds as *"a common admissible baseline"*. Measured
against the independent exact solver **before `HllcFlux.hpp` was written**:

```text
                       does the estimator BOUND the true signal speeds?
Davis (Toro 10.48)     FAILS in 8 of 14 reference cases
                       for Sod it gives S_R = 1.1831 while the right shock travels at 1.7521
Toro PVRS (10.59)      passes 14 of 14
```

An estimator that does not enclose the true wave speeds lets signal escape the control volume — a
**stability** defect, not an accuracy one, and P17.8 is exactly the phase that would have inherited it.
§9 permits *"a stronger established estimate"* and requires the formulation to be documented, so Toro's
pressure-based estimate is the production choice and **Davis is kept as the control** that proves the
criterion has teeth.

```text
This is what the pre-registration protocol is for: the defect was found by DRY-RUNNING the reference
against theory, not by a test failing later, and the estimator was chosen by measurement rather than by
citation.
```

## 1. What was built

```text
include/cfd/compressible/HllcFlux.hpp        ALL the numerics -- ONE source, compiled by gcc AND nvcc
src/compressible/HllcFlux.cpp                the host-only factory and the throwing wrappers
include/cfd/gpu/DeviceHllcFlux.hpp           the device launcher's declarations
cuda/kernels/DeviceHllcFluxKernel.cu         marshalling only -- 0 formulas
tests/unit/compressible/test_hllc_flux.cpp   35 TESTs
tests/unit/compressible/hllc_reference_data.hpp  GENERATED from the frozen reference
tests/unit/gpu/test_hllc_gpu_equivalence.cpp  4 TESTs
4 CMakeLists edits                           production sources modified: ZERO
```

The header is marked `CFD_HLLC_HOST_DEVICE`, so **CPU and GPU run the same source**. §28 asks to avoid
copy-pasted mathematical logic where a shared implementation is practical; for a pointwise flux it is,
and it is what makes CPU/GPU equivalence bitwise instead of a tolerance.

## 2. THE MEASURED RESULTS — every derived threshold, and the margin

```text
criterion                                        bound        measured           margin
F3  physical flux vs the reference                8 eps        1.114e-17           160x
C1  uniform supersonic == F_L / F_R               BITWISE      BITWISE              --
C2  uniform subsonic consistency                  32 eps       0.000e+00          exact
W1  wave speeds vs the reference                  8 eps        2.172e-16           8.2x
W2  S_L < S* < S_R                                boolean      15 of 15             --
W3  |denom| / (rho_L a_L + rho_R a_R)             >= 1         1.000000000000000  attained
W4  the estimator BOUNDS the true signal speeds   boolean      14 of 14             --
B1/B2 supersonic branches return F_L / F_R        BITWISE      BITWISE              --
B3/B4 star branches vs the reference              32 eps       1.833e-16            39x
S1  rho* > 0                                      boolean      min 1.314130e-05     --
S3  star internal energy > 0                      boolean      min 1.977800e+00     --
T1  stationary contact mass & energy flux         BITWISE 0    exactly 0            --
T3  tangential shear mass & tang. momentum        BITWISE 0    exactly 0            --
R1  dyadic rotation                               BITWISE      BITWISE              --
R3  area scaling; wave speeds unaffected          BITWISE      BITWISE              --
N1  F(L,R,n) = -F(R,L,-n)                         BITWISE      BITWISE, 15 cases    --
X1  == the exact Godunov flux where HLLC is exact 32 eps       3.254e-16            22x
X2  == the 60-digit HLLC everywhere               32 eps       3.254e-16            22x
G2  CPU vs GPU                                    BITWISE      17/17 rows, diff 0   --
G4  determinism, CPU and GPU                      BITWISE      BITWISE              --
```

Every bitwise claim was **derived in advance** (C1, T1, N1, R1 each have a written derivation in the
frozen gate) and then held. The one derivation that proved *conservative* is C2: a 32 eps bound was
derived and the measurement came out exactly 0.

```text
branch coverage   F_L = 2   F*_L = 12   F*_R = 1   F_R = 1     all four, each with a non-zero guard
Sod initial state S_L = -1.1831394376381720   S* = 0.67616713174314473   S_R = 1.7520928142199601
                  rho*_L = 0.63633370479181441   rho*_R = 0.29171697937952814
                  F = [0.43026793600057014, 0.49093303616654848, 0, 0, 0.30167470754041015]
```

## 3. §10 answered by proof, not by an epsilon

```text
q_K >= 1 for both estimators, so (S_L - u_nL) <= -a_L < 0 and (S_R - u_nR) >= +a_R > 0, hence
    |denom| >= rho_L a_L + rho_R a_R > 0  for ANY admissible pair of states.
MEASURED: the smallest ratio over every case is 1.000000000000000 -- the bound is ATTAINED (when
u_nL = u_nR and a_L = a_R), which confirms the derivation rather than merely failing to contradict it.
```

**So there is no epsilon guard and no fallback flux.** §15 permits a fallback only if justified by an
actual failure mode; there is none to fall back from, and a silent switch of solver type is what §10
forbids. There are consequently no fallback counters — criterion S5 asks for them to read zero and is
reported NOT APPLICABLE with that reason rather than as satisfied.

## 4. The contact wave — and its non-vacuity, in physical units

```text
stationary contact (rho 1.0/0.125, u = 0, p equal):
  HLLC   mass flux EXACTLY 0, energy flux EXACTLY 0, momentum flux EXACTLY p n
  HLL    spurious mass flux 243.4595971727 kg/(m^2 s) where the truth is 0
         -- while STILL passing uniform-state consistency perfectly (0.000e+00)
1e6 density-ratio contact:
  HLL    spurious mass flux 11897.6099070769 kg/(m^2 s)
```

That is precisely the structure the frozen G6 criterion names as its non-vacuity backbone, and it is
verified on CPU **and** GPU, so a device path cannot silently be a different scheme.

## 5. CPU/GPU — bitwise, because there is one source

```text
17 rows (every §29 state, plus a constructed F*_R case and the Davis estimator on device)
  rows BITWISE identical        17 of 17
  branch-selection differences  0
  max |flux difference|         0
  max |wave-speed difference|   0
compared: flux (5), S_L, S*, S_R, the denominator, both rho*, both e*, the branch, the validity code
8 inadmissible inputs refused on device with the SAME reason code as the host
```

Criterion G3, the cost of `--fmad=false`, measured as a controlled experiment and restored with a
SHA-256-verified rollback:

```text
--fmad=false (production)  17/17 bitwise, max diff 0
--fmad=true                 8/17 bitwise, max diff 1.526e-04 absolute = 1.9e-15 relative
                            wave speeds UNAFFECTED; branch selection NEVER differed
```

`-fmad=false` is already the project's convention for its twenty-odd existing kernels, so this is not a
new concession.

## 6. Regression

```text
                registered         executed          failed   runtime
Release+CUDA    2696 = PRED 2696   2634 = PRED 2634  0        42.9 min
CPU-only        2490 = PRED 2490   2430 = PRED 2430  0        32.3 min
B8 fingerprint  1dbc248e... before == after
SIMPLE fingerprint  BITWISE IDENTICAL, 0-byte diff
upstream            22 UNTOUCHED, 0 TOUCHED by SHA-256
```

## 7. Controls — all seven detect

```text
V1  Davis wave speeds                  fails W4 in 8 of 14   PERMANENT test, asserts >= 5 failures
V2  the contact wave deleted (HLL)     243.46 spurious       PERMANENT test, CPU and GPU
V3  star tangential velocity swapped   3 tests
V4  S* = 0.5(u_nL + u_nR)              5 tests
V5  the p*n term dropped               8 tests
V6  the unit-normal check removed      2 tests (incl. the GPU refusal path)
V7  star density / (S_K - u_nK)        4 tests
restore  SHA-256 verified after every plant, 0 markers left, back to 39/39
```

```text
V1 AND V2 ARE PERMANENT TESTS, NOT PLANTS, and that is a deliberate improvement. Both defects exist as
real callable code worth keeping -- WaveSpeedEstimator::Davis and hllFluxForControlOnly(). A permanent
test is re-checked by every future regression; a control script can rot unnoticed. Where the
alternative implementation has independent value, the permanent test is the stronger choice.
```

## 8. What I got wrong in this phase

```text
1  MY OWN DERIVED DENOMINATOR BOUND was tested with a strict inequality at 60 digits and reported
   FALSE for the two supersonic cases. The bound does not fail there -- it is ATTAINED, and two
   summation orders disagree in the last digits. The reference now reports the RATIO instead of a
   boolean, and the C++ criterion carries 4 eps of slack for the equality case.
2  MY OWN ERROR METRIC in the reference script normalised by max(|a|,|b|), which COLLAPSES when the
   true flux is exactly zero. It reported 60-digit round-off of 9.5e-58 as a "relative error of 1.0",
   and -- worse -- printed the SAME 1.0 for a genuinely spurious 466 kg/(m^2 s) HLL flux. Replaced by
   a scale built from the states, which cannot collapse. Without this fix the headline contact
   evidence would have been unreadable.
3  THE REGRESSION SCRIPT called a staleness checker that lives in P17.6's directory. It FAILED CLOSED
   in both configurations and ran no tests -- the correct outcome for a missing instrument, and the
   mirror image of P17.6's fail-open probe. Preserved with WHY_INVALID.md.
4  CLANG-FORMAT FAILED on HllcFlux.hpp (criterion E5) because of over-long comment lines. Fixed;
   proven formatting-only by an identical token multiset and an EMPTY diff of every measured value.
5  TWO INTEGRITY PROBES OF MINE MISREPORTED: the control-marker scan collided with P17.5's own
   '// V<id>' test labels (8 false hits), and the "no HLLC in src/" probe counted src/CMakeLists.txt,
   which legitimately names the source file. Both narrowed, with the reason printed.
6  MY AUDIT PROBE looked for `soundSpeed` when the API is `speedOfSound`, printing a blank that reads
   as "the project has no sound speed". No consequence -- the real API was read from the header -- but
   it is the same fail-open shape, so it is logged in audit.md section 6 rather than fixed silently.
```

No threshold was weakened, no criterion was changed because a test failed, and every initial failure is
preserved.

## 9. Findings recorded, none of them P17.7's to fix

```text
NEW    DAVIS WAVE SPEEDS ARE NOT SIGNAL-BOUNDING (8 of 14). Anyone adding a second Riemann solver, or
       reading §9 literally, would reintroduce this. It is now permanently guarded by the W4 control.
NEW    THE CONTROL-MARKER CONVENTION COLLIDES ACROSS PHASES. P17.5 uses '// V<id>' as test labels and
       P17.7 used it for plants. Future phases should use a phase-qualified marker ('// P17_8_V3').
NEW    Vector3's OPERATORS ARE HOST-ONLY. Any future device code doing vector arithmetic must either
       duplicate them (as HllcFlux.hpp does, bit-for-bit including cfd::dot's zero-z refinement) or
       Vector3 must be annotated. Duplication was chosen here to avoid touching a P12 core type.
CLOSED the IdealGasEOSTest "PREDICTED 21" constant, obsolete since P17.3 and carried for FIVE phases.
       This phase's new script predicts 30, and 30 was measured in both configurations.
CARRIED debug_gui predicted counts (2670/2608), stale since P17.4; that stage is deferred to P17.11.
CARRIED P15's production thermal path transports cp*T; SensibleEnthalpy is unreachable from a case;
       the two conduction boundary treatments disagree; P13 never ran clang-format (22 files fail).
CARRIED the low-Mach family is bounded on both sides by maxIterations = 3000 (P17.6).
```

## 10. Not claimed

```text
NOT CLAIMED   a POSITIVITY-PRESERVING TIME UPDATE. P17.7 guarantees admissible left/right states and
              admissible STAR states; the full update depends on CFL and reconstruction and belongs
              partly to P17.8. §14 demands this distinction and the TODO uses [~] accordingly.
NOT CLAIMED   that Sod passes. HLLC sits 0.61% / 6.1% / 29.6% (mass / x-momentum / energy) from the
              exact Godunov flux there. That is the SCHEME, not a defect, and no tight tolerance is
              derivable -- so it is REPORTED and the gate rests on the derivable parts instead.
              §18 forbids the claim and P17.9 owns full shock validation. There is no PDE solve here.
NOT CLAIMED   a GPU compressible SOLVER. G8 asks for equivalent numerical SOLVES; this is a flux
              function. G8 is satisfied FOR THE FLUX LAYER and remains open for the solver. P17.10 is
              untouched.
NOT CLAIMED   PERFORMANCE. No timing, no speed-up. A 17-element launch would say nothing about
              throughput, and G8 forbids a performance claim without equivalent solves.
NOT CLAIMED   higher-order reconstruction. First-order piecewise-constant only; the INTERFACE for
              MUSCL/WENO is defined and hllcFlux() need not change to accept them.
NOT CLAIMED   a 3D MESH. The FLUX is verified in full 3D (Vector2 is an alias of Vector3, so all three
              velocity components and an n_z normal are real), but there is no 3D mesh to drive it.
NOT CLAIMED   boundary conditions. reconstructFirstOrderBoundary() takes the exterior state FROM THE
              CALLER; supplying it is P17.8's work.
NOT CLAIMED   universal signal bounding. Toro PVRS is measured to bound in all 14 reference cases,
              spanning pressure ratios to 1e5, density ratios to 1e6, Mach ~2.4 and a near-sonic
              point. It is an ESTIMATE; no proof of universal bounding is offered.
```

## 11. Evidence index

```text
audit.md                     the pre-implementation audit -- FROZEN bd2f77dc...
acceptance_gate.md           the criteria -- FROZEN 1f4ccd8a..., verified twice
coverage_map.md              criterion -> test -> asserted?
implementation.md            states, physical flux, normal convention, reconstruction interface
wave_speeds.md               the estimator decision, the contact speed, the non-degeneracy proof
verification.md              every measured result, the controls, the discontinuity characterisation
cpu_gpu_equivalence.md       one shared source, bitwise, and the FMA experiment
regression.md                the mandated run and the integrity findings
tools/exact_riemann.py       THE INDEPENDENT REFERENCE -- FROZEN cddf94e5..., mpmath 60 digits,
                             contains no CFDApp code (G6(f) forbids using CFDApp output)
tools/gen_reference_header.py  IMPORTS the frozen script to emit the C++ table -- never edits it
tools/{audit,freeze,controls,fma_experiment,fingerprint,integrity,postformat_check}.sh
tools/{p17_7_regress,build_core}.sh   tools/staleness.py
logs/00_audit.log 01_reference.log 02_freeze.log 03_hllc_measured.log 04_gpu_attempt1.log
logs/05_fma_experiment.log 06_controls.log 07_fingerprint.log 08_postformat.log 20_integrity.log
logs/50_p17_7_regress_mandated.log
logs/PRESERVED_invalid_missing_staleness_tool/  the failed regression attempt + WHY_INVALID.md
logs/HllcFlux.hpp.preformat                     so the clang-format claim is checkable
```

## 12. Recommended next step — ONE, and NOT started

```text
P17.8 -- the density-based compressible solver.

WHY IT IS THE RIGHT ONE: P17.7 leaves a flux layer that is verified in isolation and wired to nothing.
Its value is zero until something integrates it, and every remaining P17 phase depends on that:
P17.9's shock validation needs a solve to validate, and P17.10's equivalence needs a solve to compare.
The interfaces P17.8 needs are already defined and tested -- ReconstructedFaceState for reconstruction,
an exterior state for boundaries, and exact antisymmetry so one flux evaluation updates both cells.
It is also the next phase in the frozen roadmap order, so nothing is skipped.

CARRY FORWARD INTO IT: the positivity-preserving time update (§14's third level, explicitly not
claimed here) and the CFL condition, both of which are P17.8's to establish.

NOT AUTHORIZED, NOT STARTED. Nothing was committed or pushed in P17.7.
```
