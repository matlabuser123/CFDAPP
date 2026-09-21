#!/usr/bin/env python3
"""GPU-DISC-001P -- the shared negative-control execution engine.

Phase D discipline, applied identically to every control:

  1. record the clean source sha256          (once, before anything is touched)
  2. apply exactly ONE mutation
  3. rebuild only as necessary              (ninja; then relink the harness)
  4. run the NARROWEST test expected to detect it
  5. record whether detection occurred
  6. record the first divergence             (stage / iteration, when reported)
  7. restore the production source           (byte-for-byte, from the bytes read in 1)
  8. verify sha256 restoration
  9. rebuild
 10. re-run the clean baseline

A control declares its expectation:

  "detect" -- the mutation MUST change the result. UNDETECTED is a verification
              gap, not a pass.
  "null"   -- the mutation provably cannot change any result under the
              production contract. Still executed, to CHECK the claim: a null
              control that IS detected means the proof was wrong, and that
              fails the run. Null controls are excluded from the
              detected-observable denominator.

Byte-level fidelity matters here and was learned the hard way in GPU-DISC-001J:
read_text/write_text translate newlines, so a restored file can differ from the
baseline in bytes while comparing equal as text. Everything below is bytes.

A mutated build that does not COMPILE proves nothing about the port, so the
build helper returns instead of raising -- the caller must still get to restore
the file. GPU-DISC-001K left a mutation in the working tree exactly once by
raising here.
"""

import hashlib
import pathlib
import re
import subprocess
import time

ROOT = pathlib.Path("/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp")
BUILD = ROOT / "build/cuda"
CUDA = pathlib.Path("/usr/local/cuda-12.9")
GATES = ROOT / "results/gpu-disc-001"
EVID = GATES / "negative-controls"

NL = "\n"

# Harness name -> source, relative to results/gpu-disc-001/.
HARNESS = {
    "gradient": "gradients/tools/gradient_equivalence.cpp",
    "diffusion": "diffusion/tools/diffusion_equivalence.cpp",
    "convection": "convection/tools/convection_equivalence.cpp",
    "momentum_convection": "convection/tools/momentum_convection_equivalence.cpp",
    "boundary": "boundary-conditions/tools/boundary_condition_equivalence.cpp",
    "momentum_assembly": "momentum-assembly/tools/momentum_assembly_equivalence.cpp",
    "momentum_response": "momentum-response/tools/momentum_response_equivalence.cpp",
    "face_flux": "rhie-chow/tools/face_flux_equivalence.cpp",
    "pressure_correction": "pressure-correction-assembly/tools/pressure_correction_equivalence.cpp",
    "velocity_correction": "velocity-correction/tools/velocity_correction_equivalence.cpp",
    "face_flux_correction": "face-flux-correction/tools/face_flux_correction_equivalence.cpp",
    "single_iteration": "single-iteration/tools/single_iteration_equivalence.cpp",
    "integrated_simple": "integrated-simple/tools/integrated_simple_equivalence.cpp",
    "full_solve": "full-solve-equivalence/tools/full_solve_equivalence.cpp",
    # GPU-PIPE-001 persistent fields. Paths are relative to results/gpu-disc-001/
    # like every other entry, so they climb out to the sibling evidence tree.
    # `transfers` is the detector for residency faults that change NO number --
    # re-uploading every iteration is numerically identical and invisible to
    # every equivalence gate above.
    "dirty_state": "../gpu-pipe-001/persistent-fields/tools/dirty_state.cpp",
    "transfers": "../gpu-pipe-001/persistent-fields/tools/transfer_guard.cpp",
    # GPU-PIPE-001 GPU-resident pressure solve. `residency` is the controlled
    # comparison (resident vs host round trip, bitwise, one binary);
    # `solve_transfers` is its transfer detector -- re-uploading an already
    # resident matrix changes no number, so only a transfer count sees it;
    # `sparsity` asserts the device matrix IS the matrix the host solver gets.
    "residency": "../gpu-pipe-001/gpu-resident-pressure-solve/tools/residency_comparison.cpp",
    "solve_transfers":
        "../gpu-pipe-001/gpu-resident-pressure-solve/tools/solve_transfer_guard.cpp",
    "sparsity": "../gpu-pipe-001/gpu-resident-pressure-solve/tools/roundtrip_probe.cpp",
    "rejection": "../gpu-pipe-001/gpu-resident-pressure-solve/tools/rejection_probe.cpp",
    # GPU-PIPE-001 final residency (the resident SIMPLE loop). Three detectors
    # for three kinds of defect: `loop_comparison` is the controlled comparison
    # (resident vs host round trip, bitwise, one binary); `loop_transfers` sees
    # residency faults that change NO number, which no equivalence gate can;
    # `dispatch` asserts the conditions under which the resident loop DECLINES,
    # which is the only way to catch it engaging where it was never qualified.
    "loop_comparison": "../gpu-pipe-001/final-residency/tools/resident_loop_comparison.cpp",
    "loop_transfers": "../gpu-pipe-001/final-residency/tools/loop_transfer_guard.cpp",
    "dispatch": "../gpu-pipe-001/final-residency/tools/dispatch_probe.cpp",
    # `residency_baseline` drives the facade stage by stage and is the only
    # harness that asserts the authority transition correctVelocityResident
    # performs. `dirty_state` never calls that method, so it cannot see a
    # missing transition there -- control rl6 proved exactly that by going
    # UNDETECTED against it.
    "residency_baseline": "../gpu-pipe-001/final-residency/tools/residency_baseline.cpp",
}

OBJ_DIR = pathlib.Path("/tmp/gpudisc_nc_obj")
BIN_DIR = pathlib.Path("/tmp/gpudisc_nc_bin")

# What a harness prints when it localises a divergence.
FIRST_DIVERGENCE_PATTERNS = [
    re.compile(r"FIRST DIVERGENCE: (.+?)\s*$", re.M),
    re.compile(r"first divergent iteration: (\S+)", re.M),
    re.compile(r"first divergent stage: (.+?)\s*$", re.M),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=str(ROOT), text=True, capture_output=True, **kw)


def compile_harness(name):
    """Compile one harness translation unit to an object. Done once per name,
    and again whenever a mutation touches a header the harness may include."""
    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    src = GATES / HARNESS[name]
    obj = OBJ_DIR / f"{name}.o"
    r = run(["/usr/bin/c++", f"-I{ROOT}/include", f"-I{BUILD}/generated/include",
             f"-I{BUILD}/_deps/nlohmann_json-src/include", "-isystem", f"{CUDA}/include",
             "-O2", "-DNDEBUG", "-std=c++20", "-c", "-o", str(obj), str(src)])
    if r.returncode != 0:
        return False, (r.stdout + r.stderr)[-3000:]
    return True, ""


def link_harness(name):
    """Relink an already-compiled object against the CURRENT archives.

    SIMPLE.cpp (cfdcore) calls into cfdcuda, so the two archives are mutually
    dependent: a link GROUP, not an ordering.
    """
    BIN_DIR.mkdir(parents=True, exist_ok=True)
    obj = OBJ_DIR / f"{name}.o"
    binary = BIN_DIR / name
    r = run(["/usr/bin/c++", "-o", str(binary), str(obj),
             "-Wl,--start-group", str(BUILD / "cuda/libcfdcuda.a"),
             str(BUILD / "src/libcfdcore.a"), "-Wl,--end-group",
             f"-L{CUDA}/lib64", "-lcudart"])
    if r.returncode != 0:
        return False, (r.stdout + r.stderr)[-3000:]
    return True, ""


def ninja():
    r = run(["ninja", "-C", str(BUILD)])
    if r.returncode != 0:
        return False, (r.stdout + r.stderr)[-3000:]
    return True, ""


def rebuild_for(control, harnesses):
    """ninja, then recompile the harness object if a HEADER moved, then link.

    A mutated header can be included by the harness translation unit itself, so
    the object is stale in exactly that case and only that case.
    """
    ok, message = ninja()
    if not ok:
        return False, "ninja: " + message
    if control["file"].startswith("include/"):
        for name in harnesses:
            ok, message = compile_harness(name)
            if not ok:
                return False, f"harness {name}: " + message
    for name in harnesses:
        ok, message = link_harness(name)
        if not ok:
            return False, f"link {name}: " + message
    return True, ""


def first_divergence(text):
    for pattern in FIRST_DIVERGENCE_PATTERNS:
        m = pattern.search(text)
        if m:
            return m.group(1).strip()
    return ""


def execute(controls, evidence_subdir, title):
    """Run a list of controls. Returns (ok, results)."""
    out = EVID / evidence_subdir
    out.mkdir(parents=True, exist_ok=True)

    # --- step 1: the clean bytes of every file any control will touch --------
    targets = sorted({c["file"] for c in controls})
    baseline = {}
    for rel in targets:
        path = ROOT / rel
        baseline[rel] = path.read_bytes()
        print(f"baseline {sha(path)}  {rel}")
    print()

    needed = sorted({h for c in controls for h in c["harnesses"]})
    for name in needed:
        ok, message = compile_harness(name)
        if not ok:
            print(message)
            raise SystemExit(f"harness {name} failed to compile -- nothing was mutated")
    print(f"compiled {len(needed)} harness objects\n")

    results = []
    for control in controls:
        cid = control["id"]
        rel = control["file"]
        path = ROOT / rel
        expectation = control["expect"]
        harnesses = control["harnesses"]
        started = time.time()
        print(f"=== {cid} ({expectation}) ===", flush=True)

        source = baseline[rel].decode("utf-8")
        occurrences = source.count(control["anchor"])
        if occurrences != 1:
            print(f"  !! anchor occurs {occurrences} times in {rel} -- NOT RUN")
            results.append({"id": cid, "expect": expectation, "verdict": f"ANCHOR-x{occurrences}",
                            "sha_ok": True, "repass": False, "first": "", "flag": "BAD-ANCHOR",
                            "control": control})
            continue

        # --- step 2: exactly one mutation ----------------------------------
        mutated = source.replace(control["anchor"], control["become"], 1)
        path.write_bytes(mutated.encode("utf-8"))

        # --- steps 3-6: build, run the narrowest test, record ---------------
        built, message = rebuild_for(control, harnesses)
        detections = []
        divergence = ""
        body = ""
        if built:
            for name in harnesses:
                args = control["args"].get(name, [])
                m = run([str(BIN_DIR / name)] + args)
                detected = m.returncode != 0
                detections.append((name, detected, m.returncode))
                if detected and not divergence:
                    divergence = first_divergence(m.stdout + m.stderr)
                body += (f"\n########## harness {name} {' '.join(args)} "
                         f"-> exit {m.returncode} ({'DETECTED' if detected else 'UNDETECTED'})"
                         f" ##########\n{m.stdout}{m.stderr}")
            verdict = "DETECTED" if any(d for _, d, _ in detections) else "UNDETECTED"
            print(f"  mutated: {verdict}"
                  + (f"  first divergence: {divergence}" if divergence else ""), flush=True)
        else:
            verdict = "BUILD-FAILED"
            body = f"\nthe mutation is ill-formed; it proves nothing about the port\n\n{message}"
            print("  mutated: BUILD-FAILED (ill-formed mutation, not a port defect)", flush=True)

        (out / f"{cid}_mutated.log").write_text(
            f"# {cid}\n# layer:       {control['layer']}\n# origin:      {control['origin']}\n"
            f"# expectation: {expectation}\n# {control['desc']}\n"
            f"# mutation applied to {rel}\n"
            f"# --- anchor ---\n{control['anchor']}\n"
            f"# --- became ---\n{control['become']}\n"
            f"# verdict: {verdict}\n"
            f"# first divergence: {divergence or 'n/a'}\n{body}",
            encoding="utf-8")

        # --- steps 7-10: restore, verify, rebuild, re-run clean -------------
        path.write_bytes(baseline[rel])
        ok, message = rebuild_for(control, harnesses)
        if not ok:
            print(message)
            raise SystemExit(f"{cid}: build failed while RESTORING -- the tree may be dirty")
        sha_ok = hashlib.sha256(path.read_bytes()).hexdigest() == \
            hashlib.sha256(baseline[rel]).hexdigest()
        repass = True
        restored_body = ""
        for name in harnesses:
            args = control["args"].get(name, [])
            r2 = run([str(BIN_DIR / name)] + args)
            if r2.returncode != 0:
                repass = False
            restored_body += (f"\n########## harness {name} -> exit {r2.returncode} ##########\n"
                              + (r2.stdout + r2.stderr)[-4000:])
        (out / f"{cid}_restored.log").write_text(
            f"# {cid} -- restored {rel}\n# sha256 {sha(path)}\n"
            f"# matches baseline: {sha_ok}\n# clean baseline re-passes: {repass}\n{restored_body}",
            encoding="utf-8")

        flag = "ok"
        if not sha_ok or not repass:
            flag = "RESTORE-FAILED"
        elif verdict == "BUILD-FAILED":
            flag = "ILL-FORMED-MUTATION"
        elif expectation == "detect" and verdict != "DETECTED":
            flag = "GAP"
        elif expectation == "null" and verdict == "DETECTED":
            flag = "NULL-CLAIM-WRONG"
        elif control.get("first") and divergence and control["first"] not in divergence:
            flag = "WRONG-FIRST-DIVERGENCE"
        print(f"  restored: sha-match={sha_ok} repass={repass}  {flag}"
              f"  [{time.time() - started:.0f}s]", flush=True)
        results.append({"id": cid, "expect": expectation, "verdict": verdict, "sha_ok": sha_ok,
                        "repass": repass, "first": divergence, "flag": flag, "control": control})

    # --- report ------------------------------------------------------------
    print(f"\n=== {title} ===")
    observable = [r for r in results if r["expect"] == "detect"]
    nulls = [r for r in results if r["expect"] == "null"]
    detected = [r for r in observable if r["verdict"] == "DETECTED" and r["flag"] == "ok"]
    for r in results:
        print(f"  {r['id']:52s} {r['expect']:7s} {r['verdict']:13s} "
              f"sha={r['sha_ok']} repass={r['repass']}  {r['flag']}"
              + (f"  first={r['first']}" if r["first"] else ""))
    print(f"\nobservable controls:          {len(observable)}")
    print(f"detected observable controls: {len(detected)}/{len(observable)}")
    print(f"documented null controls:     {len(nulls)}")
    ok = all(r["flag"] == "ok" for r in results) and len(detected) == len(observable)
    print(f"RESULT: {'PASS' if ok else 'FAIL'}")
    return ok, results
