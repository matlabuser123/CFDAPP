#!/usr/bin/env python3
"""GPU-DISC-001J negative controls.

Each control: inject a single-point mutation, rebuild, run the differential,
restore, rebuild, verify the restored file's sha256 against the baseline, and
re-run to confirm it passes again.

Each control declares what it expects:
  "detect" -- the mutation must change the result. An UNDETECTED one is a gap
              in the gate, not a pass.
  "null"   -- the mutation provably cannot change any result on any mesh this
              repository can build (documented in summary.md with the proof).
              Still run, to check the claim holds; a "null" control that IS
              detected means the proof was wrong, which fails the run.
"""

import hashlib
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
EVID = ROOT / "results/gpu-disc-001/velocity-correction/negative-control"
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
HARNESS = (ROOT / "results/gpu-disc-001/velocity-correction/tools"
                  "/velocity_correction_equivalence.cpp")
OBJ = pathlib.Path("/tmp/vcorr_equiv.o")
BIN = pathlib.Path("/tmp/vcorr_equiv_nc")

VC_KERNEL = "cuda/kernels/DeviceVelocityCorrectionKernel.cu"
VC_PLAN = "cuda/kernels/DeviceVelocityCorrectionPlan.cpp"
LS_KERNEL = "cuda/kernels/DeviceLeastSquaresGradientKernel.cu"
LS_PLAN = "cuda/kernels/DeviceLeastSquaresGradientPlan.cpp"

NL = "\n"

CONTROLS = [
    ("h1_reversed_gradient_sign", VC_KERNEL,
     "the pressure-gradient term added instead of subtracted",
     "  outX[c] = ux[c] - (dU[c] * gradX[c]);",
     "  outX[c] = ux[c] + (dU[c] * gradX[c]);",
     ["--quick"], "detect"),

    ("h2_wrong_velocity_component", VC_KERNEL,
     "the v correction uses the x gradient",
     "  outY[c] = uy[c] - (dV[c] * gradY[c]);",
     "  outY[c] = uy[c] - (dV[c] * gradX[c]);",
     ["--quick"], "detect"),

    ("h3_wrong_response_index", VC_KERNEL,
     "the v correction uses the U response coefficient",
     "  outY[c] = uy[c] - (dV[c] * gradY[c]);",
     "  outY[c] = uy[c] - (dU[c] * gradY[c]);",
     ["--quick"], "detect"),

    ("h4_missing_response", VC_KERNEL,
     "the v correction drops the response coefficient entirely",
     "  outY[c] = uy[c] - (dV[c] * gradY[c]);",
     "  outY[c] = uy[c] - gradY[c];",
     ["--quick"], "detect"),

    ("h5_correction_applied_twice", VC_KERNEL,
     "the u correction is applied twice",
     "  outX[c] = ux[c] - (dU[c] * gradX[c]);",
     "  outX[c] = ux[c] - (dU[c] * gradX[c]) - (dU[c] * gradX[c]);",
     ["--quick"], "detect"),

    ("h6_stale_gradient", VC_KERNEL,
     "the p' gradient is not recomputed when the output buffers are already "
     "sized -- a stale gradient reused across calls",
     "  if (scheme == kGradientSchemeLeastSquares) {" + NL +
     "    leastSquaresGradientDevice(plan.leastSquares(), pressureCorrection, gradX, gradY, gradZ);" + NL +
     "  } else {",
     "  if (gradX.size() == cellCount) {" + NL +
     "    // MUTATED: reuse whatever is already there." + NL +
     "  } else if (scheme == kGradientSchemeLeastSquares) {" + NL +
     "    leastSquaresGradientDevice(plan.leastSquares(), pressureCorrection, gradX, gradY, gradZ);" + NL +
     "  } else {",
     ["--quick"], "detect"),

    ("h7_pressure_boundaries_for_pprime", VC_PLAN,
     "makeGradientBoundaries skipped: the RAW pressure boundary set is used for "
     "the p' gradient -- the operator-level form of correcting with p instead "
     "of p'",
     "    if (pressureBoundaries.get(patch.name()).type() ==" + NL +
     "        cfd::boundary::BoundaryConditionType::FixedValue) {" + NL +
     "      gradientBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));" + NL +
     "      ++fixedValuePatches_;",
     "    if (pressureBoundaries.get(patch.name()).type() ==" + NL +
     "        cfd::boundary::BoundaryConditionType::FixedValue) {" + NL +
     "      gradientBoundaries.set(mesh, patch.name()," + NL +
     "                             std::make_unique<cfd::boundary::FixedValue>(101325.0));" + NL +
     "      ++fixedValuePatches_;",
     ["--quick"], "detect"),

    ("h8_2d_w_preserved", VC_KERNEL,
     "the 2D branch preserves the predictor's z instead of zeroing it",
     "    // Vector2{x, y}: z is value-initialised, NOT carried over from uz." + NL +
     "    outZ[c] = 0.0;",
     "    outZ[c] = uz[c];",
     ["--quick"], "detect"),

    ("h9_factor_order_within_products", LS_KERNEL,
     "the 2x2 solve's factors reordered WITHIN each product -- the CPU source "
     "spells gx and gy asymmetrically, and this proves that spelling is "
     "cosmetic, since IEEE multiplication is commutative",
     "    gy = ((sxx * by) - (sxy * bx)) / determinant;",
     "    gy = ((by * sxx) - (bx * sxy)) / determinant;",
     ["--quick"], "null"),

    ("h9b_packed_entries_swapped", LS_KERNEL,
     "the packed 2x2 layout read backwards: gx takes Sxx and gy takes Syy",
     "    const Real syy = p.c11[c], sxy = p.c12[c], sxx = p.c22[c];",
     "    const Real syy = p.c22[c], sxy = p.c12[c], sxx = p.c11[c];",
     ["--quick"], "detect"),

    ("h9c_3d_cofactor_row_swapped", LS_KERNEL,
     "the 3D adjugate's second row uses the wrong cofactor (k13 for k12)",
     "    gy = ((k12 * bx) + (k22 * by) + (k23 * bz)) / determinant;",
     "    gy = ((k13 * bx) + (k22 * by) + (k23 * bz)) / determinant;",
     ["--quick"], "detect"),

    ("h10_fallback_dropped", LS_KERNEL,
     "the Green-Gauss fallback for an ill-conditioned cell is dropped",
     "  if (p.cellConditioned[c] == 0) return;",
     "  // MUTATED: the conditioning fallback is gone.",
     [], "null"),

    ("h11_oblique_uses_face_centroid", LS_PLAN,
     "an oblique-Neumann displacement uses the face centroid instead of the "
     "foot of the normal",
     "      const Vector3 displacement = oblique.applies" + NL +
     "                                       ? (oblique.unitNormal * oblique.normalDistance)" + NL +
     "                                       : (face.centroid() - cellCentroid);",
     "      const Vector3 displacement = face.centroid() - cellCentroid;",
     ["--quick"], "detect"),

    ("h12_zero_distance_skip_removed", LS_PLAN,
     "the zero-distance displacement skip is replaced by a guarded weight",
     "      if (!(distanceSquared > 0.0)) {",
     "      if (false) {",
     ["--quick"], "null"),

    ("h13_3d_weight_in_2d", LS_PLAN,
     "a 2D cell's weight includes the z term, so the branch-appropriate "
     "distance is lost",
     "      const Real distanceSquared =" + NL +
     "          cellIsThreeD ? ((dx * dx) + (dy * dy) + (dz * dz)) : ((dx * dx) + (dy * dy));",
     "      const Real distanceSquared = (dx * dx) + (dy * dy) + (dz * dz);",
     ["--quick"], "null"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rebuild_and_link():
    r = subprocess.run(["ninja", "-C", str(BUILD)], cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        print(r.stdout[-4000:], r.stderr[-4000:])
        raise SystemExit("build failed")
    r = subprocess.run(
        ["/usr/bin/c++", "-o", str(BIN), str(OBJ),
         str(BUILD / "cuda/libcfdcuda.a"), str(BUILD / "src/libcfdcore.a"),
         f"-L{CUDA}/lib64", "-lcudart"],
        cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit("link failed")


def main():
    EVID.mkdir(parents=True, exist_ok=True)
    targets = {VC_KERNEL, VC_PLAN, LS_KERNEL, LS_PLAN}
    baseline = {t: (ROOT / t).read_text(encoding="utf-8") for t in targets}
    baseline_sha = {t: sha(ROOT / t) for t in targets}
    for t in sorted(targets):
        print(f"baseline {baseline_sha[t]}  {t}")
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
        occurrences = baseline[target].count(old)
        if occurrences != 1:
            print(f"  !! anchor occurs {occurrences} times in {target}")
            results.append((name, expectation, f"ANCHOR-x{occurrences}", False, False))
            continue

        path.write_text(baseline[target].replace(old, new, 1), encoding="utf-8")
        rebuild_and_link()
        m = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
        verdict = "DETECTED" if m.returncode != 0 else "UNDETECTED"
        (EVID / f"{name}_mutated.log").write_text(
            f"# {name}  (expectation: {expectation})\n# {description}\n"
            f"# mutation applied to {target}\n"
            f"# --- anchor ---\n{old}\n# --- became ---\n{new}\n"
            f"# verdict: {verdict} (exit {m.returncode})\n\n{m.stdout}{m.stderr}",
            encoding="utf-8")
        print(f"  mutated: exit={m.returncode} -> {verdict}")

        path.write_text(baseline[target], encoding="utf-8")
        rebuild_and_link()
        ok_sha = sha(path) == baseline_sha[target]
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
        elif expectation == "detect":
            if verdict == "DETECTED":
                detected += 1
            else:
                flag, ok = "GAP", False
        elif verdict == "DETECTED":
            flag, ok = "NULL-CLAIM-WRONG", False
        print(f"  {name:36s} {expectation:7s} {verdict:11s} sha={sha_ok} repass={repassed}  {flag}")

    print(f"\n{detected}/{expected_detect} observable controls detected; "
          f"{expected_null} claimed provably null")
    return 0 if ok and detected == expected_detect else 1


if __name__ == "__main__":
    sys.exit(main())
