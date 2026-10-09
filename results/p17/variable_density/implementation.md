# P17.1 — implementation

---

## 1. The delta, attributed

```text
file                                                     insertions  deletions
------------------------------------------------------------------------------
src/compressible/CompressibleMassFlux.cpp                    +34         0
include/cfd/compressible/CompressibleMassFlux.hpp            +12         0
tests/unit/compressible/test_compressible_mass_flux.cpp     +276         0
------------------------------------------------------------------------------
                                   3 files, 322 insertions, 0 DELETIONS
```

**Zero deletions on all three files.** The change is purely additive: no existing line
of production code, test code or contract was modified or removed. That is checkable
directly — `git diff --numstat` reports `-0` for each file — and it is the strongest
single statement about the change's blast radius.

```text
sha256 after the change
  2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7  src/…/CompressibleMassFlux.cpp
  d376375ce2308b2ffe16968d2a063bda595d70194de9d6eb8faa147e1c1a0ea3  include/…/CompressibleMassFlux.hpp
  9d4adb570b5958310eed5740fed11fd906134daa76ba5c2a112d4a7d572f9b63  tests/…/test_compressible_mass_flux.cpp
```

## 2. What the production change is

One validator gained a value check. `validateFaceDensityInputs` previously verified only
array **lengths**:

```cpp
// before -- sizes only
if (density.size() != mesh.numberOfCells()) throw ...;
if (pressureGauge.size() != mesh.numberOfCells()) throw ...;
if (temperature.size() != mesh.numberOfCells()) throw ...;
```

```cpp
// after -- the same three size checks, plus:
for (Index i = 0; i < mesh.numberOfCells(); ++i) {
  if (!std::isfinite(density[i]) || !(density[i] > 0.0)) {
    throw InvalidArgumentError(std::string(callerName) + ": cell " + std::to_string(i) +
                               ": density must be finite and > 0");
  }
}
```

Plus `#include <cmath>`, and a header comment stating the precondition as part of the
contract.

### Why this one place

```text
calculateCompressibleMassFlux  ---+
                                  +--> validateFaceDensityInputs   <- the chokepoint
evaluateCompressibleFaceDensity ---+
```

Both public entry points call this validator, and `calculateCompressibleMassFlux` itself
calls `evaluateCompressibleFaceDensity`. One edit therefore covers the entire canonical
face-density and mass-flux path — the exact path every equation consumer ultimately
reads. Four further validators in the momentum, pressure-correction, continuity and
relaxed-momentum modules would have duplicated the check over the *same* field without
adding coverage; that boundary was decided before implementing and is recorded in
`acceptance_gate.md` §0.

### Message format, matched to existing precedent

```text
IdealGasEOS               "IdealGasEOS: density must be finite and > 0"
evaluateDensityField      "evaluateDensityField: cell 3: <wrapped EOS message>"
P17.1 (new)               "calculateCompressibleMassFlux: cell 0: density must be
                           finite and > 0"
```

Names the caller, the cell and the field. A bare "invalid argument" on a large mesh is
not actionable, and both existing precedents already say so.

## 3. Why validation, and not a downstream finiteness check

This is the substance of the item, and it rests on a measurement rather than an
argument. From the dry-run on the unchanged tree (`logs/00_dryrun.log`):

```text
input              internal rho_f   mDot_f   non-finite?   would allFinite() catch it?
--------------------------------------------------------------------------------------
rho[0] = -2.0            0             0         NO                NO
rho[0] =  0.0            1             1         NO                NO
rho[0] =  NaN          nan           nan        yes               yes
rho[0] = +Inf          inf           inf        yes               yes
rho[0] = -Inf         -inf          -inf        yes               yes
```

A negative density averaged against a valid neighbour gives a face density of **exactly
zero** — a face silently carrying no mass. A zero density gives `rho_f = 1`, a
perfectly plausible non-zero flux from a state that cannot exist. **Neither is
non-finite**, so `CompressibleSIMPLE`'s `allFinite()` sweep, the only protection
downstream, cannot see either one.

That is why the check belongs at the input, and why the criterion is `finite AND > 0`
rather than `finite`. Control **N2** proves the positivity clause is doing the work: with
`> 0` dropped, the two silent cases stop being detected while the non-finite case still
is.

## 4. Rejected, never clamped

```text
policy      throw InvalidArgumentError. No clamp, no floor, no substitution.
precedent   thermal/Enthalpy.hpp: an out-of-range enthalpy is "an ERROR, not a clamp.
            Clamping would return a temperature the energy does not correspond to and
            let a diverging solve look converged."
reasoning   identical for density: a clamped rho produces a mass flux that no
            thermodynamic state corresponds to, and lets a diverging compressible solve
            look plausible.
not done    a positivity LIMITER. That is a conserved-state concern -- a density-based
            solver can legitimately need one when rho is an evolved unknown -- and it
            belongs to P17.8. P17.1 deliberately does not pre-empt that design.
```

## 5. What was deliberately NOT changed

```text
temperature / pressureGauge value checks
  The EOS already validates both at every boundary face, proven by
  CompressibleMassFluxTest.ThrowsOnNonPositiveBoundaryTemperature. That test sets
  temperature[0] = -1.0 with a VALID density and relies on reaching the boundary EOS. An
  up-front temperature check would make it throw earlier -- still passing, but no longer
  exercising the path it was written for. A contaminated instrument, for no gain.
  Also: pressureGauge is GAUGE pressure and legitimately negative below the reference, so
  positivity would be the WRONG rule for it regardless.

the incompressible path
  Untouched. src/pressure_velocity/ and include/cfd/pressure_velocity/ contain ZERO
  references to EquationOfState, IdealGasEOS or ThermodynamicProperties -- verified as
  gate criterion C2, and still 0 after the change.

the energy path's constant-density advection
  Documented, not altered. Changing it is two-way energy coupling = P17.5.
  See integration_matrix.md section 3.

the dilatational viscous stress term
  P17.4. Not touched.
```

## 6. Formatting

```text
clang-format-18 on the three changed files      CLEAN
deletions after formatting                      still 0 on all three
                                                (the test file reflowed 278 -> 276
                                                insertions; no pre-existing line moved)
```

`clang-format-18 -i` was applied to the test file only, after the first `--dry-run`
flagged 12 violations in the newly appended code. The production files were clean on
first write.

### A pre-existing failure this surfaced — recorded, not fixed

Running the whole-tree gate that `CLAUDE.md` §7 specifies found **22 files failing
clang-format, none of them P17.1's**:

```text
apps/gui/tests/test_cli_gui_equivalence.cpp
include/cfd/gpu/DeviceMomentumAssembly.hpp        include/cfd/gpu/GpuKEpsilonCorrection.hpp
include/cfd/thermal/ThermalSolver.hpp             include/cfd/turbulence/KEpsilonModel.hpp
src/io/case/PhysicsConfigParser.cpp               src/pressure_velocity/SIMPLE.cpp
src/thermal/ThermalSolver.cpp                     src/turbulence/KEpsilonModel.cpp
tests/integration/backward_facing_step/…  (2)     tests/integration/turbulence_channel/… (3)
tests/unit/gpu/…  (8)                             tests/unit/gpu/KEpsilonModelTestAccess.hpp
```

All predate P17.1 — `DeviceMomentumAssembly.hpp`, for instance, has mtime
`2026-09-24 01:57`, two days before the P17.0 freeze, and appears nowhere in the P17.1
delta.

**And `results/p13/final-qualification/` contains ZERO mentions of clang-format** — the
gate was never run there. `CLAUDE.md` §8 defines the full regression as Release,
Debug+GUI, sanitizers at CI settings, **and clang-format clean**; P13's G10 reported the
full regression as passing while never executing the fourth component.

Per `CLAUDE.md` §2 ("do not fix unrelated defects inside another phase — record them and
report them"), P17.1 does **not** reformat 22 unrelated files: that would be exactly the
accidental formatting sweep the integrity check forbids, and it would bury a 3-file
change in a several-hundred-line diff. Recorded as technical debt and reported.
