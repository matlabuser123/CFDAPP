#!/usr/bin/env python3
"""GPU-DISC-001L negative controls -- INTEGRATION level.

These mutate the CUDA LADDER's composition, not an operator's arithmetic: the
stages themselves are already covered by 001B-001K. What this gate has to
detect is a ladder that wires verified operators together wrongly -- a skipped
correction, a stale input, a reordered stage.

The mutations therefore go into the harness's own runGpuLadder, and each
declares the stage at which the divergence is expected to FIRST appear. A
control that is detected at the wrong stage is a failure: it means the
stage-by-stage report is not localising correctly, which is the whole point of
building the ladder rather than comparing only the final state.

Each control: inject -> rebuild -> run -> restore -> sha256 -> re-run.
"""

import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
EVID = ROOT / "results/gpu-disc-001/single-iteration/negative-control"
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
HARNESS = (ROOT / "results/gpu-disc-001/single-iteration/tools"
                  "/single_iteration_equivalence.cpp")
BIN = pathlib.Path("/tmp/singleiter_equiv_nc")

NL = "\n"

# (id, description, anchor, replacement, expected first divergent stage)
CONTROLS = [
    ("l1_skip_velocity_correction",
     "the velocity correction is skipped: the corrected velocity is left as the predictor",
     "  L.uNew = pull(corrected.x);" + NL +
     "  L.vNew = pull(corrected.y);" + NL +
     "  L.wNew = pull(corrected.z);",
     "  L.uNew = L.uStar;" + NL +
     "  L.vNew = L.vStar;" + NL +
     "  L.wNew = L.wStar;",
     "8 corrected U/V/W"),

    ("l2_skip_face_flux_correction",
     "the face-flux correction is skipped: the corrected flux is left as the predictor",
     "  L.fluxNew = pull(dFluxNew);",
     "  L.fluxNew = L.predictorFlux;",
     "9 corrected face flux"),

    # PROVABLY NULL, with a proof from the source rather than an observation:
    # the momentum MATRIX in this codebase is component-independent. In both
    # the diffusion and convection boundary terms the component enters only the
    # RHS, via selectComponent(uB, component), while the diagonal contribution
    # builder.add(ownerId, ownerId, coefficient) is added for every boundary
    # face regardless of condition type (MomentumEquation.cpp:136, :236, :298);
    # interior faces share one mass flux, geometry and viscosity. So
    # dU == dV == dW exactly, always, and substituting one for another cannot
    # change any result. The harness asserts that property on every case (the
    # "R" layer), so this control stops being null the moment it stops holding.
    ("l3_stale_response_coefficients",
     "the V response coefficient is reused for U -- provably null, see above",
     "  L.dU = pull(dU);" + NL +
     "  L.dV = pull(dV);",
     "  L.dU = pull(dV);" + NL +
     "  L.dV = pull(dV);",
     None),

    ("l4_previous_iteration_predicted_flux",
     "the pressure correction is assembled from the START-OF-ITERATION flux "
     "instead of this iteration's predicted flux",
     "  cfd::gpu::assemblePressureCorrectionDevice(plans.pressure, dPredictorFlux, dU, dV,",
     "  cfd::gpu::assemblePressureCorrectionDevice(plans.pressure, dMassFlux, dU, dV,",
     "5 pressure system"),

    ("l5_skip_pressure_update",
     "the pressure update is skipped: p_new is left at the old pressure",
     "    L.pressureNew[i] = c.pressure[i] + (settings.pressureRelaxation * L.pPrime[i]);",
     "    L.pressureNew[i] = c.pressure[i];",
     "7 updated pressure"),

    ("l6_reversed_pprime_sign",
     "p' is negated before the corrections consume it",
     "  dPPrime.uploadFrom(L.pPrime.data(), nc);",
     "  { std::vector<Real> flipped = L.pPrime;" + NL +
     "    for (Real& v : flipped) v = -v;" + NL +
     "    dPPrime.uploadFrom(flipped.data(), nc); }",
     "8 corrected U/V/W"),

    ("l7_correction_stage_order_swapped",
     "the face-flux correction consumes the CORRECTED velocity's flux instead of "
     "the predictor -- the classic stage-order swap",
     "  cfd::gpu::correctFaceMassFluxDevice(plans.pressure.mesh(), dPredictorFlux,",
     "  cfd::gpu::DeviceBuffer<Real> dCorrectedFlux;" + NL +
     "  cfd::gpu::calculateMassFluxDevice(plans.flux, corrected, c.fluid.density(), dCorrectedFlux);" + NL +
     "  cfd::gpu::correctFaceMassFluxDevice(plans.pressure.mesh(), dCorrectedFlux,",
     "9 corrected face flux"),

    ("l8_unrelaxed_pressure_update",
     "the pressure update drops the relaxation factor",
     "    L.pressureNew[i] = c.pressure[i] + (settings.pressureRelaxation * L.pPrime[i]);",
     "    L.pressureNew[i] = c.pressure[i] + L.pPrime[i];",
     "7 updated pressure"),

    ("l9_momentum_uses_updated_pressure",
     "the momentum assembly is fed a pressure that is not the start-of-iteration "
     "one -- a lagging/ordering error in the outer loop",
     "    dPressure.uploadFrom(p.data(), nc);",
     "    for (Index i = 0; i < nc; ++i) p[i] = p[i] * 1.0000001;" + NL +
     "    dPressure.uploadFrom(p.data(), nc);",
     "1 momentum systems"),

    ("l10_velocity_correction_uses_old_velocity",
     "the velocity correction is applied to the START-OF-ITERATION velocity "
     "instead of the predictor u*",
     "      plans.velocity, deviceVelocityStar, dU, dV, threeD ? &dW : nullptr, dPPrime,",
     "      plans.velocity, deviceVelocity, dU, dV, threeD ? &dW : nullptr, dPPrime,",
     "8 corrected U/V/W"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(fatal):
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
            raise SystemExit("build failed while restoring -- tree may be dirty")
        return False, message
    return True, ""


def firstDivergence(output):
    m = re.search(r"FIRST DIVERGENCE: (.+)", output)
    return m.group(1).strip() if m else None


def main():
    EVID.mkdir(parents=True, exist_ok=True)
    baseline = HARNESS.read_bytes().decode("utf-8")
    baseline_sha = sha(HARNESS)
    print(f"baseline {baseline_sha}  {HARNESS.name}\n")

    results = []
    for name, description, old, new, expectedStage in CONTROLS:
        print(f"=== {name} ===")
        occurrences = baseline.count(old)
        if occurrences != 1:
            print(f"  !! anchor occurs {occurrences} times")
            results.append((name, f"ANCHOR-x{occurrences}", "", False, False))
            continue

        HARNESS.write_bytes(baseline.replace(old, new, 1).encode("utf-8"))
        built, message = build(fatal=False)
        if built:
            m = subprocess.run([str(BIN), "--quick"], cwd=ROOT, text=True, capture_output=True)
            detected = m.returncode != 0
            stage = firstDivergence(m.stdout) or ""
            verdict = "DETECTED" if detected else "UNDETECTED"
            body = (f"# verdict: {verdict} (exit {m.returncode})\n"
                    f"# first divergence: {stage or '(none)'}\n"
                    f"# expected:         {expectedStage}\n\n{m.stdout}{m.stderr}")
            print(f"  mutated: {verdict}  first divergence: {stage or '(none)'}")
        else:
            verdict, stage = "BUILD-FAILED", ""
            body = f"# verdict: BUILD-FAILED -- the mutation is ill-formed\n\n{message}"
            print("  mutated: BUILD-FAILED (the mutation is ill-formed, not the ladder)")
        (EVID / f"{name}_mutated.log").write_text(
            f"# {name}\n# {description}\n# --- anchor ---\n{old}\n# --- became ---\n{new}\n{body}",
            encoding="utf-8")

        HARNESS.write_bytes(baseline.encode("utf-8"))
        build(fatal=True)
        ok_sha = sha(HARNESS) == baseline_sha
        r2 = subprocess.run([str(BIN), "--quick"], cwd=ROOT, text=True, capture_output=True)
        (EVID / f"{name}_restored.log").write_text(
            f"# {name} -- restored\n# sha256 {sha(HARNESS)}\n"
            f"# matches baseline: {ok_sha}\n\n{r2.stdout}{r2.stderr}", encoding="utf-8")
        print(f"  restored: sha-match={ok_sha} exit={r2.returncode}")
        results.append((name, verdict, stage, ok_sha, r2.returncode == 0))

    print("\n=== summary ===")
    ok = True
    detected = 0
    localised = 0
    observable = sum(1 for c in CONTROLS if c[4] is not None)
    nulls = len(CONTROLS) - observable
    for control, row in zip(CONTROLS, results):
        name = control[0]
        expected = control[4]
        _, verdict, stage, sha_ok, repassed = row
        flag = "ok"
        if not sha_ok or not repassed:
            flag, ok = "RESTORE-FAILED", False
        elif verdict == "BUILD-FAILED":
            flag, ok = "ILL-FORMED-MUTATION", False
        elif expected is None:
            if verdict == "DETECTED":
                flag, ok = "NULL-CLAIM-WRONG", False
        elif verdict != "DETECTED":
            flag, ok = "GAP", False
        else:
            detected += 1
            if stage == expected:
                localised += 1
            else:
                flag, ok = "WRONG-STAGE(got " + stage + ")", False
        shown = expected if expected is not None else "(null)"
        print("  " + name.ljust(42) + " " + verdict.ljust(11) + " stage=" + stage +
              " expected=" + shown + " sha=" + str(sha_ok) + " repass=" + str(repassed) +
              "  " + flag)

    print("")
    print("observable controls: " + str(observable))
    print("detected observable controls: " + str(detected) + "/" + str(observable))
    print("detected AT THE EXPECTED STAGE: " + str(localised) + "/" + str(observable))
    print("documented null controls: " + str(nulls))
    return 0 if ok and detected == observable and localised == observable else 1


if __name__ == "__main__":
    sys.exit(main())
