#!/usr/bin/env python3
"""G8-F2: split NV5 so it exercises the criterion its title names.

The frozen NV5 is titled "Compare a CUDA build's run against a serial baseline ->
E2 must refuse". Its implementation mutated `cpu` in suite_environment.txt -- a
MACHINE key. It therefore proved the guard catches a different CPU, which it does,
and never once exercised the build-configuration half it claims. Third instance of
this pattern in this gate: NV2/NV4 previously reported DETECTED without the
comparator reaching the regression logic at all.

After this change:
  NV5   mutates openmp_enabled in environment.json  -> E2 must refuse NAMING it
  NV5b  mutates cpu in suite_environment.txt        -> E2 must refuse NAMING it
                                                       (the original case, kept:
                                                        it is a real check, just
                                                        not the one NV5 claimed)

Each must refuse FOR ITS NAMED REASON, not merely exit non-zero. That discipline is
what exposed G8-F2, and a control that only checks the exit code would not notice
the difference.

Edits results/p13/benchmarks/tools/controls.sh, which is under results/** and so is
outside SOURCE_FINGERPRINT_V2.
"""
import io
import pathlib
import sys

PATH = pathlib.Path("results/p13/benchmarks/tools/controls.sh")

OLD = '''echo "########## NV5 -- mismatched environment; E2 must refuse"
X=/tmp/p13_bm_env; rm -rf $X; cp -r $RUN $X
sed -i 's/^cpu     :.*/cpu     : Some Other CPU/' $X/suite_environment.txt
$CMP --baseline $BASE --current $X > /tmp/nv5.log 2>&1; rc=$?
tail -3 /tmp/nv5.log | sed 's/^/    /'
[ $rc -ne 0 ] && verdict NV5 yes "refused to compare" || verdict NV5 no "compared anyway"
'''

NEW = '''echo "########## NV5 -- mismatched BUILD CONFIGURATION; E2 must refuse"
# G8-F2: this control used to mutate `cpu`, a MACHINE key, while its title claimed
# to test a build-configuration mismatch. It therefore never exercised the half of
# E2 it named. It now mutates openmp_enabled in environment.json -- the file the
# comparator had never opened (G8-F1) -- and requires the refusal to NAME the key,
# not merely to exit non-zero.
X=/tmp/p13_bm_env; rm -rf $X; cp -r $RUN $X
python3 - "$X/environment.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
before = d.get("openmp_enabled")
d["openmp_enabled"] = not bool(before)
json.dump(d, open(p, "w"), indent=2)
print("  planted: openmp_enabled %r -> %r" % (before, d["openmp_enabled"]))
PY
$CMP --baseline $BASE --current $X > /tmp/nv5.log 2>&1; rc=$?
tail -4 /tmp/nv5.log | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q 'openmp_enabled' /tmp/nv5.log; then
  verdict NV5 yes "refused, NAMING the openmp_enabled mismatch"
elif [ $rc -ne 0 ]; then
  verdict NV5 no "refused, but did NOT name openmp_enabled -- wrong reason"
else
  verdict NV5 no "compared anyway -- E2 does not see build configuration"
fi

echo
echo "########## NV5b -- mismatched MACHINE; E2 must refuse"
# The original NV5 case, kept as its own control. It is a real check of the guard;
# it simply is not the build-configuration check NV5's title promised.
Y=/tmp/p13_bm_env_cpu; rm -rf $Y; cp -r $RUN $Y
sed -i 's/^cpu     :.*/cpu     : Some Other CPU/' $Y/suite_environment.txt
$CMP --baseline $BASE --current $Y > /tmp/nv5b.log 2>&1; rc=$?
tail -3 /tmp/nv5b.log | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q 'cpu:' /tmp/nv5b.log; then
  verdict NV5b yes "refused, NAMING the cpu mismatch"
elif [ $rc -ne 0 ]; then
  verdict NV5b no "refused, but did NOT name cpu -- wrong reason"
else
  verdict NV5b no "compared anyway"
fi
'''


def main() -> int:
    text = io.open(PATH, encoding="utf-8", newline="").read()
    if NEW.splitlines()[0] in text:
        print("already applied")
        return 0
    if text.count(OLD) != 1:
        sys.stderr.write("ANCHOR NOT UNIQUE (%d occurrences) in %s\n" % (text.count(OLD), PATH))
        return 1
    io.open(PATH, "w", encoding="utf-8", newline="").write(text.replace(OLD, NEW, 1))
    print("NV5 split into NV5 (build config) + NV5b (machine)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
