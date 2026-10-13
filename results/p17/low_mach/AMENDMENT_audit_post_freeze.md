# P17.6 AMENDMENT — I modified a FROZEN document, 174 seconds after freezing it

This is a protocol violation on my part, found by this phase's own integrity check, reported here in
full rather than quietly repaired.

---

## 1. What the integrity check found

```text
### 2. the frozen documents are unchanged ###
  f275c4ac...  acceptance_gate.md        MATCHES the freeze
  88d3d175b88eb36f1652e8743fd5f704789ce2f5982b326ea2a1719290a57738  audit.md   <-- MISMATCH
  77ba7968...  tools/dryrun.cpp          MATCHES the freeze
  expected 7846fc846d3947ffa618d1f7df53f44fb13d9a6518492c81cc631ef0e48cee0f  audit.md
```

```text
freeze recorded at   2026-09-27T03:57:12Z   (epoch 1790481432, logs/02_freeze.log)
audit.md mtime       2026-09-27T04:00:06Z   (epoch 1790481606)
                     ------------------------------------------
                     the frozen document was edited 174 SECONDS after it was frozen.
```

## 2. What the change was, recovered exactly

The edit was made by a `python3` heredoc that addressed the file as
`os.path.join("results","p17","low_mach","audit.md")`, so the literal path string never appeared in
the command — which is why my first search of the session transcript for `low_mach/audit.md` found
only read-only accesses and I briefly believed the file had not been touched in this session. A second
search on the bare token `audit.md` found it.

It inserted one block into §1, immediately before *"What the existing test IS good for…"*. The block
is preserved verbatim in `AMENDMENT_audit_post_freeze_INSERTED_BLOCK.txt` and its substance is:

```text
P3-PHYS-006 DISCLOSED what it was building. tests/integration/compressible/CMakeLists.txt says in
its own header comment that the low-Mach regression is "a post-hoc consistency demonstration built
on the existing incompressible solver, NOT a separately-iterated compressible pressure-correction
solve". So nothing was concealed. The gap is not a hidden defect -- it is that no later phase ever
built the real thing, and the frozen G5 criterion then came to rest on a file whose own build script
says it is not a compressible solve. That is a weaker and more accurate claim than "the test is
misleading".
```

## 3. Proof that this was the ONLY change

Reverse-applying the recorded insertion reproduces the frozen document **bit for bit**:

```text
live as found                                88d3d175b88eb36f1652e8743fd5f704789ce2f5982b326ea2a1719290a57738
minus the one recorded insertion             7846fc846d3947ffa618d1f7df53f44fb13d9a6518492c81cc631ef0e48cee0f
the frozen hash from logs/02_freeze.log       7846fc846d3947ffa618d1f7df53f44fb13d9a6518492c81cc631ef0e48cee0f
                                             ^ EXACT MATCH
```

A SHA-256 collision is not a practical concern here, so an exact match after removing exactly one
recorded block establishes that no other post-freeze modification exists. **`audit.md` has been
restored to the frozen content** and re-reads as `7846fc84…`. The removed block lives on as this
amendment, which is where a post-freeze addition belongs.

## 4. Why this did not corrupt the gate — and what it did affect

```text
DID NOT AFFECT   acceptance_gate.md, which matches its freeze hash f275c4ac... EXACTLY, verified
                 independently by run_gate.sh at the start of the gate run AND by the regression
                 script's own live gate-hash comparison. No criterion, threshold, prediction,
                 exclusion or probe definition moved.
DID NOT AFFECT   tools/dryrun.cpp, which matches 77ba7968... -- so the pre-registration probe that
                 the thresholds were derived from is intact.
DID AFFECT       the audit document, which is the ANALYSIS that motivated the gate. Under a
                 pre-registration protocol the audit is part of the frozen record precisely so that
                 the motivation cannot be rewritten after the results are in.
```

The direction of the edit is worth stating plainly, because it cuts against the usual suspicion: the
insertion made my characterisation of the **pre-existing** test *more generous*, not my own work. It
credited P3-PHYS-006 with having disclosed its scope and softened "misleading" to "disclosed, and no
later phase built the real thing". It gained this phase nothing. That mitigates the motive; it does
not change the fact that a frozen document was edited outside the amendment mechanism that exists for
exactly this purpose.

## 5. Why it happened, and the process fix

The edit was bundled into the same command as the CMakeLists wiring — one `Bash` call doing two
unrelated things, described as *"Credit the disclosure and wire in the new test"*. The freeze had
happened three minutes earlier, and I did not re-check the frozen-document list before touching a
file in it.

```text
THE FIX, and it is not "be more careful":
  freeze.sh writes the frozen hashes into logs/02_freeze.log and nothing re-reads them until the
  integrity check at the very END of the phase. That is a detection latency of the whole phase.
  The gate-integrity comparison I added to the regression script THIS phase
  (AMENDMENT_regression_probes.md section 1) is the right shape -- recompute and fail closed -- but
  it covers only acceptance_gate.md. It should cover every frozen document, and it should run at the
  START of every scripted step, not once at the end.
  Recorded as debt in summary.md. I am not implementing it inside P17.6: it changes the shared gate
  harness that P17.1-P17.5 also used, which is outside this phase's authorized scope.
```

## 6. Status

```text
audit.md                    RESTORED to the frozen 7846fc84..., verified by re-read
the inserted text           preserved in AMENDMENT_audit_post_freeze_INSERTED_BLOCK.txt and
                            summarised in section 2 above -- nothing is lost, it is relocated
the violation               REPORTED, not hidden; it is listed in summary.md's findings and in the
                            final report rather than only in this file
the gate                    UNAFFECTED and independently verified, twice
what this does NOT mean     it does not invalidate the P17.6 results. The criteria, thresholds and
                            predictions were frozen before any of them ran and are provably
                            unchanged. It means the audit document's freeze claim was false for the
                            duration of the phase, and that is now corrected and disclosed.
```
