#!/usr/bin/env bash
# P18.9 section 45 -- LIVE CONTROL for the host sanitizers.
#
# "0 findings" is byte-for-byte what an UNINSTRUMENTED run prints. The build-time check that
# -fsanitize=address,undefined appears in the compile line proves the flag was passed; it does not
# prove the runtime was active while the 2 768 tests ran. This supplies that proof three ways:
#
#   1. ASan's own runtime banner, obtained from an ACTUAL qualification test binary. ASAN_OPTIONS=help=1
#      makes the ASan runtime print its option list and exit -- a binary with no ASan runtime linked
#      cannot do that.
#   2. The test binaries are linked against the sanitizer runtimes (ldd, nm).
#   3. PLANTED DEFECTS, compiled with the same flags and run with the same ASAN_OPTIONS/UBSAN_OPTIONS
#      as the suite, which the tools must report.
#
# ONE PLANTED DEFECT PER PROGRAM, and that is the point of this comment. The first version of this
# control put a heap-buffer-overflow, a signed overflow and a leak in a SINGLE program and reported
# "UBSan did NOT report a signed overflow". UBSan was fine: ASan aborts on a heap-buffer-overflow
# regardless of halt_on_error=0 (that option governs recoverable errors; continuing past a memory
# error needs -fsanitize-recover=address), so execution never reached the other two defects. A
# control that tests three tools through one abort tests one tool.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
OUT="${1:?usage: asan_live_control.sh <output-log>}"
BIN=build/asan/tests/unit/species/CFDSpeciesTests
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ASAN_OPTS='detect_leaks=1:halt_on_error=0'
UBSAN_OPTS='print_stacktrace=1:halt_on_error=0'

# $1 name  $2 C++ body  $3 grep pattern the tool must emit
probe() {
  local name="$1" body="$2" pattern="$3"
  printf '%s\n' "$body" > "$WORK/$name.cpp"
  if ! g++ -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
       -o "$WORK/$name" "$WORK/$name.cpp" 2>"$WORK/$name.cc.log"; then
    echo "    $name: COMPILE FAILED"
    sed 's/^/      /' "$WORK/$name.cc.log" | head -5
    return 1
  fi
  ASAN_OPTIONS="$ASAN_OPTS" UBSAN_OPTIONS="$UBSAN_OPTS" \
    "$WORK/$name" > "$WORK/$name.run.log" 2>&1
  local rc=$? hits
  hits=$(grep -c "$pattern" "$WORK/$name.run.log")
  printf '    %-22s exit %-3s  matches for %-28s : %s\n' "$name" "$rc" "\"$pattern\"" "$hits"
  grep -E "$pattern" "$WORK/$name.run.log" | head -2 | sed 's/^/        /'
  [ "$hits" -gt 0 ]
}

{
  echo "============================================================"
  echo "P18.9 section 45 -- HOST SANITIZER LIVE CONTROL"
  echo "============================================================"
  echo "date (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "options used here are IDENTICAL to the suite's:"
  echo "  ASAN_OPTIONS=$ASAN_OPTS"
  echo "  UBSAN_OPTIONS=$UBSAN_OPTS"
  echo ""
  echo "--- 1. ASan RUNTIME banner from a real qualification binary ---"
  echo "    binary: $BIN"
  echo "    sha256: $(sha256sum "$BIN" 2>/dev/null | cut -d' ' -f1)"
  banner=$(ASAN_OPTIONS=help=1 "$BIN" --gtest_list_tests 2>&1 | grep -c 'halt_on_error\|detect_leaks\|malloc_context_size')
  echo "    ASan option lines printed by the runtime: $banner"
  if [ "$banner" -gt 0 ]; then
    echo "    => an ASan RUNTIME is linked and active in this binary."
  else
    echo "    *** NO ASan RUNTIME BANNER -- the suite was not instrumented at runtime. ***"
  fi
  echo ""
  echo "--- 2. sanitizer runtimes actually linked ---"
  ldd "$BIN" 2>/dev/null | grep -iE 'asan|ubsan' | sed 's/^/    /' || true
  echo "    __asan_/__ubsan_ dynamic symbols: $(nm -D "$BIN" 2>/dev/null | grep -c '__asan_\|__ubsan_')"
  echo ""
  echo "--- 3. PLANTED DEFECTS, one program each ---"
  ok=1

  probe asan_overflow '#include <cstdlib>
#include <cstdio>
int main() {
  int* a = static_cast<int*>(std::malloc(4 * sizeof(int)));
  a[7] = 1;
  std::printf("%d\n", a[7]);
  std::free(a);
  return 0;
}' 'ERROR: AddressSanitizer: heap-buffer-overflow' || ok=0

  probe ubsan_overflow '#include <cstdio>
int main() {
  volatile int big = 2147483647;
  volatile int one = 1;
  int r = big + one;
  std::printf("%d\n", r);
  return 0;
}' 'runtime error:' || ok=0

  probe ubsan_shift '#include <cstdio>
int main() {
  volatile int n = 33;
  volatile int v = 1;
  int r = v << n;
  std::printf("%d\n", r);
  return 0;
}' 'runtime error:' || ok=0

  probe lsan_leak '#include <cstdlib>
int main() {
  void* p = std::malloc(123);
  (void)p;
  return 0;
}' 'Direct leak\|ERROR: LeakSanitizer' || ok=0

  echo ""
  echo "============ VERDICT ============"
  if [ "$ok" -eq 1 ] && [ "$banner" -gt 0 ]; then
    echo "  LIVE. AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer each report a"
    echo "  planted defect under the SAME options the suite used, and the qualification binary"
    echo "  carries an active ASan runtime linked against libasan and libubsan."
    echo ""
    echo "  So the suite's 0 ASan / 0 UBSan / 0 Leak findings over 2 768 passing tests is a"
    echo "  MEASUREMENT, not an absence of instrumentation."
  else
    echo "  NOT ESTABLISHED -- at least one tool failed to report its planted defect, so the"
    echo "  suite's clean result cannot be relied on."
  fi
} > "$OUT" 2>&1
cat "$OUT"
