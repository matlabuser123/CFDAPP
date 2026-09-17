#!/usr/bin/env bash
# P12-GRAD-002 A2: build one A2 probe against one library and run it, logging under a2/logs/.
# usage: run_a2.sh <probe-stem> <log-name> <tree> [probe args...]
#   repo     build/release (the authoritative library)
#   cur      $HOME/g2/cur      (isolated copy, current sources)
#   nograd   $HOME/g2/nograd   (isolated copy, pre-GRAD-002 Gradient.cpp only)
#   grad001  /root/m7ref/grad001 (pre-GRAD-002 with MESH-007's motion API; library 4fa871b8...)
#   base     /root/m7ref/base    (pre-MESH-007 / pre-GRAD-002; library eaadaa63...; no motion API)
# The binary is keyed by (probe source hash, library hash): a failed compile can never leave an
# older binary in use (DRIFT-001 round-5 lesson).
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
STEM=$1; LOGNAME=$2; TREE=$3; shift 3
GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
case $TREE in
  repo)    INC="-I$R/include $GEN"; LIB=$R/build/release/src/libcfdcore.a ;;
  cur)     INC="-I$HOME/g2/cur/src_tree/include -I$HOME/g2/cur/build/generated/include $GEN"; LIB=$HOME/g2/cur/build/src/libcfdcore.a ;;
  nograd)  INC="-I$HOME/g2/nograd/src_tree/include -I$HOME/g2/nograd/build/generated/include $GEN"; LIB=$HOME/g2/nograd/build/src/libcfdcore.a ;;
  grad001) INC="-I/root/m7ref/grad001/include $GEN"; LIB=/root/m7ref/grad001/libcfdcore.a ;;
  base)    INC="-I/root/m7ref/base/include -I/root/m7ref/base/build/generated/include $GEN"; LIB=/root/m7ref/base/build/src/libcfdcore.a ;;
  *) echo "unknown tree $TREE"; exit 2 ;;
esac
W=$HOME/g2/probes; mkdir -p "$W" "$P/logs"
SH=$(sha256sum "$P/tools/$STEM.cpp" | cut -c1-12)
LH=$(sha256sum "$LIB" | cut -c1-12)
BIN=$W/$STEM.$TREE.$SH.$LH
if [ ! -x "$BIN" ]; then
  c++ -std=c++20 -O2 -DNDEBUG $INC -I$R/tests/unit/discretization "$P/tools/$STEM.cpp" "$LIB" -o "$BIN.tmp" \
    > "$W/$STEM.$TREE.compile.txt" 2>&1 && mv "$BIN.tmp" "$BIN"
fi
[ -x "$BIN" ] || { echo "BUILD FAILED ($STEM, $TREE) -- fail closed"; cat "$W/$STEM.$TREE.compile.txt" | head -30; exit 1; }
{
  echo "# P12-GRAD-002 A2 $STEM  tree=$TREE  libcfdcore.a $(sha256sum "$LIB" | cut -d' ' -f1)"
  echo "# probe source $SH  args: $*  start $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  "$BIN" "$@"
  echo "exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$P/logs/$LOGNAME" 2>&1
cat "$P/logs/$LOGNAME"
