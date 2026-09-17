#!/usr/bin/env bash
# P12-MESH-007: record an amendment freeze -- the new sha256 of the gate, and proof that the text frozen
# in logs/05 (sha256 275eb19a...) is an unchanged prefix of the amended file.
# Usage: amendment_check.sh <amendment-heading-line> <log>
P=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-mesh-007
cd $P
heading="$1"
log="$2"
{
  echo "# P12-MESH-007 gate amendment freeze: '$heading', $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# test sources in the tree mentioning MeshMotion/AlePISO at this point: $(grep -rlE 'MeshMotion|AlePISO' ../../tests 2>/dev/null | wc -l)"
  sha256sum acceptance_gate.md architecture.md
  line=$(grep -n -F "$heading" acceptance_gate.md | head -1 | cut -d: -f1)
  echo "# amendment heading at line $line"
  python3 - "$line" <<'PY'
import hashlib, sys
line = int(sys.argv[1])
text = open("acceptance_gate.md", "rb").read().split(b"\n")
prefix = b"\n".join(text[: line - 2]) + b"\n"  # the frozen text: everything before the blank line + heading
h = hashlib.sha256(prefix).hexdigest()
print("frozen prefix sha256", h, "(logs/05: 275eb19a8f393f8a1f58ebcfe1867e63150d4e8f64b19b7d80dd831fca80f533)",
      "UNCHANGED" if h == "275eb19a8f393f8a1f58ebcfe1867e63150d4e8f64b19b7d80dd831fca80f533" else "CHANGED")
PY
} > $P/logs/$log 2>&1
cat $P/logs/$log
