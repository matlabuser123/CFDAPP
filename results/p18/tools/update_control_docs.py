#!/usr/bin/env python3
"""P18.0 section 37 -- record the P18.0 outcome in TODO.md and ROADMAP.md.

Written as a FILE and opened in BINARY mode on purpose:
  * both control docs are CRLF on disk, but Git Bash opens files in text mode, so `sed -i` would
    silently rewrite the whole file as LF and produce a whole-file diff instead of a few-line one;
  * the replacement blocks contain backticks and apostrophes, which a `bash -c` heredoc mangles.
Every replacement is asserted to match EXACTLY ONCE, so a stale anchor fails closed.

The anchors are written in ASCII and translated to the documents' em dash / arrow / box-drawing
characters by to_doc(), so a mismatch is a real anchor failure and not a mistyped dash.
"""
import io

# --------------------------------------------------------------------------------------- TODO.md

TODO_OLD = """## P18.0 - Baseline & Audit <- CURRENT

- [ ] Freeze P18 source fingerprint
- [ ] Run CPU / CUDA baseline
- [ ] Audit existing species classes, APIs, tests and production paths
- [ ] Audit species boundary lag
- [ ] Map P14 transport reuse
- [ ] Map P15/P17 thermal and energy coupling
- [ ] Identify implemented / partial / missing capability
- [ ] Freeze P18 verification gates under `results/p18/`

Do not duplicate existing passive-species functionality."""

TODO_NEW = """## P18.0 - Baseline & Audit <- COMPLETE

Report: `results/p18/P18_0_SUMMARY.md`. Gates frozen at `results/p18/gates/P18_GATES.md`
(sha256 `921616544e3631d4...`). `P18_START_SOURCE_FINGERPRINT`
`60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318` is **unchanged at phase end** --
P18.0 audited the tree and modified no production code.

- [x] Freeze P18 source fingerprint
- [x] Run CPU / CUDA baseline -- Release+CUDA 2712 passed / 0 failed, CPU-only 2493 / 0,
      Debug+GUI 2770 / **4 inherited timeouts**. Every count matches the P17.11 baseline exactly.
- [x] Audit existing species classes, APIs, tests and production paths
- [x] Audit species boundary lag -- **CONFIRMED DEFECT**: 335 outer iterations against thermal's 2
      on an identical pure-Neumann problem
- [x] Map P14 transport reuse
- [x] Map P15/P17 thermal and energy coupling
- [x] Identify implemented / partial / missing capability -- 6 IMPLEMENTED / 8 PARTIAL / 10 MISSING
- [x] Freeze P18 verification gates under `results/p18/`

Do not duplicate existing passive-species functionality.

### What the audit found

```text
1. SPECIES IS A SHIPPED PRODUCTION FEATURE -- case file -> parser -> config -> builder ->
   ProjectRunner -> solver -> CSV/VTK/JSON export + run metadata, with two shipped case
   directories and 92 test rows. Not a library experiment. P18 must EXTEND it compatibly.

2. THERE ARE ALREADY TWO SCALAR-TRANSPORT STACKS, and species uses the weaker one. P14's
   ScalarTransport has the rho-weighted transient, per-cell Su/Sp sources and per-cell diffusivity
   that species lacks -- and species references it ZERO times. P18 should collapse the two, not
   add a third.

3. THE BOUNDARY LAG IS REAL, AND MIGRATING TO P14 WOULD NOT FIX IT. P14 carries the same lagged
   gradient treatment (src/transport/ScalarTransport.cpp:129); only thermal was ever made exact.
   Gate G4(c) therefore requires the exact treatment deliberately.

4. THE EQUATION ACTUALLY ASSEMBLED IS   div(rho U Y) - div(rho D grad Y) = S
   -- steady (there is no transient term at all), constant density, one constant D per species,
   one uniform scalar source, passive. sum(Y_i) = 1 and mass-fraction bounds exist NOWHERE.
```

### Frozen-gate dry-run

```text
G1 PASSES. G3 PARTIAL. G10 PASSES for what exists. G11 baseline established.
G2, G5, G6, G7, G8, G9   NOT IMPLEMENTED (expected -- those capabilities do not exist yet)
G4 (a)(b)(d) PASS. G4(c) FAILS: 335 outer iterations against a criterion of <= 2.

G4(c) IS THE ONLY GATE THAT FAILS ON EXISTING, SHIPPED CODE.
```

---

NEXT: **P18.1 - Multi-Species State**
**NOT AUTHORIZED YET**"""

TODO_TREE_OLD = """P18         ACTIVE + AUTHORIZED
 |- P18.0   CURRENT"""

TODO_TREE_NEW = """P18         ACTIVE
 |- P18.0   COMPLETE  (baseline + audit; no production code changed)
 |- P18.1   NOT AUTHORIZED YET"""

# ------------------------------------------------------------------------------------ ROADMAP.md

RM_ACTIVE_OLD = """## Active phase

* **P17 - Compressible Flow** - ACTIVE, QUALIFICATION **PARTIAL**
"""

RM_ACTIVE_NEW = """## Active phase

* **P18 - Species / Reactions** - ACTIVE

  * last completed step: **P18.0 - Baseline & Audit** (COMPLETE)
  * P18.0 changed no production code. The start fingerprint
    `60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318` is unchanged at phase end.
  * baseline reproduces P17.11 exactly in all three configurations: Release+CUDA 2712/0,
    CPU-only 2493/0, Debug+GUI 2770/4 -- the same 4 inherited pre-P17 timeouts, test id for test id.
  * audit headline: the existing species capability is a **shipped, production-reachable passive
    scalar** solving `div(rho U Y) - div(rho D grad Y) = S`, steady, constant density, one constant
    D per species. It is **not** a mass-fraction model: `sum(Y_i) = 1`, bounds, mixture properties,
    reactions and any GPU species path do not exist. 6 IMPLEMENTED / 8 PARTIAL / 10 MISSING.
  * one confirmed defect in shipped code: the species Dirichlet-gradient boundary is **lagged**,
    costing 335 outer iterations where thermal's exact treatment takes 2. P14 carries the same lag,
    so it cannot be fixed by migration alone.
  * verification gates for all of P18 are **frozen** at `results/p18/gates/P18_GATES.md`
    (sha256 `921616544e3631d4...`). G4(c) is the only criterion that fails on existing code.
  * next step: **P18.1 - Multi-Species State. NOT AUTHORIZED YET.**
  * report: `results/p18/P18_0_SUMMARY.md`, audit evidence: `results/p18/audit/`

## Prior phase, closed as PARTIAL

* **P17 - Compressible Flow** - QUALIFICATION **PARTIAL**, not reopened by P18.0
"""

RM_FUTURE_OLD = """## Future phases

* **P18 - Species / Reactions** - NOT AUTHORIZED
* **P19 - Multiphase** - NOT AUTHORIZED"""

RM_FUTURE_NEW = """## Future phases

* **P19 - Multiphase** - NOT AUTHORIZED"""

RM_S12_OLD = """# 12. P18 - Species / Reactions - NOT AUTHORIZED
"""

RM_S12_NEW = """# 12. P18 - Species / Reactions - ACTIVE (P18.0 COMPLETE)
"""

RM_S12_TAIL_OLD = """Existing non-reacting species support must be audited before new implementation begins.

P18 does not begin until explicitly authorized.
"""

RM_S12_TAIL_NEW = """Existing non-reacting species support must be audited before new implementation begins.
**That audit is P18.0, and it is COMPLETE** -- `results/p18/P18_0_SUMMARY.md`.

What the audit established, which constrains the scope above:

* the existing species capability is production-reachable and **shipped** (case -> parser ->
  builder -> ProjectRunner -> export, two case directories). "Passive-scalar preservation" is
  therefore a backward-compatibility obligation on a user-visible feature, not a nice-to-have;
* the equation actually assembled is `div(rho U Y) - div(rho D grad Y) = S`: steady, constant
  density, one constant D per species, one uniform source, passive. There is no transient term to
  reuse;
* "multi-species transport" already exists as N independent scalars driven by a loop. What is
  missing is the **collection**: stable identity, `sum(Y_i) = 1`, and a bounds policy;
* P14's generic transport already supplies the transient, the per-cell `Su`/`Sp` source pair and
  the per-cell diffusivity that P18 needs. P18 should **collapse the two stacks**, not add a third;
* a real defect exists in shipped code: the species gradient boundary is lagged (335 outer
  iterations against thermal's 2). It is separable from the rest of P18;
* there is **no GPU species path at all**, so P18.8 is an implementation phase, as P17.10 was.

P18's verification gates are frozen for the whole phase at `results/p18/gates/P18_GATES.md`.

Each subsequent P18 step still requires its own explicit authorization.
"""

RM_LADDER_OLD = """P17      Compressible Flow                ACTIVE
          |- P17.0 Baseline & Audit       COMPLETE
   |
P18      Species / Reactions              NOT AUTHORIZED"""

RM_LADDER_NEW = """P17      Compressible Flow                PARTIAL (qualified within documented limits)
   |
P18      Species / Reactions              ACTIVE
          |- P18.0 Baseline & Audit       COMPLETE
          |- P18.1 Multi-Species State    NOT AUTHORIZED YET"""

# ---------------------------------------------------------------------------------------- driver

DASHES = [(" - ", " — "), ("<- ", "← "), (" <-", " ←"), (" |- ", " └─ ")]
LADDER = [("\n   |\n", "\n   ↓\n")]


def to_doc(s, subs):
    for a, b in subs:
        s = s.replace(a, b)
    return s


def patch(path, pairs, subs):
    with io.open(path, "rb") as fh:
        raw = fh.read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8")
    for old, new in pairs:
        o = to_doc(old, subs)
        n = to_doc(new, subs)
        if crlf:
            o = o.replace("\n", "\r\n")
            n = n.replace("\n", "\r\n")
        count = text.count(o)
        if count != 1:
            raise SystemExit("%s: anchor matched %d times, expected 1:\n---\n%s\n---"
                             % (path, count, o[:400]))
        text = text.replace(o, n)
    with io.open(path, "wb") as fh:
        fh.write(text.encode("utf-8"))
    print("%s  updated  (crlf=%s, %d block(s))" % (path, crlf, len(pairs)))


def main():
    patch("TODO.md", [(TODO_OLD, TODO_NEW), (TODO_TREE_OLD, TODO_TREE_NEW)], DASHES)
    patch("ROADMAP.md",
          [(RM_ACTIVE_OLD, RM_ACTIVE_NEW), (RM_FUTURE_OLD, RM_FUTURE_NEW),
           (RM_S12_OLD, RM_S12_NEW), (RM_S12_TAIL_OLD, RM_S12_TAIL_NEW),
           (RM_LADDER_OLD, RM_LADDER_NEW)],
          DASHES + LADDER)


if __name__ == "__main__":
    main()
