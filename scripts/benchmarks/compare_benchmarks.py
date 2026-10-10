#!/usr/bin/env python3
"""P13-007 -- compare a benchmark run against a recorded baseline, and FAIL on a
severe performance regression.

Before this, no benchmark could report a failure: across all seven executables
there were zero `return 1` and zero `EXIT_FAILURE` sites. They computed, wrote
CSV and JSON, and returned 0 unconditionally -- so "no severe performance
regression" was detectable only by a person reading a CSV and remembering last
month's numbers.

WHAT THIS COMPARES, AND WHY ONLY THAT
-------------------------------------
Five identical runs of the same binary on this machine
(results/p13/benchmarks/logs/00_noise.log) gave:

    AGGREGATE                             median     spread
      80x80  runtime_per_iteration_ms    30.5893       3.5%
      40x40  runtime_per_iteration_ms     6.8588       5.2%
      20x20  runtime_per_iteration_ms     1.5030       7.9%

    SUB-STAGE (sub-millisecond)
      80x80  linear_solve_seconds         0.0018      27.0%
      80x80  pressure_assembly_seconds    0.0017      30.8%
      40x40  mass_flux_seconds            0.0001     362.1%

Nothing changed between those runs. A check over every recorded metric would
therefore be a noise detector: `mass_flux_seconds` alone varies by 362% run to
run. So this compares AGGREGATE metrics only, and admits a metric only when its
baseline value is above a DIMENSIONAL floor -- the noisy quantities are exactly
the ones sitting near the clock's resolution, so the floor removes them by
construction rather than by a tolerance stretched wide enough to cover them.

THE THRESHOLD, AND WHAT IT CANNOT DO
------------------------------------
Worst measured spread on an admitted metric is 7.9%. The default threshold is
1.25x (a 25% slowdown), which is about 3x that worst-case noise -- far enough
above it that an identical binary cannot trip it, and tight enough to catch the
regressions that matter (an assembly path that doubles, a solver that stops
converging in the same iteration count).

Stated plainly, because a threshold that hides its blind spot is worse than
none: THIS CANNOT SEE A REGRESSION SMALLER THAN 25%. A 10% slowdown is real and
this will not report it. Detecting that on this hardware would need many more
repeats and a quieter machine, and a threshold set inside the noise band would
fire on runs where nothing changed -- which ends with someone switching the
check off, and that is worse than a known blind spot.
"""

import argparse
import json
import pathlib
import sys

# Metrics worth gating on: aggregate, and stable across identical runs.
AGGREGATE_METRICS = ("runtime_per_iteration_ms", "total_seconds", "solve_seconds")

# Dimensional floor. Below this a value is at the mercy of the clock rather
# than of the program: the 362%-spread metric above had a baseline of 0.0001 s.
# Expressed per unit so a millisecond metric and a second metric are both
# judged on whether they are actually measurable.
FLOOR = {"ms": 1.0, "seconds": 0.05}


def unit_of(metric):
    if metric.endswith("_ms"):
        return "ms"
    if metric.endswith("_seconds"):
        return "seconds"
    return None


# Fields that must match for a comparison to mean anything. A timing from a
# different CPU, core count, compiler or build configuration is not a slower
# version of the same measurement -- it is a different measurement.
ENVIRONMENT_KEYS = ("cpu", "cores", "compiler", "cfdapp", "loaded")

# G8-F1: the keys above live in suite_environment.txt, which the DRIVER writes and
# which carries no build information at all. The build configuration lives in
# environment.json, which the BENCHMARK BINARY writes -- and which this comparator
# never opened. So an OpenMP-ON run compared against the OpenMP-OFF baseline was
# not refused: it was reported as 2.67x / 2.33x / 1.60x REGRESSION, on a tree whose
# numerics had not changed at all.
#
# The comment on ENVIRONMENT_KEYS already claimed to cover "build configuration".
# The intent was documented; the implementation was missing. These keys are what
# make the claim true. (The same shape as P13-003's F4, where the harness header
# described a different-clients assertion that did not exist.)
#
# environment.json is read rather than having the driver copy these into
# suite_environment.txt, because the committed baseline ALREADY carries them there
# -- so no baseline has to be re-recorded. Amendment A1 of this gate exists because
# a re-recorded baseline silently broke every comparison once already.
BUILD_CONFIG_KEYS = ("build_type", "compiler", "openmp_enabled", "cuda_enabled_at_build")


def load_build_config(directory):
    """environment.json as a dict, or {} when the run has none."""
    path = pathlib.Path(directory) / "environment.json"
    if not path.exists():
        return {}
    try:
        loaded = json.loads(path.read_text())
    except (ValueError, OSError):
        return {}
    return loaded if isinstance(loaded, dict) else {}


def load_environment(directory):
    """suite_environment.txt as a dict, or {} when the run has none."""
    path = pathlib.Path(directory) / "suite_environment.txt"
    if not path.exists():
        return {}
    out = {}
    for line in path.read_text().splitlines():
        if ":" not in line:
            continue
        key, _, value = line.partition(":")
        out[key.strip()] = value.strip()
    return out


def check_environments(baseline_dir, current_dir):
    """Returns a list of human-readable mismatches."""
    base = load_environment(baseline_dir)
    curr = load_environment(current_dir)
    if not base or not curr:
        return ["one of the runs has no suite_environment.txt, so the two cannot "
                "be shown to be comparable"]
    problems = []
    for key in ENVIRONMENT_KEYS:
        if base.get(key) != curr.get(key):
            problems.append(f"{key}: baseline {base.get(key)!r} vs current {curr.get(key)!r}")
    # A run recorded on a loaded machine is not comparable to anything, even
    # to another loaded run: the load differs run to run. The same binary
    # measured 1.37x apart on this host for exactly this reason.
    for label, env in (("baseline", base), ("current", curr)):
        if env.get("loaded", "no").startswith("yes"):
            problems.append(f"the {label} run was recorded with --allow-loaded "
                            f"and is marked NOT COMPARABLE")
    # G8-F1: the build configuration, from environment.json. A serial baseline and
    # an OpenMP run are not the same measurement, and neither are a CUDA build and
    # a CPU-only one -- comparing them and calling the difference a regression is
    # worse than not comparing, because it spends the operator's trust on a number
    # that means nothing.
    base_build = load_build_config(baseline_dir)
    curr_build = load_build_config(current_dir)
    if not base_build or not curr_build:
        # Absence fails closed. A run whose build configuration cannot be read is
        # not thereby comparable to everything.
        missing = "baseline" if not base_build else "current"
        problems.append(f"the {missing} run has no readable environment.json, so its "
                        f"build configuration cannot be shown to match")
    else:
        for key in BUILD_CONFIG_KEYS:
            if base_build.get(key) != curr_build.get(key):
                problems.append(f"{key}: baseline {base_build.get(key)!r} vs "
                                f"current {curr_build.get(key)!r}")
    return problems


def load_run(directory):
    """Every benchmark_*.json in a run directory, keyed by file stem."""
    out = {}
    for path in sorted(pathlib.Path(directory).glob("benchmark_*.json")):
        try:
            out[path.stem] = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            print(f"  could not read {path}: {exc}", file=sys.stderr)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--baseline", required=True, help="directory of the recorded baseline run")
    parser.add_argument("--current", required=True, help="directory of the run to check")
    parser.add_argument(
        "--allow-environment-mismatch",
        action="store_true",
        help="compare anyway when the two runs' environments differ (they are then not "
             "comparable; the mismatch is printed either way)",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=1.25,
        help="fail when current / baseline exceeds this (default 1.25, ~3x measured noise)",
    )
    args = parser.parse_args()

    # E2: comparing across build configurations or machines is refused, not
    # silently performed. The committed 0.1.0-dev baseline (OpenMP off, CUDA
    # off) against a current CUDA+OpenMP build would produce a confident wrong
    # answer, which is worse than no answer.
    mismatches = check_environments(args.baseline, args.current)
    if mismatches:
        print("environment mismatch between the two runs:")
        for problem in mismatches:
            print(f"  {problem}")
        if not args.allow_environment_mismatch:
            print()
            print("FAIL: refusing to compare runs that are not comparable.")
            print("      (--allow-environment-mismatch compares anyway, and means nothing)")
            return 2
        print("  --allow-environment-mismatch given: comparing anyway")
        print()

    baseline = load_run(args.baseline)
    current = load_run(args.current)

    if not baseline:
        print(f"FAIL: no baseline benchmark_*.json found in {args.baseline}")
        return 2
    if not current:
        print(f"FAIL: no current benchmark_*.json found in {args.current}")
        return 2

    # A baseline case that the current run did not produce is a failure, not a
    # skip: a suite that silently stops measuring something still reports green.
    missing = sorted(set(baseline) - set(current))
    if missing:
        print(f"FAIL: the current run is missing {len(missing)} baseline case(s): "
              f"{', '.join(missing)}")
        return 1

    compared = 0
    skipped = []
    regressions = []
    print(f"threshold: {args.threshold:.2f}x   "
          f"(measured worst-case run-to-run spread on an admitted metric: 7.9%)")
    print()
    for case in sorted(baseline):
        for metric in AGGREGATE_METRICS:
            if metric not in baseline[case] or metric not in current[case]:
                continue
            base_value = float(baseline[case][metric])
            curr_value = float(current[case][metric])
            unit = unit_of(metric)
            floor = FLOOR.get(unit)
            if floor is not None and base_value < floor:
                skipped.append(f"{case}/{metric} (baseline {base_value:g} below the {floor:g} "
                               f"{unit} floor)")
                continue
            if base_value <= 0.0:
                skipped.append(f"{case}/{metric} (baseline {base_value:g})")
                continue
            ratio = curr_value / base_value
            compared += 1
            flag = "REGRESSION" if ratio > args.threshold else "ok"
            print(f"  {case:22s} {metric:26s} baseline {base_value:10.4f}  "
                  f"current {curr_value:10.4f}  {ratio:5.2f}x  {flag}")
            if ratio > args.threshold:
                regressions.append((case, metric, base_value, curr_value, ratio))

    print()
    for note in skipped:
        print(f"  not compared: {note}")

    # Comparing nothing must never look like passing -- the failure mode of
    # every green-by-default check.
    if compared == 0:
        print("\nFAIL: no metric was comparable; this check proved nothing.")
        return 2

    if regressions:
        print(f"\nFAIL: {len(regressions)} severe regression(s) of {compared} metrics compared")
        for case, metric, base_value, curr_value, ratio in regressions:
            print(f"  {case} {metric}: {base_value:.4f} -> {curr_value:.4f}  ({ratio:.2f}x)")
        return 1

    print(f"\nPASS: {compared} metrics compared, none above {args.threshold:.2f}x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
