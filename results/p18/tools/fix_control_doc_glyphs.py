#!/usr/bin/env python3
"""Repair two glyph substitutions that update_control_docs.py made too eagerly.

FINDING (recorded, not hidden): that script translated the ASCII anchor " - " into an em dash so
the replacement blocks would match the documents' existing punctuation. The rule is too broad --
it also rewrote the MINUS SIGN inside the discretized equation
    div(rho U Y) - div(rho D grad Y) = S
into an em dash, in 3 places. An em dash there is not a typographic nicety; the equation is the
central factual claim of the P18.0 audit, and a dash that is not a minus misstates it.

Also makes the first branch of a two-branch tree a tee rather than a second elbow.

Binary mode again: both files are CRLF and Git Bash would otherwise rewrite them as LF.
"""
import io

EM = "—"
ELBOW = "└─"
TEE = "├─"

FIXES = {
    "TODO.md": [
        ("div(rho U Y) %s div(rho D grad Y) = S" % EM,
         "div(rho U Y) - div(rho D grad Y) = S", 1),
        (" %s P18.0   COMPLETE" % ELBOW, " %s P18.0   COMPLETE" % TEE, 1),
    ],
    "ROADMAP.md": [
        ("div(rho U Y) %s div(rho D grad Y) = S" % EM,
         "div(rho U Y) - div(rho D grad Y) = S", 2),
        ("%s P18.0 Baseline & Audit" % ELBOW, "%s P18.0 Baseline & Audit" % TEE, 1),
    ],
}


def main():
    for path, fixes in FIXES.items():
        with io.open(path, "rb") as fh:
            raw = fh.read()
        crlf = b"\r\n" in raw
        text = raw.decode("utf-8")
        for old, new, expect in fixes:
            count = text.count(old)
            if count != expect:
                raise SystemExit("%s: %r matched %d times, expected %d"
                                 % (path, old[:60], count, expect))
            text = text.replace(old, new)
        with io.open(path, "wb") as fh:
            fh.write(text.encode("utf-8"))
        print("%s  repaired  (crlf=%s)" % (path, crlf))


if __name__ == "__main__":
    main()
