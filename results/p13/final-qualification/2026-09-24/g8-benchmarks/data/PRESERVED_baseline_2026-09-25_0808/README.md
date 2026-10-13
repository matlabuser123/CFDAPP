# The benchmark baseline as it stood before the G8 control re-run

Copied 2026-09-26 from `results/p13/benchmarks/data/baseline-linux-x64/`.

`controls_rerun.sh` line 20 re-records the committed baseline **in place**:

```bash
bash scripts/benchmarks/run_suite.sh --quick --output-dir $BASE
```

so every invocation of that control overwrites it. This copy exists so the next
invocation does not destroy the current state as the first one destroyed the
original.

## What was lost, and what survived

The ORIGINAL 2026-09-21 baseline files were overwritten by the first G8 control
run (mtimes became 2026-09-25 08:08-08:09). They were not copied first -- my
error, and one I had already flagged as a hazard in G7's report.

Their values survive in `../../logs/G8_compare_normal.log`:

```text
benchmark_20x20   runtime_per_iteration_ms   baseline    1.9442
benchmark_40x40   runtime_per_iteration_ms   baseline    9.0337
benchmark_80x80   runtime_per_iteration_ms   baseline   38.1676
```

and its environment capture was `captured: 2026-09-21T22:47:32Z`, `loadavg 4.04`,
`loaded: no`, 32 cores, `CFDApp 0.2.0-dev`.

The current baseline reads 1.6361 / 7.8336 / 32.4971 -- faster, recorded on the
same host and the same binary family by the same frozen driver. It is a legitimate
re-recording (that is what `controls_rerun.sh` exists to do, post-amendment A1),
but it is not the artifact P13-007 froze.

**Consequence for the record:** the `2.67x / 2.33x / 1.60x` ratios quoted in the
G8-F1 finding were computed against the original baseline. They are preserved in
the log above and are not reproducible from the current files. The finding itself
does not depend on them -- the reproducer now refuses on the build-configuration
mismatch before any ratio is computed.
