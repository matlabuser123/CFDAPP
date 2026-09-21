#!/usr/bin/env python3
"""GPU-DISC-001M negative controls -- ROUTING and INTEGRATION level.

These mutate the PRODUCTION integration -- SIMPLE.cpp's dispatch and the
GpuSimpleDiscretization plumbing -- not an operator's arithmetic, which
GPU-DISC-001B..001K already cover. What this gate must catch is a production
path that routes a stage back to the CPU, reuses stale device state, skips a
required upload or download, or dispatches to the wrong backend.

The harness detects them by comparing the production solve with the flag on
against the same solve with it off: those must be BITWISE identical, so any
routing mistake shows up.

Each control: inject -> rebuild -> run -> restore -> sha256 -> re-run.
A control that does not compile is reported as ILL-FORMED, never as a pass.
"""

import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
EVID = ROOT / "results/gpu-disc-001/integrated-simple/negative-control"
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
HARNESS = (ROOT / "results/gpu-disc-001/integrated-simple/tools"
                  "/integrated_simple_equivalence.cpp")
BIN = pathlib.Path("/tmp/integrated_simple_nc")

SIMPLE = "src/pressure_velocity/SIMPLE.cpp"
FACADE = "cuda/kernels/GpuSimpleDiscretizationCuda.cpp"

NL = "\n"

CONTROLS = [
    ("m1_momentum_forced_back_to_cpu", SIMPLE,
     "the momentum assembly is routed back to the CPU while every other stage "
     "stays on the device -- the mixed path the all-or-nothing rule forbids",
     "      if (useGpuDiscretization) {" + NL +
     "        // GPU-DISC-001M: the verified device assembly (GPU-DISC-001F). The",
     "      if (false) {" + NL +
     "        // GPU-DISC-001M: the verified device assembly (GPU-DISC-001F). The",
     ["--quick"], "detect"),

    ("m2_velocity_correction_forced_back_to_cpu", SIMPLE,
     "the velocity and face-flux corrections are routed back to the CPU",
     "    if (useGpuDiscretization) {" + NL +
     "      // GPU-DISC-001J and 001K, from the resident predictor velocity,",
     "    if (false) {" + NL +
     "      // GPU-DISC-001J and 001K, from the resident predictor velocity,",
     ["--quick"], "detect"),

    ("m3_skipped_iteration_upload", SIMPLE,
     "beginIteration is skipped, so the device runs on the PREVIOUS iteration's "
     "fields -- a stale device state",
     "      if (useGpuDiscretization) {" + NL +
     "        gpuDiscretization.beginIteration(velocity, pressure, massFlux, *effectiveViscosity);" + NL +
     "      }",
     "      if (false) {" + NL +
     "        gpuDiscretization.beginIteration(velocity, pressure, massFlux, *effectiveViscosity);" + NL +
     "      }",
     ["--quick"], "detect"),

    ("m4_missing_momentum_solution_upload", SIMPLE,
     "the solved predictor is never uploaded, so every stage after the momentum "
     "solve reads a stale velocityStar",
     "    if (useGpuDiscretization) {" + NL +
     "      gpuDiscretization.setMomentumSolution(0, uResult.solution);",
     "    if (false) {" + NL +
     "      gpuDiscretization.setMomentumSolution(0, uResult.solution);",
     ["--quick"], "detect"),

    ("m5_missing_pressure_correction_upload", SIMPLE,
     "p' is never uploaded, so both corrections use a stale pressure correction",
     "    if (useGpuDiscretization) gpuDiscretization.setPressureCorrection(pResult.solution);",
     "    if (false) gpuDiscretization.setPressureCorrection(pResult.solution);",
     ["--quick"], "detect"),

    ("m6_incorrect_backend_dispatch", SIMPLE,
     "the dispatch flag is inverted: a CPU-configured solve takes the device "
     "path and vice versa",
     "  bool useGpuDiscretization = false;" + NL +
     "  if (settings_.enableGpuDiscretization) {",
     "  bool useGpuDiscretization = false;" + NL +
     "  if (!settings_.enableGpuDiscretization) {",
     ["--quick"], "detect"),

    ("m7_stage_order_swapped", FACADE,
     "the response coefficients are computed from the WRONG component's "
     "diagonal -- a stage wired to the wrong source",
     "  computeMomentumResponseCoefficientDevice(mesh, impl_->systemU.diagonal, impl_->responseU);" + NL +
     "  computeMomentumResponseCoefficientDevice(mesh, impl_->systemV.diagonal, impl_->responseV);",
     "  computeMomentumResponseCoefficientDevice(mesh, impl_->systemV.diagonal, impl_->responseU);" + NL +
     "  computeMomentumResponseCoefficientDevice(mesh, impl_->systemU.diagonal, impl_->responseV);",
     ["--quick"], "null"),

    ("m8_predictor_flux_from_old_velocity", FACADE,
     "the predicted face flux is computed from the START-OF-ITERATION velocity "
     "instead of the momentum predictor",
     "    calculateMassFluxDevice(impl_->faceFlux, impl_->velocityStar, density, impl_->predictorFlux);",
     "    calculateMassFluxDevice(impl_->faceFlux, impl_->velocity, density, impl_->predictorFlux);",
     ["--quick"], "detect"),

    ("m9_rhie_chow_from_old_velocity", FACADE,
     "the Rhie-Chow predictor uses the start-of-iteration velocity instead of "
     "the momentum predictor",
     "  rhieChowMassFluxDevice(impl_->faceFlux, impl_->velocityStar, impl_->pressure, impl_->gradPx,",
     "  rhieChowMassFluxDevice(impl_->faceFlux, impl_->velocity, impl_->pressure, impl_->gradPx,",
     ["--quick"], "detect"),

    ("m10_flux_correction_uses_carried_flux", FACADE,
     "the face-flux correction is applied to the CARRIED mass flux instead of "
     "this iteration's predictor",
     "  correctFaceMassFluxDevice(impl_->pressureCorrection.mesh(), impl_->predictorFlux,",
     "  correctFaceMassFluxDevice(impl_->pressureCorrection.mesh(), impl_->massFlux,",
     ["--quick"], "detect"),

    ("m11_fallback_claims_gpu", SIMPLE,
     "a fallback reports gpuDiscretization = true -- the dispatch record lying "
     "about what ran",
     "    result.gpuDiscretization = useGpuDiscretization;",
     "    result.gpuDiscretization = true;",
     ["--quick"], "detect"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(fatal):
    r = subprocess.run(["ninja", "-C", str(BUILD)], cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        message = (r.stdout + r.stderr)[-3000:]
        if fatal:
            print(message)
            raise SystemExit("build failed while restoring -- tree may be dirty")
        return False, message
    r = subprocess.run(
        ["/usr/bin/c++", f"-I{ROOT}/include", f"-I{BUILD}/generated/include",
         f"-I{BUILD}/_deps/nlohmann_json-src/include", "-isystem", f"{CUDA}/include",
         "-O2", "-DNDEBUG", "-std=c++20", "-o", str(BIN), str(HARNESS),
         "-Wl,--start-group", str(BUILD / "cuda/libcfdcuda.a"),
         str(BUILD / "src/libcfdcore.a"), "-Wl,--end-group",
         f"-L{CUDA}/lib64", "-lcudart"],
        cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        message = (r.stdout + r.stderr)[-3000:]
        if fatal:
            print(message)
            raise SystemExit("link failed while restoring -- tree may be dirty")
        return False, message
    return True, ""


def localise(output):
    """GPU-DISC-001P: where the harness says the divergence FIRST appears.

    "the final result differs" does not localise an integration control. The
    harness now attributes the earliest divergent outer iteration to the SIMPLE
    stage whose residual history carries it; this lifts that out for the record.
    """
    iteration = re.search(r"first divergent iteration: (\S+)", output)
    stage = re.search(r"first divergent stage: (.+?)\s{3}first divergent metric: (.+?)\s*$",
                      output, re.M)
    if not iteration:
        return ""
    if stage:
        return ("iteration " + iteration.group(1) + ", stage '" + stage.group(1).strip() +
                "', metric '" + stage.group(2).strip() + "'")
    return "iteration " + iteration.group(1)


def main():
    EVID.mkdir(parents=True, exist_ok=True)
    targets = {SIMPLE, FACADE}
    baseline = {t: (ROOT / t).read_bytes().decode("utf-8") for t in targets}
    baseline_sha = {t: sha(ROOT / t) for t in targets}
    for t in sorted(targets):
        print("baseline " + baseline_sha[t] + "  " + t)
    print("")

    results = []
    for name, target, description, old, new, args, expectation in CONTROLS:
        print("=== " + name + " (" + expectation + ") ===")
        path = ROOT / target
        occurrences = baseline[target].count(old)
        if occurrences != 1:
            print("  !! anchor occurs " + str(occurrences) + " times in " + target)
            results.append((name, expectation, "ANCHOR-x" + str(occurrences), False, False))
            continue

        path.write_bytes(baseline[target].replace(old, new, 1).encode("utf-8"))
        built, message = build(fatal=False)
        if built:
            m = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
            verdict = "DETECTED" if m.returncode != 0 else "UNDETECTED"
            where = localise(m.stdout)
            body = ("# verdict: " + verdict + " (exit " + str(m.returncode) + ")\n"
                    "# first divergence: " + (where or "n/a") + "\n\n"
                    + m.stdout + m.stderr)
            print("  mutated: " + verdict + ("  " + where if where else ""))
        else:
            verdict, where = "BUILD-FAILED", ""
            body = "# verdict: BUILD-FAILED -- the mutation is ill-formed\n\n" + message
            print("  mutated: BUILD-FAILED (the mutation is ill-formed, not the integration)")
        (EVID / (name + "_mutated.log")).write_text(
            "# " + name + "  (expectation: " + expectation + ")\n# " + description +
            "\n# mutation applied to " + target + "\n# --- anchor ---\n" + old +
            "\n# --- became ---\n" + new + "\n" + body, encoding="utf-8")

        path.write_bytes(baseline[target].encode("utf-8"))
        build(fatal=True)
        ok_sha = sha(path) == baseline_sha[target]
        r2 = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
        (EVID / (name + "_restored.log")).write_text(
            "# " + name + " -- restored " + target + "\n# sha256 " + sha(path) +
            "\n# matches baseline: " + str(ok_sha) + "\n\n" + r2.stdout + r2.stderr,
            encoding="utf-8")
        print("  restored: sha-match=" + str(ok_sha) + " exit=" + str(r2.returncode))
        results.append((name, expectation, verdict, ok_sha, r2.returncode == 0, where))

    print("\n=== summary ===")
    ok = True
    detected = 0
    observable = sum(1 for c in CONTROLS if c[6] == "detect")
    nulls = len(CONTROLS) - observable
    for name, expectation, verdict, sha_ok, repassed, where in results:
        flag = "ok"
        if not sha_ok or not repassed:
            flag, ok = "RESTORE-FAILED", False
        elif verdict == "BUILD-FAILED":
            flag, ok = "ILL-FORMED-MUTATION", False
        elif expectation == "null":
            if verdict == "DETECTED":
                flag, ok = "NULL-CLAIM-WRONG", False
        elif verdict != "DETECTED":
            flag, ok = "GAP", False
        else:
            detected += 1
        print("  " + name.ljust(40) + " " + expectation.ljust(7) + " " + verdict.ljust(11) +
              " sha=" + str(sha_ok) + " repass=" + str(repassed) + "  " +
              flag.ljust(16) + " " + where)

    print("")
    print("observable controls: " + str(observable))
    print("detected observable controls: " + str(detected) + "/" + str(observable))
    print("documented null controls: " + str(nulls))
    return 0 if ok and detected == observable else 1


if __name__ == "__main__":
    sys.exit(main())
