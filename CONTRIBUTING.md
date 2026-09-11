# Contributing to CFDApp

Thanks for looking at this project. It's a small, discipline-heavy CFD
codebase — the guidance below exists to keep it that way, not to add
ceremony for its own sake.

## Build setup

```bash
cmake --preset debug        # or: release, asan
cmake --build --preset debug -j
ctest --preset debug --output-on-failure
```

See [README.md](README.md#build-requirements) for compiler/CMake versions and
optional GUI/OpenMP/CUDA flags. `CMakeUserPresets.json` (gitignored) is the
place for personal, machine-specific overrides — never edit `CMakePresets.json`
for that.

## Branch workflow

- Work off `main`; open a feature branch per change (`git checkout -b
  <short-description>`).
- Keep commits reviewable: one logical change per commit where practical,
  with a message that says *why*, not just *what* (see recent `git log` for
  the tone this project uses — commit messages here typically explain the
  reasoning/derivation behind a fix, not just restate the diff).
- Rebase onto `main` before opening a PR rather than merging `main` in.

## Coding style

- `clang-format` is authoritative — run it before committing
  (`clang-format -i <files>`) or let CI's `format` job catch drift.
- `clang-tidy` (bugprone-\*, performance-\*, modernize-\*, selected
  cppcoreguidelines-\*/readability-\*, `include/` + `src/` only) must be
  clean.
- Match the established architectural patterns rather than inventing new
  ones for a similar problem:
  - **Contribution-function assembly**: each physics/equation module
    builds its own `assembleXxxContribution(...)` writing into a shared
    `SparseMatrixBuilder` + RHS, combined by a top-level
    `assembleXxxEquation`. Each domain owns its own assembler rather than
    sharing one across domains — this is deliberate, not duplication to
    clean up.
  - **Properties / Equation / Solver** three-layer module shape (see
    `cfd::thermal`, mirrored by `cfd::species`, `cfd::multiphase`,
    `cfd::compressible`): a plain-data properties type, a pure equation-
    assembly function, and a solver that owns the outer iteration loop.
  - Steady equations needing lagged-boundary self-consistency use an outer
    Picard loop (see `ThermalSolver`/`SpeciesSolver`); inherently transient
    equations solve one timestep at a time with no outer loop (see PISO,
    `VolumeFractionSolver`) — pick the convention that matches your
    equation's own nature, don't default to one out of habit.
  - The GUI (`apps/gui/`) never contains solver or visualization-algorithm
    logic — it only adapts `cfd::app`/`cfd::viz` C++ APIs to Qt/QML types
    and draws what it's given. New GUI features should follow the same
    split: algorithm in a testable, Qt-free C++ header/source pair, then a
    thin `SimulationController` adapter, then QML presentation.
- Prefer `[[nodiscard]]`, `const`, and RAII consistently with the
  surrounding file; match its comment density rather than over- or
  under-commenting relative to it.

## Testing

Every test binary carries exactly one CTest label
(`unit`/`solver`/`integration`/`validation`, see `cmake/Testing.cmake`) so a
tier can be selected on demand (`ctest -L unit`). New tests should:

- Live under the matching `tests/{unit,solver,integration}/<module>/`
  directory, registered via `gtest_discover_tests(... PROPERTIES LABELS
  "<tier>")`.
- Prefer `matrix.multiply(unitVector)`-style probing over
  `SparseMatrix::diagonal(row)` when a row may have no stored diagonal
  entry (convection-only/zero-coefficient assemblies throw there — a
  recurring pitfall in this codebase's own history).
- Use brace-initialization (`Type{identifier}`) rather than
  `Type(identifier)` inside `EXPECT_THROW`/similar single-argument
  constructor calls — the latter is a "most vexing parse" trap that silently
  declares a function instead of constructing a temporary.
- Never weaken a numerical tolerance or delete a test to force a pass. If a
  test is wrong, fix the test with a stated reason; if the code is wrong,
  fix the code.

Run the full suite (`ctest --output-on-failure`) before opening a PR. The
current baseline is **1149/1149** (default build) / **1162/1162**
(`-DCFDAPP_BUILD_GUI=ON`) — a PR should not reduce either count.

## Numerical validation expectations

A new physics feature is not "done" when it compiles and a unit test
passes. Per this project's own standing rule (see ROADMAP.md's "Development
Rule"), it needs, in order: implementation, focused unit tests, an
analytical or published-benchmark validation case, a grid-refinement check
where an order of accuracy is meaningful, and a determinism check (repeated
runs bit-identical, or within a stated tight tolerance). Genuine measured
results only — no invented speedups, no fabricated convergence data, and no
claiming CUDA/GPU acceleration works for something that was only run on
CPU. See [QUALITY_GATE.md](QUALITY_GATE.md) for a worked example of this
discipline (two real order-of-accuracy bugs, root-caused and fixed, with the
derivation documented).

## Avoiding commits of generated/build artifacts

`.gitignore` already excludes `build/`, CMake/IDE scratch files, Python
caches, and stray compiler diagnostics — check it before adding a new
generated-output location rather than committing the output once and
special-casing it later. Note that `results/`, `validation/`, and
`cases/*/results/` **are** intentionally version-controlled in this
repository (they hold real validation/release evidence, not disposable
build output) — do not add a blanket ignore rule for `results/` or
`cases/**/results/`.

## Pull requests

- Describe *what changed and why*, not just *what*. If the change touches
  numerics, state the validation evidence directly in the PR description
  (which test, what tolerance, what it checks against).
- CI (`.github/workflows/ci.yml`) runs build+test (matrixed
  GCC/Clang × Debug/Release), `clang-format`, `clang-tidy`, ASan+UBSan, and
  the Python suite as separate jobs — all must be green.
- Keep PRs focused; a refactor and a behavior change are easier to review
  (and revert, if needed) as separate PRs.
