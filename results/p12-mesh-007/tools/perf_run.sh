#!/usr/bin/env bash
# P12-MESH-007 performance baseline (acceptance_gate.md "Performance": measurement only), run under
# the local hardware/resource policy (CLAUDE.md):
#   - Release (build/release, no sanitizers, OpenMP off -> one thread), pinned to one core;
#   - nothing else heavy running: the load average is recorded before and after and the run
#     refuses to start above 1.0;
#   - one warm-up run (discarded), then REPS measured runs of tools/perf_baseline.cpp;
#   - per item: the median over the runs of each run's median, and the spread (min-max) of those
#     medians; never the fastest run alone;
#   - the environment is recorded (CPU, memory, compilers, build configuration, GPU telemetry).
# usage: perf_run.sh [reps, default 5] [core, default 3]
set -u
REPS=${1:-5}; CORE=${2:-3}
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-007
B=$R/build/release
LOG=$P/logs/50_performance_baseline.log
W=$HOME/m7perf; rm -rf $W; mkdir -p $W
[ -e "$LOG" ] && { echo "REFUSED: $LOG exists"; exit 1; }
load() { cut -d' ' -f1 /proc/loadavg; }
mhz() { awk -F: '/cpu MHz/ {s+=$2; n++; if ($2>m) m=$2; if (min==""||$2<min) min=$2} END {printf "mean %.0f min %.0f max %.0f MHz over %d cpus", s/n, min, m, n}' /proc/cpuinfo; }
{
  echo "# P12-MESH-007 performance baseline $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(cd $R && git rev-parse HEAD)"
  echo "## environment"
  echo "nproc $(nproc); $(lscpu | grep -E '^Model name' | sed 's/  */ /g')"
  lscpu | grep -E '^(Thread|Core|Socket)|^CPU\(s\)|MHz' | sed 's/  */ /g'
  free -h | head -2
  uname -r
  echo "gcc: $(c++ --version | head -1)"
  echo "clang: $(clang++ --version 2>/dev/null | head -1 || echo none)"
  echo "cmake: $(cmake --version | head -1); ninja: $(ninja --version 2>/dev/null || echo none)"
  echo "nvcc: $(nvcc --version 2>/dev/null | grep release || echo none)"
  echo "nvidia-smi (WSL): $(nvidia-smi --query-gpu=name,driver_version,temperature.gpu,power.draw,utilization.gpu --format=csv,noheader 2>&1 | head -1)"
  echo "nvidia-smi driver-supported CUDA: $(nvidia-smi 2>/dev/null | grep -o 'CUDA Version: [0-9.]*' || echo n/a)"
  echo "  (GPU telemetry is recorded for context only; this baseline is CPU-only. Windows nvidia-smi has shown implausible power readings before, so power values are not relied on.)"
  echo "build: $(grep -E 'CMAKE_BUILD_TYPE:|CFDAPP_ENABLE_OPENMP:|CFDAPP_ENABLE_CUDA:|CFDAPP_ENABLE_SANITIZERS:|CMAKE_CXX_FLAGS_RELEASE:' $B/CMakeCache.txt | tr '\n' ' ')"
  echo "libcfdcore.a $(sha256sum $B/src/libcfdcore.a | cut -d' ' -f1)"
  echo "threads: 1 (OpenMP off; the program is pinned with taskset -c $CORE)"
  echo "thermal: WSL exposes no CPU temperature sensor here; throttling cannot be excluded directly. The per-run CPU MHz snapshots and the run-to-run spread below are the available evidence."
  echo "## build"
  c++ -std=c++20 -O3 -DNDEBUG -I$R/include -I$B/generated/include -I$B/_deps/nlohmann_json-src/include \
    $P/tools/perf_baseline.cpp $B/src/libcfdcore.a -o $W/perf 2>&1 && echo "perf_baseline.cpp $(sha256sum $P/tools/perf_baseline.cpp | cut -c1-16) built" || { echo "build failed"; exit 1; }
  echo "## runs"
  l0=$(load)
  echo "load average (1 min) before: $l0"
  awk -v l="$l0" 'BEGIN{exit !(l > 1.0)}' && { echo "REFUSED: machine not idle (load $l0 > 1.0)"; exit 1; }
  echo "warm-up run (discarded): CPU $(mhz)"
  (cd $R && taskset -c $CORE $W/perf > $W/warmup.txt 2>&1)
  for i in $(seq 1 $REPS); do
    echo "run $i: start CPU $(mhz); load $(load)"
    (cd $R && taskset -c $CORE $W/perf > $W/run$i.txt 2>&1)
    echo "run $i: exit $?; end CPU $(mhz); load $(load)"
    sed 's/^/   /' $W/run$i.txt
  done
  echo "load average (1 min) after: $(load)"
  echo "## aggregate (median of the per-run medians; spread = min-max of the per-run medians)"
  python3 - $W $REPS <<'PY'
import re, statistics, sys
w, reps = sys.argv[1], int(sys.argv[2])
items = {}
pat = re.compile(r"^(GEOM .*?\||SOLV .*?median)\s+([0-9.]+) ms/step")
for i in range(1, reps + 1):
    for line in open(f"{w}/run{i}.txt"):
        m = re.match(r"^(GEOM\s+\S+ \S+)\s.*median\s+([0-9.]+) ms/step", line) or \
            re.match(r"^(SOLV\s+.*?)\s+median\s+([0-9.]+) ms/step", line)
        if m:
            items.setdefault(m.group(1).strip(), []).append(float(m.group(2)))
for k, v in items.items():
    med = statistics.median(v)
    print(f"{k:32s} median {med:9.3f} ms/step  spread {min(v):9.3f} - {max(v):9.3f}  "
          f"({100 * (max(v) - min(v)) / med:5.1f} % of the median, n = {len(v)})")
PY
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG
