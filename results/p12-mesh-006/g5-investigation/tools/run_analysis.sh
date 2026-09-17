#!/usr/bin/env bash
# G5 investigation: regenerate every analysis result from the CLI outputs under $HOME/m6g5.
# Each step is logged to g5-investigation/logs with its command and date.
I=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-mesh-006/g5-investigation
cd $I
step() {  # step <log> <command...>
  local log=$1; shift
  {
    echo "# G5 investigation: $*"
    echo "# date $(date -u +%Y-%m-%dT%H:%M:%SZ); python $(python3 -c 'import sys, numpy, scipy, mpmath; print(sys.version.split()[0], "numpy", numpy.__version__, "scipy", scipy.__version__, "mpmath", mpmath.__version__)')"
    echo
    "$@"
    echo "# exit: $?"
  } > logs/$log 2>&1
  tail -n 3 logs/$log
}
step reference.log python3 tools/duct_reference.py data/reference.json
step discrete.log python3 tools/duct_discrete.py data/discrete.json 8 12 16 24 32 48 64 96 128 192 256
step compare.log python3 tools/compare_cfdapp.py data/cfdapp_comparison.json "$@"
step analysis.log python3 tools/analysis.py data/cfdapp_comparison.json data/analysis.json data/analysis.md
