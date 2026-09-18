---
name: cfdapp-review
description: Adversarial self-review after implementation and before closeout. Attacks the change and its evidence for vacuity, shared mistakes, hidden fallbacks, stale binaries and weakened thresholds, then classifies findings. Use after every CFDApp implementation.
---

# CFDApp adversarial review

Run after implementation and verification, before `cfdapp-closeout`. The posture is adversarial:
**try to prove your own result wrong.** A review that finds nothing should say what it attacked.

**Scale the review to the change.** For a documentation- or test-only diff, most items below are
inapplicable; answer `n/a (documentation-only)` once for the block and address only those that do
apply — typically 3, 8, 13 and the evidence question in 5. For anything touching `src/`,
`include/` or `cuda/`, answer every item.

## The attack list

Answer each explicitly. "No" needs a reason.

**The claim**
1. What assumption could be wrong? What is the change's weakest link?
2. What case was **not** tested — geometry, scale, sign, aspect ratio, boundary type, grid size?
3. Could the test be **vacuous**? Would it still pass against the unchanged baseline, or against a
   deliberately corrupted operator? If it cannot fail, it proved nothing.
4. Could the implementation and the test **share the same mistake** — same formula, same sign
   convention, same author's misreading? Is there a genuinely independent path?
5. Was production output adopted as a reference value anywhere? (Forbidden — `CLAUDE.md` §4.) And
   in the other direction: was any **recorded** value — a measured number, tolerance, SHA, phase ID
   or `results/` citation, in prose or in evidence — edited toward what someone expected it to be?

**The evidence**
6. Could a **fallback** have hidden the failure — a CPU fallback, a solver fallback, a retry, a
   default? Are the fallback counters actually zero?
7. Could **stale binaries** invalidate this? Was every test binary rebuilt against the exact
   library under test, with hashes recorded (`CLAUDE.md` §8)?
8. Could generated files pollute the diff — `results/validation/**` timing noise, build artifacts,
   CRLF rewrites from a Windows-side edit?
9. Are the counts exact and separated into executed / disabled / skipped?
10. Does the environment named in the evidence match the environment actually used? (WSL2 CUDA
    does not establish native Windows CUDA; GitHub runners have no GPU.)

**The blast radius**
11. Could this change CPU/GPU semantics, or make them diverge?
12. Could this break backward compatibility — case format, schema, export, committed cases?
13. Was unrelated technical debt accidentally changed, "tidied", or reformatted?
14. Was any threshold, tolerance, iteration limit or timeout weakened — anywhere, for any reason?
15. Is a performance claim built on a **valid solve**? Zero-work or failed runs produce fake
    speed-ups (see the rejected 18.8×/22.9× in `results/cuda-qual-001/`). End-to-end, median of
    repeats, spread reported, baseline named?

## Classification

```text
BLOCKER  the result is not trustworthy as it stands; stop and report
MAJOR    a real defect in scope; must be addressed before closeout
MINOR    a real but low-impact issue in scope
DEBT     real, out of scope -> record in TODO.md -> Technical Debt, do not fix here
NONE     attacked and found sound; say what was attacked
```

**Do not automatically repair findings classified DEBT or out of scope.** Report them. Expanding
scope silently is itself a violation (`CLAUDE.md` §2).

A BLOCKER means stop — do not proceed to closeout, do not commit, report the finding as the result.
