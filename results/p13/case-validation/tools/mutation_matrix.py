#!/usr/bin/env python3
"""P13-002 -- the case-validation mutation matrix.

Enumerating "what ought to be validated" finds what I thought of. Mutating
every field of a known-good case finds what I did not. The matrix is generated
from the case's own JSON, so a configuration field added later is covered
without editing this file.

WHAT COUNTS AS A PASS

The brief's requirement is that an invalid case fails BEFORE solver execution.
That is decidable from the production binary alone, with no inside knowledge:

    exit 2   cfd::app::ProjectRunStatus::InvalidCase -- the reader or the
             builder refused it, and ProjectRunner returns before SIMPLE runs
    exit 0   Converged            -- it RAN
    exit 3   DidNotConverge       -- it RAN
    exit 4   NumericalFailure     -- it RAN
    exit 1   ApplicationError     -- usage or export failure, not validation

So "rejected before the solver" is exactly "exit 2", and anything else means
the mutated case was accepted. Accepted rows are the findings.

WHICH MUTATIONS ARE SOUND

Only mutations that are invalid for EVERY field, whatever that field means:

    type      a number or array replaced by a string
    missing   the key removed
    unknown   an unrecognised sibling key added
    nan/inf   a numeric leaf replaced by a NaN / Infinity literal
              (written as raw text, since neither is valid JSON -- exactly
              how a corrupted or hand-edited file would carry them)

A "negative" or "zero" mutation is NOT applied blindly: negative is perfectly
valid for a velocity component and invalid for a density, and a matrix that
could not tell them apart would report noise as findings. Those live in
POSITIVE_FIELDS below, named one at a time, with the reason each is positive.

usage: mutation_matrix.py <case-dir> <cfdapp-binary> [--out results.csv]
"""
import argparse
import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile

# Fields whose value must be strictly positive, with the reason. Only these get
# the zero/negative mutations. Each entry is (file, dotted path).
POSITIVE_FIELDS = [
    ("geometry.json", "length", "a domain of zero or negative extent is not a domain"),
    ("geometry.json", "height", "as above"),
    ("mesh.json", "nx", "a mesh needs at least one cell along each axis"),
    ("mesh.json", "ny", "as above"),
    ("physics.json", "density", "rho <= 0 makes the momentum equation meaningless"),
    ("physics.json", "dynamic_viscosity", "mu <= 0 removes the diffusion operator"),
    ("solver.json", "max_iterations", "a budget of zero solves nothing"),
    ("solver.json", "velocity_tolerance", "a tolerance is a positive bound"),
    ("solver.json", "pressure_tolerance", "as above"),
    ("solver.json", "continuity_tolerance", "as above"),
    ("solver.json", "momentum_linear_solver.max_iterations", "as max_iterations"),
    ("solver.json", "pressure_linear_solver.max_iterations", "as max_iterations"),
    ("solver.json", "momentum_linear_solver.absolute_tolerance", "as tolerance"),
    ("solver.json", "pressure_linear_solver.absolute_tolerance", "as tolerance"),
]

# Fields constrained to the open interval (0, 1].
UNIT_FIELDS = [
    ("solver.json", "velocity_relaxation", "under-relaxation alpha in (0, 1]"),
    ("solver.json", "pressure_relaxation", "as above"),
]

CASE_FILES = ["case.json", "geometry.json", "mesh.json", "physics.json",
              "boundaries.json", "solver.json"]

# Keys the parsers treat as OPTIONAL, with the source that says so. Removing
# one of these is valid, so the "missing" mutation is not applied to it --
# otherwise the matrix reports a documented default as a validation gap. Each
# entry was read out of the parser, not inferred from the run.
OPTIONAL_KEYS = {
    ("case.json", "description"),        # CaseConfigParser: getOptionalString(..., "")
    ("case.json", "format_version"),     # CaseConfigParser: getOptionalInt(..., 1)
    ("physics.json", "reynolds_number"), # PhysicsConfig.hpp: "optional reporting-only metadata"
    ("solver.json", "convection_scheme"),     # SolverConfig.hpp: absent -> "upwind"
    ("solver.json", "gradient_scheme"),       # absent -> "green_gauss"
    ("solver.json", "non_orthogonal_corrections"),  # SolverConfigParser: getOptionalInt(..., 0)
    ("physics.json", "coupled"),         # PhysicsConfigParser: "optional, defaulting to false"
    ("solver.json", "face_flux"),             # absent -> "automatic"
    ("solver.json", "robustness"),            # absent -> every feature off
    ("mesh.json", "grading"),                 # absent -> uniform
    ("mesh.json", "nz"),                      # absent -> 2D
}

# Free-text fields: any string is valid, so the "enum" mutation is meaningless
# for them. A matrix that flagged a renamed case as a validation gap would be
# reporting its own assumption, not a defect.
FREE_TEXT_KEYS = {
    ("case.json", "name"),
    ("case.json", "description"),
    # A species or phase name is a label the case chooses, not a vocabulary.
    # PhysicsConfigParser only requires it to be non-empty.
    ("physics.json", "name"),
}

EXIT_INVALID_CASE = 2


def leaf_paths(node, prefix=""):
    """Every scalar and array leaf, as a dotted path."""
    if isinstance(node, dict):
        for key, value in node.items():
            path = "%s.%s" % (prefix, key) if prefix else key
            if isinstance(value, dict):
                for sub in leaf_paths(value, path):
                    yield sub
            else:
                yield path, value
    # A list of objects (species, blocks) is treated as one leaf: mutating
    # inside it needs per-schema knowledge this sweep deliberately avoids.


def object_paths(node, prefix=""):
    """Every JSON object, as a dotted path ("" is the document root)."""
    if isinstance(node, dict):
        yield prefix
        for key, value in node.items():
            if isinstance(value, dict):
                for sub in object_paths(value, "%s.%s" % (prefix, key) if prefix else key):
                    yield sub


def get_at(doc, path):
    node = doc
    for part in path.split("."):
        node = node[part]
    return node


def set_at(doc, path, value):
    parts = path.split(".")
    node = doc
    for part in parts[:-1]:
        node = node[part]
    node[parts[-1]] = value


def del_at(doc, path):
    parts = path.split(".")
    node = doc
    for part in parts[:-1]:
        node = node[part]
    del node[parts[-1]]


def parent_of(doc, path):
    parts = path.split(".")
    node = doc
    for part in parts[:-1]:
        node = node[part]
    return node


def run_case(binary, case_dir):
    """Run the production binary. Returns (exit code, combined output)."""
    try:
        proc = subprocess.run([binary, "--case", case_dir], capture_output=True,
                              text=True, timeout=120)
        return proc.returncode, (proc.stdout + proc.stderr)
    except subprocess.TimeoutExpired:
        return -1, "TIMEOUT"


def apply_mutation(work, source, filename, mutate):
    """Copy `source` to `work` and apply `mutate(doc)` to `filename`.

    Returns False when the mutation does not apply (e.g. the file has no such
    key), so a non-applicable row is skipped rather than counted as a pass.
    """
    if os.path.exists(work):
        shutil.rmtree(work)
    shutil.copytree(source, work)
    results = os.path.join(work, "results")
    if os.path.isdir(results):
        shutil.rmtree(results)
    path = os.path.join(work, filename)
    with open(path, encoding="utf-8") as handle:
        doc = json.load(handle)
    try:
        text = mutate(doc)
    except (KeyError, IndexError, TypeError):
        return False
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text if text is not None else json.dumps(doc, indent=2))
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("case")
    parser.add_argument("binary")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    tmp = tempfile.mkdtemp(prefix="p13_mutation_")
    work = os.path.join(tmp, "case")

    # Non-vacuity: the UNMUTATED case must RUN. Every row below is a claim
    # about what the mutation did; if the baseline were already rejected, the
    # whole matrix would be measuring nothing.
    shutil.copytree(args.case, work)
    base_code, base_output = run_case(args.binary, work)
    print("baseline: exit %d (%s)" % (
        base_code, "runs -- rows below are meaningful" if base_code != EXIT_INVALID_CASE
        else "ALREADY REJECTED -- the matrix would be vacuous"))
    if base_code == EXIT_INVALID_CASE:
        print(base_output[:400])
        return 2
    print("")

    rows = []

    def record(filename, path, kind, code, output):
        # The message is evidence, so it has to be comparable between runs:
        # the per-run temp directory is normalised away, leaving the part the
        # parser actually chose to say. This makes the matrix its own
        # PRESERVATION instrument -- a change that alters a message shows up
        # as a diff of this column, not as an opinion.
        output = output.replace(work, "<case>")
        rejected = code == EXIT_INVALID_CASE
        # A rejection must also SAY something useful. The project's own rule
        # (TODO.md P1 section 20) is that a message names the file and field.
        names_file = filename in output
        leaf = path.split(".")[-1]
        names_field = leaf in output
        rows.append({
            "file": filename, "path": path, "mutation": kind, "exit": code,
            "rejected": rejected, "names_file": names_file,
            "names_field": names_field,
            "message": " ".join(output.split())[:200],
        })

    for filename in CASE_FILES:
        source_path = os.path.join(args.case, filename)
        if not os.path.exists(source_path):
            continue
        with open(source_path, encoding="utf-8") as handle:
            original = json.load(handle)

        # One unrecognised-key mutation per OBJECT. The added key is what is
        # at fault, so the row is attributed to it rather than to whichever
        # sibling leaf the sweep was visiting.
        for obj_path in object_paths(original):
            def add_unknown(d, p=obj_path):
                node = d
                if p:
                    for part in p.split("."):
                        node = node[part]
                node["an_unrecognised_key"] = 1
            if apply_mutation(work, args.case, filename, add_unknown):
                code, output = run_case(args.binary, work)
                record(filename, (obj_path + "." if obj_path else "") + "an_unrecognised_key",
                       "unknown", code, output)

        for path, value in list(leaf_paths(original)):
            mutations = []
            # A string where a number or array belongs. Skipped for a leaf
            # that is already a string: that is the "unknown enum" case, which
            # gets its own mutation below.
            if isinstance(value, (int, float, list)) and not isinstance(value, bool):
                mutations.append(("type", lambda d, p=path: set_at(d, p, "not-a-number")))
            leaf_key = path.split(".")[-1]
            if isinstance(value, str) and (filename, leaf_key) not in FREE_TEXT_KEYS:
                mutations.append(("enum", lambda d, p=path: set_at(d, p, "definitely_not_valid")))
            if (filename, leaf_key) not in OPTIONAL_KEYS:
                mutations.append(("missing", lambda d, p=path: del_at(d, p)))
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                # NaN and Infinity are not valid JSON, so they are injected as
                # raw text -- which is exactly how a corrupted file carries
                # them, and what the parser has to survive.
                for token in ("NaN", "Infinity"):
                    def raw(d, p=path, t=token):
                        set_at(d, p, 0)
                        return json.dumps(d, indent=2).replace(
                            '"%s": 0' % p.split(".")[-1], '"%s": %s' % (p.split(".")[-1], t), 1)
                    mutations.append((token.lower(), raw))

            for kind, mutate in mutations:
                if not apply_mutation(work, args.case, filename, mutate):
                    continue
                code, output = run_case(args.binary, work)
                record(filename, path, kind, code, output)

    # Tier 2: the explicitly-reasoned range mutations.
    for filename, path, _reason in POSITIVE_FIELDS:
        for kind, new in (("zero", 0), ("negative", -1)):
            if not apply_mutation(work, args.case, filename,
                                  lambda d, p=path, v=new: set_at(d, p, v)):
                continue
            code, output = run_case(args.binary, work)
            record(filename, path, kind, code, output)
    for filename, path, _reason in UNIT_FIELDS:
        for kind, new in (("zero", 0.0), ("above_one", 1.5), ("negative", -0.5)):
            if not apply_mutation(work, args.case, filename,
                                  lambda d, p=path, v=new: set_at(d, p, v)):
                continue
            code, output = run_case(args.binary, work)
            record(filename, path, kind, code, output)

    accepted = [r for r in rows if not r["rejected"]]
    # See acceptance_gate_A2.md. Two columns, because the frozen criterion and
    # the first implementation of it disagreed:
    #   vague -- names NO file and NO field. This is criterion M2.
    #   weak  -- names no file OR no field. Stricter, and wrong for a cascade
    #            message that correctly blames the constraint the edit broke
    #            rather than the edit itself. Kept because it is more sensitive
    #            and is what surfaced the manifest-reference finding.
    vague = [r for r in rows if r["rejected"] and not r["names_file"] and not r["names_field"]]
    weak = [r for r in rows if r["rejected"] and not (r["names_file"] and r["names_field"])]

    print("mutations applied      %d" % len(rows))
    print("rejected before solve  %d" % (len(rows) - len(accepted)))
    print("ACCEPTED (findings)    %d" % len(accepted))
    print("rejected but vague     %d   (no file AND no field -- criterion M2)" % len(vague))
    print("rejected but weak      %d   (no file OR no field -- proxy, M2b)" % len(weak))
    print("")
    if accepted:
        print("=== ACCEPTED -- the solver ran on an invalid case ===")
        for row in accepted:
            print("  %-16s %-42s %-9s exit %d" % (
                row["file"], row["path"], row["mutation"], row["exit"]))
    if weak:
        print("")
        print("=== REJECTED, but the message does not name both file and field ===")
        for row in weak:
            print("  %-16s %-42s %-9s file=%s field=%s" % (
                row["file"], row["path"], row["mutation"],
                row["names_file"], row["names_field"]))
            print("      %s" % row["message"][:150])

    if args.out:
        import csv
        with open(args.out, "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print("")
        print("wrote %s" % args.out)

    shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
