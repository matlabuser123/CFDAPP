#!/usr/bin/env python3
"""P17.11 section 48 -- update ROADMAP.md.

§48 says to mark P17 COMPLETE *only if* it qualifies successfully. It did not: G6 and G10 are
PARTIAL. So the phase marker stays on P17 and the status becomes QUALIFIED - PARTIAL, with the two
open items named. P18 is NOT authorized and is not touched.

Line endings are preserved (see update_todo.py for why).
"""
import io

EDITS = [
    (
        """* **P17 — Compressible Flow** — ACTIVE

  * last completed step: **P17.10 — CPU/GPU Equivalence**
  * next step: **P17.11 — Final Qualification** (not authorized)""",
        """* **P17 — Compressible Flow** — ACTIVE, QUALIFICATION **PARTIAL**

  * last completed step: **P17.11 — Final Qualification** (run; result PARTIAL)
  * implementation: COMPLETE (P17.1–P17.10)
  * qualification: 8 of 10 gates PASS. **G6 PARTIAL** (HLLC star-density positivity fails for
    admissible head-on input) and **G10 PARTIAL** (Debug+GUI has 4 timeouts on pre-P17 tests).
  * next step: **none authorized.** Closing G6 needs a wave-speed-estimator change with its own
    frozen gate; closing G10 needs a decision about the Debug timeout contract. Both are outside
    a qualification phase's remit.
  * report: `results/p17/FINAL_QUALIFICATION.md` · limits: `results/p17/KNOWN_LIMITATIONS.md`""",
    ),
    (
        """P17.10 CPU / GPU equivalence            complete   ← the last finished step
  ↓
P17.11 Final qualification              NEXT, not authorized -- and see the note below
```""",
        """P17.10 CPU / GPU equivalence            complete
  ↓
P17.11 Final qualification              RUN -- result PARTIAL   ← the last finished step
```

```text
P17 IS NOT COMPLETE. Its implementation is; its qualification is not. Two gates did not close:

  G6  PARTIAL   a REAL numerical defect. HLLC's wave-speed estimate does not bound a strong
                symmetric head-on collision, so star densities go negative for admissible input
                above a wall-normal Mach of ~1.76. The solver refuses loudly rather than answering
                wrongly; a direct caller of hllcFlux() does not.
                results/p17/qualification/G6_POSITIVITY_FAILURE.md
  G10 PARTIAL   a CONFIGURATION limitation. Four pre-P17 backward-facing-step tests cannot finish
                under Debug inside the frozen 7200 s timeout. They pass in both Release
                configurations and produced no wrong answer, only no answer in time. The timeout
                was not raised.
                results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md

Everything else passed on the final integrated tree, re-measured rather than inherited: SIMPLE
preserved BITWISE, P14/P15/P16 preserved, the compressible canonical and shock suites, CPU/GPU
equivalence bitwise at every level, determinism, and clean CUDA and host sanitizers.
```""",
    ),
    (
        """WHAT P17.11 INHERITS THAT IS NOT A PASS. P17.10 found that Toro's pressure-based wave-speed
estimate does not bound a strong symmetric head-on collision: above a normal Mach number of ~1.76
the wave ordering inverts, the star densities go negative, and HLLC returns the undisturbed
upstream flux while still reporting valid = true. It is in P17.7's flux layer, which P17.10 was not
authorized to change, so it is reported with a closed-form reproducer and left open.
results/p17/cpu_gpu_equivalence/BLOCKING_FINDINGS.md.""",
        """WHAT P17.11 INHERITED THAT WAS NOT A PASS, AND STILL IS NOT. P17.10 found that Toro's
pressure-based wave-speed estimate does not bound a strong symmetric head-on collision: above a
normal Mach number of ~1.76 the wave ordering inverts, the star densities go negative, and HLLC
returns the undisturbed upstream flux while still reporting valid = true.

P17.11 confirmed this maps onto frozen criterion G6(e) -- "rho and p remain strictly positive for
admissible input, a boolean, no tolerance" -- and left G6 at PARTIAL. No amendment was proposed:
the criterion is correct and the implementation does not meet it, so amending it would have been
weakening a valid threshold. In fairness to P17.7, its own summary had already disclaimed universal
signal bounding as unproven; what it over-claimed was one word, describing star-state admissibility
as "proven" when the proof covers only the denominator bound.
results/p17/cpu_gpu_equivalence/BLOCKING_FINDINGS.md ·
results/p17/qualification/G6_POSITIVITY_FAILURE.md.""",
    ),
]


def main():
    path = "ROADMAP.md"
    with io.open(path, "rb") as fh:
        raw = fh.read()
    crlf = raw.count(b"\r\n") > 0
    text = raw.decode("utf-8")
    for old, new in EDITS:
        o = old.replace("\n", "\r\n") if crlf else old
        n = new.replace("\n", "\r\n") if crlf else new
        if text.count(o) != 1:
            raise SystemExit("edit matched %d times, expected 1:\n%s" % (text.count(o), old[:70]))
        text = text.replace(o, n)
    with io.open(path, "wb") as fh:
        fh.write(text.encode("utf-8"))
    print("ROADMAP.md updated (crlf=%s), %d edits" % (crlf, len(EDITS)))


if __name__ == "__main__":
    main()
