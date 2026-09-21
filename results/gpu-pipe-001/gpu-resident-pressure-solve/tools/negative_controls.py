#!/usr/bin/env python3
"""GPU-PIPE-001 GPU-resident pressure solve -- negative controls.

Reuses the GPU-DISC-001P control engine, so the Phase D discipline (record
sha256, one mutation, rebuild, run the narrowest detector, restore, verify
sha256, rebuild, re-pass) is the one already qualified rather than a second
implementation of it.

THREE KINDS OF DEFECT LIVE HERE, and they need three kinds of detector:

  numerical   the solve computes something different -- the controlled
              comparison (`residency`) sees it, because it runs the same case
              through both entry points in one binary and compares bitwise.

  invisible   the solve computes exactly the right answer but pays for traffic
              it did not need. NO equivalence gate can see this. `rp7` is one,
              and it is the reason this gate has a transfer detector at all --
              the same lesson `pf7` taught in persistent fields.

  structural  the device matrix stops being the matrix the host solver would
              have received. `rp1` is one, and it is expected to be a NULL
              control: the extra entries are explicit zeros, and adding 0.0 to
              a running sum almost always changes nothing. "Almost always" is
              not a basis for a bitwise claim, which is exactly why the
              structure is asserted directly (`sparsity`) instead of being
              trusted to show up in a number.
"""

import sys

TOOLS = "/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/gpu-disc-001/negative-controls/tools"
sys.path.insert(0, TOOLS)

from control_engine import NL, ROOT, execute  # noqa: E402

SIMPLE = "src/pressure_velocity/SIMPLE.cpp"
FACADE = "cuda/kernels/GpuSimpleDiscretizationCuda.cpp"
KERNEL = "cuda/kernels/GpuResidentSolveKernel.cu"
PLAN = "cuda/kernels/DevicePressureCorrectionPlan.cpp"

NUM = ["residency"]
ARGS = {"residency": [], "solve_transfers": [], "sparsity": ["40"]}


def c(cid, file, desc, anchor, become, harnesses, expect="detect"):
    return {"id": cid, "layer": "resident-solve",
            "origin": "GPU-PIPE-001 resident pressure solve", "file": file,
            "desc": desc, "anchor": anchor, "become": become,
            "harnesses": harnesses, "expect": expect, "args": ARGS,
            "first": None}


CONTROLS = [
    c("rp1_structure_not_compacted", PLAN,
      "the pinned row keeps the explicit zeros toHostSystem() drops, so the device "
      "matrix stops being the matrix the host solver receives. Expected NULL for the "
      "residency comparison -- adding 0.0 to a sum does not change it -- which is the "
      "whole argument for asserting the STRUCTURE rather than trusting a number.",
      "        if (c == referenceCell && fullColumnIndices_[k] != c) continue;",
      "        // MUTATED: the pinned row keeps its explicit zeros.",
      ["sparsity"]),

    c("rp2_jacobi_not_inverted", KERNEL,
      "the device Jacobi preconditioner stores the diagonal instead of its "
      "reciprocal, so M is applied as diag(A) rather than diag(A)^-1",
      "  inverseDiagonal[row] = 1.0 / diag;",
      "  inverseDiagonal[row] = diag;",
      NUM),

    c("rp3_nonzero_initial_guess", FACADE,
      "the resident solve starts from all-ones instead of the all-zero vector "
      "LinearSolver::solve(system) supplies, so the two entry points solve the same "
      "system from different starting points",
      "  fill(impl_->krylov.x, nc, 0.0);",
      "  fill(impl_->krylov.x, nc, 1.0);",
      NUM),

    c("rp4_rhs_not_copied", FACADE,
      "the assembled right-hand side is never moved into the workspace, so the solve "
      "runs against whatever the previous iteration left in b",
      "  copyToWorkspaceRhs(system.rhs, impl_->krylov);",
      "  // MUTATED: the RHS never reaches the solver workspace.",
      NUM),

    c("rp5_solution_not_carried", FACADE,
      "p' is left in the Krylov workspace and never carried into the buffer the "
      "velocity and face-flux corrections read, so both corrections use a stale p'",
      "  copyWorkspaceSolution(impl_->krylov, nc, impl_->pPrime);",
      "  // MUTATED: p' never reaches the corrections.",
      NUM),

    c("rp6_diagonal_rejection_removed", KERNEL,
      "the device Jacobi build stops rejecting a zero, near-zero, missing or "
      "non-finite diagonal, so the resident path accepts systems the host path "
      "refuses -- a behavioural difference hidden inside a performance change",
      "  if (!found || !isfinite(diag) || diag == 0.0 || fabs(diag) < cfd::constants::small) {",
      "  if (false) {",
      ["rejection"]),

    c("rp7_redundant_round_trip", FACADE,
      "the resident solve additionally downloads the whole system every time, exactly "
      "as the old path did. NUMERICALLY IDENTICAL -- every equivalence gate in this "
      "project passes it. Only a transfer count can tell, which is why residency "
      "needs a transfer detector to be a falsifiable claim at all.",
      "  impl_->krylov.resize(nc);",
      "  impl_->krylov.resize(nc);" + NL +
      "  (void)toHostSystem(nc, downloadIndices(system.rowOffsets)," + NL +
      "                     downloadIndices(system.columnIndices), download(system.values)," + NL +
      "                     download(system.rhs));",
      ["solve_transfers"]),

    c("rp8_resident_path_disabled", SIMPLE,
      "the dispatch condition is forced false, so every solve silently takes the host "
      "round trip. Numerically identical again -- the comparison catches it ONLY "
      "through its non-vacuity assertion, which is what that assertion is for.",
      "  const bool useResidentPressureSolve =" + NL +
      "      useGpuDiscretization && !settings_.enableGpuResidency &&",
      "  const bool useResidentPressureSolve =" + NL +
      "      false && useGpuDiscretization && !settings_.enableGpuResidency &&",
      NUM),
]


def check():
    bad = 0
    for control in CONTROLS:
        source = (ROOT / control["file"]).read_text(encoding="utf-8")
        n = source.count(control["anchor"])
        if n != 1:
            bad += 1
            print(f"  ANCHOR x{n}  {control['id']:34s} {control['file']}")
            print(f"    {control['anchor'].splitlines()[0]!r}")
    print(f"\n{len(CONTROLS)} controls, {bad} bad anchors")
    print(f"observable: {sum(1 for x in CONTROLS if x['expect'] == 'detect')}")
    print(f"null:       {sum(1 for x in CONTROLS if x['expect'] == 'null')}")
    return 1 if bad else 0


if __name__ == "__main__":
    if "--check" in sys.argv:
        raise SystemExit(check())
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1].split(",")
    selected = [x for x in CONTROLS if only is None or x["id"] in only]
    ok, _ = execute(selected,
                    "../../gpu-pipe-001/gpu-resident-pressure-solve/negative-controls",
                    "GPU-PIPE-001 resident pressure solve negative controls")
    raise SystemExit(0 if ok else 1)
