#!/usr/bin/env bash
# P17.8 section 1 -- audit BEFORE any implementation.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/density_based_solver
cd "$ROOT" || exit 1
mkdir -p "$P17/logs" "$P17/tools"
SRC=(include src cuda tests apps)

log=$P17/logs/00_audit.log
{
echo "P17.8 AUDIT -- existing density-based / explicit-solver infrastructure"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### 1a. the section 1 term list -- FILES then LINES ###"
printf '  %-30s %7s %7s\n' TERM FILES LINES
for t in 'DensityBased' 'density_based' 'EulerSolver' 'CompressibleSolver' 'explicit update' \
         'conserved update' 'finite-volume residual' 'finite volume residual' 'CFL' 'cfl' \
         'local time stepping' 'localTimeStep' 'global time stepping' 'pseudo-time' \
         'pseudoTime' 'Runge-Kutta' 'RungeKutta' 'rungeKutta' 'forward Euler' 'residual norm' \
         'residualNorm' 'shock solver' 'HLLC' 'timeStep' 'deltaTime' 'restart' 'checkpoint'; do
  f=$(grep -rliI -- "$t" "${SRC[@]}" 2>/dev/null | wc -l)
  l=$(grep -rniI -- "$t" "${SRC[@]}" 2>/dev/null | wc -l)
  printf '  %-30s %7s %7s\n' "$t" "$f" "$l"
done
echo ""

echo "### 1b. the DECISIVE terms, in full ###"
for t in DensityBased EulerSolver CompressibleSolver 'Runge-Kutta' RungeKutta 'pseudoTime' \
         'residualNorm' 'localTimeStep'; do
  n=$(grep -rniI -- "$t" "${SRC[@]}" 2>/dev/null | wc -l)
  echo "  --- $t ($n) ---"
  grep -rniI -- "$t" "${SRC[@]}" 2>/dev/null | sed 's/^/      /' | head -14
done
echo ""

echo "### 1c. CFL as a WHOLE WORD, and where it lives ###"
echo "  word-boundary CFL/cfl: $(grep -rnwiI -- 'cfl' "${SRC[@]}" 2>/dev/null | wc -l)"
grep -rnwiI -- 'cfl' "${SRC[@]}" 2>/dev/null | sed 's/^/    /' | head -20
echo ""
echo "  files that mention courant:"
grep -rliI -- 'courant' "${SRC[@]}" 2>/dev/null | sed 's/^/    /' | head
echo ""

echo "### 1d. EXISTING SOLVER INTERFACES and dispatch ###"
echo "  --- classes named *SIMPLE / *Solver in include/ ---"
grep -rnE '^(class|struct) [A-Za-z]*(SIMPLE|Solver)' include/ 2>/dev/null | sed 's/^/    /' | head -20
echo ""
echo "  --- how a solver is SELECTED (the dispatch surface) ---"
for pat in 'compressible.method' 'solver_type' 'solverType' 'method =' 'SolverKind' 'SolverBackend' \
           'useCompressible' 'compressible.enabled' 'runCompressible'; do
  printf '    %-28s %s\n' "$pat" "$(grep -rnI -- "$pat" include/ src/ apps/ 2>/dev/null | wc -l)"
done
echo ""
echo "  --- ProjectRunner: what it can dispatch to ---"
grep -nE 'SIMPLE|Compressible|solve|run' include/cfd/app/ProjectRunner.hpp 2>/dev/null \
  | sed 's/^/    /' | head -24
echo ""

echo "### 1e. the CASE FORMAT -- what keys the compressible block already has ###"
grep -rnE 'compressible\.[a-z_]+' include/ src/ 2>/dev/null \
  | grep -oE 'compressible\.[a-z_]+' | sort | uniq -c | sort -rn | sed 's/^/    /' | head -24
echo ""

echo "### 1f. TIME-STEP infrastructure that already exists ###"
for pat in 'TimeControl' 'timeControl' 'transient' 'time.transient' 'deltaT' 'dt_' 'nTimeSteps' \
           'endTime' 'currentTime'; do
  printf '    %-24s %s\n' "$pat" "$(grep -rnI -- "$pat" include/ src/ 2>/dev/null | wc -l)"
done
echo ""
echo "  the transient/time struct, if any:"
grep -rlnE 'struct .*Time|class .*Time' include/ 2>/dev/null | sed 's/^/    /' | head
echo ""

echo "### 1g. RESTART / CHECKPOINT infrastructure ###"
grep -rliI -- 'restart\|checkpoint' include/ src/ 2>/dev/null | sed 's/^/    /' | head -20
echo ""
echo "  what is serialized today (field names in the restart writer):"
grep -rnE 'write|serialize' $(grep -rliI -- 'restart' include/cfd 2>/dev/null | head -3) 2>/dev/null \
  | sed 's/^/    /' | head -12
echo ""

echo "### 1h. MESH LOOP conventions -- owner/neighbour and face areas ###"
for pat in 'owner(' 'neighbour(' 'isBoundaryFace' 'faceArea' 'areaVector' 'faceNormal' \
           'cellVolume' 'volume(' 'faceCount' 'cellCount'; do
  printf '    %-22s %s\n' "$pat" "$(grep -rnI -- "$pat" include/cfd/mesh/ 2>/dev/null | wc -l)"
done
echo ""
echo "  the Mesh accessors P17.8 will loop over:"
grep -nE '\[\[nodiscard\]\].*(owner|neighbour|faceArea|areaVector|volume|Count)' \
  include/cfd/mesh/Mesh.hpp 2>/dev/null | sed 's/^/    /' | head -20
echo ""

echo "### 1i. what P17.7 left for P17.8 to consume ###"
grep -nE 'reconstructFirstOrder|ReconstructedFaceState|hllcFlux|integrateOverArea|makePrimitiveState|toConserved|FluxVector|ConservedState' \
  include/cfd/compressible/HllcFlux.hpp | sed 's/^/    /'
echo ""

echo "### 1j. the GPU solver surface -- is there a resident solve to extend? ###"
for s in GpuResidentSolve ResidentSimple DeviceHllc residentSolve; do
  printf '    %-24s files: %s\n' "$s" "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
