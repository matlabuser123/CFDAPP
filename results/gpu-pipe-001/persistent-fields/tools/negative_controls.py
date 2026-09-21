#!/usr/bin/env python3
"""GPU-PIPE-001 Persistent Fields -- residency negative controls.

Reuses the GPU-DISC-001P control engine, so the Phase D discipline (record
sha256, one mutation, rebuild, run the narrowest detector, restore, verify
sha256, rebuild, re-pass) is the same one already qualified rather than a second
implementation of it.

WHAT IS DIFFERENT HERE: a residency fault does not always change a number. Two
of these controls produce numerically IDENTICAL results and are only visible as
a transfer-count regression -- so the suite needs a detector that counts
transfers, not just one that compares fields. `transfer_guard.py` is that
detector, and controls declaring `harnesses: ["transfers"]` are checked with it.
"""

import sys

TOOLS = "/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/gpu-disc-001/negative-controls/tools"
sys.path.insert(0, TOOLS)

from control_engine import NL, ROOT, execute  # noqa: E402

SIMPLE = "src/pressure_velocity/SIMPLE.cpp"
FACADE = "cuda/kernels/GpuSimpleDiscretizationCuda.cpp"
KERNEL = "cuda/kernels/DevicePersistentFieldsKernel.cu"
CMAKE = "cuda/CMakeLists.txt"

# The GPU-DISC harnesses detect numerical divergence. `full_solve` is the one
# that compares whole residual histories across many iterations, which is where
# a stale carry shows up.
NUM = ["full_solve"]
ARGS = {"full_solve": ["--quick"]}


def c(cid, layer, file, desc, anchor, become, harnesses, args=None, expect="detect"):
    return {"id": cid, "layer": layer, "origin": "GPU-PIPE-001 persistent fields", "file": file,
            "desc": desc, "anchor": anchor, "become": become, "harnesses": harnesses,
            "expect": expect, "args": args or ARGS, "first": None}


CONTROLS = [
    c("pf1_skip_initial_upload", "residency", SIMPLE,
      "the one-time upload is skipped, so every device stage runs on buffers that "
      "were never filled -- the residency equivalent of a missing beginIteration",
      "          gpuDiscretization.uploadInitialState(velocity, pressure, massFlux, "
      "*effectiveViscosity);",
      "          // MUTATED: the initial upload is skipped.",
      NUM),

    c("pf2_pressure_carry_omitted", "residency", FACADE,
      "updatePressure computes the new pressure but never carries it back, so the "
      "resident pressure is frozen at its initial value for the whole solve",
      "  carryFieldDevice(impl_->cellCount, impl_->pressureNext, impl_->pressure);",
      "  // MUTATED: the updated pressure is never carried back.",
      NUM),

    c("pf3_velocity_carry_omitted", "residency", FACADE,
      "the corrected velocity is computed but never carried into the persistent "
      "field, so every iteration after the first assembles from a stale velocity",
      "  carryFieldDevice(nc, impl_->correctedVelocity.x, impl_->velocity.x);",
      "  // MUTATED: the corrected u is never carried into the resident velocity.",
      NUM),

    c("pf4_flux_carry_omitted", "residency", FACADE,
      "the corrected face flux is never carried into the persistent mass flux, so "
      "the convection term uses the start-of-solve flux forever",
      "  carryFieldDevice(impl_->faceCount, impl_->correctedFlux, impl_->massFlux);",
      "  // MUTATED: the corrected flux is never carried into the resident massFlux.",
      NUM),

    c("pf5_fmad_on_pressure_update", "residency", CMAKE,
      "the persistent-field kernel loses -fmad=false, so the device pressure update "
      "contracts p + alpha*p' into a single FMA and stops matching the CPU bitwise",
      "  kernels/DevicePersistentFieldsKernel.cu" + NL +
      "  PROPERTIES COMPILE_OPTIONS \"-fmad=false\"",
      "  PROPERTIES COMPILE_OPTIONS \"-fmad=false\"",
      NUM),

    c("pf6_authority_lies_after_device_write", "residency", FACADE,
      "updatePressure leaves the authority at Synchronized after writing the field "
      "on the device -- the record claiming the host copy is current when it is not",
      "  impl_->pressureAuthority = FieldAuthority::DeviceOwned;",
      "  impl_->pressureAuthority = FieldAuthority::Synchronized;",
      ["dirty_state"], {"dirty_state": []}),

    c("pf7_reupload_every_iteration", "residency", SIMPLE,
      "the initial upload is repeated EVERY iteration -- numerically identical, and "
      "invisible to every equivalence gate. Only a transfer count detects it, which "
      "is the whole reason this gate needs a transfer detector.",
      "          gpuDiscretization.beginIterationResident();",
      "          gpuDiscretization.uploadInitialState(velocity, pressure, massFlux, "
      "*effectiveViscosity);",
      ["transfers"], {"transfers": []}),

    c("pf8_stale_viscosity_never_refreshed", "residency", SIMPLE,
      "the viscosity is never re-uploaded even when the turbulence model changes it, "
      "so a non-laminar solve runs on the first iteration's mu_eff forever",
      "            gpuDiscretization.setViscosity(*effectiveViscosity);" + NL +
      "            previousViscosity = *effectiveViscosity;",
      "            previousViscosity = *effectiveViscosity;",
      NUM, expect="null"),
]


def check():
    bad = 0
    for control in CONTROLS:
        source = (ROOT / control["file"]).read_text(encoding="utf-8")
        n = source.count(control["anchor"])
        if n != 1:
            bad += 1
            print(f"  ANCHOR x{n}  {control['id']:44s} {control['file']}")
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
    ok, _ = execute(selected, "../../gpu-pipe-001/persistent-fields/negative-controls",
                    "GPU-PIPE-001 persistent-field negative controls")
    raise SystemExit(0 if ok else 1)
