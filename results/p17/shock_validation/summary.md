# P17.9 — Shock Validation — SUMMARY

```text
STATUS   P17.9 COMPLETE
GATE     results/p17/shock_validation/acceptance_gate.md
         sha256 b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40
         FROZEN before any benchmark ran, and UNMOVED since -- including through three
         authorised amendments, which live in amendments/ and edit it not at all.
SCOPE    1D problems on 2D meshes, first-order HLLC, Euler only. G7 is claimed; G8 is not
         applicable (no GPU density-based solver exists); G10 is P17.11's.
```

---

## 1. What was proven

That CFDApp's density-based HLLC solver does not merely produce visually plausible discontinuities,
but quantitatively satisfies Riemann-wave structure, wave location, analytical jump relations,
positivity and global conservation — measured against a **60-digit reference computed outside
CFDApp**, cross-checked against P17.7's independent derivation, and self-checked against eight
defining physical properties that neither implementation can satisfy by accident.

```text
NORMAL SHOCK, M1 = 2      every ratio within 1.1e-04 of the analytical relation
  rho2/rho1  2.6667102673 vs 2.6670020510    rel 1.0941e-04
  p2/p1      4.4995544351 vs 4.4998113700    rel 5.7099e-05
  T2/T1      1.6873053253 vs 1.6872170639    rel 5.2312e-05
  u2/u1      0.3749263486 vs 0.3749528425    rel 7.0659e-05
  M2         0.5772701741 vs 0.5773260664    rel 9.6812e-05
  Rankine-Hugoniot residuals  mass 1.8006e-04  momentum 1.1869e-04  enthalpy 4.0216e-05
  shock drift over 1389 steps 0.0178 dx      retries 0   rejected 0

SOD, the three structures recovered and LOCATED
  shock            0.82 dx,  and err/dx flat at 0.82 / 0.85 / 0.86 under refinement -> genuinely O(dx)
  contact          1.15 dx
  rarefaction tail 2.71 dx
  rarefaction head diffusive, err halves under refinement (see amendment A2)
  L1 rates         rho 0.66   u 0.83   p 0.75  -- inside the band the gate predicted IN ADVANCE

STATIONARY CONTACT  position, pressure variation and velocity variation all EXACTLY 0;
                    0 smeared cells -- HLLC's contact wave, exactly preserved
MOVING CONTACT Ma 3.4  position 0.141 dx, p variation 4.09e-14, u variation 3.00e-15
COMPRESSION         p* rel 1.77e-03, rho* rel 3.98e-03, both RISE, u* exactly 0
EXPANSION           p* rel 3.97e-03, rho* rel 2.91e-03, both FALL, min rho 0.688, min p 0.612
SUPERSONIC ADVECTION  centroid position error 2.0e-13; p and u variation ~1e-14
CONSERVATION        mass 2.313e-15 / 1.511e-14, energy 6.305e-15 / 4.288e-15 (bounds 3.6e-13 / 7.1e-13)
DETERMINISM         dt, time, residuals, retries, full state, AND the detected shock and contact
                    positions -- ALL BITWISE identical across repeated runs
CFL SENSITIVITY     0.4 vs 0.2: shock err 4.09e-03 vs 4.35e-03, L1 1.278e-02 vs 1.304e-02
POSITIVITY          rho > 0, p > 0, T > 0 everywhere; 0 retries, 0 rejections, 0 non-finite
```

## 2. The three amendments

All three were authorised by the user after being reported as blocking. Each preserves the original
frozen criterion, the original failing result, and an independent justification.
`amendments/coverage_map.md` holds the table; the audit trail is below.

### A1 — GATE-STRENGTHENING. The one that mattered.

```text
PROBLEM   Group V was VACUOUS. HLL -- HLLC with the contact wave DELETED -- passed the ENTIRE
          shock-validation gate (logs/07_controls.log). G7's own text says that means the gate was
          not measuring shock capturing.
WHY       a contact is linearly degenerate: p and u are continuous across it, so no criterion
          written on them can see it. On Sod it is already smeared by both schemes (16 cells vs 18),
          and the star states are set by the shock and the fan, not by the contact's sharpness.
FIX       a STATIONARY contact must be preserved with EXACTLY 0 smeared cells. Not a tolerance --
          it is an exact steady solution of the Euler equations, and P17.7 proved the flux carries
          exactly zero mass and energy across one, bitwise.
MEASURED  HLLC 0 cells    HLL 21 cells    Rusanov 21 cells
RESULT    both negative controls now FAIL, on that assertion, for the intended reason.
```

```text
AND A DISTINCTION WORTH KEEPING. P17.7 ALREADY had a contact-deletion non-vacuity test --
HllcContactTest.T4 -- but at the FLUX layer: it calls hllFluxForControlOnly directly, so planting
HLL inside the SOLVER leaves it passing. The flux layer was protected; the solver layer was not.
A1 closes the second gap and does not duplicate the first.
```

### A2 — CORRECTS A DIMENSIONAL ERROR

```text
PROBLEM   X3 bounded the rarefaction-head position at 2 dx. Measured 11.78 dx, and WORSE on finer
          meshes -- 15.44 and 19.89 -- while the error itself HALVED.
WHY       the head is a DERIVATIVE discontinuity: rho is continuous, only d(rho)/dx jumps. A
          monotone first-order scheme smooths it over the diffusion length sqrt(2 D t) with
          D ~ s dx/2, so a position detected at a fixed fraction of AMPLITUDE moves like sqrt(dx),
          not like dx. A C*dx bound is unsatisfiable as dx -> 0, whatever C is.
FIX       gate the derived scaling instead, with NO free constant: err must fall; err/sqrt(dx) must
          not grow; err/dx must grow.
REJECTED  a rigorous absolute bound using D <= s_max dx/2 was computed and DISCARDED -- it fails at
          nx = 400 by 1.1 %, so imposing it would have been tuning. That attempt is recorded.
CONTROL   Rusanov, being more diffusive, must give a LARGER head error. Measured 6.3477e-02 against
          HLLC's 5.8921e-02. The derivation predicted the direction and the prediction held.
```

### A3 — REMOVES MEASUREMENT CONTAMINATION

```text
PROBLEM   S6 required |rho*_R - exact| to fall under refinement. It did not: 3.59e-03 -> 3.75e-03
          -> 3.42e-03.
PROOF     p*, u* and rho*_R come from the SAME window, the SAME policy, the SAME run:
            p*      5.1691e-04 -> 2.0220e-04 -> 7.7698e-05   monotone, rate 1.367
            rho*_R  2.9073e-03 -> 3.2447e-03 -> 3.0927e-03   does not converge, rate -0.045
          A contact carries a density jump but NO pressure or velocity jump, so only the density
          measurement can be corrupted by its tail. The contamination is predicted by the physics
          and then confirmed by WHICH quantity fails -- not inferred from the failure.
FIX       measure the flattest 9-cell sub-window between the two waves, which isolates the plateau
          from both tails and remains fully sensitive to a genuinely wrong plateau value.
MEASURED  isolated error is 14x / 948x / 227x cleaner than the contaminated one.
LIMIT     the isolated error is still not monotone (2.54e-04 -> 3.95e-06 -> 1.51e-05); A3 therefore
          requires overall improvement plus a 10x isolation factor, and this weaker form is stated
          rather than buried.
```

## 3. Negative controls — the evidence that the gate means something

```text
logs/11_controls_amended.log
  V1  Rusanov / local Lax-Friedrichs, NO Riemann solver
        contact case      FAIL  (A1: 21 smeared cells)
        Sod plateau       FAIL
        conservation      PASS  -- still conservative, as it must be
  V2  HLL, HLLC with the CONTACT WAVE deleted
        contact case      FAIL  (A1: 21 smeared cells)
        normal shock      PASS  -- the shock is still right, as expected
        Sod plateau       PASS  -- recorded: no Sod criterion can separate HLLC from HLL
  CONTROLS WITH AN UNEXPECTED RESULT: 0

detector controls, run on every invocation of the suite
  V3  the profile shifted 3 dx  -> the position criteria break (2.92 dx measured)
  V4  a jumpless monotone ramp  -> the detector reports NO SHOCK rather than inventing one
```

```text
THE FROZEN V2 ROW NAMED THE WRONG CRITERIA, and that is recorded rather than accommodated. It
predicted W3, X2 and S3 would break under HLL. Measured, they do not and cannot: X2 1.28 dx against
a 2 dx limit, W3 1.75e-03 against 5e-02, S3 9.03e-03 against 1.61e-02. Only the stationary contact
discriminates. No Sod tolerance was tightened to force a detection.
```

## 4. Regression

```text
                          release_cuda          cpu_only
registered                2745  PRED 2745       2539  PRED 2539
exclusions                  13  PRED 13           13  PRED 13
EXECUTED                  2683  PRED 2683       2479  PRED 2479
passed                    100%                  100%
FAILED                       0  PRED 0             0  PRED 0
Not Run                      0                     0
staleness (fail closed)   49 bins, 0 stale      48 bins, 0 stale
runtime                   2239 s                1699 s
NEW FAILURES: none.   KNOWN FAILURES: none.
P17_CODE_FINGERPRINT unmoved: 24c617f33336cca09c83c3065556ead7bdefbfc8bf198b82899989057f20865f
```

The sharpest statement that P17.9 changed no production code: **`libcfdcore.a` came out of both
configurations with the same sha256 P17.8 recorded** — `188e5039…` and `1305a82a…`.

## 5. Gate verdict

```text
G1  baseline preservation          PASS   no production source touched; libcfdcore.a byte-identical
G3  variable-density conservation  PASS   mass 2.313e-15 closed box
G4  total-energy conservation      PASS   energy 6.305e-15 closed box
G6  HLLC correctness in solver     PASS   inherited from P17.7/P17.8, re-run: 39 Hllc rows
G7  SHOCK VALIDATION               PASS   this phase -- wave structure, positions, jump relations,
                                          Rankine-Hugoniot, conservation, positivity, and a
                                          non-vacuous control set
G9  deterministic repeatability    PASS   bitwise, including the detected wave positions
G8  CPU/GPU solver equivalence     NOT APPLICABLE -- no GPU density-based solver exists
G10 full qualification             NOT CLAIMED -- P17.11's
```

## 6. What is NOT claimed

```text
2D or 3D shock structure     every case here is 1D on a 2D mesh, as G7's constraint note requires
                             every claim to say.
viscous shock structure      Euler only. The shock layer here is NUMERICAL, not physical.
higher-order accuracy        first order only. §34 requires the first-order baseline to qualify
                             first, and it does; no high-order scheme was used to mask anything.
formal order of accuracy     the L1 rates (0.66 / 0.83 / 0.75) are REPORTED. There is no Richardson
                             extrapolation and no GCI, and a discontinuous solution has no single
                             first-order rate to assert.
subsonic open boundaries     NOT IMPLEMENTED in P17.8. Every case is built to need only walls (with
                             the wave never reaching them) or genuinely supersonic in/outflow.
Debug+GUI, sanitizers        NOT RUN -- deferred to P17.11 by the standing decision every P17 phase
                             has recorded. Not skipped for runtime.
an absolute bound on the     A2 deliberately imposes none; the derivation supports the scaling and
rarefaction-head position    not a constant, and the rejected attempt is recorded.
```

## 7. Recorded, not fixed

```text
* The frozen V2 row named criteria that cannot detect a deleted contact wave (§3 above).
* The frozen reference's verify_defining_properties() rejects SYMMETRIC Riemann problems, because
  it asserts every one has a contact with a density jump -- false when rho*_L == rho*_R exactly.
  Worked around in the generator with targeted Rankine-Hugoniot checks; the frozen file is untouched.
* The regression harness's REPORTING grep counts 17 shock rows where 13 ran, because a bare
  `Contact` also matches P17.7's HllcContactTest. Enumerated and corrected in regression.md §3; the
  script was not edited mid-run.
* 22 files fail tree-wide clang-format. All pre-existing, none of them P17.9's. Standing debt first
  recorded in P17.1.
```
