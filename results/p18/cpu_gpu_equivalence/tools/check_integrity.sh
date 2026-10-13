#!/usr/bin/env bash
# P18.8 §64 -- the evidence integrity check.
#
# IT MUST FAIL CLOSED, and a previous phase's version of this script did not: it set `fail=1` inside
# a `$( )` command substitution, which is a SUBSHELL, so the assignment was discarded and the script
# printed three "*** NOT 0 ***" lines and then "INTEGRITY: PASS". Every check below therefore
# assigns to FAIL from the function body directly -- never inside `$( )`, never on the right-hand
# side of a pipeline.
#
# Run with --self-test to prove the fail path works by injecting corruptions.
#
# Usage:  check_integrity.sh [--self-test]
set -u

cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
D=results/p18/cpu_gpu_equivalence
FAIL=0
CHECKS=0

pass() { CHECKS=$((CHECKS + 1)); printf '  OK    %s\n' "$1"; }
bad()  { CHECKS=$((CHECKS + 1)); FAIL=1; printf '  FAIL  %s\n' "$1"; }

# Compares a file's sha256 against a recorded value. Assigns to FAIL in THIS shell.
check_hash() {
  local path="$1" expected="$2" actual
  if [ ! -f "$path" ]; then
    bad "$path -- MISSING"
    return
  fi
  actual="$(sha256sum "$path" | cut -d' ' -f1)"
  if [ "$actual" = "$expected" ]; then
    pass "$path  $actual"
  else
    bad "$path
          expected $expected
          actual   $actual"
  fi
}

check_exists() {
  if [ -f "$1" ]; then pass "present: $1"; else bad "MISSING: $1"; fi
}

# $1 label  $2 expected  $3 actual
check_equal() {
  if [ "$2" = "$3" ]; then pass "$1 = $3"; else bad "$1: expected $2, got $3"; fi
}

echo "P18.8 EVIDENCE INTEGRITY"
echo "date (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "git HEAD: $(git rev-parse HEAD)"
echo ""

echo "--- 1. frozen documents, recomputed against their recorded values ---"
check_hash results/p18/gates/P18_GATES.md \
  921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
check_hash $D/acceptance_gate.md \
  3fc7a020c2e7f3a6a494dd677b6f63927b8ca7571378a44b57c0d1d369103d03
check_hash $D/audit.md \
  d98c57d87e40658a38f04162c03b48d992a3c704077e81532b4d6a4e5ecba9e9
check_hash $D/libm_divergence.md \
  3d6f83ff0cc4b9d71a6808271ccf2e71cc3e79718d0149229dfd6e8ff2d4fae9
check_hash $D/acceptance_gate_A2.md \
  f0973e34068930d119aed6a485646e05e3947945ec898c8d1346181f454d11fe
check_hash $D/regression_prediction.md \
  ab941cf5f7cc08766ccd1c96a68e0618a90b712ee21364455cbf59b09dc1dc69
check_hash results/p18/heat_release/energy_accounting.md \
  20d882371cfba16f2423cf8ee5e13a939df0a677884dcb0e10fd2b710e480ebe
echo ""

echo "--- 2. the evidence set required by the authorization (§63) ---"
for f in kernel_inventory mixture_properties arrhenius reaction_sources heat_release energy \
         batch_reactor conservation invalid_states determinism cuda_diagnostics \
         transfer_residency negative_controls regression coverage_map summary; do
  check_exists "$D/$f.md"
done
echo ""

echo "--- 3. the preserved failures must still be present and non-empty ---"
for f in 02_first_failure_PRESERVED 03_second_failure_PRESERVED; do
  if [ -s "$D/logs/$f.log" ]; then
    pass "preserved: logs/$f.log ($(wc -l < "$D/logs/$f.log") lines)"
  else
    bad "preserved failure missing or empty: logs/$f.log"
  fi
done
# the second preserved failure must still record the failures it recorded
n="$(grep -c 'Failure' "$D/logs/03_second_failure_PRESERVED.log" 2>/dev/null || echo 0)"
check_equal "assertion failures in 03_second_failure_PRESERVED.log" 2478 "$n"
echo ""

echo "--- 4. --use_fast_math must not appear anywhere in the build (authorization §6) ---"
n="$(grep -rho 'use_fast_math' cuda/CMakeLists.txt CMakeLists.txt cmake/ 2>/dev/null | wc -l)"
check_equal "use_fast_math occurrences" 0 "$n"
echo ""

echo "--- 5. the two --fmad flags, read from NINJA's own command lines ---"
# Not from the CMake text: a grep of CMakeLists.txt would pass even if the property never reached
# the compiler.
f="$(ninja -C build/qual-final -t commands cuda/CMakeFiles/cfdcuda.dir/kernels/DeviceSpeciesKernel.cu.o 2>/dev/null | grep -o 'fmad=[a-z]*' | sort -u)"
check_equal "DeviceSpeciesKernel.cu flag" "fmad=false" "$f"
f="$(ninja -C build/qual-final -t commands cuda/CMakeFiles/cfdcuda.dir/kernels/DeviceSpeciesFmadControl.cu.o 2>/dev/null | grep -o 'fmad=[a-z]*' | sort -u)"
check_equal "DeviceSpeciesFmadControl.cu flag" "fmad=true" "$f"
echo ""

echo "--- 6. the CPU reference is byte-identical to P18.8's recorded baseline ---"
# Against baseline/00_start_state.log, NOT against git HEAD: nothing in the P18 series has been
# committed, so `git status` lists all of P18.1-P18.7's work and could never read 0. The question
# this phase has to answer is whether P18.8 touched the CPU reference, and these 12 hashes were
# recorded for exactly that.
check_hash include/cfd/species/MassFractionState.hpp \
  34a70761f2debb6da62bc97e5a0905997c6fdb7df8d736506f1856e8472b0159
check_hash include/cfd/species/SpeciesTransport.hpp \
  850f5a5e03a5ea701988d2cb260230a46e9ac30ae8d0392c2fd0c27611852a95
check_hash include/cfd/species/MixtureThermodynamics.hpp \
  58d9cf18b8b298664be50b6d9d75b132db1fa62e88b054a86d932361194ca540
check_hash include/cfd/species/ReactionMechanism.hpp \
  0e0b8829ab4a14389c145392055921c8d4bb79c3eafbc48dbb450c27db46a5d2
check_hash include/cfd/species/ArrheniusKinetics.hpp \
  44d6561f14bf23c1d81a4013cb641e281c65464da0c880bf39db517ffc7e5e58
check_hash include/cfd/species/HeatRelease.hpp \
  3b8efc2d4778f106cff7f4fcb653e77ced57130b9a5d3029ccc499b50590e65c
check_hash src/species/MassFractionState.cpp \
  429321301b079cd80b26b8a2dcef4929fbf85ece08f28cbe7ff08179cffa1f4f
check_hash src/species/SpeciesTransport.cpp \
  69cc3141ea92d286fbf5c55fc8e40bb3913a78edfe659503b9dd8f7572cbf508
check_hash src/species/MixtureThermodynamics.cpp \
  313928eb1d175532da4da14036e045eff75bc58bb6092438d70b680c9e783f8f
check_hash src/species/ReactionMechanism.cpp \
  0145fab31061a97ceb7eb72e752e7fe17057a97370e6d30365d7d649205de236
check_hash src/species/ArrheniusKinetics.cpp \
  ba095f0e5ec785bf89f70834bf9359455bd3a901600372890ceceae6722af795
check_hash src/species/HeatRelease.cpp \
  3adfd95b3438f7c59fd22f4fa23281680f17220936364ede2faa35150ac31e65
echo ""

echo "--- 7. the fmad control must not be CALLED from production ---"
# Only the test suite may call it. A solver-path caller would put a deliberately contracted kernel
# on the production path.
#
# The control's OWN declaration header is excluded: a declaration is not a call, and the first
# version of this check flagged include/cfd/gpu/DeviceSpeciesFmadControl.hpp for declaring the
# functions it exists to declare.
n="$(grep -rln 'FmadControlDevice' src apps include 2>/dev/null \
      | grep -v '^include/cfd/gpu/DeviceSpeciesFmadControl\.hpp$' | wc -l)"
check_equal "production files referencing the fmad control" 0 "$n"
# and the definition must live only in the control translation unit
n="$(grep -rln 'FmadControlDevice' cuda 2>/dev/null \
      | grep -v '^cuda/kernels/DeviceSpeciesFmadControl\.cu$' | wc -l)"
check_equal "other CUDA files referencing the fmad control" 0 "$n"
# The only callers may be under tests/.
#
# Scoped to the source directories rather than '.' with a post-hoc `grep -v '^./build'`: grep
# traverses what it is told to traverse, and walking the build trees took this check from under a
# second to over ten minutes while the regression was competing for the disk.
callers="$(grep -rln 'FmadControlDevice' src apps include cuda tests \
      --include='*.cpp' --include='*.cu' --include='*.hpp' --include='*.cuh' \
      2>/dev/null | grep -v 'DeviceSpeciesFmadControl' | sort | tr '\n' ' ')"
if [ -z "$callers" ]; then
  bad "nothing calls the fmad control at all -- then it is not a live control"
else
  nontest="$(echo "$callers" | tr ' ' '\n' | grep -v '^tests/' | grep -c . )"
  if [ "$nontest" -eq 0 ]; then
    pass "the only callers of the fmad control are under tests/: $callers"
  else
    bad "the fmad control is called from outside tests/: $callers"
  fi
fi
echo ""

echo "--- 8. claimed test counts must match the logs they come from ---"
n="$(grep -c '^\[       OK \]' "$D/logs/08_A2_rerun_FRESH.log" 2>/dev/null || echo 0)"
if [ "$n" -ge 34 ]; then
  pass "08_A2_rerun_FRESH.log records $n passing tests (>= 34: 17 link one + 17 link two)"
else
  bad "08_A2_rerun_FRESH.log records only $n passing tests"
fi
if grep -q 'E13: PASS' "$D/logs/09_E13_cuda_diagnostics.log" 2>/dev/null; then
  pass "09_E13_cuda_diagnostics.log records E13: PASS"
else
  bad "09_E13_cuda_diagnostics.log does not record E13: PASS"
fi
echo ""

echo "--- 9. clang-format over every P18.8 source ---"
n="$(clang-format-18 --dry-run --Werror \
  include/cfd/gpu/DeviceSpeciesThermochemistry.hpp \
  include/cfd/gpu/DeviceSpeciesLaunch.hpp \
  include/cfd/gpu/DeviceSpeciesFmadControl.hpp \
  cuda/kernels/DeviceSpeciesKernel.cu \
  cuda/kernels/DeviceSpeciesFmadControl.cu \
  tests/unit/species/test_device_species_equivalence.cpp \
  tests/unit/gpu/SpeciesEquivalenceCompare.hpp \
  tests/unit/gpu/test_device_species_gpu_equivalence.cpp \
  tests/unit/gpu/test_device_species_a2_dryrun.cpp \
  tests/unit/gpu/test_device_species_sanitizer_control.cpp 2>&1 | grep -c 'violations')"
check_equal "clang-format violations" 0 "$n"
echo ""

if [ "${1:-}" = "--self-test" ]; then
  echo "--- 10. SELF-TEST: the fail path must actually fail ---"
  echo "    (this is here because a previous phase's integrity script printed PASS while"
  echo "     printing three failures, having set its flag inside a subshell)"
  before="$FAIL"
  FAIL=0
  tmp="$(mktemp)"
  echo "corrupted" > "$tmp"
  echo "  injecting a wrong hash:"
  check_hash "$tmp" 0000000000000000000000000000000000000000000000000000000000000000
  echo "  injecting a missing file:"
  check_exists "$D/this_file_does_not_exist.md"
  echo "  injecting a count mismatch:"
  check_equal "a deliberately wrong count" 1 2
  rm -f "$tmp"
  if [ "$FAIL" -eq 1 ]; then
    echo "  SELF-TEST OK: three injected corruptions all set FAIL in this shell."
    FAIL="$before"
  else
    echo "  SELF-TEST FAILED: the injected corruptions did NOT set FAIL. This script is"
    echo "  FAIL-OPEN and none of its PASS verdicts can be believed."
    FAIL=1
  fi
  echo ""
fi

echo "=============================================="
echo "checks run: $CHECKS"
if [ "$FAIL" -eq 0 ]; then
  echo "INTEGRITY: PASS"
else
  echo "INTEGRITY: FAIL"
fi
exit "$FAIL"
