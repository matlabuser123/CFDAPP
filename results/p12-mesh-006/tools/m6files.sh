#!/usr/bin/env bash
# P12-MESH-006: every file changed or added relative to the pre-MESH-006 snapshot ($HOME/m6ref/base),
# excluding build trees, generated case outputs and this phase's own evidence directory.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$HOME/m6ref/base
cd $R
diff -rq -x build -x .git -x results -x __pycache__ -x '*.pyc' $B . 2>/dev/null \
  | sed -e "s|^Files $B/\(.*\) and ./.* differ$|M \1|" -e "s|^Only in \./*\(.*\): \(.*\)$|A \1/\2|" -e "s|^Only in $B/*\(.*\): \(.*\)$|D \1/\2|" \
  | sed -e 's|A /|A |' -e 's|A \./|A |' | sort
echo "---- source/test line counts (diff -u, + / -)"
for f in $(diff -rq -x build -x .git -x results $B . 2>/dev/null | grep '^Files' | sed "s|^Files $B/\(.*\) and .*|\1|"); do
  diff -u $B/$f $f | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | awk -v f=$f '/^\+/{a++} /^-/{d++} END{printf "%-70s +%d -%d\n", f, a, d}'
done
echo "---- base snapshot integrity"
(cd $B && sha256sum -c --quiet $HOME/m6ref/base.src.sha256 2>&1 | tail -3; echo "base hash check rc=$?")
