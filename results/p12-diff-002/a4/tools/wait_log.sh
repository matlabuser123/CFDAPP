#!/usr/bin/env bash
# Wait until $1 contains the marker $2 (or the attempt budget runs out), then print its tail.
set -u
L=$1
MARK=$2
N=${3:-100}
for i in $(seq 1 "$N"); do
  if [ -f "$L" ] && grep -q "$MARK" "$L"; then echo "DONE after $((i * 6))s"; break; fi
  sleep 6
done
tail -${4:-40} "$L" 2>/dev/null || echo "(no log yet)"
