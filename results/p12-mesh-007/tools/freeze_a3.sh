#!/usr/bin/env bash
# P12-MESH-007 Amendment A3 freeze: hashes every artifact the fresh G9 execution uses or relies on,
# and the two libraries, into logs/39_a3_freeze.log (written once). g9_a3.sh fresh re-verifies
# every FROZEN and FROZEN-LIB line.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $R || exit 1
M=results/p12-mesh-007
LOG=$M/logs/39_a3_freeze.log
[ -e $LOG ] && { echo "REFUSED: $LOG exists (the freeze is written once)"; exit 1; }
NOM7=$HOME/m7g9/nom7/build/src/libcfdcore.a
nom7_dry=$(sed -n 's/^nom7 libcfdcore.a \([0-9a-f]\{64\}\);.*/\1/p' $M/logs/30_a3_dry_build_nom7.log)
nom7_now=$(sha256sum $NOM7 | cut -d' ' -f1)
[ -n "$nom7_dry" ] && [ "$nom7_dry" = "$nom7_now" ] || { echo "REFUSED: nom7 library $nom7_now vs dry log '$nom7_dry'"; exit 1; }
{
  echo "# P12-MESH-007 AMENDMENT A3 FREEZE $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  echo "# gate documents and the dry-run record"
  for f in $M/acceptance_gate.md $M/acceptance_gate_A3.md $M/a3/dryrun.md; do echo "FROZEN $(sha256sum $f | cut -d' ' -f1)  $f"; done
  echo "# instruments"
  for f in $M/tools/g9_a3.sh $M/tools/g9_compat.sh $M/tools/build_nom7.sh $M/tools/nom7_fidelity.py \
           $M/tools/g93_inputs.py $M/tools/bitprobe7.cpp results/p12-mesh-005/tools/bitprobe.cpp \
           results/p12-mesh-006/tools/bitprobe6.cpp results/p12-grad-002/a2/tools/classify_scope.py \
           results/p12-mesh-006/a3/tools/classify_generated_outputs.py $M/tools/freeze_a3.sh; do
    echo "FROZEN $(sha256sum $f | cut -d' ' -f1)  $f"
  done
  echo "# dry-run logs (final sequence and every preserved attempt)"
  for f in $(ls $M/logs/3[0-4]_a3_dry_* $M/logs/3x_a3_dry_driver.log | sort); do echo "FROZEN $(sha256sum $f | cut -d' ' -f1)  $f"; done
  echo "# earlier MESH-007 evidence this amendment relies on (the original failure and the unchanged rerun)"
  for f in $M/logs/12_gate_stage3_G1.2_G5_G6_G7_G8.log $M/logs/20_rerun_stage1_G1.1_G1.3_G2_G3_G4.log \
           $M/logs/21_rerun_stage2_G1.4_G6.1_G6.5.log $M/logs/22_rerun_stage3_G1.2_G5_G6_G7_G8.log \
           $M/logs/26_g23_gate.log; do
    echo "FROZEN $(sha256sum $f | cut -d' ' -f1)  $f"
  done
  echo "# libraries"
  echo "FROZEN-LIB new $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "FROZEN-LIB nom7 $nom7_now"
  echo "# BASE (pre-MESH-007 reference used by F2/F3/G9.3): $(sha256sum /root/m7ref/base/build/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# control libraries (N1, N3): cur $(sha256sum $HOME/g2/cur/build/src/libcfdcore.a | cut -d' ' -f1), nograd $(sha256sum $HOME/g2/nograd/build/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG
cat $LOG
sha256sum $LOG
