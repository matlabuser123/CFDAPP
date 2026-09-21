#!/usr/bin/env python3
"""GPU-DISC-001K negative controls.

Each control: inject a single-point mutation, rebuild, run the differential,
restore, rebuild, verify the restored file's sha256 against the baseline, and
re-run to confirm it passes again.

Each control declares what it expects:
  "detect" -- the mutation must change the result. An UNDETECTED one is a gap
              in the gate, not a pass.
  "null"   -- the mutation provably cannot change any result under the
              production contract (documented in summary.md with the proof).
              Still run, to check the claim; a "null" control that IS detected
              means the proof was wrong, which fails the run. Null controls are
              excluded from the detected-control denominator.
"""

import hashlib
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
EVID = ROOT / "results/gpu-disc-001/face-flux-correction/negative-control"
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
HARNESS = (ROOT / "results/gpu-disc-001/face-flux-correction/tools"
                  "/face_flux_correction_equivalence.cpp")
OBJ = pathlib.Path("/tmp/ffcorr_equiv.o")
BIN = pathlib.Path("/tmp/ffcorr_equiv_nc")

KERNEL = "cuda/kernels/DeviceFaceFluxCorrectionKernel.cu"

NL = "\n"

CONTROLS = [
    ("k1_correction_sign_reversed", KERNEL,
     "the correction subtracted instead of added",
     "  Real value = predictorMassFlux[f] + fluxCorrection;",
     "  Real value = predictorMassFlux[f] - fluxCorrection;",
     ["--quick"], "detect"),

    ("k2_owner_neighbour_swapped", KERNEL,
     "the pressure-correction difference taken neighbour minus owner",
     "  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);",
     "  const Real fluxCorrection = faceCoefficient[f] * (pNeighbor - pOwner);",
     ["--quick"], "detect"),

    ("k3_wrong_pprime_difference", KERNEL,
     "the face jump replaced by the owner value alone, dropping the neighbour",
     "  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);",
     "  const Real fluxCorrection = faceCoefficient[f] * pOwner;",
     ["--quick"], "detect"),

    ("k4_wrong_face_coefficient", KERNEL,
     "the coefficient of the NEXT face used, so the matrix and the flux update "
     "no longer share one number",
     "  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);",
     "  const Real fluxCorrection = faceCoefficient[(f + 1) % faceCount] * (pOwner - pNeighbor);",
     ["--quick"], "detect"),

    ("k5_boundary_special_case_missing", KERNEL,
     "a boundary face reads a neighbour value instead of the Dirichlet 0.0 -- "
     "the owner's own p', which is what a missing boundary case looks like",
     "  const Real pNeighbor = neighbor == DeviceMesh::kNoNeighbor ? 0.0 : pressureCorrection[neighbor];",
     "  const Real pNeighbor = neighbor == DeviceMesh::kNoNeighbor ? pOwner : pressureCorrection[neighbor];",
     ["--quick"], "detect"),

    ("k6_correction_applied_twice", KERNEL,
     "the correction applied twice",
     "  Real value = predictorMassFlux[f] + fluxCorrection;",
     "  Real value = predictorMassFlux[f] + fluxCorrection + fluxCorrection;",
     ["--quick"], "detect"),

    ("k7_stale_pressure_correction", KERNEL,
     "the owner's p' read from the NEXT face's owner cell -- a stale/misindexed "
     "pressure correction. Indexed through faceOwner so it stays in range "
     "without needing a cell count the kernel does not take",
     "  const Real pOwner = pressureCorrection[faceOwner[f]];",
     "  const Real pOwner = pressureCorrection[faceOwner[(f + 1) % faceCount]];",
     ["--quick"], "detect"),

    ("k8_explicit_term_dropped", KERNEL,
     "the explicit non-orthogonal term dropped, so the corrected flux no longer "
     "matches what the pressure-correction RHS assumed",
     "  if (explicitFaceFlux != nullptr) value = value + explicitFaceFlux[f];",
     "  // MUTATED: the explicit term is gone.",
     ["--quick"], "detect"),

    ("k9_explicit_term_folded", KERNEL,
     "the explicit term folded into the correction instead of added separately: "
     "F* + (F' + E) rather than (F* + F') + E",
     "  Real value = predictorMassFlux[f] + fluxCorrection;" + NL +
     "  // A SEPARATE addition, matching the CPU's `+=`: (F* + F') + E." + NL +
     "  if (explicitFaceFlux != nullptr) value = value + explicitFaceFlux[f];",
     "  Real correction = fluxCorrection;" + NL +
     "  if (explicitFaceFlux != nullptr) correction = correction + explicitFaceFlux[f];" + NL +
     "  Real value = predictorMassFlux[f] + correction;",
     ["--quick"], "detect"),

    ("k10_density_factor_introduced", KERNEL,
     "a density factor introduced into the correction -- the double-count the "
     "audit says must not happen, since rho is already inside faceCoefficient",
     "  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);",
     "  const Real fluxCorrection = 998.2 * faceCoefficient[f] * (pOwner - pNeighbor);",
     ["--quick"], "detect"),

    # Originally classified "null" on the reasoning that short-circuiting a
    # coefficient of exactly 0.0 reaches the same answer by a different route.
    # That was WRONG and the driver caught it: production evaluates
    # F* + 0.0*(p'_owner - 0.0), which turns a -0.0 predictor into +0.0, while
    # the branch returns -0.0 unchanged. Numerically identical (maxAbs = 0),
    # bitwise different. So reproducing the CPU's arithmetic rather than
    # branching around a known no-op is load-bearing after all, and this is a
    # genuine observable control. See summary.md.
    ("k11_boundary_branch_instead_of_arithmetic", KERNEL,
     "a Neumann-like boundary face SHORT-CIRCUITED rather than evaluated -- "
     "numerically the same answer, but it preserves a -0.0 predictor the "
     "production arithmetic would have turned into +0.0",
     "  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);" + NL +
     "  Real value = predictorMassFlux[f] + fluxCorrection;",
     "  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);" + NL +
     "  Real value = faceCoefficient[f] == 0.0 ? predictorMassFlux[f]" + NL +
     "                                        : predictorMassFlux[f] + fluxCorrection;",
     ["--quick"], "detect"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rebuild_and_link(fatal=True):
    """Rebuild and relink. Returns (ok, message).

    A MUTATED build may legitimately fail to compile -- that is a badly written
    mutation, not a defect in the port. Returning instead of raising is what
    lets the caller restore the file before moving on: an abort here would
    leave the mutation in the working tree, which is exactly what happened on
    the first run of this driver.
    """
    r = subprocess.run(["ninja", "-C", str(BUILD)], cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        message = (r.stdout + r.stderr)[-3000:]
        if fatal:
            print(message)
            raise SystemExit("build failed while restoring -- tree may be dirty")
        return False, message
    r = subprocess.run(
        ["/usr/bin/c++", "-o", str(BIN), str(OBJ),
         str(BUILD / "cuda/libcfdcuda.a"), str(BUILD / "src/libcfdcore.a"),
         f"-L{CUDA}/lib64", "-lcudart"],
        cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        message = (r.stdout + r.stderr)[-3000:]
        if fatal:
            print(message)
            raise SystemExit("link failed while restoring -- tree may be dirty")
        return False, message
    return True, ""


def main():
    EVID.mkdir(parents=True, exist_ok=True)
    baseline = (ROOT / KERNEL).read_text(encoding="utf-8")
    baseline_sha = sha(ROOT / KERNEL)
    print(f"baseline {baseline_sha}  {KERNEL}")
    print(f"harness  {sha(HARNESS)}\n")

    r = subprocess.run(
        ["/usr/bin/c++", f"-I{ROOT}/include", f"-I{BUILD}/generated/include",
         f"-I{BUILD}/_deps/nlohmann_json-src/include", "-isystem", f"{CUDA}/include",
         "-O2", "-DNDEBUG", "-std=c++20", "-c", "-o", str(OBJ), str(HARNESS)],
        cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit("harness compile failed")

    results = []
    for name, target, description, old, new, args, expectation in CONTROLS:
        print(f"=== {name} ({expectation}) ===")
        path = ROOT / target
        occurrences = baseline.count(old)
        if occurrences != 1:
            print(f"  !! anchor occurs {occurrences} times in {target}")
            results.append((name, expectation, f"ANCHOR-x{occurrences}", False, False))
            continue

        path.write_text(baseline.replace(old, new, 1), encoding="utf-8")
        built, buildMessage = rebuild_and_link(fatal=False)
        if built:
            m = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
            verdict = "DETECTED" if m.returncode != 0 else "UNDETECTED"
            body = f"# verdict: {verdict} (exit {m.returncode})\n\n{m.stdout}{m.stderr}"
            print(f"  mutated: exit={m.returncode} -> {verdict}")
        else:
            # A mutation that does not compile proves nothing about the port.
            verdict = "BUILD-FAILED"
            body = f"# verdict: BUILD-FAILED -- the mutation is ill-formed\n\n{buildMessage}"
            print("  mutated: BUILD-FAILED (the mutation is ill-formed, not the port)")
        (EVID / f"{name}_mutated.log").write_text(
            f"# {name}  (expectation: {expectation})\n# {description}\n"
            f"# mutation applied to {target}\n"
            f"# --- anchor ---\n{old}\n# --- became ---\n{new}\n{body}",
            encoding="utf-8")

        path.write_text(baseline, encoding="utf-8")
        rebuild_and_link(fatal=True)
        ok_sha = sha(path) == baseline_sha
        r2 = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
        (EVID / f"{name}_restored.log").write_text(
            f"# {name} -- restored {target}\n# sha256 {sha(path)}\n"
            f"# matches baseline: {ok_sha}\n\n{r2.stdout}{r2.stderr}", encoding="utf-8")
        print(f"  restored: sha-match={ok_sha} exit={r2.returncode}")
        results.append((name, expectation, verdict, ok_sha, r2.returncode == 0))

    print("\n=== summary ===")
    ok = True
    detected = 0
    expected_detect = sum(1 for c in CONTROLS if c[6] == "detect")
    expected_null = sum(1 for c in CONTROLS if c[6] == "null")
    for name, expectation, verdict, sha_ok, repassed in results:
        flag = "ok"
        if not sha_ok or not repassed:
            flag, ok = "RESTORE-FAILED", False
        elif verdict == "BUILD-FAILED":
            flag, ok = "ILL-FORMED-MUTATION", False
        elif expectation == "detect":
            if verdict == "DETECTED":
                detected += 1
            else:
                flag, ok = "GAP", False
        elif verdict == "DETECTED":
            flag, ok = "NULL-CLAIM-WRONG", False
        print(f"  {name:42s} {expectation:7s} {verdict:11s} sha={sha_ok} repass={repassed}  {flag}")

    print(f"\nobservable controls: {expected_detect}")
    print(f"detected observable controls: {detected}/{expected_detect}")
    print(f"documented null controls: {expected_null}")
    return 0 if ok and detected == expected_detect else 1


if __name__ == "__main__":
    sys.exit(main())
