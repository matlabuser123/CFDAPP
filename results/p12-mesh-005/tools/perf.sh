#!/usr/bin/env bash
# P12-MESH-005, reported (not gated): the cost of the 3-component vector on 2D production runs.
# The same committed 2D cases run with the pre-MESH-005 CLI (BASE = $HOME/m5ref/base, Release) and the final
# CLI (NEW = build/release), alternating BASE/NEW, REPS repetitions each, nothing else running.
# Per run: wall time (monotonic clock) and the child's user+sys CPU time (getrusage) -- the CPU time is
# immune to WSL wall-clock jumps. Minimum and median per case, and the NEW/BASE ratios.
# usage: perf.sh [REPS] [case ...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
NEW=$R/build/release/apps/cli/cfdapp
BASE=$HOME/m5ref/base/build/apps/cli/cfdapp
REPS=${1:-5}; shift
CASES=${*:-"lid_driven_cavity_40x40 poiseuille_distorted curved_channel_multiblock heated_cavity"}
W=$HOME/m5perf; rm -rf $W; mkdir -p $W
echo "# P12-MESH-005 performance (reported, not gated): 2D CLI, BASE (pre-MESH-005) vs NEW (final), $REPS alternating repetitions; $(date -u)"
echo "# NEW $(sha256sum $NEW | cut -c1-16)  BASE $(sha256sum $BASE | cut -c1-16)  nproc $(nproc)  $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
echo "# both Release -O3 (GCC $(c++ -dumpfullversion)), OMP_NUM_THREADS=1, WSL2 $(uname -r)"
export OMP_NUM_THREADS=1
python3 - "$R" "$NEW" "$BASE" "$W" "$REPS" $CASES <<'PY'
import os, resource, shutil, statistics, subprocess, sys, time
R, NEW, BASE, W, REPS = sys.argv[1:6]
cases = sys.argv[6:]
REPS = int(REPS)
def cpu():
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime
print("%-28s %-5s %10s %10s %10s %10s   %s" % ("case", "build", "wall min", "wall med", "cpu min", "cpu med", "cpu per run [s]"))
for case in cases:
    wall = {"BASE": [], "NEW": []}
    cput = {"BASE": [], "NEW": []}
    for rep in range(REPS):
        for tag, exe in (("BASE", BASE), ("NEW", NEW)):
            d = os.path.join(W, tag, case)
            shutil.rmtree(d, ignore_errors=True)
            shutil.copytree(os.path.join(R, "cases", case), d)
            shutil.rmtree(os.path.join(d, "results"), ignore_errors=True)
            c0 = cpu(); t0 = time.monotonic()
            rc = subprocess.run([exe, "--case", d], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
            t = time.monotonic() - t0; c = cpu() - c0
            assert rc == 0, (case, tag, rc)
            wall[tag].append(t); cput[tag].append(c)
    for tag in ("BASE", "NEW"):
        print("%-28s %-5s %10.3f %10.3f %10.3f %10.3f   %s" % (case, tag, min(wall[tag]), statistics.median(wall[tag]),
              min(cput[tag]), statistics.median(cput[tag]), " ".join("%.3f" % x for x in cput[tag])))
    print("%-28s NEW/BASE: wall min %.3f, wall median %.3f; cpu min %.3f, cpu median %.3f" % (case,
          min(wall["NEW"]) / min(wall["BASE"]), statistics.median(wall["NEW"]) / statistics.median(wall["BASE"]),
          min(cput["NEW"]) / min(cput["BASE"]), statistics.median(cput["NEW"]) / statistics.median(cput["BASE"])))
    sys.stdout.flush()
PY
echo "perf exit $?"
