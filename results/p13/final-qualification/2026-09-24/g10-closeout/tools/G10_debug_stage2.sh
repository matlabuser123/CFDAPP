#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G10, Debug+GUI: the FULL suite at the amended timeout.
#
# Amendment: AMENDMENT_G10_debug_timeout.md. Debug+GUI only, --timeout 7200 -> 14400.
# Everything else unchanged and verified below before the run:
#   OMP_NUM_THREADS=1, -j 8, the same 5 documented exclusions verbatim,
#   the same 2550-test set, the SAME EXISTING BINARIES, no rebuild, no source change.
#
# Stage 1 (the three tests alone) was authorized and then deliberately SKIPPED:
# the full suite contains those three tests, so this run subsumes it and is
# stronger evidence, at half the wall time. The Stage 1 script is retained at
# tools/G10_debug_retry.sh; its partial run was stopped and is not evidence.
#
# If any of the three times out or fails an assertion, the failure is preserved
# and G10 STOPS. The timeout is not raised again.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G10=$ROOT/results/p13/final-qualification/2026-09-24/g10-closeout
L=$G10/logs
B=build/qual-debug
cd "$ROOT"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'
THREE=(DriverSeegmillerInlet.MappedInletPreservesTheMassFlow
       DriverSeegmiller.CorrectedSequenceIsDeterministic
       DriverSeegmillerInlet.MediumGridAB)

log=$L/G10_debug_stage2.log
{
echo "P13 G10 -- Debug+GUI, FULL suite at the amended timeout"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "### policy, stated before the run ###"
echo "  OMP_NUM_THREADS : 1              unchanged"
echo "  -j              : 8              unchanged"
echo "  --timeout       : 14400          AMENDED from 7200 (Debug+GUI only)"
echo "  exclusions      : the same 5, verbatim"
echo "    $EXCLUDE"
echo "  rebuild         : NONE -- the binaries under test are the ones the 7200 s run used"
echo "  machine         : cores=$(nproc)  loadavg=$(cut -d' ' -f1 /proc/loadavg)"
echo ""
echo "### pre-run integrity: no rebuild, no source change ###"
echo "  qual-debug GUI test binary mtime : $(stat -c%y "$B/apps/gui/CFDGuiControllerTests" 2>/dev/null | cut -c1-19)"
echo "  registered tests in this tree    : $(ctest --test-dir "$B" -N 2>/dev/null | grep -c 'Test *#')"
echo "  SOURCE_FINGERPRINT_V2 before     : $(bash results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh G10_stage2_before | grep -oE '[0-9a-f]{64}' | head -1)"
echo ""

echo "############ FULL Debug+GUI suite, --timeout 14400 ############"
t0=$(date +%s)
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 --timeout 14400 -E "$EXCLUDE" \
  --output-on-failure > "$L/G10_debug_gui_ctest_amended.log" 2>&1
rc=$?
secs=$(( $(date +%s) - t0 ))
total=$(grep -aoE 'out of [0-9]+' "$L/G10_debug_gui_ctest_amended.log" | tail -1 | grep -oE '[0-9]+')
pct=$(grep -aoE '[0-9]+% tests passed' "$L/G10_debug_gui_ctest_amended.log" | tail -1)
failed=$(grep -aoE '[0-9]+ tests failed' "$L/G10_debug_gui_ctest_amended.log" | tail -1 | grep -oE '^[0-9]+')
echo "  ctest exit status : $rc"
echo "  total tests       : ${total:-unknown}"
echo "  passed            : ${pct:-unknown}"
echo "  failed            : ${failed:-0}"
echo "  Not Run           : $(grep -acE '\(Not Run\)' "$L/G10_debug_gui_ctest_amended.log" || true)"
echo "  Disabled          : $(grep -acE '\(Disabled\)' "$L/G10_debug_gui_ctest_amended.log" || true)"
echo "  runtime           : ${secs} s ($(awk -v s=$secs 'BEGIN{printf "%.1f", s/60}') min)"
echo ""

echo "### the three previously timed-out tests -- must PASS ###"
three_ok=1
for t in "${THREE[@]}"; do
  line=$(grep -aoE "Test +#[0-9]+: $t \.+ +[A-Za-z]+ +[0-9.]+ sec" "$L/G10_debug_gui_ctest_amended.log" || true)
  verdict=$(printf '%s' "$line" | sed -E 's/.*\.\.+ +//')
  printf '  %-58s %s\n' "$t" "${verdict:-NOT FOUND}"
  printf '%s' "$verdict" | grep -q '^Passed' || three_ok=0
done
echo ""

echo "### the two G4-added rows ###"
for t in CliGuiEquivalenceTest.AThermalCaseAgreesBitwise \
         CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli; do
  printf '  %-62s %s\n' "$t" \
    "$(grep -aoE "Test +#[0-9]+: $t \.+ +[A-Za-z]+ +[0-9.]+ sec" "$L/G10_debug_gui_ctest_amended.log" \
       | sed -E 's/.*\.\.+ +//' || echo 'NOT FOUND')"
done
echo ""

if [ "${failed:-0}" != "0" ] || [ $rc -ne 0 ] || [ $three_ok -ne 1 ]; then
  echo "### NON-PASSING TESTS -- preserved, G10 STOPS, the timeout is NOT raised again ###"
  grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/G10_debug_gui_ctest_amended.log" | sed 's/^/    /'
  echo ""
  echo "VERDICT: Debug+GUI NOT PASS"
else
  echo "VERDICT: Debug+GUI PASS -- ${total} / ${total}, 0 failed, all three amended tests PASS"
fi
echo ""

echo "### SOURCE_FINGERPRINT_V2 after -- nothing may have changed to obtain this ###"
after=$(bash results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh G10_stage2_after | grep -oE '[0-9a-f]{64}' | head -1)
echo "  after    : $after"
echo "  required : 8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
[ "$after" = "8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c" ] \
  && echo "  MATCH" || echo "  *** MISMATCH -- STOP ***"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
