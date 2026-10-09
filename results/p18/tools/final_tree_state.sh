#!/usr/bin/env bash
# P18.0 close-out -- the working-tree state, stated explicitly as the authorization requires.
# A SCRIPT FILE, not an inline wsl.exe command: the nested $( ... grep -c "^ M" ... ) quoting gets
# mangled by `wsl.exe -- bash -lc "..."` and silently produces EMPTY counts, which would read as
# "clean tree" -- exactly the fail-open shape this phase is supposed to avoid.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

RAW=$(git status --porcelain)

MOD=$(printf '%s\n' "$RAW" | grep -c '^ M')
UNT=$(printf '%s\n' "$RAW" | grep -c '^??')
STG=$(printf '%s\n' "$RAW" | grep -cE '^[MADRC] ')
DEL=$(printf '%s\n' "$RAW" | grep -c '^ D')

echo "branch            : $(git rev-parse --abbrev-ref HEAD)"
echo "HEAD              : $(git rev-parse HEAD)"
echo "tracked modified  : $MOD"
echo "tracked deleted   : $DEL"
echo "untracked entries : $UNT   (git collapses directories, so this is NOT a file count)"
echo "staged            : $STG"
echo ""
echo "DIRTY: the working tree was already dirty at P18.0 start and still is. That is preserved"
echo "deliberately -- nothing was committed, pushed, reset, cleaned or stashed."
echo ""
echo "--- changed TRACKED files, listed in full ---"
printf '%s\n' "$RAW" | grep -E '^( M| D|[MADRC] )' | sed 's/^/  /'
echo ""
echo "--- untracked ENTRIES, listed in full ---"
printf '%s\n' "$RAW" | grep '^??' | sed 's/^/  /'
