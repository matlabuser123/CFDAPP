#!/usr/bin/env bash
# P12-MESH-006 CPU performance baseline (acceptance_gate.md "CPU baseline": measurement only, no
# optimization). Lid-driven cube Re = 100 (cases/lid_driven_cavity_3d: QUICK, face flux automatic =
# Rhie-Chow, relaxation 0.7/0.3, tolerances 1e-6) at 16^3, 32^3, 64^3 through the Release CLI, one at a
# time with nothing else running. Single thread: the Release build has CFDAPP_ENABLE_OPENMP=OFF.
# Timing: /usr/bin/time -v (wall clock; and user+sys CPU time, which WSL wall-clock jumps cannot affect).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CLI=$R/build/release/apps/cli/cfdapp
W=$HOME/m6cpu; rm -rf $W; mkdir -p $W
echo "# P12-MESH-006 CPU baseline, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# host: $(uname -srm); CPU: $(lscpu | sed -n 's/^Model name: *//p'); logical CPUs: $(nproc); memory: $(free -g | awk '/Mem:/{print $2}') GB"
echo "# build: build/release, CMAKE_BUILD_TYPE=$(grep CMAKE_BUILD_TYPE: $R/build/release/CMakeCache.txt | cut -d= -f2), $(grep CMAKE_CXX_FLAGS_RELEASE: $R/build/release/CMakeCache.txt | cut -d= -f2), $(c++ --version | head -1)"
echo "# OpenMP: $(grep CFDAPP_ENABLE_OPENMP: $R/build/release/CMakeCache.txt | cut -d= -f2) (single thread); cfdapp sha256 $(sha256sum $CLI | cut -c1-64)"
echo "# other processes using CPU at start: $(ps -eo pcpu,comm --sort=-pcpu | awk 'NR>1 && $1>5' | wc -l)"
echo
printf "%6s %9s %6s %12s %12s %10s %12s %12s %16s\n" grid cells iters wall_s cpu_s status s_per_it_wall s_per_it_cpu cell_it_per_s_cpu
for n in 16 32 64; do
  cp -r $R/cases/lid_driven_cavity_3d $W/cube$n
  rm -rf $W/cube$n/results
  python3 - $W/cube$n/mesh.json $n <<'EOF'
import json, sys
p, n = sys.argv[1], int(sys.argv[2])
json.dump({"type": "structured_cartesian", "nx": n, "ny": n, "nz": n}, open(p, "w"), indent=2)
EOF
  /usr/bin/time -v $CLI --case $W/cube$n > $W/cube$n.out 2> $W/cube$n.time
  it=$(sed -n 's/^Iterations: //p' $W/cube$n.out)
  st=$(sed -n 's/^Converged: //p' $W/cube$n.out)
  wall=$(sed -n 's/.*Elapsed (wall clock) time (h:mm:ss or m:ss): //p' $W/cube$n.time | awk -F: '{ if (NF==3) print $1*3600+$2*60+$3; else print $1*60+$2 }')
  user=$(sed -n 's/.*User time (seconds): //p' $W/cube$n.time)
  sys=$(sed -n 's/.*System time (seconds): //p' $W/cube$n.time)
  rss=$(sed -n 's/.*Maximum resident set size (kbytes): //p' $W/cube$n.time)
  cells=$((n*n*n))
  cpu=$(python3 -c "print($user + $sys)")
  printf "%6s %9d %6s %12.2f %12.2f %10s %12.4f %12.4f %16.0f   peak RSS %s KB\n" "${n}^3" $cells $it $wall $cpu "$st" \
    $(python3 -c "print($wall/$it)") $(python3 -c "print($cpu/$it)") $(python3 -c "print($cells*$it/$cpu)") $rss
done
echo
echo "# linear-solver iteration counts: not exported by the CLI/metadata (not available)."
echo "# full CLI reports:"
for n in 16 32 64; do echo "## ${n}^3"; grep -E "Mesh:|Converged|Iterations|residual|Continuity|Mass|Face flux" $W/cube$n.out; done
