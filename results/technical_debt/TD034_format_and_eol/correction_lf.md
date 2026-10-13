> **INVALID AS AUTHORITATIVE (2026-10-11).** This file's central claim -- that there is no CRLF
> anywhere in the repository -- is FALSE. Measured with a binary byte count: **102 tracked text
> files contain CRLF, 21 621 lines.** The probe behind this correction failed the same way the
> probe behind TD-041 did, in the opposite direction. Superseded by
> [correction_2_crlf_in_head.md](correction_2_crlf_in_head.md). Retained unaltered below per
> CLAUDE.md §4; nothing after this banner has been edited.

# CORRECTION — the repository is PURE LF. The probe behind TD-041 was broken, and so was its refutation.

```text
measured    2026-10-10, definitively, by counting bytes rather than grepping
corrects    verification.md §2, §3.2 and §4 · TD-041's register row · and the claim
            "CRLF is this repository's committed convention" which reached THREE commit messages
            (5075f0d, 1979332, and by reference 0b71669)
status      the CONCLUSION of TD-041 stands -- there is no CRLF defect. Its REASONING was wrong in
            both directions, and the true state was never measured until now.
```

---

## 1. The measurement, by bytes

```text
file                                  CRLF bytes   LF-only   `grep -c $'\r'` reported
CMakeLists.txt                                 0       138                        138
src/CMakeLists.txt                             0       508                        508
tests/unit/species/CMakeLists.txt              0        55                         55
CMakePresets.json                              0        81                         81
TODO.md                                        0       320                        320
CLAUDE.md                                      0       358                        358
```

And against `HEAD`, for the four files TD-041 named:

```text
file                    HEAD CRLF   TREE CRLF
CMakeLists.txt                  0           0
src/CMakeLists.txt              0           0
cuda/CMakeLists.txt             0           0
tests/CMakeLists.txt            0           0
```

**There is no CRLF anywhere — not in `HEAD`, not in the working tree, not in one file.** The
repository is pure LF, which is what `.gitattributes`'s `* -text` plus `core.autocrlf = input`
produces: bytes stored verbatim, and the bytes are LF.

## 2. THE BROKEN PROBE

Every CR figure in this campaign came from

```bash
grep -c $'\r' "$f"
```

**`$'\r'` was mangled to an empty pattern**, and `grep -c ''` matches every line. So each reported
"CR count" was the file's LINE COUNT. The same command with a literal `\r` argument returns `0`,
which is the true answer.

## 3. What that did to TD-041, in both directions

```text
THE CLAIM       "CRLF has GROWN in 21 CMakeLists.txt: 100 -> 138, 411 -> 508, every file up."
                Those were LINE counts. The files grew by exactly those amounts because lines were
                ADDED for the P18 species wiring. Nothing about line endings was measured.

THE REFUTATION  "the CR/lines RATIO is 1.000 on both sides for all 21 files."
                TAUTOLOGICAL. The numerator and denominator were the same quantity. A ratio of
                1.000 was guaranteed by construction and could not have come out otherwise.
```

**So the refutation was not a measurement either.** I recorded it as the decisive check that closed
TD-041, and it was arithmetic on a number divided by itself. The right answer — no CRLF at all —
was reachable the whole time with one byte count.

## 4. The claim that must be withdrawn

`verification.md` §3.2 states:

> *"`TODO.md` and `ROADMAP.md` are **fully CRLF in HEAD** — 416 of 416 lines and 366 of 366. So is
> every one of the 63 staged evidence `.md` files. **CRLF is this repository's committed
> convention**…"*

**False.** `TODO.md` in `HEAD` has **zero** CRLF bytes. Those "416 of 416" and "366 of 366" figures
are line counts matched against themselves.

The same claim was carried into commit `5075f0d`'s message ("verbatim capture is this repo's
practice… 73 of 400 logs already in HEAD carry 98 106 such lines") and into `1979332`'s reasoning.
**The trailing-whitespace part of that argument is unaffected** — those 98 106 lines were counted
with `grep -cE ' +$'`, a pattern that works, and the raw logs genuinely do carry trailing spaces.
It is only the **line-ending** half that is wrong.

## 5. What is NOT affected

```text
UNAFFECTED   TD-034's own resolution. clang-format at CI settings is clean: 741 files, exit 0,
             0 warnings, measured by running clang-format and reading its exit code.
UNAFFECTED   every `git diff --check` figure. That is git's own instrument, not mine.
UNAFFECTED   the decision to commit raw logs verbatim. It rests on trailing whitespace inside
             captured stdout and on §4's prohibition on rewriting results, not on line endings.
UNAFFECTED   the five files held back from 1979332 (the .so, two .pyc, the PDF and its extraction).
             Those were identified by `file` and by extension, not by this probe.
AFFECTED     nothing in the production code. No source file was ever edited on the strength of a
             CR count.
```

## 6. The lesson, which is not the one I drew the first time

TD-034's `verification.md` §3.1 already called this a fail-open probe and said *"an absolute CR
count cannot distinguish 'the endings flipped' from 'the file got longer'."* That diagnosis was
**too generous to the instrument**: the probe was not measuring CRs ambiguously, it was not
measuring CRs at all.

```text
The correct lesson: VERIFY THAT A PROBE RESPONDS TO ITS OWN INPUT.
One run of `grep -c $'\r'` against a file known to contain CRLF, or against one known not to,
would have shown it returning the line count either way. The campaign's own rule -- give a
sanitizer a live control kernel -- applies to a one-line shell probe exactly as it does to a
compute-sanitizer run.
```

That makes nine instrument defects in this campaign, and this one is the most productive of false
conclusions: it manufactured a debt item, a refutation of that item, and a "committed convention"
that does not exist.
