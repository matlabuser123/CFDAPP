# G10 — pre-closeout state, frozen

Recorded 2026-09-25T04:50:53Z before any G10 build or test ran.

## HEAD and working tree

```text
HEAD          : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
HEAD short    : 3bd32f918656
branch        : main
dirty         : YES
tracked modified                 : 163
untracked                        : 106
tracked engineering files changed: 112
```

The tree is deliberately dirty: P13, P14, P15 and P16 are all uncommitted.
Commit and push are not authorized, so `3bd32f918656-modified` is the honest
build identity and V2 is the qualification provenance. They are different
things and are not conflated (see G5 §6).

## The qualified source fingerprint

Canonical method: `g4-cli-gui/tools/fingerprint_v2.sh`, adopted at G4 when V1
was withdrawn for including `TODO.md`. Excludes exactly `results/**` and
`TODO.md`; nothing else.

```text
  label     : G10_before
  date      : 2026-09-25T04:51:10Z
  HEAD      : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
  definition: SOURCE_FINGERPRINT_V2 (excludes results/** and TODO.md only)
  
  file counts
    tracked engineering files changed : 112
    untracked engineering+output files: 122
      of which solver-output files    : 8
  
  SOURCE_FINGERPRINT_V2 = 8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
  
  components
    tracked diff sha256      : 2f71bb14e75e1b00d9ffd1d761e180e7a9d22b0c69b2c5cdeccfa3406bad8484
    untracked contents sha256: 3e5e17a20baccdc8a5e166d6e69c2f22b81b81668fa5a44f04585e759744675b
  
  attribution breakdown (diagnostic only -- NOT excluded from V2)
    untracked ENGINEERING source : 750012b4b74afcee75dee8291745dbbd0e9e4e5c90bbc7aecd4e1e809bcda377
    untracked SOLVER OUTPUT      : 3bf622aa72e8e0d3b59c21ff7f57e27b2eef6366f73ad6be04e14d2009e45d74
```

### The qualified-state value this must match at closeout

```text
V2 at G4 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 at G5 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 at G6 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 at G7 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 at G9 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

Unmoved across five consecutive gates. That is the qualified state.

## V1, for the record only

```text
V1 (WITHDRAWN at G4)  86346f151e5359e37d23b13fca2cdc7f5549c88900a9f9dc7e77e58973fc8d5f
```

Never compared numerically with a V2 value: they hash different file sets.
Withdrawn because it included `TODO.md`, which qualification itself edits at
every gate closeout, so it could not survive closing a gate. Full derivation:
`g4-cli-gui/AMENDMENT_G4_fingerprint.md`.

## Canonical build configurations for G10

The G1 qualification trees, built clean on 2026-09-24:

```text
build/qual-final CFDAPP_BUILD_GUI:BOOL=OFF CFDAPP_ENABLE_CUDA:BOOL=ON CFDAPP_ENABLE_OPENMP:BOOL=ON CMAKE_BUILD_TYPE:STRING=Release 
build/qual-debug CFDAPP_BUILD_GUI:BOOL=ON CFDAPP_ENABLE_CUDA:BOOL=ON CFDAPP_ENABLE_OPENMP:BOOL=OFF CMAKE_BUILD_TYPE:STRING=Debug 
build/qual-cpuonly CFDAPP_BUILD_GUI:BOOL=OFF CFDAPP_ENABLE_CUDA:BOOL=OFF CFDAPP_ENABLE_OPENMP:BOOL=ON CMAKE_BUILD_TYPE:STRING=Release 
```

`build/final` is NOT used. It is `CFDAPP_ENABLE_CUDA:BOOL=OFF` with no
`cuda/libcfdcuda.a`, so it cannot be the Release+CUDA configuration. The
frozen `regress_both.sh` iterates `build/cuda` and `build/final`; G10 applies
its exclusions and invocation policy to the three canonical qual trees
instead. Third gate to hit this (G6, G8, G9) -- see the G9 report.

## Frozen invocation policy

```text
ctest -j 8 --timeout 7200 -E <documented exclusions>
```

From `regress_both.sh` and G3's amendment. G3's first attempt used -j16 with
the default 1500 s timeout and produced 250 timeouts and zero assertion
failures; that invalid invocation is preserved at
`2026-09-24/logs/G3_FAILURE_PRESERVED_j16_default_timeout.log`.
