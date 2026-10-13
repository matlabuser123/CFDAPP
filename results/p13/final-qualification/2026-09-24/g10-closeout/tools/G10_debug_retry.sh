#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G10, Debug+GUI under the amended timeout.
#
# Amendment: AMENDMENT_G10_debug_timeout.md. Debug+GUI only; --timeout 7200 -> 14400.
# Everything else unchanged: OMP_NUM_THREADS=1, -j 8, the same 5 documented
# exclusions verbatim, the same 2550-test set, no source touched.
#
# Stage 1: the three previously timed-out tests, alone, under the new ceiling.
#          They must PASS, not merely avoid timing out.
# Stage 2: only if stage 1 passes -- the full Debug+GUI suite, which is what the
#          frozen G10 criterion "Full Debug+GUI regression" requires.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G10=$ROOT/results/p13/final-qualification/2026-09-24/g10-closeout
L=$G10/logs
B=build/qual-debug
cd "$ROOT"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'
THREE='DriverSeegmiller\.CorrectedSequenceIsDeterministic|DriverSeegmillerInlet\.MappedInletPreservesTheMassFlow|DriverSeegmillerInlet\.MediumGridAB'

log=$L/G10_debug_retry.log
{
echo "P13 G10 -- Debug+GUI under the amended timeout"
echo "date: $(date -u +%FT%TZ)"
echo "amendment: --timeout 14400 (was 7200), Debug+GUI ONLY"
echo "unchanged: OMP_NUM_THREADS=1, -j 8, the same 5 exclusions, the same test set"
echo ""
echo "machine: cores=$(nproc) loadavg=$(cut -d' ' -f1 /proc/loadavg)"
echo ""

echo "### no rebuild -- the binaries under test are the ones the 7200 s run used ###"
echo "  a rebuild would change what is being re-tested. Proving the tree is current:"
cmake --build "$B" -j 16 2>&1 | tail -2 | sed 's/^/    /'
echo ""

echo "############ STAGE 1 -- the three tests, under the amended ceiling ############"
t0=$(date +%s)
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 --timeout 14400 -R "$THREE" \
  --output-on-failure > "$L/G10_debug_retry_three.log" 2>&1
rc=$?
secs=$(( $(date +%s) - t0 ))
echo "  ctest exit status : $rc"
echo "  wall time         : ${secs} s ($(awk -v s=$secs 'BEGIN{printf "%.1f", s/60}') min)"
echo ""
echo "  per test:"
grep -aoE 'Test +#[0-9]+: [A-Za-z0-9_.]+ \.+ +[A-Za-z]+ +[0-9.]+ sec' "$L/G10_debug_retry_three.log" \
  | sed -E 's/Test +#[0-9]+: //' | sed 's/^/    /'
echo ""
grep -aE 'tests passed|tests failed out of' "$L/G10_debug_retry_three.log" | tail -1 | sed 's/^/  /'
if [ $rc -ne 0 ]; then
  echo ""
  echo "  *** STAGE 1 DID NOT PASS -- stopping. Stage 2 is not run. ***"
  grep -aE '\(Failed\)|\(Timeout\)' "$L/G10_debug_retry_three.log" | sed 's/^/    /'
  echo ""
  echo "aborted: $(date -u +%FT%TZ)"
  exit 1
fi
echo "  STAGE 1: all three PASS under the amended ceiling."
echo ""

echo "############ STAGE 2 -- the full Debug+GUI suite ############"
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
if [ "${failed:-0}" != "0" ] || [ $rc -ne 0 ]; then
  echo "  --- every non-passing test ---"
  grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/G10_debug_gui_ctest_amended.log" | sed 's/^/    /'
fi
echo ""
echo "  the three, in the full run:"
for t in DriverSeegmillerInlet.MappedInletPreservesTheMassFlow \
         DriverSeegmiller.CorrectedSequenceIsDeterministic \
         DriverSeegmillerInlet.MediumGridAB; do
  printf '    %-58s %s\n' "$t" \
    "$(grep -aoE "Test +#[0-9]+: $t \.+ +[A-Za-z]+ +[0-9.]+ sec" "$L/G10_debug_gui_ctest_amended.log" \
       | sed -E 's/.*\.\.+ +//' || echo 'NOT FOUND')"
done
echo ""
echo "### the two G4-added rows still pass in this run ###"
for t in CliGuiEquivalenceTest.AThermalCaseAgreesBitwise \
         CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli; do
  printf '  %-62s %s\n' "$t" \
    "$(grep -aoE "Test +#[0-9]+: $t \.+ +[A-Za-z]+ +[0-9.]+ sec" "$L/G10_debug_gui_ctest_amended.log" \
       | sed -E 's/.*\.\.+ +//' || echo 'NOT FOUND')"
done
echo ""
echo "### SOURCE_FINGERPRINT_V2 -- nothing may have changed to obtain this ###"
bash results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh G10_after_amendment \
  | grep -E 'SOURCE_FINGERPRINT_V2 =' | sed 's/^/  /'
echo "  required: 8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
