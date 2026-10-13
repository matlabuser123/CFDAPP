# P18.1 — review pass

Findings from reading the final, formatted source. Recorded **during** the regression, when
production source must not change, so each entry says what was done about it and when.

---

## R1 — a comment in `compositionSum()` overclaims the mechanism — FIXED AFTER THE REGRESSION

```text
WHAT IT SAYS   "...and validate() must use this same function so the two can never disagree."
WHAT IS TRUE   validate() -> validateCell() accumulates its OWN sum over the supplied composition
               vector. It does not call compositionSum(), which accumulates over fields_.
IMPACT         NOT a behavioural defect. Both loops run ascending species index, left to right,
               over identical values, so they return the identical double. validate() builds its
               composition vector from fields_ in ascending index order immediately before summing
               it, so the two sequences of additions are the same sequence.
WHY IT MATTERS ANYWAY
               the comment states a GUARANTEE that the code does not structurally provide. A
               maintainer who later rewrites validateCell's accumulation -- to a compensated sum,
               say -- would believe the comment protects them. It does not.
COVERAGE       the equivalence IS exercised, if indirectly: AcceptsASumWithinRoundOff reads
               compositionSum() and asserts the 0.5-eps deviation on a state that validateCell
               accepted under the same tolerance. A disagreement between the two would break it.
DISPOSITION    the comment is corrected to describe what the code actually does. Deferred until
               the regression finished, because changing production source mid-run would mean the
               tested library was not built from the final source (CLAUDE.md section 8).
               The correction is COMMENT-ONLY and is proved so: see section 3.
```

## R2 — "cell 0" in a uniform-constructor diagnostic — NO CHANGE, deliberate

```text
OBSERVED   MassFractionState(registry, cellCount, uniformComposition) validates via
           validateCell(0, uniformComposition), so a rejection reads "...at cell 0" even though
           the caller supplied one composition rather than a per-cell field.
ASSESSED   NOT misleading. The composition passed IS the composition destined for cell 0 (and for
           every other cell), so the message is accurate, if indirect. The offending species, the
           value, the sum and the admissible range -- everything section 15 asks for -- are all
           named correctly.
DISPOSITION NO CHANGE. Rewording it would need a second diagnostic path for no gain in
           diagnosability, and CLAUDE.md prefers the smallest correct change.
```

## R3 — checked and found correct

```text
member init order        registry_ then cellCount_ is consistent with the declaration order
                         (registry_, fields_, cellCount_); fields_ is simply absent from the list.
                         No -Wreorder exposure. Confirmed by a clean build under
                         cfdapp::compiler_warnings.
validateCell before      the uniform constructor calls validateCell(0, ...) while fields_ is still
fields_ exist            empty. validateCell reads only registry_ and the supplied composition.
                         Safe.
fields_[0] indexing      the second constructor indexes fields_[0] only AFTER asserting
                         fields_.size() == registry_.size(), and SpeciesRegistry rejects an empty
                         name list, so registry_.size() >= 1 and fields_ is non-empty. Safe.
uniform validation once  validating the single composition once rather than per cell is correct
                         because every cell receives identical values, and it turns an O(N_cells)
                         check into O(N_species).
default construction     neither type is default-constructible -- both declare a constructor,
                         which suppresses the implicit one. Section 22's "valid by construction"
                         in its strongest form: there is no uninitialised state to document.
copy / move              implicit, from std::vector<ScalarField> and SpeciesRegistry. Section 21
                         says not to add custom operators when the standard semantics suffice.
                         Tested by CopyPreservesIdentityValuesAndOrdering,
                         CopyIsIndependentOfTheOriginal, MovePreservesIdentityAndValues.
std::abs on Real         <cmath> included; std::abs(double) selected. Not ::abs(int).
atomicity of the mutator setCellComposition validates the WHOLE composition before writing any
                         element, so a rejected call leaves the state bitwise unchanged. Asserted
                         by ARejectedMutationLeavesTheStateUnchanged.
```

## 3. Proof that the R1 correction is comment-only

Recorded after the regression completed, so the library that was regressed and the library after
the correction can be compared directly.

```text
APPLIED AFTER THE REGRESSION COMPLETED.   logs/13_r1_comment_only.log

source sha256  before  3f7a6bc7f8576cb0d7791eaf66ace0994e0d7b0dcb7ea8c6ef6df7247e1b700c
               after   429321301b079cd80b26b8a2dcef4929fbf85ece08f28cbe7ff08179cffa1f4f

object sha256  before  e258b1a6c36598d1f4451fce6b85cfb478390c5897e26fa3b02b28152c2dc3af
               after   e258b1a6c36598d1f4451fce6b85cfb478390c5897e26fa3b02b28152c2dc3af
                       BYTE-IDENTICAL

changed lines that are not comments : 0   (checked mechanically, not by eye)
clang-format                        : clean
focused tests after the change      : 97 / 97 passed
```

The proof is the OBJECT hash, not the promise that only a comment was touched. This repository's
host toolchain is bit-reproducible, so an identical object file establishes that the edit emitted
not one different instruction -- and therefore that the three-configuration regression, which ran
against the pre-edit source, remains authoritative for it.

The corrected comment now states what the code does: `validateCell()` accumulates its own sum, the
two agree by construction because `validate()` builds its composition vector from `fields_` in the
same ascending order immediately before summing it, and that agreement is NOT enforced by
`compositionSum()`.


## R4 — a defect I introduced in the control docs, and fixed — PRESERVED

```text
WHAT HAPPENED   TODO.md and ROADMAP.md were edited with a Python heredoc that ran under WINDOWS
                python3. Its text mode reads CRLF/LF to LF and writes LF back as os.linesep --
                CRLF on Windows. Both files were converted WHOLESALE to CRLF.

WHY IT MATTERS  .gitattributes in this repository says `* -text`: "Store every file byte-for-byte:
                no end-of-line conversion, in either direction", precisely because the evidence
                rules hash working-tree files and a converted file would no longer reproduce its
                recorded sha256. A whole-file line-ending flip changes 100% of a file's bytes.

HOW IT SURFACED git diff --check, which P17.9's integrity log had recorded as CLEAN, suddenly
                flagged EVERY line of ROADMAP.md as trailing whitespace. That is the instrument
                working: a CR at end of line is trailing whitespace to git.

FIX             both files converted back to LF with sed under WSL, matching CLAUDE.md, the
                sibling control doc, which is LF. Verified: 0 CR lines in all three.
                Content verified intact afterwards (20 [x] boxes, all P18.1 sections present).

NOT AFFECTED    the P18 source fingerprint EXCLUDES TODO.md and ROADMAP.md by construction (the
                DOCEXC list in freeze_p18_start.sh), so no frozen hash was ever at risk. Both
                frozen gate files were re-hashed at close and are unchanged.

LESSON          in this repository, edit files from WSL, or pass newline='' to preserve bytes.
                A Windows text-mode write is a silent whole-file rewrite.
```
