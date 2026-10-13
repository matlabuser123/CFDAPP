# P17.0 — verification of the audit itself

Every conclusion in this audit, cross-checked against repository evidence before P17.0
is called complete. This file exists because an audit is itself a measurement, and an
unverified measurement is not evidence.

---

## 1. Each IMPLEMENTED claim, six ways

The authorization requires that an `IMPLEMENTED` claim confirm: source exists, runtime
path exists, test exists, test actually runs, test passes, evidence preserved.

```text
claim                source  runtime path              test  runs  passes  evidence
-------------------------------------------------------------------------------------
Variable density     YES     ProjectRunner.cpp:799     YES   26    YES     §1.1
                             CompressibleSIMPLE:455
Ideal-gas EOS        YES     via ThermodynamicProps    YES   28    YES     §1.1
                             ProjectRunner, SIMPLE
Low-Mach validation  YES     the tests ARE the         YES    8    YES     §1.1
                             production path exercise
CPU compressible     YES     ProjectRunner.cpp:756     YES   129   YES     §1.1
  support                    both dispatch modes
```

`passes` is a property of a specific executed run, so it is asserted only against that
run's log, in §1.1. It was written as `NOT VERIFIED` here until the run landed, rather
than filled in with an anticipated `YES`.

"runs" is `ctest -N` registration in `build/qual-final`, measured, not assumed —
G10 learned that `gtest_discover_tests` populates the list at build time, so a stale
tree silently omits new rows.

```text
registered, measured at the P17.0 freeze
  IdealGasEOSTest                    21    (12 pre-existing + 9 from P17.2)
  ThermodynamicPropertiesTest         7
  CompressibleMassFluxTest           12
  CompressibleContinuityTest          6
  CompressibleSimpleTest              7
  CompressiblePressureCorrection      6
  CompressibleRelaxedMomentum         5
  LowMachRegressionTest               7
  CompressibleSimpleMMS               1
  CompressibleCaseTest               15
  CompressibleProductionCase          8
  CompressibleCoupledProductionCase   6
  aggregate Compressible|LowMach|IdealGas|Thermodynamic   129
```

### 1.1 "passes" — the evidence, and its limits

**VERIFIED 2026-09-26.** The baseline regression completed and every claim above is
established by it. `passes = YES` for all four IMPLEMENTED rows.

```text
configuration   executed   failed   IdealGasEOSTest rows Passed
-----------------------------------------------------------------
Release+CUDA      2497        0             21 / 21
CPU-only          2297        0             21 / 21
```

Both landed **exactly** on counts frozen before the run
(`baseline/baseline_gate.md` §3), and the arithmetic closes with nothing left over:
`2559 - 13 - 49 = 2497` and `2357 - 13 - 47 = 2297`. The 129 compressible tests, the
26 variable-density tests, the 28 EOS tests and the 8 low-Mach/MMS tests are all inside
those totals and all passed.

`passes` is established by the P17.0 baseline regression, not by an assumption and not
by reusing an earlier run:

```text
Release+CUDA   results/p17/baseline/logs/10_release_cuda_ctest.log
CPU-only       results/p17/baseline/logs/10_cpu_only_ctest.log
driver         results/p17/baseline/logs/10_baseline_regress_mandated.log
verdict        results/p17/baseline/cpu_regression.md, cuda_regression.md
```

Two earlier harness attempts are preserved as
`logs/PRESERVED_invalid_binary_discovery/` and `logs/PRESERVED_invalid_system_binary/`
with their own `WHY_INVALID.md`. Both failed closed without executing a test; neither
is evidence of anything about the tree, and neither was deleted.

**Limit, stated rather than glossed:** `passes` is established in the WSL2
configurations named in `baseline/toolchain.md`. Per `CLAUDE.md` §4 it does not
establish native Windows behaviour, which remains unqualified.

## 2. Each PARTIAL claim — exactly what is present, exactly what is missing

### Compressible continuity — PARTIAL

```text
PRESENT   the STEADY constraint div(rho U) = 0, enforced through the
          pressure-correction equation, whose header states it IS the discretized
          compressible continuity constraint solved for pressure
          one authoritative face mass flux, m_dot_f = rho_f (U_f . n_f) A_f
          boundary mass fluxes at real EOS boundary density (P12-COMP-001)
          global balance reported as |globalNetFlux| and gated in the low-Mach suite
          a transient residual DIAGNOSTIC, evaluateCompressibleContinuity, with 6
          unit tests
MISSING   any production route to the transient form. evaluateCompressibleContinuity
          has NO production caller -- verified by enumerating every caller:
            src/compressible/CompressibleContinuity.cpp   (its own definition)
            tests/unit/compressible/test_compressible_continuity.cpp   9 references
            tests/integration/compressible/test_low_mach_regression.cpp   1
            include/cfd/compressible/CompressibleContinuity.hpp   (declaration)
          CompressibleSIMPLE.cpp calls the steady physics::evaluateContinuity at
          line 478 instead.
MISSING   time accuracy. pseudoTimeStep is a stabilization parameter
          (CompressibleSIMPLE.hpp:27) and densityOld == densityNew at the call site.
```

### Compressible momentum — PARTIAL

```text
PRESENT   d(rho U)/dt with rho_old u_old and rho_new u_new kept separate, reducing
          exactly to the incompressible formula at constant density (tested)
          div(rho U x U) flux-based on the compressible mass flux -- conservative
          -grad(p)
          production reachable via assembleRelaxedCompressibleMomentumComponent
          (CompressibleSIMPLE.cpp:265)
MISSING   the dilatational stress term. physics::assembleDiffusionContribution is
          the INCOMPRESSIBLE Laplacian mu lap U.
MISSING   higher-order convection -- upwind only, MMS order gate 1.0 [0.55, 1.3]
MISSING   3D -- requireTwoDimensional at CompressibleSIMPLE.cpp:147
NOT       assembleCompressibleMomentumComponent exists with 7 tests but has no
REACHABLE production caller: it calls .build() internally, so it has no relaxation
          hook. Library capability, not production capability.
```

## 3. Each MISSING claim — absence proved four independent ways

A `MISSING` claim is the easiest to get wrong, in both directions: a false negative
from different naming, or a false positive from a substring match. Both were tested
for.

### 3.1 Search coverage

```text
searched   include/ src/ cuda/ apps/ tests/ benchmarks/ cases/ tools/ scripts/
           results/          -- the evidence tree, for work done and later removed
           git log --all --diff-filter=D   -- for deleted files
           git branch -a, git tag          -- for work parked elsewhere
method     grep -rniI, case-insensitive, substring (not word-bounded), so a
           differently-cased or differently-affixed name still matches
```

### 3.2 The counts

```text
symbol                 src/test tree   results/   git-deleted   verdict
-----------------------------------------------------------------------------
HLLC                         0            8*          0         ABSENT
HLL                          0            -           0         ABSENT
Rusanov                      0            4*          0         ABSENT
LaxFriedrichs                0            -           0         ABSENT
Sod                          0            6*          0         ABSENT
shockTube / shock_tube       0            3*          0         ABSENT
shock                        0            -           0         ABSENT
normalShock                  0            -           0         ABSENT
supersonic / transonic       0            -           0         ABSENT
rarefaction / choked         0            -           0         ABSENT
nozzle                       0            -           0         ABSENT
conservedVariable            0            -           0         ABSENT
primitiveVariable            0            -           0         ABSENT
totalEnergy / TotalEnergy    0            5*          0         ABSENT
rhoE / rho_e                 0            5*          0         ABSENT
internalEnergy               0            4*          0         ABSENT
pressureWork                 0            -           0         ABSENT
viscousDissipation           0            -           0         ABSENT
viscousWork                  0            -           0         ABSENT
grad(div / dilatation        0            -           0         ABSENT
bulkViscosity / twoThirds    0            -           0         ABSENT
Stokes                       0            -           0         ABSENT

* every results/ hit is a P17 audit or gate DOCUMENT describing the absence --
  this file and its siblings, plus results/p17/compressible/audit/AUDIT.md and
  results/p17/compressible/eos/summary.md. Not one is an implementation.
  Only one branch exists (main); tags run to v0.2.0; no deleted file matches.
```

### 3.3 False positives caught — and why this matters

Three symbols returned non-zero and would have been misread as partial capability:

```text
Roe      14 hits   ALL substrings of "zeroes" / "zeroed" / "Zero":
                     DeviceBuffer.hpp "release() zeroes capacity_"
                     GpuLinearSolverCuda.cpp "restart() overwrites v_ ... then zeroes it"
                     LaminarModelTest.TurbulentViscosityIsZeroEverywhere
                   Not one is the Roe flux.
Euler   107 hits   ALL implicitEulerTimeDerivative (46), aleImplicitEulerTime-
                   Derivative (15), bare "Euler" in comments about implicit-Euler
                   TIME INTEGRATION (36), and ImplicitEuler* test names.
                   Never the Euler equations.
Riemann   1 hit    EquationOfState.hpp:51 -- a FORWARD-LOOKING COMMENT that P17.2
                   itself wrote: "the energy equation and the Riemann flux to come".
                   A comment about future work, not a flux.
```

A bare symbol census would have reported `Roe: 14, Euler: 107, Riemann: 1` and
concluded a Riemann/Euler-flux foundation exists. **It does not.** This is exactly the
error the authorization's "do not classify capability by symbol existence alone"
guards against, and it was live in this repository.

### 3.4 False negatives guarded against

```text
"dissipat" DOES appear under include/cfd/turbulence and src/turbulence. Checked
           individually: it is the turbulent dissipation rate epsilon of the
           k-epsilon model -- a turbulence closure quantity, NOT div(tau . U) in an
           energy balance. 0 hits under include/cfd/thermal, include/cfd/physics,
           src/thermal, src/physics.
"CFL"      DOES exist: include/cfd/solver/CFL.hpp, 53 hits. Checked: a DIAGNOSTIC
           reporter, ADVECTIVE only, whose own header says "this does not modify dt
           or suggest a value... No adaptive time stepping here or implied by this
           API." So "CFL-based time-step control" is correctly MISSING while "a
           reusable CFL reporter" is correctly PRESENT. Reporting a flat 0 for CFL
           would have been a false negative and would have hidden a reusable piece.
"discont-  4 hits, each checked: harmonic face interpolation (flux-preserving across
 inuity"   a coefficient discontinuity), an alpha-advection comment, and
           AutomaticWallTreatment.TheContinuityTestCanDetectADiscontinuity. None is
           a flow discontinuity.
"energy"   P15's thermal/EnergyEquation.hpp exists and is substantial. Checked: it
           transports TEMPERATURE, steady or transient. Classified as thermodynamic
           property/transport support, NOT as a total-energy equation -- the
           distinction the authorization requires.
```

## 4. Claims corrected during self-verification

Recorded rather than silently fixed.

```text
1. CompressibleSIMPLE.cpp:481 -> :478
   The steady evaluateContinuity call. Cited as 481 in three documents from a range
   read; the exact line is 478. Corrected in all three.

2. SimulationSetup.hpp:59 quotation
   Quoted as "use the case's own converged thermal temperature field". The actual
   text is "use the case's own converged thermal field instead". The line number was
   right, the wording was not. Corrected.

3. Test count 90 across 13 files -> 124 across 15 files (source-level),
   129 registered
   The earlier AUDIT.md figure does not reproduce. Superseded with a per-file table
   and an exact definition for each of the two counts, per CLAUDE.md §4.

4. "Compressible continuity: YES / EXISTS" -> PARTIAL
   The earlier audit classified it as existing. Tracing callers showed the transient
   form has no production route. Reclassified with the caller enumeration as evidence.

5. "Compressible momentum: YES / EXISTS" -> PARTIAL
   The earlier audit classified it as existing. The dilatational stress term is
   absent. Reclassified, with the MMS test's own not_covered field as corroboration.
```

```text
6. Low-Mach validation UNDER-credited, then corrected UPWARD
   First described as "7 regression tests plus an MMS study", i.e. verification only.
   Reading tests/integration/case/test_compressible_coupled_production_case.cpp showed
   a genuine LITERATURE VALIDATION as well:
   MatchesArkilicIsothermalLubricationPressureProfile compares pressureAbsolute against
   the analytical isothermal compressible lubrication profile at L2/Linf < 5%, with the
   tolerance DERIVED from the lubrication approximation's own O(Re*(H/L)) ~ 0.17 error
   rather than fitted. Plus a negative control proving the post-hoc mode is unchanged.
   Corrected in capability_matrix.md and test_inventory.md.

7. pseudoTimeStep unreachability, upgraded from inference to proof
   First stated from a ProjectRunner comment. Verified directly: 0 hits for
   pseudo_time / pseudoTimeStep / pseudo-time across src/io, include/cfd/io, apps,
   docs, cases and tests/data. It is genuinely unreachable from the case format, the
   CLI and the documentation.
```

Items 4 and 5 change P17's actual scope, which is the substantive return on
re-auditing at depth rather than accepting the earlier note. Item 6 is the audit
correcting itself in the *generous* direction — worth recording, because an audit that
only ever revises downward is not measuring, it is arguing.

## 5. Claims this audit deliberately does NOT make

```text
- that native Windows behaviour is established. It is not; WSL2 only.
- that the 129 compressible tests validate anything above low Mach. They do not, and
  the MMS test says so in its own not_covered field.
- that CPU/GPU compressible equivalence "passes because there is nothing to compare".
  That is a vacuous pass; it is recorded MISSING and G8 is recorded DEFERRED.
- that evaluateCompressibleContinuity or assembleCompressibleMomentumComponent are
  production capability. They are tested library entry points with no production
  caller.
- that IdealGasEOS::pressure is integrated. P17.2 added it; nothing calls it yet.
- that the dual-cp hazard is currently causing a wrong answer. It is latent and
  reachable, not active -- no present case configures both.
- any tolerance for G4, G6(f), G7 or G8. Those depend on a mesh family and a scheme
  order that do not exist yet; each is marked TOLERANCE REQUIRES JUSTIFICATION with
  the derivation it must come from.
```
