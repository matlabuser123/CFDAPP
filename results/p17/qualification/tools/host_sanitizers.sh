#!/usr/bin/env bash
# P17.11 section 28 -- host sanitizers (ASan + UBSan) on the FINAL tree.
#
# SCOPE, stated before the numbers: build/asan is Debug + SANITIZERS ON + CUDA OFF + GUI OFF +
# OpenMP OFF. So this covers the CPU P17 path only. It says nothing about the CUDA kernels, which are
# covered separately by compute-sanitizer, and nothing about the GUI.
#
# The FOCUSED P17 suite is run, not the full regression. Reason, stated rather than implied: ASan on
# Debug is slower again than plain Debug, and plain Debug already pushed four pre-P17
# backward-facing-step tests past the frozen 7200 s timeout. A full ASan regression would reproduce
# those four timeouts and add hours without telling us anything about P17. The scope is therefore
# narrowed DELIBERATELY and reported as narrow -- not quietly.
#
# CI's own ASan settings are used verbatim (from the project's CI job): detect_leaks=1,
# halt_on_error=0, print_stacktrace=1, --timeout 7200.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
Q=results/p17/qualification

echo "=== configuration asserted BEFORE anything runs ==="
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DCFDAPP_ENABLE_SANITIZERS=ON \
  -DCFDAPP_ENABLE_CUDA=OFF -DCFDAPP_BUILD_GUI=OFF -DCFDAPP_ENABLE_OPENMP=OFF \
  > "$Q/logs/60_asan_configure.log" 2>&1
echo "  configure exit: $?"
grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_ENABLE_SANITIZERS|CFDAPP_ENABLE_CUDA|CFDAPP_BUILD_GUI|CFDAPP_ENABLE_OPENMP):' \
  build/asan/CMakeCache.txt | sed 's/^/    /'
SAN=$(grep -E '^CFDAPP_ENABLE_SANITIZERS:' build/asan/CMakeCache.txt | cut -d= -f2)
if [ "$SAN" != "ON" ]; then
  echo "  *** SANITIZERS ARE $SAN -- this tree would prove nothing. FAILING CLOSED. ***"
  exit 1
fi
echo "  --- the sanitizer flags actually compiled in ---"
grep -aoE '\-fsanitize=[a-z,]+' build/asan/CMakeFiles/*/CMakeCXXCompiler.cmake 2>/dev/null | head -2
grep -rhoE '\-fsanitize=[a-z,]+' build/asan/CMakeFiles --include='*.ninja' 2>/dev/null | sort -u | head -3
grep -rhoE '\-fsanitize=[a-z,]+' build/asan/build.ninja 2>/dev/null | sort -u | sed 's/^/    /' | head -3

echo ""
echo "=== build ==="
t0=$(date +%s)
cmake --build build/asan -j 16 > "$Q/logs/60_asan_build.log" 2>&1
rc=$?
echo "  build exit: $rc   ($(( $(date +%s) - t0 )) s)"
if [ "$rc" -ne 0 ]; then
  grep -aE '(^|[^a-zA-Z])error:' "$Q/logs/60_asan_build.log" | head -10 | sed 's/^/    /'
  exit "$rc"
fi

P17RE='IdealGasEOSTest|ThermodynamicPropertiesTest|MachNumberTest|EvaluateDensityFieldTest'
P17RE="$P17RE"'|DensityPropertyDemonstrationTest|TimeDerivativeFieldDensity'
P17RE="$P17RE"'|CompressibleContinuityTest|CompressibleMassFluxTest'
P17RE="$P17RE"'|CompressibleMomentum|CompressibleRelaxedMomentumTest|DilatationalStressTest'
P17RE="$P17RE"'|TotalEnergy|SensibleEnthalpyTest|EnergyEquation'
P17RE="$P17RE"'|LowMachFamilyTest|LowMachRegressionTest'
P17RE="$P17RE"'|Hllc|DensityBased'
P17RE="$P17RE"'|SodShockTubeTest|NormalShockTest|ContactDiscontinuityTest|CompressionExpansionTest'
P17RE="$P17RE"'|ShockConservationTest|ShockDetectorTest|ShockDeterminismTest|SupersonicAdvection'
P17RE="$P17RE"'|P17_10_DryRun|P17_10_Finding|DeviceThermodynamicsEquivalence'

REG=$(ctest --test-dir build/asan -N -R "$P17RE" 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
echo ""
echo "=== focused P17 suite under ASan+UBSan ==="
echo "  registered by the filter: ${REG:-0}"
if [ "${REG:-0}" -lt 100 ]; then
  echo "  *** fewer than 100 tests matched -- the filter is wrong. FAILING CLOSED. ***"
  exit 1
fi

t0=$(date +%s)
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
OMP_NUM_THREADS=1 ctest --test-dir build/asan -j 8 --timeout 7200 -R "$P17RE" --output-on-failure \
  > "$Q/logs/61_asan_focused.log" 2>&1
crc=$?
echo "  ctest exit : $crc   ($(( $(date +%s) - t0 )) s)"
echo "  Passed     : $(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*   Passed' "$Q/logs/61_asan_focused.log")"
echo "  FAILED     : $(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$Q/logs/61_asan_focused.log")"
echo "  Skipped    : $(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*Skipped' "$Q/logs/61_asan_focused.log")"
echo "  Disabled   : $(grep -acE 'Not Run \(Disabled\)' "$Q/logs/61_asan_focused.log")"
echo ""
echo "  --- sanitizer diagnostics found ---"
echo "  AddressSanitizer errors  : $(grep -ac 'ERROR: AddressSanitizer' "$Q/logs/61_asan_focused.log")"
echo "  LeakSanitizer leaks      : $(grep -ac 'ERROR: LeakSanitizer' "$Q/logs/61_asan_focused.log")"
echo "  UBSan runtime errors     : $(grep -ac 'runtime error:' "$Q/logs/61_asan_focused.log")"
if [ "$crc" -ne 0 ]; then
  echo "  --- non-passing tests ---"
  grep -aE '\(Failed\)|\(Timeout\)' "$Q/logs/61_asan_focused.log" | head -20 | sed 's/^/    /'
fi
echo ""
echo "  --- the CONTROL: is the sanitizer actually instrumenting? ---"
echo "  A clean ASan run prints no diagnostics, which is indistinguishable from an uninstrumented"
echo "  binary. The check is that the binary LINKS the sanitizer runtime:"
BIN=build/asan/tests/unit/compressible/CFDCompressibleTests
if [ -x "$BIN" ]; then
  n=$(nm -C "$BIN" 2>/dev/null | grep -ac '__asan\|__ubsan')
  echo "    __asan/__ubsan symbols in CFDCompressibleTests : $n"
  if [ "$n" -eq 0 ]; then
    echo "    *** ZERO sanitizer symbols -- the binary is NOT instrumented and the zeros above are"
    echo "        meaningless. FAILING CLOSED. ***"
    exit 1
  else
    echo "    CONTROL OK: the binary is instrumented, so the diagnostic counts above are real."
  fi
else
  echo "    *** test binary missing -- cannot verify instrumentation ***"
  exit 1
fi
exit "$crc"
