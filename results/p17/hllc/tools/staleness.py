#!/usr/bin/env python3
"""P17.6 -- CLAUDE.md section 8 step 3, made precise.

Every BUILT test binary must postdate
  (a) the newest PRODUCTION source in include/ src/ cuda/  (those libraries link into all of them),
  (b) every source THAT BINARY ACTUALLY COMPILES, read from CMake's own object list.

(b) replaces "the newest test source in the binary's directory", which was wrong both ways: it
falsely accused CFDLowMachRegressionTests of a sibling target's new source, and it never checked a
source compiled in from another directory. See logs/PRESERVED_invalid_perdir_staleness/WHY_INVALID.md.

Fails closed on its own blind spots: a missing object dir, no objects, or zero resolvable sources for
a binary are all reported as "cannot verify" and exit nonzero.

usage: staleness.py <build dir> <repo root> <newest production source epoch>   [binaries on stdin]
"""
import os
import sys
import time


def gmt(t):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t))


def main():
    build, root, newest_src_t = sys.argv[1], sys.argv[2], int(sys.argv[3])
    absbuild = os.path.realpath(build)
    bins = [l.strip() for l in sys.stdin if l.strip()]

    nbin = external = stale_prod = stale_own = unverifiable = 0
    objs_total = objs_resolved = 0
    oldest_t, oldest_f = None, None

    for b in bins:
        rb = os.path.realpath(b)
        if not (rb == absbuild or rb.startswith(absbuild + os.sep)):
            external += 1
            print("    external tool, not staleness-checked: %s  (%s)"
                  % (b, time.strftime("%Y-%m-%d", time.gmtime(os.path.getmtime(b)))))
            continue

        nbin += 1
        rel_bin = os.path.relpath(rb, absbuild)
        bt = os.path.getmtime(rb)
        if oldest_t is None or bt < oldest_t:
            oldest_t, oldest_f = bt, rel_bin

        if bt < newest_src_t:
            stale_prod += 1
            print("    STALE vs PRODUCTION: %s" % rel_bin)

        odir = os.path.join(os.path.dirname(rb), "CMakeFiles", os.path.basename(rb) + ".dir")
        if not os.path.isdir(odir):
            unverifiable += 1
            print("    CANNOT VERIFY (no CMakeFiles/%s.dir): %s -- FAIL CLOSED"
                  % (os.path.basename(rb), rel_bin))
            continue

        objects = []
        for dirpath, _dirnames, filenames in os.walk(odir):
            for f in filenames:
                if f.endswith(".o") or f.endswith(".obj"):
                    objects.append(os.path.join(dirpath, f))
        if not objects:
            unverifiable += 1
            print("    CANNOT VERIFY (no objects under CMakeFiles/%s.dir): %s -- FAIL CLOSED"
                  % (os.path.basename(rb), rel_bin))
            continue

        srcdir_rel = os.path.relpath(os.path.dirname(rb), absbuild)
        worst_t, worst_f, nres = 0, None, 0
        for o in objects:
            objs_total += 1
            orel = os.path.relpath(o, odir)
            srel = orel[:-2] if orel.endswith(".o") else orel[:-4]
            # CMake encodes a leading "../" in a source path as "__/".
            srel = srel.replace("__" + os.sep, ".." + os.sep).replace("__/", "../")
            cand = os.path.normpath(os.path.join(root, srcdir_rel, srel))
            if not os.path.isfile(cand):
                # A generated source (gtest_main, a configured file). Not in the repo, so there is
                # nothing to be stale against. Counted; the totals below make a bad mapping visible.
                continue
            nres += 1
            objs_resolved += 1
            st = os.path.getmtime(cand)
            if st > worst_t:
                worst_t, worst_f = st, os.path.relpath(cand, root)

        if nres == 0:
            unverifiable += 1
            print("    CANNOT VERIFY (0 of %d objects mapped to a repo source): %s -- FAIL CLOSED"
                  % (len(objects), rel_bin))
            continue

        if bt < worst_t:
            stale_own += 1
            print("    STALE vs ITS OWN SOURCE: %s" % rel_bin)
            print("        binary %s   newest source it compiles %s  %s"
                  % (gmt(bt), gmt(worst_t), worst_f))

    print("  built test binaries       : %d" % nbin)
    print("  external tools in registry: %d" % external)
    print("  objects inspected         : %d   mapped to a repo source: %d   generated: %d"
          % (objs_total, objs_resolved, objs_total - objs_resolved))
    if nbin:
        print("  oldest built binary       : %s  %s" % (gmt(oldest_t), oldest_f))
    print("  stale vs production       : %d" % stale_prod)
    print("  stale vs its own sources  : %d" % stale_own)
    print("  unverifiable (fail closed): %d" % unverifiable)

    if nbin == 0:
        print("  *** NO BUILT TEST BINARIES FOUND -- FAIL CLOSED ***")
        return 1
    if stale_prod or stale_own or unverifiable:
        return 1
    print("  VERDICT: all %d built test binaries postdate the newest production source AND every"
          % nbin)
    print("           source they actually compile, per CMake's own object lists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
