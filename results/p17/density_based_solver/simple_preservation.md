# P17.8 — SIMPLE preservation

The authorization's §3 and §28 make this a **hard requirement**: the incompressible SIMPLE path, the
`CompressibleSIMPLE` path and the existing low-Mach pressure-based path must remain intact, with
results, dispatch and default configuration unchanged.

Gate Group A, and gate criterion **G1**.

---

## 1. Why the claim is structural before it is numerical

```text
P17.8 ADDS NO DISPATCH AND NO CASE-FORMAT KEY.

src/app/ProjectRunner.cpp's existing branch
    if (setup.compressible.has_value()) { ... incompressible warm start ... if (coupled) ->
    CompressibleSIMPLE }
is NOT EDITED. No line that selects a solver is touched anywhere in the repository.
```

That is the strongest form of this guarantee available, and it is why the consequence is stated
plainly rather than buried: **the density-based solver is library-level and cannot be reached from a
case file.** No production capability is claimed for it. `architecture.md` §3 recorded that
trade-off before implementation, and §39 of the authorization permits it explicitly.

## 2. The structural evidence

`tools/structural.sh`, `logs/19_structural.log` (authoritative; `logs/06_structural.log` is the
earlier run, preserved):

```text
A1  src/app/ProjectRunner.cpp                       SHA-256 identical to logs/02_freeze.log
    include/cfd/app/ProjectRunner.hpp               SHA-256 identical
A2  src/pressure_velocity/SIMPLE.cpp                SHA-256 identical
    include/cfd/pressure_velocity/SIMPLE.hpp        SHA-256 identical
    src/compressible/CompressibleSIMPLE.cpp         SHA-256 identical
    include/cfd/compressible/CompressibleSIMPLE.hpp SHA-256 identical
A5  include/cfd/solver/CFL.hpp                      SHA-256 identical to the freeze
    src/solver/CFL.cpp                              git-clean vs HEAD
    include/cfd/solver/TransientSolver.hpp          git-clean vs HEAD
    src/solver/TransientSolver.cpp                  git-clean vs HEAD
A6  27 further P17.1-P17.7 production sources       UNTOUCHED: 27   TOUCHED: 0
    ONE DECLARED EXCEPTION: HllcFlux.hpp -- see AMENDMENT_A6_upstream_exception.md
A4  files under a SIMPLE path naming the solver     0   (sanity control: 7 files match 'SIMPLE')
```

```text
THE A4 SANITY LINE IS NOT DECORATION. A grep that names a directory that does not exist, or a
pattern that matches a renamed symbol, prints 0 -- which reads exactly like a clean result. The
control shows the same paths DO match a pattern that should be there, so the 0 above means absence
rather than blindness. This project has had three fail-open probes of that shape (P17.6 B6, P17.7
audit §6, and this phase's own Q2 probe, which the control caught).
```

## 3. The numerical evidence — criterion A3

`tools/fingerprint.sh`, `logs/18_fingerprint.log` (authoritative; `logs/11_fingerprint.log`
is the earlier run, preserved). The probe is P13-001's own
`results/p13/restart/tools/solve_fingerprint.cpp`, **unmodified**, and the procedure is copied
verbatim from P17.7, which copied it from P17.6/P17.3/P17.2. Reusing the instrument unchanged is
what makes "bitwise identical" comparable across P17.1 … P17.8.

```text
library rebuilt from the current source first: libcfdcore.a
  c7997cfd2a0d07301c1773a8fe43491006c11fa46cffa02ae0427003bb779609

cases: 10    (P13-001 records 10)
lines: 130   (P13-001 records 130)

BITWISE IDENTICAL -- P17.8 moved no bit of the incompressible solver
the diff file is EMPTY (0 bytes) and is preserved as evidence, exactly as G1 requires
```

## 4. The non-vacuity demonstration G1 requires — and which no earlier P17 phase ran

G1's own text:

> the fingerprint instrument must be shown able to FAIL: P13 proved this by planting a defect. Any
> P17 item that claims G1 must show the instrument detecting a deliberate perturbation, not merely
> reporting "identical".

```text
EVERY P17 SCRIPT THAT DRIVES THE PROBE, and whether it ever perturbs anything:

  grep -rl "solve_fingerprint" results/p17/*/tools/   then  grep -ciE 'perturb|plant|non-vacu'

  results/p17/variable_density/tools/run_fingerprint.sh          0
  results/p17/ideal_gas_eos/tools/run_fingerprint.sh             0
  results/p17/compressible_continuity/tools/run_fingerprint.sh   0
  results/p17/compressible_momentum/tools/g3_fingerprint.sh      0
  results/p17/total_energy/tools/fingerprint.sh                  0
  results/p17/low_mach/tools/fingerprint.sh                      0
  results/p17/low_mach/tools/audit.sh                            0
  results/p17/hllc/tools/fingerprint.sh                          0
  results/p17/density_based_solver/tools/fingerprint.sh         22   <- this phase

P17.1 through P17.7 each reported "BITWISE IDENTICAL" without demonstrating the instrument could
report anything else. That is a finding about the EVIDENCE, not about the code -- those fingerprints
are very likely correct, and P13 did plant a defect when the instrument was built. But G1 asks each
P17 item to show it, and none of them did.
```

P17.8 runs it:

```text
plant     src/pressure_velocity/SIMPLE.cpp -- the velocity relaxation actually used by the
          iteration, multiplied by 0.999. Small enough that the solve still converges, so what the
          diff shows is a MOVED SOLUTION rather than a crash.
          pristine sha256 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1
          planted  sha256 1def49d5c49df5895bff5e2e9035d1ccfd190e437fe38b7646c360caa000fcb4
rebuild   cfdcore rebuilt from the planted source, exit 0
result    DETECTED -- 112 differing lines. First difference:
            <   residual_u  3f70e484c99999e7        >   residual_u  3f70f0af08c48503
            <   residual_v  3f6a6c7d9568a4c4        >   residual_v  3f6a7779b12399aa
restore   sha256 back to 34f4f995..., verified, and cfdcore rebuilt from the restored source.
          The script ABORTS if the restore hash does not match, so a plant cannot leak into the
          regression.
```

A 0.1 % change in one relaxation factor moves 112 of 130 fingerprint lines. The instrument is not
blind, and the empty diff in §3 is therefore a measurement rather than a default.

## 5. The structural corollary

```text
EOS references under src/pressure_velocity and include/cfd/pressure_velocity : 0
DensityBasedSolver references from SIMPLE or from ProjectRunner                : 0
```

The incompressible path remains EOS-free and solver-free. It cannot be routed through HLLC or
through the density-based solver, because it does not name them.

## 6. The regression half

The fingerprint covers the incompressible solver's numbers. The full regression covers everything
else the two SIMPLE paths touch — see `regression.md` for the exact counts per configuration, and
for the named P17.2 (`IdealGasEOSTest`, 30 rows), P17.7 (`Hllc*`) and low-Mach rows executed by
name, since registration is not execution.

## 7. What is NOT claimed

```text
* NOT claimed: that P17.8 was exercised through a case file, the CLI or the GUI. It cannot be --
  it is library-level by design, and that is the point of §1 above.
* NOT claimed: that the density-based solver is a drop-in alternative to CompressibleSIMPLE at low
  Mach. It is measurably stiffer there; see verification.md and the §27 case. P17.6's pressure-based
  path remains the low-Mach reference.
* NOT claimed: Debug+GUI preservation. That configuration is deferred to P17.11 by standing
  decision and was NOT run.
```
