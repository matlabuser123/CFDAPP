#!/usr/bin/env bash
# P13-007 audit -- how much does an IDENTICAL benchmark vary run to run on this
# machine? A regression threshold that is not derived from this number is
# invented, and will either miss real regressions or fire on noise.
#
# Run from a scratch directory on purpose: every benchmark writes to
# "results/performance/..." RELATIVE TO THE WORKING DIRECTORY, so launching one
# from the repo root overwrites committed evidence. Running it from /tmp is the
# workaround that the --output-dir option exists to replace.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
R=$ROOT/results/p13/benchmarks
W=/tmp/p13_bench_noise; rm -rf $W; mkdir -p $W
cd $W

B=$ROOT/build/final/benchmarks/cpu/cfd_benchmark_cpu
echo "binary: $B"
for i in 1 2 3 4 5; do
  rm -rf $W/results
  /usr/bin/time -f "%e" $B > $W/run_$i.log 2> $W/time_$i.txt
  echo "  run $i: wall $(cat $W/time_$i.txt) s"
  cp -r $W/results $W/results_$i 2>/dev/null
done

echo
echo "=== per-grid solve times across 5 identical runs ==="
python3 - <<'PY'
import json,glob,os,statistics
rows={}
for i in range(1,6):
    for f in sorted(glob.glob(f"/tmp/p13_bench_noise/results_{i}/performance/baseline/benchmark_*.json")):
        d=json.load(open(f))
        key=os.path.basename(f)
        # find a duration-like field
        for k,v in d.items():
            if isinstance(v,(int,float)) and ("second" in k or "time" in k or "ms" in k):
                rows.setdefault((key,k),[]).append(v)
if not rows:
    print("  no timing fields found; dumping one file's keys:")
    fs=sorted(glob.glob("/tmp/p13_bench_noise/results_1/performance/baseline/*.json"))
    for f in fs[:2]:
        print("   ",os.path.basename(f),list(json.load(open(f)).keys()))
for (key,field),vals in sorted(rows.items()):
    if len(vals)<2: continue
    lo,hi=min(vals),max(vals)
    med=statistics.median(vals)
    spread=(hi-lo)/med*100 if med else 0
    print(f"  {key:34s} {field:22s} median {med:10.4f}  min {lo:10.4f}  max {hi:10.4f}  spread {spread:6.1f}%")
PY
