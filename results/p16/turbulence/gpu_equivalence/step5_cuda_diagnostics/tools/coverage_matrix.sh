#!/usr/bin/env bash
# P16 Step 5 -- CUDA kernel coverage matrix, by MEASUREMENT.
#
# Traces each scoped test suite separately so every kernel can be attributed to
# the suite that launched it, then checks the decisive property:
#
#     kernels(full suite) \ kernels(scoped set)  ==  empty
#
# A non-empty difference means the amended sanitizer scope would leave a
# production CUDA entry point unchecked, and the scope must grow until it is
# empty. That is a measurement, not an argument about what the long benchmarks
# "probably" launch.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=results/p16/turbulence/gpu_equivalence/step5_cuda_diagnostics
BIN=./build/cuda/tests/unit/gpu/CFDGpuTests
PRELOAD=$PWD/$T/tools/kernel_tracer.so
PER=$T/per_suite
mkdir -p "$PER"

SUITES="DeviceTurbulenceAlgebra DeviceKTransport DeviceEpsilonTransport
        DeviceKEpsilonCorrection DeviceScalarTransportSemantics
        DeviceEffectiveViscosity DeviceWallFaceViscosity DeviceMaxAbsDifference
        ProductionKEpsilonBackend ResidentKEpsilonEntry ResidentWallExchange
        ResidentTurbulentSimple KrylovRestartParity GpuBiCGSTABRestartTest
        TurbulenceBackendTrace GpuJacobiTest"

echo "=== per-suite kernel traces ==="
for s in $SUITES; do
  out="$PER/${s}.txt"
  : > "$out"
  CFD_KERNEL_TRACE="$PWD/$out" LD_PRELOAD="$PRELOAD" "$BIN" --gtest_filter="${s}.*" > /dev/null 2>&1
  printf '  %-34s %s kernels\n' "$s" "$(sort -u "$out" | wc -l)"
done

# Demangle for readability; keep the mangled form as the join key so two
# kernels with the same short name in different translation units stay distinct.
# Demangling order matters and was wrong twice before this:
#   s/.*:://  first  -> cuts inside a PARAMETER type containing "::"
#   s/\(.*//  first  -> cuts at "(anonymous namespace)", leaving nothing
# So: remove "(anonymous namespace)::", THEN the parameter list, THEN the
# namespace. The join key is always the MANGLED name, so these display fixes
# never affected the set differences -- two kernels share the short name
# "countNonFiniteKernel" across translation units and must stay distinct.
demangle() { c++filt < "$1" | sed -E 's/\(anonymous namespace\):://g; s/\(.*//; s/.*:://' | sort -u; }

echo ""
echo "=== coverage matrix (kernel -> scoped suites that launch it) ==="
sort -u "$T/trace_full.txt" > /tmp/k_full.txt
sort -u "$T/trace_scoped.txt" > /tmp/k_scoped.txt
sort -u "$T/trace_channelprod.txt" > /tmp/k_chan.txt

while read -r mangled; do
  short=$(echo "$mangled" | c++filt | sed -E 's/\(anonymous namespace\):://g; s/\(.*//; s/.*:://')
  hits=""
  for s in $SUITES; do
    if grep -qxF "$mangled" "$PER/${s}.txt" 2>/dev/null; then
      hits="$hits ${s}"
    fi
  done
  if [ -z "$hits" ]; then
    printf '  %-34s UNCOVERED\n' "$short"
  else
    printf '  %-34s%s\n' "$short" "$hits"
  fi
done < /tmp/k_full.txt

echo ""
echo "=== the decisive check ==="
echo "kernels in FULL suite        : $(wc -l < /tmp/k_full.txt)"
echo "kernels in SCOPED set        : $(wc -l < /tmp/k_scoped.txt)"
echo "kernels in ResidentChannelProduction : $(wc -l < /tmp/k_chan.txt)"
echo ""
echo "--- in FULL but NOT in SCOPED (must be empty) ---"
comm -23 /tmp/k_full.txt /tmp/k_scoped.txt | c++filt | sed -E 's/\(anonymous namespace\):://g; s/\(.*//; s/.*:://' | sed 's/^/  /'
missing=$(comm -23 /tmp/k_full.txt /tmp/k_scoped.txt | wc -l)
echo "  count: $missing"
echo ""
echo "--- in ResidentChannelProduction but NOT in SCOPED (must be empty to exclude it) ---"
comm -23 /tmp/k_chan.txt /tmp/k_scoped.txt | c++filt | sed -E 's/\(anonymous namespace\):://g; s/\(.*//; s/.*:://' | sed 's/^/  /'
chan_missing=$(comm -23 /tmp/k_chan.txt /tmp/k_scoped.txt | wc -l)
echo "  count: $chan_missing"

echo ""
if [ "$missing" -eq 0 ] && [ "$chan_missing" -eq 0 ]; then
  echo "COVERAGE: PASS -- the scoped set launches every kernel the full suite does,"
  echo "          including every kernel the excluded benchmark group launches."
else
  echo "COVERAGE: FAIL -- $missing kernel(s) unreached by the scoped set."
  echo "          The scope must grow to include them. Do NOT proceed."
fi
