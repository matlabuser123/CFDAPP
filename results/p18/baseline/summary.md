# P18.0 baseline — summary (§3, §4)

```text
P18_START_SOURCE_FINGERPRINT = 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318
                               identical to P17's final fingerprint -- the tree P18 audits is
                               exactly the tree P17 qualified.
VERDICT: all three configurations reproduce the P17.11 measured baseline EXACTLY.
         Every count matches. The only failures are the 4 inherited Debug+GUI timeouts.
```

| Configuration | Selected | Passed | Failed | Skipped | Disabled | Runtime | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| Release+CUDA | 2763 | 2712 | 0 | 2 | 49 | 2226 s | **matches baseline** |
| CPU-only | 2540 | 2493 | 0 | 0 | 47 | 1675 s | **matches baseline** |
| Debug+GUI | 2825 | 2770 | **4** | 2 | 49 | 8514 s | **matches baseline**, incl. its 4 inherited timeouts |

```text
EVERY COUNT MATCHES P17.11, ACROSS ALL THREE CONFIGURATIONS. Runtimes differ by -3.9% to +9.7%,
which is ordinary machine load on multi-thousand-second solves.

CONFIGURATION WAS ASSERTED, NOT ASSUMED, for each: build type, GUI, CUDA and OpenMP are checked
against expected values AFTER configure, failing closed on a mismatch. This is P17.11's fix to a
harness that previously printed the cache BEFORE configuring and silently ran the wrong build type.

DISABLED TESTS ARE COUNTED, 49 / 47 / 49. The inherited harness reported "Not Run : 0" in every P17
phase while these were not running. P18 inherits the fixed harness.
```

## The 4 Debug+GUI failures

```text
  DriverSeegmiller.MeshSequence · .CorrectedSequenceIsDeterministic
  DriverSeegmillerInlet.MappedInletPreservesTheMassFlow · .MediumGridAB

All timeouts at the frozen 7200 s; identical set to P17.11's, test id for test id. Pre-P17 tests
that pass in both Release configurations of this same run. Recorded as INHERITED BASELINE
BEHAVIOUR per §3, not as a P18 regression. The timeout was not raised.
```

## Library preservation

```text
release_cuda  src/libcfdcore.a  188e50397ed13718f449f369ef996768cc4011ac1048955dc312da92716390f6
cpu_only      src/libcfdcore.a  1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a

Both IDENTICAL to the archives P17.8, P17.9 and P17.11 recorded. P17.11 established that the CPU-only
archive is reproducible from a from-scratch rebuild, so a change here would be real evidence of a
source change rather than build noise.
```

## Species, specifically

```text
species rows passed: 76 in Release+CUDA, 76 in CPU-only -- the same count.
  This is the measurement behind gpu_inventory.md: the species path has no CUDA dependency because
  it has no GPU implementation at all. Every species test runs identically with CUDA OFF.
```

## Baseline frozen

```text
This baseline is frozen BEFORE any P18 implementation, as §4 requires. Any later P18 phase that
changes these counts is changing something, and G1 gates exactly that.
```
