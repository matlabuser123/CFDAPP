#!/usr/bin/env python3
"""GPU-PIPE-001 Final Residency -- negative controls for the resident SIMPLE loop.

Reuses the GPU-DISC-001P control engine, so the discipline (record sha256, ONE
mutation, rebuild, run the narrowest detector, restore, verify sha256, rebuild,
re-pass) is the one already qualified rather than a second implementation of it.

THE COMBINED ARCHITECTURE NEEDS THREE KINDS OF DETECTOR, because it has three
kinds of defect:

  numerical   the solve computes something different. `loop_comparison` sees
              it: one binary, one case, two arms differing only in whether the
              outer iteration is resident, compared bitwise.

  invisible   the solve computes exactly the right answer and pays for traffic
              it did not need. NO equivalence gate can see this -- downloading
              a field and using the device's own copy anyway is bit-identical.
              `loop_transfers` is the only thing that can, which is what makes
              "the SIMPLE loop is resident" falsifiable rather than asserted.

  dispatch    the resident path engages in a configuration it was never
              qualified for. Numerically it may look fine on the case at hand
              while silently changing what some OTHER configuration does.
              `dispatch` asserts every decline condition.

WHAT IS NOT RE-TESTED HERE. Persistent-field residency (stale pressure, stale
face flux, a lying authority flag, a forced full-field re-upload) and the
resident pressure system (stale p', stale RHS, stale matrix, removed rejection)
already have executed, recorded controls:

    results/gpu-pipe-001/persistent-fields/negative-controls/      pf1..pf8
    results/gpu-pipe-001/gpu-resident-pressure-solve/negative-controls/  rp1..rp8

Repeating them would add runtime and no information. The controls here are the
ones the SIMPLE-loop change newly makes possible.
"""

import sys

TOOLS = "/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/gpu-disc-001/negative-controls/tools"
sys.path.insert(0, TOOLS)

from control_engine import NL, ROOT, execute  # noqa: E402

SIMPLE = "src/pressure_velocity/SIMPLE.cpp"
FACADE = "cuda/kernels/GpuSimpleDiscretizationCuda.cpp"

NUM = ["loop_comparison"]
ARGS = {
    "loop_comparison": ["--quick"],
    "loop_transfers": ["--quick"],
    "dispatch": [],
    "dirty_state": [],
    "residency_baseline": ["--quick"],
}


def c(cid, file, desc, anchor, become, harnesses, expect="detect"):
    return {"id": cid, "layer": "resident-simple-loop",
            "origin": "GPU-PIPE-001 final residency", "file": file,
            "desc": desc, "anchor": anchor, "become": become,
            "harnesses": harnesses, "expect": expect, "args": ARGS,
            "first": None}


CONTROLS = [
    # --- stale / wrong resident state -------------------------------------
    c("rl1_wrong_resident_component", FACADE,
      "the relaxation's phiOld is taken from the resident U component for EVERY "
      "momentum component, so V and W are assembled against the wrong resident "
      "velocity field. The defect class the brief calls 'stale U/V/W device "
      "field': the device holds the state and the wrong part of it is read.",
      "  const DeviceBuffer<Real>& source = component == kVelocityU   ? impl_->velocity.x" + NL +
      "                                     : component == kVelocityV ? impl_->velocity.y" + NL +
      "                                                               : impl_->velocity.z;",
      "  const DeviceBuffer<Real>& source = impl_->velocity.x;  // MUTATED",
      NUM),

    c("rl2_momentum_rhs_not_copied", FACADE,
      "the assembled momentum right-hand side never reaches the Krylov "
      "workspace, so each momentum solve runs against whatever the PREVIOUS "
      "solve left in b -- the previous component's RHS, or the pressure "
      "correction's. Stale RHS and shared-workspace contamination in one.",
      "  copyToWorkspaceRhs(system.rhs, impl_->krylov);" + NL +
      "  // The warm start. The host path passes `toVector(previousU)`, which is the",
      "  // MUTATED: the momentum RHS never reaches the solver workspace." + NL +
      "  // The warm start. The host path passes `toVector(previousU)`, which is the",
      NUM),

    c("rl3_momentum_guess_zeroed", FACADE,
      "the momentum solves start from an all-zero vector instead of the "
      "start-of-iteration component the host path warm-starts from. The "
      "CONVERGED answer is barely affected -- which is the point: "
      "SolverResult::initialResidual is measured from the guess and IS SIMPLE's "
      "outer convergence measure, so the residual history and the iteration "
      "count change even when the final field does not.",
      "  copyToWorkspaceGuess(guess, impl_->krylov);",
      "  fill(impl_->krylov.x, nc, 0.0);  // MUTATED",
      NUM),

    c("rl4_predictor_wrong_component", FACADE,
      "every component's momentum solution is carried into the U slot of the "
      "device predictor, so V* and W* never reach the stages that read them",
      "  copyWorkspaceSolution(impl_->krylov, nc, target);",
      "  copyWorkspaceSolution(impl_->krylov, nc, impl_->velocityStar.x);  // MUTATED",
      NUM),

    c("rl5_krylov_guess_not_reset", FACADE,
      "the initial guess is never written into the shared workspace at all, so "
      "each momentum solve inherits whatever x the previous solve left there -- "
      "the previous component's u*, or the pressure correction's p'. This is "
      "exactly 'workspace reuse without the required reset', and it is the "
      "control that justifies sharing ONE Krylov workspace between four "
      "resident solves per outer iteration.",
      "  copyToWorkspaceGuess(guess, impl_->krylov);" + NL +
      "" + NL +
      "  cfd::algebra::SolverResult result =",
      "  // MUTATED: the workspace keeps the previous solve's x." + NL +
      "" + NL +
      "  cfd::algebra::SolverResult result =",
      NUM),

    c("rl12_momentum_assembly_skipped", FACADE,
      "the resident momentum assembly returns before assembling, so the solver "
      "adopts whatever the system buffers already held -- nothing on the first "
      "iteration, the previous iteration's matrix afterwards. The 'a GPU stage "
      "silently did not run inside the GPU loop' class.",
      "  const Index nc = impl_->cellCount;" + NL +
      "  // The relaxation's phiOld. The host path uploads",
      "  if (true) return;  // MUTATED" + NL +
      "  const Index nc = impl_->cellCount;" + NL +
      "  // The relaxation's phiOld. The host path uploads",
      NUM),

    c("rl11_2d_w_predictor_fill_removed", FACADE,
      "the device fill that zeroes the W predictor on a 2D mesh is removed. "
      "Nothing else writes it on a 2D solve, so the stage that reads it either "
      "reads a value no stage produced or refuses. The requireResident guards "
      "exist for precisely this, and this control proves they are load-bearing "
      "rather than decorative.",
      "    fillFieldDevice(impl_->cellCount, 0.0, impl_->velocityStar.z);",
      "    // MUTATED: the 2D W predictor is never zeroed.",
      NUM),

    # --- dirty state -------------------------------------------------------
    c("rl6_velocity_authority_not_marked", FACADE,
      "the resident velocity correction stops marking velocity DeviceOwned, so "
      "the authority model reports a Synchronized field whose host copy is in "
      "fact stale. No number changes; only the authority contract does, which "
      "is what the dirty-state suite exists to assert.",
      "  carryFieldDevice(nc, impl_->correctedVelocity.z, impl_->velocity.z);" + NL +
      "  impl_->velocityAuthority = FieldAuthority::DeviceOwned;",
      "  carryFieldDevice(nc, impl_->correctedVelocity.z, impl_->velocity.z);" + NL +
      "  // MUTATED: the authority transition is missing.",
      # DETECTOR CORRECTED after this control went UNDETECTED.
      #
      # It was first pointed at `dirty_state`, which never calls
      # correctVelocityResident at all -- it only exercises the HostOnly ->
      # Synchronized transition that uploadInitialState performs. A detector
      # that never invokes the mutated method cannot see the mutation, and the
      # control suite proved that by reporting UNDETECTED.
      #
      # `residency_baseline` drives the facade stage by stage and asserts
      # `authority(Velocity) == DeviceOwned` immediately after the resident
      # correction, which is exactly the transition this control removes.
      #
      # The original UNDETECTED result is preserved at
      # negative-controls/driver-12controls-rl6-UNDETECTED.log and
      # negative-controls/rl6-UNDETECTED-by-dirty_state.log.
      ["residency_baseline"]),

    # --- invisible: changes no number ---------------------------------------
    c("rl7_velocity_redownloaded_every_iteration", SIMPLE,
      "the per-iteration full-field velocity download is restored. "
      "NUMERICALLY IDENTICAL -- the device keeps its own copy and every "
      "downstream stage still reads that -- so every equivalence gate in this "
      "project passes it. Only a transfer count can tell, which is why the "
      "residency claim needs a transfer detector to be falsifiable at all. The "
      "same lesson pf7 taught for fields and rp7 for the pressure system.",
      "      if (!useResidentSimpleLoop) gpuDiscretization.downloadVelocity(velocityNew);",
      "      gpuDiscretization.downloadVelocity(velocityNew);  // MUTATED",
      ["loop_transfers"]),

    c("rl8_resident_loop_disabled", SIMPLE,
      "the dispatch condition is forced false, so every solve silently takes "
      "the host round trip. Numerically identical again -- the comparison "
      "catches it ONLY through its non-vacuity assertion, which is what that "
      "assertion is for. A vacuity guard nobody tests is itself untested.",
      "  const bool useResidentSimpleLoop =" + NL +
      "      useResidentPressureSolve && activeModel->name() == \"laminar\" &&",
      "  const bool useResidentSimpleLoop =" + NL +
      "      false && useResidentPressureSolve && activeModel->name() == \"laminar\" &&",
      NUM),

    # --- dispatch ----------------------------------------------------------
    c("rl9_backend_condition_dropped", SIMPLE,
      "the resident loop stops requiring a GPU momentum backend, so a case that "
      "asked for a CPU momentum solver silently gets the device one. No gate "
      "that runs the production configuration can see this; only an assertion "
      "about the DECLINE can.",
      "      settings_.momentumSolver.backend == cfd::algebra::LinearSolverBackend::GPU &&",
      "      true &&  // MUTATED",
      ["dispatch"]),

    c("rl10_laminar_condition_dropped", SIMPLE,
      "the resident loop stops requiring the laminar turbulence model, so a "
      "transport model's correct() is handed a host velocity the resident loop "
      "deliberately leaves stale -- a silent behaviour change in a "
      "configuration this milestone never qualified.",
      "      useResidentPressureSolve && activeModel->name() == \"laminar\" &&",
      "      useResidentPressureSolve &&  // MUTATED",
      ["dispatch"]),
]

# Documented NULL controls -- defects the brief names that this architecture
# cannot exhibit, recorded here rather than dropped, because "we did not test
# it" and "it cannot happen" are different statements.
NULL_CONTROLS = [
    ("skip required synchronization before downstream p' consumption",
     "There is no synchronization to skip. Every device-to-device carry in the "
     "resident path -- copyToWorkspaceRhs, copyToWorkspaceGuess, "
     "copyWorkspaceSolution, carryFieldDevice -- is a cudaMemcpy issued on the "
     "DEFAULT stream, and every kernel that produces or consumes those buffers "
     "is launched on the same stream. Stream ordering is the guarantee; there is "
     "no explicit cudaDeviceSynchronize between a resident solve and the "
     "corrections that read its result, so no mutation can remove one. The "
     "positive evidence is compute-sanitizer --tool synccheck over the resident "
     "workload, which examines exactly this."),
    ("stale p, stale face flux, stale p', incorrect authority flag, forced "
     "full-field field re-upload",
     "Already covered by EXECUTED controls in the predecessor gates -- pf2, pf4, "
     "pf6, pf7 (persistent fields) and rp4, rp5, rp7 (resident pressure solve). "
     "Their logs are in those gates' negative-controls directories. Re-running "
     "them would cost time and add no information; rl6 and rl7 above are the "
     "SIMPLE-loop analogues of pf6 and pf7 on the boundary this gate moved."),
]


def check():
    bad = 0
    for control in CONTROLS:
        source = (ROOT / control["file"]).read_text(encoding="utf-8")
        n = source.count(control["anchor"])
        if n != 1:
            bad += 1
            print(f"  ANCHOR x{n}  {control['id']:36s} {control['file']}")
            print(f"    {control['anchor'].splitlines()[0]!r}")
    print(f"\n{len(CONTROLS)} controls, {bad} bad anchors")
    print(f"observable: {sum(1 for x in CONTROLS if x['expect'] == 'detect')}")
    print(f"documented null: {len(NULL_CONTROLS)}")
    for name, why in NULL_CONTROLS:
        print(f"  NULL  {name}")
        print(f"        {why.splitlines()[0]}")
    return 1 if bad else 0


if __name__ == "__main__":
    if "--check" in sys.argv:
        raise SystemExit(check())
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1].split(",")
    selected = [x for x in CONTROLS if only is None or x["id"] in only]
    ok, _ = execute(selected,
                    "../../gpu-pipe-001/final-residency/negative-controls",
                    "GPU-PIPE-001 final residency negative controls")
    raise SystemExit(0 if ok else 1)
