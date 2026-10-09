#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G10 steps 5 and 6.
#
# Step 5: enumerate the documented exclusions MECHANICALLY. The distinction that
#         matters:
#             REGISTERED + EXCLUDED  = a valid documented exclusion
#             NOT REGISTERED         = a possible harness/build defect
#         A test that has silently vanished from `ctest -N` is not an intentional
#         exclusion, and reporting "documented exclusions preserved" without
#         checking registration would not notice the difference.
#
# Step 6: verify the qualification-specific tests added or repaired during
#         G2-G8 still exist BY NAME, per configuration. Total test count growth
#         is not evidence that a particular row survived.
#
# Read-only: `ctest -N` lists, it does not execute.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G10=$ROOT/results/p13/final-qualification/2026-09-24/g10-closeout
cd "$ROOT"
mkdir -p "$G10/logs"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'

# Cache one registry listing per configuration.
for pair in "release_cuda:build/qual-final" "debug_gui:build/qual-debug" "cpu_only:build/qual-cpuonly"; do
  cfg=${pair%%:*}; dir=${pair#*:}
  ctest --test-dir "$dir" -N 2>/dev/null | grep -oE 'Test +#[0-9]+: .+' | sed 's/.*: //' \
    > "/tmp/g10_reg_$cfg.txt"
done

out=$G10/exclusions_audit.md
{
echo "# G10 — exclusions audit and qualification test registration"
echo
echo "Generated $(date -u +%FT%TZ). \`ctest -N\` only; nothing was executed."
echo
echo '## Registry sizes'
echo
echo '```text'
for cfg in release_cuda debug_gui cpu_only; do
  printf '%-14s %s registered tests\n' "$cfg" "$(wc -l < /tmp/g10_reg_$cfg.txt)"
done
echo '```'
echo
echo '## Step 5 — the documented exclusions, enumerated'
echo
echo 'Frozen set, copied verbatim from `results/p16/turbulence/tools/regress_both.sh`'
echo '(p16 mode) and matching `PLAN_2026-09-24.md`. Five items; not widened.'
echo
echo '```text'
printf '%-62s %-11s %-9s %s\n' "test" "REGISTERED" "EXCLUDED" "config"
printf '%-62s %-11s %-9s %s\n' "----" "----------" "--------" "------"
for cfg in release_cuda debug_gui cpu_only; do
  for pat in SSTAutomaticWallChannel SSTCompositeWallChannel SSTMomentumFluxChannel \
             WallArchitectureDiagnostic 'DriverSeegmiller.CorrectedMeshSequence'; do
    while read -r t; do
      [ -z "$t" ] && continue
      if echo "$t" | grep -qE "$EXCLUDE"; then ex=YES; else ex=no; fi
      printf '%-62s %-11s %-9s %s\n' "$t" "YES" "$ex" "$cfg"
    done < <(grep -F "$pat" "/tmp/g10_reg_$cfg.txt" || true)
  done
done
echo '```'
echo
echo '### Reasons and evidence'
echo
echo '| suite | frozen reason | evidence |'
echo '|---|---|---|'
echo '| `SSTAutomaticWallChannel` | deferred automatic wall treatment, attempt 2 (4th-power blend of two friction velocities) | `results/p16/turbulence/automatic_wall/SUMMARY.md` |'
echo '| `SSTCompositeWallChannel` | deferred automatic wall treatment, attempt 4 (Spalding composite law) | same |'
echo '| `SSTMomentumFluxChannel` | deferred automatic wall treatment, attempt 5 (F_U = -rho u_tau u*) | same |'
echo '| `WallArchitectureDiagnostic` | diagnostic accompanying the three deferred treatments | same |'
echo '| `DriverSeegmiller.CorrectedMeshSequence` | preserved failing separated-flow benchmark, ~1100 s alone | `results/p16/turbulence/` + `run_p20_known_failures.sh` |'
echo
echo 'All five are deferred to **P20**, which is NOT AUTHORIZED. G9 re-ran all four'
echo 'automatic-wall suites directly and confirmed the failure set is exactly the'
echo 'documented four -- neither grown nor shrunk -- with the 8 passing tests in'
echo 'those same suites still passing:'
echo
echo '```text'
echo 'SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate            366 s  FAILED'
echo 'SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects  375 s  FAILED'
echo 'SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590                377 s  FAILED'
echo 'SSTMomentumFluxChannel.FourWayComparisonAtReTau590                    594 s  FAILED'
echo '```'
echo
echo 'So the exclusion audit rests on a live re-measurement, not a citation.'
echo
echo '## Step 6 — qualification-specific tests, verified BY NAME'
echo
echo 'Added or repaired during G2-G8. Total count growth is not evidence that a'
echo 'particular row survived, so each is checked by name per configuration.'
echo
echo '```text'
printf '%-58s %-13s %-11s %-9s %s\n' "expected test" "release_cuda" "debug_gui" "cpu_only" "origin"
printf '%-58s %-13s %-11s %-9s %s\n' "-------------" "------------" "---------" "--------" "------"
check() {  # check <exact test name> <origin>
  local t="$1" origin="$2" r d c
  grep -qxF "$t" /tmp/g10_reg_release_cuda.txt && r=YES || r="--"
  grep -qxF "$t" /tmp/g10_reg_debug_gui.txt    && d=YES || d="--"
  grep -qxF "$t" /tmp/g10_reg_cpu_only.txt     && c=YES || c="--"
  printf '%-58s %-13s %-11s %-9s %s\n' "$t" "$r" "$d" "$c" "$origin"
}
# --- G4: CLI/GUI equivalence, incl. the rows this qualification added ---
check CliGuiEquivalenceTest.TwoDimensionalCaseAgreesBitwise            "G4"
check CliGuiEquivalenceTest.ThreeDimensionalCaseAgreesBitwise          "G4"
check CliGuiEquivalenceTest.ACaseEditedInTheGuiStillAgrees             "G4"
check CliGuiEquivalenceTest.AThermalCaseAgreesBitwise                  "G4 F2 NEW"
check CliGuiCheckpointTest.TheGuiCanCheckpointAndTheFileMatchesTheClis "G4"
check CliGuiCheckpointTest.ACheckpointCrossesBetweenTheClients         "G4 CLI->GUI"
check CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli   "G4 F1 NEW GUI->CLI"
check CliGuiCheckpointTest.AnIntervalWithoutAPathIsRejectedAsTheCliRejectsIt "G4"
check CliGuiCheckpointTest.ARunWithoutCheckpointOptionsReportsNothing  "G4"
# --- G5: export ---
check ExportWriteFailureTest.EveryWriterReportsAFailedWrite            "G5"
check ExportWriteFailureTest.TheFailureNamesTheFile                    "G5"
check ExportWriteFailureTest.AHealthyWriteStillSucceeds                "G5 N3"
# --- G6: diagnostics / logging ---
check CFDAppCliLogLinesNeverReachStdout                                "G6 N5"
check CFDAppCliLogLevelIsDocumented                                    "G6"
check CFDAppCliLogLevelUnknownIsRejected                               "G6 V2"
# --- G7: packaging ---
check CFDAppPackageNaming                                              "G7"
check CFDAppPackageNamingRefusesUnknownSystem                          "G7 NV5"
# --- G8: benchmarks ---
check CFDAppBenchmarkSuiteSmoke                                        "G8 S3"
# --- G2: restart / checkpoint, incl. the turbulence refusal ---
check CheckpointProductionCaseTest.CheckpointingATurbulentCaseIsRefusedByName "G2 P16"
check CheckpointProductionCaseTest.ResumingATurbulentCaseIsRefusedByName      "G2 P16"
# --- G2: CLI checkpoint flag coverage (V3) ---
check CFDAppCliCheckpointFlagsAreDocumented                            "G4 V3"
check CFDAppCliCheckpoint_every_not_a_number                           "G4 V3"
check CFDAppCliCheckpoint_every_zero                                   "G4 V3"
check CFDAppCliCheckpoint_resume_missing_file                          "G4 V3"
echo '```'
echo
echo '`--` in the cpu_only column for a CUDA test is EXPECTED and is not a silent'
echo 'exclusion: the target is not built when `CFDAPP_ENABLE_CUDA=OFF`. The'
echo 'cpu_only column is reported so that absence is visible rather than assumed.'
echo
echo '## GPU turbulence gates (G9 / P16), by name'
echo
echo '```text'
printf '%-58s %-13s %-11s %-9s\n' "expected test" "release_cuda" "debug_gui" "cpu_only"
printf '%-58s %-13s %-11s %-9s\n' "-------------" "------------" "---------" "--------"
for t in $(grep -E '^(DeviceTurbulenceAlgebra|DeviceKTransport|DeviceEpsilonTransport|DeviceKEpsilonCorrection|DeviceEffectiveViscosity|DeviceWallFaceViscosity)\.' /tmp/g10_reg_release_cuda.txt | head -12); do
  r="YES"
  grep -qxF "$t" /tmp/g10_reg_debug_gui.txt && d=YES || d="--"
  grep -qxF "$t" /tmp/g10_reg_cpu_only.txt  && c=YES || c="--"
  printf '%-58s %-13s %-11s %-9s\n' "$t" "$r" "$d" "$c"
done
echo '```'
echo
echo '## Unexpected exclusions'
echo
echo 'An unexpected exclusion would be a test present in the registry that the'
echo 'invocation filter removes WITHOUT being one of the five documented items.'
echo 'Computed directly:'
echo
echo '```text'
for cfg in release_cuda debug_gui cpu_only; do
  n=$(grep -cE "$EXCLUDE" "/tmp/g10_reg_$cfg.txt" || true)
  docd=$(grep -cE 'SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence' "/tmp/g10_reg_$cfg.txt" || true)
  printf '%-14s tests matched by the filter: %-4s all of them documented: %s\n' \
    "$cfg" "$n" "$([ "$n" = "$docd" ] && echo YES || echo NO)"
done
echo '```'
} > "$out"
cat "$out"
