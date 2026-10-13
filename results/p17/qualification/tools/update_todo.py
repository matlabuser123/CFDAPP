#!/usr/bin/env python3
"""P17.11 section 47 -- update TODO.md's P17.11 block with the actual qualification outcome.

Written as a FILE rather than an inline heredoc: the block contains backticks, and passing it
through `bash -c "..."` executed them as command substitution.
"""
import io

OLD = """## P17.11 — P17 Regression & Qualification

* [ ] Full Release+CUDA regression
* [ ] Full Debug+GUI regression
* [ ] Full CPU-only regression
* [ ] Existing SIMPLE fingerprints preserved where expected
* [ ] P14 generic transport preserved
* [ ] P15 thermal / energy preserved
* [ ] P16 turbulence preserved
* [ ] Compressible canonical suite PASS
* [ ] Shock suite PASS
* [ ] CPU/GPU equivalence PASS
* [ ] Sanitizer coverage recorded
* [ ] Determinism evidence recorded
* [ ] Performance measurements recorded separately
* [ ] Known limitations documented
* [ ] Final P17 source fingerprint recorded
* [ ] P17 qualification report completed"""

NEW = """## P17.11 — P17 Regression & Qualification  [PARTIAL]

Report: `results/p17/FINAL_QUALIFICATION.md`.
Final fingerprint `60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318`, unchanged
across the whole phase — P17.11 qualified the tree rather than changing it.

* [x] Full Release+CUDA regression — 2763 selected, 2712 passed, 0 failed, 2 skipped, 49 disabled
* [~] Full Debug+GUI regression — 2825 selected, 2770 passed, **4 timeouts**, 49 disabled. First run
      in any P17 phase. All four are pre-P17 `DriverSeegmiller*` cases that pass in both Release
      configurations; Debug is >3.8x-6.0x slower than the frozen 7200 s limit was set for. The
      timeout was NOT raised. See `results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md`.
* [x] Full CPU-only regression — 2540 selected, 2493 passed, 0 failed, 47 disabled
* [x] Existing SIMPLE fingerprints preserved — P13-001 **BITWISE IDENTICAL**, empty diff, and the
      instrument demonstrated able to fail (planted defect gave 112 differing lines)
* [x] P14 generic transport preserved — 40 rows (20 CPU-only)
* [x] P15 thermal / energy preserved — 422 rows (426 Debug+GUI)
* [x] P16 turbulence preserved — 644 rows (550 CPU-only); deferred P20 SST scope not reopened
* [x] Compressible canonical suite PASS — 336 focused tests, 334 passed, 2 skipped by design
* [x] Shock suite PASS — 13 tests, every wave position inside its frozen bound
* [x] CPU/GPU equivalence PASS — BITWISE at every level; 8 negative controls all fire
* [x] Sanitizer coverage recorded — CUDA memcheck/racecheck/initcheck/synccheck all 0 with live
      controls; host ASan+UBSan rerun on the final tree, 315 tests, 0 findings. Scope limits stated.
* [x] Determinism evidence recorded — CPU and GPU bitwise; GPU also across processes and block sizes
* [x] Performance measurements recorded separately — `results/p17/performance/`, no gate applied;
      the GPU is honestly slower on the smallest case (0.45x)
* [x] Known limitations documented — 37 entries in `results/p17/KNOWN_LIMITATIONS.md`
* [x] Final P17 source fingerprint recorded
* [x] P17 qualification report completed

### Two gates are NOT closed

```text
G6  PARTIAL  HLLC star-density positivity fails for admissible head-on input above a wall-normal
             Mach of ~1.76. A REAL numerical defect, open. No amendment proposed: the criterion is
             correct and the implementation does not meet it.
             results/p17/qualification/G6_POSITIVITY_FAILURE.md
G10 PARTIAL  Debug+GUI has 4 timeouts. A configuration limitation, not a numerical one.
             results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md
```

**P17 OVERALL: PARTIAL.** Implementation is complete; qualification is not. The compressible
capability is qualified and usable within its documented limits, and two specific, characterized
items stand between it and an unqualified PASS."""


def main():
    path = "TODO.md"
    with io.open(path, "rb") as fh:
        raw = fh.read()
    # TODO.md is CRLF (1182 CRLF, 0 bare LF). Match and write in the file's OWN line ending rather
    # than normalising it -- rewriting 1182 line endings would show up as a whole-file diff and
    # bury the one block that actually changed.
    crlf = raw.count(b"\r\n") > 0
    text = raw.decode("utf-8")
    old = OLD.replace("\n", "\r\n") if crlf else OLD
    new = NEW.replace("\n", "\r\n") if crlf else NEW
    if text.count(old) != 1:
        raise SystemExit("expected exactly one P17.11 block, found %d (crlf=%s)"
                         % (text.count(old), crlf))
    with io.open(path, "wb") as fh:
        fh.write(text.replace(old, new).encode("utf-8"))
    print("TODO.md updated (crlf=%s)" % crlf)


if __name__ == "__main__":
    main()
