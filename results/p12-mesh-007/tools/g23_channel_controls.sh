#!/usr/bin/env bash
# P12-MESH-007 G2.3 supplementary channel controls (NOT a gate criterion); see g23_channel_controls.py.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-007
LOG=$P/logs/27_g23_channel_controls_NOT_gate.log
[ -e "$LOG" ] && { echo "REFUSED: $LOG exists"; exit 1; }
rm -rf $HOME/m7g23/controls
{
  echo "# G2.3 supplementary channel controls (NOT a gate criterion); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# instrument $(sha256sum $P/tools/independent_geometry.py | cut -d' ' -f1); control $(sha256sum $P/tools/g23_channel_controls.py | cut -d' ' -f1)"
  echo "# gate dumps /root/m7g23/gate:"; (cd $HOME/m7g23/gate && sha256sum geometry_*.json | sed 's/^/#   /')
  python3 $P/tools/g23_channel_controls.py $HOME/m7g23/gate $HOME/m7g23/controls
  echo "exit $?"
  echo "## identity control vs gate log 26 (the G2.3 verdict lines must be identical)"
  diff <(grep '^G2.3 [A-Z]' $P/logs/26_g23_gate.log) <(python3 $P/tools/independent_geometry.py $HOME/m7g23/controls/identity | grep '^G2.3 [A-Z]') && echo "identical"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG
