# P10-APP-004 — Production Physics Compatibility Matrix

**Status: COMPLETE — implemented and verified with real evidence.**

## What changed

`src/io/case/PhysicsConfigParser.cpp` previously enforced cross-block
physics-compatibility rules inline, scattered across four separate `if`
blocks inside `parsePhysicsConfig` (one each for buoyancy-requires-thermal,
multiphase-excludes-turbulence, the multiphase viscosity invariant, and
compressible.thermal_coupled-requires-thermal). This consolidates all of
them — plus one newly-added rule — into a single function,
`validatePhysicsCompatibility(const PhysicsConfig&, path)`, called exactly
once at the end of `parsePhysicsConfig` after every individual block has
already been parsed. This is now the one authoritative place any
cross-physics-block rule is enforced.

**Audited combinations** (thermal, turbulence, buoyancy, species,
multiphase, compressible):

| Rule | Kind | Status before | Status now |
|---|---|---|---|
| Buoyancy requires Thermal | requirement | enforced, inline | enforced, consolidated |
| Compressible.thermal_coupled requires Thermal | requirement | enforced, inline | enforced, consolidated |
| Multiphase excludes Turbulence | exclusion | enforced, inline | enforced, consolidated |
| Multiphase's viscosity invariant | numeric constraint | enforced, inline | enforced, consolidated |
| **Multiphase excludes Compressible** | exclusion | **not enforced** | **enforced (new)** |
| Species + Multiphase | supported | untested | supported, tested |
| Species + Compressible | supported | untested | supported, tested |
| Compressible + Buoyancy | supported | untested | supported, tested |
| Compressible + Turbulence | supported | untested | supported, tested |
| Everything compatible at once (Thermal+Turbulence+Buoyancy+Species+Compressible) | supported | untested | supported, tested |

**Rationale for the one new rule** (multiphase excludes compressible):
multiphase's linear-mixture density/viscosity model and compressible's
ideal-gas EOS reinterpretation describe two different, mutually incoherent
fluids — there is no single physical reading of "a two-phase liquid/gas
mixture that is also an ideal gas." No case or test in the codebase
exercised this combination before; nothing here removes existing behavior.

**No other combination was restricted.** Species, in particular, has no
exclusions with anything — it is a passive scalar riding the existing mass
flux, orthogonal to every other module — confirmed by tracing
`ProjectRunner.cpp`'s dispatch order (species is computed independently of
multiphase/compressible's own blocks).

**Architecture confirmation (no duplication found or introduced):** GUI
validation (`apps/gui/SimulationControllerEditing.cpp:213`) round-trips
through the exact same `cfd::io::CaseBuilder{}.build(...)` call that
`ProjectRunner`/CLI use — there was no separate, duplicated compatibility
logic in the GUI to consolidate. `PhysicsEditor.qml` and
`CaseModelAdapter.cpp` contain zero hardcoded exclusion rules. The single
consolidated function is therefore already the sole authority reached by
JSON validation, `ProjectRunner`, and the GUI alike.

## New test coverage

`tests/unit/io/test_physics_compatibility.cpp` (new file, registered in
`tests/unit/io/CMakeLists.txt`) — 10 tests, all exercising cross-block
combinations only (per-block field validation stays in each block's own
`test_<x>_case.cpp`):

- 5 supported-combination tests (including the maximal "everything
  compatible together" case)
- 4 unsupported-combination tests, each asserting the exception message
  actually names the relevant field (not just that *some* exception was
  thrown)
- 1 conflicting/malformed-configuration test (two independently-sufficient
  rejection reasons at once, confirming rejection either way)

## Verification evidence (fresh run, this session)

- Incremental rebuild (WSL2/gcc debug): clean, zero errors, zero new
  warnings.
- `tests/unit/io/CFDIoTests --gtest_filter='PhysicsCompatibilityTest.*'`:
  **10/10 PASS**.
- `tests/unit/io/CFDIoTests` (full): **194/194 PASS** (up from 184 pre-change
  — the 10 new tests, everything else unchanged and still passing,
  confirming the refactor preserved every existing rule's behavior
  exactly).
- `tests/integration/case/CFDCaseIntegrationTests` (run from source root):
  **22/22 PASS**.
- `apps/gui/CFDGuiControllerTests`: **40/40 PASS**.
- Full regression suite (`ctest -j4`): **100% passed, 0 failed, 1300/1300**
  active tests (13 pre-existing disabled tests unchanged) — up from
  1290/1290 before this change, confirming no regression anywhere in the
  suite.

## Documentation

The compatibility matrix is documented, for now, in
`validatePhysicsCompatibility`'s own header comment in
`src/io/case/PhysicsConfigParser.cpp` (the authoritative, code-adjacent
copy). A user-facing copy in `docs/user_guide/case_format.md` is the next
closeout item (tracked in `TODO.md`), not yet done as of this commit.
