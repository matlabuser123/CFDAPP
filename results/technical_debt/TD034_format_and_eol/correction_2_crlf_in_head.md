# CORRECTION 2 — there IS CRLF in `HEAD`. My own correction of TD-041 was also wrong.

```text
STATUS:      **`correction_lf.md` IS FALSE AND IS MARKED INVALID AS AUTHORITATIVE.** It states
             "There is no CRLF anywhere -- not in HEAD, not in the working tree, not in one file."
             Measured: **102 tracked text files contain CRLF, 21 621 lines.**
             It is committed (0a9bc42), so per §10 it is corrected FORWARD here, never rewritten.
SCOPE:       the line-ending question only. No production change. Found while clearing §11.2 for
             step 9's commit, which is why it was found at all.
INSTRUMENT:  a byte count -- `read(); count(b"\r\n")` -- over `git ls-files -z`, in binary mode.
             No grep, no shell pattern, no text-mode read. Listed in §2 against the two that failed.
EVIDENCE:    this file. The measurement is reproduced inline and is cheap to re-run.
CONSEQUENCE: **TD-041 RETURNS TO OPEN.** Its N/A rested on this false measurement.
COMMIT:      step 9's, which carries this file. No separate authorization sought.
```

---

## 1. The measurement

```text
tracked files on disk                                     7080
flagged by a raw CRLF byte count                           137
  of which BINARY, so not line endings at all               35   <- my probe's own artifact
  genuine TEXT files containing CRLF                       102
genuine CRLF text lines                                 21 621

by extension        .csv 50   .log 21   .json 10   .cpp 8   .md 8   .py 3   .txt 2
by directory        results/ 91        tests/ 11
```

### 1.1 Thirty-five of the 137 are my probe counting compressed bytes

34 `.png` and 1 `.pdf`. **A PNG signature is `89 50 4E 47 0D 0A 1A 0A` — it contains `0D 0A` by
specification**, which is why most of them scored exactly 1-2. Counting `\r\n` in a binary file is
meaningless, and I reported those 137 before separating them. The separation is by NUL byte in the
first 8 KiB plus a binary extension list.

```text
So the honest headline is 102 files / 21 621 lines, not 137 / 21 677.
```

## 2. BOTH probes in this item were broken, in OPPOSITE directions

```text
TD-041's original    grep -c $'\r'      the shell mangled $'\r' to an EMPTY pattern, so it
                     returned LINE COUNTS. Every file "had CRLF". -> "CRLF is growing"
my refutation        (the same family)  concluded ZERO CRLF everywhere, and I wrote that into
                     a committed evidence file and TWO commit messages.
this measurement     binary byte count  102 files, 21 621 lines.
```

**The second probe failed the same way the first did and I did not notice, because its answer was
the clean one.** A result that says "nothing is wrong" ends the investigation; that is exactly when
the instrument needs the same scrutiny as a result that says something is wrong. This is the twelfth
instrument defect this campaign and the first **repeat inside one item**.

## 3. Why nothing in this repository could see it

Three independent mechanisms, each individually reasonable:

```text
.gitattributes:9    `*  -text`           disables ALL normalization. It OVERRIDES core.autocrlf,
                                         which is `input` in WSL and would otherwise convert on
                                         commit. Bytes are stored verbatim -- so CRLF persists.
.clang-format       sets neither LineEnding nor DeriveLineEnding, so clang-format-18 DERIVES the
                    ending per file. All 3 CRLF .cpp files are in the §7 742-file set and PASS.
git diff --check    DOES flag CRLF -- but only for files in the diff. The 3 CRLF sources have not
                    been re-staged since they landed, so §11.2 has never seen them.
```

Verified: `clang-format-18 --dry-run --Werror` returns **PASS** on all three of
`tests/integration/io/test_result_export.cpp` (231/231 CRLF),
`tests/integration/species/test_species_solver_coupling.cpp` (325/325),
`tests/unit/discretization/test_ale_operators.cpp` (495/495).

**So the repository has no line-ending guard at all.** Not a weak one — none. That is the real
defect, and it is why a claim about line endings could stand uncontradicted for two commits.

## 4. What this does NOT change

```text
UNCHANGED   converting step 9's four new files to LF was correct -- but NOT for the reason I gave.
            The reason is not "the repo is pure LF" (false). It is that LF is the convention in
            739 of the 742 source files the §7 gate considers, and that §11.2 requires
            `git diff --check` clean, which CRLF defeats. Same action, sound reason.
UNCHANGED   those four were `staged=A`, absent from HEAD. Choosing their bytes is not rewriting
            history.
UNCHANGED   step 9's 387 staged files are now pure LF by byte count, and §11.2 reports 0 check
            lines in source.
UNCHANGED   TD-034's clang-format result. 742 files, exit 0. That gate is about formatting and it
            passed; this finding is that it is SILENT on line endings, not that it was wrong.
```

## 5. TD-041 returns to OPEN, with a corrected premise

Its original claim was *"CRLF grew"*. That claim's INSTRUMENT was broken, so the claim was never
measured — and my refutation replaced it with a false measurement rather than a real one.

```text
what is now MEASURED      102 text files in HEAD contain CRLF, 21 621 lines.
what is still NOT         whether that count GREW, and when. Nothing here measures history.
                          "Grew" needs a per-commit measurement that no probe has run.
the defect worth fixing   the repository cannot detect line endings, §3. That is actionable and
                          independent of whether the count grew.
```

## 6. What is NOT established

```text
NOT established  that the 102 are a defect rather than a convention. 91 of them are under
                 `results/` -- raw captures from Windows tools, which this campaign commits
                 VERBATIM by established precedent. On that reading only the 11 under `tests/`
                 are candidates, and only the 3 .cpp files are under the format gate.
NOT established  that the count grew, shrank or held. §5.
NOT measured     the 50 `.csv` files' consumers. If a fixture parser depends on those bytes,
                 converting them would be a behaviour change, not a cleanup.
NOT done         any conversion of tracked files. Nothing in HEAD was touched. §2 forbids fixing
                 an out-of-scope defect inside another item's commit, and this is one.
NOT re-run       the 137-file figure is superseded by 102 in this same file; the raw byte count
                 that produced 137 is correct for what it measured and wrong as a file count.
```

## 7. The one-line reproduction

```bash
git ls-files -z | python3 -c "import sys,io,os; n=[x for x in sys.stdin.buffer.read().split(b'\0') if x]; print(sum(1 for p in [q.decode('utf-8','surrogateescape') for q in n] if os.path.isfile(p) and b'\x00' not in io.open(p,'rb').read(8192) and io.open(p,'rb').read().count(b'\r\n')))"
```
