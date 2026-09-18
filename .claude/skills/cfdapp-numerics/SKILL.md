---
name: cfdapp-numerics
description: Rules and evidence requirements for CFDApp numerical changes - formulation before code, derived tolerances, manufactured solutions, grid convergence and observed order, conservation, independent reference paths. Use for any change to discretization, gradients, fluxes, boundary treatment, solver criteria or mesh operators.
---

# CFDApp numerics

A change is a **numerical** change if it can alter a computed value — discretization, gradient
scheme, convection or diffusion operator, face interpolation, boundary treatment, mesh geometry or
quality metric, solver criterion, relaxation, tolerance. Three lines qualifies.

## 1. Formulation before code

Write this down **before** implementing. If it cannot be written, the change is not understood yet.

```text
CONTINUOUS EQUATION   the PDE / operator being discretized
DISCRETE EQUATION     the exact finite-volume form, per face and per cell
UNITS                 of every term; they must match
SIGN CONVENTION       outward normals, flux direction, source sign
BOUNDARY TREATMENT    Dirichlet / Neumann / periodic / wall, and its order of accuracy
CONSERVATION          what is conserved, locally and globally, and to what level
CONSISTENCY           the truncation error, and that it → 0 as h → 0
STABILITY             CFL / diagonal dominance / positivity constraints
EXPECTED CONVERGENCE  the design order, and where it is expected to degrade
```

State the expected result **before** measuring it. A derivation produced after seeing the output is
not a prediction.

## 2. Evidence required

Where applicable (`CLAUDE.md` §6), a numerical change needs:

* an analytical or reference derivation, stated in advance;
* a **manufactured solution** (MMS);
* **grid convergence** — observed order, Richardson extrapolation, GCI;
* **conservation** residuals, local and global;
* dimensional consistency;
* an **independent reference path** — e.g. exact-rational Python with no CFDApp code;
* negative controls and non-vacuity checks.

**New numerical methods require quantitative convergence verification. New physics requires
independent physical validation** against analytical or literature results.

Never accept **"the test passes"** as evidence for a new numerical method.

## 2a. Use the instruments this repo already has

Do **not** build an order study from scratch. CFDApp ships the whole shelf — check it before
writing any harness:

```text
include/cfd/validation/GridConvergence.hpp          observedOrder, orderValid(), Richardson, GCI,
include/cfd/validation/GridConvergenceStudy.hpp     and the monotonic_not_asymptotic classifier
include/cfd/validation/ManufacturedSolutionStudy.hpp
include/cfd/validation/ErrorNorms.hpp               L1 / L2 / Linf
include/cfd/validation/ProductionValidation.hpp
src/validation/                                     their implementations

tests/unit/discretization/ManufacturedFields.hpp    manufactured fields with exact gradients
                                                    and exact per-face Dirichlet values
tests/unit/discretization/DistortedMesh.hpp         createDistortedQuad2D(nx, ny, Lx, Ly, amplitude)
tests/integration/mms/test_mms_simple.cpp           an existing MMS study with frozen order gates
                                                    (e.g. pressure L2 gated to [1.5, 2.4])
results/validation/production/*_grid_convergence.json   the committed report format
```

An independent reference path still means **outside** this shelf — exact-rational Python with no
CFDApp code — because these instruments share the codebase under test.

## 3. Observed order — validity conditions

An observed-order claim is valid **only if all** hold:

* the quantity is iteratively converged — check against a long-run plateau, not the default budget;
* the mesh family is self-similar and inside the asymptotic range;
* the error does not change sign (order is undefined across a zero crossing);
* refinement triplets are parity-consistent.

If any fails, the number is not an order. Say so rather than reporting it.

## 4. Tolerances

* **Derive** every threshold from the quantity's dimensional, round-off and discretization
  behaviour. If it scales with 1/h, a coordinate ratio or a mesh metric, the threshold scales too.
* Label empirical constants as empirical.
* An absolute threshold on a quantity whose scale varies is a defect waiting to happen — a fixed
  `1e-30` breakdown test misread a healthy small-residual iteration as a breakdown
  (`results/gpu-pcorr-001/`). Prefer scale-relative criteria for scale-varying quantities.
* **Never weaken a tolerance to obtain PASS** (`CLAUDE.md` §1.4). Never inflate an iteration limit
  to convert a non-convergence into a pass.

## 5. Not silently changing existing behaviour

Incompressible, thermal, turbulence, species, multiphase, compressible, CLI, GUI, CPU/OpenMP/CUDA
and file formats must not change silently. When shared code changes, **prove** existing results are
unchanged — bitwise where that is claimed. Attribution needs two libraries: build the baseline
without the change and measure both.

## 6. Verification checks that are not validation

Label each check honestly (`CLAUDE.md` §4):

| Label | Means |
| --- | --- |
| regression | previously-passing behaviour still holds |
| consistency | internal agreement between two of our own paths |
| verification | solving the equations right — MMS, order, conservation |
| validation | solving the right equations — analytical or literature reference |

Re-deriving the same formula in a test is **consistency**, not validation. Never adopt production
output as a reference value.

## 7. Evidence layout

`results/<phase>/`: `formulation.md` (the §1 content), `acceptance_gate.md` (frozen, with sha256 in
`logs/00_freeze.log`), `logs/NN_*.log`, `tools/` (probes and independent reference
implementations), `data/`, `summary.md`. Amendments get their own `acceptance_gate_AN.md` with
their own dry-run and non-vacuity check, and a fresh rerun — never a reused earlier result.
