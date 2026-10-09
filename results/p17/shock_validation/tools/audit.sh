#!/usr/bin/env bash
# P17.9 -- audit of what shock / discontinuity infrastructure ALREADY exists.
# Run BEFORE any P17.9 source is written. Section 1 of the authorization.
#
# Every count is paired with the files it came from, so a 0 can be told apart from a blind grep.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
ROOTS="include src cuda tests apps results/p17"

echo "P17.9 AUDIT -- existing shock / discontinuity infrastructure"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo "searched roots: $ROOTS"
echo ""

echo "### 1. the term sweep (section 1's list, verbatim) ###"
printf '%-26s %6s %6s   %s\n' term files lines "where"
sweep() {
  local label="$1" pat="$2" scope="${3:-$ROOTS}"
  local f l
  f=$(grep -rlE "$pat" $scope 2>/dev/null | wc -l)
  l=$(grep -rhE "$pat" $scope 2>/dev/null | wc -l)
  local where
  where=$(grep -rlE "$pat" $scope 2>/dev/null | head -3 | tr '\n' ' ')
  printf '%-26s %6s %6s   %s\n' "$label" "$f" "$l" "${where:- --}"
}
sweep "Sod"                    '\bSod\b|sod_'
sweep "shock tube"             'shock.?tube|shockTube'
sweep "normal shock"           'normal.?shock|normalShock'
sweep "Rankine-Hugoniot"       'Rankine|Hugoniot'
sweep "supersonic"             'supersonic|Supersonic'
sweep "contact discontinuity"  'contact.?discontinuity|contactDiscontinuity'
sweep "compression wave"       'compression.?wave|compressionWave'
sweep "expansion wave"         'expansion.?wave|expansionWave|rarefaction|Rarefaction'
sweep "Riemann exact"          'exact.?Riemann|Riemann.?exact|exactRiemann'
sweep "shock position"         'shock.?position|shockPosition'
sweep "density ratio"          'density.?ratio|densityRatio'
sweep "pressure ratio"         'pressure.?ratio|pressureRatio'
sweep "Mach relation"          'Mach.?relation|machRelation'
echo ""

echo "### 2. what P17.7 / P17.8 actually left behind ###"
echo "-- the independent exact Riemann reference (P17.7's, frozen then AMENDED):"
ls -la results/p17/hllc/tools/exact_riemann.py 2>/dev/null | sed 's/^/   /'
echo "   sha256 $(sha256sum results/p17/hllc/tools/exact_riemann.py 2>/dev/null | awk '{print $1}')"
echo "   what it EXPORTS (def/class at module level):"
grep -nE '^(def|class) ' results/p17/hllc/tools/exact_riemann.py 2>/dev/null | sed 's/^/     /'
echo ""
echo "-- Sod appearances in P17.7's reference data / tests:"
grep -rn 'sod' results/p17/hllc/tools/*.py tests/unit/compressible/*.hpp tests/unit/compressible/test_hllc_flux.cpp 2>/dev/null \
  | head -8 | sed 's/^/     /'
echo ""
echo "-- P17.8's canonical cases (what is already integrated at SOLVER level):"
grep -nE '^TEST\(' tests/integration/compressible/test_density_based_canonical.cpp 2>/dev/null | sed 's/^/     /'
grep -cE 'contact|Contact' tests/unit/compressible/test_density_based_solver.cpp 2>/dev/null \
  | sed 's/^/     contact mentions in the P17.8 unit suite: /'
echo ""

echo "### 3. the solver API P17.9 must drive (P17.8's, unchanged) ###"
grep -nE 'DensityBasedSolver\(|solve\(|assembleResidual\(|globalTimeStep\(|applyUpdate\(|toPrimitive\(|fromPrimitive\(' \
  include/cfd/compressible/DensityBasedSolver.hpp | sed 's/^/   /'
echo ""
echo "   boundary treatments available:"
sed -n '/^enum class BoundaryKind/,/^};/p' include/cfd/compressible/DensityBasedSolver.hpp \
  | grep -E '^\s+(InviscidWall|SupersonicInflow|SupersonicOutflow)' | sed 's/^/   /'
echo ""

echo "### 4. mesh factories usable for a 1D-on-2D shock tube ###"
grep -nE 'static Mesh create(Cartesian2D|Rectilinear2D|Graded2D)' include/cfd/mesh/MeshGeometry.hpp | sed 's/^/   /'
echo ""

echo "### 5. existing validation-harness conventions to REUSE, not reinvent ###"
echo "   grid-convergence / GCI helpers:"
grep -rlE 'richardson|Richardson|gci|GCI' include/ src/ tests/ 2>/dev/null | head -5 | sed 's/^/     /'
echo "   L1/L2 error helpers:"
grep -rlE 'l1CellError|l2CellError|ManufacturedFields' include/ src/ 2>/dev/null | head -5 | sed 's/^/     /'
echo "   validation report writers:"
ls -1 results/validation/ 2>/dev/null | head -6 | sed 's/^/     /'
echo ""

echo "### 6. is there ANY existing shock benchmark that already runs a solver? ###"
n=$(grep -rlE '\bSod\b|shock.?tube' tests/ 2>/dev/null | wc -l)
echo "   test files mentioning Sod / shock tube: $n"
grep -rlE '\bSod\b|shock.?tube' tests/ 2>/dev/null | sed 's/^/     /'
echo "   SANITY -- the pattern must FIND something where Sod is known to live."
echo "   The first version of this control used a \b-anchored sod and returned 0, which reads as"
echo "   'no reference data' when the truth is that \b cannot match inside sod_literature --"
echo "   underscore is a word character. Corrected; BOTH forms are printed so the difference is"
echo "   visible rather than silently fixed:"
echo "     \bsod\b anchored : $(grep -rlE 'sod' results/p17/hllc/ 2>/dev/null | wc -l) files   <- the BLIND one"
echo "     plain sod        : $(grep -rl 'sod' results/p17/hllc/ 2>/dev/null | wc -l) files   (must be > 0)"
echo "     Sod|sod in p17   : $(grep -rlE 'Sod|sod' results/p17/ 2>/dev/null | wc -l) files"
echo ""
echo "done: $(date -u +%FT%TZ)"
