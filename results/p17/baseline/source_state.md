# P17.0 — the frozen P17 starting state

Recorded 2026-09-26T04:03Z, before any P17.0 build or test ran. Nothing was
committed, pushed, reset, cleaned, stashed or checked out. The dirty working tree
is preserved exactly as found.

---

## 1. Repository

```text
HEAD           3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
HEAD short     3bd32f918656
HEAD subject   feat(gpu): complete resident CUDA SIMPLE pipeline
branch         main
dirty          YES — deliberately
submodules     NONE (no .gitmodules)
stash          empty
```

```text
tracked modified                   167
untracked (porcelain entries)      107
tracked engineering files changed  116     <- what V2 hashes
untracked engineering+output files 122     <- what V2 hashes
```

P13, P14, P15, P16 **and** P17.2 are all uncommitted. `3bd32f918656-modified` is the
honest build identity; the fingerprints below are the qualification provenance. They
are different things and are not conflated.

Raw: `logs/01_git_state.log`.

### Movement since P13's G10 closeout, attributed

```text
                                    G10 (P13)   P17.0 freeze   delta
tracked modified                       163          167         +4
tracked engineering files changed      112          116         +4
untracked engineering+output files     122          122          0
untracked porcelain entries            106          107         +1
```

The **+4 tracked** files are exactly P17.2's authorized delta, and nothing else:

```text
include/cfd/compressible/EquationOfState.hpp    +24  -0
include/cfd/compressible/IdealGasEOS.hpp         +5  -0
src/compressible/IdealGasEOS.cpp                +13  -0
tests/unit/compressible/test_ideal_gas_eos.cpp +123  -0
                                    4 files, 165 insertions, 0 deletions
```

The **+1 untracked porcelain entry** is `results/p17/` itself, which is why the
engineering+output file count is unchanged at 122: `git status --porcelain` applies
no exclusions, whereas V2 excludes `results/**`.

Raw: `logs/02_p17_2_delta.log`.

## 2. The two fingerprints

### SOURCE_FINGERPRINT_V2 — the P13-comparable instrument, preserved verbatim

Method: `results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh`,
unmodified. Excludes exactly `results/**` and `TODO.md`.

```text
P13 G4..G10 qualified      8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
P13 final / P17 baseline   721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955
post-P17.2, as recorded    fb69dc2a9e045f939df71243a01cea931cb7ce29a64e35b2171bc95c9cd284af
P17.0 freeze, measured     fbf786656eac2284956a4e29fe2745157a3e59ab339833a9f49ff6e048bd9fd6
```

Full record with components: `source_fingerprint.txt`.

### P17_CODE_FINGERPRINT — documentation-invariant, new at P17.0

```text
P17_CODE_FINGERPRINT       1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
tracked code files changed 114     (V2's 116, minus ROADMAP.md and docs/user_guide/cli.md)
```

Full record with its exact file-selection algorithm: `code_fingerprint.txt`.

## 3. V2 moved during this freeze, for a documentation reason — FINDING

This is recorded as a finding rather than smoothed over, because it changes what the
instrument can be used for.

```text
~03:50Z  V2 independently recomputed = fb69dc2a…   MATCHES P17.2's recorded value
 03:58:06Z  ROADMAP.md rewritten, 624 insertions / 257 deletions, by work
            outside P17.0 — a concurrent session or a hand edit
 04:03:16Z  V2 recomputed = fbf78665…              MOVED
```

**Attribution, proved rather than assumed:**

```text
untracked component     069c034ce321… BEFORE and AFTER — byte-identical, unmoved
tracked component       ef7c2767… -> b3c2ffe1…      the sole mover
documentation files inside V2's tracked component:
                        ROADMAP.md, docs/user_guide/cli.md   (2 of 116)
only tracked file with mtime > 03:44Z:
                        ROADMAP.md   at 03:58:06Z
src/compressible/IdealGasEOS.cpp sha256
  recorded by P17.2's controls  aba335fe053620482e8fdb0b294226ca5742bb97bf7dae3bde8cc436a027f8d0
  measured at this freeze       aba335fe053620482e8fdb0b294226ca5742bb97bf7dae3bde8cc436a027f8d0
                                MATCH — the P17.2 source is untouched
```

**No source changed. A documentation file changed, and V2 could not tell the
difference.** V2 excludes only `results/**` and `TODO.md`, so `ROADMAP.md`,
`CLAUDE.md`, `README.md` and `docs/**` are all inside it.

### Classification, and what was NOT done

Under `CLAUDE.md` §5 this is a **contaminated instrument**, not an obsolete
constant: the property V2 is meant to detect — "did any code change?" — still holds
perfectly; the instrument merely mixes documentation into the same hash.

```text
NOT DONE   redefining V2 to exclude documentation so the numbers would match
NOT DONE   excluding additional files to obtain a match
NOT DONE   reverting, stashing or touching another session's ROADMAP.md edit
NOT DONE   restating fb69dc2a as the current value
DONE       V2 preserved verbatim, its new value recorded as measured
DONE       an additional, narrower, separately named instrument added
DONE       the move attributed to one documentation file, with proof
```

`P17_CODE_FINGERPRINT` and `SOURCE_FINGERPRINT_V2` hash **different file sets** and
must never be compared numerically with one another — the same warning
`fingerprint_v2.sh` carries about V1 versus V2.

### Consequence for the rest of P17

Later P17 items must claim "no code changed" against
**P17_CODE_FINGERPRINT**, and report V2 alongside for continuity with P13. Using V2
alone would make any future documentation edit look like a source regression, and
would make a genuine source change harder to see inside doc noise.

## 4. Concurrent work in this repository — ACTIVE

`ROADMAP.md` is being rewritten by something outside P17.0 and was still only
minutes old at freeze time. Per `CLAUDE.md` §2, files another session is changing
must not be edited.

```text
ROADMAP.md      DO NOT EDIT during P17.0 — in flight elsewhere
TODO.md         P17.0 updates its own P17.0 checkboxes only (§13 of the authorization)
results/p17/**  P17.0's own evidence root
```

P17.0 therefore updates `TODO.md` and `results/p17/**` and **leaves `ROADMAP.md`
alone**, including the `ROADMAP.md` staleness previously reported — that is now
somebody else's in-flight work, not P17.0's to fix.
