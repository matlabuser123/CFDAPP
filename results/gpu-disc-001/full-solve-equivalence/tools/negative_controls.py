#!/usr/bin/env python3
"""GPU-DISC-001N negative controls -- FULL-SOLVE level.

These break equivalence ACROSS ITERATIONS: a stage skipped every N iterations,
a stale field carried forward, an update omitted. Operator arithmetic
(001B-001K) and single-iteration wiring (001L, 001M) are already covered; what
this gate must catch is a defect that only shows up once iterations couple.

Each control declares the outer iteration and the stage at which the divergence
should FIRST appear, and the harness reports both. A control detected at the
wrong iteration is a failure: the point of the history and per-iteration field
layers is that they LOCALISE, not merely that they notice.

Each control: inject -> rebuild -> run -> restore -> sha256 -> re-run.
"""

import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
EVID = ROOT / "results/gpu-disc-001/full-solve-equivalence/negative-controls"
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
HARNESS = (ROOT / "results/gpu-disc-001/full-solve-equivalence/tools"
                  "/full_solve_equivalence.cpp")
BIN = pathlib.Path("/tmp/full_solve_nc")

SIMPLE = "src/pressure_velocity/SIMPLE.cpp"
FACADE = "cuda/kernels/GpuSimpleDiscretizationCuda.cpp"

NL = "\n"

# SIMPLE's own per-solve loop variable is used for "every Nth iteration"
# instead of a counter. A `static` counter persists across solves within one
# process, and the harness runs each case many times, so the first version of
# these controls fired at unpredictable iterations -- three were undetected and
# three crashed. `outerIteration` (SIMPLE.cpp:290, = iteration + 1) is in scope
# at every anchor below and resets with each solve.

# (id, file, description, anchor, replacement, expected first iteration)
CONTROLS = [
    ("n1_stale_face_flux_every_5", SIMPLE,
     "every 5th outer iteration commits the PREVIOUS iteration's mass flux "
     "instead of the corrected one",
     "      gpuDiscretization.correctFaceMassFlux(settings_.nonOrthogonalCorrections > 1, fluxNew);",
     "      gpuDiscretization.correctFaceMassFlux(settings_.nonOrthogonalCorrections > 1, fluxNew);" + NL +
     "      if (outerIteration % 5 == 0) fluxNew = massFlux;",
     5),

    ("n2_stale_pprime_one_iteration", SIMPLE,
     "the pressure correction upload is skipped on the 3rd outer iteration, so "
     "that iteration's corrections use the PREVIOUS p'",
     "    if (useGpuDiscretization) gpuDiscretization.setPressureCorrection(pResult.solution);",
     "    if (useGpuDiscretization && outerIteration != 3)" + NL +
     "      gpuDiscretization.setPressureCorrection(pResult.solution);",
     3),

    ("n3_wrong_velocity_component_uploaded", SIMPLE,
     "the U predictor is uploaded into the V slot -- a component mix-up that is "
     "correctly sized, so it corrupts rather than crashes",
     "      gpuDiscretization.setMomentumSolution(1, vResult.solution);",
     "      gpuDiscretization.setMomentumSolution(1, uResult.solution);",
     1),

    ("n4_stale_response_coefficients", SIMPLE,
     "the response coefficients are computed on the first outer iteration only "
     "and reused for the rest of the solve",
     "      gpuDiscretization.computeResponseCoefficients(threeDimensional);",
     "      if (outerIteration == 1) gpuDiscretization.computeResponseCoefficients(threeDimensional);",
     2),

    # The first version skipped the update on BOTH arms, so the two stayed
    # identical and the harness correctly saw nothing -- the mutation was not a
    # backend-routing defect at all. Gated on useGpuDiscretization it is one.
    ("n5_skip_pressure_update_every_7", SIMPLE,
     "the pressure update is skipped on every 7th outer iteration OF THE GPU "
     "ARM only",
     "      pressureNew[cell.id()] = pressure[cell.id()] + (relaxation.pressure * pPrime[cell.id()]);",
     "      pressureNew[cell.id()] = (useGpuDiscretization && outerIteration % 7 == 0)" + NL +
     "                                   ? pressure[cell.id()]" + NL +
     "                                   : pressure[cell.id()] + (relaxation.pressure * pPrime[cell.id()]);",
     7),

    ("n6_predictor_flux_from_previous_iteration", FACADE,
     "the pressure correction is assembled from the PREVIOUS iteration's mass "
     "flux instead of this iteration's predicted flux",
     "  assemblePressureCorrectionDevice(" + NL +
     "      impl_->pressureCorrection, impl_->predictorFlux, impl_->responseU, impl_->responseV,",
     "  assemblePressureCorrectionDevice(" + NL +
     "      impl_->pressureCorrection, impl_->massFlux, impl_->responseU, impl_->responseV,",
     1),

    ("n7_mixed_path_one_iteration", SIMPLE,
     "one outer iteration is forced through a CPU/GPU mixed path: the velocity "
     "and flux corrections run on the CPU while every other stage stays on the "
     "device",
     "    if (useGpuDiscretization) {" + NL +
     "      // GPU-DISC-001J and 001K, from the resident predictor velocity,",
     "    if (useGpuDiscretization && outerIteration != 4) {" + NL +
     "      // GPU-DISC-001J and 001K, from the resident predictor velocity,",
     # Detected by an EXPLICIT error, not an iteration number, so the expectation
     # is None. On the device path dU/dV are deliberately left empty -- nothing
     # there reads a host copy -- so falling into the CPU correction branch
     # throws "field size does not match mesh cell count" rather than quietly
     # computing something wrong. That is the all-or-nothing design working:
     # a half-GPU iteration cannot run, it can only fail.
     None),

    ("n8_missing_upload_is_explicit", SIMPLE,
     "the V predictor is never uploaded at all. This is the control that found "
     "the missing residency guard: it used to fail as a CUDA illegal memory "
     "access from inside a kernel, and now fails explicitly. Detected by the "
     "error, not by an iteration number, so its expectation is None.",
     "      gpuDiscretization.setMomentumSolution(1, vResult.solution);",
     "      // MUTATED: V is never uploaded.",
     None),
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


def firstIteration(output):
    """The earliest 'first divergent iteration' the harness reports."""
    found = []
    for m in re.finditer(r"first divergent iteration: (\S+)", output):
        token = m.group(1).strip()
        if token != "none":
            try:
                found.append(int(token))
            except ValueError:
                pass
    return min(found) if found else None


def firstStageAndMetric(output):
    """GPU-DISC-001P: the stage and metric the harness attributes the earliest
    divergence to. The iteration alone does not localise a control -- it says
    when, not what."""
    m = re.search(r"first divergent stage: (.+?)\s{3}first divergent metric: (.+?)\s*$",
                  output, re.M)
    return (m.group(1).strip(), m.group(2).strip()) if m else ("", "")


def main():
    EVID.mkdir(parents=True, exist_ok=True)
    targets = {SIMPLE, FACADE}
    baseline = {t: (ROOT / t).read_bytes().decode("utf-8") for t in targets}
    baseline_sha = {t: sha(ROOT / t) for t in targets}
    for t in sorted(targets):
        print("baseline " + baseline_sha[t] + "  " + t)
    print("")

    results = []
    for name, target, description, old, new, expectedIteration in CONTROLS:
        print("=== " + name + " ===")
        path = ROOT / target
        occurrences = baseline[target].count(old)
        if occurrences != 1:
            print("  !! anchor occurs " + str(occurrences) + " times in " + target)
            results.append((name, "ANCHOR-x" + str(occurrences), None, expectedIteration,
                            False, False))
            continue

        path.write_bytes(baseline[target].replace(old, new, 1).encode("utf-8"))
        built, message = build(fatal=False)
        if built:
            m = subprocess.run([str(BIN), "--controls"], cwd=ROOT, text=True, capture_output=True)
            verdict = "DETECTED" if m.returncode != 0 else "UNDETECTED"
            observed = firstIteration(m.stdout)
            stage, metric = firstStageAndMetric(m.stdout)
            body = ("# verdict: " + verdict + " (exit " + str(m.returncode) + ")\n"
                    "# first divergent iteration observed: " + str(observed) + "\n"
                    "# expected:                          " + str(expectedIteration) + "\n"
                    "# first divergent stage:              " + (stage or "n/a") + "\n"
                    "# first divergent metric:             " + (metric or "n/a") + "\n\n"
                    + m.stdout + m.stderr)
            print("  mutated: " + verdict + "  first divergent iteration: " + str(observed) +
                  " (expected " + str(expectedIteration) + ")" +
                  ("  stage: " + stage + "  metric: " + metric if stage else ""))
        else:
            verdict, observed, stage, metric = "BUILD-FAILED", None, "", ""
            body = "# verdict: BUILD-FAILED -- the mutation is ill-formed\n\n" + message
            print("  mutated: BUILD-FAILED (the mutation is ill-formed, not the integration)")
        (EVID / (name + "_mutated.log")).write_text(
            "# " + name + "\n# " + description + "\n# mutation applied to " + target +
            "\n# --- anchor ---\n" + old + "\n# --- became ---\n" + new + "\n" + body,
            encoding="utf-8")

        path.write_bytes(baseline[target].encode("utf-8"))
        build(fatal=True)
        ok_sha = sha(path) == baseline_sha[target]
        r2 = subprocess.run([str(BIN), "--controls"], cwd=ROOT, text=True, capture_output=True)
        (EVID / (name + "_restored.log")).write_text(
            "# " + name + " -- restored " + target + "\n# sha256 " + sha(path) +
            "\n# matches baseline: " + str(ok_sha) + "\n\n" + r2.stdout + r2.stderr,
            encoding="utf-8")
        print("  restored: sha-match=" + str(ok_sha) + " exit=" + str(r2.returncode))
        results.append((name, verdict, observed, expectedIteration, ok_sha, r2.returncode == 0,
                        stage))

    print("\n=== summary ===")
    ok = True
    detected = 0
    localised = 0
    for name, verdict, observed, expected, sha_ok, repassed, stage in results:
        flag = "ok"
        if not sha_ok or not repassed:
            flag, ok = "RESTORE-FAILED", False
        elif verdict == "BUILD-FAILED":
            flag, ok = "ILL-FORMED-MUTATION", False
        elif verdict != "DETECTED":
            flag, ok = "GAP", False
        else:
            detected += 1
            if observed == expected:
                localised += 1
            else:
                flag, ok = "WRONG-ITERATION", False
        print("  " + name.ljust(40) + " " + verdict.ljust(11) + " first=" + str(observed).ljust(6) +
              " expected=" + str(expected).ljust(6) + " sha=" + str(sha_ok) +
              " repass=" + str(repassed) + "  " + flag.ljust(16) +
              " stage=" + (stage or "explicit error"))

    print("")
    print("observable controls: " + str(len(CONTROLS)))
    print("detected: " + str(detected) + "/" + str(len(CONTROLS)))
    print("detected AT THE EXPECTED ITERATION: " + str(localised) + "/" + str(len(CONTROLS)))
    print("documented null controls: 0")
    return 0 if ok and detected == len(CONTROLS) and localised == len(CONTROLS) else 1


if __name__ == "__main__":
    sys.exit(main())
