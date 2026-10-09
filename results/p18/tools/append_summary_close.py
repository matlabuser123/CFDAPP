#!/usr/bin/env python3
"""Append the closing sections (control-doc update + one self-inflicted finding) to
results/p18/P18_0_SUMMARY.md, and bring its evidence index in line with the files that now exist.

A file rather than a heredoc: the text contains backticks and apostrophes, both of which a
`bash -c` heredoc mangles in this environment.
"""
import io

PATH = "results/p18/P18_0_SUMMARY.md"

OLD_TOOLS = ("├── tools/      boundary_lag_probe.cpp (instrumentation) "
             "· freeze_p18_start.sh ·\n")
NEW_TOOLS = ("├── tools/      boundary_lag_probe.cpp (instrumentation) "
             "· freeze_p18_start.sh ·\n")

TAIL = """
## 11. Control-document update (§37)

`TODO.md` and `ROADMAP.md` are the only two files outside `results/p18/` that P18.0 changed, and
both are excluded from the source fingerprint by construction, so the integrity proof in §9 still
holds after them. Applied by `results/p18/tools/update_control_docs.py`.

```text
TODO.md     P18.0 block   CURRENT -> COMPLETE, all 8 items [x], with the measured baseline counts,
                          the four audit findings and the frozen-gate dry-run recorded inline
            execution     P18 ACTIVE / P18.0 COMPLETE / P18.1 NOT AUTHORIZED YET; the stale
            order         "P18 ACTIVE + AUTHORIZED" line replaced
            NEXT          P18.1 - Multi-Species State, NOT AUTHORIZED YET

ROADMAP.md  §2           P18 becomes the ACTIVE phase; P17 moves to a new "Prior phase, closed as
                          PARTIAL" heading with its bullets unchanged and marked not reopened
            §2           P18 removed from "Future phases" (it is no longer future)
            §12          heading NOT AUTHORIZED -> ACTIVE (P18.0 COMPLETE); the closing
                          "P18 does not begin until explicitly authorized" replaced by what the
                          audit established, and by the note that each later step still needs its
                          own authorization
            §23          ladder: P17 corrected from "ACTIVE / P17.0 COMPLETE" (stale since
                          P17.11) to PARTIAL; P18 ACTIVE with its two sub-steps
```

Both files are CRLF on disk while Git Bash presents them as LF, so the patch is applied in binary
mode; `file` confirms CRLF is preserved on both, and the edits are localized rather than a
whole-file line-ending rewrite.

## 12. One self-inflicted finding, recorded rather than quietly fixed

```text
WHAT   update_control_docs.py writes its anchors in ASCII and translates " - " to the em dash the
       control documents use. That rule is too broad: it also rewrote the MINUS SIGN in

           div(rho U Y) - div(rho D grad Y) = S

       into an em dash, in 3 places across the two documents.

WHY IT MATTERS ENOUGH TO RECORD   that equation is the central factual claim of this audit -- the
       whole point of section 3 is that the shipped solver assembles THAT equation and not the
       conservative mass-fraction form. A dash that is not a minus misstates it, and it would have
       been read as typography rather than as an error.

CAUGHT BY   reading the applied diff, not by the script, which cannot tell a separator dash from
       an operator. FIXED BY results/p18/tools/fix_control_doc_glyphs.py, which asserts the exact
       occurrence count (1 in TODO.md, 2 in ROADMAP.md) before touching anything.

NO PRODUCTION IMPACT: both files are documentation and both are outside the fingerprint. Recorded
       because the phase rule is to preserve findings, including the ones I caused.
```

## 13. P18.0 status

```text
P18.0                      COMPLETE
production code changed    NONE (fingerprint 60cf951b... identical at start and end)
gates                      FROZEN, sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
                           no amendments, no threshold weakened
baseline                   reproduces P17.11 exactly in all three configurations
open defect found          species boundary lag (G4(c)) -- NOT fixed, fixing it is not P18.0 scope
P17 open items             G6 and G10 untouched, as instructed
next                       P18.1 - Multi-Species State, NOT AUTHORIZED
```
"""


def main():
    with io.open(PATH, "rb") as fh:
        raw = fh.read()
    if b"\r\n" in raw:
        raise SystemExit("unexpected CRLF in %s" % PATH)
    text = raw.decode("utf-8")
    if "## 11. Control-document update" in text:
        raise SystemExit("already appended -- refusing to duplicate")
    # keep the index honest about the two scripts this phase added last
    old = "verify_no_source_change.sh · restore_validation_noise.sh · run_lag_probe.sh"
    new = ("verify_no_source_change.sh · restore_validation_noise.sh · run_lag_probe.sh ·\n"
           "│               update_control_docs.py · fix_control_doc_glyphs.py · "
           "append_summary_close.py")
    if text.count(old) != 1:
        raise SystemExit("tools index anchor matched %d times" % text.count(old))
    text = text.replace(old, new)
    if not text.endswith("\n"):
        text += "\n"
    text += TAIL
    with io.open(PATH, "wb") as fh:
        fh.write(text.encode("utf-8"))
    print("%s  appended (sections 11-13) and index updated" % PATH)


if __name__ == "__main__":
    main()
