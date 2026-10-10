# TD-034 — formatting is CLEAN. A defect I "found" alongside it was REFUTED by its own follow-up.

```text
opened        2026-10-07 (format not clean on this tree, recorded as a PUSH BLOCKER in TODO.md)
measured      2026-10-09
status        TD-034 RESOLVED. clang-format at CI settings: 741 files, exit 0, 0 warnings.
              TD-041, opened during this audit, is N/A AS A DEFECT -- refuted in §3 below.
scope         The §11 pre-commit audit for the evidence/docs commit. No production change.
evidence      logs/01_clang_format.log · logs/02_crlf_census.log
```

---

## 1. TD-034 itself: the blocker was stale, and the property holds

`TODO.md` §10 carried `PUSH BLOCKER — TD-034 — clang-format is not clean on this tree`.
Re-measured at CI settings (`CLAUDE.md` §7):

```text
clang-format-18 --dry-run --Werror  over include/ src/ apps/ tests/
  version            Ubuntu clang-format version 18.1.8
  files considered   741
  exit code          0
  offending files    0
  warning lines      0
```

```text
git diff --check  (unstaged)  0 lines
git diff --cached --check     0 lines
```

**TD-034 is RESOLVED.** This is the only claim in this document that survived measurement.

## 2. The claim I went looking for, and the wrong reading I got

While closing TD-034 I had recorded that *"three fully-CRLF `CMakeLists.txt` files were converted
to LF"*. Auditing that, I counted CR bytes in `git show HEAD:<file>` against the working tree for
every modified `CMakeLists.txt` and found **every one had MORE**:

```text
CMakeLists.txt            100 -> 138    cuda/CMakeLists.txt       100 -> 206
src/CMakeLists.txt        411 -> 508    tests/CMakeLists.txt      170 -> 299
tests/unit/gpu/...         15 ->  90    ... 21 files, every one up, none down
```

I read that as CRLF *spreading* against `.gitattributes` (`* -text`, *"no end-of-line conversion,
in either direction"*), opened **TD-041** for it, and wrote it into the register as a P1 commit
blocker.

## 3. THE REFUTATION — the probe could not measure what I asked of it

The follow-up compared each CR count against the file's **total line count**, on both sides:

```text
                                       HEAD CR/lines     TREE CR/lines
CMakeLists.txt                             100/100           138/138
cuda/CMakeLists.txt                        100/100           206/206
src/CMakeLists.txt                         411/411           508/508
tests/CMakeLists.txt                       170/170           299/299

total modified CMakeLists.txt   21
fully CRLF in HEAD              21
fully CRLF in the tree          21
```

**21 of 21 fully CRLF before, 21 of 21 fully CRLF after.** The line endings did not change at all.
The CR count rose because **the files grew** — lines were added for the P18 species wiring — and
every added line followed the convention already in the file.

```text
TD-041 is N/A AS A DEFECT. There is no CRLF growth. There is file growth.
```

### 3.1 Why the first probe was incapable, stated plainly

An **absolute CR count cannot distinguish "the endings flipped" from "the file got longer."** Both
raise it. The quantity that answers the question is the **ratio** CR/lines, and I did not compute
it until after I had already written the finding into the register. The instrument was not broken —
it measured CR bytes correctly. It was *the wrong instrument for the hypothesis*, and the number it
returned was consistent with the defect I expected, which is why I believed it.

This is the fail-open pattern a seventh time, in a new shape: not a probe that printed success
regardless, but a probe whose output was **ambiguous between the defect and the benign
explanation**, read as the defect because that is what I was looking for.

> **§3.2's CLAIM IS FALSE — corrected 2026-10-10, nothing deleted.** `TODO.md` in HEAD has ZERO
> CRLF bytes. "416 of 416" and "366 of 366" were LINE counts matched against themselves, because
> the probe `grep -c $''` was mangled to an EMPTY pattern and matched every line. **The
> repository is pure LF**, in HEAD and in the tree, in every file. So "CRLF is this repository's
> committed convention" is wrong, and so was §2's claim of CRLF growth AND §3's refutation of it
> (a ratio of a number to itself). TD-041's conclusion survives — there is no CRLF defect — but
> neither its claim nor its refutation measured anything. Full correction: `correction_lf.md`.

### 3.2 The thing that should have stopped me immediately

`TODO.md` and `ROADMAP.md` are **fully CRLF in HEAD** — 416 of 416 lines and 366 of 366. So is every
one of the 63 staged evidence `.md` files. **CRLF is this repository's committed convention**, which
`* -text` exists to preserve byte-for-byte rather than to forbid. A policy reading under which the
repo's own tracked files are all in violation should have been suspect on its face.

## 4. My ORIGINAL claim is still wrong, in the other direction

Both readings fail against the same table:

```text
"three CMakeLists.txt were converted to LF"   WRONG -- 21 of 21 are fully CRLF in the tree;
                                              no file was converted to LF.
"CRLF has grown in 21 CMakeLists.txt"         WRONG -- 21 of 21 were already fully CRLF in HEAD;
                                              nothing grew but the files.
```

The truth is the dull one: **the `CMakeLists.txt` line endings were never touched.** The real
`git diff --check` improvement from 656 lines to 0 came from the clang-format reformat removing
trailing whitespace, not from any end-of-line work. I should not have claimed EOL work I had not
done, and the audit that caught it then overcorrected into a defect that was not there.

## 5. What this establishes, and what it does not

```text
ESTABLISHED      clang-format at CI settings is clean: 741 files, exit 0, 0 warnings. TD-034 closed.
ESTABLISHED      git diff --check is clean, staged and unstaged, 0 lines each.
ESTABLISHED      all 21 modified CMakeLists.txt are fully CRLF in HEAD and fully CRLF in the tree;
                 the ratio is 1.00 on both sides and no file's endings changed.
ESTABLISHED      CRLF is the committed convention: TODO.md 416/416 and ROADMAP.md 366/366 in HEAD,
                 and all 63 staged evidence .md files contain CR.
REFUTED          TD-041, by §3. Marked N/A as a defect in the register, not deleted.
REFUTED          my earlier claim that three CMakeLists.txt were converted to LF, by §4.
NOT established  that no file ANYWHERE in the tree has had its endings flipped. The census covers
                 the 21 modified CMakeLists.txt only, because that is where my claim was made.
                 The 146 untracked .cpp/.hpp/.cu/.h files under include/ src/ apps/ tests/ -- the
                 P18 species subsystem among them -- are not censused.
NOT established  that `git diff --check` would catch an EOL flip if one occurred. It did not flag
                 these fully-CRLF files, so it is not evidence either way, and a commit that
                 genuinely flipped endings would need a ratio probe to detect it.
```

**The last line is the one worth keeping.** `--check` being clean is not an EOL guarantee; it never
was. The CR/lines ratio is the instrument, and it costs one command.
