# results/p17 — evidence index

P17 evidence lives in two trees, for a reason worth stating once so a future reader
does not assume one supersedes the other.

```text
results/p17/
├── README.md                  <- this file
├── P17_0_SUMMARY.md           P17.0's own summary and verdict
│
├── baseline/                  P17.0 -- the frozen starting state and the baseline
│   │                          regressions
│   ├── source_state.md          HEAD, working tree, both fingerprints, and the
│   │                            documentation-move finding
│   ├── source_fingerprint.txt   SOURCE_FINGERPRINT_V2, components, reference values
│   ├── code_fingerprint.txt     P17_CODE_FINGERPRINT + its exact file-selection
│   │                            algorithm and why it exists
│   ├── toolchain.md             compilers, CUDA, GPU, the three qualified build trees
│   ├── baseline_gate.md         the FROZEN gate -- predicted counts, written before
│   │                            any run
│   ├── cuda_regression.md       Release+CUDA verdict
│   ├── cpu_regression.md        CPU-only verdict
│   ├── logs/                    raw logs, including PRESERVED_invalid_*/ runs
│   └── tools/                   the harnesses, preserved as run
│
├── audit/                     P17.0 -- the capability audit
│   ├── compressible_inventory.md  files, traced production path, callers, limits
│   ├── test_inventory.md          two exactly-defined test counts, criteria
│   ├── capability_matrix.md       the 12 authoritative classifications
│   ├── p14_reuse_map.md           generic transport: what P17 may and may not reuse
│   ├── p15_reuse_map.md           thermal/energy: property support vs conservation
│   ├── implementation_gaps.md     the 24 questions, plus cross-cutting blockers
│   └── audit_verification.md      the audit checking itself, and its corrections
│
├── gates/
│   └── P17_GATES.md           G1-G10 definitions. DEFINITIONS, not results.
│
└── compressible/              per-item evidence, by topic -- mirrors results/p16/
    ├── audit/AUDIT.md           the earlier, shorter Phase-0 note. PRESERVED.
    ├── eos/                     P17.2, CLOSED and PASS
    ├── controls/                P17.2's non-vacuity controls
    └── variable_density/ continuity/ momentum/ energy/ hllc/ low_mach/
        shock_validation/ preservation/ final_gate/     empty -- not yet started
```

## Why two trees

`compressible/` follows this project's established per-topic convention (it mirrors
`results/p16/turbulence/`) and holds **per-item** evidence: P17.2's gate, logs, tools
and controls live there.

`baseline/`, `audit/` and `gates/` are the paths the P17.0 authorization named
explicitly. P17.0 is not a per-item gate — it is the phase-wide starting point that
every later item measures against — so it sits at the top level rather than under one
topic.

`compressible/audit/AUDIT.md` is the earlier, shorter Phase-0 note. It is **preserved
unchanged**. `audit/compressible_inventory.md` extends it, and that file's §7 records
the five places the two differ and why the deeper measurement supersedes the earlier
aggregate. Nothing was overwritten or deleted.

## Reading order

```text
1. P17_0_SUMMARY.md              the verdict and the one recommended next step
2. baseline/source_state.md      what state P17 starts from, and what moved during
                                 the freeze
3. audit/capability_matrix.md    what exists, strictly classified
4. audit/implementation_gaps.md  what P17 actually still has to build
5. gates/P17_GATES.md            how each of it will be judged
```

## Two things a future reader must not do

```text
1. Do NOT compare SOURCE_FINGERPRINT_V2 with P17_CODE_FINGERPRINT numerically.
   They hash DIFFERENT FILE SETS. V2 includes documentation (ROADMAP.md, CLAUDE.md,
   README.md, docs/**); CODE excludes it. This is the same warning
   fingerprint_v2.sh carries about V1 versus V2.

2. Do NOT mix the narrow P14/P15/P16 preservation regexes with a broad keyword sweep.
   The narrow, established instrument gives 68 / 161 / 315. A broad sweep gives
   158 / 250 / 473. Quoting one and measuring with the other would show phantom
   test gains. See audit/test_inventory.md §3.
```
