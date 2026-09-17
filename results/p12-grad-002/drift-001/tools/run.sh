#!/usr/bin/env bash
# P12-GRAD-002-DRIFT-001: build drift_probe against one library tree and run it.
#   TREE=repo     the authoritative working tree (build/release)
#   TREE=nodiff   /root/uf001_baseline: the working tree with ONLY the DIFF-002 reconstruction
#                 block removed (UF-001 / W8-INV-001's isolated baseline, libcfdcore 719d0fc7...)
#   TREE=pregrad  /root/m7ref/base: the pre-MESH-007 / pre-GRAD-002 tree (libcfdcore eaadaa63...)
# Case inputs are always the authoritative repo's cases/ (cwd = repo root), so every library
# solves byte-identical case files.
# usage: TREE=... run.sh <log-name> <drift_probe args...>
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/drift-001
TREE=${TREE:-repo}
case "$TREE" in
  repo)    T=$R;                    B=$R/build/release ;;
  nodiff)  T=/root/uf001_baseline;  B=$T/build/release ;;
  pregrad) T=/root/m7ref/base;      B=$T/build ;;
  # UF-001's corrupted-operator controls (working tree with the DIFF-002 far-cell coefficient
  # doubled, libcfdcore 078668ba..., or its sign flipped, 1e5fbb7b...)
  farx2)    T=/root/uf001_ctrl_farx2;    B=$T/build/release ;;
  signflip) T=/root/uf001_ctrl_signflip; B=$T/build/release ;;
  *) echo "unknown TREE $TREE"; exit 2 ;;
esac
LIB=$B/src/libcfdcore.a
INC="-I$T/include -I$B/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
W=$HOME/drift001; mkdir -p "$W" "$P/logs"
BIN=$W/drift_probe.$TREE
LOCK=$W/.build.$TREE.lock
# FAIL CLOSED (round-5 lesson): the binary is keyed by the SOURCE HASH, so a failed compile can
# never silently leave an older binary in use.
SRCHASH=$(sha256sum "$P/tools/drift_probe.cpp" | cut -c1-16)
BIN=$W/drift_probe.$TREE.$SRCHASH
(
  flock 9
  if [ ! -x "$BIN" ] || [ "$LIB" -nt "$BIN" ]; then
    c++ -std=c++20 -O3 -DNDEBUG $INC "$P/tools/drift_probe.cpp" "$LIB" -o "$BIN.tmp" && mv "$BIN.tmp" "$BIN"
  fi
) 9>"$LOCK"
[ -x "$BIN" ] || { echo "BUILD FAILED for source $SRCHASH -- fail closed" >&2; exit 1; }
cd "$R" || exit 1
LOG=$P/logs/$1
shift
{
  echo "# P12-GRAD-002-DRIFT-001 drift_probe  tree=$TREE"
  echo "# libcfdcore.a $(sha256sum "$LIB" | cut -d' ' -f1)"
  echo "# probe source $SRCHASH  binary $(sha256sum "$BIN" | cut -d' ' -f1)"
  echo "# case inputs  $(cat cases/poiseuille_distorted/*.json | sha256sum | cut -c1-16) (poiseuille_distorted)" \
       "$(cat cases/curved_channel_multiblock/*.json | sha256sum | cut -c1-16) (curved_channel_multiblock)"
  echo "# args: $*"
  echo "# start $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  "$BIN" "$@"
  echo "exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
