# P17.6 — Low-Mach Validation: summary

```text
VERDICT   the compressible solution DOES converge to the incompressible one as Ma -> 0, at FIRST
          order in Mach, demonstrated across FOUR levels spanning 8x -- not asserted from one case.
          All 10 gate groups PASS or are reported NOT PRESENT. 0 test failures in either
          configuration. Every frozen count prediction matched exactly.
```

---

## 0. The central finding of the audit, which set the whole phase

The pre-existing low-Mach test — the one the frozen G5 criterion rests on — **runs no compressible
solve at all**. It takes an incompressible SIMPLE solution and reinterprets it through the
compressible foundation post hoc, so its "agreement with the incompressible limit" is true by
construction rather than measured.

This was **disclosed**, not concealed: `tests/integration/compressible/CMakeLists.txt` says in its own
header that it is *"a post-hoc consistency demonstration built on the existing incompressible solver,
NOT a separately-iterated compressible pressure-correction solve"*. The gap is that **no later phase
ever built the real thing**, and the frozen criterion came to rest on that file.

```text
CONSEQUENCE, and it is the reason this phase exists:
  section 20 of the authorization says "a compressible case silently falling back to incompressible
  SIMPLE is not valid low-Mach evidence". The existing test is not a fallback by accident -- it never
  had a compressible path to fall back FROM. So P17.6 could not validate by re-running it.
  It is PRESERVED VERBATIM and re-run (9/9 pass, hash matches the freeze) and a SEPARATE executable
  was added that drives the real CompressibleSIMPLE.
```

## 1. What was built

```text
tests/integration/compressible/test_low_mach_family.cpp   NEW, 15 TESTs, 604 non-empty lines
tests/integration/compressible/CMakeLists.txt             +19 -0, a SECOND add_executable
production source                                         ZERO changes -- 17 files verified UNTOUCHED
```

A separate executable deliberately, so the P3-PHYS-006 test stays byte-for-byte unchanged as the
frozen G5 criterion requires. Four levels, `U0 = {1, 0.5, 0.25, 0.125}`, each a genuine
`CompressibleSIMPLE` solve against its **own matched** incompressible `SIMPLE` reference on the same
mesh, viscosity and boundary conditions.

## 2. THE MEASURED RESULT

```text
U0     inletMa     machMax     drho/rho    relL2(|U|)  relL2(p')   keRelDiff   itComp  itInc
1      0.0028802   0.0041552   1.1192e-04  7.9159e-05  7.1600e-04  1.6233e-04  1959    2031
0.5    0.0014401   0.0020790   5.4856e-05  3.1193e-05  7.6267e-05  7.8081e-05  2261    2450
0.25   0.00072005  0.0010397   2.7130e-05  1.6984e-05  7.3820e-05  3.8465e-05  2403    2626
0.125  0.00036003  0.00051994  1.3482e-05  1.2574e-05  8.1806e-05  1.9409e-05  2439    2680

observed exponents, error ~ C Ma^n
  drho/rho    1.0178   <- theory for this case is EXACTLY 1
  keRelDiff   1.0216
  relL2(p')   1.0432   (absolute measure; the relative one is floor-limited)
  fluxRelDiff 0.9706
  relL2(|U|)  0.8848

maxEosError              EXACTLY 0 at every cell, every level
T recovery from rhoE     EXACTLY 0 at every cell, every level
determinism, repeat run  BITWISE identical -- velocity, pressure, density, mass flux, residuals
```

**Why 1 and not 2.** For this channel the pressure drop is viscous-dominated,
`dp ~ 12 mu U L / H^2`, not dynamic (`rho U^2`), so `drho/rho = dp/p ~ Ma^1`. A quadratic expectation
would have been wrong. The exponent was declared in advance in `case_family.md` §4 and only the
density exponent is gated tightly, because it is the one theory pins — §14's instruction not to invent
an expected exponent unless theory justifies one.

## 3. Conservation

```text
mass     |sum| 1.78e-08 -> 1.80e-09 against the solver's own convergence contract
         (continuityTolerance x |inletFlow|), margins 66.2x -> 10.2x
momentum net flux + pressure force implies a wall shear within 7.1% -> 5.6% of the analytical
         Poiseuille value, MONOTONE toward 1 as Re falls -- the entrance-effect explanation made
         testable rather than asserted
energy   the KINETIC contribution bounded against ITSELF: keRelDiff 1.62e-04 -> 1.94e-05.
         ke/e is 4.8e-06 -> 7.6e-08, i.e. six to eight orders below the internal energy
```

**The total-energy balance is reported and NOT gated below S/N 1**, with its ratio to an independently
computed kinetic-energy gain: 0.979, 0.917, 0.686, −0.220 at S/N 46.97, 12.10, 3.18, 0.82. It inverts
exactly where the signal falls below the noise. The noise floor is quantified: `|massImbalance| x h`
≈ 5.4e-4 against a physical signal of 4.4e-4 at the lowest level — the noise *exceeds* the signal. The
case is also isothermal, so the isothermal constraint is an unmodelled heat sink, and P17.4 supplies
no stress tensor for viscous work. Gating it would have been gating noise.

## 4. Gate

```text
15 UNTOUCHED / 0 TOUCHED by SHA-256      clang-format exit 0
new family                 15 / 15      existing low-Mach   9 / 9   (hash matches the freeze)
MMS compressible            1 / 1       Poiseuille         13 / 13
cavity Ghia                 3 / 3       P14 transport      68 / 68
P15 thermal               161 / 161     compressible      225 / 225  = 210 + P17.6's 15
SIMPLE fingerprint        BITWISE IDENTICAL, 0-byte diff
X1 GPU compressible       all 10 probes 0 -> reported NOT PRESENT, never PASS
```

## 5. Controls — every one behaved as predicted

```text
N1  reference = the compressible solution itself      DETECTED, 3/15
N2  rho_ref = 1.0 instead of the EOS-derived value    DETECTED, 4/15
N3  machMax = the requested, not achieved, Mach       DETECTED, 1/15
N5  energy criterion normalised by the rhoE scale     PASSED, 0 failures -- AS THE TRAP PREDICTS
N4  drop the 1.25 x inletMach clause                  NOT planted: a deleted assertion always passes,
                                                      so it is degenerate. Replaced by MEASURING the
                                                      margin, 1.4427 against the 1.25 bar.
restore  SHA-256 verified after every plant, 0 markers left, suite back to 15/15
```

N5 is inverted on purpose: the frozen G5 clause claims a criterion on `rhoE` "can be satisfied while
the kinetic part is entirely wrong", and the only way to show that is real is to plant the wrong
normalisation and watch the suite go green. It did.

## 6. Regression

```text
                registered         executed          failed   runtime
Release+CUDA    2657 = PRED 2657   2595 = PRED 2595  0        42.8 min
CPU-only        2455 = PRED 2455   2395 = PRED 2395  0        30.8 min
B8 fingerprint  cf4d53a2... before == after
```

## 7. What I got wrong in this phase, and what it cost

```text
1  THE C2 MASS BOUND was derived at ROUND-OFF (8 eps x N_faces x flow) when the quantity is bounded
   by the SOLVER'S CONVERGENCE CONTRACT. It failed by 15412x - 19057x. A converged SIMPLE solve stops
   at its continuity tolerance, not at machine precision. AMENDMENT_thresholds.md section 1.
2  CRITERION A3 HAD NO ASSERTION and would have failed if it had. relL2(p') goes
   7.16e-4 -> 7.63e-5 -> 7.38e-5 -> 8.18e-5 -- it TICKS UP, because it is floor-limited at
   5.2083e-5. The frozen wording "strictly decreases" was arithmetically impossible. Retargeted onto
   the absolute measure plus a dedicated floor-companion test. Found by the coverage map, which is
   why that map now carries an "asserted?" column. AMENDMENT_thresholds.md section 2.
3  THE FIRST CONTROLS RUN planted NOTHING for N1 -- the gate's clang-format had reformatted the
   anchored line -- and was on course to report a spurious VACUOUS, a false accusation against a
   sound criterion. Stopped; anchor corrected; a verify_planted() guard now aborts before every
   control if the marker is absent.
4  FOUR DEFECTS IN THE REGRESSION INSTRUMENT, three of them FAIL-OPEN, found by reading it against
   the logs rather than by running it: a wrong hardcoded gate hash from another phase; a B6 probe
   reading a filename this script never writes (so "0 nvcc invocations" was printed without reading
   any log -- debt since P17.5, now closed); a post-freeze-addition grep still naming P17.4's tests;
   and a three-phase-stale fingerprint label. AMENDMENT_regression_probes.md.
5  THE PER-DIRECTORY STALENESS HEURISTIC failed the mandated run closed, correctly stopping it but
   for the wrong reason. Replaced by a per-target check on CMake's own object lists -- STRICTLY
   STRONGER, since the old glob never checked cross-directory sources at all. AMENDMENT_regression_
   probes.md section 5; failure preserved with WHY_INVALID.md.
6  I MODIFIED A FROZEN DOCUMENT. audit.md was edited 174 seconds after it was frozen, bundled into a
   command that was also wiring up the CMakeLists. Found by this phase's own integrity check.
   Restored, and proved to be the ONLY post-freeze change by reverse-applying the edit to the frozen
   hash bit for bit. The gate itself was unaffected and verified twice.
   AMENDMENT_audit_post_freeze.md.
7  I MIS-STATED MY OWN EVIDENCE in a draft of validation.md, claiming the inlet-to-outlet pressure
   drop agreed monotonically to better than 0.1%. dropRelDiff actually goes
   7.30e-4 -> 4.67e-5 -> 6.15e-5 -> 7.53e-5 -- it RISES after level 2, and I had quoted the third
   level as if it were the last. Corrected against logs/01_dryrun.log. It turned out to STRENGTHEN
   finding 2: two independently defined pressure measures show the same non-monotone signature, which
   confirms the convergence floor is real rather than an artefact of one metric's definition. No gate
   criterion touches the pressure drop, so there was no gate impact.
```

No threshold was weakened, no iteration limit raised, no test deleted, and no criterion changed
because a test failed. Every initial failure is preserved.

## 8. Findings recorded, none of them P17.6's to fix

```text
NEW   THE FROZEN-DOCUMENT CHECK HAS PHASE-LENGTH DETECTION LATENCY. freeze.sh writes the hashes and
      nothing re-reads them until the integrity check at the very END. The live gate-hash comparison
      added this phase is the right shape but covers only acceptance_gate.md. It should cover every
      frozen document and run at the START of every scripted step. Not done here: it changes the
      shared harness P17.1-P17.5 also used, which is outside this phase's scope.
NEW   THE LOW-MACH FAMILY IS BOUNDED ON BOTH SIDES BY maxIterations = 3000. The INCOMPRESSIBLE
      reference cannot converge at U0 = 2 or 4 at all, and iterations climb 1959 -> 2439 (comp) and
      2031 -> 2680 (inc) as Mach falls. Classic low-Mach conditioning of a pressure-based solver.
      Preconditioning is a later numerical phase's work; section 21 places it there.
NEW   THE MASS-IMBALANCE MARGIN SHRINKS with the tolerance, 66x -> 10x. The imbalance does not fall
      as fast as the tolerance tightens -- the same conditioning signature as the iteration counts.
CARRIED  IdealGasEOSTest "PREDICTED 21" is obsolete for the FOURTH phase; the real count is 30. A
         stale CONSTANT on a LIVE detector -- all 30 pass by name. Fix the constant, keep the probe.
CARRIED  debug_gui predicted counts (2670/2608) stale since P17.4; that stage is deferred to P17.11.
CARRIED  P15's production thermal path transports cp*T, which P15's own Enthalpy.hpp calls wrong;
         26-103% error for cp(T).
CARRIED  SensibleEnthalpy is unreachable from any case file.
CARRIED  discretization::diffusion and P15's thermal assembler disagree on prescribed-flux boundaries.
CARRIED  P13 never ran clang-format; 22 files still fail it. P17.6's own file is clean.
CLOSED   the B6 stale-filename fail-open probe, carried since P17.4/P17.5 -- fixed this phase.
```

## 9. Not claimed

```text
NOT CLAIMED   a second flow TOPOLOGY as a Mach family. The cavity is a CLOSED domain with no
              through-flow and therefore no pressure drop to drive a density variation, so it cannot
              carry a low-Mach limit study. Run unchanged as a preservation case; reason in
              case_family.md section 5.
NOT CLAIMED   Mach levels above the existing test's anchor -- blocked by the inherited iteration
              limit, which section 22 forbids raising.
NOT CLAIMED   a CLOSED total-energy balance. Impossible on an isothermal solve; quantified.
NOT CLAIMED   CPU/GPU low-Mach equivalence. No GPU compressible path exists -- 10 probes return 0,
              the single hit being a comment about the INCOMPRESSIBLE path. Reported NOT PRESENT,
              never PASS. P17.10's qualification is not claimed and the gate marks G8 DEFERRED.
NOT CLAIMED   the viscous-work flux (inherited from P17.5: P17.4 builds no stress tensor).
NOT CLAIMED   3D. The compressible path is 2D-only.
NOT CLAIMED   a formal order-of-accuracy result. Exponents are reported; only the density exponent,
              which theory pins, is gated tightly.
NOT CLAIMED   temperature as a SOLVED field. CompressibleSIMPLE takes T as an input and this case is
              isothermal; the meaningful check is exact recovery from the conserved variables, and
              that is what is measured.
```

## 10. Evidence index

```text
audit.md                      the pre-implementation audit -- FROZEN 7846fc84...
acceptance_gate.md            the criteria -- FROZEN f275c4ac..., verified twice
coverage_map.md               criterion -> test -> asserted? -- found the A3 gap
case_family.md                the family, the reference mapping and the expected limits
validation.md                 the measured behaviour, all ten authorization sections
regression.md                 the mandated run
AMENDMENT_thresholds.md               the C2 bound and the A3 retargeting
AMENDMENT_regression_probes.md        five instrument defects, four of them fail-open
AMENDMENT_audit_post_freeze.md        my own frozen-document violation
logs/01_dryrun.log            the unchanged tree, before the freeze
logs/02_freeze.log            the freeze
logs/03_gate.log              the gate
logs/04_fingerprint.log       the SIMPLE fingerprint
logs/05_controls.log          Group N
logs/06_staleness_probe.log   the replacement instrument's non-vacuity proof
logs/20_integrity.log         repository integrity
logs/50_p17_6_regress_mandated.log            the passing regression
logs/PRESERVED_invalid_perdir_staleness/      the failing attempt, with WHY_INVALID.md
logs/PRESERVED_gate_attempt1/                 the failing gate attempt
tools/                        audit.sh dryrun.cpp run_probe.sh freeze.sh run_gate.sh controls.sh
                              fingerprint.sh staleness.py p17_6_regress.sh integrity.sh
```

## 11. Recommended next step — ONE, and NOT started

```text
P17.7 -- HLLC / Riemann flux.

WHY IT IS THE RIGHT ONE: P17.6 has now measured what the pressure-based path can do, and its limit is
visible in the data. Iterations climb monotonically as Mach falls and the family is bounded on BOTH
sides by maxIterations = 3000 -- the incompressible reference cannot converge at U0 = 2 or 4 at all.
The pressure-based formulation is at the edge of its useful range in both directions, so the next real
capability is the density-based path, and HLLC is its first component. It is also the next phase in
the frozen roadmap order, so nothing is skipped.

NOT AUTHORIZED, NOT STARTED. Nothing was committed or pushed in P17.6.
```
