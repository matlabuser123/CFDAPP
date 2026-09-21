#!/usr/bin/env python3
"""GPU-DISC-001I negative controls.

Each control: inject a single-point mutation into the CUDA kernel, rebuild,
run the differential, restore, rebuild, verify the restored file's sha256
against the baseline, and re-run to confirm it passes again.

Each control declares what it expects:
  "detect" -- the mutation must change the result. An UNDETECTED one is a gap
              in the gate, not a pass.
  "null"   -- the mutation provably cannot change any result (documented in
              summary.md with the proof). It is still run, to check that the
              claim holds; a "null" control that IS detected means the proof
              was wrong, which fails the run. It is never counted as a
              detection.
"""

import hashlib
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
KERNEL = ROOT / "cuda/kernels/DevicePressureCorrectionKernel.cu"
EVID = ROOT / "results/gpu-disc-001/pressure-correction-assembly/negative-control"
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
HARNESS = (ROOT / "results/gpu-disc-001/pressure-correction-assembly/tools"
                  "/pressure_correction_equivalence.cpp")
OBJ = pathlib.Path("/tmp/pcorr_equiv.o")
BIN = pathlib.Path("/tmp/pcorr_equiv_nc")

NL = "\n"

CONTROLS = [
    ("g1_reversed_rhs_sign",
     "RHS sign reversed: rhs = +imbalance instead of -imbalance",
     "  Real rhsValue = -imbalance;",
     "  Real rhsValue = imbalance;",
     ["--quick"], "detect"),

    ("g2_skipped_opposite_row",
     "the whole face is skipped when one endpoint is the reference cell, instead of "
     "suppressing only the reference's own row",
     "      const Real d = faceCoefficient[f];" + NL +
     "      const Index other = (owner == c) ? neighbor : owner;",
     "      const Real d = faceCoefficient[f];" + NL +
     "      const Index other = (owner == c) ? neighbor : owner;" + NL +
     "      if (pinReferenceCell && other == referenceCell) continue;",
     ["--quick"], "detect"),

    ("g3_additive_pin",
     "additive pin: the reference row accumulates its face terms and then has 1.0 added, "
     "instead of never being written",
     "    if (isReference) {" + NL +
     "      // The face loop never writes this row, so the diagonal is EXACTLY the" + NL +
     "      // 1.0 added afterwards and every off-diagonal is exactly 0.0." + NL +
     "      values[k] = (column == c) ? 1.0 : 0.0;" + NL +
     "      continue;" + NL +
     "    }" + NL +
     "    Real value = 0.0;",
     "    Real value = (isReference && column == c) ? 1.0 : 0.0;",
     ["--quick"], "detect"),

    ("g4_precomputed_decomposition",
     "decomposeAreaVector fed the geometric area vector instead of the field-dependent "
     "response vector -- i.e. treated as precomputable",
     "    if (decomposeAreaVectorDevice(p.dX[f], p.dY[f], p.dZ[f], rx, ry, rz, "
     "ox, oy, oz, nx, ny, nz)) {",
     "    if (decomposeAreaVectorDevice(p.dX[f], p.dY[f], p.dZ[f], p.areaX[f], p.areaY[f]," + NL +
     "                                  p.areaZIsZero[f] != 0 ? 0.0 : p.areaZ[f], ox, oy, oz," + NL +
     "                                  nx, ny, nz)) {",
     ["--quick"], "detect"),

    ("g5_reversed_off_diagonal",
     "off-diagonal sign reversed: A(P,N) += D instead of -= D",
     "      else if (column == other) value = value - d;",
     "      else if (column == other) value = value + d;",
     ["--quick"], "detect"),

    ("g6_neumann_boundary_couples",
     "a Neumann-like boundary face is given a real coupling and a real explicit flux "
     "instead of exactly zero",
     "  if (neighbor == DeviceMesh::kNoNeighbor && p.isFixedValue[f] == 0) {" + NL +
     "    faceCoefficient[f] = 0.0;" + NL +
     "    explicitFaceFlux[f] = 0.0;" + NL +
     "    return;" + NL +
     "  }",
     "  // MUTATED: every boundary face now couples.",
     ["--quick"], "detect"),

    ("g6n_matrix_side_filter_only",
     "the MATRIX-side FixedValue filter alone is removed, leaving faceTermsKernel's "
     "zeroing in place",
     "        if (p.isFixedValue[f] == 0) continue;  // Neumann-like: no entry at all",
     "        // MUTATED: the matrix-side FixedValue filter is gone.",
     ["--quick"], "null"),

    ("g7_reversed_continuity_order",
     "the continuity imbalance accumulated in reverse traversal order, changing the "
     "left-to-right summation",
     "  for (Index slot = p.cellFaceOffsets[c]; slot < p.cellFaceOffsets[c + 1]; ++slot) {" + NL +
     "    const Index f = p.cellFaceIds[slot];",
     "  for (Index slot = p.cellFaceOffsets[c + 1]; slot-- > p.cellFaceOffsets[c];) {" + NL +
     "    const Index f = p.cellFaceIds[slot];",
     ["--quick"], "detect"),

    ("g7n_continuity_face_id_order",
     "the continuity imbalance gathered in face-id order instead of cell.faceIds() order",
     "  for (Index slot = p.cellFaceOffsets[c]; slot < p.cellFaceOffsets[c + 1]; ++slot) {" + NL +
     "    const Index f = p.cellFaceIds[slot];",
     "  for (Index slot = p.sortedOffsets[c]; slot < p.sortedOffsets[c + 1]; ++slot) {" + NL +
     "    const Index f = p.sortedFaces[slot];",
     ["--quick"], "null"),

    ("g8_wellposedness_guard_removed",
     "the 1e-6*|d|*|sf| well-posedness guard reduced to a bare positivity test",
     "  if (!(dDotSf > (1e-6 * dMag * sfMag))) return false;",
     "  if (!(dDotSf > 0.0)) return false;",
     ["--quick"], "detect"),
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
    baseline = KERNEL.read_text(encoding="utf-8")
    baseline_sha = sha(KERNEL)
    print(f"baseline sha256 {baseline_sha}")
    print(f"harness         {sha(HARNESS)}\n")

    r = subprocess.run(
        ["/usr/bin/c++", f"-I{ROOT}/include", f"-I{BUILD}/generated/include",
         f"-I{BUILD}/_deps/nlohmann_json-src/include", "-isystem", f"{CUDA}/include",
         "-O2", "-DNDEBUG", "-std=c++20", "-c", "-o", str(OBJ), str(HARNESS)],
        cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit("harness compile failed")

    results = []
    for name, description, old, new, args, expectation in CONTROLS:
        print(f"=== {name} ({expectation}) ===")
        occurrences = baseline.count(old)
        if occurrences != 1:
            print(f"  !! anchor occurs {occurrences} times -- cannot inject unambiguously")
            results.append((name, expectation, f"ANCHOR-x{occurrences}", False, False))
            continue

        KERNEL.write_text(baseline.replace(old, new, 1), encoding="utf-8")
        rebuild_and_link()
        m = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
        verdict = "DETECTED" if m.returncode != 0 else "UNDETECTED"
        (EVID / f"{name}_mutated.log").write_text(
            f"# {name}  (expectation: {expectation})\n# {description}\n"
            f"# mutation applied to {KERNEL.relative_to(ROOT)}\n"
            f"# --- anchor ---\n{old}\n# --- became ---\n{new}\n"
            f"# verdict: {verdict} (exit {m.returncode})\n\n{m.stdout}{m.stderr}",
            encoding="utf-8")
        print(f"  mutated: exit={m.returncode} -> {verdict}")

        KERNEL.write_text(baseline, encoding="utf-8")
        rebuild_and_link()
        restored_sha = sha(KERNEL)
        ok_sha = restored_sha == baseline_sha
        r2 = subprocess.run([str(BIN)] + args, cwd=ROOT, text=True, capture_output=True)
        (EVID / f"{name}_restored.log").write_text(
            f"# {name} -- restored\n# sha256 {restored_sha}\n"
            f"# matches baseline: {ok_sha}\n\n{r2.stdout}{r2.stderr}", encoding="utf-8")
        print(f"  restored: sha-match={ok_sha} exit={r2.returncode}")
        results.append((name, expectation, verdict, ok_sha, r2.returncode == 0))

    print("\n=== summary ===")
    ok = True
    detected = 0
    expected_detect = sum(1 for c in CONTROLS if c[5] == "detect")
    expected_null = sum(1 for c in CONTROLS if c[5] == "null")
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
            # A mutation claimed unobservable that IS observable means the
            # proof was wrong -- a failure, not a bonus detection.
            flag, ok = "NULL-CLAIM-WRONG", False
        print(f"  {name:34s} {expectation:7s} {verdict:11s} sha={sha_ok} repass={repassed}  {flag}")

    print(f"\n{detected}/{expected_detect} observable controls detected; "
          f"{expected_null} provably-null controls confirmed unobservable")
    return 0 if ok and detected == expected_detect else 1


if __name__ == "__main__":
    sys.exit(main())
